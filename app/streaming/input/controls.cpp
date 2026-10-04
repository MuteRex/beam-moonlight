// Beam: clickable in-session pill and options menu drawn over the stream.
//
// The pill sits top-center. Clicking it (or Ctrl+Alt+Shift+B) opens a menu
// whose items invoke the existing special key combo handlers, so no stream
// control logic is duplicated here. The pill and menu are composited into a
// single surface and handed to the OverlayManager, which every renderer
// already knows how to draw.

#include "input.h"

#include <Limelight.h>
#include "SDL_compat.h"
#include "streaming/session.h"

namespace {

// Beam palette (matches the launcher)
const SDL_Color k_PillBackground = {0x14, 0x14, 0x14, 0xEB};
const SDL_Color k_PanelBackground = {0x1B, 0x1B, 0x1B, 0xF2};
const SDL_Color k_Border = {0x2E, 0x2E, 0x2E, 0xFF};
const SDL_Color k_RowHover = {0x2A, 0x2A, 0x2A, 0xFF};
const SDL_Color k_Accent = {0x9A, 0xE6, 0x00, 0xFF};
const SDL_Color k_Text = {0xE8, 0xE8, 0xE8, 0xFF};
const SDL_Color k_TextDim = {0x80, 0x80, 0x80, 0xFF};
const SDL_Color k_Danger = {0xFF, 0x6B, 0x6B, 0xFF};

// Opacity of the pill when it isn't being interacted with
const float k_IdleOpacity = 0.55f;

const char* const k_FontCandidates[] = {
    "/usr/share/fonts/truetype/ubuntu/Ubuntu-M.ttf",
    "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
    "/usr/share/fonts/opentype/cantarell/Cantarell-VF.otf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
};

struct ControlsLayout {
    int width, height;
    SDL_Rect pill;
    SDL_Rect panel;
    SDL_Rect rows[7];
    int footerY;
};

ControlsLayout computeLayout(bool menuOpen, float scale)
{
    auto px = [scale](float v) { return (int)(v * scale + 0.5f); };

    ControlsLayout layout = {};
    int pillWidth = px(104), pillHeight = px(30), pillTop = px(6);
    int panelWidth = px(272);

    layout.width = menuOpen ? panelWidth : pillWidth;
    layout.pill = { (layout.width - pillWidth) / 2, pillTop, pillWidth, pillHeight };

    if (!menuOpen) {
        layout.height = pillTop + pillHeight;
        return layout;
    }

    int y = layout.pill.y + layout.pill.h + px(8);
    layout.panel = { 0, y, panelWidth, 0 };

    y += px(8);
    for (int i = 0; i < 7; i++) {
        // Separate the destructive action from the rest
        if (i == 6) {
            y += px(9);
        }
        layout.rows[i] = { px(6), y, panelWidth - px(12), px(34) };
        y += px(34);
    }

    layout.footerY = y + px(4);
    y = layout.footerY + px(22);

    layout.panel.h = y - layout.panel.y;
    layout.height = y;
    return layout;
}

inline Uint8 blendChannel(Uint8 src, Uint8 dst, float srcA, float dstA, float outA)
{
    if (outA <= 0.0f) {
        return 0;
    }
    return (Uint8)((src * srcA + dst * dstA * (1.0f - srcA)) / outA + 0.5f);
}

// Straight-alpha "over" of a single pixel into an ARGB8888 surface
void blendPixel(SDL_Surface* surface, int x, int y, SDL_Color color, float coverage)
{
    if (x < 0 || y < 0 || x >= surface->w || y >= surface->h || coverage <= 0.0f) {
        return;
    }

    Uint32* pixel = (Uint32*)((Uint8*)surface->pixels + y * surface->pitch) + x;
    float srcA = (color.a / 255.0f) * SDL_min(coverage, 1.0f);
    float dstA = ((*pixel >> 24) & 0xFF) / 255.0f;
    float outA = srcA + dstA * (1.0f - srcA);

    Uint8 r = blendChannel(color.r, (*pixel >> 16) & 0xFF, srcA, dstA, outA);
    Uint8 g = blendChannel(color.g, (*pixel >> 8) & 0xFF, srcA, dstA, outA);
    Uint8 b = blendChannel(color.b, *pixel & 0xFF, srcA, dstA, outA);
    *pixel = ((Uint32)(outA * 255.0f + 0.5f) << 24) | (r << 16) | (g << 8) | b;
}

// Anti-aliased filled rounded rectangle
void fillRoundedRect(SDL_Surface* surface, SDL_Rect rect, float radius, SDL_Color color)
{
    radius = SDL_min(radius, SDL_min(rect.w, rect.h) / 2.0f);

    for (int y = rect.y; y < rect.y + rect.h; y++) {
        for (int x = rect.x; x < rect.x + rect.w; x++) {
            // Distance from the pixel center to the nearest corner circle center
            float px = x + 0.5f, py = y + 0.5f;
            float cx = SDL_max(rect.x + radius, SDL_min(px, rect.x + rect.w - radius));
            float cy = SDL_max(rect.y + radius, SDL_min(py, rect.y + rect.h - radius));
            float dist = SDL_sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy));
            blendPixel(surface, x, y, color, radius + 0.5f - dist);
        }
    }
}

