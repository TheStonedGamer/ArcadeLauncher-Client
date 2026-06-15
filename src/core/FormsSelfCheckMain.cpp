// FormsSelfCheckMain.cpp — headless KAT for the portable settings-panel model
// (Linux port L4-f). Locks forms::Panel: row layout geometry, fieldAt hit-test
// (including an open combo's overlay popup), Tab focus traversal (wrap + skip
// disabled), mouse click→toggle+focus, keyboard text entry, keyboard activation
// (Space toggles a checkbox, Enter clicks a button, Enter opens a combo), and
// keyed value readback. Exit 0 on success. No display/GL — pure logic.

#include "Forms.h"

#include <cstdio>
#include <string>

namespace {
using platform::Event;
using platform::EventType;
using platform::Key;
using platform::MouseButton;

int g_fail = 0;

void ck(const char* what, bool cond) {
    if (!cond) { std::printf("  FAIL %s\n", what); ++g_fail; }
}
void eqf(const char* what, float got, float want) {
    const float d = got - want;
    if (d > 0.01f || d < -0.01f) {
        std::printf("  FAIL %s: got %.2f want %.2f\n", what, got, want);
        ++g_fail;
    }
}
void eqs(const char* what, const std::string& got, const std::string& want) {
    if (got != want) {
        std::printf("  FAIL %s: got \"%s\" want \"%s\"\n", what, got.c_str(), want.c_str());
        ++g_fail;
    }
}

Event mouse(EventType t, float x, float y) {
    Event e; e.type = t; e.x = x; e.y = y; e.button = MouseButton::Left; return e;
}
Event key(Key k, bool shift = false) {
    Event e; e.type = EventType::KeyDown; e.key = k; e.shift = shift; return e;
}
Event text(const char* s) { Event e; e.type = EventType::TextInput; e.text = s; return e; }

// A panel mirroring a slice of the Windows General/Steam/Dolphin settings: two
// behavior checkboxes, a path edit, a backend combo, and a Browse button.
forms::Panel makePanel() {
    forms::Panel p;
    p.bounds = {200, 100, 560, 400};
    p.rowHeight = 40; p.labelWidth = 240; p.controlH = 28; p.padX = 12;
    forms::addCheckbox(p, "fullscreen", "Start fullscreen");
    forms::addCheckbox(p, "tray",       "Minimize to tray when a game launches");
    forms::addTextEdit(p, "steamPath",  "Steam root path", "");
    forms::addCombo   (p, "backend",    "Graphics backend",
                       {"D3D11", "D3D12", "Vulkan", "OpenGL"}, 0);
    forms::addButton  (p, "browse",     "Browse\xE2\x80\xA6");  // "Browse…"
    forms::layout(p);
    return p;
}
} // namespace

