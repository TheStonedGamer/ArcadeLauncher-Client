#pragma once
#include "pch.h"

// Posted to the settings window when a download completes.
// WPARAM = page index, LPARAM = new EmuDownloadResult* (receiver must delete).
static constexpr UINT WM_EMUDOWNLOAD_DONE = WM_USER + 51;

struct EmuDownloadResult {
    std::wstring exePath;   // empty = exe not found after extract; L"ERR:..." = failure
    std::wstring tag;       // GitHub release tag_name; empty if unavailable
};

struct EmulatorDownloadSpec {
    std::string  githubRepo;  // "owner/repo"
    std::wstring urlPattern;  // case-insensitive substring matched against asset download URL
    std::wstring exeName;     // exe filename to locate after extraction
    std::wstring destName;    // subfolder under %AppData%\GameLauncher\emulators
    std::wstring directUrl;    // optional direct archive/exe URL for non-GitHub release hosts
};

// Launches a background thread that fetches the latest GitHub release, downloads
// the matching asset, extracts it, and posts WM_EMUDOWNLOAD_DONE to hwnd.
void DownloadEmulatorAsync(HWND hwnd, int pageIdx, EmulatorDownloadSpec spec,
                           const std::wstring& appDataDir);
