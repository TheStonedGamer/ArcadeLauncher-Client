#include "pch.h"
#include "FirstLaunchSetup.h"
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

// ── Fonts ─────────────────────────────────────────────────────────────────────

static HFONT s_font     = nullptr;
static HFONT s_boldFont = nullptr;

static void EnsureFonts() {
    if (s_font) return;
    NONCLIENTMETRICSW ncm{ sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    s_font = CreateFontIndirectW(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfWeight = FW_SEMIBOLD;
    s_boldFont = CreateFontIndirectW(&ncm.lfMessageFont);
}
static void SetF(HWND h, bool bold = false) {
    SendMessageW(h, WM_SETFONT, (WPARAM)(bold ? s_boldFont : s_font), TRUE);
}

// ── Control helpers ───────────────────────────────────────────────────────────

static HWND SL(HWND p, const wchar_t* t, int x, int y, int w, int h = 17,
               bool bold = false, DWORD extra = 0) {
    HWND hw = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE | extra,
                              x, y, w, h, p, nullptr, nullptr, nullptr);
    SetF(hw, bold);
    return hw;
}
static HWND SLI(HWND p, const wchar_t* t, int id, int x, int y, int w, int h = 17,
                DWORD extra = 0) {
    HWND hw = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE | extra,
                              x, y, w, h, p, (HMENU)(intptr_t)id, nullptr, nullptr);
    SetF(hw);
    return hw;
}
static HWND BT(HWND p, const wchar_t* t, int id, int x, int y, int w = 110, int h = 26) {
    HWND hw = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              x, y, w, h, p, (HMENU)(intptr_t)id, nullptr, nullptr);
    SetF(hw);
    return hw;
}
static HWND Rule(HWND p, int x, int y, int w) {
    return CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                           x, y, w, 2, p, nullptr, nullptr, nullptr);
}

// ── Layout ────────────────────────────────────────────────────────────────────

static constexpr int MX = 20;                         // horizontal margin
static constexpr int CW = EmulatorSetupWindow::WIN_W - 2 * MX;  // 480

// Client area height for n entries
static int ClientH(int n) {
    // header(84) + rows(n*30) + rule+pb+status+gap+buttons+margin
    // = 84 + n*30 + 8 + 2 + 10 + 18 + 10 + 17 + 16 + 26 + 20 = n*30 + 211
    return EmulatorSetupWindow::ROW_Y0 + n * EmulatorSetupWindow::ROW_H + 127;
}

// ── WndProc ───────────────────────────────────────────────────────────────────

