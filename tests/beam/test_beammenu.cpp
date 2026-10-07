// make -C tests/beam test

#include "streaming/beammenu.h"

#include <Limelight.h>
#include <cstdio>
#include <map>

using namespace BeamMenu;

static int g_Failures = 0;
static int g_Checks = 0;

#define CHECK(cond) do { g_Checks++; if (!(cond)) { g_Failures++; \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static bool inside(const SDL_Rect& inner, const SDL_Rect& outer)
{
    return inner.x >= outer.x && inner.y >= outer.y &&
            inner.x + inner.w <= outer.x + outer.w && inner.y + inner.h <= outer.y + outer.h;
}

static bool overlaps(const SDL_Rect& a, const SDL_Rect& b)
{
    return SDL_HasIntersection(&a, &b) == SDL_TRUE;
}

static void testLayoutGeometry()
{
    const float scales[] = { 1.0f, 1.5f, 2.0f, 2.5f };
    for (float scale : scales) {
        for (int screens = 1; screens <= MaxScreens; screens++) {
            Layout l = computeLayout(true, scale, screens);
            CHECK(l.screens == screens);
            CHECK(l.width == l.panel.w);
            CHECK(l.height == l.panel.y + l.panel.h);
            CHECK(inside(l.pill, { 0, 0, l.width, l.height }));

            for (int i = 0; i < RowCount; i++) {
                CHECK(inside(l.rows[i], l.panel));
                if (i > 0) {
                    CHECK(l.rows[i].y >= l.rows[i - 1].y + l.rows[i - 1].h);
                }
            }
            // Disconnect sits apart from the rest
            CHECK(l.rows[RowDisconnect].y > l.rows[RowMinimize].y + l.rows[RowMinimize].h);
            CHECK(l.footerY >= l.rows[RowDisconnect].y + l.rows[RowDisconnect].h);

            const SDL_Rect& row = l.rows[RowScreen];
            for (int i = 0; i < screens; i++) {
                CHECK(inside(l.screenChips[i], row));
                // Room for the "Screen" label on the left
                CHECK(l.screenChips[i].x >= row.x + (int)(80 * scale));
                if (i > 0) {
                    CHECK(!overlaps(l.screenChips[i], l.screenChips[i - 1]));
                    CHECK(l.screenChips[i].x > l.screenChips[i - 1].x);
                }
            }
        }
    }
}

static void testScreenCountClamped()
{
    CHECK(computeLayout(true, 1.0f, 0).screens == 1);
    CHECK(computeLayout(true, 1.0f, -4).screens == 1);
    CHECK(computeLayout(true, 1.0f, 99).screens == MaxScreens);
    // A wide menu only when many screens need the room
    CHECK(computeLayout(true, 1.0f, 2).width == computeLayout(true, 1.0f, 1).width);
    CHECK(computeLayout(true, 1.0f, 12).width > computeLayout(true, 1.0f, 2).width);
}

static int centerX(const SDL_Rect& r) { return r.x + r.w / 2; }
static int centerY(const SDL_Rect& r) { return r.y + r.h / 2; }

static void testHitTestClosed()
{
    Layout l = computeLayout(false, 1.0f, 2);
    CHECK(l.width == l.pill.w);
    CHECK(hitTest(l, false, false, centerX(l.pill), centerY(l.pill)) == HitPill);
    CHECK(hitTest(l, false, true, centerX(l.pill), centerY(l.pill)) == HitNone);
    CHECK(hitTest(l, false, false, l.width + 5, l.height + 5) == HitNone);
}

