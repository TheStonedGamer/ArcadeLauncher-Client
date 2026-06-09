#pragma once
#include "pch.h"

// In-launcher configuration for Xenia Canary (Xbox 360). Xenia is portable and
// keeps xenia-canary.config.toml next to its exe. We edit that file in place,
// preserving every unmanaged key/comment.
struct XeniaSettings {
    std::wstring gpu        = L"any";   // [GPU] gpu: any / d3d12 / vulkan
    int  resScale           = 1;        // [GPU] draw_resolution_scale_x/_y (1..3)
    int  frameLimit         = 0;        // [GPU] framerate_limit (0 = unlimited)
    bool vsync              = true;     // [GPU] vsync
    bool fullscreen         = false;    // [Display] fullscreen
    bool letterbox          = true;     // [Display] present_letterbox
    std::wstring hid        = L"any";   // [HID] hid: any / xinput / sdl
    bool vibration          = true;     // [HID] vibration
};

std::wstring XeniaConfigPath(const std::wstring& xeniaExe);
void XeniaLoadSettings(const std::wstring& xeniaExe, XeniaSettings& out);
bool XeniaApplySettings(const std::wstring& xeniaExe, const XeniaSettings& s);