int main() {
    // 1. Layout geometry: checkbox row spans the inset width; non-checkbox rows
    //    place the control in the right column past labelWidth.
    {
        forms::Panel p = makePanel();
        const auto& cb = p.fields[0].checkbox.bounds;   // row 0
        eqf("cb.x", cb.x, 212);  eqf("cb.y", cb.y, 106);
        eqf("cb.w", cb.w, 536);  eqf("cb.h", cb.h, 28);
        const auto& te = p.fields[2].textedit.bounds;   // row 2
        eqf("te.x", te.x, 440);  eqf("te.y", te.y, 186);
        eqf("te.w", te.w, 308);  eqf("te.h", te.h, 28);
    }

    // 2. fieldAt hit-test over closed controls, and -1 outside any row.
    {
        forms::Panel p = makePanel();
        ck("fieldAt checkbox0", forms::fieldAt(p, 222, 120) == 0);
        ck("fieldAt textedit2", forms::fieldAt(p, 450, 190) == 2);
        ck("fieldAt outside",   forms::fieldAt(p, 205, 600) == -1);
    }

    // 3. Tab focus traversal wraps; the focused TextEdit gets focused=true.
    {
        forms::Panel p = makePanel();
        ck("focus starts -1", p.focused == -1);
        forms::focusNext(p); ck("focus->0", p.focused == 0);
        forms::focusNext(p); ck("focus->1", p.focused == 1);
        forms::focusNext(p); ck("focus->2", p.focused == 2);
        ck("textedit focused flag", p.fields[2].textedit.focused);
        forms::focusNext(p); ck("focus->3", p.focused == 3);
        forms::focusNext(p); ck("focus->4", p.focused == 4);
        forms::focusNext(p); ck("focus wraps to 0", p.focused == 0);
        forms::focusPrev(p); ck("focusPrev wraps to 4", p.focused == 4);
    }

    // 4. Disabled fields are skipped by traversal.
    {
        forms::Panel p = makePanel();
        p.fields[1].checkbox.enabled = false;   // disable the "tray" checkbox
        forms::setFocus(p, 0);
        forms::focusNext(p);
        ck("traversal skips disabled", p.focused == 2);
    }

    // 5. A mouse click toggles a checkbox AND moves focus to that row.
    {
        forms::Panel p = makePanel();
        forms::handle(p, mouse(EventType::MouseDown, 222, 120));
        forms::PanelResult up = forms::handle(p, mouse(EventType::MouseUp, 222, 120));
        ck("click toggled checkbox0", forms::boolValue(p, "fullscreen"));
        ck("click set focus to 0", p.focused == 0);
        ck("click reported clicked=0", up.clicked == 0);
    }

    // 6. Typing into the focused text field flows through the editing model.
    {
        forms::Panel p = makePanel();
        forms::setFocus(p, 2);
        forms::handle(p, text("/"));
        forms::handle(p, text("g")); forms::handle(p, text("o"));
        eqs("typed text", forms::textValue(p, "steamPath"), "/go");
    }

    // 7. Keyboard activation: Space toggles a focused checkbox; Enter "clicks" a
    //    focused button.
    {
        forms::Panel p = makePanel();
        forms::setFocus(p, 1);
        forms::PanelResult r = forms::handle(p, key(Key::Space));
        ck("Space toggled checkbox1", forms::boolValue(p, "tray"));
        ck("Space reported click=1", r.clicked == 1);

        forms::setFocus(p, 4);
        forms::PanelResult b = forms::handle(p, key(Key::Enter));
        ck("Enter clicked button", b.clicked == 4);
    }

    // 8. Keyboard combo: Enter opens it, Down moves the highlight, Enter commits.
    {
        forms::Panel p = makePanel();
        forms::setFocus(p, 3);
        forms::handle(p, key(Key::Enter));               // open (highlight = selected 0)
        ck("combo opened", p.fields[3].combo.open);
        forms::handle(p, key(Key::Down));                // highlight -> 1
        forms::handle(p, key(Key::Enter));               // commit highlight
        ck("combo closed after commit", !p.fields[3].combo.open);
        ck("combo committed index 1", forms::choiceValue(p, "backend") == 1);
    }

    // 9. An open combo's popup overlays the rows below: a click in the dropped
    //    list commits that combo (not the button geometrically beneath it).
    {
        forms::Panel p = makePanel();
        forms::setFocus(p, 3);
        forms::handle(p, key(Key::Enter));               // open the combo
        const platform::Rect row2 = widgets::comboItemRect(p.fields[3].combo, 2);
        forms::PanelResult r =
            forms::handle(p, mouse(EventType::MouseDown,
                                   row2.x + 10, row2.y + row2.h * 0.5f));
        ck("popup click commits combo row", r.clicked == 3);
        ck("popup click selected index 2", forms::choiceValue(p, "backend") == 2);
        ck("popup click closed combo", !p.fields[3].combo.open);
    }

    // 10. Readback type-safety: wrong key / wrong kind return the supplied default.
    {
        forms::Panel p = makePanel();
        ck("missing bool default", forms::boolValue(p, "nope", true) == true);
        eqs("wrong-kind text default", forms::textValue(p, "backend", "x"), "x");
        ck("wrong-kind choice default", forms::choiceValue(p, "steamPath", -7) == -7);
    }

    if (g_fail == 0)
        std::printf("forms self-check: OK — layout + hit-test + focus + mouse + "
                    "keyboard + readback KATs passed\n");
    else
        std::printf("forms self-check: FAILED (%d)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
