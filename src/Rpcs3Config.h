#pragma once
#include "pch.h"

// In-launcher configuration for RPCS3 (PlayStation 3). RPCS3 stores config.yml
// next to its exe (config\config.yml). The file is YAML with nested maps; we
// edit only top-level-section direct-child scalar keys in place, preserving
// every other line (comments, nested sub-maps, unmanaged keys).
struct Rpcs3Settings {
    std::wstring renderer      = L"Vulkan";    // Video: Renderer (Null/OpenGL/Vulkan)
    std::wstring resolution    = L"1280x720";  // Video: Resolution
    std::wstring aspect        = L"16:9";      // Video: Aspect ratio
    std::wstring frameLimit    = L"Auto";      // Video: Frame limit
    std::wstring msaa          = L"Auto";      // Video: MSAA (Disabled/Auto)
    bool         vsync         = false;        // Video: VSync Mode (Full/Disabled)
    int          resScale      = 100;          // Video: Resolution Scale (%)
    int          aniso         = 0;            // Video: Anisotropic Filter Override
    bool         writeColorBuf = false;        // Video: Write Color Buffers
    bool         stretchScreen = false;        // Video: Stretch To Display Area
    std::wstring audioRenderer = L"Cubeb";     // Audio: Renderer
    int          masterVolume  = 100;          // Audio: Master Volume
};

std::wstring Rpcs3ConfigPath(const std::wstring& rpcs3Exe);
void Rpcs3LoadSettings(const std::wstring& rpcs3Exe, Rpcs3Settings& out);
bool Rpcs3ApplySettings(const std::wstring& rpcs3Exe, const Rpcs3Settings& s);
