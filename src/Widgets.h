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

// ── Text edit ────────────────────────────────────────────────────────────────
// A single-line text input. The *editing* model (content, caret, selection) is
// pure and UTF-8-correct and lives here; mouse focus and click-to-caret are the
// view's job (they need glyph metrics). `caret`/`anchor` are byte offsets into
// `text`; the selection is the half-open byte range [min(caret,anchor),
// max(caret,anchor)). When caret == anchor there is no selection.
struct TextEdit {
    Rect        bounds;
    std::string text;            // UTF-8 content
    bool        enabled = true;
    bool        focused = false; // receives keyboard input + draws a caret
    bool        hover   = false;
    size_t      caret   = 0;     // byte offset of the caret
    size_t      anchor  = 0;     // selection anchor (byte offset)
};

// UTF-8 boundary helpers: the byte offset of the char boundary before / after
// `byteIdx` (clamped to [0, size]). Exposed so the view can place the caret on a
// click without splitting a multibyte character.
size_t prevCharBoundary(const std::string& s, size_t byteIdx);
size_t nextCharBoundary(const std::string& s, size_t byteIdx);

// True when the field has a non-empty selection.
bool hasSelection(const TextEdit& t);
// The currently selected substring ("" when there is no selection).
std::string selectedText(const TextEdit& t);
// Set the caret to `byteIdx` (snapped to a char boundary); `extend` keeps the
// anchor (growing the selection) instead of collapsing it. For the view's
// click/drag caret placement.
void setCaret(TextEdit& t, size_t byteIdx, bool extend);

// Feed one event to a focused, enabled field: TextInput inserts at the caret
// (replacing any selection); Backspace/Delete edit; Left/Right/Home/End move the
// caret, with Shift extending the selection. Returns consumed=true when the field
// handled the event. Mouse events are ignored here (the view drives focus/caret).
InputResult handle(TextEdit& t, const platform::Event& ev);

// ── Combo (dropdown) ─────────────────────────────────────────────────────────
// A dropdown: a header showing the selected option, which on click opens a popup
// list below it. The open/select/highlight state machine and the popup geometry
// are pure here; the view draws the header + list. `itemHeight` defaults to the
// header height when 0. `highlight` is the keyboard/mouse-highlighted row while
// open; `selected` is the committed choice (-1 = none).
struct Combo {
    Rect                      bounds;       // closed header
    std::vector<std::string>  options;
    int    selected   = -1;
    bool   open       = false;
    bool   enabled    = true;
    bool   hover      = false;
    int    highlight  = -1;
    float  itemHeight = 0.0f;
};

// Popup geometry (pure, valid whether or not the combo is open).
float comboItemHeight(const Combo& c);            // itemHeight>0 ? itemHeight : bounds.h
Rect  comboPopupRect(const Combo& c);             // the whole open list, below the header
Rect  comboItemRect(const Combo& c, int index);   // one row's rect
int   comboItemAt(const Combo& c, float x, float y);  // row under (x,y), or -1

// The selected option text ("" when none).
std::string selectedText(const Combo& c);

// Header visual state (Disabled / Hover when open-or-hovered / Normal).
Visual visualState(const Combo& c);

// Feed one event. Closed: a left-click on the header opens it. Open: a left-click
// on a row commits that row (clicked=true) and closes; clicking the header again
// or outside closes; Up/Down move the highlight, Enter commits the highlight,
// Escape closes. Disabled is inert.
InputResult handle(Combo& c, const platform::Event& ev);

// ── ListBox (scrollable list) ────────────────────────────────────────────────
// A vertically scrolling list with single selection. Scroll/selection logic and
// row geometry are pure here; the view draws the clipped rows + scrollbar.
// `scroll` is the pixel offset from the top of the content; `itemHeight` defaults
// to 24 when 0. `highlight` is the mouse-hovered row (-1 = none).
struct ListBox {
    Rect                      bounds;
    std::vector<std::string>  options;
    int    selected   = -1;
    float  scroll     = 0.0f;
    float  itemHeight = 0.0f;
    bool   enabled    = true;
    bool   hover      = false;
    int    highlight  = -1;
};

float listItemHeight(const ListBox& l);      // itemHeight>0 ? itemHeight : 24
float listContentHeight(const ListBox& l);   // options.size() * itemHeight
float listMaxScroll(const ListBox& l);       // max(0, content - bounds.h)
int   listItemAt(const ListBox& l, float x, float y);  // row under (x,y), or -1
void  listClampScroll(ListBox& l);           // clamp scroll into [0, maxScroll]
void  listEnsureVisible(ListBox& l, int index);  // scroll so `index` is fully shown

// Feed one event: wheel scrolls (clamped), a left-click selects the row under the
// cursor (clicked=true), Up/Down move the selection (auto-scrolling to keep it
// visible), Home/End jump to first/last, Enter re-fires the selection. Disabled
// is inert.
InputResult handle(ListBox& l, const platform::Event& ev);

} // namespace widgets
