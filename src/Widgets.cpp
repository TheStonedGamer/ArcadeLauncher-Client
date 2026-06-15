// Widgets.cpp — portable retained widget logic (Linux port L4-a). The push-
// button interaction state machine; pure of any rendering back-end.

#include "Widgets.h"

namespace widgets {

using platform::EventType;
using platform::MouseButton;

InputResult handle(Button& b, const platform::Event& ev) {
    InputResult res;

    // A disabled button is inert: it never hovers, arms, or clicks.
    if (!b.enabled) {
        b.hover = false;
        b.pressed = false;
        return res;
    }

    switch (ev.type) {
        case EventType::MouseMove:
            b.hover = contains(b.bounds, ev.x, ev.y);
            break;

        case EventType::MouseDown:
            if (ev.button == MouseButton::Left &&
                contains(b.bounds, ev.x, ev.y)) {
                b.pressed = true;
                b.hover = true;
                res.consumed = true;
            }
            break;

        case EventType::MouseUp:
            // Only a release that completes an armed press is ours. Click fires
            // only when the release is also inside the button.
            if (ev.button == MouseButton::Left && b.pressed) {
                const bool inside = contains(b.bounds, ev.x, ev.y);
                b.pressed = false;
                b.hover = inside;
                res.consumed = true;
                res.clicked = inside;
            }
            break;

        default:
            break;
    }
    return res;
}

Visual visualState(const Button& b) {
    if (!b.enabled) return Visual::Disabled;
    if (b.pressed && b.hover) return Visual::Pressed;
    if (b.hover) return Visual::Hover;
    return Visual::Normal;
}

} // namespace widgets
