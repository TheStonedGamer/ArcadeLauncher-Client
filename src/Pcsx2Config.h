#pragma once
#include "pch.h"

// In-launcher configuration for PCSX2 (PlayStation 2). PCSX2 stores its config
// in PCSX2.ini — either next to the exe in an `inis\` folder when portable
// (portable.ini marker), or under Documents\PCSX2\inis otherwise. We edit that
// INI in place, preserving every unmanaged key/comment.
struct Pcsx2Settings {
    int          renderer    = -1;                 // [EmuCore/GS] Renderer (enum int)
    int          upscale     = 1;                  // [EmuCore/GS] upscale_multiplier
    std::wstring aspect      = L"Auto 4:3/3:2";    // [EmuCore/GS] AspectRatio (string)
    int          maxAniso    = 0;                  // [EmuCore/GS] MaxAnisotropy
    bool         vsync       = false;              // [EmuCore/GS] VsyncEnable
    bool         mipmap      = true;               // [EmuCore/GS] mipmap
    bool         fxaa        = false;              // [EmuCore/GS] fxaa
    bool         fullscreen  = false;              // [UI] StartFullscreen
    bool         cheats      = false;              // [EmuCore] EnableCheats
    bool         widescreen  = false;              // [EmuCore] EnableWideScreenPatches
    bool         fastBoot    = true;               // [EmuCore] EnableFastBoot
    std::wstring audioBackend= L"Cubeb";           // [SPU2/Output] Backend (string)
    int          volume      = 100;                // [SPU2/Output] StandardVolume
};

std::wstring Pcsx2ConfigPath(const std::wstring& pcsx2Exe);
void Pcsx2LoadSettings(const std::wstring& pcsx2Exe, Pcsx2Settings& out);
bool Pcsx2ApplySettings(const std::wstring& pcsx2Exe, const Pcsx2Settings& s);
