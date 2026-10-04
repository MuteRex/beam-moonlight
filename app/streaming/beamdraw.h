#pragma once

// Beam: shared drawing for the in-stream overlays (controls pill, stats panel).
// Everything draws into ARGB8888 surfaces with straight alpha, which every
// renderer's overlay path accepts.

#include "SDL_compat.h"
#include <SDL_ttf.h>

namespace BeamDraw {

// Beam palette (matches the launcher)
inline constexpr SDL_Color k_PillBackground = {0x14, 0x14, 0x14, 0xEB};
inline constexpr SDL_Color k_PanelBackground = {0x1B, 0x1B, 0x1B, 0xF2};
inline constexpr SDL_Color k_Border = {0x2E, 0x2E, 0x2E, 0xFF};
inline constexpr SDL_Color k_RowHover = {0x2A, 0x2A, 0x2A, 0xFF};
inline constexpr SDL_Color k_Accent = {0x9A, 0xE6, 0x00, 0xFF};
inline constexpr SDL_Color k_Text = {0xE8, 0xE8, 0xE8, 0xFF};
inline constexpr SDL_Color k_TextDim = {0x80, 0x80, 0x80, 0xFF};
inline constexpr SDL_Color k_Danger = {0xFF, 0x6B, 0x6B, 0xFF};

// When idle the pill collapses to a slim handle on the top edge so it doesn't
// cover game UI or the host's cursor; hovering it expands the full pill.
inline constexpr SDL_Color k_HandleBacking = {0x00, 0x00, 0x00, 0x70};
inline constexpr SDL_Color k_Handle = {0x9A, 0xE6, 0x00, 0xC0};
inline constexpr SDL_Color k_Warning = {0xF5, 0xC2, 0x11, 0xFF};

// Anti-aliased filled rounded rectangle
void fillRoundedRect(SDL_Surface* surface, SDL_Rect rect, float radius, SDL_Color color);

// Background plus a border, keeping the background's translucency
void fillRoundedPanel(SDL_Surface* surface, SDL_Rect rect, float radius, SDL_Color background, int borderWidth);

// Draws text vertically centered in the given band. Alignment: -1 left, 0 center, 1 right.
// Returns the drawn width.
int drawText(SDL_Surface* surface, TTF_Font* font, const char* text, SDL_Color color,
             int x, int bandY, int bandHeight, int align);

// Opens the UI font (or the monospace one for numbers) at the given size
TTF_Font* openFont(int pointSize, bool monospace = false);

SDL_Surface* createSurface(int width, int height);

}