// Background plus a 1px border
void fillRoundedPanel(SDL_Surface* surface, SDL_Rect rect, float radius, SDL_Color background, int borderWidth)
{
    fillRoundedRect(surface, rect, radius, k_Border);
    SDL_Rect inner = { rect.x + borderWidth, rect.y + borderWidth, rect.w - 2 * borderWidth, rect.h - 2 * borderWidth };

    // The inner fill replaces rather than blends so translucency is preserved
    SDL_Rect bounds = { 0, 0, surface->w, surface->h }, clear;
    if (!SDL_IntersectRect(&inner, &bounds, &clear)) {
        return;
    }
    for (int y = clear.y; y < clear.y + clear.h; y++) {
        Uint32* row = (Uint32*)((Uint8*)surface->pixels + y * surface->pitch);
        for (int x = clear.x; x < clear.x + clear.w; x++) {
            row[x] &= 0x00FFFFFF;
        }
    }
    fillRoundedRect(surface, inner, SDL_max(0.0f, radius - borderWidth), background);
}

// Draws text vertically centered in the given band. Alignment: -1 left, 0 center, 1 right.
void drawText(SDL_Surface* surface, TTF_Font* font, const char* text, SDL_Color color,
              int x, int bandY, int bandHeight, int align)
{
    if (font == nullptr || text == nullptr || text[0] == 0) {
        return;
    }

    SDL_Surface* textSurface = TTF_RenderUTF8_Blended(font, text, color);
    if (textSurface == nullptr) {
        return;
    }

    if (align == 0) {
        x -= textSurface->w / 2;
    }
    else if (align > 0) {
        x -= textSurface->w;
    }

    SDL_Rect dst = { x, bandY + (bandHeight - textSurface->h) / 2, textSurface->w, textSurface->h };
    SDL_SetSurfaceBlendMode(textSurface, SDL_BLENDMODE_BLEND);
    SDL_BlitSurface(textSurface, nullptr, surface, &dst);
    SDL_FreeSurface(textSurface);
}

void multiplyAlpha(SDL_Surface* surface, float factor)
{
    for (int y = 0; y < surface->h; y++) {
        Uint32* row = (Uint32*)((Uint8*)surface->pixels + y * surface->pitch);
        for (int x = 0; x < surface->w; x++) {
            Uint32 a = (Uint32)(((row[x] >> 24) & 0xFF) * factor + 0.5f);
            row[x] = (row[x] & 0x00FFFFFF) | (a << 24);
        }
    }
}

TTF_Font* openControlsFont(int pointSize)
{
    for (const char* path : k_FontCandidates) {
        TTF_Font* font = TTF_OpenFont(path, pointSize);
        if (font != nullptr) {
            return font;
        }
    }

    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "No font found for Beam controls overlay: %s",
                TTF_GetError());
    return nullptr;
}

}

