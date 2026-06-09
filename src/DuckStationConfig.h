#pragma once
#include "pch.h"

// In-launcher configuration for DuckStation (PlayStation 1). DuckStation stores
// settings.ini next to the exe when portable (portable.txt), otherwise under
// %APPDATA%\DuckStation. Apply only writes into an existing settings.ini, so it
// is a no-op until DuckStation has been run once and created its config.
struct DuckSettings {
    std::wstring renderer   = L"Automatic";          // [GPU] Renderer
    int          resScale   = 1;                     // [GPU] ResolutionScale
    std::wstring dithering  = L"TrueColor";          // [GPU] DitheringMode
    bool         pgxp       = false;                 // [GPU] PGXPEnable
    std::wstring aspect     = L"Auto (Game Native)"; // [Display] AspectRatio
    bool         vsync      = false;                 // [Display] VSync
    bool         fullscreen = false;                 // [Main] StartFullscreen
    std::wstring audioBackend = L"Cubeb";            // [Audio] Backend
    int          volume     = 100;                   // [Audio] OutputVolume
};

std::wstring DuckConfigPath(const std::wstring& duckExe);
void DuckLoadSettings(const std::wstring& duckExe, DuckSettings& out);
bool DuckApplySettings(const std::wstring& duckExe, const DuckSettings& s);
