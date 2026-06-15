// WidgetsSelfCheckMain.cpp — headless KAT for the portable widget logic (Linux
// port L4-a). Locks widgets::handle's push-button contract: a click fires only
// when a left press starts inside the button AND the matching release is also
// inside; press-away, release-away, disabled, and right-button cases never fire.
// Also checks hover tracking and visualState. Exit 0 on success.

#include "Widgets.h"

#include <cstdio>

namespace {
using platform::Event;
using platform::EventType;
using platform::MouseButton;
using widgets::Button;
using widgets::Visual;

int g_fail = 0;

void check(bool cond, const char* what) {
    if (!cond) { std::printf("  FAIL %s\n", what); ++g_fail; }
}

Event move(float x, float y) {
    Event e; e.type = EventType::MouseMove; e.x = x; e.y = y; return e;
}
Event down(float x, float y, MouseButton b = MouseButton::Left) {
    Event e; e.type = EventType::MouseDown; e.x = x; e.y = y; e.button = b; return e;
}
Event up(float x, float y, MouseButton b = MouseButton::Left) {
    Event e; e.type = EventType::MouseUp; e.x = x; e.y = y; e.button = b; return e;
}

Button makeButton() {
    Button b;
    b.bounds = {100, 50, 120, 40};  // x:[100,220) y:[50,90)
    b.label = "OK";
    return b;
}
} // namespace

int main() {
    // Inside / outside reference points.
    const float ix = 150, iy = 70;    // inside
    const float ox = 300, oy = 70;    // outside (to the right)

    // 1. Full click: down inside, up inside -> clicked, consumed, state cleared.
    {
        Button b = makeButton();
        auto d = widgets::handle(b, down(ix, iy));
        check(d.consumed && !d.clicked, "down inside consumes, no click yet");
        check(b.pressed && b.hover, "armed + hover after down inside");
        auto u = widgets::handle(b, up(ix, iy));
        check(u.consumed && u.clicked, "up inside completes the click");
        check(!b.pressed, "press cleared after release");
    }

    // 2. Press inside, release OUTSIDE -> no click, press cleared, not hovering.
    {
        Button b = makeButton();
        widgets::handle(b, down(ix, iy));
        auto u = widgets::handle(b, up(ox, oy));
        check(u.consumed && !u.clicked, "release outside: consumed, no click");
        check(!b.pressed && !b.hover, "release outside clears press + hover");
    }

    // 3. Press OUTSIDE, release inside -> never armed, no click, not consumed.
    {
        Button b = makeButton();
        auto d = widgets::handle(b, down(ox, oy));
        check(!d.consumed && !b.pressed, "down outside is ignored");
        auto u = widgets::handle(b, up(ix, iy));
        check(!u.consumed && !u.clicked, "release without a prior arm never clicks");
    }

    // 4. Disabled button: down+up inside -> inert (no click, nothing consumed).
    {
        Button b = makeButton();
        b.enabled = false;
        auto d = widgets::handle(b, down(ix, iy));
        auto u = widgets::handle(b, up(ix, iy));
        check(!d.consumed && !u.consumed && !u.clicked, "disabled button is inert");
        check(!b.pressed && !b.hover, "disabled button never arms/hovers");
    }

    // 5. Right-button press inside does not arm the button.
    {
        Button b = makeButton();
        auto d = widgets::handle(b, down(ix, iy, MouseButton::Right));
        check(!d.consumed && !b.pressed, "right-button down ignored");
    }

    // 6. Hover tracking via MouseMove in then out.
    {
        Button b = makeButton();
        widgets::handle(b, move(ix, iy));
        check(b.hover, "hover set when moving inside");
        widgets::handle(b, move(ox, oy));
        check(!b.hover, "hover cleared when moving outside");
    }

    // 7. visualState transitions.
    {
        Button b = makeButton();
        check(widgets::visualState(b) == Visual::Normal, "fresh button is Normal");
        widgets::handle(b, move(ix, iy));
        check(widgets::visualState(b) == Visual::Hover, "hover -> Hover");
        widgets::handle(b, down(ix, iy));
        check(widgets::visualState(b) == Visual::Pressed, "armed+hover -> Pressed");
        widgets::handle(b, move(ox, oy));  // drag off while held
        check(widgets::visualState(b) == Visual::Normal,
              "armed but dragged off -> Normal");
        b.enabled = false;
        check(widgets::visualState(b) == Visual::Disabled, "disabled -> Disabled");
    }

    if (g_fail == 0)
        std::printf("widgets self-check: OK — all push-button KATs passed\n");
    else
        std::printf("widgets self-check: FAILED (%d)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