void SdlInputHandler::setControlsVisible(bool visible)
{
#ifdef Q_OS_LINUX
    m_ControlsVisible = visible;
#else
    // The overlay is only positioned by the Linux renderers
    m_ControlsVisible = false;
    (void)visible;
#endif

    if (!m_ControlsVisible) {
        m_ControlsMenuOpen = false;
    }

    refreshControls();
}

void SdlInputHandler::refreshControls()
{
    float scale = 1.0f;
    SDL_DisplayMode mode;
    if (m_Window != nullptr && SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(m_Window), &mode) == 0) {
        scale = SDL_max(1.0f, SDL_min(2.5f, mode.h / 1080.0f));
    }

    // Fonts are sized for the scale, so reopen them if it changed
    if (scale != m_ControlsScale) {
        cleanupControls();
        m_ControlsScale = scale;
    }

    renderControls();
}

void SdlInputHandler::cleanupControls()
{
    if (m_ControlsFont != nullptr || m_ControlsSmallFont != nullptr) {
        if (m_ControlsFont != nullptr) {
            TTF_CloseFont(m_ControlsFont);
            m_ControlsFont = nullptr;
        }
        if (m_ControlsSmallFont != nullptr) {
            TTF_CloseFont(m_ControlsSmallFont);
            m_ControlsSmallFont = nullptr;
        }

        // Balances the TTF_Init() in renderControls()
        TTF_Quit();
    }
}

bool SdlInputHandler::controlsAcceptPointer()
{
    // With relative mouse capture there is no on-screen cursor to click with,
    // so the pill only takes clicks in desktop mouse mode or when uncaptured.
    // The menu itself releases capture while it is open.
    return m_ControlsVisible &&
            (m_ControlsMenuOpen || (!m_ControlsPillHidden && (m_AbsoluteMouseMode || !isCaptureActive())));
}

int SdlInputHandler::controlsHitTest(int windowX, int windowY)
{
    int windowWidth, windowHeight;
    SDL_GetWindowSize(m_Window, &windowWidth, &windowHeight);

    int x, y;
    if (!Session::get()->getOverlayManager().windowPointToOverlay(Overlay::OverlayControls,
                                                                  windowX, windowY,
                                                                  windowWidth, windowHeight,
                                                                  &x, &y)) {
        return ControlsItemNone;
    }

    ControlsLayout layout = computeLayout(m_ControlsMenuOpen, m_ControlsScale);
    SDL_Point point = { x, y };

    if (!m_ControlsPillHidden && SDL_PointInRect(&point, &layout.pill)) {
        return ControlsItemPill;
    }
    else if (m_ControlsMenuOpen) {
        for (int i = 0; i < ControlsItemMax; i++) {
            if (SDL_PointInRect(&point, &layout.rows[i])) {
                return i;
            }
        }
        if (SDL_PointInRect(&point, &layout.panel)) {
            return ControlsItemPanel;
        }
    }

    return ControlsItemNone;
}

