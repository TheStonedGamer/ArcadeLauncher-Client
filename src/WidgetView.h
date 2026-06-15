#pragma once
// WidgetView.h — portable widget *drawing* (Linux port L4-a). Pairs with Widgets
// (state/logic) the same way CatalogGridView pairs with GridLayout: Widgets says
// what state a control is in, WidgetView says how it's painted — entirely through
// platform::IRenderer2D primitives, so it links into both arcade_gui (Linux,
// nanovg/GL) and the Windows build (Direct2D IRenderer2D). The Linux gui_demo
// draws through this today; the SettingsWindow/dialogs adopt it as the port's
// retained UI replaces the Win32 controls.

#include "Platform/Renderer2D.h"
#include "Widgets.h"

namespace widgetview {

// Palette + font a button draws with. One color per Visual state, plus label
// colors and the corner radius. `fontPx` is the loaded size of `font`, needed to
// vertically center the label (nanovg aligns text to its top).
struct Theme {
    platform::Color normal, hover, pressed, disabled;       // fill per state
    platform::Color border;                                 // 1px outline
    platform::Color accent;                                 // checked box / emphasis
    platform::Color text, textDisabled;                     // label colors
    platform::FontId font   = 0;
    float            fontPx = 15.0f;
    float            radius = 6.0f;
};

// The GitHub-dark button theme matching the rest of the launcher (font left 0 —
// the caller fills in font/fontPx after loading).
Theme darkTheme();

// Draw `b` into its bounds: a rounded fill chosen by widgets::visualState, a 1px
// border, and the centered label.
void drawButton(platform::IRenderer2D& r, const widgets::Button& b, const Theme& th);

// Draw `c`: a square box (left-aligned, vertically centered in bounds) whose fill
// follows visualState, an accent fill + checkmark when checked, and the label to
// the right. Uses the same Theme (accent box reuses `border`/`text`).
void drawCheckbox(platform::IRenderer2D& r, const widgets::Checkbox& c, const Theme& th);

// Draw a single-line text field: rounded box (accent border when focused), the
// selection highlight, the text clipped to the inner area, and a caret when
// focused. Glyph metrics come from the renderer, so the caret/selection line up
// with what's drawn.
void drawTextEdit(platform::IRenderer2D& r, const widgets::TextEdit& t, const Theme& th);

// Map a click x (pixels) to a caret byte offset within `t`, using the same font
// metrics drawTextEdit uses (snaps to char boundaries). For the view's MouseDown
// focus + caret placement.
size_t caretIndexFromX(platform::IRenderer2D& r, const widgets::TextEdit& t,
                       const Theme& th, float xPx);

// Draw a dropdown: the header (selected text + chevron) and, when open, the popup
// list (highlighted row uses the accent). Because the open list overlays other
// content, draw open combos LAST in the frame. `placeholder` shows when nothing
// is selected.
void drawCombo(platform::IRenderer2D& r, const widgets::Combo& c, const Theme& th,
               const std::string& placeholder = "Select…");

// Draw a scrollable list: box + border, rows clipped to the box (selected row =
// accent fill, hovered row = subtle tint), and a scrollbar thumb when the content
// overflows. Uses the renderer's clip stack so partial rows are cut cleanly.
void drawListBox(platform::IRenderer2D& r, const widgets::ListBox& l, const Theme& th);

} // namespace widgetview
