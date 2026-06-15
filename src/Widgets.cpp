// Widgets.cpp — portable retained widget logic (Linux port L4). Pure of any
// rendering back-end. Button and Checkbox share one press/release contract.

#include "Widgets.h"

namespace widgets {

using platform::EventType;
using platform::MouseButton;

namespace {

// The shared push contract for any control with bounds/enabled/hover/pressed:
// arm on a left-down inside, and on the matching left-up report consumed, plus
// clicked when the release is also inside. Disabled is inert. Returns the result;
// the caller decides what a click *does* (fire, toggle, …).
template <typename W>
InputResult pushContract(W& w, const platform::Event& ev) {
    InputResult res;
    if (!w.enabled) {
        w.hover = false;
        w.pressed = false;
        return res;
    }
    switch (ev.type) {
        case EventType::MouseMove:
            w.hover = contains(w.bounds, ev.x, ev.y);
            break;
        case EventType::MouseDown:
            if (ev.button == MouseButton::Left &&
                contains(w.bounds, ev.x, ev.y)) {
                w.pressed = true;
                w.hover = true;
                res.consumed = true;
            }
            break;
        case EventType::MouseUp:
            if (ev.button == MouseButton::Left && w.pressed) {
                const bool inside = contains(w.bounds, ev.x, ev.y);
                w.pressed = false;
                w.hover = inside;
                res.consumed = true;
                res.clicked = inside;
            }
            break;
        default:
            break;
    }
    return res;
}

template <typename W>
Visual visualOf(const W& w) {
    if (!w.enabled) return Visual::Disabled;
    if (w.pressed && w.hover) return Visual::Pressed;
    if (w.hover) return Visual::Hover;
    return Visual::Normal;
}

} // namespace

InputResult handle(Button& b, const platform::Event& ev) {
    return pushContract(b, ev);
}

Visual visualState(const Button& b) { return visualOf(b); }

InputResult handle(Checkbox& c, const platform::Event& ev) {
    InputResult res = pushContract(c, ev);
    if (res.clicked) c.checked = !c.checked;  // a completed click toggles
    return res;
}

Visual visualState(const Checkbox& c) { return visualOf(c); }

} // namespace widgets
