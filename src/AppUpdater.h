#pragma once
#include "pch.h"

// Posted to the main window when a newer release is found on GitHub.
// WPARAM = 0, LPARAM = AppUpdateInfo* (receiver must delete).
static constexpr UINT WM_APP_UPDATE_FOUND = WM_USER + 5;

// Posted to the main window when the MSI has been handed off to msiexec.
// WPARAM = 0 (success) or 1 (download failed), LPARAM = 0.
static constexpr UINT WM_APP_UPDATE_READY = WM_USER + 6;

// Posted repeatedly while the MSI downloads. WPARAM = percent (0-100), LPARAM = 0.
static constexpr UINT WM_APP_UPDATE_PROGRESS = WM_USER + 9;

// Posted only after a *manual* check (Tools → Check for Updates) that did not
// start an update, so the user always gets feedback.
// WPARAM = 0 (already up to date) or 1 (check failed), LPARAM = 0.
static constexpr UINT WM_APP_UPDATE_NONE = WM_USER + 10;

struct AppUpdateInfo {
    std::wstring tag;     // e.g. L"client-v1.2.3"
    std::wstring msiUrl;  // direct download URL for ArcadeLauncher-Server-Client-x64.msi
};

// Fires a background thread that checks GitHub for a newer release.
// Posts WM_APP_UPDATE_FOUND if one is found. An automatic check (manual=false)
// is silent on failure / up-to-date and honors the loop-breaker cooldown; a
// manual check (manual=true) bypasses the cooldown and posts WM_APP_UPDATE_NONE
// when no update is started so the caller can report the result.
void CheckForAppUpdateAsync(HWND hwnd, bool manual = false);

// Fires a background thread that downloads the MSI and hands it to an elevated
// installer helper. Posts WM_APP_UPDATE_READY when done (success or failure).
void DownloadAndInstallAsync(HWND hwnd, std::wstring tag, std::wstring msiUrl);
