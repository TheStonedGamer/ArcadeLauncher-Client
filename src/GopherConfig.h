#pragma once
#include "pch.h"

// In-launcher configuration for gopher64 (Nintendo 64). gopher64 stores
// config.json under %APPDATA%\gopher64. We edit the "video" and "emulation"
// scalar objects in place, line by line, preserving the large "input" map and
// every other key untouched.
struct GopherSettings {
    int  upscale            = 1;      // video.upscale (integer factor)
    bool ssaa               = false;  // video.ssaa
    bool integerScaling     = false;  // video.integer_scaling
    bool fullscreen         = false;  // video.fullscreen
    bool widescreen         = false;  // video.widescreen
    bool vsync              = true;   // video.vsync
    bool crt                = false;  // video.crt
    bool overclock          = false;  // emulation.overclock
    bool disableExpansionPak= false;  // emulation.disable_expansion_pak
    bool usb                = false;  // emulation.usb
};

std::wstring GopherConfigPath();
void GopherLoadSettings(GopherSettings& out);
bool GopherApplySettings(const GopherSettings& s);
