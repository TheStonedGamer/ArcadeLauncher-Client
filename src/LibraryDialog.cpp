#include "pch.h"
#include "LibraryDialog.h"
#include "DarkTheme.h"

#include <windows.h>
#include <shobjidl.h>
#include <string>
#include <vector>

// ── shared helpers (mirrors AccountDialog.cpp's modal idiom) ────────────────

namespace {

std::wstring TextOf(HWND h) {
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return {};
    std::wstring s(len + 1, L'\0');
    GetWindowTextW(h, s.data(), len + 1);
    s.resize(len);
    return s;
}

HWND MakeLabel(HWND p, const wchar_t* text, int x, int y, int w, int h = 18) {
    return CreateWindowExW(0, WC_STATICW, text, WS_CHILD | WS_VISIBLE | SS_LEFT,
        x, y, w, h, p, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND MakeEdit(HWND p, int id, int x, int y, int w, DWORD extra = 0) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extra,
        x, y, w, 23, p, (HMENU)(intptr_t)id, GetModuleHandleW(nullptr), nullptr);
}

HWND MakeButton(HWND p, int id, const wchar_t* text, int x, int y, int w, int h = 28,
                DWORD extra = 0) {
    return CreateWindowExW(0, WC_BUTTONW, text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | extra,
        x, y, w, h, p, (HMENU)(intptr_t)id, GetModuleHandleW(nullptr), nullptr);
}

HWND MakeList(HWND p, int id, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
        x, y, w, h, p, (HMENU)(intptr_t)id, GetModuleHandleW(nullptr), nullptr);
}

