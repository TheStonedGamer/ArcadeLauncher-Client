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

struct AppUpdateInfo {
    std::wstring tag;     // e.g. L"client-v1.2.3"
    std::wstring msiUrl;  // direct download URL for ArcadeLauncher-Server-Client-x64.msi
};

// Fires a background thread that checks GitHub for a newer release.
// Posts WM_APP_UPDATE_FOUND if one is found; silent on failure / up-to-date.
void CheckForAppUpdateAsync(HWND hwnd);

// Fires a background thread that downloads the MSI and hands it to msiexec.
// Posts WM_APP_UPDATE_READY when done (success or failure).
void DownloadAndInstallAsync(HWND hwnd, std::wstring msiUrl);
