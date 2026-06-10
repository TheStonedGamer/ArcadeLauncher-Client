#include "pch.h"
#include "DarkTheme.h"
#include <dwmapi.h>
#include <uxtheme.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace dark {

HBRUSH BgBrush() {
    static HBRUSH b = CreateSolidBrush(BG);
    return b;
}
HBRUSH PanelBrush() {
    static HBRUSH b = CreateSolidBrush(PANEL);
    return b;
}

void EnableTitleBar(HWND hwnd) {
    BOOL on = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &on, sizeof(on));
}

static void ThemeChild(HWND child) {
    wchar_t cls[64] = {};
    GetClassNameW(child, cls, 63);

    // Drop the white sunken 3D border that makes input boxes (and framed labels)
    // look mismatched against a dark dialog; they read as flat dark panels instead.
    LONG_PTR ex = GetWindowLongPtrW(child, GWL_EXSTYLE);
    if (ex & WS_EX_CLIENTEDGE) {
        SetWindowLongPtrW(child, GWL_EXSTYLE, ex & ~WS_EX_CLIENTEDGE);
        SetWindowPos(child, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }

    if (_wcsicmp(cls, L"EDIT") == 0 || _wcsicmp(cls, L"LISTBOX") == 0) {
        SetWindowTheme(child, L"DarkMode_Explorer", nullptr);  // dark scrollbars
    } else if (_wcsicmp(cls, L"COMBOBOX") == 0) {
        SetWindowTheme(child, L"DarkMode_CFD", nullptr);
    } else if (_wcsicmp(cls, L"BUTTON") == 0) {
        // Themes checkbox/radio glyphs dark; plain push buttons stay system-drawn
        // but pick up the dark text/background via WM_CTLCOLORBTN.
        SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
    } else {
        SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
    }
}

void Apply(HWND hwnd) {
    EnumChildWindows(hwnd, [](HWND child, LPARAM) -> BOOL {
        ThemeChild(child);
        return TRUE;
    }, 0);
}

bool OnCtlColor(UINT msg, WPARAM wp, LPARAM lp, LRESULT& out) {
    switch (msg) {
    case WM_CTLCOLORDLG: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, BG);
        SetTextColor(hdc, TEXT);
        out = (LRESULT)BgBrush();
        return true;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        // Labels, group boxes, checkboxes — text drawn over the dialog background.
        HDC hdc = (HDC)wp;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, TEXT);
        SetBkColor(hdc, BG);
        out = (LRESULT)BgBrush();
        return true;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        // Input fields and lists — flat dark panel.
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, PANEL);
        SetTextColor(hdc, TEXT);
        out = (LRESULT)PanelBrush();
        return true;
    }
    default:
        (void)lp;
        return false;
    }
}

} // namespace dark