bool SdlInputHandler::handleControlsButtonEvent(SDL_MouseButtonEvent* event)
{
    if (!controlsAcceptPointer()) {
        return false;
    }

    int hit = controlsHitTest(event->x, event->y);

    if (!m_ControlsMenuOpen) {
        if (hit != ControlsItemPill) {
            return false;
        }

        if (event->button == SDL_BUTTON_LEFT) {
            if (event->state == SDL_PRESSED) {
                m_ControlsPressedItem = ControlsItemPill;
            }
            else if (m_ControlsPressedItem == ControlsItemPill) {
                m_ControlsPressedItem = ControlsItemNone;
                setControlsMenuOpen(true);
            }
        }

        // Clicks on the pill never reach the host
        return true;
    }

    // While the menu is open, it owns all clicks
    if (event->state == SDL_PRESSED) {
        m_ControlsPressedItem = hit;
        return true;
    }

    int pressed = m_ControlsPressedItem;
    m_ControlsPressedItem = ControlsItemNone;

    if (event->button != SDL_BUTTON_LEFT || hit != pressed) {
        return true;
    }

    if (hit == ControlsItemPill || hit == ControlsItemNone) {
        // Clicking the pill again or anywhere outside the menu dismisses it
        setControlsMenuOpen(false);
    }
    else if (hit >= 0) {
        static const KeyCombo itemCombos[ControlsItemMax] = {
            KeyComboToggleMouseMode,
            KeyComboToggleFullScreen,
            KeyComboToggleStatsOverlay,
            KeyComboPasteText,
            KeyComboUngrabInput,
            KeyComboToggleMinimize,
            KeyComboQuit,
        };

        // Close first so capture is restored before the action changes it
        setControlsMenuOpen(false);

        if (m_SpecialKeyCombos[itemCombos[hit]].enabled) {
            performSpecialKeyCombo(itemCombos[hit]);
        }
    }

    return true;
}

void SdlInputHandler::handleControlsMotionEvent(int windowX, int windowY)
{
    int hit = controlsAcceptPointer() ? controlsHitTest(windowX, windowY) : (int)ControlsItemNone;
    if (hit == ControlsItemPanel) {
        hit = ControlsItemNone;
    }

    if (hit != m_ControlsHoveredItem) {
        m_ControlsHoveredItem = hit;
        renderControls();
    }
}

void SdlInputHandler::setControlsMenuOpen(bool open)
{
    if (!m_ControlsVisible || open == m_ControlsMenuOpen) {
        return;
    }

    if (open) {
        if (isCaptureActive()) {
            // Buttons held when the menu opens would otherwise never be released on the host
            Uint32 buttons = SDL_GetMouseState(nullptr, nullptr);
            static const struct { Uint32 sdl; int ml; } buttonMap[] = {
                { SDL_BUTTON_LMASK, BUTTON_LEFT }, { SDL_BUTTON_MMASK, BUTTON_MIDDLE },
                { SDL_BUTTON_RMASK, BUTTON_RIGHT }, { SDL_BUTTON_X1MASK, BUTTON_X1 },
                { SDL_BUTTON_X2MASK, BUTTON_X2 },
            };
            for (const auto& button : buttonMap) {
                if (buttons & button.sdl) {
                    LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, button.ml);
                }
            }

            // Relative capture hides the cursor, so release it while the menu is up
            if (!m_AbsoluteMouseMode) {
                m_ControlsRestoreCapture = true;
                setCaptureActive(false);
            }
        }
    }
    else if (m_ControlsRestoreCapture) {
        m_ControlsRestoreCapture = false;
        setCaptureActive(true);
    }

    m_ControlsMenuOpen = open;
    m_ControlsPressedItem = ControlsItemNone;
    m_ControlsHoveredItem = ControlsItemNone;
    renderControls();
}

