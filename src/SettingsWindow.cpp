#include "pch.h"
#include "SettingsWindow.h"
#include "EmulatorDownloader.h"
#include "EmulatorUpdateChecker.h"
#include "PlatformIcons.h"
#include "IgdbSync.h"
#include "DolphinConfig.h"
#include "XeniaConfig.h"
#include "Pcsx2Config.h"
#include "Rpcs3Config.h"
#include "GopherConfig.h"
#include <shobjidl_core.h>
#include <commdlg.h>
#include <shellapi.h>

// ─── Colors ───────────────────────────────────────────────────────────────────
static constexpr COLORREF C_SB_BG      = RGB(30,  30,  30);
static constexpr COLORREF C_SB_ITEM    = RGB(200, 200, 200);
static constexpr COLORREF C_SB_SEL_BG  = RGB( 0,  112, 204);
static constexpr COLORREF C_SB_SEL_TXT = RGB(255, 255, 255);
static constexpr COLORREF C_SB_HOV_BG  = RGB( 50,  50,  52);

// ─── Fonts ────────────────────────────────────────────────────────────────────
static HFONT gFont     = nullptr;
static HFONT gBoldFont = nullptr;

static void EnsureFonts() {
    if (gFont) return;
    NONCLIENTMETRICSW ncm{ sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    gFont = CreateFontIndirectW(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfWeight = FW_SEMIBOLD;
    gBoldFont = CreateFontIndirectW(&ncm.lfMessageFont);
}

static void ApplyFont(HWND h, bool bold = false) {
    SendMessageW(h, WM_SETFONT, (WPARAM)(bold ? gBoldFont : gFont), TRUE);
}

static std::wstring TrimTrailingSlash(std::wstring value) {
    while (!value.empty() && (value.back() == L'/' || value.back() == L'\\'))
        value.pop_back();
    return value;
}

static std::wstring EmulatorArchiveUrl(const AppConfig& cfg, const wchar_t* archiveName) {
    std::wstring base = TrimTrailingSlash(cfg.server.baseUrl.empty()
        ? L"http://10.0.0.210:8721"
        : cfg.server.baseUrl);
    return base + L"/emulators/" + archiveName;
}

// ─── Win32 control factory ────────────────────────────────────────────────────

static HWND Label(HWND p, const wchar_t* t, int x, int y, int w, int h = 17, bool bold = false) {
    HWND hw = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE,
                              x, y, w, h, p, nullptr, nullptr, nullptr);
    ApplyFont(hw, bold);
    return hw;
}
static HWND SmallLabel(HWND p, const wchar_t* t, int x, int y, int w) {
    HWND hw = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE,
                              x, y, w, 28, p, nullptr, nullptr, nullptr);
    ApplyFont(hw);
    return hw;
}
static HWND Edit(HWND p, int id, int x, int y, int w, DWORD extra = 0) {
    HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | extra,
                              x, y, w, 22, p, (HMENU)(intptr_t)id, nullptr, nullptr);
    ApplyFont(hw);
    return hw;
}
static HWND Btn(HWND p, const wchar_t* t, int id, int x, int y, int w = 90, int h = 24) {
    HWND hw = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              x, y, w, h, p, (HMENU)(intptr_t)id, nullptr, nullptr);
    ApplyFont(hw);
    return hw;
}
static HWND Check(HWND p, const wchar_t* t, int id, int x, int y, int w) {
    HWND hw = CreateWindowExW(0, L"BUTTON", t,
                              WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                              x, y, w, 20, p, (HMENU)(intptr_t)id, nullptr, nullptr);
    ApplyFont(hw);
    return hw;
}
static HWND Group(HWND p, const wchar_t* t, int x, int y, int w, int h) {
    HWND hw = CreateWindowExW(0, L"BUTTON", t, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                              x, y, w, h, p, nullptr, nullptr, nullptr);
    ApplyFont(hw);
    return hw;
}
static HWND ListBox(HWND p, int id, int x, int y, int w, int h) {
    HWND hw = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                              LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                              x, y, w, h, p, (HMENU)(intptr_t)id, nullptr, nullptr);
    ApplyFont(hw);
    return hw;
}
static HWND StatLabel(HWND p, const wchar_t* t, int id, int x, int y, int w, int h = 17) {
    HWND hw = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE,
                              x, y, w, h, p, (HMENU)(intptr_t)id, nullptr, nullptr);
    ApplyFont(hw);
    return hw;
}
static HWND Rule(HWND p, int x, int y, int w) {
    return CreateWindowExW(0, L"STATIC", nullptr,
                           WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                           x, y, w, 2, p, nullptr, nullptr, nullptr);
}

static HWND Combo(HWND p, int id, int x, int y, int w, int dropH = 220) {
    HWND hw = CreateWindowExW(0, L"COMBOBOX", L"",
                              WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                              x, y, w, dropH, p, (HMENU)(intptr_t)id, nullptr, nullptr);
    ApplyFont(hw);
    return hw;
}
// Fill a combobox from a null-terminated list of labels and select `sel`.
static void ComboFill(HWND combo, std::initializer_list<const wchar_t*> items, int sel) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (auto* it : items) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)it);
    SendMessageW(combo, CB_SETCURSEL, sel, 0);
}
static int ComboSel(HWND combo) { return (int)SendMessageW(combo, CB_GETCURSEL, 0, 0); }

static void  Chk(HWND h, bool v) { SendMessageW(h, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0); }
static bool  IsChk(HWND h)       { return SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED; }
static std::wstring GetTxt(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (!n) return {};
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}

// ─── File-scope layout constants (keep in sync with SettingsWindow.h) ─────────
static constexpr int K_CX  = 186;              // content left
static constexpr int K_CY  = 14;               // content top
static constexpr int K_CW  = 590;              // content width
static constexpr int K_BX  = K_CX + K_CW - 102; // button column x, 12px from group right (674)
static constexpr int K_LW  = K_BX - K_CX - 18;  // list width beside buttons (470)

// Page header: bold title + etched rule. Returns y where content starts.
static int PageHeader(HWND hwnd, std::vector<HWND>& pc, const wchar_t* title) {
    pc.push_back(Label(hwnd, title, K_CX, K_CY, K_CW, 22, true));
    pc.push_back(Rule (hwnd, K_CX, K_CY + 26, K_CW));
    return K_CY + 36;
}

// ─── SettingsWindow ───────────────────────────────────────────────────────────

void SettingsWindow::Open(HWND parent, AppConfig& cfg,
                           std::function<void()> onSave,
                           std::function<void()> onRefreshMeta,
                           std::function<void()> onReacquireMeta,
                           int startPage,
                           IgdbClient* igdbClient) {
    if (IsOpen()) { SetForegroundWindow(m_hwnd); return; }
    m_parent           = parent;
    m_cfg              = &cfg;
    m_work             = cfg;
    m_onSave           = onSave;
    m_onRefreshMeta    = onRefreshMeta;
    m_onReacquireMeta  = onReacquireMeta;
    m_startPage        = startPage;
    m_igdbClient       = igdbClient;

    EnsureFonts();

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = WNDCLASS;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    RECT pr; GetWindowRect(parent, &pr);
    int X = pr.left + (pr.right  - pr.left - WIN_W) / 2;
    int Y = pr.top  + (pr.bottom - pr.top  - WIN_H) / 2;

    m_hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        WNDCLASS, L"Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        X, Y, WIN_W, WIN_H, parent, nullptr, GetModuleHandleW(nullptr), this);

    if (m_hwnd) {
        EnableWindow(parent, FALSE);
        SetForegroundWindow(m_hwnd);
    }
}

void SettingsWindow::Close() {
    if (!m_hwnd) return;
    if (m_parent) { EnableWindow(m_parent, TRUE); SetForegroundWindow(m_parent); }
    if (m_sidebarBrush) { DeleteObject(m_sidebarBrush); m_sidebarBrush = nullptr; }
    DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
}

bool SettingsWindow::IsOpen() const {
    return m_hwnd && IsWindow(m_hwnd);
}

// ─── Window proc ─────────────────────────────────────────────────────────────

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        auto* cs   = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* self = reinterpret_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        self->m_hwnd = hwnd;
        self->CreateChrome(hwnd);
        self->SwitchPage(self->m_startPage);
        return 0;
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->HandleMsg(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT SettingsWindow::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wp;
        RECT rc; GetClientRect(hwnd, &rc);
        // Sidebar area — dark
        RECT sb = { 0, 0, SB_W, rc.bottom };
        if (!m_sidebarBrush) m_sidebarBrush = CreateSolidBrush(C_SB_BG);
        FillRect(hdc, &sb, m_sidebarBrush);
        // Separator line
        RECT sep = { SB_W, 0, SB_W + 1, rc.bottom };
        HBRUSH sepBr = CreateSolidBrush(RGB(70, 70, 70));
        FillRect(hdc, &sep, sepBr);
        DeleteObject(sepBr);
        // Content + bottom
        RECT ct = { SB_W + 1, 0, rc.right, rc.bottom };
        FillRect(hdc, &ct, GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }

    case WM_CTLCOLORLISTBOX: {
        HWND ctrl = (HWND)lp;
        if (ctrl == m_sidebar) {
            HDC hdc = (HDC)wp;
            SetBkColor(hdc, C_SB_BG);
            SetTextColor(hdc, C_SB_ITEM);
            if (!m_sidebarBrush) m_sidebarBrush = CreateSolidBrush(C_SB_BG);
            return (LRESULT)m_sidebarBrush;
        }
        break;
    }

    case WM_MEASUREITEM: {
        auto* mi = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if ((int)mi->CtlID == ID_SIDEBAR) { mi->itemHeight = 34; return TRUE; }
        break;
    }

    case WM_DRAWITEM: {
        auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if ((int)di->CtlID == ID_SIDEBAR) { DrawSidebarItem(di); return TRUE; }
        break;
    }

    case WM_COMMAND: {
        int  id   = LOWORD(wp);
        int  note = HIWORD(wp);

        if (id == ID_SAVE) {
            SaveCurrentPage();
            *m_cfg = m_work;
            Close();
            if (m_onSave) m_onSave();
            return 0;
        }
        if (id == ID_APPLY) {
            SaveCurrentPage();
            *m_cfg = m_work;
            if (m_onSave) m_onSave();
            return 0;
        }
        if (id == ID_CANCEL) {
            Close();
            return 0;
        }
        if (id == ID_SIDEBAR && note == LBN_SELCHANGE) {
            int sel = (int)SendMessageW(m_sidebar, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) SwitchPage(sel);
            return 0;
        }
        HandlePageCommand(id);
        return 0;
    }

    case WM_IGDBSYNC_DONE: {
        // Background sync finished — re-enable the button and show result.
        HWND btn  = PC(ID_P_BTN6);
        HWND stat = PC(ID_P_STAT2);
        if (btn)  { EnableWindow(btn, TRUE); SetWindowTextW(btn, L"Sync from IGDB"); }
        if (stat) {
            int total = (int)wp;
            if (total > 0) {
                std::wstring msg = std::to_wstring(total)
                    + L" games synced.";
                SetWindowTextW(stat, msg.c_str());
                if (m_parent)
                    PostMessageW(m_parent, WM_IGDBSYNC_DONE, wp, 0);
            } else {
                SetWindowTextW(stat, L"Sync failed — check credentials.");
            }
        }
        return 0;
    }

    case WM_EMUCHECK_DONE: {
        int  page    = (int)wp;
        auto* result = reinterpret_cast<EmuCheckResult*>(lp);
        if (page == m_currentPage && result) {
            if (!result->isError) {
                SetVersionLabel(InstalledTagForPage(page), result->latestTag);
            } else {
                HWND stat = PC(ID_P_STAT1);
                if (stat) {
                    std::wstring installed = InstalledTagForPage(page);
                    std::wstring txt = installed.empty()
                        ? L"(could not check for updates)"
                        : installed + L"  \x2013  (could not check for updates)";
                    SetWindowTextW(stat, txt.c_str());
                }
            }
        }
        delete result;
        return 0;
    }

    case WM_EMUDOWNLOAD_PROGRESS: {
        int page = (int)wp;
        auto* progress = reinterpret_cast<EmuDownloadProgress*>(lp);
        if (progress && page == m_currentPage) {
            HWND bar = PC(ID_P_PROG1);
            HWND stat = PC(ID_P_STAT1);
            if (bar) {
                SendMessageW(bar, PBM_SETRANGE32, 0, 1000);
                int pos = 0;
                if (progress->total > 0)
                    pos = (int)std::min<uint64_t>(1000, progress->downloaded * 1000 / progress->total);
                SendMessageW(bar, PBM_SETPOS, pos, 0);
            }
            if (stat) {
                auto mb = [](uint64_t b) { return (double)b / (1024.0 * 1024.0); };
                std::wstringstream ss;
                ss.setf(std::ios::fixed);
                ss.precision(1);
                ss << L"Downloading " << mb(progress->downloaded) << L" MB";
                if (progress->total > 0)
                    ss << L" / " << mb(progress->total) << L" MB";
                SetWindowTextW(stat, ss.str().c_str());
            }
        }
        delete progress;
        return 0;
    }

    case WM_EMUDOWNLOAD_DONE: {
        int  page    = (int)wp;
        auto* result = reinterpret_cast<EmuDownloadResult*>(lp);

        if (result) {
            bool onPage = (page == m_currentPage);

            // Re-enable the Download button if the user is still on this page
            if (onPage) {
                HWND btnDl = PC(ID_P_BTN5);
                if (btnDl) { EnableWindow(btnDl, TRUE); SetWindowTextW(btnDl, L"Download latest"); }
                HWND bar = PC(ID_P_PROG1);
                if (bar) SendMessageW(bar, PBM_SETPOS, 0, 0);
            }

            if (!result->exePath.empty() && result->exePath.substr(0, 4) == L"ERR:") {
                // Only surface error dialogs when the page is still visible
                if (onPage)
                    MessageBoxW(m_hwnd, result->exePath.c_str() + 4, L"Download failed",
                                MB_OK | MB_ICONERROR);
            } else {
                if (!result->exePath.empty()) {
                    // Always persist to m_work/m_cfg — even if the user navigated away.
                    // SetPathForPage also syncs the NES<->SNES sibling (same Mesen2 exe).
                    SetPathForPage(page, result->exePath);
                    if (onPage)
                        SetWindowTextW(PC(ID_P_EDIT1), result->exePath.c_str());
                } else if (onPage) {
                    MessageBoxW(m_hwnd,
                        L"Download and extraction succeeded, but the executable was not found "
                        L"inside the archive. Check the emulators folder manually.",
                        L"Executable not found", MB_OK | MB_ICONWARNING);
                }

                if (!result->tag.empty()) {
                    SaveTagForPage(page, result->tag);
                    // Keep NES and SNES tags in sync (both use Mesen2)
                    if (page == PAGE_NES) {
                        m_work.emulators.snesTag = result->tag;
                        m_cfg->emulators.snesTag = result->tag;
                    } else if (page == PAGE_SNES) {
                        m_work.emulators.nesTag = result->tag;
                        m_cfg->emulators.nesTag = result->tag;
                    }
                    // PS1/PS2/Xbox360 don't share exes — no sibling sync needed
                    if (onPage)
                        SetVersionLabel(result->tag, result->tag);
                }
            }
        }
        delete result;
        return 0;
    }

    case WM_CLOSE:
        // X button — commit settings the same way the Close button does.
        SaveCurrentPage();
        *m_cfg = m_work;
        Close();
        if (m_onSave) m_onSave();
        return 0;

    case WM_DESTROY:
        if (m_parent) { EnableWindow(m_parent, TRUE); SetForegroundWindow(m_parent); }
        m_hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void SettingsWindow::AddEmulatorProgressBar(int x, int y, int w) {
    HWND bar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
                               WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                               x, y, w, 14, m_hwnd,
                               (HMENU)(intptr_t)ID_P_PROG1, nullptr, nullptr);
    SendMessageW(bar, PBM_SETRANGE32, 0, 1000);
    SendMessageW(bar, PBM_SETPOS, 0, 0);
    AddPC(bar);
}

