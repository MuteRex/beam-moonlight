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
#include "streaming/beamdraw.h"
#include "streaming/beamstats.h"

using namespace BeamDraw;

namespace {


const int k_ControlsRows = 7;

struct ControlsLayout {
    int width, height;
    SDL_Rect pill;
    SDL_Rect panel;
    SDL_Rect rows[k_ControlsRows];
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
    for (int i = 0; i < k_ControlsRows; i++) {
        // Separate the destructive action from the rest
        if (i == k_ControlsRows - 1) {
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


}

void SdlInputHandler::setControlsVisible(bool visible)
{
    // Every menu item needs a row, and Disconnect must be the last one
    static_assert(ControlsItemMax == k_ControlsRows, "menu rows out of sync");
    static_assert(ControlsItemDisconnect == k_ControlsRows - 1, "Disconnect must be last");

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
    else if (hit == ControlsItemStats) {
        // Cycle Off → Basic → Standard → Advanced → Off, keeping the menu open
        Overlay::OverlayManager& overlays = Session::get()->getOverlayManager();
        if (!overlays.isOverlayEnabled(Overlay::OverlayDebug)) {
            BeamStats::setLevel(BeamStats::LevelBasic);
            overlays.setOverlayState(Overlay::OverlayDebug, true);
            BeamStats::refresh();
        }
        else if (BeamStats::level() < BeamStats::LevelAdvanced) {
            BeamStats::setLevel(BeamStats::level() + 1);
            BeamStats::refresh();
        }
        else {
            overlays.setOverlayState(Overlay::OverlayDebug, false);
        }
        renderControls();
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

    bool pointerOver = controlsAcceptPointer() && controlsHitTest(windowX, windowY) != ControlsItemNone;
    if (pointerOver != m_ControlsPointerOver) {
        m_ControlsPointerOver = pointerOver;
        updateControlsCursor();
    }
}

void SdlInputHandler::updateControlsCursor()
{
    // In desktop mouse mode the local cursor is hidden and the host draws one
    // into the video, but the overlay is drawn over that. Show the real cursor
    // while it's over the pill or menu, or while the menu is open.
    if (!isCaptureActive() || !m_AbsoluteMouseMode ||
            m_MouseCursorCapturedVisibilityState != SDL_DISABLE) {
        return;
    }

    SDL_ShowCursor((m_ControlsPointerOver || m_ControlsMenuOpen) ? SDL_ENABLE : SDL_DISABLE);
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
    updateControlsCursor();
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
        m_ControlsFont = openFont(px(14));
        m_ControlsSmallFont = openFont(px(11));
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

    if (!m_ControlsPillHidden && !pillActive) {
        // Idle: slim handle centered on the top edge
        SDL_Rect handle = { layout.pill.x + (layout.pill.w - px(44)) / 2, px(3), px(44), SDL_max(3, px(5)) };
        SDL_Rect backing = { handle.x - px(2), handle.y - px(2), handle.w + px(4), handle.h + px(4) };
        fillRoundedRect(surface, backing, backing.h / 2.0f, k_HandleBacking);
        fillRoundedRect(surface, handle, handle.h / 2.0f, k_Handle);
    }

    // Pill: hamburger glyph and label
    if (!m_ControlsPillHidden && pillActive) {
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
            { "Performance stats", stats ? BeamStats::levelName(BeamStats::level()) : "Off" },
            { isPasting() ? "Stop pasting" : "Paste clipboard", nullptr },
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

    overlayManager.updateOverlaySurface(Overlay::OverlayControls, surface);
}