LRESULT CALLBACK EmulatorSetupWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    auto* self = reinterpret_cast<EmulatorSetupWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMsg(hwnd, msg, wp, lp)
                : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT EmulatorSetupWindow::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_CLOSE:
        // If a download is running the thread will post to this HWND after it's
        // destroyed — PostMessageW to an invalid HWND fails silently, which is fine.
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        m_hwnd = nullptr;
        if (m_parent) EnableWindow(m_parent, TRUE);
        // Call onDone if FinishAll hasn't already done so.
        if (m_state != WinState::Done && m_onDone) m_onDone();
        return 0;

    case WM_COMMAND:
        if (LOWORD(wp) == IDC_START) {
            if (m_state == WinState::Done) {
                DestroyWindow(hwnd);
            } else if (m_state == WinState::Idle) {
                m_state = WinState::Running;
                EnableWindow(m_btnStart, FALSE);
                SetWindowTextW(m_btnStart, L"Downloading\x2026");
                SetWindowTextW(m_btnSkip, L"Skip");
                SendMessageW(m_progress, PBM_SETMARQUEE, TRUE, 40);
                StartNext();
            }
        } else if (LOWORD(wp) == IDC_SKIP) {
            SkipRemaining();
        }
        return 0;

    case WM_EMUDOWNLOAD_DONE: {
        int idx = (int)wp;
        auto* res = reinterpret_cast<EmuDownloadResult*>(lp);

        if (idx >= 0 && idx < (int)m_entries.size()) {
            auto& e = m_entries[idx];
            if (e.state == EmuEntry::St::Downloading && res) {
                bool ok = !res->exePath.empty() &&
                          res->exePath.compare(0, 4, L"ERR:") != 0;
                if (ok) {
                    e.state = EmuEntry::St::Done;
                    SetWindowTextW(e.hStatus, L"\x2713 Done");
                    if (m_cfg) e.apply(*m_cfg, res->exePath, res->tag);
                } else {
                    e.state = EmuEntry::St::Failed;
                    std::wstring msg = res->exePath.size() > 4
                        ? res->exePath.substr(4) : L"Download failed.";
                    SetWindowTextW(e.hStatus, L"\x2717 Failed");
                    SetWindowTextW(m_statusLbl, msg.c_str());
                }
            }
        }
        delete res;

        // Update progress
        int done = 0;
        for (auto& e : m_entries)
            if (e.state != EmuEntry::St::Queued && e.state != EmuEntry::St::Downloading)
                done++;
        SendMessageW(m_progress, PBM_SETMARQUEE, FALSE, 0);
        SendMessageW(m_progress, PBM_SETPOS, done, 0);

        StartNext();
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Build ─────────────────────────────────────────────────────────────────────

void EmulatorSetupWindow::Build(HWND hwnd) {
    EnsureFonts();

    int n = (int)m_entries.size();

    // Header
    SL(hwnd, L"Download Emulators", MX, 14, CW, 20, true);
    SL(hwnd,
       L"These emulators are not set up yet.  Click \x201c" L"Download All\x201d to grab them "
       L"automatically, or \x201c" L"Skip All\x201d to configure them later in Settings.",
       MX, 38, CW, 34);
    Rule(hwnd, MX, 76, CW);

    // Emulator rows
    for (int i = 0; i < n; i++) {
        int ry = ROW_Y0 + i * ROW_H;
        SL(hwnd, m_entries[i].name, MX, ry + 6, 320, 20);
        m_entries[i].hStatus = SLI(hwnd, L"Queued", 20 + i,
                                   MX + 320, ry + 6, 160, 20, SS_RIGHT);
    }

    // Separator + progress bar + status label
    int ay = ROW_Y0 + n * ROW_H + 8;
    Rule(hwnd, MX, ay, CW);

    m_progress = CreateWindowExW(0, PROGRESS_CLASS, nullptr,
                                 WS_CHILD | WS_VISIBLE | PBS_SMOOTH | PBS_MARQUEE,
                                 MX, ay + 10, CW, 18, hwnd,
                                 (HMENU)(intptr_t)IDC_PROG, nullptr, nullptr);
    SendMessageW(m_progress, PBM_SETRANGE32, 0, n);
    SendMessageW(m_progress, PBM_SETPOS, 0, 0);

    m_statusLbl = SLI(hwnd, L"Click \x201c" L"Download All\x201d to begin.",
                      IDC_STATUS, MX, ay + 36, CW, 17);

    // Buttons (right-aligned)
    int btnY = ClientH(n) - 46;
    m_btnSkip  = BT(hwnd, L"Skip All", IDC_SKIP,  WIN_W - MX - 234, btnY);
    m_btnStart = BT(hwnd, L"Download All", IDC_START, WIN_W - MX - 118, btnY);
}

// ── Open ──────────────────────────────────────────────────────────────────────

void EmulatorSetupWindow::Open(HWND parent, AppConfig& cfg,
                               std::function<void()> onDone) {
    m_cfg    = &cfg;
    m_onDone = std::move(onDone);
    m_parent = parent;
    m_state  = WinState::Idle;
    m_entries.clear();

    auto hasExe = [](const std::wstring& p) -> bool {
        return !p.empty() &&
               GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
    };

    if (!hasExe(cfg.emulators.rpcs3Path))
        m_entries.push_back({
            L"RPCS3  \x2014  PS3 emulator",
            { "RPCS3/rpcs3-binaries-win", L"win64_msvc.7z", L"rpcs3.exe", L"rpcs3" },
            [](AppConfig& c, const std::wstring& exe, const std::wstring& tag) {
                c.emulators.rpcs3Path = exe;
                c.emulators.rpcs3Tag  = tag;
            }
        });

    if (!hasExe(cfg.emulators.n64Path))
        m_entries.push_back({
            L"Gopher64  \x2014  Nintendo 64 emulator",
            { "gopher64/gopher64", L"windows-x86_64.exe", L"gopher64.exe", L"gopher64" },
            [](AppConfig& c, const std::wstring& exe, const std::wstring& tag) {
                c.emulators.n64Path = exe;
                c.emulators.n64Tag  = tag;
            }
        });

    if (!hasExe(cfg.emulators.nesPath) || !hasExe(cfg.emulators.snesPath))
        m_entries.push_back({
            L"Mesen  \x2014  NES + SNES emulator",
            { "", L"2.1.1", L"Mesen.exe", L"mesen2",
              L"https://github.com/SourMesen/Mesen2/releases/download/2.1.1/Mesen_2.1.1_Windows.zip" },
            [](AppConfig& c, const std::wstring& exe, const std::wstring& tag) {
                c.emulators.nesPath  = exe;
                c.emulators.snesPath = exe;
                c.emulators.nesTag   = tag;
                c.emulators.snesTag  = tag;
            }
        });

    if (!hasExe(cfg.emulators.duckstationPath))
        m_entries.push_back({
            L"DuckStation  \x2014  PlayStation 1 emulator",
            { "stenzek/duckstation", L"windows-x64-release.zip",
              L"duckstation-qt-x64-ReleaseLTCG.exe", L"duckstation" },
            [](AppConfig& c, const std::wstring& exe, const std::wstring& tag) {
                c.emulators.duckstationPath = exe;
                c.emulators.duckstationTag  = tag;
            }
        });

    if (!hasExe(cfg.emulators.pcsx2Path))
        m_entries.push_back({
            L"PCSX2  \x2014  PlayStation 2 emulator",
            { "PCSX2/pcsx2", L"windows-x64-Qt.7z", L"pcsx2-qt.exe", L"pcsx2" },
            [](AppConfig& c, const std::wstring& exe, const std::wstring& tag) {
                c.emulators.pcsx2Path = exe;
                c.emulators.pcsx2Tag  = tag;
            }
        });

    if (!hasExe(cfg.emulators.xeniaPath))
        m_entries.push_back({
            L"Xenia Canary  \x2014  Xbox 360 emulator",
            { "xenia-canary/xenia-canary", L"xenia_canary_windows.zip",
              L"xenia_canary.exe", L"xenia-canary" },
            [](AppConfig& c, const std::wstring& exe, const std::wstring& tag) {
                c.emulators.xeniaPath = exe;
                c.emulators.xeniaTag  = tag;
            }
        });

    if (!hasExe(cfg.emulators.xemuPath))
        m_entries.push_back({
            L"XEMU  \x2014  Original Xbox emulator",
            { "xemu-project/xemu", L"win-x86_64-release.zip",
              L"xemu.exe", L"xemu" },
            [](AppConfig& c, const std::wstring& exe, const std::wstring& tag) {
                c.emulators.xemuPath = exe;
                c.emulators.xemuTag  = tag;
            }
        });

    if (m_entries.empty()) {
        // All emulators already present — nothing to do.
        if (m_onDone) m_onDone();
        return;
    }

    // Register window class once
    static bool s_registered = false;
    if (!s_registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = WC_NAME;
        RegisterClassExW(&wc);
        s_registered = true;
    }

    // Compute window size from desired client size
    DWORD style   = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW;
    int   clientH = ClientH((int)m_entries.size());
    RECT  r       = { 0, 0, WIN_W, clientH };
    AdjustWindowRectEx(&r, style, FALSE, exStyle);
    int winW = r.right  - r.left;
    int winH = r.bottom - r.top;

    // Center over parent
    RECT pr{};
    GetWindowRect(parent, &pr);
    int cx = pr.left + (pr.right  - pr.left - winW) / 2;
    int cy = pr.top  + (pr.bottom - pr.top  - winH) / 2;

    m_hwnd = CreateWindowExW(exStyle, WC_NAME, L"Emulator Setup", style,
                             cx, cy, winW, winH,
                             parent, nullptr, GetModuleHandleW(nullptr), this);

    Build(m_hwnd);

    // Disable the parent while this window is open (simulates modal)
    EnableWindow(parent, FALSE);
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
}

// ── StartNext ─────────────────────────────────────────────────────────────────

void EmulatorSetupWindow::StartNext() {
    if (m_state == WinState::Done) return;

    for (int i = 0; i < (int)m_entries.size(); i++) {
        if (m_entries[i].state == EmuEntry::St::Queued) {
            m_entries[i].state = EmuEntry::St::Downloading;
            SetWindowTextW(m_entries[i].hStatus, L"Downloading\x2026");
            SetWindowTextW(m_statusLbl,
                (std::wstring(L"Downloading ") + m_entries[i].name + L"\x2026").c_str());
            SendMessageW(m_progress, PBM_SETMARQUEE, TRUE, 40);
            DownloadEmulatorAsync(m_hwnd, i, m_entries[i].spec, GetAppDataPath());
            return;
        }
    }

    FinishAll();
}

// ── FinishAll ─────────────────────────────────────────────────────────────────

void EmulatorSetupWindow::FinishAll() {
    if (m_state == WinState::Done) return;
    m_state = WinState::Done;

    int doneCount = 0;
    for (auto& e : m_entries) {
        if (e.state == EmuEntry::St::Done) {
            doneCount++;
        } else if (e.state == EmuEntry::St::Queued) {
            e.state = EmuEntry::St::Skipped;
            SetWindowTextW(e.hStatus, L"Skipped");
        }
    }

    SendMessageW(m_progress, PBM_SETMARQUEE, FALSE, 0);
    SendMessageW(m_progress, PBM_SETPOS, (WPARAM)m_entries.size(), 0);

    std::wstring msg;
    if (doneCount == (int)m_entries.size())
        msg = L"All emulators ready.  Click Close to continue.";
    else if (doneCount > 0)
        msg = std::to_wstring(doneCount) + L" of " +
              std::to_wstring(m_entries.size()) +
              L" emulators ready.  Click Close to continue.";
    else
        msg = L"Setup skipped.  You can download emulators any time in Settings.";
    SetWindowTextW(m_statusLbl, msg.c_str());

    EnableWindow(m_btnSkip, FALSE);
    EnableWindow(m_btnStart, TRUE);
    SetWindowTextW(m_btnStart, L"Close");

    if (m_onDone) m_onDone();
}

// ── SkipRemaining ─────────────────────────────────────────────────────────────

void EmulatorSetupWindow::SkipRemaining() {
    if (m_state == WinState::Done) return;

    for (auto& e : m_entries) {
        if (e.state == EmuEntry::St::Queued) {
            e.state = EmuEntry::St::Skipped;
            SetWindowTextW(e.hStatus, L"Skipped");
        }
    }

    if (m_state == WinState::Idle) {
        // Nothing started yet — close immediately.
        FinishAll();
    } else {
        // A download is in progress; WM_EMUDOWNLOAD_DONE → StartNext → FinishAll.
        SetWindowTextW(m_statusLbl, L"Finishing current download\x2026");
        EnableWindow(m_btnSkip, FALSE);
    }
}
