#include "beamdraw.h"

namespace BeamDraw {

const char* const k_FontCandidates[] = {
    "/usr/share/fonts/truetype/ubuntu/Ubuntu-M.ttf",
    "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
    "/usr/share/fonts/opentype/cantarell/Cantarell-VF.otf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
};

const char* const k_MonoFontCandidates[] = {
    "/usr/share/fonts/truetype/ubuntu/UbuntuMono-B.ttf",
    "/usr/share/fonts/truetype/ubuntu/UbuntuSansMono[wght].ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/ubuntu/Ubuntu-M.ttf",
};

namespace {

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
int drawText(SDL_Surface* surface, TTF_Font* font, const char* text, SDL_Color color,
             int x, int bandY, int bandHeight, int align)
{
    if (font == nullptr || text == nullptr || text[0] == 0) {
        return 0;
    }

    SDL_Surface* textSurface = TTF_RenderUTF8_Blended(font, text, color);
    if (textSurface == nullptr) {
        return 0;
    }
    int width = textSurface->w;

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
    return width;
}

TTF_Font* openFont(int pointSize, bool monospace)
{
    for (const char* path : monospace ? k_MonoFontCandidates : k_FontCandidates) {
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

SDL_Surface* createSurface(int width, int height)
{
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface != nullptr) {
        SDL_FillRect(surface, nullptr, 0);
    }
    return surface;
}

}
