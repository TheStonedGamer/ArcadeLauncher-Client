#pragma once
#include "pch.h"

// In-launcher configuration for xemu (original Xbox). xemu stores xemu.toml
// either next to the exe (portable) or under %APPDATA%\xemu\xemu. We edit the
// [display.*] tables in place, preserving the firmware/[sys] keys the launcher
// provisions. Strings are written single-quoted to match xemu's own writer.
struct XemuSettings {
    std::wstring renderer   = L"OPENGL";  // [display] renderer (NULL/OPENGL/VULKAN)
    int          renderScale= 1;          // [display.quality] surface_scale
    std::wstring aspect     = L"auto";    // [display.ui] aspect_ratio (native/auto/4x3/16x9)
    std::wstring fit        = L"scale";   // [display.ui] fit (center/scale/stretch)
    bool         fullscreen = false;      // [display.window] fullscreen_on_startup
    bool         vsync      = true;       // [display.window] vsync
};

std::wstring XemuConfigPath(const std::wstring& xemuExe);
void XemuLoadSettings(const std::wstring& xemuExe, XemuSettings& out);
bool XemuApplySettings(const std::wstring& xemuExe, const XemuSettings& s);
