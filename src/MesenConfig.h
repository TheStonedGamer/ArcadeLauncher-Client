#pragma once
#include "pch.h"

// In-launcher configuration for Mesen2 (NES / SNES, also GB/GBA/PCE/SMS). Mesen2
// stores a nested, pretty-printed settings.json (one key per line). We rewrite
// only the value token of existing scalar keys inside the named sub-objects
// ("Video", "Audio", "Emulation", "Preferences", and the per-console object),
// preserving the UTF-8 BOM, indentation, trailing commas and every other key.
//
// Mesen2 handles NES and SNES with the same executable + settings file, so the
// NES and SNES settings pages share the global Video/Audio options and differ
// only in which console object's Region they edit.
struct MesenSettings {
    // Video
    std::wstring aspectRatio = L"NoStretching"; // NoStretching/Auto/Standard/Widescreen
    std::wstring videoFilter = L"None";         // None/NtscBlargg/NtscBisqwit/LcdGrid
    bool verticalSync           = false;
    bool bilinear               = false;
    bool integerFpsMode         = false;
    bool fullscreenIntegerScale = false;
    bool exclusiveFullscreen    = false;
    // Audio
    int  masterVolume     = 100;   // 0..100
    int  audioLatency     = 60;    // ms
    bool enableAudio      = true;
    bool muteInBackground = false;
    // Emulation / Preferences (global)
    int  runAheadFrames = 0;       // 0..10
    bool enableRewind   = true;
    // Per-console
    std::wstring region = L"Auto"; // Auto/Ntsc/Pal/Dendy
};

// console must be "Nes" or "Snes". Returns the resolved settings.json path
// (portable next to the exe, else %USERPROFILE%\Documents\Mesen2), or empty.
std::wstring MesenConfigPath(const std::wstring& exePath);
void MesenLoadSettings(const std::wstring& exePath, const std::wstring& console,
                       MesenSettings& out);
bool MesenApplySettings(const std::wstring& exePath, const std::wstring& console,
                        const MesenSettings& s);
