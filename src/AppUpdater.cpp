#include "pch.h"
#include "AppUpdater.h"

// T10c: the unified Tauri client replaces this native build. Auto-updates from
// ArcadeLauncher-Client releases are retired; existing installs get a one-time
// notice (or a manual Tools → Check for Updates prompt) pointing at the new
// per-user, admin-free installer.

static constexpr wchar_t UNIFIED_RELEASES_URL[] =
    L"https://github.com/TheStonedGamer/ArcadeLauncher-Unified-Client/releases/latest";

static std::wstring MigrationMarkerPath() {
    return GetAppDataPath() + L"\\unified_client_notice.txt";
}

static bool MigrationNoticePending() {
    return GetFileAttributesW(MigrationMarkerPath().c_str()) == INVALID_FILE_ATTRIBUTES;
}

static void DismissMigrationNotice() {
    std::wstring dir = GetAppDataPath();
    CreateDirectoryW(dir.c_str(), nullptr);
    HANDLE hf = CreateFileW(MigrationMarkerPath().c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return;
    const char marker[] = "dismissed";
    DWORD written = 0;
    WriteFile(hf, marker, (DWORD)(sizeof(marker) - 1), &written, nullptr);
    CloseHandle(hf);
}

static void MigrateWorker(HWND hwnd, bool manual) {
    if (manual || MigrationNoticePending())
        PostMessageW(hwnd, WM_APP_UPDATE_MIGRATE, manual ? 1 : 0, 0);
}

void CheckForAppUpdateAsync(HWND hwnd, bool manual) {
    std::thread(MigrateWorker, hwnd, manual).detach();
}

void DownloadAndInstallAsync(HWND hwnd, std::wstring /*tag*/, std::wstring /*msiUrl*/) {
    PostMessageW(hwnd, WM_APP_UPDATE_MIGRATE, 1, 0);
}

void DismissUnifiedClientNotice() {
    DismissMigrationNotice();
}
