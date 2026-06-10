#pragma once
#include "pch.h"
#include "Config.h"

// ── Per-launch emulator / extras self-heal ───────────────────────────────────
// On every launch the client verifies that each configured emulator's exe is
// present and that the server-hosted "extras" (PS1 BIOS, OG Xbox firmware, PS3
// firmware) are in place — silently re-downloading anything missing from the
// server and (re)deploying emulator settings. Runs entirely on a detached
// thread; results are posted back to the main window so config writes + toasts
// happen on the UI thread.

// Identifies which emulator a download result applies to, so the UI thread can
// write the matching EmulatorConfig path/tag fields.
enum class EmuSlot {
    Dolphin, Ryujinx, Rpcs3, N64, NesSnes, Duckstation, Pcsx2, Xenia, Xemu
};

// Posted after an emulator was (re)installed. lParam = new AssetEmuResult*
// (the UI-thread handler takes ownership and deletes it).
static constexpr UINT WM_APP_ASSETS_EMU   = WM_USER + 112;
// Posted to surface a Steam-style toast. lParam = new AssetToast* (UI thread
// owns/deletes).
static constexpr UINT WM_APP_ASSETS_TOAST = WM_USER + 113;

struct AssetEmuResult {
    EmuSlot      slot;
    std::wstring exePath;   // located exe after install
    std::wstring tag;       // release tag (may be empty)
};

struct AssetToast {
    std::wstring title;
    std::wstring message;
};

// Builds the server emulators base URL, e.g. http://host:port/emulators/<name>.
// Centralized here so SettingsWindow / FirstLaunchSetup / AssetEnsure agree.
std::wstring EmulatorArchiveUrl(const AppConfig& cfg, const wchar_t* archiveName);

// Kick off the per-launch self-heal on a detached thread. Snapshots `cfg` by
// value (read-only on the worker); posts WM_APP_ASSETS_EMU / WM_APP_ASSETS_TOAST
// to `hwnd`. Safe to call once at startup; guarded against concurrent runs.
void EnsureServerAssetsAsync(HWND hwnd, AppConfig cfg, std::wstring appDataDir);
