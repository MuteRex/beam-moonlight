#include "beammenu.h"

#include <Limelight.h>

namespace BeamMenu {

Layout computeLayout(bool menuOpen, float scale, int screens)
{
    auto px = [scale](float v) { return (int)(v * scale + 0.5f); };

    Layout layout = {};
    layout.screens = SDL_max(1, SDL_min(MaxScreens, screens));

    int pillWidth = px(104), pillHeight = px(30), pillTop = px(6);
    // Wide enough for the Screen label plus its buttons
    int panelWidth = SDL_max(px(272), px(128) + layout.screens * px(29));

    layout.width = menuOpen ? panelWidth : pillWidth;
    layout.pill = { (layout.width - pillWidth) / 2, pillTop, pillWidth, pillHeight };

    if (!menuOpen) {
        layout.height = pillTop + pillHeight;
        return layout;
    }

    int y = layout.pill.y + layout.pill.h + px(8);
    layout.panel = { 0, y, panelWidth, 0 };

    y += px(8);
    for (int i = 0; i < RowCount; i++) {
        // Separate the destructive action from the rest
        if (i == RowDisconnect) {
            y += px(9);
        }
        layout.rows[i] = { px(6), y, panelWidth - px(12), px(34) };
        y += px(34);
    }

    const SDL_Rect& screenRow = layout.rows[RowScreen];
    int chipW = px(24), chipH = px(22), chipGap = px(5);
    int chipX = screenRow.x + screenRow.w - px(8) - layout.screens * chipW - (layout.screens - 1) * chipGap;
    for (int i = 0; i < layout.screens; i++) {
        layout.screenChips[i] = { chipX + i * (chipW + chipGap), screenRow.y + (screenRow.h - chipH) / 2, chipW, chipH };
    }

    layout.footerY = y + px(4);
    y = layout.footerY + px(22);

    layout.panel.h = y - layout.panel.y;
    layout.height = y;
    return layout;
}

int hitTest(const Layout& layout, bool menuOpen, bool pillHidden, int x, int y)
{
    SDL_Point point = { x, y };

    if (!pillHidden && SDL_PointInRect(&point, &layout.pill)) {
        return HitPill;
    }
    if (!menuOpen) {
        return HitNone;
    }
    for (int i = 0; i < layout.screens; i++) {
        if (SDL_PointInRect(&point, &layout.screenChips[i])) {
            return HitScreenChip + i;
        }
    }
    for (int i = 0; i < RowCount; i++) {
        if (SDL_PointInRect(&point, &layout.rows[i])) {
            return i;
        }
    }
    return SDL_PointInRect(&point, &layout.panel) ? HitPanel : HitNone;
}

bool immersionOn(int captureMode, bool fullScreen)
{
    return captureMode == CaptureAlways || (captureMode == CaptureFullScreen && fullScreen);
}

int toggledCaptureMode(int captureMode, bool fullScreen)
{
    return immersionOn(captureMode, fullScreen) ? CaptureOff : CaptureAlways;
}

std::vector<KeyEvent> screenSwitchChord(int index)
{
    std::vector<KeyEvent> events;
    if (index < 0 || index >= MaxScreens) {
        return events;
    }

    // Left Ctrl, Alt, Shift (Windows VK codes, as Moonlight sends them)
    const short chord[] = { (short)(0x8000 | 0xA2), (short)(0x8000 | 0xA4), (short)(0x8000 | 0xA0) };
    const char held[] = { MODIFIER_CTRL, MODIFIER_CTRL | MODIFIER_ALT,
                          MODIFIER_CTRL | MODIFIER_ALT | MODIFIER_SHIFT };
    const short fKey = (short)(0x8000 | (0x70 + index)); // VK_F1 + index

    for (int i = 0; i < 3; i++) {
        events.push_back({ chord[i], KEY_ACTION_DOWN, held[i] });
    }
    events.push_back({ fKey, KEY_ACTION_DOWN, held[2] });
    events.push_back({ fKey, KEY_ACTION_UP, held[2] });
    for (int i = 2; i >= 0; i--) {
        events.push_back({ chord[i], KEY_ACTION_UP, (char)(i > 0 ? held[i - 1] : 0) });
    }
    return events;
}

}
