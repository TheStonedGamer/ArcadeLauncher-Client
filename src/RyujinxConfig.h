#pragma once
#include "pch.h"

// In-launcher configuration for Ryujinx (Nintendo Switch). Ryujinx stores a
// flat, pretty-printed Config.json (one key per line) under %APPDATA%\Ryujinx.
// We rewrite only the value token of existing top-level scalar keys, preserving
// indentation, trailing commas, the file's nested input maps and every other
// key untouched — so editing can never restructure the document.
struct RyujinxSettings {
    std::wstring graphicsBackend  = L"Vulkan";   // "Vulkan" | "OpenGl"
    int          resScale         = 1;           // res_scale (1..4; -1 = custom)
    int          maxAnisotropy    = -1;          // -1 = auto, else 2/4/8/16
    std::wstring aspectRatio      = L"Fixed16x9";// Fixed4x3/16x9/16x10/21x9/32x9/Stretched
    std::wstring antiAliasing     = L"None";     // None/Fxaa/SmaaLow/Medium/High/Ultra
    std::wstring backendThreading = L"Auto";     // Auto/Off/On
    std::wstring audioBackend     = L"SDL2";     // Dummy/OpenAl/SoundIo/SDL2
    int          audioVolume      = 100;         // 0..100 (stored as 0..1 float)
    bool         enableVsync          = true;
    bool         shaderCache          = true;
    bool         textureRecompression = false;
    bool         macroHle             = true;
    bool         startFullscreen      = false;
    bool         dockedMode           = true;
};

std::wstring RyujinxConfigPath();
void RyujinxLoadSettings(RyujinxSettings& out);
bool RyujinxApplySettings(const RyujinxSettings& s);