void SdlInputHandler::renderControls()
{
    Overlay::OverlayManager& overlayManager = Session::get()->getOverlayManager();

    if (!m_ControlsVisible || (m_ControlsPillHidden && !m_ControlsMenuOpen)) {
        overlayManager.updateOverlaySurface(Overlay::OverlayControls, nullptr);
        return;
    }

    auto px = [this](float v) { return (int)(v * m_ControlsScale + 0.5f); };

    if (m_ControlsFont == nullptr && m_ControlsSmallFont == nullptr) {
        if (TTF_Init() != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "TTF_Init() failed: %s",
                        TTF_GetError());
            return;
        }
        m_ControlsFont = openControlsFont(px(14));
        m_ControlsSmallFont = openControlsFont(px(11));
        if (m_ControlsFont == nullptr && m_ControlsSmallFont == nullptr) {
            TTF_Quit();
        }
    }

    ControlsLayout layout = computeLayout(m_ControlsMenuOpen, m_ControlsScale);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, layout.width, layout.height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface == nullptr) {
        return;
    }
    SDL_FillRect(surface, nullptr, 0);

    bool pillActive = m_ControlsMenuOpen || m_ControlsHoveredItem == ControlsItemPill;

    // Pill: hamburger glyph and label
    if (!m_ControlsPillHidden) {
        fillRoundedPanel(surface, layout.pill, layout.pill.h / 2.0f, k_PillBackground, 1);

        int iconX = layout.pill.x + px(16);
        int iconY = layout.pill.y + layout.pill.h / 2 - px(5);
        for (int i = 0; i < 3; i++) {
            fillRoundedRect(surface, { iconX, iconY + i * px(4) + (i > 0 ? px(0.5f) : 0), px(14), SDL_max(2, px(2)) },
                            px(1), pillActive ? k_Accent : k_Text);
        }
        drawText(surface, m_ControlsFont, "Beam", pillActive ? k_Accent : k_Text,
                 iconX + px(22), layout.pill.y, layout.pill.h, -1);
    }

    if (m_ControlsMenuOpen) {
        bool captured = isCaptureActive() || m_ControlsRestoreCapture;
        bool fullScreen = (SDL_GetWindowFlags(m_Window) & SDL_WINDOW_FULLSCREEN) != 0;
        bool stats = Session::get()->getOverlayManager().isOverlayEnabled(Overlay::OverlayDebug);

        struct { const char* label; const char* value; } items[ControlsItemMax] = {
            { "Mouse mode", m_AbsoluteMouseMode ? "Desktop" : "Game" },
            { "Fullscreen", fullScreen ? "On" : "Off" },
            { "Performance stats", stats ? "On" : "Off" },
            { "Paste clipboard", nullptr },
            { captured ? "Release mouse" : "Capture mouse", nullptr },
            { "Minimize", nullptr },
            { "Disconnect", nullptr },
        };
        static const KeyCombo itemCombos[ControlsItemMax] = {
            KeyComboToggleMouseMode, KeyComboToggleFullScreen, KeyComboToggleStatsOverlay,
            KeyComboPasteText, KeyComboUngrabInput, KeyComboToggleMinimize, KeyComboQuit,
        };

        fillRoundedPanel(surface, layout.panel, px(12), k_PanelBackground, 1);

        for (int i = 0; i < ControlsItemMax; i++) {
            const SDL_Rect& row = layout.rows[i];
            bool enabled = m_SpecialKeyCombos[itemCombos[i]].enabled;

            if (i == ControlsItemDisconnect) {
                SDL_Rect divider = { layout.panel.x + px(14), row.y - px(5), layout.panel.w - px(28), SDL_max(1, px(1)) };
                SDL_FillRect(surface, &divider, SDL_MapRGBA(surface->format, k_Border.r, k_Border.g, k_Border.b, k_Border.a));
            }

            if (enabled && i == m_ControlsHoveredItem) {
                fillRoundedRect(surface, row, px(7), k_RowHover);
                fillRoundedRect(surface, { row.x + px(4), row.y + px(9), px(3), row.h - px(18) }, px(1.5f),
                                i == ControlsItemDisconnect ? k_Danger : k_Accent);
            }

            SDL_Color labelColor = !enabled ? k_TextDim : (i == ControlsItemDisconnect ? k_Danger : k_Text);
            drawText(surface, m_ControlsFont, items[i].label, labelColor, row.x + px(16), row.y, row.h, -1);
            drawText(surface, m_ControlsFont, items[i].value, enabled ? k_Accent : k_TextDim,
                     row.x + row.w - px(12), row.y, row.h, 1);
        }

        drawText(surface, m_ControlsSmallFont, "Ctrl+Alt+Shift+B toggles this menu", k_TextDim,
                 layout.panel.x + layout.panel.w / 2, layout.footerY, px(18), 0);
    }

    if (!pillActive) {
        multiplyAlpha(surface, k_IdleOpacity);
    }

    overlayManager.updateOverlaySurface(Overlay::OverlayControls, surface);
}
