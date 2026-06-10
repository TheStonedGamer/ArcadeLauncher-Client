#include "pch.h"
#include "App.h"


// Extract the game id following a `--launch ` token, if present.
static std::wstring ParseLaunchId(LPWSTR cmd) {
    if (!cmd) return {};
    const wchar_t* p = wcsstr(cmd, L"--launch");
    if (!p) return {};
    p += 8;                              // skip "--launch"
    while (*p == L' ' || *p == L'\t') ++p;
    bool quoted = (*p == L'"');
    if (quoted) ++p;
    std::wstring id;
    while (*p && (quoted ? *p != L'"' : (*p != L' ' && *p != L'\t')))
        id += *p++;
    return id;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    bool startInTray = (lpCmdLine && wcsstr(lpCmdLine, L"--tray") != nullptr);
    std::wstring launchId = ParseLaunchId(lpCmdLine);

    // Single-instance check — if already running, show/restore it (and forward a
    // shortcut's `--launch <id>` request to it via WM_COPYDATA).
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"ArcadeLauncherSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"ArcadeLauncherWnd", nullptr);
        if (existing) {
            ShowWindow(existing, SW_SHOW);
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
            if (!launchId.empty()) {
                COPYDATASTRUCT cds{};
                cds.dwData = App::kCopyDataLaunchId;
                cds.cbData = (DWORD)((launchId.size() + 1) * sizeof(wchar_t));
                cds.lpData = (PVOID)launchId.c_str();
                SendMessageW(existing, WM_COPYDATA, 0, (LPARAM)&cds);
            }
        }
        CloseHandle(hMutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    {
        App app;
        if (!launchId.empty()) app.SetPendingLaunchId(launchId);
        if (app.Initialize(hInstance, startInTray))
            app.Run();
    }

    CoUninitialize();
    CloseHandle(hMutex);
    return 0;
}