// ─── Sidebar drawing ──────────────────────────────────────────────────────────

void SettingsWindow::DrawSidebarItem(DRAWITEMSTRUCT* dis) {
    if (dis->itemID == (UINT)-1) return;

    HDC  hdc = dis->hDC;
    RECT rc  = dis->rcItem;
    bool sel = (dis->itemState & ODS_SELECTED) != 0;

    // Background
    COLORREF bg = sel ? C_SB_SEL_BG : C_SB_BG;
    HBRUSH bgBr = CreateSolidBrush(bg);
    FillRect(hdc, &rc, bgBr);
    DeleteObject(bgBr);

    // Left accent bar when selected
    if (sel) {
        RECT bar = { rc.left, rc.top + 5, rc.left + 3, rc.bottom - 5 };
        HBRUSH barBr = CreateSolidBrush(C_SB_SEL_TXT);
        FillRect(hdc, &bar, barBr);
        DeleteObject(barBr);
    }

    // Item text
    wchar_t buf[256] = {};
    SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)buf);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, sel ? C_SB_SEL_TXT : C_SB_ITEM);

    HFONT old = (HFONT)SelectObject(hdc, gFont);
    RECT tr = { rc.left + 18, rc.top, rc.right - 4, rc.bottom };
    DrawTextW(hdc, buf, -1, &tr, DT_VCENTER | DT_SINGLELINE | DT_LEFT | DT_NOPREFIX);
    SelectObject(hdc, old);
}

// ─── Chrome (sidebar + buttons) ───────────────────────────────────────────────

void SettingsWindow::CreateChrome(HWND hwnd) {
    // Sidebar listbox — owner-drawn, spans from top to above bottom bar
    m_sidebar = CreateWindowExW(0, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL |
        LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        0, 0, SB_W, BOT_Y, hwnd,
        (HMENU)(intptr_t)ID_SIDEBAR, GetModuleHandleW(nullptr), nullptr);
    ApplyFont(m_sidebar);

    // Bottom bar buttons
    Btn(hwnd, L"OK",     ID_SAVE,   WIN_W - 258, BOT_Y + 12, 72, 26);
    Btn(hwnd, L"Cancel", ID_CANCEL, WIN_W - 178, BOT_Y + 12, 78, 26);
    Btn(hwnd, L"Apply",  ID_APPLY,  WIN_W - 92,  BOT_Y + 12, 78, 26);

    RebuildSidebarItems();
}

void SettingsWindow::RebuildSidebarItems() {
    SendMessageW(m_sidebar, LB_RESETCONTENT, 0, 0);
    static const wchar_t* fixed[] = {
        L"General", L"Steam", L"Epic Games", L"GOG Galaxy",
        L"Dolphin", L"Ryujinx", L"RPCS3", L"N64", L"NES", L"SNES",
        L"PS1", L"PS2", L"Xbox 360", L"Xbox"
    };
    for (auto* s : fixed)
        SendMessageW(m_sidebar, LB_ADDSTRING, 0, (LPARAM)s);
    SendMessageW(m_sidebar, LB_SETCURSEL, m_currentPage, 0);
}

// ─── Page switching ───────────────────────────────────────────────────────────

void SettingsWindow::SwitchPage(int idx) {
    SaveCurrentPage();
    DestroyPageControls();
    m_currentPage = idx;
    SendMessageW(m_sidebar, LB_SETCURSEL, idx, 0);

    switch (idx) {
    case PAGE_GENERAL: BuildGeneralPage(); break;
    case PAGE_STEAM:   BuildSteamPage();   break;
    case PAGE_EPIC:    BuildEpicPage();    break;
    case PAGE_GOG:     BuildGogPage();     break;
    case PAGE_DOLPHIN: BuildDolphinPage(); break;
    case PAGE_RYUJINX: BuildRyujinxPage(); break;
    case PAGE_RPCS3:   BuildRpcs3Page();   break;
    case PAGE_N64:     BuildN64Page();     break;
    case PAGE_NES:     BuildNesPage();     break;
    case PAGE_SNES:    BuildSnesPage();    break;
    case PAGE_PS1:     BuildPS1Page();     break;
    case PAGE_PS2:     BuildPS2Page();     break;
    case PAGE_XBOX360: BuildXbox360Page(); break;
    case PAGE_XBOX:    BuildXboxPage();    break;
    default: break;
    }
    LoadCurrentPage();
}

void SettingsWindow::SaveCurrentPage() {
    if (m_pageControls.empty()) return;
    switch (m_currentPage) {
    case PAGE_GENERAL: SaveGeneralPage(); break;
    case PAGE_STEAM:   SaveSteamPage();   break;
    case PAGE_EPIC:    SaveEpicPage();    break;
    case PAGE_GOG:     SaveGogPage();     break;
    case PAGE_DOLPHIN: SaveDolphinPage(); break;
    case PAGE_RYUJINX: SaveRyujinxPage(); break;
    case PAGE_RPCS3:   SaveRpcs3Page();   break;
    case PAGE_N64:     SaveN64Page();     break;
    case PAGE_NES:     SaveNesPage();     break;
    case PAGE_SNES:    SaveSnesPage();    break;
    case PAGE_PS1:     SavePS1Page();     break;
    case PAGE_PS2:     SavePS2Page();     break;
    case PAGE_XBOX360: SaveXbox360Page(); break;
    case PAGE_XBOX:    SaveXboxPage();    break;
    default: break;
    }
}

void SettingsWindow::LoadCurrentPage() {
    switch (m_currentPage) {
    case PAGE_GENERAL: LoadGeneralPage(); break;
    case PAGE_STEAM:   LoadSteamPage();   break;
    case PAGE_EPIC:    LoadEpicPage();    break;
    case PAGE_GOG:     LoadGogPage();     break;
    case PAGE_DOLPHIN: LoadDolphinPage(); break;
    case PAGE_RYUJINX: LoadRyujinxPage(); break;
    case PAGE_RPCS3:   LoadRpcs3Page();   break;
    case PAGE_N64:     LoadN64Page();     break;
    case PAGE_NES:     LoadNesPage();     break;
    case PAGE_SNES:    LoadSnesPage();    break;
    case PAGE_PS1:     LoadPS1Page();     break;
    case PAGE_PS2:     LoadPS2Page();     break;
    case PAGE_XBOX360: LoadXbox360Page(); break;
    case PAGE_XBOX:    LoadXboxPage();    break;
    default: break;
    }
}

HWND SettingsWindow::AddPC(HWND h) {
    m_pageControls.push_back(h);
    return h;
}

void SettingsWindow::DestroyPageControls() {
    for (HWND h : m_pageControls)
        if (IsWindow(h)) DestroyWindow(h);
    m_pageControls.clear();
}

// ─── Page builders ────────────────────────────────────────────────────────────

void SettingsWindow::BuildGeneralPage() {
    int y = PageHeader(m_hwnd, m_pageControls, L"General");

    AddPC(Group(m_hwnd, L" Behavior ", K_CX, y, K_CW, 90));
    AddPC(Check(m_hwnd, L"Start fullscreen  (F11 to toggle at any time)",
                ID_P_CHK1, K_CX + 12, y + 20, K_CW - 24));
    AddPC(Check(m_hwnd, L"Minimize launcher to tray when a game launches",
                ID_P_CHK2, K_CX + 12, y + 42, K_CW - 24));
    AddPC(Check(m_hwnd, L"Start at Boot  (launches hidden in the system tray on Windows login)",
                ID_P_CHK3, K_CX + 12, y + 64, K_CW - 24));
    y += 98;

    AddPC(Group(m_hwnd, L" IGDB Metadata  (optional — enables cover art and descriptions) ",
                K_CX, y, K_CW, 158));
    y += 20;
    AddPC(Label(m_hwnd, L"Twitch Client ID:", K_CX + 12, y + 6, 130));
    AddPC(Edit (m_hwnd, ID_P_EDIT1, K_CX + 146, y + 4, K_CW - 158));
    y += 28;
    AddPC(Label(m_hwnd, L"Client Secret:",   K_CX + 12, y + 6, 130));
    AddPC(Edit (m_hwnd, ID_P_EDIT2, K_CX + 146, y + 4, K_CW - 158, ES_PASSWORD));
    y += 28;
    AddPC(SmallLabel(m_hwnd,
          L"Register a free app at dev.twitch.tv → OAuth Apps to get these credentials.",
          K_CX + 12, y + 4, K_CW - 24));
    y += 34;
    AddPC(Btn(m_hwnd, L"Refresh Metadata",   ID_P_BTN4, K_CX + 12, y, 136, 26));
    AddPC(Btn(m_hwnd, L"Re-acquire All",      ID_P_BTN5, K_CX + 156, y, 120, 26));
    AddPC(SmallLabel(m_hwnd,
          L"Refresh: fetch missing.   Re-acquire: clear all matches and refetch everything.",
          K_CX + 284, y + 4, K_CW - 296));
    y += 38;

    // ── Game Database ─────────────────────────────────────────────────────────
    AddPC(Group(m_hwnd, L" Game Database  (requires IGDB credentials) ", K_CX, y, K_CW, 68));
    AddPC(Btn(m_hwnd, L"Sync from IGDB", ID_P_BTN6, K_CX + 12, y + 20, 130, 26));
    AddPC(StatLabel(m_hwnd,
          L"Downloads the full game catalogue for all emulated platforms.",
          ID_P_STAT2, K_CX + 152, y + 26, K_CW - 164, 17));
    y += 76;

    AddPC(Group(m_hwnd, L" ArcadeLauncher Server ", K_CX, y, K_CW, 176));
    AddPC(Check(m_hwnd, L"Enable server library sync", ID_P_CHK4,
                K_CX + 12, y + 20, K_CW - 24));
    AddPC(Label(m_hwnd, L"Server URL:", K_CX + 12, y + 48, 92));
    AddPC(Edit(m_hwnd, ID_P_EDIT3, K_CX + 106, y + 46, K_CW - 118));
    AddPC(Label(m_hwnd, L"Username:", K_CX + 12, y + 76, 92));
    AddPC(Edit(m_hwnd, ID_P_EDIT4, K_CX + 106, y + 74, K_CW - 118));
    AddPC(Label(m_hwnd, L"Password:", K_CX + 12, y + 104, 92));
    AddPC(Edit(m_hwnd, ID_P_EDIT5, K_CX + 106, y + 102, K_CW - 118, ES_PASSWORD));
    AddPC(Label(m_hwnd, L"Install Root:", K_CX + 12, y + 132, 92));
    AddPC(Edit(m_hwnd, ID_P_EDIT6, K_CX + 106, y + 130, K_CW - 118));
    AddPC(SmallLabel(m_hwnd, L"Bearer tokens are issued automatically after username/password sign-in.",
                     K_CX + 12, y + 160, K_CW - 24));
    y += 184;

    AddPC(Group(m_hwnd, L" Local ROM Libraries ", K_CX, y, K_CW, 68));
    AddPC(Btn(m_hwnd, L"Clear Local ROM Libraries", ID_P_BTN3, K_CX + 12, y + 24, 176, 26));
    AddPC(SmallLabel(m_hwnd, L"Removes local ROM scan paths. Steam, Epic, and GOG stay enabled.",
                     K_CX + 200, y + 26, K_CW - 212));
}

