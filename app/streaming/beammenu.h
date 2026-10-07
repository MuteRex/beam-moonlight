#pragma once

// Beam: the in-stream menu's pure logic (layout, hit-testing, the Sunshine
// screen-switch chord, the Full immersion rule). No window, renderer or
// connection needed, so tests/beam can check it directly.

#include <SDL.h>
#include <vector>

namespace BeamMenu {

// Row order; SdlInputHandler::ControlsItem mirrors it (checked by static_assert)
enum Row {
    RowMouseMode,
    RowFullScreen,
    RowImmersion,
    RowScreen,
    RowStats,
    RowPaste,
    RowReleaseMouse,
    RowMinimize,
    RowDisconnect,
    RowCount
};

// Hit-test results besides a row index
const int HitPanel = -3;        // inside the menu, between rows
const int HitNone = -2;
const int HitPill = -1;
const int HitScreenChip = 100;  // + screen index

// Sunshine switches monitors on Ctrl+Alt+Shift+F1..F12
const int MaxScreens = 12;

struct Layout {
    int width, height;
    SDL_Rect pill;
    SDL_Rect panel;
    SDL_Rect rows[RowCount];
    SDL_Rect screenChips[MaxScreens];
    int screens;                // chips laid out (clamped to 1..MaxScreens)
    int footerY;
};

Layout computeLayout(bool menuOpen, float scale, int screens);

int hitTest(const Layout& layout, bool menuOpen, bool pillHidden, int x, int y);

// Matches StreamingPreferences::CaptureSysKeysMode (checked in input.cpp)
enum CaptureMode { CaptureOff, CaptureFullScreen, CaptureAlways };

// Full immersion is "on" when system keys are captured in the current window
// mode; toggling switches between always and off.
bool immersionOn(int captureMode, bool fullScreen);
int toggledCaptureMode(int captureMode, bool fullScreen);

struct KeyEvent {
    short keyCode;
    char action;
    char modifiers;
};

// Ctrl+Alt+Shift+F<index+1> as real key presses: Sunshine looks for its
// shortcuts in the modifier key-downs, not just the modifier flags.
// Empty for an index outside 0..MaxScreens-1.
std::vector<KeyEvent> screenSwitchChord(int index);

}
