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

} // namespace widgetview
