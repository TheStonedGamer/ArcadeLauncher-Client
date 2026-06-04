#pragma once
#include "pch.h"
#include "Config.h"
#include "EmulatorDownloader.h"

// Shown automatically on first launch when any downloadable emulator is missing.
// Downloads RPCS3, Gopher64, and Mesen sequentially, updating AppConfig as each
// completes. Simulates modal: disables the parent window while open.
class EmulatorSetupWindow {
public:
    void Open(HWND parent, AppConfig& cfg, std::function<void()> onDone);
    bool IsOpen() const { return m_hwnd && IsWindow(m_hwnd); }

private:
    struct EmuEntry {
        const wchar_t*       name;
        EmulatorDownloadSpec spec;
        std::function<void(AppConfig&, const std::wstring&, const std::wstring&)> apply;

        enum class St { Queued, Downloading, Done, Failed, Skipped } state{};
        HWND hStatus = nullptr;
    };

    enum class WinState { Idle, Running, Done };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    void Build(HWND hwnd);
    void StartNext();
    void FinishAll();
    void SkipRemaining();

    HWND       m_hwnd   = nullptr;
    HWND       m_parent = nullptr;
    AppConfig* m_cfg    = nullptr;
    std::function<void()> m_onDone;

    std::vector<EmuEntry> m_entries;
    WinState              m_state = WinState::Idle;

    HWND m_progress  = nullptr;
    HWND m_statusLbl = nullptr;
    HWND m_btnStart  = nullptr;   // "Download All" → disabled → "Close"
    HWND m_btnSkip   = nullptr;   // "Skip All" → "Skip" → hidden

    static constexpr int  IDC_START  = 1;
    static constexpr int  IDC_SKIP   = 2;
    static constexpr int  IDC_STATUS = 10;
    static constexpr int  IDC_PROG   = 11;
    static constexpr wchar_t WC_NAME[] = L"EmuFirstLaunchWnd";

public:
    static constexpr int  ROW_H  = 30;
    static constexpr int  ROW_Y0 = 84;
    static constexpr int  WIN_W  = 520;
};
