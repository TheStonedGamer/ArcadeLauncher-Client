#pragma once
#include "pch.h"

// In-launcher Dolphin configuration. We read/write Dolphin's own INI files
// (GFX.ini, Dolphin.ini, GCPadNew.ini, WiimoteNew.ini) in the user directory
// Dolphin itself resolves on Windows — a portable "User" folder next to the exe
// when present, otherwise %USERPROFILE%\Documents\Dolphin Emulator. Every write
// preserves keys/sections the launcher doesn't manage.

struct DolphinSettings {
    // ── Graphics / Enhancements ──────────────────────────────────────────────
    std::wstring backend     = L"D3D";   // Dolphin.ini [Core] GFXBackend:
                                         //   D3D, D3D12, Vulkan, OGL, Software Renderer
    bool   fullscreen        = false;    // Dolphin.ini [Display] Fullscreen
    bool   vsync             = true;     // GFX.ini    [Hardware] VSync
    int    aspectRatio       = 0;        // GFX.ini    [Settings] AspectRatio
                                         //   0 Auto, 1 Force 16:9, 2 Force 4:3, 3 Stretch
    int    internalRes       = 1;        // GFX.ini    [Settings] InternalResolution
                                         //   0 Auto, 1 1x(native) … 8 8x
    int    msaa              = 1;        // GFX.ini    [Settings] MSAA sample count (1/2/4/8)
    bool   ssaa              = false;    // GFX.ini    [Settings] SSAA
    int    maxAnisotropy     = 0;        // GFX.ini    [Settings] MaxAnisotropy
                                         //   0 1x, 1 2x, 2 4x, 3 8x, 4 16x

    // ── Core / General ───────────────────────────────────────────────────────
    bool   dualCore          = true;     // Dolphin.ini [Core] CPUThread
    bool   enableCheats      = false;    // Dolphin.ini [Core] EnableCheats
    bool   overclockEnable   = false;    // Dolphin.ini [Core] OverclockEnable
    int    overclockPercent  = 100;      // Dolphin.ini [Core] Overclock (= percent/100.0)
    int    gcLanguage        = 0;        // Dolphin.ini [Core] SelectedLanguage
                                         //   0 EN, 1 DE, 2 FR, 3 ES, 4 IT, 5 NL

    // ── Audio ────────────────────────────────────────────────────────────────
    std::wstring audioBackend = L"Cubeb";// Dolphin.ini [DSP] Backend
    int    volume            = 100;      // Dolphin.ini [DSP] Volume (0..100)
    bool   dspHLE            = true;     // Dolphin.ini [Core] DSPHLE (HLE vs LLE)
};

// Resolve Dolphin's user directory the way Dolphin does on Windows.
std::wstring DolphinResolveUserDir(const std::wstring& dolphinExe);

// Populate `out` from the on-disk INI files (defaults for any missing keys).
void DolphinLoadSettings(const std::wstring& dolphinExe, DolphinSettings& out);

// Write `s` back into the INI files, preserving all unmanaged content.
// Returns false on I/O failure.
bool DolphinApplySettings(const std::wstring& dolphinExe, const DolphinSettings& s);

// Write a standard XInput gamepad mapping to GCPadNew.ini ([GCPad1]); when
// `alsoWiimote` is set, also map an emulated Wiimote ([Wiimote1]). Existing
// content for other ports is preserved. Returns false on I/O failure.
bool DolphinWriteGamepadPreset(const std::wstring& dolphinExe, bool alsoWiimote,
                               std::wstring& err);