void SettingsWindow::BuildSteamPage() {
    int y = PageHeader(m_hwnd, m_pageControls, L"Steam");

    AddPC(Group(m_hwnd, L" Steam root path ", K_CX, y, K_CW, 70));
    AddPC(Label(m_hwnd, L"Path:", K_CX + 12, y + 22, 44));
    AddPC(Edit (m_hwnd, ID_P_EDIT1, K_CX + 58, y + 20, K_BX - K_CX - 64));
    AddPC(Btn  (m_hwnd, L"Browse…", ID_P_BTN1, K_BX, y + 20));
    AddPC(SmallLabel(m_hwnd, L"Leave blank to auto-detect from the registry.",
                     K_CX + 12, y + 48, K_CW - 24));
    y += 78;

    AddPC(Group(m_hwnd, L" Extra library folders ", K_CX, y, K_CW, 206));
    AddPC(SmallLabel(m_hwnd,
          L"Folders not in libraryfolders.vdf — games installed outside Steam's "
          L"own library manager.",
          K_CX + 12, y + 18, K_CW - 24));
    AddPC(ListBox(m_hwnd, ID_P_LIST1, K_CX + 12, y + 40, K_LW, 148));
    AddPC(Btn(m_hwnd, L"Add Folder…", ID_P_BTN2, K_BX, y + 40));
    AddPC(Btn(m_hwnd, L"Remove",      ID_P_BTN3, K_BX, y + 68));
}

void SettingsWindow::BuildEpicPage() {
    int y = PageHeader(m_hwnd, m_pageControls, L"Epic Games");

    AddPC(Group(m_hwnd, L" Manifest directories ", K_CX, y, K_CW, 250));
    AddPC(Label(m_hwnd,
          L"Epic stores game metadata in .item manifest files. Add entries here "
          L"only if EGL is installed to a non-standard location.",
          K_CX + 12, y + 18, K_CW - 24, 34));
    AddPC(Label(m_hwnd, L"Leave empty to auto-detect.",
                K_CX + 12, y + 56, K_CW - 24));
    AddPC(ListBox(m_hwnd, ID_P_LIST1, K_CX + 12, y + 78, K_LW, 158));
    AddPC(Btn(m_hwnd, L"Add Dir…", ID_P_BTN1, K_BX, y + 78));
    AddPC(Btn(m_hwnd, L"Remove",   ID_P_BTN2, K_BX, y + 106));
}

void SettingsWindow::BuildGogPage() {
    int y = PageHeader(m_hwnd, m_pageControls, L"GOG Galaxy");


    AddPC(Group(m_hwnd, L" Detection ", K_CX, y, K_CW, 66));
    AddPC(Label(m_hwnd,
          L"GOG games are detected automatically from the Windows registry.",
          K_CX + 12, y + 20, K_CW - 24));
    AddPC(Label(m_hwnd, L"No manual path configuration is needed.",
                K_CX + 12, y + 40, K_CW - 24));
}

// Column anchors for the Dolphin config groups.
static constexpr int D_COL1 = K_CX + 12;
static constexpr int D_COL2 = D_COL1 + 188;
static constexpr int D_COL3 = D_COL2 + 188;
static constexpr int D_CBW  = 170;   // combo / control width

