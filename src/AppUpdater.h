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

// Posted when the native client is retired (T10c) and users should install the
// unified Tauri client. WPARAM = 1 for an explicit Tools → Check for Updates
// request, 0 for the one-time launch notice.
static constexpr UINT WM_APP_UPDATE_MIGRATE = WM_USER + 11;

struct AppUpdateInfo {
    std::wstring tag;     // e.g. L"client-v1.2.3"
    std::wstring msiUrl;  // direct download URL for ArcadeLauncher-Server-Client-x64.msi
};

// Fires a background thread that notifies the user the native client is retired.
// Automatic checks (manual=false) show a one-time migration notice; manual
// checks always surface the unified-client download link.
void CheckForAppUpdateAsync(HWND hwnd, bool manual = false);

// Retained for ABI compatibility; the native auto-update channel is retired.
void DownloadAndInstallAsync(HWND hwnd, std::wstring tag, std::wstring msiUrl);

// Marks the one-time unified-client launch notice as seen.
void DismissUnifiedClientNotice();
