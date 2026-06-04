#include "pch.h"
#include "App.h"


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    bool startInTray = (lpCmdLine && wcsstr(lpCmdLine, L"--tray") != nullptr);

    // Single-instance check — if already running, show/restore it.
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"ArcadeLauncherSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"ArcadeLauncherWnd", nullptr);
        if (existing) {
            ShowWindow(existing, SW_SHOW);
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(hMutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    {
        App app;
        if (app.Initialize(hInstance, startInTray))
            app.Run();
    }

    CoUninitialize();
    CloseHandle(hMutex);
    return 0;
}