static void testHitTestOpen()
{
    Layout l = computeLayout(true, 1.0f, 3);
    CHECK(hitTest(l, true, false, centerX(l.pill), centerY(l.pill)) == HitPill);
    // The pill can be hidden while the menu is open (Ctrl+Alt+Shift+B)
    CHECK(hitTest(l, true, true, centerX(l.pill), centerY(l.pill)) != HitPill);

    for (int i = 0; i < RowCount; i++) {
        // Left side of each row: the label, never a chip
        CHECK(hitTest(l, true, false, l.rows[i].x + 10, centerY(l.rows[i])) == i);
    }
    for (int i = 0; i < l.screens; i++) {
        CHECK(hitTest(l, true, false, centerX(l.screenChips[i]), centerY(l.screenChips[i])) == HitScreenChip + i);
    }

    // The gap above Disconnect is panel, not a row
    int gapY = l.rows[RowMinimize].y + l.rows[RowMinimize].h + 2;
    CHECK(hitTest(l, true, false, centerX(l.panel), gapY) == HitPanel);
    CHECK(hitTest(l, true, false, l.width + 5, 5) == HitNone);
}

static void testImmersion()
{
    CHECK(!immersionOn(CaptureOff, false));
    CHECK(!immersionOn(CaptureOff, true));
    CHECK(!immersionOn(CaptureFullScreen, false));
    CHECK(immersionOn(CaptureFullScreen, true));
    CHECK(immersionOn(CaptureAlways, false));
    CHECK(immersionOn(CaptureAlways, true));

    // Toggling always lands on a mode that flips what the menu shows
    const int modes[] = { CaptureOff, CaptureFullScreen, CaptureAlways };
    for (int mode : modes) {
        for (bool fullScreen : { false, true }) {
            int next = toggledCaptureMode(mode, fullScreen);
            CHECK(immersionOn(next, fullScreen) != immersionOn(mode, fullScreen));
            CHECK(next == CaptureOff || next == CaptureAlways);
        }
    }
    // Windowed with the "fullscreen only" default: turning it on means always
    CHECK(toggledCaptureMode(CaptureFullScreen, false) == CaptureAlways);
}

static void testScreenSwitchChord()
{
    CHECK(screenSwitchChord(-1).empty());
    CHECK(screenSwitchChord(MaxScreens).empty());

    for (int index = 0; index < MaxScreens; index++) {
        std::vector<KeyEvent> events = screenSwitchChord(index);
        CHECK(events.size() == 8);
        if (events.size() != 8) {
            continue;
        }

        const short ctrl = (short)0x80A2, alt = (short)0x80A4, shift = (short)0x80A0;
        const short fKey = (short)(0x8000 | (0x70 + index));
        const char all = MODIFIER_CTRL | MODIFIER_ALT | MODIFIER_SHIFT;

        // Real modifier key-downs first: Sunshine tracks those, not the flags
        CHECK(events[0].keyCode == ctrl && events[0].action == KEY_ACTION_DOWN);
        CHECK(events[1].keyCode == alt && events[1].action == KEY_ACTION_DOWN);
        CHECK(events[2].keyCode == shift && events[2].action == KEY_ACTION_DOWN);
        CHECK(events[3].keyCode == fKey && events[3].action == KEY_ACTION_DOWN && events[3].modifiers == all);
        CHECK(events[4].keyCode == fKey && events[4].action == KEY_ACTION_UP);

        // Every key that goes down comes back up, and nothing is left held
        std::map<short, int> held;
        for (const KeyEvent& e : events) {
            held[e.keyCode] += e.action == KEY_ACTION_DOWN ? 1 : -1;
            CHECK(held[e.keyCode] >= 0);
        }
        for (const auto& entry : held) {
            CHECK(entry.second == 0);
        }
        CHECK(events.back().action == KEY_ACTION_UP && events.back().modifiers == 0);
    }

    // F1 for the first screen, F12 for the last
    CHECK(screenSwitchChord(0)[3].keyCode == (short)0x8070);
    CHECK(screenSwitchChord(11)[3].keyCode == (short)0x807B);
}

int main()
{
    testLayoutGeometry();
    testScreenCountClamped();
    testHitTestClosed();
    testHitTestOpen();
    testImmersion();
    testScreenSwitchChord();

    std::printf("%d checks, %d failed\n", g_Checks, g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