void ApplyFont(HWND parent, HFONT font, HFONT titleFont) {
    for (HWND c = GetWindow(parent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    if (HWND first = GetWindow(parent, GW_CHILD))
        SendMessageW(first, WM_SETFONT, (WPARAM)titleFont, TRUE);
}

void RegisterModalClass(const wchar_t* name, WNDPROC proc) {
    static std::vector<std::wstring> registered;
    for (auto& r : registered) if (r == name) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = dark::BgBrush();
    wc.lpszClassName = name;
    RegisterClassExW(&wc);
    registered.push_back(name);
}

HWND CreateCenteredModal(const wchar_t* cls, const wchar_t* title, HWND owner,
                         int w, int h, void* param) {
    // w/h are the desired CLIENT size. Inflate by the non-client frame so the
    // caption/borders don't eat into the right and bottom padding (controls are
    // positioned in client coordinates).
    RECT wr = { 0, 0, w, h };
    AdjustWindowRectEx(&wr, WS_CAPTION | WS_POPUP | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;
    RECT rc{};
    if (owner) GetWindowRect(owner, &rc);
    else rc = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    return CreateWindowExW(WS_EX_DLGMODALFRAME, cls, title,
        WS_CAPTION | WS_POPUP | WS_SYSMENU,
        rc.left + ((rc.right - rc.left) - winW) / 2,
        rc.top + ((rc.bottom - rc.top) - winH) / 2,
        winW, winH, owner, nullptr, GetModuleHandleW(nullptr), param);
}

void RunModalLoop(HWND hwnd, HWND owner, bool& done) {
    dark::EnableTitleBar(hwnd);
    dark::Apply(hwnd);   // child controls already exist (created in WM_CREATE)
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    MSG msg{};
    while (!done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
}

HFONT g_uiFont = nullptr;
HFONT g_titleFont = nullptr;

void EnsureFonts() {
    if (!g_uiFont)
        g_uiFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!g_titleFont)
        g_titleFont = CreateFontW(-21, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

// ── disk / formatting helpers ───────────────────────────────────────────────

std::wstring FormatBytes(uint64_t b) {
    const wchar_t* u[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    double v = (double)b;
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
    wchar_t buf[64];
    swprintf_s(buf, L"%.1f %s", v, u[i]);
    return buf;
}

bool DiskSpace(const std::wstring& path, uint64_t& freeB, uint64_t& totalB) {
    ULARGE_INTEGER freeAvail{}, total{}, totalFree{};
    if (GetDiskFreeSpaceExW(path.c_str(), &freeAvail, &total, &totalFree)) {
        freeB = freeAvail.QuadPart;
        totalB = total.QuadPart;
        return true;
    }
    return false;
}

std::wstring FolderListLine(const std::wstring& folder, bool isDefault) {
    std::wstring line = (isDefault ? L"\x2605 " : L"   ") + folder;
    uint64_t freeB = 0, totalB = 0;
    if (DiskSpace(folder, freeB, totalB))
        line += L"    \x2014  " + FormatBytes(freeB) + L" free of " + FormatBytes(totalB);
    else
        line += L"    \x2014  (unavailable)";
    return line;
}

// Modern folder picker (matches SettingsWindow::BrowseFolder).
std::wstring BrowseForFolder(HWND owner) {
    std::wstring result;
    IFileOpenDialog* pfd = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&pfd))))
        return {};
    DWORD opts = 0;
    pfd->GetOptions(&opts);
    pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pfd->SetTitle(L"Select library folder");
    if (SUCCEEDED(pfd->Show(owner))) {
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

// ── Library Folders manager dialog (#29) ────────────────────────────────────

enum {
    IDC_LF_LIST = 100,
    IDC_LF_ADD,
    IDC_LF_REMOVE,
    IDC_LF_DEFAULT,
    IDC_LF_ASK,
    IDC_LF_CLOSE,
};

struct LibFoldersState {
    ServerConfig* cfg = nullptr;
    HWND hwnd = nullptr;
    bool done = false;
    bool changed = false;
    HWND list = nullptr;
    HWND askCheck = nullptr;
};

void RepopulateFolders(LibFoldersState* st) {
    SendMessageW(st->list, LB_RESETCONTENT, 0, 0);
    auto& cfg = *st->cfg;
    for (size_t i = 0; i < cfg.libraryFolders.size(); ++i) {
        std::wstring line = FolderListLine(cfg.libraryFolders[i],
                                           (int)i == cfg.defaultLibraryIndex);
        SendMessageW(st->list, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
    if (!cfg.libraryFolders.empty())
        SendMessageW(st->list, LB_SETCURSEL,
                     (WPARAM)std::min(cfg.defaultLibraryIndex,
                                      (int)cfg.libraryFolders.size() - 1), 0);
}

LRESULT CALLBACK LibFoldersProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<LibFoldersState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<LibFoldersState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        st->hwnd = hwnd;

        MakeLabel(hwnd, L"Library Folders", 22, 16, 400, 24);
        MakeLabel(hwnd, L"Games downloaded from the server install into one of these "
                        L"folders. The \x2605 folder is the default.", 22, 48, 520, 34);

        st->list = MakeList(hwnd, IDC_LF_LIST, 22, 90, 520, 180);

        MakeButton(hwnd, IDC_LF_ADD,     L"Add Folder\x2026", 22, 282, 120);
        MakeButton(hwnd, IDC_LF_REMOVE,  L"Remove",           150, 282, 100);
        MakeButton(hwnd, IDC_LF_DEFAULT, L"Make Default",     258, 282, 130);

        st->askCheck = CreateWindowExW(0, WC_BUTTONW,
            L"Always ask where to install each game",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            22, 324, 360, 22, hwnd, (HMENU)(intptr_t)IDC_LF_ASK,
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(st->askCheck, BM_SETCHECK,
                     st->cfg->alwaysAskInstallLocation ? BST_CHECKED : BST_UNCHECKED, 0);

        MakeButton(hwnd, IDC_LF_CLOSE, L"Close", 450, 356, 92, 28, BS_DEFPUSHBUTTON);

        EnsureFonts();
        ApplyFont(hwnd, g_uiFont, g_titleFont);
        RepopulateFolders(st);
        return 0;
    }
    if (!st) return DefWindowProcW(hwnd, msg, wp, lp);
    if (LRESULT r; dark::OnCtlColor(msg, wp, lp, r)) return r;

    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        auto& cfg = *st->cfg;
        if (id == IDC_LF_ADD) {
            std::wstring f = BrowseForFolder(hwnd);
            if (!f.empty()) {
                bool dup = false;
                for (auto& e : cfg.libraryFolders)
                    if (_wcsicmp(e.c_str(), f.c_str()) == 0) { dup = true; break; }
                if (!dup) {
                    cfg.libraryFolders.push_back(f);
                    if (cfg.libraryFolders.size() == 1) cfg.defaultLibraryIndex = 0;
                    st->changed = true;
                    RepopulateFolders(st);
                }
            }
            return 0;
        }
        if (id == IDC_LF_REMOVE) {
            int sel = (int)SendMessageW(st->list, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)cfg.libraryFolders.size()) {
                cfg.libraryFolders.erase(cfg.libraryFolders.begin() + sel);
                if (cfg.defaultLibraryIndex >= (int)cfg.libraryFolders.size())
                    cfg.defaultLibraryIndex = std::max(0, (int)cfg.libraryFolders.size() - 1);
                else if (sel < cfg.defaultLibraryIndex)
                    --cfg.defaultLibraryIndex;
                st->changed = true;
                RepopulateFolders(st);
            }
            return 0;
        }
        if (id == IDC_LF_DEFAULT) {
            int sel = (int)SendMessageW(st->list, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)cfg.libraryFolders.size()) {
                cfg.defaultLibraryIndex = sel;
                st->changed = true;
                RepopulateFolders(st);
            }
            return 0;
        }
        if (id == IDC_LF_ASK) {
            cfg.alwaysAskInstallLocation =
                SendMessageW(st->askCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            st->changed = true;
            return 0;
        }
        if (id == IDC_LF_CLOSE || id == IDCANCEL) {
            st->done = true; DestroyWindow(hwnd); return 0;
        }
    } else if (msg == WM_CLOSE) {
        st->done = true; DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Install-location picker (#30 / #31) ─────────────────────────────────────

enum {
    IDC_IL_LIST = 200,
    IDC_IL_ADD,
    IDC_IL_DONTASK,
    IDC_IL_DESKTOP,
    IDC_IL_STARTMENU,
    IDC_IL_OK,
    IDC_IL_CANCEL,
};

struct InstallLocState {
    ServerConfig* cfg = nullptr;
    bool moving = false;
    std::wstring* outRoot = nullptr;
    bool* outDesktop = nullptr;
    bool* outStartMenu = nullptr;
    HWND hwnd = nullptr;
    bool done = false;
    bool ok = false;
    HWND list = nullptr;
    HWND dontAsk = nullptr;
    HWND desktopChk = nullptr;
    HWND startMenuChk = nullptr;
};

void RepopulateInstallList(InstallLocState* st) {
    SendMessageW(st->list, LB_RESETCONTENT, 0, 0);
    auto& cfg = *st->cfg;
    for (size_t i = 0; i < cfg.libraryFolders.size(); ++i)
        SendMessageW(st->list, LB_ADDSTRING, 0,
                     (LPARAM)FolderListLine(cfg.libraryFolders[i], false).c_str());
    int sel = cfg.defaultLibraryIndex;
    if (sel < 0 || sel >= (int)cfg.libraryFolders.size()) sel = 0;
    if (!cfg.libraryFolders.empty())
        SendMessageW(st->list, LB_SETCURSEL, (WPARAM)sel, 0);
}

LRESULT CALLBACK InstallLocProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<InstallLocState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<InstallLocState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        st->hwnd = hwnd;

        MakeLabel(hwnd, st->moving ? L"Move to library folder"
                                   : L"Choose where to install", 22, 16, 460, 24);
        st->list = MakeList(hwnd, IDC_IL_LIST, 22, 56, 520, 150);
        MakeButton(hwnd, IDC_IL_ADD, L"Add Folder\x2026", 22, 216, 120);

        if (!st->moving) {
            st->desktopChk = CreateWindowExW(0, WC_BUTTONW,
                L"Create a desktop shortcut",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                22, 250, 300, 22, hwnd, (HMENU)(intptr_t)IDC_IL_DESKTOP,
                GetModuleHandleW(nullptr), nullptr);
            st->startMenuChk = CreateWindowExW(0, WC_BUTTONW,
                L"Add to Start Menu (Arcade Launcher \x2192 Games)",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                22, 274, 360, 22, hwnd, (HMENU)(intptr_t)IDC_IL_STARTMENU,
                GetModuleHandleW(nullptr), nullptr);
            st->dontAsk = CreateWindowExW(0, WC_BUTTONW,
                L"Always install to the selected folder (don't ask again)",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                22, 298, 460, 22, hwnd, (HMENU)(intptr_t)IDC_IL_DONTASK,
                GetModuleHandleW(nullptr), nullptr);
        }

        int by = st->moving ? 256 : 332;
        MakeButton(hwnd, IDC_IL_OK, st->moving ? L"Move" : L"Install",
                   336, by, 100, 28, BS_DEFPUSHBUTTON);
        MakeButton(hwnd, IDC_IL_CANCEL, L"Cancel", 444, by, 98, 28);

        EnsureFonts();
        ApplyFont(hwnd, g_uiFont, g_titleFont);
        RepopulateInstallList(st);
        return 0;
    }
    if (!st) return DefWindowProcW(hwnd, msg, wp, lp);
    if (LRESULT r; dark::OnCtlColor(msg, wp, lp, r)) return r;

    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        auto& cfg = *st->cfg;
        if (id == IDC_IL_ADD) {
            std::wstring f = BrowseForFolder(hwnd);
            if (!f.empty()) {
                int existing = -1;
                for (size_t i = 0; i < cfg.libraryFolders.size(); ++i)
                    if (_wcsicmp(cfg.libraryFolders[i].c_str(), f.c_str()) == 0) {
                        existing = (int)i; break;
                    }
                if (existing < 0) {
                    cfg.libraryFolders.push_back(f);
                    existing = (int)cfg.libraryFolders.size() - 1;
                }
                RepopulateInstallList(st);
                SendMessageW(st->list, LB_SETCURSEL, (WPARAM)existing, 0);
            }
            return 0;
        }
        if (id == IDC_IL_OK || id == IDOK) {
            int sel = (int)SendMessageW(st->list, LB_GETCURSEL, 0, 0);
            if (sel < 0 || sel >= (int)cfg.libraryFolders.size()) {
                MessageBeep(MB_ICONWARNING);
                return 0;
            }
            *st->outRoot = cfg.libraryFolders[sel];
            if (st->dontAsk &&
                SendMessageW(st->dontAsk, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                cfg.alwaysAskInstallLocation = false;
                cfg.defaultLibraryIndex = sel;
            }
            if (st->outDesktop)
                *st->outDesktop = st->desktopChk &&
                    SendMessageW(st->desktopChk, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (st->outStartMenu)
                *st->outStartMenu = st->startMenuChk &&
                    SendMessageW(st->startMenuChk, BM_GETCHECK, 0, 0) == BST_CHECKED;
            st->ok = true; st->done = true; DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDC_IL_CANCEL || id == IDCANCEL) {
            st->done = true; DestroyWindow(hwnd); return 0;
        }
    } else if (msg == WM_CLOSE) {
        st->done = true; DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Generic text prompt (#32 / #33) ─────────────────────────────────────────

struct TextPromptState {
    HWND hwnd = nullptr;
    bool done = false;
    bool ok = false;
    std::wstring* value = nullptr;
    std::wstring label;
    HWND edit = nullptr;
};

LRESULT CALLBACK TextPromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<TextPromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<TextPromptState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        st->hwnd = hwnd;
        MakeLabel(hwnd, st->label.c_str(), 22, 18, 420, 36);
        st->edit = MakeEdit(hwnd, 601, 22, 60, 420);
        SetWindowTextW(st->edit, st->value->c_str());
        MakeButton(hwnd, IDOK, L"OK", 252, 100, 100, 28, BS_DEFPUSHBUTTON);
        MakeButton(hwnd, IDCANCEL, L"Cancel", 360, 100, 90, 28);
        EnsureFonts();
        ApplyFont(hwnd, g_uiFont, g_uiFont);
        SetFocus(st->edit);
        SendMessageW(st->edit, EM_SETSEL, 0, -1);
        return 0;
    }
    if (!st) return DefWindowProcW(hwnd, msg, wp, lp);
    if (LRESULT r; dark::OnCtlColor(msg, wp, lp, r)) return r;
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDOK) {
            *st->value = TextOf(st->edit);
            st->ok = true; st->done = true; DestroyWindow(hwnd); return 0;
        }
        if (id == IDCANCEL) { st->done = true; DestroyWindow(hwnd); return 0; }
    } else if (msg == WM_CLOSE) {
        st->done = true; DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

// ── public entry points ─────────────────────────────────────────────────────

bool ShowLibraryFoldersDialog(HWND owner, ServerConfig& cfg) {
    const wchar_t* cls = L"ArcadeLauncherLibFoldersWnd";
    RegisterModalClass(cls, LibFoldersProc);
    LibFoldersState st;
    st.cfg = &cfg;
    HWND hwnd = CreateCenteredModal(cls, L"Library Folders", owner, 564, 406, &st);
    if (!hwnd) return false;
    RunModalLoop(hwnd, owner, st.done);
    return st.changed;
}

bool ShowInstallLocationDialog(HWND owner, ServerConfig& cfg,
                               const std::wstring& gameTitle,
                               std::wstring& outRoot, bool moving,
                               bool* outDesktopShortcut,
                               bool* outStartMenuShortcut) {
    const wchar_t* cls = L"ArcadeLauncherInstallLocWnd";
    RegisterModalClass(cls, InstallLocProc);
    InstallLocState st;
    st.cfg = &cfg;
    st.moving = moving;
    st.outRoot = &outRoot;
    st.outDesktop = outDesktopShortcut;
    st.outStartMenu = outStartMenuShortcut;
    std::wstring title = (moving ? L"Move " : L"Install ") + gameTitle;
    HWND hwnd = CreateCenteredModal(cls, title.c_str(), owner,
                                    564, moving ? 306 : 392, &st);
    if (!hwnd) return false;
    RunModalLoop(hwnd, owner, st.done);
    return st.ok;
}

bool ShowTextPrompt(HWND owner, const std::wstring& title,
                    const std::wstring& label, std::wstring& value) {
    const wchar_t* cls = L"ArcadeLauncherTextPromptWnd";
    RegisterModalClass(cls, TextPromptProc);
    TextPromptState st;
    st.value = &value;
    st.label = label;
    HWND hwnd = CreateCenteredModal(cls, title.c_str(), owner, 472, 150, &st);
    if (!hwnd) return false;
    RunModalLoop(hwnd, owner, st.done);
    return st.ok;
}
