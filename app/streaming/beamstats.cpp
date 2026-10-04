#include "beamstats.h"
#include "beamdraw.h"
#include "session.h"

#include <QFile>
#include <QString>
#include <vector>
#include <string>

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

using namespace BeamDraw;

namespace BeamStats {

namespace {

SDL_atomic_t s_Level;
SDL_SpinLock s_LevelInit;
bool s_LevelLoaded;

SDL_mutex* s_Lock; // guards everything below (fonts are not thread-safe)
Snapshot s_Last;
bool s_HasLast;
char s_Gpu[96];
char s_Cpu[96];
float s_FontScale;
TTF_Font* s_LabelFont;
TTF_Font* s_ValueFont;
TTF_Font* s_BigFont;
TTF_Font* s_SmallFont;

SDL_mutex* lock()
{
    static SDL_SpinLock initLock;
    SDL_AtomicLock(&initLock);
    if (s_Lock == nullptr) {
        s_Lock = SDL_CreateMutex();
    }
    SDL_AtomicUnlock(&initLock);
    return s_Lock;
}

void loadCpuName()
{
    if (s_Cpu[0] != 0) {
        return;
    }
    QFile file("/proc/cpuinfo");
    if (file.open(QIODevice::ReadOnly)) {
        for (const QByteArray& line : file.readAll().split('\n')) {
            if (line.startsWith("model name")) {
                QByteArray name = line.mid(line.indexOf(':') + 1).trimmed();
                SDL_strlcpy(s_Cpu, name.constData(), sizeof(s_Cpu));
                break;
            }
        }
    }
    if (s_Cpu[0] == 0) {
        SDL_strlcpy(s_Cpu, "Unknown", sizeof(s_Cpu));
    }
}

// Fonts are sized for the display scale; reopen when it changes
bool ensureFonts(float scale)
{
    if (s_LabelFont != nullptr && s_FontScale == scale) {
        return true;
    }
    for (TTF_Font** font : { &s_LabelFont, &s_ValueFont, &s_BigFont, &s_SmallFont }) {
        if (*font != nullptr) {
            TTF_CloseFont(*font);
            *font = nullptr;
        }
    }

    // Our own reference keeps TTF alive across sessions while we hold fonts
    static bool ttfInit = false;
    if (!ttfInit) {
        if (TTF_Init() != 0) {
            return false;
        }
        ttfInit = true;
    }

    auto px = [scale](float v) { return (int)(v * scale + 0.5f); };
    s_LabelFont = openFont(px(12));
    s_ValueFont = openFont(px(13), true);
    s_BigFont = openFont(px(24), true);
    s_SmallFont = openFont(px(10.5f));
    s_FontScale = scale;
    return s_LabelFont != nullptr && s_ValueFont != nullptr;
}

int textWidth(TTF_Font* font, const char* text)
{
    int w = 0, h = 0;
    if (font != nullptr && text != nullptr && text[0] != 0) {
        TTF_SizeUTF8(font, text, &w, &h);
    }
    return w;
}

SDL_Color rate(double value, double good, double ok)
{
    return value <= good ? k_Accent : value <= ok ? k_Warning : k_Danger;
}

SDL_Color rateFps(double fps, int target)
{
    if (target <= 0) {
        return k_Text;
    }
    return fps >= target * 0.95 ? k_Accent : fps >= target * 0.8 ? k_Warning : k_Danger;
}

double totalLatency(const Snapshot& s)
{
    return (s.hasHostLatency ? s.hostMs : 0) + (s.hasRtt ? s.rttMs : 0) +
            s.decodeMs + s.queueMs + s.renderMs;
}

std::string fmt(const char* format, ...)
{
    char buffer[128];
    va_list args;
    va_start(args, format);
    SDL_vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return buffer;
}

// ---- Basic: one compact pill -----------------------------------------

struct Segment {
    std::string text;
    TTF_Font* font;
    SDL_Color color;
    int gapAfter;
};

SDL_Surface* renderBasic(const Snapshot& s)
{
    auto px = [&s](float v) { return (int)(v * s.scale + 0.5f); };

    double frametime = s.hasFrametime ? s.frametimeMs : (s.renderedFps > 0 ? 1000.0 / s.renderedFps : 0);
    double latency = totalLatency(s);

    std::vector<Segment> segments = {
        { fmt("%.0f", s.renderedFps), s_ValueFont, rateFps(s.renderedFps, s.targetFps), px(4) },
        { "FPS", s_LabelFont, k_TextDim, px(14) },
        { fmt("%.1f", frametime), s_ValueFont, k_Text, px(4) },
        { "ms", s_LabelFont, k_TextDim, px(14) },
        { fmt("%.1f", latency), s_ValueFont, rate(latency, 12, 20), px(4) },
        { "ms latency", s_LabelFont, k_TextDim, 0 },
    };

    int padX = px(12), height = px(28), margin = px(8);
    int width = 2 * padX;
    for (const Segment& seg : segments) {
        width += textWidth(seg.font, seg.text.c_str()) + seg.gapAfter;
    }

    SDL_Surface* surface = createSurface(width + 2 * margin, height + 2 * margin);
    if (surface == nullptr) {
        return nullptr;
    }
    SDL_Rect pill = { margin, margin, width, height };
    fillRoundedPanel(surface, pill, height / 2.0f, k_PillBackground, 1);

    int x = pill.x + padX;
    for (const Segment& seg : segments) {
        x += drawText(surface, seg.font, seg.text.c_str(), seg.color, x, pill.y, pill.h, -1) + seg.gapAfter;
    }
    return surface;
}

// ---- Standard / Advanced: panel of rows -------------------------------

enum RowKind { RowSection, RowValue, RowNote };

struct Row {
    RowKind kind;
    std::string label;
    std::string value;
    SDL_Color color;
};

SDL_Surface* renderPanel(const Snapshot& s, bool advanced)
{
    auto px = [&s](float v) { return (int)(v * s.scale + 0.5f); };

    double frametime = s.hasFrametime ? s.frametimeMs : (s.renderedFps > 0 ? 1000.0 / s.renderedFps : 0);
    double latency = totalLatency(s);
    double drops = s.networkDropPct + s.jitterDropPct;

    std::vector<Row> rows;
    auto section = [&rows](const char* title) { rows.push_back({ RowSection, title, "", k_Accent }); };
    auto value = [&rows](const std::string& label, const std::string& v, SDL_Color c = k_Text) {
        rows.push_back({ RowValue, label, v, c });
    };
    auto note = [&rows](const std::string& text) { rows.push_back({ RowNote, text, "", k_TextDim }); };

    std::string hostStr = s.hasHostLatency ? fmt("%.1f", s.hostMs) : "–";
    std::string rttStr = s.hasRtt ? fmt("%.0f", s.rttMs) : "–";

    if (!advanced) {
        value("Latency", fmt("%.1f ms", latency), rate(latency, 12, 20));
        note(fmt("host %s · net %s · decode %.1f · render %.1f",
                 hostStr.c_str(), rttStr.c_str(), s.decodeMs, s.queueMs + s.renderMs));
        value("Stream", fmt("%dx%d %s", s.width, s.height, s.codec));
        value("Bitrate", fmt("%.1f Mbps", s.bitrateMbps));
        value("Dropped", fmt("%.2f%%", drops), rate(drops, 0.5, 2));
    }
    else {
        section("FRAMES");
        if (s.hasFrametime) {
            value("Frametime min / max", fmt("%.1f / %.1f ms", s.frametimeMinMs, s.frametimeMaxMs));
            value("Frame jitter", fmt("%.2f ms", s.frametimeJitterMs), rate(s.frametimeJitterMs, 1, 3));
        }
        value("Host › net › decode", fmt("%.0f › %.0f › %.0f", s.hostFps, s.receivedFps, s.decodedFps));
        value("Rendered", fmt("%.1f fps (target %d)", s.renderedFps, s.targetFps),
              rateFps(s.renderedFps, s.targetFps));

        section("LATENCY");
        value("Total (est.)", fmt("%.1f ms", latency), rate(latency, 12, 20));
        if (s.hasHostLatency) {
            value("Host encode", fmt("%.1f ms (%.1f–%.1f)", s.hostMs, s.hostMinMs, s.hostMaxMs),
                  rate(s.hostMs, 6, 10));
        }
        value("Network RTT", s.hasRtt ? fmt("%.0f ms ±%.0f", s.rttMs, s.rttVarianceMs) : "–",
              s.hasRtt ? rate(s.rttMs, 3, 8) : k_TextDim);
        value("Decode", fmt("%.2f ms", s.decodeMs), rate(s.decodeMs, 3, 8));
        value("Queue + render", fmt("%.2f ms", s.queueMs + s.renderMs), rate(s.queueMs + s.renderMs, 3, 8));

        section("NETWORK");
        value("Bitrate", fmt("%.1f Mbps (peak %.1f)", s.bitrateMbps, s.peakBitrateMbps));
        value("Dropped by network", fmt("%.2f%%", s.networkDropPct), rate(s.networkDropPct, 0.5, 2));
        value("Dropped by jitter", fmt("%.2f%%", s.jitterDropPct), rate(s.jitterDropPct, 0.5, 2));

        section("VIDEO");
        value("Stream", fmt("%dx%d %s", s.width, s.height, s.codec));
        value("Display", fmt("%dx%d @ %d Hz", s.displayWidth, s.displayHeight, s.displayHz));
        value("V-Sync", s.vsync ? "On" : "Off");

        section("HARDWARE");
        value("GPU", s_Gpu[0] ? s_Gpu : "Unknown");
        value("Decoder", s.renderer);
        value("CPU", s_Cpu);
        value("Moonlight CPU", fmt("%.1f%%", s.clientCpuPct));
    }

    // Measure
    int padX = px(14), padTop = px(10), padBottom = px(10), margin = px(8);
    int headerH = px(36), rowH = px(21), noteH = px(17), sectionH = px(24), gap = px(16);
    int contentW = 0, contentH = headerH;
    for (const Row& row : rows) {
        if (row.kind == RowSection) {
            contentH += sectionH;
        }
        else if (row.kind == RowNote) {
            contentW = SDL_max(contentW, textWidth(s_SmallFont, row.label.c_str()));
            contentH += noteH;
        }
        else {
            contentW = SDL_max(contentW, textWidth(s_LabelFont, row.label.c_str()) + gap +
                                         textWidth(s_ValueFont, row.value.c_str()));
            contentH += rowH;
        }
    }
    std::string fpsText = fmt("%.0f", s.renderedFps);
    std::string frametimeText = fmt("%.2f ms", frametime);
    int headerW = textWidth(s_BigFont, fpsText.c_str()) + px(6) + textWidth(s_LabelFont, "FPS") + gap +
                  textWidth(s_ValueFont, frametimeText.c_str());
    contentW = SDL_max(contentW, SDL_max(headerW, px(advanced ? 300 : 240)));

    int panelW = contentW + 2 * padX, panelH = contentH + padTop + padBottom;
    SDL_Surface* surface = createSurface(panelW + 2 * margin, panelH + 2 * margin);
    if (surface == nullptr) {
        return nullptr;
    }
    SDL_Rect panel = { margin, margin, panelW, panelH };
    fillRoundedPanel(surface, panel, px(12), k_PanelBackground, 1);

    // Header: big FPS, frametime on the right
    int left = panel.x + padX, right = panel.x + panel.w - padX;
    int y = panel.y + padTop;
    int fpsW = drawText(surface, s_BigFont, fpsText.c_str(), rateFps(s.renderedFps, s.targetFps),
                        left, y, headerH, -1);
    drawText(surface, s_LabelFont, "FPS", k_TextDim, left + fpsW + px(6), y + px(3), headerH, -1);
    drawText(surface, s_ValueFont, frametimeText.c_str(), k_Text, right, y - px(6), headerH, 1);
    drawText(surface, s_SmallFont, "frametime", k_TextDim, right, y + px(10), headerH, 1);
    y += headerH;

    for (const Row& row : rows) {
        if (row.kind == RowSection) {
            SDL_Rect divider = { left, y + px(8), right - left, SDL_max(1, px(1)) };
            SDL_FillRect(surface, &divider, SDL_MapRGBA(surface->format, k_Border.r, k_Border.g, k_Border.b, 0xFF));
            drawText(surface, s_SmallFont, row.label.c_str(), row.color, left, y + px(9), sectionH - px(4), -1);
            y += sectionH;
        }
        else if (row.kind == RowNote) {
            drawText(surface, s_SmallFont, row.label.c_str(), row.color, left, y, noteH, -1);
            y += noteH;
        }
        else {
            drawText(surface, s_LabelFont, row.label.c_str(), k_TextDim, left, y, rowH, -1);
            drawText(surface, s_ValueFont, row.value.c_str(), row.color, right, y, rowH, 1);
            y += rowH;
        }
    }
    return surface;
}

// Caller holds s_Lock
void renderAndPush(const Snapshot& s)
{
    Overlay::OverlayManager& overlays = Session::get()->getOverlayManager();
    if (!overlays.isOverlayEnabled(Overlay::OverlayDebug) || !ensureFonts(s.scale)) {
        return;
    }
    loadCpuName();

    SDL_Surface* surface = level() == LevelBasic ? renderBasic(s) : renderPanel(s, level() == LevelAdvanced);
    if (surface != nullptr) {
        // Only replaces the picture; the user's on/off state stays in charge
        overlays.updateOverlaySurface(Overlay::OverlayDebug, surface, false);
    }
}

}

int level()
{
    if (!s_LevelLoaded) {
        SDL_AtomicLock(&s_LevelInit);
        if (!s_LevelLoaded) {
            QByteArray env = qgetenv("BEAM_STATS_LEVEL").toLower();
            int initial = env == "basic" ? LevelBasic : env == "advanced" ? LevelAdvanced : LevelStandard;
            SDL_AtomicSet(&s_Level, initial);
            s_LevelLoaded = true;
        }
        SDL_AtomicUnlock(&s_LevelInit);
    }
    return SDL_AtomicGet(&s_Level);
}

void setLevel(int newLevel)
{
    level(); // make sure the env default doesn't overwrite this later
    SDL_AtomicSet(&s_Level, SDL_clamp(newLevel, (int)LevelBasic, (int)LevelAdvanced));
}

const char* levelName(int lvl)
{
    switch (lvl) {
    case LevelBasic: return "Basic";
    case LevelAdvanced: return "Advanced";
    default: return "Standard";
    }
}

void reset()
{
    SDL_LockMutex(lock());
    s_HasLast = false;
    SDL_UnlockMutex(lock());
}

void publish(const Snapshot& snapshot)
{
    SDL_LockMutex(lock());
    s_Last = snapshot;
    s_HasLast = true;
    renderAndPush(s_Last);
    SDL_UnlockMutex(lock());
}

void refresh()
{
    SDL_LockMutex(lock());
    if (s_HasLast) {
        renderAndPush(s_Last);
    }
    SDL_UnlockMutex(lock());
}

void setGpuName(const char* vendorString)
{
    if (vendorString == nullptr) {
        return;
    }

    // Mesa: "Mesa Gallium driver 26.0.8 for AMD Radeon RX 9060 XT (radeonsi, ...)"
    QString name(vendorString);
    int forPos = name.indexOf(" for ");
    if (forPos >= 0) {
        name = name.mid(forPos + 5);
    }
    int paren = name.indexOf(" (");
    if (paren > 0) {
        name = name.left(paren);
    }

    SDL_LockMutex(lock());
    SDL_strlcpy(s_Gpu, name.trimmed().toUtf8().constData(), sizeof(s_Gpu));
    SDL_UnlockMutex(lock());
}

double sampleProcessCpu()
{
#ifdef Q_OS_LINUX
    static uint64_t lastTicks = 0, lastUs = 0;

    QFile file("/proc/self/stat");
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    QByteArray stat = file.readAll();
    // Fields after the parenthesised command name; utime and stime are 14 and 15
    QList<QByteArray> fields = stat.mid(stat.lastIndexOf(')') + 2).split(' ');
    if (fields.size() < 13) {
        return 0;
    }
    uint64_t ticks = fields[11].toULongLong() + fields[12].toULongLong();
    uint64_t nowUs = SDL_GetPerformanceCounter() * 1000000 / SDL_GetPerformanceFrequency();

    double pct = 0;
    if (lastUs != 0 && nowUs > lastUs) {
        double cpuSecs = (double)(ticks - lastTicks) / sysconf(_SC_CLK_TCK);
        double wallSecs = (nowUs - lastUs) / 1e6;
        pct = cpuSecs / wallSecs / SDL_max(1, SDL_GetCPUCount()) * 100.0;
    }
    lastTicks = ticks;
    lastUs = nowUs;
    return pct;
#else
    return 0;
#endif
}

}
