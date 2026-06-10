#pragma once
#include "pch.h"

// Shared dark-mode theming for all the launcher's Win32 dialogs, so every dialog
// (sign-in, account, library, game edit, metadata picker, first-launch, settings)
// shares one consistent dark look with flat input fields instead of the default
// light backgrounds and white sunken edit borders.
//
// Usage in a dialog:
//   1. After creating the window + all child controls:
//          dark::EnableTitleBar(hwnd);
//          dark::Apply(hwnd);          // strips white edit borders, themes children
//   2. At the top of the window procedure:
//          if (LRESULT r; dark::OnCtlColor(msg, wp, lp, r)) return r;
//   3. (Optional) paint the background in WM_ERASEBKGND with dark::BgBrush().
namespace dark {

// ── Palette ───────────────────────────────────────────────────────────────────
constexpr COLORREF BG      = RGB(32,  32,  34);   // window / dialog background
constexpr COLORREF PANEL   = RGB(48,  48,  52);   // input field / list background
constexpr COLORREF TEXT    = RGB(236, 236, 236);  // primary text
constexpr COLORREF SUBTEXT = RGB(168, 168, 172);  // secondary text
constexpr COLORREF ACCENT  = RGB( 0,  120, 215);  // selection / focus accent
constexpr COLORREF BORDER  = RGB(64,  64,  68);   // hairline separators

// Process-lifetime cached brushes (never deleted — fine for app shutdown).
HBRUSH BgBrush();
HBRUSH PanelBrush();

// Switch the title bar / window frame to dark via DWM. Safe on older Windows
// (the attribute is simply ignored).
void EnableTitleBar(HWND hwnd);

// Theme a window and all of its child controls: strips the white WS_EX_CLIENTEDGE
// border off edit/list/combo controls (the source of the "non-uniform" boxes),
// and applies the dark Explorer theme so scrollbars and combo dropdowns match.
void Apply(HWND hwnd);

// Handle a WM_CTLCOLOR* message. Returns true (and sets `out` to the brush to
// return from the window proc) when the message was a control-color message.
bool OnCtlColor(UINT msg, WPARAM wp, LPARAM lp, LRESULT& out);

} // namespace dark
