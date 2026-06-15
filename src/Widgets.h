#pragma once
// Widgets.h — portable (UTF-8, Win32-free) retained widget *logic* (Linux port
// L4-a). The launcher's settings/dialog screens are built from Win32 controls on
// Windows; the Linux port can't use those, so we grow one cross-platform widget
// set instead. This header is the foundation plus the first widget (a push
// button): the pure interaction state machine — hit-testing, hover/press
// tracking, and click detection — with no drawing. The matching drawing lives in
// widgetview (arcade_gui, IRenderer2D only); the two are paired the same way
// GridLayout (geometry) pairs with CatalogGridView (drawing).
//
// Keeping the state machine here, free of any back-end, lets one KAT
// (widgets_selfcheck) lock its behavior on both Windows and Linux, exactly like
// the grid/search/sort/filter modules. Widgets to follow (checkbox, combo,
// listbox, text-edit) build on these same primitives.

#include "Platform/Renderer2D.h"  // platform::Rect / Color (POD)
#include "Platform/Window.h"      // platform::Event (POD)

#include <string>

namespace widgets {

using platform::Rect;

// True when (x, y) lies inside r (left/top inclusive, right/bottom exclusive),
// matching the half-open convention the grid hit-tests use.
inline bool contains(const Rect& r, float x, float y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// Visual state a clickable widget resolves to, for the drawing layer to style.
enum class Visual { Normal, Hover, Pressed, Disabled };

// A push button. `bounds`/`label`/`enabled` are caller-owned; hover/pressed are
// runtime state the state machine maintains.
struct Button {
    Rect        bounds;
    std::string label;
    bool        enabled = true;
    bool        hover   = false;  // cursor is over the button
    bool        pressed = false;  // armed: mouse went down inside and is held
};

// What feeding one event to a widget produced.
struct InputResult {
    bool consumed = false;  // the widget handled (claimed) this event
    bool clicked  = false;  // a full press+release inside completed on this event
};

// Feed one input event to `b`. Updates hover/pressed and reports `clicked` on the
// left-button release that completes a press which *started* inside the button
// and *ends* inside it (the standard push-button contract: press away or release
// away never fires). A disabled button consumes nothing and clears its state.
// Pure — performs no drawing.
InputResult handle(Button& b, const platform::Event& ev);

// Map a button's runtime state to a Visual for the drawing layer. Pressed-but-
// dragged-off shows Normal (the press is still armed, but not under the cursor).
Visual visualState(const Button& b);

// A checkbox / toggle. Same press/release contract as Button, but a completed
// click flips `checked` (and InputResult::clicked reports the flip). `bounds` is
// the whole clickable row (box + label); the drawing layer places the box.
struct Checkbox {
    Rect        bounds;
    std::string label;
    bool        checked = false;
    bool        enabled = true;
    bool        hover   = false;
    bool        pressed = false;
};

// Feed one input event to `c`. On the left release that completes a press started
// inside, toggles `checked` and reports clicked == true. Disabled is inert.
InputResult handle(Checkbox& c, const platform::Event& ev);

// Runtime state → Visual, same mapping as the button.
Visual visualState(const Checkbox& c);

} // namespace widgets
