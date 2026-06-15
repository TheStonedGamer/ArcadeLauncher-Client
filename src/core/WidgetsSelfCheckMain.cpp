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
using platform::Key;
using widgets::Button;
using widgets::Checkbox;
using widgets::Combo;
using widgets::TextEdit;
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
Event typeText(const char* s) {
    Event e; e.type = EventType::TextInput; e.text = s; return e;
}
Event keyDown(Key k, bool shift = false) {
    Event e; e.type = EventType::KeyDown; e.key = k; e.shift = shift; return e;
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

    // ── Checkbox: same push contract, but a completed click toggles `checked`.
    auto makeCheck = []() {
        Checkbox c;
        c.bounds = {100, 50, 200, 28};  // x:[100,300) y:[50,78)
        c.label = "Enable thing";
        return c;
    };

    // 8. Full click toggles off->on, and a second click toggles on->off.
    {
        Checkbox c = makeCheck();
        widgets::handle(c, down(110, 60));
        auto u = widgets::handle(c, up(110, 60));
        check(u.clicked && c.checked, "checkbox: first click checks it");
        widgets::handle(c, down(110, 60));
        auto u2 = widgets::handle(c, up(110, 60));
        check(u2.clicked && !c.checked, "checkbox: second click unchecks it");
    }

    // 9. Release-away does NOT toggle (press started inside, released outside).
    {
        Checkbox c = makeCheck();
        widgets::handle(c, down(110, 60));
        auto u = widgets::handle(c, up(900, 900));
        check(!u.clicked && !c.checked, "checkbox: release away does not toggle");
    }

    // 10. Disabled checkbox never toggles.
    {
        Checkbox c = makeCheck();
        c.enabled = false;
        widgets::handle(c, down(110, 60));
        auto u = widgets::handle(c, up(110, 60));
        check(!u.consumed && !c.checked, "checkbox: disabled never toggles");
    }

    // 11. visualState mirrors the button mapping.
    {
        Checkbox c = makeCheck();
        check(widgets::visualState(c) == Visual::Normal, "checkbox fresh -> Normal");
        widgets::handle(c, move(110, 60));
        check(widgets::visualState(c) == Visual::Hover, "checkbox hover -> Hover");
        widgets::handle(c, down(110, 60));
        check(widgets::visualState(c) == Visual::Pressed, "checkbox armed -> Pressed");
    }

    // ── TextEdit: pure UTF-8 editing model.
    auto eqs = [](const char* what, const std::string& got, const std::string& want) {
        if (got != want) {
            std::printf("  FAIL %s: got \"%s\" want \"%s\"\n", what, got.c_str(),
                        want.c_str());
            ++g_fail;
        }
    };
    auto focused = []() { TextEdit t; t.focused = true; return t; };

    // 12. Typing inserts at the caret; backspace deletes the char before it.
    {
        TextEdit t = focused();
        widgets::handle(t, typeText("a"));
        widgets::handle(t, typeText("b"));
        widgets::handle(t, typeText("c"));
        eqs("typing", t.text, "abc");
        check(t.caret == 3, "caret at end after typing");
        widgets::handle(t, keyDown(Key::Backspace));
        eqs("backspace", t.text, "ab");
        check(t.caret == 2, "caret follows backspace");
    }

    // 13. Caret movement + Delete in the middle.
    {
        TextEdit t = focused();
        widgets::handle(t, typeText("abcd"));
        widgets::handle(t, keyDown(Key::Left));
        widgets::handle(t, keyDown(Key::Left));   // caret between b and c (idx 2)
        check(t.caret == 2, "two Lefts -> caret 2");
        widgets::handle(t, keyDown(Key::Delete));  // removes 'c'
        eqs("delete forward", t.text, "abd");
        widgets::handle(t, keyDown(Key::Home));
        check(t.caret == 0, "Home -> caret 0");
        widgets::handle(t, keyDown(Key::End));
        check(t.caret == t.text.size(), "End -> caret at size");
    }

    // 14. Shift+Left selects; typing replaces the selection.
    {
        TextEdit t = focused();
        widgets::handle(t, typeText("hello"));
        widgets::handle(t, keyDown(Key::Left, true));
        widgets::handle(t, keyDown(Key::Left, true));  // select last "lo"
        check(widgets::hasSelection(t), "shift+left makes a selection");
        eqs("selected text", widgets::selectedText(t), "lo");
        widgets::handle(t, typeText("p"));             // replace selection
        eqs("type over selection", t.text, "help");
        check(!widgets::hasSelection(t), "selection cleared after replace");
    }

    // 15. UTF-8: a 2-byte char is inserted and removed whole (no split).
    {
        TextEdit t = focused();
        widgets::handle(t, typeText("a"));
        widgets::handle(t, typeText("\xC3\xA9"));  // 'é' (U+00E9, 2 bytes)
        widgets::handle(t, typeText("b"));
        eqs("utf8 insert", t.text, "a\xC3\xA9" "b");
        check(t.caret == 4, "caret counts bytes (1+2+1)");
        widgets::handle(t, keyDown(Key::Left));     // move before 'b'
        widgets::handle(t, keyDown(Key::Backspace)); // delete whole 'é'
        eqs("utf8 backspace removes whole char", t.text, "ab");
        check(t.caret == 1, "caret moved back by 2 bytes");
    }

    // 16. An unfocused or disabled field ignores input.
    {
        TextEdit t;  // not focused
        auto r = widgets::handle(t, typeText("x"));
        check(!r.consumed && t.text.empty(), "unfocused field ignores typing");
        TextEdit d = focused(); d.enabled = false;
        widgets::handle(d, typeText("x"));
        check(d.text.empty(), "disabled field ignores typing");
    }

    // ── Combo (dropdown): open/select via mouse and keyboard.
    auto makeCombo = []() {
        Combo c;
        c.bounds = {100, 50, 160, 28};       // header; items 28px each below it
        c.options = {"Low", "Medium", "High"};
        return c;
    };

    // 17. Click header opens; click a row commits it and closes.
    {
        Combo c = makeCombo();
        auto o = widgets::handle(c, down(110, 60));   // inside header
        check(o.consumed && c.open, "combo: header click opens");
        // Rows are 28px tall starting at y=78: row0 [78,106), row1 [106,134).
        // Click the middle of row 1 ("Medium") at y=120.
        auto pick = widgets::handle(c, down(110, 120));
        check(pick.clicked && c.selected == 1 && !c.open,
              "combo: row click commits + closes");
        eqs("combo selected text", widgets::selectedText(c), "Medium");
    }

    // 18. Clicking outside an open combo closes it without changing selection.
    {
        Combo c = makeCombo();
        widgets::handle(c, down(110, 60));   // open
        widgets::handle(c, down(900, 900));  // far outside
        check(!c.open && c.selected == -1, "combo: outside click closes, no change");
    }

    // 19. Keyboard: Down moves highlight, Enter commits it.
    {
        Combo c = makeCombo();
        widgets::handle(c, down(110, 60));        // open; highlight = selected(-1)
        widgets::handle(c, keyDown(Key::Down));   // -> 0
        widgets::handle(c, keyDown(Key::Down));   // -> 1
        check(c.highlight == 1, "combo: two Downs -> highlight 1");
        auto e = widgets::handle(c, keyDown(Key::Enter));
        check(e.clicked && c.selected == 1 && !c.open, "combo: Enter commits highlight");
    }

    // 20. Down highlight clamps at the last row; Escape closes without committing.
    {
        Combo c = makeCombo();
        widgets::handle(c, down(110, 60));
        for (int i = 0; i < 9; ++i) widgets::handle(c, keyDown(Key::Down));
        check(c.highlight == 2, "combo: Down clamps at last row");
        widgets::handle(c, keyDown(Key::Escape));
        check(!c.open && c.selected == -1, "combo: Escape closes, no commit");
    }

    // 21. Disabled combo never opens.
    {
        Combo c = makeCombo();
        c.enabled = false;
        widgets::handle(c, down(110, 60));
        check(!c.open, "combo: disabled never opens");
    }

    if (g_fail == 0)
        std::printf("widgets self-check: OK — button + checkbox + textedit + combo KATs passed\n");
    else
        std::printf("widgets self-check: FAILED (%d)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