void SettingsWindow::BuildDolphinPage() {
    int y = PageHeader(m_hwnd, m_pageControls, L"Dolphin");

    // ── Executable ────────────────────────────────────────────────────────────
    AddPC(Group(m_hwnd, L" Executable ", K_CX, y, K_CW, 120));
    AddPC(Label(m_hwnd, L"Path:", K_CX + 12, y + 22, 44));
    AddPC(Edit (m_hwnd, ID_P_EDIT1, K_CX + 58, y + 20, K_BX - K_CX - 64));
    AddPC(Btn  (m_hwnd, L"Browse…",          ID_P_BTN1, K_BX, y + 20));
    AddPC(Btn  (m_hwnd, L"Auto-detect",       ID_P_BTN2, K_BX, y + 48));
    AddPC(Btn  (m_hwnd, L"Get Dolphin\x2026",  ID_P_BTN5, K_BX, y + 76));
    AddEmulatorProgressBar(K_CX + 12, y + 82, K_BX - K_CX - 16);
    AddPC(StatLabel(m_hwnd, L"", ID_P_STAT1, K_CX + 12, y + 100, K_BX - K_CX - 16));
    y += 130;

    // ── Graphics & Enhancements ────────────────────────────────────────────────
    AddPC(Group(m_hwnd, L" Graphics & Enhancements ", K_CX, y, K_CW, 132));
    AddPC(SmallLabel(m_hwnd, L"Backend",      D_COL1, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_D_BACKEND,    D_COL1, y + 34, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Internal resolution", D_COL2, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_D_RES,        D_COL2, y + 34, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Aspect ratio", D_COL3, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_D_ASPECT,     D_COL3, y + 34, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Anti-aliasing (MSAA)", D_COL1, y + 64, D_CBW));
    AddPC(Combo     (m_hwnd, ID_D_MSAA,       D_COL1, y + 80, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Anisotropic filtering", D_COL2, y + 64, D_CBW));
    AddPC(Combo     (m_hwnd, ID_D_ANISO,      D_COL2, y + 80, D_CBW));
    AddPC(Check(m_hwnd, L"Fullscreen", ID_D_FULLSCREEN, D_COL1,       y + 108, 110));
    AddPC(Check(m_hwnd, L"V-Sync",     ID_D_VSYNC,      D_COL1 + 116, y + 108, 90));
    AddPC(Check(m_hwnd, L"SSAA (supersampling)", ID_D_SSAA, D_COL1 + 212, y + 108, 180));
    y += 142;

    // ── Core / General ─────────────────────────────────────────────────────────
    AddPC(Group(m_hwnd, L" Core / General ", K_CX, y, K_CW, 84));
    AddPC(Check(m_hwnd, L"Dual core",     ID_D_DUALCORE, D_COL1,       y + 20, 100));
    AddPC(Check(m_hwnd, L"Enable cheats", ID_D_CHEATS,   D_COL1 + 108, y + 20, 120));
    AddPC(Check(m_hwnd, L"Overclock CPU", ID_D_OCEN,     D_COL1 + 236, y + 20, 110));
    AddPC(Edit (m_hwnd, ID_D_OCPCT, D_COL1 + 348, y + 18, 44));
    AddPC(Label(m_hwnd, L"%", D_COL1 + 396, y + 22, 16));
    AddPC(Label(m_hwnd, L"GameCube language", D_COL1, y + 52, 130));
    AddPC(Combo(m_hwnd, ID_D_LANG, D_COL1 + 140, y + 50, D_CBW));
    y += 94;

    // ── Audio ──────────────────────────────────────────────────────────────────
    AddPC(Group(m_hwnd, L" Audio ", K_CX, y, K_CW, 60));
    AddPC(Label(m_hwnd, L"Backend", D_COL1, y + 24, 56));
    AddPC(Combo(m_hwnd, ID_D_AUDIO, D_COL1 + 60, y + 22, 150));
    AddPC(Label(m_hwnd, L"Volume", D_COL1 + 230, y + 24, 52));
    AddPC(Edit (m_hwnd, ID_D_VOLUME, D_COL1 + 286, y + 22, 44));
    AddPC(Label(m_hwnd, L"%", D_COL1 + 334, y + 24, 16));
    AddPC(Check(m_hwnd, L"DSP HLE (fast)", ID_D_DSPHLE, D_COL1 + 360, y + 24, 160));
    y += 70;

    // ── Controllers ────────────────────────────────────────────────────────────
    AddPC(Group(m_hwnd, L" Controllers ", K_CX, y, K_CW, 76));
    AddPC(Btn  (m_hwnd, L"Apply Gamepad Preset", ID_D_CTRL_PRESET, D_COL1, y + 18, 170, 26));
    AddPC(Check(m_hwnd, L"Also map Wii Remote", ID_D_CTRL_WIIMOTE, D_COL1 + 184, y + 22, 200));
    AddPC(Btn  (m_hwnd, L"Open Dolphin (Controllers menu)\x2026", ID_D_CTRL_OPEN,
                D_COL1, y + 46, 250, 26));
    AddPC(SmallLabel(m_hwnd,
          L"Preset maps an Xbox-style pad; fine-tune in Dolphin's own dialog.",
          D_COL1 + 260, y + 50, K_CW - 284));
    y += 84;
}

void SettingsWindow::BuildRyujinxPage() {
    int y = PageHeader(m_hwnd, m_pageControls, L"Ryujinx");


    AddPC(Group(m_hwnd, L" Executable ", K_CX, y, K_CW, 144));
    AddPC(Label(m_hwnd, L"Path:", K_CX + 12, y + 22, 44));
    AddPC(Edit (m_hwnd, ID_P_EDIT1, K_CX + 58, y + 20, K_BX - K_CX - 64));
    AddPC(Btn  (m_hwnd, L"Browse…",          ID_P_BTN1, K_BX, y + 20));
    AddPC(Btn  (m_hwnd, L"Auto-detect",       ID_P_BTN2, K_BX, y + 48));
    AddPC(SmallLabel(m_hwnd, L"Searches common install locations for Ryujinx.exe.",
                     K_CX + 12, y + 52, K_BX - K_CX - 16));
    AddPC(Btn  (m_hwnd, L"Get Ryujinx\x2026",  ID_P_BTN5, K_BX, y + 76));
    AddEmulatorProgressBar(K_CX + 12, y + 104, K_CW - 24);
    AddPC(StatLabel(m_hwnd, L"", ID_P_STAT1,
                    K_CX + 12, y + 120, K_CW - 24));
    y += 152;
}

void SettingsWindow::BuildRpcs3Page() {
    int y = PageHeader(m_hwnd, m_pageControls, L"RPCS3");

    AddPC(Group(m_hwnd, L" Executable ", K_CX, y, K_CW, 120));
    AddPC(Label(m_hwnd, L"Path:", K_CX + 12, y + 22, 44));
    AddPC(Edit (m_hwnd, ID_P_EDIT1, K_CX + 58, y + 20, K_BX - K_CX - 64));
    AddPC(Btn  (m_hwnd, L"Browse…",        ID_P_BTN1, K_BX, y + 20));
    AddPC(Btn  (m_hwnd, L"Download latest", ID_P_BTN5, K_BX, y + 48));
    AddEmulatorProgressBar(K_CX + 12, y + 82, K_BX - K_CX - 16);
    AddPC(StatLabel(m_hwnd, L"Checking for updates\x2026", ID_P_STAT1,
                    K_CX + 12, y + 100, K_BX - K_CX - 16, 20));
    y += 130;

    // ── Graphics (RPCS3) ───────────────────────────────────────────────────────
    AddPC(Group(m_hwnd, L" Graphics ", K_CX, y, K_CW, 130));
    AddPC(SmallLabel(m_hwnd, L"Renderer",     D_COL1, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C1,        D_COL1, y + 34, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Resolution",   D_COL2, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C2,        D_COL2, y + 34, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Aspect ratio", D_COL3, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C3,        D_COL3, y + 34, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Frame limit",  D_COL1, y + 64, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C4,        D_COL1, y + 80, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Anisotropic filtering", D_COL2, y + 64, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C5,        D_COL2, y + 80, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Resolution scale %", D_COL3, y + 64, 120));
    AddPC(Edit      (m_hwnd, ID_EC_E1,        D_COL3, y + 80, 70));
    AddPC(Check(m_hwnd, L"V-Sync",  ID_EC_K1, D_COL1,       y + 110, 80));
    AddPC(Check(m_hwnd, L"MSAA",    ID_EC_K2, D_COL1 + 84,  y + 110, 80));
    AddPC(Check(m_hwnd, L"Stretch to display", ID_EC_K3, D_COL2,     y + 110, 150));
    AddPC(Check(m_hwnd, L"Write color buffers", ID_EC_K4, D_COL2 + 150, y + 110, 160));
    y += 140;

    // ── Audio / Controllers (RPCS3) ────────────────────────────────────────────
    AddPC(Group(m_hwnd, L" Audio & Controllers ", K_CX, y, K_CW, 80));
    AddPC(SmallLabel(m_hwnd, L"Audio backend", D_COL1, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C6,         D_COL1, y + 34, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Master volume", D_COL2, y + 18, 120));
    AddPC(Edit      (m_hwnd, ID_EC_E2,         D_COL2, y + 34, 70));
    AddPC(Btn  (m_hwnd, L"Open RPCS3\x2026", ID_EC_B1, D_COL3, y + 32, 150, 26));
    y += 90;
}

void SettingsWindow::BuildN64Page() {
    int y = PageHeader(m_hwnd, m_pageControls, L"N64 Emulator");

    AddPC(Group(m_hwnd, L" Executable ", K_CX, y, K_CW, 130));
    AddPC(Label(m_hwnd, L"Path:", K_CX + 12, y + 22, 44));
    AddPC(Edit (m_hwnd, ID_P_EDIT1, K_CX + 58, y + 20, K_BX - K_CX - 64));
    AddPC(Btn  (m_hwnd, L"Browse…",        ID_P_BTN1, K_BX, y + 20));
    AddPC(Btn  (m_hwnd, L"Download latest", ID_P_BTN5, K_BX, y + 48));
    AddPC(SmallLabel(m_hwnd,
          L"Recommended: Gopher64 \x2014 modern Rust-based, 4-player, high accuracy.",
          K_CX + 12, y + 78, K_CW - 24));
    AddEmulatorProgressBar(K_CX + 12, y + 96, K_BX - K_CX - 16);
    AddPC(StatLabel(m_hwnd, L"Checking for updates\x2026", ID_P_STAT1,
                    K_CX + 12, y + 110, K_BX - K_CX - 16, 20));
    y += 140;

    // ── Graphics (gopher64) ────────────────────────────────────────────────────
    AddPC(Group(m_hwnd, L" Graphics ", K_CX, y, K_CW, 116));
    AddPC(SmallLabel(m_hwnd, L"Internal resolution", D_COL1, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C1,               D_COL1, y + 34, D_CBW));
    AddPC(Check(m_hwnd, L"Supersampling (SSAA)", ID_EC_K1, D_COL2,     y + 36, 170));
    AddPC(Check(m_hwnd, L"Integer scaling",      ID_EC_K2, D_COL3,     y + 36, 150));
    AddPC(Check(m_hwnd, L"V-Sync",       ID_EC_K4, D_COL1,       y + 70, 90));
    AddPC(Check(m_hwnd, L"Widescreen",   ID_EC_K3, D_COL1 + 96,  y + 70, 110));
    AddPC(Check(m_hwnd, L"CRT filter",   ID_EC_K6, D_COL2,       y + 70, 110));
    AddPC(Check(m_hwnd, L"Start fullscreen", ID_EC_K5, D_COL2 + 116, y + 70, 140));
    y += 126;

    // ── Emulation / Controllers (gopher64) ─────────────────────────────────────
    AddPC(Group(m_hwnd, L" Emulation & Controllers ", K_CX, y, K_CW, 72));
    AddPC(Check(m_hwnd, L"Overclock CPU",        ID_EC_K7, D_COL1, y + 22, 160));
    AddPC(Check(m_hwnd, L"Disable Expansion Pak", ID_EC_K8, D_COL1, y + 46, 180));
    AddPC(SmallLabel(m_hwnd, L"Controllers auto-map to XInput pads.", D_COL2, y + 28, 200));
    AddPC(Btn  (m_hwnd, L"Open Gopher64\x2026", ID_EC_B1, D_COL3, y + 24, 150, 26));
    y += 82;
}

void SettingsWindow::BuildNesPage() {
    int y = PageHeader(m_hwnd, m_pageControls, L"NES Emulator");


    AddPC(Group(m_hwnd, L" Executable ", K_CX, y, K_CW, 156));
    AddPC(Label(m_hwnd, L"Path:", K_CX + 12, y + 22, 44));
    AddPC(Edit (m_hwnd, ID_P_EDIT1, K_CX + 58, y + 20, K_BX - K_CX - 64));
    AddPC(Btn  (m_hwnd, L"Browse…",        ID_P_BTN1, K_BX, y + 20));
    AddPC(Btn  (m_hwnd, L"Download latest", ID_P_BTN5, K_BX, y + 48));
    AddPC(SmallLabel(m_hwnd,
          L"Recommended: Mesen2 (mesen.ca) \x2014 cycle-accurate, 4-player, also handles SNES.",
          K_CX + 12, y + 76, K_CW - 24));
    AddEmulatorProgressBar(K_CX + 12, y + 112, K_CW - 24);
    AddPC(StatLabel(m_hwnd, L"Checking for updates\x2026", ID_P_STAT1,
                    K_CX + 12, y + 128, K_CW - 24, 20));
    y += 164;
}

void SettingsWindow::BuildSnesPage() {
    int y = PageHeader(m_hwnd, m_pageControls, L"SNES Emulator");


    AddPC(Group(m_hwnd, L" Executable ", K_CX, y, K_CW, 156));
    AddPC(Label(m_hwnd, L"Path:", K_CX + 12, y + 22, 44));
    AddPC(Edit (m_hwnd, ID_P_EDIT1, K_CX + 58, y + 20, K_BX - K_CX - 64));
    AddPC(Btn  (m_hwnd, L"Browse…",        ID_P_BTN1, K_BX, y + 20));
    AddPC(Btn  (m_hwnd, L"Download latest", ID_P_BTN5, K_BX, y + 48));
    AddPC(SmallLabel(m_hwnd,
          L"Recommended: Mesen2 (mesen.ca) \x2014 same exe as NES, cycle-accurate, 4-player.",
          K_CX + 12, y + 76, K_CW - 24));
    AddEmulatorProgressBar(K_CX + 12, y + 112, K_CW - 24);
    AddPC(StatLabel(m_hwnd, L"Checking for updates\x2026", ID_P_STAT1,
                    K_CX + 12, y + 128, K_CW - 24, 20));
    y += 164;
}

// ── PS1 / PS2 / Xbox 360 ──────────────────────────────────────────────────────

void SettingsWindow::BuildPS1Page() {
    int y = PageHeader(m_hwnd, m_pageControls, L"PS1 Emulator");
    AddPC(Group(m_hwnd, L" Executable ", K_CX, y, K_CW, 120));
    AddPC(Label(m_hwnd, L"Path:", K_CX + 12, y + 22, 44));
    AddPC(Edit (m_hwnd, ID_P_EDIT1, K_CX + 58, y + 20, K_BX - K_CX - 64));
    AddPC(Btn  (m_hwnd, L"Browse…",         ID_P_BTN1, K_BX, y + 20));
    AddPC(Btn  (m_hwnd, L"Download latest", ID_P_BTN5, K_BX, y + 48));
    AddEmulatorProgressBar(K_CX + 12, y + 76, K_CW - 24);
    AddPC(StatLabel(m_hwnd, L"Checking for updates\x2026", ID_P_STAT1,
                    K_CX + 12, y + 92, K_CW - 24, 20));
    y += 128;
}

void SettingsWindow::BuildPS2Page() {
    int y = PageHeader(m_hwnd, m_pageControls, L"PS2 Emulator");
    AddPC(Group(m_hwnd, L" Executable ", K_CX, y, K_CW, 120));
    AddPC(Label(m_hwnd, L"Path:", K_CX + 12, y + 22, 44));
    AddPC(Edit (m_hwnd, ID_P_EDIT1, K_CX + 58, y + 20, K_BX - K_CX - 64));
    AddPC(Btn  (m_hwnd, L"Browse…",         ID_P_BTN1, K_BX, y + 20));
    AddPC(Btn  (m_hwnd, L"Download latest", ID_P_BTN5, K_BX, y + 48));
    AddEmulatorProgressBar(K_CX + 12, y + 82, K_BX - K_CX - 16);
    AddPC(StatLabel(m_hwnd, L"Checking for updates\x2026", ID_P_STAT1,
                    K_CX + 12, y + 100, K_BX - K_CX - 16, 20));
    y += 130;

    // ── Graphics (PCSX2 / GS) ──────────────────────────────────────────────────
    AddPC(Group(m_hwnd, L" Graphics ", K_CX, y, K_CW, 130));
    AddPC(SmallLabel(m_hwnd, L"Renderer",          D_COL1, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C1,             D_COL1, y + 34, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Internal resolution", D_COL2, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C2,             D_COL2, y + 34, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Aspect ratio",      D_COL3, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C3,             D_COL3, y + 34, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Anisotropic filtering", D_COL1, y + 64, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C4,             D_COL1, y + 80, D_CBW));
    AddPC(Check(m_hwnd, L"V-Sync",       ID_EC_K1, D_COL2,       y + 70, 90));
    AddPC(Check(m_hwnd, L"Mipmapping",   ID_EC_K2, D_COL2 + 96,  y + 70, 110));
    AddPC(Check(m_hwnd, L"FXAA",         ID_EC_K3, D_COL2,       y + 96, 90));
    AddPC(Check(m_hwnd, L"Start fullscreen", ID_EC_K4, D_COL2 + 96, y + 96, 130));
    y += 140;

    // ── Core / Audio (PCSX2) ───────────────────────────────────────────────────
    AddPC(Group(m_hwnd, L" Core & Audio ", K_CX, y, K_CW, 96));
    AddPC(Check(m_hwnd, L"Enable cheats (pnach)", ID_EC_K5, D_COL1, y + 22, 170));
    AddPC(Check(m_hwnd, L"Widescreen patches",    ID_EC_K6, D_COL1, y + 48, 170));
    AddPC(Check(m_hwnd, L"Fast boot (skip BIOS)",  ID_EC_K7, D_COL1, y + 74, 170));
    AddPC(SmallLabel(m_hwnd, L"Audio backend", D_COL2, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C5,         D_COL2, y + 34, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Volume (0\x2013""100)", D_COL3, y + 18, 120));
    AddPC(Edit      (m_hwnd, ID_EC_E1,         D_COL3, y + 34, 70));
    y += 106;

    // ── Controllers (PCSX2) ────────────────────────────────────────────────────
    AddPC(Group(m_hwnd, L" Controllers ", K_CX, y, K_CW, 64));
    AddPC(SmallLabel(m_hwnd,
          L"PCSX2 has a full controller mapper \x2014 keyboard defaults are preset.",
          D_COL1, y + 24, K_CW - 180));
    AddPC(Btn  (m_hwnd, L"Open PCSX2\x2026", ID_EC_B1, D_COL3, y + 20, 150, 26));
    y += 74;
}

void SettingsWindow::BuildXbox360Page() {
    int y = PageHeader(m_hwnd, m_pageControls, L"Xbox 360 Emulator");
    AddPC(Group(m_hwnd, L" Executable ", K_CX, y, K_CW, 120));
    AddPC(Label(m_hwnd, L"Path:", K_CX + 12, y + 22, 44));
    AddPC(Edit (m_hwnd, ID_P_EDIT1, K_CX + 58, y + 20, K_BX - K_CX - 64));
    AddPC(Btn  (m_hwnd, L"Browse…",         ID_P_BTN1, K_BX, y + 20));
    AddPC(Btn  (m_hwnd, L"Download latest", ID_P_BTN5, K_BX, y + 48));
    AddEmulatorProgressBar(K_CX + 12, y + 82, K_BX - K_CX - 16);
    AddPC(StatLabel(m_hwnd, L"Checking for updates\x2026", ID_P_STAT1,
                    K_CX + 12, y + 100, K_BX - K_CX - 16, 20));
    y += 130;

    // ── Graphics (Xenia) ───────────────────────────────────────────────────────
    AddPC(Group(m_hwnd, L" Graphics ", K_CX, y, K_CW, 96));
    AddPC(SmallLabel(m_hwnd, L"Graphics backend", D_COL1, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C1,            D_COL1, y + 34, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Resolution scale", D_COL2, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C2,            D_COL2, y + 34, D_CBW));
    AddPC(SmallLabel(m_hwnd, L"Frame-rate limit", D_COL3, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C3,            D_COL3, y + 34, D_CBW));
    AddPC(Check(m_hwnd, L"V-Sync",     ID_EC_K1, D_COL1,       y + 66, 90));
    AddPC(Check(m_hwnd, L"Fullscreen", ID_EC_K2, D_COL1 + 96,  y + 66, 110));
    AddPC(Check(m_hwnd, L"Letterbox (keep aspect)", ID_EC_K3, D_COL1 + 212, y + 66, 200));
    y += 106;

    // ── Controllers (Xenia) ────────────────────────────────────────────────────
    AddPC(Group(m_hwnd, L" Controllers ", K_CX, y, K_CW, 70));
    AddPC(SmallLabel(m_hwnd, L"Input backend", D_COL1, y + 18, D_CBW));
    AddPC(Combo     (m_hwnd, ID_EC_C4,         D_COL1, y + 34, D_CBW));
    AddPC(Check(m_hwnd, L"Controller vibration", ID_EC_K4, D_COL2, y + 36, 180));
    AddPC(Btn  (m_hwnd, L"Open Xenia\x2026", ID_EC_B1, D_COL3, y + 32, 150, 26));
    AddPC(SmallLabel(m_hwnd, L"Xenia auto-detects XInput controllers \x2014 no mapping needed.",
                     D_COL1, y + 56, K_CW - 28));
    y += 80;
}

void SettingsWindow::BuildXboxPage() {
    int y = PageHeader(m_hwnd, m_pageControls, L"Xbox Emulator");
    AddPC(Group(m_hwnd, L" Executable ", K_CX, y, K_CW, 120));
    AddPC(Label(m_hwnd, L"Path:", K_CX + 12, y + 22, 44));
    AddPC(Edit (m_hwnd, ID_P_EDIT1, K_CX + 58, y + 20, K_BX - K_CX - 64));
    AddPC(Btn  (m_hwnd, L"Browse…",         ID_P_BTN1, K_BX, y + 20));
    AddPC(Btn  (m_hwnd, L"Download latest", ID_P_BTN5, K_BX, y + 48));
    AddEmulatorProgressBar(K_CX + 12, y + 76, K_CW - 24);
    AddPC(StatLabel(m_hwnd, L"Checking for updates\x2026", ID_P_STAT1,
                    K_CX + 12, y + 92, K_CW - 24, 20));
    y += 128;
}

void SettingsWindow::BuildCustomPage(int /*libIdx*/) {
    int y = PageHeader(m_hwnd, m_pageControls, L"Custom Library");

    AddPC(Label(m_hwnd, L"Name:", K_CX, y + 4, 46));
    AddPC(Edit (m_hwnd, ID_P_EDIT1, K_CX + 50, y + 2, K_CW - 50));
    y += 32;

    AddPC(Group(m_hwnd, L" Directories to scan ", K_CX, y, K_CW, 240));
    AddPC(SmallLabel(m_hwnd,
          L"Each immediate subdirectory is treated as a separate game — "
          L"the folder name becomes the game title.",
          K_CX + 12, y + 18, K_CW - 24));
    AddPC(ListBox(m_hwnd, ID_P_LIST1, K_CX + 12, y + 40, K_LW, 182));
    AddPC(Btn(m_hwnd, L"Add Folder…", ID_P_BTN1, K_BX, y + 40));
    AddPC(Btn(m_hwnd, L"Remove",      ID_P_BTN2, K_BX, y + 68));
    y += 248;

    AddPC(Btn(m_hwnd, L"Delete This Library", ID_P_BTN3, K_CX, y, 150, 26));
}

// ─── Page loaders / savers ────────────────────────────────────────────────────

static bool ReadStartupReg() {
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS) return false;
    DWORD sz = 0;
    bool found = RegQueryValueExW(hk, L"ArcadeLauncher",
                                  nullptr, nullptr, nullptr, &sz) == ERROR_SUCCESS;
    RegCloseKey(hk);
    return found;
}

static void WriteStartupReg(bool enable) {
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &hk) != ERROR_SUCCESS) return;
    if (enable) {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring val = std::wstring(L"\"") + exe + L"\" --tray";
        RegSetValueExW(hk, L"ArcadeLauncher", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(val.c_str()),
            static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hk, L"ArcadeLauncher");
    }
    RegCloseKey(hk);
}

void SettingsWindow::LoadGeneralPage() {
    Chk(PC(ID_P_CHK1), m_work.startFullscreen);
    Chk(PC(ID_P_CHK2), m_work.minimizeOnLaunch);
    Chk(PC(ID_P_CHK3), ReadStartupReg());
    SetWindowTextW(PC(ID_P_EDIT1), m_work.igdbClientId.c_str());
    SetWindowTextW(PC(ID_P_EDIT2), m_work.igdbClientSecret.c_str());
    Chk(PC(ID_P_CHK4), m_work.server.enabled);
    SetWindowTextW(PC(ID_P_EDIT3), m_work.server.baseUrl.c_str());
    SetWindowTextW(PC(ID_P_EDIT4), m_work.server.username.c_str());
    SetWindowTextW(PC(ID_P_EDIT5), m_work.server.password.c_str());
    SetWindowTextW(PC(ID_P_EDIT6), m_work.server.installRoot.c_str());
}
void SettingsWindow::SaveGeneralPage() {
    m_work.startFullscreen  = IsChk(PC(ID_P_CHK1));
    m_work.minimizeOnLaunch = IsChk(PC(ID_P_CHK2));
    WriteStartupReg(IsChk(PC(ID_P_CHK3)));
    m_work.igdbClientId     = GetTxt(PC(ID_P_EDIT1));
    m_work.igdbClientSecret = GetTxt(PC(ID_P_EDIT2));
    m_work.server.enabled     = IsChk(PC(ID_P_CHK4));
    m_work.server.baseUrl     = GetTxt(PC(ID_P_EDIT3));
    m_work.server.username    = GetTxt(PC(ID_P_EDIT4));
    m_work.server.password    = GetTxt(PC(ID_P_EDIT5));
    m_work.server.installRoot = GetTxt(PC(ID_P_EDIT6));
}

void SettingsWindow::LoadSteamPage() {
    auto& lb = m_work.libraries;
    SetWindowTextW(PC(ID_P_EDIT1), lb.steamPath.c_str());
    VecToList(PC(ID_P_LIST1), lb.steamExtraFolders);
}
void SettingsWindow::SaveSteamPage() {
    auto& lb = m_work.libraries;
    lb.steamPath    = GetTxt(PC(ID_P_EDIT1));
    ListToVec(PC(ID_P_LIST1), lb.steamExtraFolders);
}

void SettingsWindow::LoadEpicPage() {
    auto& lb = m_work.libraries;
    VecToList(PC(ID_P_LIST1), lb.epicManifestDirs);
}
void SettingsWindow::SaveEpicPage() {
    auto& lb = m_work.libraries;
    ListToVec(PC(ID_P_LIST1), lb.epicManifestDirs);
}

void SettingsWindow::LoadGogPage() {
}
void SettingsWindow::SaveGogPage() {
}

// ── Dolphin config value tables (combo index <-> stored value) ────────────────
static const wchar_t* kBackendVals[] = { L"D3D", L"D3D12", L"Vulkan", L"OGL", L"Software Renderer" };
static const int      kResVals[]     = { 0, 1, 2, 3, 4, 5, 6, 8 };
static const int      kMsaaVals[]    = { 1, 2, 4, 8 };
static const wchar_t* kAudioVals[]   = { L"Cubeb", L"XAudio2", L"OpenAL", L"No audio output" };

template <class T, size_t N>
static int IndexOf(const T (&arr)[N], const T& v, int def = 0) {
    for (size_t i = 0; i < N; ++i) if (arr[i] == v) return (int)i;
    return def;
}
static int IndexOfStr(const wchar_t* const* arr, size_t n, const std::wstring& v, int def = 0) {
    for (size_t i = 0; i < n; ++i) if (v == arr[i]) return (int)i;
    return def;
}

void SettingsWindow::LoadDolphinPage() {
    auto& e = m_work.emulators;
    SetWindowTextW(PC(ID_P_EDIT1), e.dolphinPath.c_str());
    SetWindowTextW(PC(ID_P_STAT1), L"Builds available at dolphin-emu.org/download");

    DolphinSettings s;
    DolphinLoadSettings(e.dolphinPath, s);

    ComboFill(PC(ID_D_BACKEND), { L"Direct3D 11", L"Direct3D 12", L"Vulkan", L"OpenGL", L"Software" },
              IndexOfStr(kBackendVals, 5, s.backend));
    ComboFill(PC(ID_D_RES),
              { L"Auto (window size)", L"1\xD7 Native (640\xD7""528)", L"2\xD7 (720p)",
                L"3\xD7 (1080p)", L"4\xD7 (1440p)", L"5\xD7", L"6\xD7 (4K)", L"8\xD7" },
              IndexOf(kResVals, s.internalRes));
    ComboFill(PC(ID_D_ASPECT), { L"Auto", L"Force 16:9", L"Force 4:3", L"Stretch" },
              (s.aspectRatio >= 0 && s.aspectRatio <= 3) ? s.aspectRatio : 0);
    ComboFill(PC(ID_D_MSAA), { L"None", L"2\xD7", L"4\xD7", L"8\xD7" }, IndexOf(kMsaaVals, s.msaa));
    ComboFill(PC(ID_D_ANISO), { L"1\xD7 (off)", L"2\xD7", L"4\xD7", L"8\xD7", L"16\xD7" },
              (s.maxAnisotropy >= 0 && s.maxAnisotropy <= 4) ? s.maxAnisotropy : 0);
    ComboFill(PC(ID_D_LANG),
              { L"English", L"German", L"French", L"Spanish", L"Italian", L"Dutch" },
              (s.gcLanguage >= 0 && s.gcLanguage <= 5) ? s.gcLanguage : 0);
    ComboFill(PC(ID_D_AUDIO), { L"Cubeb", L"XAudio2", L"OpenAL", L"No audio output" },
              IndexOfStr(kAudioVals, 4, s.audioBackend));

    Chk(PC(ID_D_FULLSCREEN), s.fullscreen);
    Chk(PC(ID_D_VSYNC),      s.vsync);
    Chk(PC(ID_D_SSAA),       s.ssaa);
    Chk(PC(ID_D_DUALCORE),   s.dualCore);
    Chk(PC(ID_D_CHEATS),     s.enableCheats);
    Chk(PC(ID_D_OCEN),       s.overclockEnable);
    Chk(PC(ID_D_DSPHLE),     s.dspHLE);
    Chk(PC(ID_D_CTRL_WIIMOTE), false);
    SetWindowTextW(PC(ID_D_OCPCT),  std::to_wstring(s.overclockPercent).c_str());
    SetWindowTextW(PC(ID_D_VOLUME), std::to_wstring(s.volume).c_str());
}
void SettingsWindow::SaveDolphinPage() {
    auto& e = m_work.emulators;
    e.dolphinPath = GetTxt(PC(ID_P_EDIT1));
    if (e.dolphinPath.empty()) return;  // nothing to configure until Dolphin exists

    auto clampInt = [](const std::wstring& t, int lo, int hi, int def) {
        if (t.empty()) return def;
        try { int v = std::stoi(t); return v < lo ? lo : (v > hi ? hi : v); }
        catch (...) { return def; }
    };

    DolphinSettings s;
    int bi = ComboSel(PC(ID_D_BACKEND)); s.backend = kBackendVals[(bi >= 0 && bi < 5) ? bi : 0];
    int ri = ComboSel(PC(ID_D_RES));     s.internalRes = kResVals[(ri >= 0 && ri < 8) ? ri : 1];
    int ai = ComboSel(PC(ID_D_ASPECT));  s.aspectRatio = (ai >= 0) ? ai : 0;
    int mi = ComboSel(PC(ID_D_MSAA));    s.msaa = kMsaaVals[(mi >= 0 && mi < 4) ? mi : 0];
    int ni = ComboSel(PC(ID_D_ANISO));   s.maxAnisotropy = (ni >= 0) ? ni : 0;
    int li = ComboSel(PC(ID_D_LANG));    s.gcLanguage = (li >= 0) ? li : 0;
    int di = ComboSel(PC(ID_D_AUDIO));   s.audioBackend = kAudioVals[(di >= 0 && di < 4) ? di : 0];

    s.fullscreen      = IsChk(PC(ID_D_FULLSCREEN));
    s.vsync           = IsChk(PC(ID_D_VSYNC));
    s.ssaa            = IsChk(PC(ID_D_SSAA));
    s.dualCore        = IsChk(PC(ID_D_DUALCORE));
    s.enableCheats    = IsChk(PC(ID_D_CHEATS));
    s.overclockEnable = IsChk(PC(ID_D_OCEN));
    s.dspHLE          = IsChk(PC(ID_D_DSPHLE));
    s.overclockPercent = clampInt(GetTxt(PC(ID_D_OCPCT)), 10, 400, 100);
    s.volume           = clampInt(GetTxt(PC(ID_D_VOLUME)), 0, 100, 100);

    DolphinApplySettings(e.dolphinPath, s);
}

void SettingsWindow::LoadRyujinxPage() {
    auto& e = m_work.emulators;
    SetWindowTextW(PC(ID_P_EDIT1), e.ryujinxPath.c_str());
    SetWindowTextW(PC(ID_P_STAT1), L"Project discontinued Oct 2024 \x2014 no new versions available.");
}
void SettingsWindow::SaveRyujinxPage() {
    auto& e = m_work.emulators;
    e.ryujinxPath    = GetTxt(PC(ID_P_EDIT1));
}

// ── RPCS3 config value tables (combo index <-> stored value) ──────────────────
static const wchar_t* kRpcs3RendVals[]   = { L"Vulkan", L"OpenGL", L"Null" };
static const wchar_t* kRpcs3ResVals[]    = { L"1280x720", L"1920x1080" };
static const wchar_t* kRpcs3AspectVals[] = { L"16:9", L"4:3" };
static const wchar_t* kRpcs3FpsVals[]    = { L"Auto", L"Off", L"30", L"60", L"120" };
static const int      kRpcs3AnisoVals[]  = { 0, 2, 4, 8, 16 };
static const wchar_t* kRpcs3AudioVals[]  = { L"Cubeb", L"XAudio2", L"FAudio" };

void SettingsWindow::LoadRpcs3Page() {
    auto& e = m_work.emulators;
    SetWindowTextW(PC(ID_P_EDIT1), e.rpcs3Path.c_str());
    SetVersionLabel(e.rpcs3Tag, {});
    CheckEmulatorUpdateAsync(m_hwnd, PAGE_RPCS3, "RPCS3/rpcs3-binaries-win");

    Rpcs3Settings s;
    Rpcs3LoadSettings(e.rpcs3Path, s);

    ComboFill(PC(ID_EC_C1), { L"Vulkan", L"OpenGL", L"Disabled (Null)" },
              IndexOfStr(kRpcs3RendVals, 3, s.renderer));
    ComboFill(PC(ID_EC_C2), { L"1280\xD7""720 (720p)", L"1920\xD7""1080 (1080p)" },
              IndexOfStr(kRpcs3ResVals, 2, s.resolution));
    ComboFill(PC(ID_EC_C3), { L"16:9", L"4:3" },
              IndexOfStr(kRpcs3AspectVals, 2, s.aspect));
    ComboFill(PC(ID_EC_C4), { L"Auto", L"Off", L"30 fps", L"60 fps", L"120 fps" },
              IndexOfStr(kRpcs3FpsVals, 5, s.frameLimit));
    ComboFill(PC(ID_EC_C5), { L"Auto", L"2\xD7", L"4\xD7", L"8\xD7", L"16\xD7" },
              IndexOf(kRpcs3AnisoVals, s.aniso));
    ComboFill(PC(ID_EC_C6), { L"Cubeb", L"XAudio2", L"FAudio" },
              IndexOfStr(kRpcs3AudioVals, 3, s.audioRenderer));

    Chk(PC(ID_EC_K1), s.vsync);
    Chk(PC(ID_EC_K2), s.msaa != L"Disabled");
    Chk(PC(ID_EC_K3), s.stretchScreen);
    Chk(PC(ID_EC_K4), s.writeColorBuf);
    SetWindowTextW(PC(ID_EC_E1), std::to_wstring(s.resScale).c_str());
    SetWindowTextW(PC(ID_EC_E2), std::to_wstring(s.masterVolume).c_str());
}
void SettingsWindow::SaveRpcs3Page() {
    auto& e = m_work.emulators;
    e.rpcs3Path    = GetTxt(PC(ID_P_EDIT1));
    if (e.rpcs3Path.empty()) return;

    Rpcs3Settings s;
    int ri = ComboSel(PC(ID_EC_C1)); s.renderer   = kRpcs3RendVals[(ri >= 0 && ri < 3) ? ri : 0];
    int si = ComboSel(PC(ID_EC_C2)); s.resolution = kRpcs3ResVals[(si >= 0 && si < 2) ? si : 0];
    int pi = ComboSel(PC(ID_EC_C3)); s.aspect     = kRpcs3AspectVals[(pi >= 0 && pi < 2) ? pi : 0];
    int fi = ComboSel(PC(ID_EC_C4)); s.frameLimit = kRpcs3FpsVals[(fi >= 0 && fi < 5) ? fi : 0];
    int ni = ComboSel(PC(ID_EC_C5)); s.aniso      = kRpcs3AnisoVals[(ni >= 0 && ni < 5) ? ni : 0];
    int di = ComboSel(PC(ID_EC_C6)); s.audioRenderer = kRpcs3AudioVals[(di >= 0 && di < 3) ? di : 0];

    s.vsync         = IsChk(PC(ID_EC_K1));
    s.msaa          = IsChk(PC(ID_EC_K2)) ? L"Auto" : L"Disabled";
    s.stretchScreen = IsChk(PC(ID_EC_K3));
    s.writeColorBuf = IsChk(PC(ID_EC_K4));

    auto clamp = [](const std::wstring& t, int lo, int hi, int def) {
        if (t.empty()) return def;
        try { int v = std::stoi(t); return v < lo ? lo : (v > hi ? hi : v); } catch (...) { return def; }
    };
    s.resScale     = clamp(GetTxt(PC(ID_EC_E1)), 25, 300, 100);
    s.masterVolume = clamp(GetTxt(PC(ID_EC_E2)), 0, 200, 100);

    Rpcs3ApplySettings(e.rpcs3Path, s);
}

static const int kGopherUpscaleVals[] = { 1, 2, 3, 4, 6, 8 };

void SettingsWindow::LoadN64Page() {
    auto& e = m_work.emulators;
    SetWindowTextW(PC(ID_P_EDIT1), e.n64Path.c_str());
    SetVersionLabel(e.n64Tag, {});
    CheckEmulatorUpdateAsync(m_hwnd, PAGE_N64, "gopher64/gopher64");

    GopherSettings s;
    GopherLoadSettings(s);

    ComboFill(PC(ID_EC_C1),
              { L"Native (1\xD7)", L"2\xD7", L"3\xD7", L"4\xD7", L"6\xD7", L"8\xD7" },
              IndexOf(kGopherUpscaleVals, s.upscale, 0));
    Chk(PC(ID_EC_K1), s.ssaa);
    Chk(PC(ID_EC_K2), s.integerScaling);
    Chk(PC(ID_EC_K3), s.widescreen);
    Chk(PC(ID_EC_K4), s.vsync);
    Chk(PC(ID_EC_K5), s.fullscreen);
    Chk(PC(ID_EC_K6), s.crt);
    Chk(PC(ID_EC_K7), s.overclock);
    Chk(PC(ID_EC_K8), s.disableExpansionPak);
}
void SettingsWindow::SaveN64Page() {
    auto& e = m_work.emulators;
    e.n64Path    = GetTxt(PC(ID_P_EDIT1));

    // Load first so the un-exposed "usb" key (and anything else) is preserved.
    GopherSettings s;
    GopherLoadSettings(s);
    int ui = ComboSel(PC(ID_EC_C1)); s.upscale = kGopherUpscaleVals[(ui >= 0 && ui < 6) ? ui : 0];
    s.ssaa               = IsChk(PC(ID_EC_K1));
    s.integerScaling     = IsChk(PC(ID_EC_K2));
    s.widescreen         = IsChk(PC(ID_EC_K3));
    s.vsync              = IsChk(PC(ID_EC_K4));
    s.fullscreen         = IsChk(PC(ID_EC_K5));
    s.crt                = IsChk(PC(ID_EC_K6));
    s.overclock          = IsChk(PC(ID_EC_K7));
    s.disableExpansionPak= IsChk(PC(ID_EC_K8));
    GopherApplySettings(s);
}

void SettingsWindow::LoadNesPage() {
    auto& e = m_work.emulators;
    // Mesen2 handles both NES and SNES — if NES path is unset but SNES has the exe, inherit it
    if (e.nesPath.empty() && !e.snesPath.empty()) {
        e.nesPath = e.snesPath;
        if (e.nesTag.empty()) e.nesTag = e.snesTag;
    }
    SetWindowTextW(PC(ID_P_EDIT1), e.nesPath.c_str());
    SetVersionLabel(e.nesTag, {});
    CheckEmulatorUpdateAsync(m_hwnd, PAGE_NES, "SourMesen/Mesen2");
}
void SettingsWindow::SaveNesPage() {
    auto& e = m_work.emulators;
    e.nesPath    = GetTxt(PC(ID_P_EDIT1));
}

void SettingsWindow::LoadSnesPage() {
    auto& e = m_work.emulators;
    // Mesen2 handles both NES and SNES — if SNES path is unset but NES has the exe, inherit it
    if (e.snesPath.empty() && !e.nesPath.empty()) {
        e.snesPath = e.nesPath;
        if (e.snesTag.empty()) e.snesTag = e.nesTag;
    }
    SetWindowTextW(PC(ID_P_EDIT1), e.snesPath.c_str());
    SetVersionLabel(e.snesTag, {});
    CheckEmulatorUpdateAsync(m_hwnd, PAGE_SNES, "SourMesen/Mesen2");
}
void SettingsWindow::SaveSnesPage() {
    auto& e = m_work.emulators;
    e.snesPath    = GetTxt(PC(ID_P_EDIT1));
    // Mirror directly to live config — same pattern as SetPathForPage for the exe path,
    // ensures ROM dirs survive regardless of when *m_cfg = m_work runs.
    if (m_cfg) {
        m_cfg->emulators.snesPath    = e.snesPath;
    }
}

void SettingsWindow::LoadPS1Page() {
    auto& e = m_work.emulators;
    SetWindowTextW(PC(ID_P_EDIT1), e.duckstationPath.c_str());
    SetVersionLabel(e.duckstationTag, {});
    CheckEmulatorUpdateAsync(m_hwnd, PAGE_PS1, "stenzek/duckstation");
}
void SettingsWindow::SavePS1Page() {
    auto& e = m_work.emulators;
    e.duckstationPath    = GetTxt(PC(ID_P_EDIT1));
    if (m_cfg) {
        m_cfg->emulators.duckstationPath    = e.duckstationPath;
    }
}

// ── PCSX2 config value tables (combo index <-> stored value) ──────────────────
static const int      kPcsx2RendVals[]   = { -1, 3, 15, 14, 12, 13 };  // GSRendererType
static const int      kPcsx2UpscaleVals[]= { 1, 2, 3, 4, 5, 6, 8 };
static const wchar_t* kPcsx2AspectVals[] = { L"Auto 4:3/3:2", L"4:3", L"16:9", L"Stretch" };
static const int      kPcsx2AnisoVals[]  = { 0, 2, 4, 8, 16 };
static const wchar_t* kPcsx2AudioVals[]  = { L"Cubeb", L"XAudio2" };

void SettingsWindow::LoadPS2Page() {
    auto& e = m_work.emulators;
    SetWindowTextW(PC(ID_P_EDIT1), e.pcsx2Path.c_str());
    SetVersionLabel(e.pcsx2Tag, {});
    CheckEmulatorUpdateAsync(m_hwnd, PAGE_PS2, "PCSX2/pcsx2");

    Pcsx2Settings s;
    Pcsx2LoadSettings(e.pcsx2Path, s);

    ComboFill(PC(ID_EC_C1),
              { L"Automatic", L"Direct3D 11", L"Direct3D 12", L"Vulkan", L"OpenGL", L"Software" },
              IndexOf(kPcsx2RendVals, s.renderer));
    ComboFill(PC(ID_EC_C2),
              { L"Native (1\xD7)", L"2\xD7", L"3\xD7", L"4\xD7", L"5\xD7", L"6\xD7", L"8\xD7" },
              IndexOf(kPcsx2UpscaleVals, s.upscale, 0));
    ComboFill(PC(ID_EC_C3), { L"Auto (4:3 / 3:2)", L"4:3", L"16:9", L"Stretch" },
              IndexOfStr(kPcsx2AspectVals, 4, s.aspect));
    ComboFill(PC(ID_EC_C4), { L"Off", L"2\xD7", L"4\xD7", L"8\xD7", L"16\xD7" },
              IndexOf(kPcsx2AnisoVals, s.maxAniso));
    ComboFill(PC(ID_EC_C5), { L"Cubeb", L"XAudio2" },
              IndexOfStr(kPcsx2AudioVals, 2, s.audioBackend));

    Chk(PC(ID_EC_K1), s.vsync);
    Chk(PC(ID_EC_K2), s.mipmap);
    Chk(PC(ID_EC_K3), s.fxaa);
    Chk(PC(ID_EC_K4), s.fullscreen);
    Chk(PC(ID_EC_K5), s.cheats);
    Chk(PC(ID_EC_K6), s.widescreen);
    Chk(PC(ID_EC_K7), s.fastBoot);
    SetWindowTextW(PC(ID_EC_E1), std::to_wstring(s.volume).c_str());
}
void SettingsWindow::SavePS2Page() {
    auto& e = m_work.emulators;
    e.pcsx2Path    = GetTxt(PC(ID_P_EDIT1));
    if (m_cfg) {
        m_cfg->emulators.pcsx2Path    = e.pcsx2Path;
    }
    if (e.pcsx2Path.empty()) return;

    Pcsx2Settings s;
    int ri = ComboSel(PC(ID_EC_C1)); s.renderer = kPcsx2RendVals[(ri >= 0 && ri < 6) ? ri : 0];
    int ui = ComboSel(PC(ID_EC_C2)); s.upscale  = kPcsx2UpscaleVals[(ui >= 0 && ui < 7) ? ui : 0];
    int ai = ComboSel(PC(ID_EC_C3)); s.aspect   = kPcsx2AspectVals[(ai >= 0 && ai < 4) ? ai : 0];
    int ni = ComboSel(PC(ID_EC_C4)); s.maxAniso = kPcsx2AnisoVals[(ni >= 0 && ni < 5) ? ni : 0];
    int di = ComboSel(PC(ID_EC_C5)); s.audioBackend = kPcsx2AudioVals[(di >= 0 && di < 2) ? di : 0];

    s.vsync      = IsChk(PC(ID_EC_K1));
    s.mipmap     = IsChk(PC(ID_EC_K2));
    s.fxaa       = IsChk(PC(ID_EC_K3));
    s.fullscreen = IsChk(PC(ID_EC_K4));
    s.cheats     = IsChk(PC(ID_EC_K5));
    s.widescreen = IsChk(PC(ID_EC_K6));
    s.fastBoot   = IsChk(PC(ID_EC_K7));

    std::wstring vt = GetTxt(PC(ID_EC_E1));
    if (!vt.empty()) { try { int v = std::stoi(vt); s.volume = v < 0 ? 0 : (v > 100 ? 100 : v); } catch (...) {} }

    Pcsx2ApplySettings(e.pcsx2Path, s);
}

static const wchar_t* kXeniaGpuVals[] = { L"any", L"d3d12", L"vulkan" };
static const wchar_t* kXeniaHidVals[] = { L"any", L"xinput", L"sdl" };
static const int      kXeniaFpsVals[] = { 0, 30, 60, 120 };

void SettingsWindow::LoadXbox360Page() {
    auto& e = m_work.emulators;
    SetWindowTextW(PC(ID_P_EDIT1), e.xeniaPath.c_str());
    SetVersionLabel(e.xeniaTag, {});
    CheckEmulatorUpdateAsync(m_hwnd, PAGE_XBOX360, "xenia-canary/xenia-canary");

    XeniaSettings s;
    XeniaLoadSettings(e.xeniaPath, s);
    ComboFill(PC(ID_EC_C1), { L"Auto", L"Direct3D 12", L"Vulkan" },
              IndexOfStr(kXeniaGpuVals, 3, s.gpu));
    ComboFill(PC(ID_EC_C2), { L"1\xD7 (720p)", L"2\xD7 (1440p)", L"3\xD7 (4K)" },
              (s.resScale >= 1 && s.resScale <= 3) ? s.resScale - 1 : 0);
    ComboFill(PC(ID_EC_C3), { L"Unlimited", L"30 fps", L"60 fps", L"120 fps" },
              IndexOf(kXeniaFpsVals, s.frameLimit));
    ComboFill(PC(ID_EC_C4), { L"Auto", L"XInput", L"SDL" },
              IndexOfStr(kXeniaHidVals, 3, s.hid));
    Chk(PC(ID_EC_K1), s.vsync);
    Chk(PC(ID_EC_K2), s.fullscreen);
    Chk(PC(ID_EC_K3), s.letterbox);
    Chk(PC(ID_EC_K4), s.vibration);
}
void SettingsWindow::SaveXbox360Page() {
    auto& e = m_work.emulators;
    e.xeniaPath = GetTxt(PC(ID_P_EDIT1));
    if (m_cfg) m_cfg->emulators.xeniaPath = e.xeniaPath;
    if (e.xeniaPath.empty()) return;

    XeniaSettings s;
    int gi = ComboSel(PC(ID_EC_C1)); s.gpu = kXeniaGpuVals[(gi >= 0 && gi < 3) ? gi : 0];
    int ri = ComboSel(PC(ID_EC_C2)); s.resScale = (ri >= 0 && ri < 3) ? ri + 1 : 1;
    int fi = ComboSel(PC(ID_EC_C3)); s.frameLimit = kXeniaFpsVals[(fi >= 0 && fi < 4) ? fi : 0];
    int hi = ComboSel(PC(ID_EC_C4)); s.hid = kXeniaHidVals[(hi >= 0 && hi < 3) ? hi : 0];
    s.vsync      = IsChk(PC(ID_EC_K1));
    s.fullscreen = IsChk(PC(ID_EC_K2));
    s.letterbox  = IsChk(PC(ID_EC_K3));
    s.vibration  = IsChk(PC(ID_EC_K4));
    XeniaApplySettings(e.xeniaPath, s);
}

void SettingsWindow::LoadXboxPage() {
    auto& e = m_work.emulators;
    SetWindowTextW(PC(ID_P_EDIT1), e.xemuPath.c_str());
    SetVersionLabel(e.xemuTag, {});
    CheckEmulatorUpdateAsync(m_hwnd, PAGE_XBOX, "xemu-project/xemu");
}
void SettingsWindow::SaveXboxPage() {
    auto& e = m_work.emulators;
    e.xemuPath    = GetTxt(PC(ID_P_EDIT1));
    if (m_cfg) {
        m_cfg->emulators.xemuPath    = e.xemuPath;
    }
}

void SettingsWindow::LoadCustomPage(int idx) {
    auto& libs = m_work.libraries.customLibraries;
    if (idx < 0 || idx >= (int)libs.size()) return;
    SetWindowTextW(PC(ID_P_EDIT1), libs[idx].name.c_str());
    VecToList(PC(ID_P_LIST1), libs[idx].dirs);
}
void SettingsWindow::SaveCustomPage(int idx) {
    auto& libs = m_work.libraries.customLibraries;
    if (idx < 0 || idx >= (int)libs.size()) return;
    libs[idx].name    = GetTxt(PC(ID_P_EDIT1));
    ListToVec(PC(ID_P_LIST1), libs[idx].dirs);
    // Sync sidebar label
    std::wstring lbl = libs[idx].name;
    SendMessageW(m_sidebar, LB_DELETESTRING, m_currentPage, 0);
    SendMessageW(m_sidebar, LB_INSERTSTRING, m_currentPage, (LPARAM)lbl.c_str());
    SendMessageW(m_sidebar, LB_SETCURSEL, m_currentPage, 0);
}

// ─── Command dispatch ─────────────────────────────────────────────────────────

void SettingsWindow::HandlePageCommand(int id) {
    // General page: metadata action buttons
    if (m_currentPage == PAGE_GENERAL) {
        if (id == ID_P_BTN3) {
            auto& e = m_work.emulators;
            e.dolphinRomDirs.clear();
            e.ryujinxRomDirs.clear();
            e.rpcs3RomDirs.clear();
            e.n64RomDirs.clear();
            e.nesRomDirs.clear();
            e.snesRomDirs.clear();
            e.duckstationRomDirs.clear();
            e.pcsx2RomDirs.clear();
            e.xeniaRomDirs.clear();
            e.xemuRomDirs.clear();
            m_work.libraries.customLibraries.clear();
            MessageBoxW(m_hwnd,
                L"Local ROM library paths were cleared. Click Apply or OK to save and rescan.",
                L"Local Libraries Cleared", MB_OK | MB_ICONINFORMATION);
            return;
        }
        if (id == ID_P_BTN4 && m_onRefreshMeta)   { m_onRefreshMeta();   return; }
        if (id == ID_P_BTN5 && m_onReacquireMeta) { m_onReacquireMeta(); return; }
        if (id == ID_P_BTN6) {
            if (!m_igdbClient || !m_igdbClient->HasCredentials()) {
                MessageBoxW(m_hwnd,
                    L"Enter your IGDB (Twitch) credentials above and save first.",
                    L"No credentials", MB_OK | MB_ICONINFORMATION);
                return;
            }
            EnableWindow(PC(ID_P_BTN6), FALSE);
            SetWindowTextW(PC(ID_P_BTN6), L"Syncing…");
            SetWindowTextW(PC(ID_P_STAT2), L"Downloading game list from IGDB…");

            std::wstring dest = GetAppDataPath() + L"\\romdb.sqlite";
            IgdbSync::StartAsync(m_hwnd, *m_igdbClient, dest);
            return;
        }
    }

    switch (m_currentPage) {

    case PAGE_STEAM:
        if (id == ID_P_BTN1) {
            std::wstring p = BrowseFolder();
            if (!p.empty()) SetWindowTextW(PC(ID_P_EDIT1), p.c_str());
        }
        else if (id == ID_P_BTN2) ListAddPath(PC(ID_P_LIST1));
        else if (id == ID_P_BTN3) ListRemoveSel(PC(ID_P_LIST1));
        break;

    case PAGE_EPIC:
        if      (id == ID_P_BTN1) ListAddPath(PC(ID_P_LIST1));
        else if (id == ID_P_BTN2) ListRemoveSel(PC(ID_P_LIST1));
        break;

    case PAGE_DOLPHIN:
        if (id == ID_P_BTN1) {
            std::wstring p = BrowseExe(GetTxt(PC(ID_P_EDIT1)));
            if (!p.empty()) SetWindowTextW(PC(ID_P_EDIT1), p.c_str());
        }
        else if (id == ID_P_BTN2) {
            std::wstring p = PlatformIcons::FindDolphinExe(L"");
            if (!p.empty()) SetWindowTextW(PC(ID_P_EDIT1), p.c_str());
            else MessageBoxW(m_hwnd, L"Dolphin not found in common locations.",
                             L"Auto-detect", MB_OK | MB_ICONINFORMATION);
        }
        else if (id == ID_P_BTN5) {
            EnableWindow(PC(ID_P_BTN5), FALSE);
            SetWindowTextW(PC(ID_P_BTN5), L"Downloading...");
            DownloadEmulatorAsync(m_hwnd, PAGE_DOLPHIN,
                { "", L"server", L"Dolphin.exe", L"dolphin",
                  EmulatorArchiveUrl(m_work, L"dolphin-x64.7z") },
                GetAppDataPath());
        }
        else if (id == ID_D_CTRL_PRESET) {
            std::wstring exe = GetTxt(PC(ID_P_EDIT1));
            if (exe.empty()) {
                MessageBoxW(m_hwnd, L"Set the Dolphin executable path first.",
                            L"Controllers", MB_OK | MB_ICONINFORMATION);
                break;
            }
            std::wstring err;
            bool wii = IsChk(PC(ID_D_CTRL_WIIMOTE));
            if (DolphinWriteGamepadPreset(exe, wii, err)) {
                MessageBoxW(m_hwnd,
                    wii ? L"Gamepad preset applied to GameCube port 1 and Wii Remote 1.\n"
                          L"Connect an Xbox-style controller and start a game."
                        : L"Gamepad preset applied to GameCube controller port 1.\n"
                          L"Connect an Xbox-style controller and start a game.",
                    L"Controllers", MB_OK | MB_ICONINFORMATION);
            } else {
                MessageBoxW(m_hwnd, err.empty() ? L"Could not write controller config." : err.c_str(),
                            L"Controllers", MB_OK | MB_ICONWARNING);
            }
        }
        else if (id == ID_D_CTRL_OPEN) {
            std::wstring exe = GetTxt(PC(ID_P_EDIT1));
            if (exe.empty()) {
                MessageBoxW(m_hwnd, L"Set the Dolphin executable path first.",
                            L"Controllers", MB_OK | MB_ICONINFORMATION);
                break;
            }
            ShellExecuteW(m_hwnd, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            MessageBoxW(m_hwnd,
                L"Dolphin is opening. Use its Controllers menu to fine-tune mappings, "
                L"then close Dolphin.",
                L"Controllers", MB_OK | MB_ICONINFORMATION);
        }
        break;

    case PAGE_RYUJINX:
        if (id == ID_P_BTN1) {
            std::wstring p = BrowseExe(GetTxt(PC(ID_P_EDIT1)));
            if (!p.empty()) SetWindowTextW(PC(ID_P_EDIT1), p.c_str());
        }
        else if (id == ID_P_BTN2) {
            std::wstring p = PlatformIcons::FindRyujinxExe(L"");
            if (!p.empty()) SetWindowTextW(PC(ID_P_EDIT1), p.c_str());
            else MessageBoxW(m_hwnd, L"Ryujinx not found in common locations.",
                             L"Auto-detect", MB_OK | MB_ICONINFORMATION);
        }
        else if (id == ID_P_BTN5) {
            EnableWindow(PC(ID_P_BTN5), FALSE);
            SetWindowTextW(PC(ID_P_BTN5), L"Downloading...");
            DownloadEmulatorAsync(m_hwnd, PAGE_RYUJINX,
                { "", L"server", L"Ryujinx.exe", L"ryujinx",
                  EmulatorArchiveUrl(m_work, L"ryujinx-win-x64.zip") },
                GetAppDataPath());
        }
        break;

    case PAGE_RPCS3:
        if (id == ID_P_BTN1) {
            std::wstring p = BrowseExe(GetTxt(PC(ID_P_EDIT1)));
            if (!p.empty()) SetWindowTextW(PC(ID_P_EDIT1), p.c_str());
        }
        else if (id == ID_P_BTN5) {
            EnableWindow(PC(ID_P_BTN5), FALSE);
            SetWindowTextW(PC(ID_P_BTN5), L"Downloading…");
            DownloadEmulatorAsync(m_hwnd, PAGE_RPCS3,
                { "", L"server", L"rpcs3.exe", L"rpcs3",
                  EmulatorArchiveUrl(m_work, L"rpcs3-win64.7z") },
                GetAppDataPath());
        }
        else if (id == ID_EC_B1) {
            std::wstring exe = GetTxt(PC(ID_P_EDIT1));
            if (exe.empty())
                MessageBoxW(m_hwnd, L"Set the RPCS3 executable path first.",
                            L"RPCS3", MB_OK | MB_ICONINFORMATION);
            else
                ShellExecuteW(m_hwnd, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;

    case PAGE_N64:
        if (id == ID_P_BTN1) {
            std::wstring p = BrowseExe(GetTxt(PC(ID_P_EDIT1)));
            if (!p.empty()) SetWindowTextW(PC(ID_P_EDIT1), p.c_str());
        }
        else if (id == ID_P_BTN5) {
            EnableWindow(PC(ID_P_BTN5), FALSE);
            SetWindowTextW(PC(ID_P_BTN5), L"Downloading…");
            DownloadEmulatorAsync(m_hwnd, PAGE_N64,
                { "", L"server", L"gopher64.exe", L"gopher64",
                  EmulatorArchiveUrl(m_work, L"gopher64-windows-x86_64.exe") },
                GetAppDataPath());
        }
        else if (id == ID_EC_B1) {
            std::wstring exe = GetTxt(PC(ID_P_EDIT1));
            if (exe.empty())
                MessageBoxW(m_hwnd, L"Set the gopher64 executable path first.",
                            L"gopher64", MB_OK | MB_ICONINFORMATION);
            else
                ShellExecuteW(m_hwnd, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;

    case PAGE_NES:
        if (id == ID_P_BTN1) {
            std::wstring p = BrowseExe(GetTxt(PC(ID_P_EDIT1)));
            if (!p.empty()) SetWindowTextW(PC(ID_P_EDIT1), p.c_str());
        }
        else if (id == ID_P_BTN5) {
            EnableWindow(PC(ID_P_BTN5), FALSE);
            SetWindowTextW(PC(ID_P_BTN5), L"Downloading…");
            DownloadEmulatorAsync(m_hwnd, PAGE_NES,
                { "", L"server", L"Mesen.exe", L"mesen2",
                  EmulatorArchiveUrl(m_work, L"mesen-windows.zip") },
                GetAppDataPath());
        }
        break;

    case PAGE_SNES:
        if (id == ID_P_BTN1) {
            std::wstring p = BrowseExe(GetTxt(PC(ID_P_EDIT1)));
            if (!p.empty()) SetWindowTextW(PC(ID_P_EDIT1), p.c_str());
        }
        else if (id == ID_P_BTN5) {
            EnableWindow(PC(ID_P_BTN5), FALSE);
            SetWindowTextW(PC(ID_P_BTN5), L"Downloading…");
            DownloadEmulatorAsync(m_hwnd, PAGE_SNES,
                { "", L"server", L"Mesen.exe", L"mesen2",
                  EmulatorArchiveUrl(m_work, L"mesen-windows.zip") },
                GetAppDataPath());
        }
        break;

    case PAGE_PS1:
        if (id == ID_P_BTN1) {
            std::wstring p = BrowseExe(GetTxt(PC(ID_P_EDIT1)));
            if (!p.empty()) SetWindowTextW(PC(ID_P_EDIT1), p.c_str());
        }
        else if (id == ID_P_BTN5) {
            EnableWindow(PC(ID_P_BTN5), FALSE);
            SetWindowTextW(PC(ID_P_BTN5), L"Downloading…");
            DownloadEmulatorAsync(m_hwnd, PAGE_PS1,
                { "", L"server",
                  L"duckstation-qt-x64-ReleaseLTCG.exe", L"duckstation",
                  EmulatorArchiveUrl(m_work, L"duckstation-windows-x64.zip") },
                GetAppDataPath());
        }
        break;

    case PAGE_PS2:
        if (id == ID_P_BTN1) {
            std::wstring p = BrowseExe(GetTxt(PC(ID_P_EDIT1)));
            if (!p.empty()) SetWindowTextW(PC(ID_P_EDIT1), p.c_str());
        }
        else if (id == ID_P_BTN5) {
            EnableWindow(PC(ID_P_BTN5), FALSE);
            SetWindowTextW(PC(ID_P_BTN5), L"Downloading…");
            DownloadEmulatorAsync(m_hwnd, PAGE_PS2,
                { "", L"server", L"pcsx2-qt.exe", L"pcsx2",
                  EmulatorArchiveUrl(m_work, L"pcsx2-windows-x64.7z") },
                GetAppDataPath());
        }
        else if (id == ID_EC_B1) {
            std::wstring exe = GetTxt(PC(ID_P_EDIT1));
            if (exe.empty())
                MessageBoxW(m_hwnd, L"Set the PCSX2 executable path first.",
                            L"PCSX2", MB_OK | MB_ICONINFORMATION);
            else
                ShellExecuteW(m_hwnd, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;

    case PAGE_XBOX360:
        if (id == ID_P_BTN1) {
            std::wstring p = BrowseExe(GetTxt(PC(ID_P_EDIT1)));
            if (!p.empty()) SetWindowTextW(PC(ID_P_EDIT1), p.c_str());
        }
        else if (id == ID_P_BTN5) {
            EnableWindow(PC(ID_P_BTN5), FALSE);
            SetWindowTextW(PC(ID_P_BTN5), L"Downloading…");
            DownloadEmulatorAsync(m_hwnd, PAGE_XBOX360,
                { "", L"server", L"xenia_canary.exe", L"xenia-canary",
                  EmulatorArchiveUrl(m_work, L"xenia-canary-windows.zip") },
                GetAppDataPath());
        }
        else if (id == ID_EC_B1) {
            std::wstring exe = GetTxt(PC(ID_P_EDIT1));
            if (exe.empty())
                MessageBoxW(m_hwnd, L"Set the Xenia executable path first.",
                            L"Xenia", MB_OK | MB_ICONINFORMATION);
            else
                ShellExecuteW(m_hwnd, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;

    case PAGE_XBOX:
        if (id == ID_P_BTN1) {
            std::wstring p = BrowseExe(GetTxt(PC(ID_P_EDIT1)));
            if (!p.empty()) SetWindowTextW(PC(ID_P_EDIT1), p.c_str());
        }
        else if (id == ID_P_BTN5) {
            EnableWindow(PC(ID_P_BTN5), FALSE);
            SetWindowTextW(PC(ID_P_BTN5), L"Downloading…");
            DownloadEmulatorAsync(m_hwnd, PAGE_XBOX,
                { "", L"server", L"xemu.exe", L"xemu",
                  EmulatorArchiveUrl(m_work, L"xemu-win-x86_64-release.zip") },
                GetAppDataPath());
        }
        break;

    default:
        if (m_currentPage >= PAGE_CUSTOM0) {
            int li = m_currentPage - PAGE_CUSTOM0;
            if      (id == ID_P_BTN1) ListAddPath(PC(ID_P_LIST1));
            else if (id == ID_P_BTN2) ListRemoveSel(PC(ID_P_LIST1));
            else if (id == ID_P_BTN3) {
                // Delete library
                SaveCustomPage(li);
                auto& libs = m_work.libraries.customLibraries;
                if (li < (int)libs.size()) libs.erase(libs.begin() + li);
                RebuildSidebarItems();
                m_currentPage = -1;
                SwitchPage(std::max(0, PAGE_CUSTOM0 + li - 1));
            }
        }
        break;
    }
}

// ─── List helpers ─────────────────────────────────────────────────────────────

void SettingsWindow::ListAddPath(HWND lb) {
    std::wstring p = BrowseFolder();
    if (!p.empty())
        SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)p.c_str());
}

void SettingsWindow::ListRemoveSel(HWND lb) {
    int sel = (int)SendMessageW(lb, LB_GETCURSEL, 0, 0);
    if (sel != LB_ERR) SendMessageW(lb, LB_DELETESTRING, sel, 0);
}

void SettingsWindow::ListToVec(HWND lb, std::vector<std::wstring>& out) const {
    out.clear();
    int n = (int)SendMessageW(lb, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; ++i) {
        int len = (int)SendMessageW(lb, LB_GETTEXTLEN, i, 0);
        if (len <= 0) continue;
        std::wstring s(len + 1, L'\0');
        SendMessageW(lb, LB_GETTEXT, i, (LPARAM)s.data());
        s.resize(len);
        out.push_back(s);
    }
}

void SettingsWindow::VecToList(HWND lb, const std::vector<std::wstring>& v) const {
    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    for (auto& s : v)
        SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)s.c_str());
}

// ─── File / folder browsers ───────────────────────────────────────────────────

std::wstring SettingsWindow::BrowseExe(const std::wstring& initial) {
    wchar_t buf[MAX_PATH]{};
    if (!initial.empty()) wcsncpy_s(buf, initial.c_str(), MAX_PATH - 1);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hwnd;
    ofn.lpstrFilter = L"Executables (*.exe)\0*.exe\0All Files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = L"Select Executable";
    return GetOpenFileNameW(&ofn) ? buf : L"";
}

std::wstring SettingsWindow::BrowseFolder() {
    std::wstring result;
    IFileOpenDialog* pfd = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&pfd)))) return {};
    DWORD opts = 0;
    pfd->GetOptions(&opts);
    pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pfd->SetTitle(L"Select folder");
    if (SUCCEEDED(pfd->Show(m_hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(pfd->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                result = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    pfd->Release();
    return result;
}

// ─── Version check helpers ────────────────────────────────────────────────────

void SettingsWindow::SetVersionLabel(const std::wstring& installed,
                                     const std::wstring& latest) {
    HWND stat = PC(ID_P_STAT1);
    if (!stat) return;

    std::wstring txt;
    if (latest.empty()) {
        // Check still in flight — show installed tag if known, else "Checking…"
        txt = installed.empty()
            ? L"Checking for updates\x2026"
            : L"Installed: " + installed + L"  \x2013  Checking\x2026";
    } else if (installed.empty()) {
        txt = L"Latest available: " + latest;
    } else if (installed == latest) {
        txt = L"Up to date  (" + installed + L")";
    } else {
        txt = L"Update available: " + installed + L"  \x2192  " + latest;
    }
    SetWindowTextW(stat, txt.c_str());
}

void SettingsWindow::SaveTagForPage(int page, const std::wstring& tag) {
    auto& ew = m_work.emulators;
    auto& ec = m_cfg->emulators;
    switch (page) {
    case PAGE_DOLPHIN:  ew.dolphinTag = ec.dolphinTag = tag; break;
    case PAGE_RYUJINX:  ew.ryujinxTag = ec.ryujinxTag = tag; break;
    case PAGE_RPCS3:    ew.rpcs3Tag   = ec.rpcs3Tag   = tag; break;
    case PAGE_N64:      ew.n64Tag     = ec.n64Tag     = tag; break;
    case PAGE_NES:      ew.nesTag          = ec.nesTag          = tag; break;
    case PAGE_SNES:     ew.snesTag         = ec.snesTag         = tag; break;
    case PAGE_PS1:      ew.duckstationTag  = ec.duckstationTag  = tag; break;
    case PAGE_PS2:      ew.pcsx2Tag        = ec.pcsx2Tag        = tag; break;
    case PAGE_XBOX360:  ew.xeniaTag        = ec.xeniaTag        = tag; break;
    case PAGE_XBOX:     ew.xemuTag         = ec.xemuTag         = tag; break;
    }
}

// Sets the emulator exe path in both m_work and m_cfg for the given page.
// For PAGE_NES and PAGE_SNES the sibling is also updated because both use Mesen2.
void SettingsWindow::SetPathForPage(int page, const std::wstring& exePath) {
    auto& ew = m_work.emulators;
    auto& ec = m_cfg->emulators;
    switch (page) {
    case PAGE_DOLPHIN:
        ew.dolphinPath = ec.dolphinPath = exePath; break;
    case PAGE_RYUJINX:
        ew.ryujinxPath = ec.ryujinxPath = exePath; break;
    case PAGE_RPCS3:
        ew.rpcs3Path   = ec.rpcs3Path   = exePath; break;
    case PAGE_N64:
        ew.n64Path     = ec.n64Path     = exePath; break;
    case PAGE_NES:
        ew.nesPath = ec.nesPath = exePath;
        ew.snesPath = ec.snesPath = exePath;  // same Mesen2 exe
        break;
    case PAGE_SNES:
        ew.snesPath = ec.snesPath = exePath;
        ew.nesPath  = ec.nesPath  = exePath;  // same Mesen2 exe
        break;
    case PAGE_PS1:
        ew.duckstationPath = ec.duckstationPath = exePath; break;
    case PAGE_PS2:
        ew.pcsx2Path       = ec.pcsx2Path       = exePath; break;
    case PAGE_XBOX360:
        ew.xeniaPath       = ec.xeniaPath       = exePath; break;
    case PAGE_XBOX:
        ew.xemuPath        = ec.xemuPath        = exePath; break;
    }
}

std::wstring SettingsWindow::InstalledTagForPage(int page) const {
    auto& e = m_work.emulators;
    switch (page) {
    case PAGE_DOLPHIN:  return e.dolphinTag;
    case PAGE_RYUJINX:  return e.ryujinxTag;
    case PAGE_RPCS3:    return e.rpcs3Tag;
    case PAGE_N64:      return e.n64Tag;
    case PAGE_NES:      return e.nesTag;
    case PAGE_SNES:     return e.snesTag;
    case PAGE_PS1:      return e.duckstationTag;
    case PAGE_PS2:      return e.pcsx2Tag;
    case PAGE_XBOX360:  return e.xeniaTag;
    case PAGE_XBOX:     return e.xemuTag;
    default:            return {};
    }
}

std::string SettingsWindow::GithubRepoForPage(int page) const {
    switch (page) {
    case PAGE_DOLPHIN:  return "dolphin-emu/dolphin";
    case PAGE_RYUJINX:  return "Ryujinx/release-channel-master";
    case PAGE_RPCS3:    return "RPCS3/rpcs3-binaries-win";
    case PAGE_N64:      return "gopher64/gopher64";
    case PAGE_NES:
    case PAGE_SNES:     return "SourMesen/Mesen2";
    case PAGE_PS1:      return "stenzek/duckstation";
    case PAGE_PS2:      return "PCSX2/pcsx2";
    case PAGE_XBOX360:  return "xenia-canary/xenia-canary";
    case PAGE_XBOX:     return "xemu-project/xemu";
    default:            return {};
    }
}
