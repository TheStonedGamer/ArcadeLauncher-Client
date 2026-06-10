#include "pch.h"
#include "App.h"
#include "Platform/SteamScanner.h"
#include "Platform/EpicScanner.h"
#include "Platform/GogScanner.h"
#include "IgdbSync.h"
#include "AccountDialog.h"
#include "LibraryDialog.h"
#include "DarkTheme.h"
#include <commctrl.h>
#include <shlobj.h>      // SHGetKnownFolderPath, IShellLinkW
#include <shobjidl.h>

#pragma comment(lib, "comctl32.lib")

// Full path to this running launcher executable.
static std::wstring ThisExePath() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

// Write a .lnk at lnkPath that runs `target args`. Creates parent dirs.
// Returns true on success. COM is assumed initialized (it is, in wWinMain).
static bool WriteShellLink(const std::wstring& lnkPath, const std::wstring& target,
                           const std::wstring& args, const std::wstring& desc) {
    std::error_code ec;
    fs::create_directories(fs::path(lnkPath).parent_path(), ec);
    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&link))))
        return false;
    link->SetPath(target.c_str());
    if (!args.empty()) link->SetArguments(args.c_str());
    if (!desc.empty()) link->SetDescription(desc.c_str());
    link->SetIconLocation(target.c_str(), 0);
    ComPtr<IPersistFile> pf;
    if (FAILED(link.As(&pf))) return false;
    return SUCCEEDED(pf->Save(lnkPath.c_str(), TRUE));
}

// Sanitize a game title into a safe .lnk filename (strip path-invalid chars).
static std::wstring SafeShortcutName(const std::wstring& title) {
    std::wstring out;
    for (wchar_t c : title) {
        if (wcschr(L"\\/:*?\"<>|", c)) out += L' ';
        else out += c;
    }
    while (!out.empty() && (out.back() == L' ' || out.back() == L'.')) out.pop_back();
    if (out.empty()) out = L"Game";
    return out;
}

// Create desktop and/or Start Menu shortcuts that launch a game by id through
// the launcher (`ArcadeLauncher.exe --launch <id>`). Start Menu entries live
// under "Arcade Launcher\Games\<platform>".
static void CreateGameShortcuts(const std::wstring& gameId, const std::wstring& title,
                                const std::wstring& platform,
                                bool desktop, bool startMenu) {
    if (!desktop && !startMenu) return;
    std::wstring exe = ThisExePath();
    std::wstring args = L"--launch " + gameId;
    std::wstring name = SafeShortcutName(title);

    auto knownFolder = [](REFKNOWNFOLDERID id) -> std::wstring {
        PWSTR p = nullptr; std::wstring r;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) r = p;
        if (p) CoTaskMemFree(p);
        return r;
    };

    if (desktop) {
        std::wstring dir = knownFolder(FOLDERID_Desktop);
        if (!dir.empty())
            WriteShellLink(dir + L"\\" + name + L".lnk", exe, args, title);
    }
    if (startMenu) {
        std::wstring programs = knownFolder(FOLDERID_Programs);
        if (!programs.empty()) {
            std::wstring dir = programs + L"\\Arcade Launcher\\Games\\" +
                               SafeShortcutName(platform);
            WriteShellLink(dir + L"\\" + name + L".lnk", exe, args, title);
        }
    }
}

static const wchar_t* WNDCLASS_NAME = L"ArcadeLauncherWnd";
static const wchar_t* COPY_PROGRESS_WNDCLASS = L"ArcadeLauncherCopyProgressWnd";
static const wchar_t* SERVER_LOGIN_WNDCLASS = L"ArcadeLauncherServerLoginWnd";
static const wchar_t* DOWNLOAD_STATUS_WNDCLASS = L"ArcadeLauncherDownloadStatusWnd";
static const wchar_t* DOWNLOAD_GRAPH_WNDCLASS = L"ArcadeLauncherDownloadGraphWnd";

// Timer that drives throughput sampling + graph repaint while the download
// status window is visible. Local to that window, not the main window.
static constexpr UINT TIMER_DLSPEED = 1;

// Format a bytes/second rate as a compact human string (e.g. "12.4 MB/s").
static std::wstring FormatSpeed(double bps) {
    const wchar_t* unit = L"B/s";
    double v = bps;
    if (v >= 1024.0 * 1024.0 * 1024.0) { v /= 1024.0 * 1024.0 * 1024.0; unit = L"GB/s"; }
    else if (v >= 1024.0 * 1024.0)      { v /= 1024.0 * 1024.0;          unit = L"MB/s"; }
    else if (v >= 1024.0)               { v /= 1024.0;                   unit = L"KB/s"; }
    wchar_t buf[48];
    swprintf(buf, 48, (v >= 100.0 || unit[0] == L'B') ? L"%.0f %s" : L"%.1f %s", v, unit);
    return buf;
}

struct ServerInstallDonePayload {
    std::wstring gameId;
    ServerInstallResult result;
};

struct ServerInstallProgressPayload {
    std::wstring gameId;
    uint64_t done = 0;
    uint64_t total = 0;
};

static LRESULT CALLBACK CopyProgressWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (LRESULT r; dark::OnCtlColor(msg, wp, lp, r)) return r;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

class CopyProgressDialog {
public:
    bool Create(HWND owner, const std::wstring& title, const std::wstring& verb = L"Copying to local temp cache") {
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_PROGRESS_CLASS };
        InitCommonControlsEx(&icc);

        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = CopyProgressWndProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = dark::BgBrush();
            wc.lpszClassName = COPY_PROGRESS_WNDCLASS;
            RegisterClassExW(&wc);
            registered = true;
        }

        RECT ownerRc{};
        GetWindowRect(owner, &ownerRc);
        int w = 420;
        int h = 138;
        RECT wr = { 0, 0, w, h };
        AdjustWindowRectEx(&wr, WS_CAPTION | WS_POPUP, FALSE, WS_EX_DLGMODALFRAME);
        int winW = wr.right - wr.left;
        int winH = wr.bottom - wr.top;
        int x = ownerRc.left + ((ownerRc.right - ownerRc.left) - winW) / 2;
        int y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - winH) / 2;

        m_hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, COPY_PROGRESS_WNDCLASS,
            title.c_str(),
            WS_CAPTION | WS_POPUP,
            x, y, winW, winH, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!m_hwnd) return false;

        m_title = CreateWindowExW(0, WC_STATICW, title.c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            18, 18, w - 36, 22, m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        m_verb = verb;
        m_status = CreateWindowExW(0, WC_STATICW, (m_verb + L"...").c_str(),
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            18, 45, w - 36, 20, m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        m_progress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            18, 74, w - 36, 22, m_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        SendMessageW(m_progress, PBM_SETRANGE32, 0, 1000);
        SendMessageW(m_progress, PBM_SETPOS, 0, 0);

        dark::EnableTitleBar(m_hwnd);
        dark::Apply(m_hwnd);

        EnableWindow(owner, FALSE);
        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
        Pump();
        m_owner = owner;
        return true;
    }

    void SetProgress(uint64_t copied, uint64_t total, double mbps) {
        if (!m_hwnd) return;
        int pos = total > 0 ? (int)((copied * 1000) / total) : 0;
        if (pos > 1000) pos = 1000;
        SendMessageW(m_progress, PBM_SETPOS, pos, 0);

        std::wstring text = m_verb + L"... "
            + std::to_wstring((pos + 5) / 10) + L"%";
        if (mbps > 0.0) {
            wchar_t speed[64]{};
            swprintf_s(speed, L"  %.1f MB/s", mbps);
            text += speed;
        }
        SetWindowTextW(m_status, text.c_str());
        Pump();
    }

    void Close() {
        if (m_owner) {
            EnableWindow(m_owner, TRUE);
            SetForegroundWindow(m_owner);
        }
        if (m_hwnd) DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        m_owner = nullptr;
    }

private:
    void Pump() {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    HWND m_owner = nullptr;
    HWND m_hwnd = nullptr;
    HWND m_title = nullptr;
    HWND m_status = nullptr;
    HWND m_progress = nullptr;
    std::wstring m_verb;
};

struct ServerLoginState {
    ServerConfig* cfg = nullptr;
    HWND owner = nullptr;
    HWND hwnd = nullptr;
    HWND url = nullptr;
    HWND username = nullptr;
    HWND password = nullptr;
    HWND totpCode = nullptr;
    HWND installRoot = nullptr;
    HFONT uiFont = nullptr;
    HFONT titleFont = nullptr;
    bool done = false;
    bool saved = false;
};

static std::wstring TextOf(HWND h) {
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return {};
    std::wstring s(len + 1, L'\0');
    GetWindowTextW(h, s.data(), len + 1);
    s.resize(len);
    return s;
}

static HWND LoginLabel(HWND p, const wchar_t* text, int x, int y, int w) {
    return CreateWindowExW(0, WC_STATICW, text, WS_CHILD | WS_VISIBLE,
        x, y, w, 18, p, nullptr, GetModuleHandleW(nullptr), nullptr);
}

static HWND LoginEdit(HWND p, int id, const std::wstring& text, int x, int y, int w, DWORD extra = 0) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, text.c_str(),
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extra,
        x, y, w, 23, p, (HMENU)(intptr_t)id, GetModuleHandleW(nullptr), nullptr);
}

static LRESULT CALLBACK ServerLoginWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<ServerLoginState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<ServerLoginState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        st->hwnd = hwnd;

        CreateWindowExW(0, WC_STATICW, L"ArcadeLauncher Server",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            26, 22, 390, 24, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, WC_STATICW, L"Sign in to your private library before the launcher opens.",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            26, 50, 430, 18, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        LoginLabel(hwnd, L"Backend URL", 26, 86, 110);
        st->url = LoginEdit(hwnd, 101, st->cfg->baseUrl.empty() ? L"http://10.0.0.210:8721" : st->cfg->baseUrl, 150, 84, 330);
        LoginLabel(hwnd, L"Username/email", 26, 122, 110);
        st->username = LoginEdit(hwnd, 102, st->cfg->username, 150, 120, 330);
        LoginLabel(hwnd, L"Password", 26, 158, 110);
        st->password = LoginEdit(hwnd, 103, st->cfg->password, 150, 156, 330, ES_PASSWORD);
        LoginLabel(hwnd, L"2FA code", 26, 194, 110);
        st->totpCode = LoginEdit(hwnd, 105, L"", 150, 192, 120);
        LoginLabel(hwnd, L"Install Root", 26, 230, 110);
        st->installRoot = LoginEdit(hwnd, 104,
            st->cfg->installRoot.empty() ? GetAppDataPath() + L"\\ServerLibrary" : st->cfg->installRoot,
            150, 228, 330);

        CreateWindowExW(0, WC_BUTTONW, L"Sign In", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            286, 276, 92, 30, hwnd, (HMENU)IDOK, GetModuleHandleW(nullptr), nullptr);
        CreateWindowExW(0, WC_BUTTONW, L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            388, 276, 92, 30, hwnd, (HMENU)IDCANCEL, GetModuleHandleW(nullptr), nullptr);

        // Apply a modern UI font to every child control for a cleaner look.
        HFONT uiFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT titleFont = CreateFontW(-21, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        st->uiFont = uiFont;
        st->titleFont = titleFont;
        for (HWND child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
            SendMessageW(child, WM_SETFONT, (WPARAM)uiFont, TRUE);
        // The first static is the big title — give it the larger font.
        if (HWND firstChild = GetWindow(hwnd, GW_CHILD))
            SendMessageW(firstChild, WM_SETFONT, (WPARAM)titleFont, TRUE);
        dark::EnableTitleBar(hwnd);
        dark::Apply(hwnd);
        SetFocus(st->username);
        return 0;
    }
    if (!st) return DefWindowProcW(hwnd, msg, wp, lp);

    if (LRESULT r; dark::OnCtlColor(msg, wp, lp, r)) return r;

    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDOK) {
            st->cfg->enabled = true;
            st->cfg->baseUrl = TextOf(st->url);
            st->cfg->username = TextOf(st->username);
            st->cfg->password = TextOf(st->password);
            st->cfg->totpCode = TextOf(st->totpCode);
            st->cfg->installRoot = TextOf(st->installRoot);
            st->cfg->authToken.clear();
            st->saved = true;
            st->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (id == IDCANCEL) {
            st->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
    } else if (msg == WM_CLOSE) {
        st->done = true;
        DestroyWindow(hwnd);
        return 0;
    } else if (msg == WM_NCDESTROY) {
        if (st->uiFont)    { DeleteObject(st->uiFont);    st->uiFont = nullptr; }
        if (st->titleFont) { DeleteObject(st->titleFont); st->titleFont = nullptr; }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool ShowServerLoginDialog(HWND owner, ServerConfig& cfg) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ServerLoginWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = dark::BgBrush();
        wc.lpszClassName = SERVER_LOGIN_WNDCLASS;
        RegisterClassExW(&wc);
        registered = true;
    }

    RECT rc{};
    if (owner)
        GetWindowRect(owner, &rc);
    else
        rc = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    int w = 506, h = 336;
    RECT lwr = { 0, 0, w, h };
    AdjustWindowRectEx(&lwr, WS_CAPTION | WS_POPUP | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int winW = lwr.right - lwr.left;
    int winH = lwr.bottom - lwr.top;
    ServerLoginState st{ &cfg, owner };
    if (owner) EnableWindow(owner, FALSE);
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, SERVER_LOGIN_WNDCLASS,
        L"ArcadeLauncher Server Sign In", WS_CAPTION | WS_POPUP | WS_SYSMENU,
        rc.left + ((rc.right - rc.left) - winW) / 2,
        rc.top + ((rc.bottom - rc.top) - winH) / 2,
        winW, winH, owner, nullptr, GetModuleHandleW(nullptr), &st);
    if (!hwnd) {
        if (owner) EnableWindow(owner, TRUE);
        return false;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    return st.saved;
}

struct CopyProgressState {
    CopyProgressDialog* dialog = nullptr;
    uint64_t totalBytes = 0;
    uint64_t completedBeforeFile = 0;
    uint64_t copiedBytes = 0;
    std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
};

static DWORD CALLBACK CopyProgressRoutine(LARGE_INTEGER totalFileSize,
                                          LARGE_INTEGER totalBytesTransferred,
                                          LARGE_INTEGER,
                                          LARGE_INTEGER,
                                          DWORD,
                                          DWORD,
                                          HANDLE,
                                          HANDLE,
                                          LPVOID data) {
    auto* state = static_cast<CopyProgressState*>(data);
    if (!state || !state->dialog) return PROGRESS_CONTINUE;

    uint64_t current = state->completedBeforeFile + (uint64_t)totalBytesTransferred.QuadPart;
    state->copiedBytes = current;
    double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - state->startedAt).count();
    double mbps = seconds > 0.0 ? ((double)current / (1024.0 * 1024.0)) / seconds : 0.0;
    state->dialog->SetProgress(current, state->totalBytes, mbps);
    return PROGRESS_CONTINUE;
}

static std::wstring QuoteWindowsArgLocal(const std::wstring& arg) {
    std::wstring out = L"\"";
    size_t slashCount = 0;

    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashCount;
            continue;
        }

        if (c == L'"') {
            out.append(slashCount * 2 + 1, L'\\');
            out.push_back(c);
            slashCount = 0;
            continue;
        }

        out.append(slashCount, L'\\');
        slashCount = 0;
        out.push_back(c);
    }

    out.append(slashCount * 2, L'\\');
    out.push_back(L'"');
    return out;
}

static std::wstring GetLocalAppDataPath() {
    wchar_t path[MAX_PATH]{};
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path);
    return std::wstring(path) + L"\\ArcadeLauncher";
}

static std::wstring StablePathIdLocal(const std::wstring& path) {
    uint64_t hash = 1469598103934665603ull;
    for (wchar_t c : path) {
        hash ^= (uint64_t)towlower(c);
        hash *= 1099511628211ull;
    }

    wchar_t buf[17]{};
    swprintf_s(buf, L"%016llx", (unsigned long long)hash);
    return buf;
}

static bool IsRemoteFilePath(const std::wstring& path) {
    if (path.rfind(L"\\\\", 0) == 0) return true;
    if (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) {
        wchar_t root[] = { path[0], L':', L'\\', L'\0' };
        return GetDriveTypeW(root) == DRIVE_REMOTE;
    }
    return false;
}

static std::wstring ParentPathLocal(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}

static std::wstring FileNameOnlyLocal(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

static bool IsXbox360GodPackagePath(const std::wstring& romPath) {
    if (romPath.empty() || romPath.find(L'.') != std::wstring::npos) return false;

    std::wstring godDir = ParentPathLocal(romPath);
    std::wstring contentDir = FileNameOnlyLocal(godDir);
    for (auto& c : contentDir) c = towlower(c);
    if (contentDir != L"00007000" && contentDir != L"0007000") return false;

    DWORD attr = GetFileAttributesW((romPath + L".data").c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static void ReplaceAllLocal(std::wstring& s, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

static std::wstring RebuildArgsForCachedRom(const Game& game, const std::wstring& cachedRomPath) {
    std::wstring args = game.arguments;
    std::wstring oldQuoted = QuoteWindowsArgLocal(game.romPath);
    std::wstring newQuoted = QuoteWindowsArgLocal(cachedRomPath);

    ReplaceAllLocal(args, oldQuoted, newQuoted);
    ReplaceAllLocal(args, game.romPath, cachedRomPath);
    if (args == game.arguments)
        args = newQuoted;
    return args;
}

static uint64_t FileSizeOrZero(const std::wstring& path) {
    try {
        return fs::exists(path) && fs::is_regular_file(path) ? (uint64_t)fs::file_size(path) : 0;
    } catch (...) {
        return 0;
    }
}

static uint64_t DirectorySizeOrZero(const std::wstring& path) {
    uint64_t total = 0;
    try {
        if (!fs::exists(path)) return 0;
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.is_regular_file())
                total += (uint64_t)entry.file_size();
        }
    } catch (...) {
    }
    return total;
}

static bool CopyFileWithProgress(const std::wstring& src,
                                 const std::wstring& dst,
                                 CopyProgressState& progress) {
    fs::create_directories(ParentPathLocal(dst));

    BOOL cancel = FALSE;
    if (!CopyFileExW(src.c_str(), dst.c_str(), CopyProgressRoutine,
                     &progress, &cancel, 0)) {
        return false;
    }

    progress.completedBeforeFile += FileSizeOrZero(src);
    double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - progress.startedAt).count();
    double mbps = seconds > 0.0
        ? ((double)progress.completedBeforeFile / (1024.0 * 1024.0)) / seconds
        : 0.0;
    progress.dialog->SetProgress(progress.completedBeforeFile, progress.totalBytes, mbps);
    return true;
}

static bool CopyDirectoryWithProgress(const std::wstring& srcDir,
                                      const std::wstring& dstDir,
                                      CopyProgressState& progress) {
    try {
        fs::create_directories(dstDir);
        for (const auto& entry : fs::recursive_directory_iterator(srcDir)) {
            const fs::path rel = fs::relative(entry.path(), srcDir);
            const fs::path dst = fs::path(dstDir) / rel;

            if (entry.is_directory()) {
                fs::create_directories(dst);
                continue;
            }

            if (!entry.is_regular_file()) continue;
            if (!CopyFileWithProgress(entry.path().wstring(), dst.wstring(), progress))
                return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

static bool EnsureXbox360GodLocalCache(const Game& game,
                                       std::wstring& cachedRomPath,
                                       std::wstring& cacheRootPath,
                                       HWND owner) {
    if (game.platform != Platform::Xbox360) return false;
    if (!IsRemoteFilePath(game.romPath)) return false;
    if (!IsXbox360GodPackagePath(game.romPath)) return false;

    try {
        std::wstring srcGodDir = ParentPathLocal(game.romPath);
        std::wstring packageName = FileNameOnlyLocal(game.romPath);
        std::wstring srcDataDir = game.romPath + L".data";
        std::wstring cacheRoot = GetLocalAppDataPath() + L"\\XeniaGodCache\\" + StablePathIdLocal(srcGodDir);
        std::wstring dstGodDir = cacheRoot + L"\\00007000";
        std::wstring dstPackage = dstGodDir + L"\\" + packageName;
        std::wstring dstDataDir = dstPackage + L".data";

        CopyProgressDialog dialog;
        std::wstring title = L"Copying " + game.title;
        dialog.Create(owner, title);

        CopyProgressState progress{};
        progress.dialog = &dialog;
        progress.totalBytes = FileSizeOrZero(game.romPath) + DirectorySizeOrZero(srcDataDir);

        fs::remove_all(cacheRoot);
        fs::create_directories(dstGodDir);
        bool copied = CopyFileWithProgress(game.romPath, dstPackage, progress)
            && CopyDirectoryWithProgress(srcDataDir, dstDataDir, progress);
        dialog.Close();
        if (!copied) {
            fs::remove_all(cacheRoot);
            return false;
        }

        cachedRomPath = dstPackage;
        cacheRootPath = cacheRoot;
        return true;
    } catch (...) {
        cachedRomPath.clear();
        cacheRootPath.clear();
        return false;
    }
}

static void CleanupXbox360GodLocalCache(const std::wstring& cacheRootPath) {
    if (cacheRootPath.empty()) return;

    try {
        std::wstring expectedRoot = GetLocalAppDataPath() + L"\\XeniaGodCache\\";
        if (cacheRootPath.rfind(expectedRoot, 0) != 0) return;
        fs::remove_all(cacheRootPath);
    } catch (...) {
    }
}

App::App() {}
App::~App() { StopArtDecodeWorker(); }

void App::StartArtDecodeWorker() {
    if (m_artThread.joinable()) return;
    HWND hwnd = m_hwnd;
    m_artThread = std::thread([this, hwnd]() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (;;) {
            std::pair<std::wstring, std::wstring> job;
            {
                std::unique_lock<std::mutex> lk(m_artQueueMutex);
                m_artQueueCv.wait(lk, [this] { return m_artStop || !m_artQueue.empty(); });
                if (m_artStop && m_artQueue.empty()) break;
                job = std::move(m_artQueue.front());
                m_artQueue.pop_front();
            }
            auto* img = new Renderer::DecodedImage();
            img->gameId = job.first;
            if (Renderer::DecodeImageFile(job.second, *img) && !img->pixels.empty()) {
                if (!PostMessageW(hwnd, WM_ART_DECODED, 0, (LPARAM)img))
                    delete img;   // window gone — drop it
            } else {
                delete img;
            }
        }
        CoUninitialize();
    });
}

void App::StopArtDecodeWorker() {
    {
        std::lock_guard<std::mutex> lk(m_artQueueMutex);
        m_artStop = true;
    }
    m_artQueueCv.notify_all();
    if (m_artThread.joinable()) m_artThread.join();
}

void App::QueueArtDecode(const std::wstring& gameId, const std::wstring& path) {
    if (gameId.empty() || path.empty()) return;
    {
        std::lock_guard<std::mutex> lk(m_artQueueMutex);
        m_artQueue.emplace_back(gameId, path);
    }
    m_artQueueCv.notify_one();
}

bool App::Initialize(HINSTANCE hInstance, bool startInTray) {
    m_hInst = hInstance;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = WNDCLASS_NAME;
    wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(101)); // IDI_APPICON
    wc.hIconSm       = LoadIcon(hInstance, MAKEINTRESOURCE(101)); // IDI_APPICON (small)
    RegisterClassExW(&wc);

    LoadAll();

    auto& cfg = m_config.Get();
    // Steam-style login gate: when not signed in, show ONLY the sign-in window.
    // The launcher window is not created until the user signs in. Closing or
    // cancelling the sign-in window exits the app instead of opening the client.
    if (cfg.server.baseUrl.empty() || cfg.server.username.empty() || cfg.server.password.empty()) {
        if (!ShowServerLoginDialog(nullptr, cfg.server))
            return false;
        SaveAll();
    }

    m_hwnd = CreateWindowExW(0, WNDCLASS_NAME, L"ArcadeLauncher",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        cfg.windowWidth, cfg.windowHeight,
        nullptr, nullptr, hInstance, this);
    if (!m_hwnd) return false;

    // Auto-detect emulator paths on first launch (persist so they survive restarts)
    {
        auto& emus = m_config.Get().emulators;
        if (emus.dolphinPath.empty())
            emus.dolphinPath = PlatformIcons::FindDolphinExe(L"");
        if (emus.ryujinxPath.empty())
            emus.ryujinxPath = PlatformIcons::FindRyujinxExe(L"");
        if (!emus.dolphinPath.empty() || !emus.ryujinxPath.empty())
            m_config.Save(GetAppDataPath() + L"\\config.json");
    }

    m_renderer.Initialize(m_hwnd);

    // Load platform icons on the render thread (D2D bitmap creation must happen here)
    // The FitGirl download is quick; icon extraction is local-only.
    m_renderer.LoadPlatformIcons(m_platformIcons, m_config.Get().emulators);

    // Wire up IGDB client with saved credentials + cached token
    if (!cfg.igdbClientId.empty()) {
        m_igdbClient.SetCredentials(cfg.igdbClientId, cfg.igdbClientSecret);
        if (!cfg.igdbAccessToken.empty())
            m_igdbClient.RestoreToken(cfg.igdbAccessToken, cfg.igdbTokenExpiry);

        std::wstring artDir = GetAppDataPath() + L"\\art";
        m_metaManager = std::make_unique<MetadataManager>(
            m_library, m_igdbClient, artDir);
    }

    ApplyFilter();

    // Start the off-thread art decoder and immediately load any covers already
    // cached on disk, so the grid shows local box art instantly instead of
    // waiting for the network metadata sync to complete.
    StartArtDecodeWorker();
    for (const auto& g : m_library.All()) {
        if (!g.coverArtPath.empty() &&
            GetFileAttributesW(g.coverArtPath.c_str()) != INVALID_FILE_ATTRIBUTES)
            QueueArtDecode(g.id, g.coverArtPath);
    }

    if (cfg.startFullscreen) {
        DWORD style = (DWORD)GetWindowLongW(m_hwnd, GWL_STYLE);
        SetWindowLongW(m_hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        auto& r = mi.rcMonitor;
        SetWindowPos(m_hwnd, HWND_TOP, r.left, r.top,
                     r.right - r.left, r.bottom - r.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        m_fullscreen = true;
    }

    CreateTrayIcon();

    if (startInTray) {
        // Boot launch — stay hidden in tray, don't flash on screen.
        ShowWindow(m_hwnd, SW_HIDE);
    } else {
        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
    }

    // On first launch, offer to download missing emulators.
    if (!m_config.Get().firstLaunchDone) {
        m_setup.Open(m_hwnd, m_config.Get(), [this]() {
            m_config.Get().firstLaunchDone = true;
            SaveAll();
            m_renderer.LoadPlatformIcons(m_platformIcons, m_config.Get().emulators);
            UpdateSidebarFlags();
            std::thread([this]() { ScanAllPlatforms(); }).detach();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        });
    }

    // Start art fetcher
    std::wstring artDir = GetAppDataPath() + L"\\art";
    m_fetcher = std::make_unique<MetadataFetcher>(artDir, m_config.Get().steamGridDbApiKey);

    // If the Repacks/FitGirl icon isn't cached yet, download it in the background.
    // Once the file is on disk, post WM_USER+3 so the render thread can create
    // the D2D bitmap (D2D is single-threaded; bitmap creation must stay on this thread).
    {
        std::wstring appDir  = GetAppDataPath();
        bool needRepacks     = GetFileAttributesW((appDir + L"\\repacks_icon.png").c_str()) == INVALID_FILE_ATTRIBUTES;
        bool needConsole     = GetFileAttributesW((appDir + L"\\icon_ps.ico").c_str())       == INVALID_FILE_ATTRIBUTES
                            || GetFileAttributesW((appDir + L"\\icon_xbox.ico").c_str())     == INVALID_FILE_ATTRIBUTES;
        if (needRepacks || needConsole) {
            std::thread([this, needRepacks, needConsole]() {
                std::wstring dir = GetAppDataPath();
                bool any = false;
                if (needRepacks)  any |= !PlatformIcons::DownloadRepacksIcon(dir).empty();
                if (needConsole)  any |=  PlatformIcons::DownloadConsoleIcons(dir);
                if (any) PostMessageW(m_hwnd, WM_USER + 3, 0, 0);
            }).detach();
        }
    }

    {
        std::wstring dbPath = GetAppDataPath() + L"\\romdb.sqlite";
        m_romDb.Load(dbPath);
        if (m_igdbClient.HasCredentials())
            IgdbSync::StartAsync(m_hwnd, m_igdbClient, dbPath);
    }

    UpdateSidebarFlags();

    // Kick off initial scan in background
    std::thread([this]() { ScanAllPlatforms(); }).detach();

    // Fetch the account profile picture for the topbar profile button.
    RefreshAvatar();

    // If a server library is configured, check whether the admin has flagged this
    // account for a forced password change. Done off the UI thread; the modal is
    // shown via WM_USER+5 once we know.
    if (m_config.Get().server.enabled) {
        std::thread([this]() {
            ServerConfig sc = m_config.Get().server;
            ServerClient client(sc);
            ServerClient::AccountInfo info;
            std::wstring err;
            if (client.GetAccount(info, err) && info.mustChangePassword)
                PostMessageW(m_hwnd, WM_USER + 7, 0, 0);
        }).detach();
    }

    // Check for a newer release on GitHub (silent — only fires WM_APP_UPDATE_FOUND if one exists)
    CheckForAppUpdateAsync(m_hwnd);

    // Timers
    SetTimer(m_hwnd, TIMER_ANIM,    16,  nullptr); // ~60fps
    SetTimer(m_hwnd, TIMER_SAVE, 30000,  nullptr); // autosave every 30s
    SetTimer(m_hwnd, TIMER_RESYNC, 600000, nullptr); // re-sync server catalog every 10 min

    // Launched from a game shortcut (`--launch <id>`): start it once the loop runs.
    if (!m_pendingLaunchId.empty())
        PostMessageW(m_hwnd, WM_USER + 8, 0,
                     (LPARAM)new std::wstring(m_pendingLaunchId));

    return true;
}

void App::LaunchById(const std::wstring& id) {
    if (auto* g = m_library.FindById(id)) {
        Game copy = *g;
        LaunchGame(copy);
    }
}

int App::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        // Allow picker dialog to handle Tab/Enter keyboard navigation
        if (m_picker.IsOpen() && IsDialogMessage(m_picker.GetHwnd(), &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (app) return app->HandleMessage(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Heap payload posted from the changelog worker thread to the render thread.
// Owned and deleted by the WM_CHANGELOGS_READY handler (main thread).
struct ChangelogPayload {
    std::wstring gameId;   // local Game::id the changelogs belong to
    std::vector<ServerClient::ChangelogEntry> entries;
};

LRESULT App::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
        // Hide to tray instead of closing — use Exit from the tray menu to truly quit.
        ShowWindow(m_hwnd, SW_HIDE);
        return 0;

    case WM_SYSCOMMAND:
        // Minimize button → hide to tray as well.
        if ((wp & 0xFFF0) == SC_MINIMIZE) {
            ShowWindow(m_hwnd, SW_HIDE);
            return 0;
        }
        break;

    case WM_ROMDB_READY:
    case WM_IGDBSYNC_DONE: {
        // ROM database finished downloading — load it and rescan so titles
        // are enhanced immediately without requiring a launcher restart.
        std::wstring dbPath = GetAppDataPath() + L"\\romdb.sqlite";
        if (m_romDb.Load(dbPath))
            std::thread([this]() { ScanAllPlatforms(); }).detach();
        return 0;
    }

    case WM_GAME_CLOSED:
        // A launched game just exited — restore the launcher to the front.
        ShowWindow_(true);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_SERVER_INSTALL_DONE: {
        auto* payload = reinterpret_cast<ServerInstallDonePayload*>(lp);
        if (!payload) return 0;
        std::unique_ptr<ServerInstallDonePayload> done(payload);
        bool autoLaunch = false;
        bool wantDesktop = false, wantStartMenu = false;
        {
            std::lock_guard<std::mutex> lk(m_downloadMutex);
            for (auto& job : m_downloadQueue) {
                if (job.gameId == done->gameId) {
                    job.running = false;
                    job.complete = done->result.ok;
                    job.failed = !done->result.ok;
                    job.error = done->result.error;
                    autoLaunch = job.autoLaunch;
                    wantDesktop = job.makeDesktopShortcut;
                    wantStartMenu = job.makeStartMenuShortcut;
                    if (done->result.ok && job.total > 0) job.done = job.total;
                    break;
                }
            }
        }
        if (auto* stored = m_library.FindById(done->gameId)) {
            if (done->result.ok) {
                stored->installRoot = done->result.installRoot;
                if (stored->emulatorPath.empty())
                    stored->exePath = done->result.launchPath;
                else
                    stored->romPath = done->result.launchPath;
                stored->arguments = done->result.arguments;
                stored->serverVersion = done->result.version;
                stored->installState = InstallState::Installed;
                stored->installProgressPermille = 1000;
                SaveAll();
                if (wantDesktop || wantStartMenu)
                    CreateGameShortcuts(stored->id, stored->title,
                                        PlatformName(stored->platform),
                                        wantDesktop, wantStartMenu);
                if (!autoLaunch)
                    ShowToast(L"Download complete",
                              stored->title + L" is ready to play.");
                if (autoLaunch) {
                    Game launchCopy = *stored;
                    ApplyFilter();
                    InvalidateRect(m_hwnd, nullptr, FALSE);
                    RefreshDownloadStatusWindow();
                    LaunchInstalledGame(launchCopy);
                    return 0;
                }
            } else {
                stored->installState = InstallState::Missing;
                stored->installProgressPermille = 0;
                // A 401 means the cached token was rejected (e.g. revoked or the
                // machine fingerprint drifted) — drop it and offer interactive
                // re-authentication so a stale session can be recovered without
                // restarting the launcher.
                if (done->result.error.find(L"401") != std::wstring::npos) {
                    InvalidateServerToken();
                    if (PromptReAuth()) {
                        ShowToast(L"Signed back in",
                                  L"Your session was renewed — retry the download.");
                    } else {
                        MessageBoxW(m_hwnd,
                            (L"Background download failed:\n" + done->result.error).c_str(),
                            L"ArcadeLauncher Server", MB_OK | MB_ICONERROR);
                    }
                } else {
                    MessageBoxW(m_hwnd,
                        (L"Background download failed:\n" + done->result.error).c_str(),
                        L"ArcadeLauncher Server", MB_OK | MB_ICONERROR);
                }
            }
            ApplyFilter();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        RefreshDownloadStatusWindow();
        return 0;
    }

    case WM_SERVER_INSTALL_PROGRESS: {
        auto* payload = reinterpret_cast<ServerInstallProgressPayload*>(lp);
        if (!payload) return 0;
        std::unique_ptr<ServerInstallProgressPayload> progress(payload);
        {
            std::lock_guard<std::mutex> lk(m_downloadMutex);
            for (auto& job : m_downloadQueue) {
                if (job.gameId == progress->gameId) {
                    job.done = progress->done;
                    job.total = progress->total;
                    job.running = true;
                    break;
                }
            }
        }
        if (auto* stored = m_library.FindById(progress->gameId)) {
            stored->installState = InstallState::Downloading;
            stored->installProgressPermille = progress->total > 0
                ? (int)std::min<uint64_t>(1000, progress->done * 1000 / progress->total)
                : 0;
        }
        if (m_downloadStatusWnd && IsWindowVisible(m_downloadStatusWnd))
            RefreshDownloadStatusWindow();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_TRAYICON:
        switch (LOWORD(lp)) {
        case WM_LBUTTONDBLCLK:
            ShowWindow_(true);
            break;
        case WM_RBUTTONUP:
            ShowTrayMenu();
            break;
        }
        return 0;

    case WM_DESTROY:
        OnDestroy();
        PostQuitMessage(0);
        return 0;

    case WM_SIZE:
        OnSize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        OnPaint();
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        OnTimer((UINT)wp);
        return 0;

    case WM_ACTIVATEAPP:
        // Re-sync the server catalog when the launcher regains focus
        // (debounced inside TriggerResync to absorb focus flicker).
        if (wp /* becoming active */)
            TriggerResync();
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONDOWN:
        OnLButtonDown((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONDBLCLK:
        OnLButtonDblClk((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONUP:
        OnLButtonUp((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
        return 0;

    case WM_RBUTTONDOWN:
        OnRButtonDown((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
        return 0;

    case WM_CHAR:
        OnChar((wchar_t)wp);
        return 0;

    case WM_KEYDOWN:
        OnKeyDown(wp);
        return 0;

    case WM_SYSKEYDOWN:
        // Alt key alone — show our menu bar instead of the default system menu
        if (wp == VK_MENU && !m_menuActive) {
            ShowMenuBar();
            return 0;
        }
        break;

    case WM_MOUSEWHEEL: {
        short delta = GET_WHEEL_DELTA_WPARAM(wp);
        OnScroll((float)delta);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize = { 800, 500 };
        return 0;
    }

    case WM_USER + 1: {
        // Scan complete — update sidebar visibility flags, rebuild visible list,
        // and repaint. This runs on the main thread (safe to touch render state).
        auto* scanned = reinterpret_cast<std::vector<Game>*>(lp);
        if (scanned) {
            // Snapshot which games already showed an update so a resync only
            // toasts newly-discovered ones (Steam-style "update available").
            std::unordered_set<std::wstring> wasUpdatable;
            for (const auto& g : m_library.All())
                if (g.installState == InstallState::UpdateAvailable)
                    wasUpdatable.insert(g.id);

            m_library.MergeGames(std::move(*scanned));
            delete scanned;

            int newUpdates = 0;
            std::wstring lastTitle;
            for (const auto& g : m_library.All()) {
                if (g.serverBacked && g.installState == InstallState::UpdateAvailable &&
                    wasUpdatable.count(g.id) == 0) {
                    ++newUpdates;
                    lastTitle = g.title;
                }
            }
            if (newUpdates == 1)
                ShowToast(L"Update available", lastTitle + L" has a new version.");
            else if (newUpdates > 1)
                ShowToast(L"Updates available",
                          std::to_wstring(newUpdates) + L" games have new versions.");
        }

        if (m_metaManager) {
            // The server marks a game igdbMatched as soon as it has an igdbId, even
            // if it never produced a cover URL (e.g. the freshly-split GameCube/Wii
            // entries). Without this, ScanAllAsync would skip them and they'd never
            // get box art. Re-arm the IGDB search for any game with no cover at all.
            for (const auto& g : m_library.All()) {
                if (g.coverArtUrl.empty() && g.coverArtPath.empty()) {
                    if (Game* gm = m_library.FindById(g.id))
                        gm->igdbMatched = false;
                }
            }
            m_metaManager->ScanAllAsync([this](const std::wstring& id, bool /*matched*/) {
                PostMessageW(m_hwnd, WM_USER + 4, 0, (LPARAM)new std::wstring(id));
            });
        }

        if (m_fetcher) {
            for (auto& g : m_library.All()) {
                if (g.coverArtPath.empty()) {
                    Game copy = g;
                    m_fetcher->FetchArtAsync(copy, [this, id = g.id](const std::wstring& path) {
                        if (auto* pg = m_library.FindById(id))
                            pg->coverArtPath = path;
                        PostMessageW(m_hwnd, WM_USER + 4, 0, (LPARAM)new std::wstring(id));
                    });
                } else {
                    PostMessageW(m_hwnd, WM_USER + 4, 0, (LPARAM)new std::wstring(g.id));
                }
            }
        }

        UpdateSidebarFlags();
        ApplyFilter();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_USER + 3:
        // Background thread finished downloading platform icons.
        // Create D2D bitmaps here on the render thread (D2D is single-threaded).
        m_platformIcons.TryDownloadAndLoadRepacks(m_renderer.GetRT(), m_renderer.GetWIC());
        m_platformIcons.TryLoadConsoleIcons(m_renderer.GetRT(), m_renderer.GetWIC());
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;

    case WM_USER + 4: {
        // Background thread signalled that a game's art is ready on disk.
        // Create the D2D bitmap here on the render thread.
        std::wstring* idPtr = reinterpret_cast<std::wstring*>(lp);
        std::wstring  id    = std::move(*idPtr);
        delete idPtr;
        // Decode off the UI thread — the worker posts WM_ART_DECODED when the
        // CPU buffer is ready, keeping the render thread responsive.
        if (auto* g = m_library.FindById(id)) {
            if (!g->coverArtPath.empty())
                QueueArtDecode(g->id, g->coverArtPath);
        }
        return 0;
    }

    case WM_ART_DECODED: {
        // Worker finished decoding a cover into a CPU buffer. Upload it as a D2D
        // bitmap here on the render thread (cheap) and repaint.
        std::unique_ptr<Renderer::DecodedImage> img(
            reinterpret_cast<Renderer::DecodedImage*>(lp));
        if (img) {
            m_renderer.StoreDecodedArt(*img);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_USER + 8: {
        // Launch a game by id (from a desktop/Start-Menu shortcut, possibly
        // forwarded from a second instance).
        std::unique_ptr<std::wstring> id(reinterpret_cast<std::wstring*>(lp));
        if (id && !id->empty()) LaunchById(*id);
        return 0;
    }

    case WM_COPYDATA: {
        // A second `--launch <id>` instance forwarded a game id to us.
        auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lp);
        if (cds && cds->dwData == kCopyDataLaunchId && cds->lpData &&
            cds->cbData >= sizeof(wchar_t)) {
            std::wstring id(reinterpret_cast<const wchar_t*>(cds->lpData),
                            cds->cbData / sizeof(wchar_t));
            while (!id.empty() && id.back() == L'\0') id.pop_back();
            if (!id.empty()) {
                ShowWindow(hwnd, SW_SHOW);
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
                LaunchById(id);
            }
        }
        return TRUE;
    }

    case WM_CHANGELOGS_READY: {
        // Changelog fetch finished on a worker thread. Take ownership of the
        // heap payload and apply it only if the detail panel is still showing
        // the same game (the user may have navigated away or closed it).
        auto* payload = reinterpret_cast<ChangelogPayload*>(lp);
        if (payload) {
            if (m_renderState.detailChangelogGameId == payload->gameId) {
                m_renderState.detailChangelogs.clear();
                for (auto& e : payload->entries) {
                    ChangelogView v;
                    v.version   = std::move(e.version);
                    v.title     = std::move(e.title);
                    v.body      = std::move(e.body);
                    v.createdAt = e.createdAt;
                    m_renderState.detailChangelogs.push_back(std::move(v));
                }
                m_renderState.detailChangelogsLoading = false;
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            delete payload;
        }
        return 0;
    }

    case WM_AVATAR_READY: {
        // Avatar bytes fetched on a worker thread (lParam = std::string* or null
        // to clear). WIC decode + D2D bitmap creation happen here on the UI
        // (render-target) thread.
        auto* payload = reinterpret_cast<std::string*>(lp);
        if (payload) {
            m_renderer.SetAvatarFromMemory(payload->data(), payload->size());
            delete payload;
        } else {
            m_renderer.ClearAvatar();
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_USER + 7: {
        // The server flagged this account for a forced password change.
        ShowWindow_(true);
        ServerConfig sc = m_config.Get().server;
        if (ShowForcedPasswordChange(m_hwnd, sc)) {
            m_config.Get().server.password = sc.password;
            m_config.Get().server.authToken = sc.authToken;
            SaveAll();
        }
        return 0;
    }

    case WM_APP_UPDATE_FOUND: {
        // Background thread found a newer release — surface the window so the
        // prompt is visible even when running hidden in the tray.
        ShowWindow_(true);
        auto* info = reinterpret_cast<AppUpdateInfo*>(lp);
        std::wstring msg =
            L"ArcadeLauncher " + info->tag + L" is available!\n\n"
            L"Download and install now?\n"
            L"The app will close automatically once the installer is ready.";
        int choice = MessageBoxW(hwnd, msg.c_str(),
                                 L"Update Available",
                                 MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON1);
        if (choice == IDYES)
            DownloadAndInstallAsync(m_hwnd, info->msiUrl);
        delete info;
        return 0;
    }

    case WM_APP_UPDATE_READY:
        if (wp == 1) {
            // Download failed
            MessageBoxW(hwnd,
                L"The update could not be downloaded.\n\n"
                L"Please visit github.com/TheStonedGamer/ArcadeLauncher-Client/releases to update manually.",
                L"Update Failed", MB_OK | MB_ICONWARNING);
        } else {
            // msiexec is running — exit immediately so it can replace the binary.
            // Schedule a PowerShell one-liner that waits 10 s then relaunches us,
            // then call ExitProcess so no thread holds a file lock on the exe.
            wchar_t exePath[MAX_PATH]{};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);

            std::wstring psCmd =
                L"Start-Sleep -Seconds 10; "
                L"Start-Process -FilePath '" + std::wstring(exePath) + L"'";
            std::wstring psArgs =
                L"-NoProfile -WindowStyle Hidden -Command \"" + psCmd + L"\"";
            ShellExecuteW(nullptr, L"open", L"powershell.exe",
                          psArgs.c_str(), nullptr, SW_HIDE);

            SaveAll();
            RemoveTrayIcon();
            ExitProcess(0);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Event handlers ────────────────────────────────────────────────────────────

void App::OnCreate(HWND) {}

void App::OnDestroy() {
    RemoveTrayIcon();
    if (m_metaManager) m_metaManager->Shutdown();
    m_fetcher->Shutdown();
    SaveAll();
}

void App::OnSize(UINT w, UINT h) {
    if (w && h) {
        m_renderer.Resize(w, h);
        // Persist window dimensions so the next launch opens at the same size.
        // Skip while fullscreen — we don't want to overwrite the windowed size.
        if (!m_fullscreen) {
            m_config.Get().windowWidth  = (int)w;
            m_config.Get().windowHeight = (int)h;
        }
        m_renderState.sidebarScroll =
            std::min(m_renderState.sidebarScroll, m_renderer.MaxSidebarScroll(m_renderState));
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void App::OnPaint() {
    m_renderState.metaScanning = m_metaManager && m_metaManager->IsRunning();
    {
        std::lock_guard<std::mutex> lk(m_downloadMutex);
        int active = 0;
        for (const auto& job : m_downloadQueue)
            if (!job.complete && !job.failed) ++active;
        m_renderState.activeDownloadCount = active;
    }
    m_renderer.Render(m_visibleGames, m_renderState);
}

void App::OnTimer(UINT timerId) {
    if (timerId == TIMER_SAVE) {
        SaveAll();
        return;
    }

    if (timerId == TIMER_RESYNC) {
        TriggerResync();
        return;
    }

    // TIMER_ANIM: smooth scroll animation
    float& s = m_renderState.scrollOffset;
    float& t = m_renderState.targetScroll;
    float diff = t - s;
    if (fabsf(diff) > 0.5f) {
        s += diff * 0.18f;
        InvalidateRect(m_hwnd, nullptr, FALSE);
    } else {
        s = t;
    }

    // Repaint if game is running (for any live updates)
    if (m_monitor.IsRunning())
        InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::OnMouseMove(float x, float y) {
    m_lastMouseX = x; m_lastMouseY = y;

    // Auto-reveal menu bar: only trigger in the top-left corner hot zone,
    // not the entire top edge of the window.
    if (y < 20.0f && x < 80.0f && !m_menuActive)
        ShowMenuBar();

    int prev = m_renderState.hoveredIndex;

    if (!m_renderState.detailOpen) {
        m_renderState.hoveredIndex =
            m_renderer.HitTestGrid(x, y, m_renderState, m_visibleGames.size());
    }

    if (m_renderState.hoveredIndex != prev)
        InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::OnLButtonDown(float x, float y) {
    if (m_renderState.detailOpen) {
        if (m_renderer.HitTestLaunchBtn(x, y)) {
            if (m_renderState.detailIndex >= 0 &&
                m_renderState.detailIndex < (int)m_visibleGames.size())
                LaunchGame(*m_visibleGames[m_renderState.detailIndex]);
        } else {
            // Close detail view
            m_renderState.detailOpen = false;
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return;
    }

    if (m_renderer.HitTestSearch(x, y)) {
        m_renderState.focusArea = FocusArea::Search;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_renderer.HitTestSelectModeBtn(x, y)) {
        m_renderState.selectionMode = !m_renderState.selectionMode;
        if (!m_renderState.selectionMode)
            m_renderState.selectedGameIds.clear();
        m_renderState.focusArea = FocusArea::Grid;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_renderer.HitTestDownloadsBtn(x, y)) {
        OpenDownloadStatus();
        return;
    }

    if (m_renderer.HitTestSortBtn(x, y)) {
        // Cycle Recent -> Title -> Platform -> Rating -> Playtime -> Recent
        int next = (static_cast<int>(m_renderState.sortMode) + 1) % 5;
        m_renderState.sortMode = static_cast<SortMode>(next);
        ApplyFilter();
        // No toast — the sort button glyph itself now reflects the active mode.
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_renderer.HitTestProfileBtn(x, y)) {
        ShowProfileMenu();
        return;
    }

    if (m_renderer.HitTestSettingsBtn(x, y)) {
        OpenSettings();
        return;
    }

    if (m_renderer.HitTestEmptyStateBtn(x, y)) {
        // Map the currently-filtered platform to its settings page
        int page = SettingsWindow::PAGE_GENERAL;
        if (!m_renderState.filterAll) {
            switch (m_renderState.filterPlatform) {
            case Platform::Dolphin: page = SettingsWindow::PAGE_DOLPHIN; break;
            case Platform::GameCube: page = SettingsWindow::PAGE_DOLPHIN; break;
            case Platform::Wii:     page = SettingsWindow::PAGE_DOLPHIN; break;
            case Platform::Ryujinx: page = SettingsWindow::PAGE_RYUJINX; break;
            case Platform::RPCS3:   page = SettingsWindow::PAGE_RPCS3;   break;
            case Platform::N64:     page = SettingsWindow::PAGE_N64;     break;
            case Platform::NES:     page = SettingsWindow::PAGE_NES;     break;
            case Platform::SNES:    page = SettingsWindow::PAGE_SNES;    break;
            case Platform::PS1:     page = SettingsWindow::PAGE_PS1;     break;
            case Platform::PS2:     page = SettingsWindow::PAGE_PS2;     break;
            case Platform::Xbox360: page = SettingsWindow::PAGE_XBOX360; break;
            case Platform::Xbox:    page = SettingsWindow::PAGE_XBOX;    break;
            default: break;
            }
        }
        OpenSettings(page);
        return;
    }

    Platform p; bool all; LibraryPage page; std::wstring col;
    if (m_renderer.HitTestSidebar(x, y, m_renderState, p, all, page, col)) {
        m_renderState.filterAll        = all;
        m_renderState.filterPlatform   = p;
        m_renderState.libraryPage      = page;
        m_renderState.filterCollection = col;
        m_renderState.focusArea        = FocusArea::Sidebar;
        ApplyFilter();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    int idx = m_renderer.HitTestGrid(x, y, m_renderState, m_visibleGames.size());
    if (idx >= 0) {
        m_renderState.focusArea = FocusArea::Grid;
        if (m_renderState.selectionMode) {
            const std::wstring& id = m_visibleGames[idx]->id;
            auto it = m_renderState.selectedGameIds.find(id);
            if (it != m_renderState.selectedGameIds.end())
                m_renderState.selectedGameIds.erase(it);
            else
                m_renderState.selectedGameIds.insert(id);
            m_renderState.selectedIndex = idx;
        } else {
            m_renderState.selectedIndex = idx;
            OpenDetail(idx);
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    } else {
        // Click on background — return focus to grid
        m_renderState.focusArea = FocusArea::Grid;
    }
}

void App::OnLButtonDblClk(float x, float y) {
    if (m_renderState.selectionMode || m_renderState.detailOpen)
        return;

    int idx = m_renderer.HitTestGrid(x, y, m_renderState, m_visibleGames.size());
    if (idx >= 0 && idx < (int)m_visibleGames.size()) {
        m_renderState.focusArea = FocusArea::Grid;
        m_renderState.selectedIndex = idx;
        LaunchGame(*m_visibleGames[idx]);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void App::OnLButtonUp(float x, float y) {
    (void)x; (void)y;
}

void App::OnChar(wchar_t ch) {
    if (m_renderState.focusArea != FocusArea::Search) return;
    if (ch == L'\b') {
        if (!m_renderState.searchQuery.empty())
            m_renderState.searchQuery.pop_back();
    } else if (ch == L'\t' || ch == L'\r' || ch == L'\x1B') {
        return; // handled in OnKeyDown
    } else if (ch >= 32) {
        m_renderState.searchQuery += ch;
    }
    ApplyFilter();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::OnKeyDown(WPARAM vk) {
    bool shift = (GetKeyState(VK_SHIFT)   & 0x8000) != 0;
    bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    // ── Detail panel — eats all keys while open ───────────────────────────────
    if (m_renderState.detailOpen) {
        int n = (int)m_visibleGames.size();
        switch (vk) {
        case VK_ESCAPE:
        case VK_BACK:
            m_renderState.detailOpen = false;
            break;
        case VK_RETURN:
        case 'L':
            if (m_renderState.detailIndex >= 0 && m_renderState.detailIndex < n)
                LaunchGame(*m_visibleGames[m_renderState.detailIndex]);
            break;
        case 'M':
            if (m_renderState.detailIndex >= 0 && m_renderState.detailIndex < n) {
                auto* g = m_visibleGames[m_renderState.detailIndex];
                OpenMetadataPicker(g->id, g->title);
            }
            break;
        case 'E':
            if (m_renderState.detailIndex >= 0 && m_renderState.detailIndex < n)
                OpenEditTitle(m_renderState.detailIndex);
            break;
        case 'P':
            if (m_renderState.detailIndex >= 0 && m_renderState.detailIndex < n)
                OpenProperties(m_renderState.detailIndex);
            break;
        case VK_LEFT:
            if (m_renderState.detailIndex > 0) {
                --m_renderState.detailIndex;
                m_renderState.selectedIndex = m_renderState.detailIndex;
                m_renderState.detailScroll = 0.0f;
                RequestChangelogs(*m_visibleGames[m_renderState.detailIndex]);
            }
            break;
        case VK_RIGHT:
            if (m_renderState.detailIndex < n - 1) {
                ++m_renderState.detailIndex;
                m_renderState.selectedIndex = m_renderState.detailIndex;
                m_renderState.detailScroll = 0.0f;
                RequestChangelogs(*m_visibleGames[m_renderState.detailIndex]);
            }
            break;
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // ── Global shortcuts (work in any focus area) ─────────────────────────────
    if (vk == VK_F5) {
        std::thread([this]() { ScanAllPlatforms(); }).detach();
        return;
    }
    if (vk == VK_F2 || (ctrl && vk == VK_OEM_COMMA)) {
        OpenSettings();
        return;
    }
    if (vk == VK_F11) {
        if (!m_fullscreen) {
            GetWindowPlacement(m_hwnd, &m_savedPlacement);
            DWORD style = (DWORD)GetWindowLongW(m_hwnd, GWL_STYLE);
            SetWindowLongW(m_hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            MONITORINFO mi{ sizeof(mi) };
            GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
            auto& r = mi.rcMonitor;
            SetWindowPos(m_hwnd, HWND_TOP, r.left, r.top,
                         r.right - r.left, r.bottom - r.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_fullscreen = true;
        } else {
            DWORD style = (DWORD)GetWindowLongW(m_hwnd, GWL_STYLE);
            SetWindowLongW(m_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
            SetWindowPlacement(m_hwnd, &m_savedPlacement);
            SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_fullscreen = false;
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    // Ctrl+F or '/' → focus search
    if ((ctrl && vk == 'F') || vk == VK_OEM_2) {
        m_renderState.focusArea = FocusArea::Search;
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    // Number keys 1-7: quick platform filter
    if (vk >= '1' && vk <= '7' && m_renderState.focusArea != FocusArea::Search) {
        ApplySidebarFilter((int)(vk - '1'));
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // ── Tab: cycle focus areas ─────────────────────────────────────────────────
    if (vk == VK_TAB) {
        auto& fa = m_renderState.focusArea;
        if (shift) {
            if      (fa == FocusArea::Grid)    fa = FocusArea::Search;
            else if (fa == FocusArea::Search)  fa = FocusArea::Sidebar;
            else                               fa = FocusArea::Grid;
        } else {
            if      (fa == FocusArea::Grid)    fa = FocusArea::Sidebar;
            else if (fa == FocusArea::Sidebar) fa = FocusArea::Search;
            else                               fa = FocusArea::Grid;
        }
        // When entering sidebar, sync focus to active filter
        if (m_renderState.focusArea == FocusArea::Sidebar) {
            auto es = Renderer::BuildSidebarEntries(m_renderState);
            for (int i = 0; i < (int)es.size(); ++i) {
                bool match = es[i].page == m_renderState.libraryPage &&
                             (es[i].page != LibraryPage::Platform ||
                              es[i].p == m_renderState.filterPlatform);
                if (match) {
                    m_renderState.sidebarFocusIdx = i;
                    break;
                }
            }
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    // ── Focus-area-specific keys ───────────────────────────────────────────────
    switch (m_renderState.focusArea) {

    case FocusArea::Search:
        switch (vk) {
        case VK_ESCAPE:
            if (!m_renderState.searchQuery.empty()) {
                m_renderState.searchQuery.clear();
                ApplyFilter();
            } else {
                m_renderState.focusArea = FocusArea::Grid;
            }
            break;
        case VK_RETURN:
        case VK_DOWN:
            m_renderState.focusArea = FocusArea::Grid;
            if (!m_visibleGames.empty() && m_renderState.selectedIndex < 0)
                m_renderState.selectedIndex = 0;
            break;
        }
        break;

    case FocusArea::Sidebar: {
        int& si = m_renderState.sidebarFocusIdx;
        switch (vk) {
        case VK_UP:    si = std::max(0, si - 1);                                                     break;
        case VK_DOWN:  si = std::min(Renderer::GetSidebarEntryCount(m_renderState) - 1, si + 1);   break;
        case VK_HOME:  si = 0;                                                                       break;
        case VK_END:   si = Renderer::GetSidebarEntryCount(m_renderState) - 1;                     break;
        case VK_RETURN:
        case VK_SPACE:
            ApplySidebarFilter(si);
            m_renderState.focusArea = FocusArea::Grid;
            break;
        case VK_ESCAPE:
            m_renderState.focusArea = FocusArea::Grid;
            break;
        }
        break;
    }

    case FocusArea::Grid: {
        int n   = (int)m_visibleGames.size();
        int& sel = m_renderState.selectedIndex;
        int cols = m_renderer.GetCols();
        switch (vk) {
        case VK_ESCAPE:
            if (m_renderState.selectionMode) {
                m_renderState.selectionMode = false;
                m_renderState.selectedGameIds.clear();
            } else if (!m_renderState.searchQuery.empty()) {
                m_renderState.searchQuery.clear();
                ApplyFilter();
            } else if (m_fullscreen) {
                DWORD style = (DWORD)GetWindowLongW(m_hwnd, GWL_STYLE);
                SetWindowLongW(m_hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
                SetWindowPlacement(m_hwnd, &m_savedPlacement);
                SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                             SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
                m_fullscreen = false;
            }
            break;
        case VK_RETURN:
            if (m_renderState.selectionMode && sel >= 0 && sel < n) {
                const std::wstring& id = m_visibleGames[sel]->id;
                auto it = m_renderState.selectedGameIds.find(id);
                if (it != m_renderState.selectedGameIds.end())
                    m_renderState.selectedGameIds.erase(it);
                else
                    m_renderState.selectedGameIds.insert(id);
            } else if (sel >= 0 && sel < n) {
                OpenDetail(sel);
            } else if (n > 0) {
                sel = 0;
            }
            break;
        case VK_SPACE:
            if (m_renderState.selectionMode && sel >= 0 && sel < n) {
                const std::wstring& id = m_visibleGames[sel]->id;
                auto it = m_renderState.selectedGameIds.find(id);
                if (it != m_renderState.selectedGameIds.end())
                    m_renderState.selectedGameIds.erase(it);
                else
                    m_renderState.selectedGameIds.insert(id);
            }
            break;
        case VK_LEFT:
            if (n == 0) break;
            sel = (sel <= 0) ? 0 : sel - 1;
            ScrollToSelected();
            break;
        case VK_RIGHT:
            if (n == 0) break;
            sel = (sel < 0) ? 0 : std::min(sel + 1, n - 1);
            ScrollToSelected();
            break;
        case VK_UP:
            if (n == 0) break;
            if (sel < 0) sel = 0;
            else sel = std::max(0, sel - cols);
            ScrollToSelected();
            break;
        case VK_DOWN:
            if (n == 0) break;
            if (sel < 0) sel = 0;
            else sel = std::min(n - 1, sel + cols);
            ScrollToSelected();
            break;
        case VK_HOME:
            if (n > 0) { sel = 0; m_renderState.targetScroll = 0; }
            break;
        case VK_END:
            if (n > 0) { sel = n - 1; ScrollToSelected(); }
            break;
        case VK_PRIOR: { // Page Up
            RECT rc; GetClientRect(m_hwnd, &rc);
            m_renderState.targetScroll = std::max(0.0f,
                m_renderState.targetScroll - (float)(rc.bottom - rc.top - 80));
            break;
        }
        case VK_NEXT: { // Page Down
            RECT rc; GetClientRect(m_hwnd, &rc);
            float viewH = (float)(rc.bottom - rc.top);
            int rows = ((int)m_visibleGames.size() + cols - 1) / cols;
            float rowH = m_renderer.GridRowHeight();
            float maxScroll = std::max(0.0f, rows * rowH - viewH + 80.0f);
            m_renderState.targetScroll = std::min(
                m_renderState.targetScroll + viewH - 80.0f, maxScroll);
            break;
        }
        }
        break;
    }
    } // switch focusArea

    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::OnScroll(float delta) {
    if (m_renderState.detailOpen) {
        // Scroll the dashboard's right content pane (summary + changelogs). The
        // renderer clamps detailScroll to the real content height each draw.
        m_renderState.detailScroll =
            std::max(0.0f, m_renderState.detailScroll - delta * 0.5f);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    if (m_lastMouseX < m_renderer.SidebarWidth()) {
        float maxSidebar = m_renderer.MaxSidebarScroll(m_renderState);
        m_renderState.sidebarScroll =
            std::clamp(m_renderState.sidebarScroll - delta * 0.25f, 0.0f, maxSidebar);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    m_renderState.targetScroll -= delta * 0.5f;
    m_renderState.targetScroll = std::max(0.0f, m_renderState.targetScroll);
    // Max scroll: rough upper bound
    RECT rc; GetClientRect(m_hwnd, &rc);
    int cols = std::max(1, m_renderer.GetCols());
    int rows = ((int)m_visibleGames.size() + cols - 1) / cols;
    float maxScroll = std::max(0.0f, rows * m_renderer.GridRowHeight() - (float)(rc.bottom - rc.top) + 80.0f);
    m_renderState.targetScroll = std::min(m_renderState.targetScroll, maxScroll);
}

// ── Core logic ────────────────────────────────────────────────────────────────

void App::ShowMenuBar() {
    auto& emu = m_config.Get().emulators;

    HMENU hTools = CreatePopupMenu();
    bool anyTool = false;

    auto addTool = [&](UINT id, const wchar_t* label, const std::wstring& path) {
        UINT flags = MF_STRING | (path.empty() ? MF_GRAYED : 0);
        AppendMenuW(hTools, flags, id, label);
        anyTool = true;
    };

    addTool(IDM_TOOL_DOLPHIN, L"Launch Dolphin",  emu.dolphinPath);
    addTool(IDM_TOOL_RYUJINX, L"Launch Ryujinx",  emu.ryujinxPath);
    addTool(IDM_TOOL_RPCS3,   L"Launch RPCS3",    emu.rpcs3Path);
    addTool(IDM_TOOL_N64,     L"Launch N64 Emulator", emu.n64Path);
    addTool(IDM_TOOL_NES,     L"Launch NES Emulator",    emu.nesPath);
    addTool(IDM_TOOL_SNES,    L"Launch SNES Emulator",   emu.snesPath);
    addTool(IDM_TOOL_PS1,     L"Launch DuckStation",     emu.duckstationPath);
    addTool(IDM_TOOL_PS2,     L"Launch PCSX2",           emu.pcsx2Path);
    addTool(IDM_TOOL_XBOX360, L"Launch Xenia Canary",    emu.xeniaPath);
    addTool(IDM_TOOL_XBOX,    L"Launch XEMU",             emu.xemuPath);

    AppendMenuW(hTools, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hTools, MF_STRING, IDM_TOOL_LIBRARY, L"Library Folders\x2026");

    HMENU hMenuBar = CreatePopupMenu();
    AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hTools, L"Tools");

    POINT pt = { 0, 0 };
    ClientToScreen(m_hwnd, &pt);

    m_menuActive = true;
    int cmd = TrackPopupMenu(hMenuBar, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
                             pt.x, pt.y, 0, m_hwnd, nullptr);
    m_menuActive = false;
    DestroyMenu(hMenuBar); // also destroys hTools as a child

    auto launchStandalone = [this](const std::wstring& path, const std::wstring& args) {
        if (path.empty()) return;
        std::wstring workDir;
        size_t sep = path.rfind(L'\\');
        if (sep != std::wstring::npos) workDir = path.substr(0, sep);
        m_monitor.Launch(path, args, workDir, [](uint64_t) {});
    };

    switch (cmd) {
    case IDM_TOOL_DOLPHIN: launchStandalone(emu.dolphinPath, emu.dolphinArgs); break;
    case IDM_TOOL_RYUJINX: launchStandalone(emu.ryujinxPath, emu.ryujinxArgs); break;
    case IDM_TOOL_RPCS3:   launchStandalone(emu.rpcs3Path,   emu.rpcs3Args);   break;
    case IDM_TOOL_N64:     launchStandalone(emu.n64Path,     emu.n64Args);     break;
    case IDM_TOOL_NES:     launchStandalone(emu.nesPath,           emu.nesArgs);           break;
    case IDM_TOOL_SNES:    launchStandalone(emu.snesPath,          emu.snesArgs);          break;
    case IDM_TOOL_PS1:     launchStandalone(emu.duckstationPath,   emu.duckstationArgs);   break;
    case IDM_TOOL_PS2:     launchStandalone(emu.pcsx2Path,         emu.pcsx2Args);         break;
    case IDM_TOOL_XBOX360: launchStandalone(emu.xeniaPath,         emu.xeniaArgs);         break;
    case IDM_TOOL_XBOX:    launchStandalone(emu.xemuPath,          emu.xemuArgs);          break;
    case IDM_TOOL_LIBRARY:
        if (ShowLibraryFoldersDialog(m_hwnd, m_config.Get().server))
            SaveAll();
        break;
    }
}

void App::TriggerResync() {
    // Debounce: ignore triggers within 30s of the last one (focus-flicker guard)
    // and skip if a re-sync is already running.
    ULONGLONG now = GetTickCount64();
    if (m_lastResyncTick != 0 && now - m_lastResyncTick < 30000) return;
    bool expected = false;
    if (!m_resyncRunning.compare_exchange_strong(expected, true)) return;
    m_lastResyncTick = now;

    std::thread([this]() {
        ScanAllPlatforms();
        m_resyncRunning.store(false);
    }).detach();
}

static std::wstring RegStringHKLM(const wchar_t* subkey, const wchar_t* value) {
    wchar_t buf[256]{};
    DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, subkey, value, RRF_RT_REG_SZ,
                     nullptr, buf, &sz) == ERROR_SUCCESS)
        return buf;
    return L"";
}

// A stable identity for this install that changes on a major hardware or
// software change, so a cached server token is reused until then.
//  - MachineGuid  : regenerated on an OS reinstall (software).
//  - CurrentBuild : bumps on a major Windows feature update (software).
//  - C: volume serial : changes on a disk swap / fresh install (hardware).
//  - computer name + CPU arch/count : board/CPU swaps (hardware).
static std::wstring MachineFingerprint() {
    std::wstring guid  = RegStringHKLM(L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid");
    std::wstring build = RegStringHKLM(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuild");

    wchar_t name[256]{}; DWORD nameLen = _countof(name);
    if (!GetComputerNameW(name, &nameLen)) name[0] = 0;

    DWORD volSerial = 0;
    GetVolumeInformationW(L"C:\\", nullptr, 0, &volSerial, nullptr, nullptr, nullptr, 0);

    SYSTEM_INFO si{}; GetNativeSystemInfo(&si);

    return guid + L"|" + build + L"|" + name + L"|" +
        std::to_wstring(volSerial) + L"|" +
        std::to_wstring(si.wProcessorArchitecture) + L"|" +
        std::to_wstring(si.dwNumberOfProcessors);
}

bool App::EnsureServerToken(std::wstring& error) {
    std::lock_guard<std::mutex> lk(m_authMutex);
    auto& s = m_config.Get().server;
    if (!s.enabled) return true;
    if (s.baseUrl.empty() || s.username.empty() || s.password.empty()) {
        error = L"Server credentials are not configured.";
        return false;
    }

    std::wstring fp = MachineFingerprint();
    if (!s.authToken.empty() && s.tokenFingerprint == fp)
        return true; // reuse the cached, machine-bound token

    // No token, or the machine fingerprint changed: authenticate once and
    // persist the issued token. The server reuses one stable token per user,
    // so this credential is shared by every later client (re-sync + worker)
    // without re-logging in.
    ServerConfig probe = s;
    probe.authToken.clear();
    ServerClient client(probe);
    if (!client.Authenticate(error)) {
        if (error.empty()) error = L"Server authentication failed.";
        return false;
    }
    s.authToken = client.AuthToken();
    s.tokenFingerprint = fp;
    m_config.Save(GetAppDataPath() + L"\\config.json");
    return true;
}

void App::InvalidateServerToken() {
    std::lock_guard<std::mutex> lk(m_authMutex);
    auto& s = m_config.Get().server;
    if (s.authToken.empty()) return;
    s.authToken.clear();
    s.tokenFingerprint.clear();
    m_config.Save(GetAppDataPath() + L"\\config.json");
}

bool App::PromptReAuth() {
    auto& s = m_config.Get().server;
    if (!s.enabled) return false;

    // Drop the stale token up front so the dialog + EnsureServerToken below are
    // forced to re-authenticate rather than reuse the rejected credential.
    {
        std::lock_guard<std::mutex> lk(m_authMutex);
        s.authToken.clear();
        s.tokenFingerprint.clear();
    }

    // Reuse the startup sign-in modal; it edits the server config in place and
    // returns false if the user picked "Offline" / closed the dialog.
    if (!ShowServerLoginDialog(m_hwnd, s))
        return false;
    m_config.Save(GetAppDataPath() + L"\\config.json");

    std::wstring err;
    if (!EnsureServerToken(err)) {
        MessageBoxW(m_hwnd,
            (L"Sign-in failed:\n" + (err.empty() ? L"Unknown error" : err)).c_str(),
            L"ArcadeLauncher Server", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

void App::ScanAllPlatforms() {
    auto& lib = m_config.Get().libraries;
    auto& emu = m_config.Get().emulators;

    std::vector<std::unique_ptr<IScanner>> scanners;
    scanners.push_back(std::make_unique<SteamScanner>(lib.steamPath, lib.steamExtraFolders));
    scanners.push_back(std::make_unique<EpicScanner>(lib.epicManifestDirs));
    scanners.push_back(std::make_unique<GogScanner>());

    std::vector<Game> all;
    for (auto& s : scanners) {
        auto games = s->Scan();
        all.insert(all.end(), games.begin(), games.end());
    }

    if (m_config.Get().server.enabled) {
        std::wstring authErr;
        EnsureServerToken(authErr); // populate/refresh the shared token first
    }
    const auto serverCfg = m_config.Get().server;
    if (serverCfg.enabled && !serverCfg.baseUrl.empty()) {
        ServerClient client(serverCfg);
        std::vector<Game> serverGames;
        std::wstring error;
        if (client.FetchCatalog(serverGames, error)) {
            for (auto& g : serverGames) {
                switch (g.platform) {
                case Platform::Dolphin: g.emulatorPath = emu.dolphinPath; break;
                case Platform::GameCube: g.emulatorPath = emu.dolphinPath; break;
                case Platform::Wii: g.emulatorPath = emu.dolphinPath; break;
                case Platform::Ryujinx: g.emulatorPath = emu.ryujinxPath; break;
                case Platform::RPCS3: g.emulatorPath = emu.rpcs3Path; break;
                case Platform::N64: g.emulatorPath = emu.n64Path; break;
                case Platform::NES: g.emulatorPath = emu.nesPath; break;
                case Platform::SNES: g.emulatorPath = emu.snesPath; break;
                case Platform::PS1: g.emulatorPath = emu.duckstationPath; break;
                case Platform::PS2: g.emulatorPath = emu.pcsx2Path; break;
                case Platform::Xbox360: g.emulatorPath = emu.xeniaPath; break;
                case Platform::Xbox: g.emulatorPath = emu.xemuPath; break;
                default: break;
                }
                all.push_back(std::move(g));
            }
        }
    }

    auto* payload = new std::vector<Game>(std::move(all));
    if (!PostMessageW(m_hwnd, WM_USER + 1, 0, (LPARAM)payload))
        delete payload;
}

void App::LaunchGame(const Game& game) {
    if (m_monitor.IsRunning()) return;

    Game launchGame = game;
    if (launchGame.serverBacked) {
        bool ready = launchGame.installState == InstallState::Installed &&
            !launchGame.installRoot.empty() && PathFileExistsW(launchGame.installRoot.c_str());
        if (ready) {
            // Already installed: launch immediately, no UI-thread install work.
            LaunchInstalledGame(launchGame);
            return;
        }
        // Not installed: install on the background worker. Returns immediately so
        // the UI never freezes. The game is NOT auto-launched on completion — the
        // user starts it manually once the download finishes.
        QueueServerInstall(launchGame, /*autoLaunch=*/false);
        return;
    }

    LaunchInstalledGame(launchGame);
}

// Enqueue a server game for background install (optionally auto-launching when
// the install completes). Shared by the Play action and the Download button.
bool App::ChooseInstallRoot(const std::wstring& title, std::wstring& outRoot,
                            bool* outDesktopShortcut, bool* outStartMenuShortcut) {
    ServerConfig& server = m_config.Get().server;
    outRoot.clear();
    if (outDesktopShortcut)   *outDesktopShortcut = false;
    if (outStartMenuShortcut) *outStartMenuShortcut = false;
    if (server.libraryFolders.empty()) return true;  // server default installRoot
    if (server.alwaysAskInstallLocation) {
        if (!ShowInstallLocationDialog(m_hwnd, server, title, outRoot, false,
                                       outDesktopShortcut, outStartMenuShortcut))
            return false;
        SaveAll();  // dialog may have added a folder or changed the default
        return true;
    }
    int di = server.defaultLibraryIndex;
    if (di < 0 || di >= (int)server.libraryFolders.size()) di = 0;
    outRoot = server.libraryFolders[di];
    return true;
}

void App::QueueServerInstall(const Game& game, bool autoLaunch) {
    if (!game.serverBacked) return;

    // Steam-style install-location selection. Pick the target library folder
    // before queuing so the worker installs into the chosen drive.
    std::wstring chosenRoot;
    bool wantDesktop = false, wantStartMenu = false;
    if (!ChooseInstallRoot(game.title, chosenRoot, &wantDesktop, &wantStartMenu))
        return;  // cancelled

    // Make sure a valid shared token is in the config before the background
    // worker (which reads m_config) starts hitting the server.
    std::wstring authErr;
    EnsureServerToken(authErr);
    {
        std::lock_guard<std::mutex> lk(m_downloadMutex);
        auto exists = std::find_if(m_downloadQueue.begin(), m_downloadQueue.end(),
            [&](const DownloadJob& j) {
                return j.gameId == game.id && !j.complete && !j.failed;
            });
        if (exists != m_downloadQueue.end()) {
            if (autoLaunch) exists->autoLaunch = true;
            OpenDownloadStatus();
            StartDownloadWorker();
            return;
        }
        DownloadJob job{};
        job.game = game;
        job.gameId = game.id;
        job.title = game.title;
        job.autoLaunch = autoLaunch;
        job.installRoot = chosenRoot;
        job.makeDesktopShortcut = wantDesktop;
        job.makeStartMenuShortcut = wantStartMenu;
        m_downloadQueue.push_back(std::move(job));
    }
    if (auto* stored = m_library.FindById(game.id)) {
        stored->installState = InstallState::Downloading;
        stored->installProgressPermille = 0;
    }
    SaveAll();
    ApplyFilter();
    InvalidateRect(m_hwnd, nullptr, FALSE);
    OpenDownloadStatus();
    StartDownloadWorker();
}

// Combine a game's base arguments with its custom launch options (Steam-style).
// "%command%" in the options is replaced by the base args; otherwise the options
// are appended.
static std::wstring ApplyLaunchOptions(const std::wstring& baseArgs,
                                       const std::wstring& opts) {
    if (opts.empty()) return baseArgs;
    size_t p = opts.find(L"%command%");
    if (p != std::wstring::npos) {
        std::wstring r = opts;
        r.replace(p, wcslen(L"%command%"), baseArgs);
        return r;
    }
    return baseArgs.empty() ? opts : baseArgs + L" " + opts;
}

void App::LaunchInstalledGame(Game launchGame) {
    bool ok = false;

    if (!launchGame.launchUri.empty()) {
        // URI launch (Steam, Epic).
        // Use the game's own exe filename as a precise hint when we have it
        // (Epic sets exePath from the manifest). For Steam we leave the hint
        // empty so LaunchUri falls back to new-process detection — watching
        // steam.exe (always running) was wrong and never fired the callback.
        std::wstring hint;
        if (!launchGame.exePath.empty()) {
            auto sl = launchGame.exePath.rfind(L'\\');
            hint = (sl != std::wstring::npos) ? launchGame.exePath.substr(sl + 1) : launchGame.exePath;
        }
        ok = m_monitor.LaunchUri(launchGame.launchUri, hint, 60,
            [this, id = launchGame.id](uint64_t elapsed) {
                if (auto* g = m_library.FindById(id)) {
                    g->playtimeSeconds += elapsed;
                    g->lastPlayed = (int64_t)std::chrono::duration_cast<
                        std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                }
                PostMessageW(m_hwnd, WM_GAME_CLOSED, 0, 0);
            });
    } else if (!launchGame.emulatorPath.empty()) {
        // Emulator launch
        std::wstring args = launchGame.arguments;
        std::wstring cachedRomPath;
        std::wstring godCacheRoot;
        if (EnsureXbox360GodLocalCache(launchGame, cachedRomPath, godCacheRoot, m_hwnd))
            args = RebuildArgsForCachedRom(launchGame, cachedRomPath);
        // xemu (original Xbox) boots a disc image only via -dvd_path; a bare
        // positional path just opens the emulator UI. The server's {rom}
        // template (and catalog rows installed before this fix) yield only the
        // quoted ISO path, so normalise it here for existing installs too.
        if (launchGame.platform == Platform::Xbox &&
            !args.empty() &&
            args.find(L"-dvd_path") == std::wstring::npos) {
            args = L"-dvd_path " + args;
        }
        args = ApplyLaunchOptions(args, launchGame.launchOptions);

        ok = m_monitor.Launch(launchGame.emulatorPath, args, {},
            [this, id = launchGame.id, godCacheRoot](uint64_t elapsed) {
                CleanupXbox360GodLocalCache(godCacheRoot);
                if (auto* g = m_library.FindById(id)) {
                    g->playtimeSeconds += elapsed;
                    g->lastPlayed = (int64_t)std::chrono::duration_cast<
                        std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                }
                PostMessageW(m_hwnd, WM_GAME_CLOSED, 0, 0);
            });
    } else if (!launchGame.exePath.empty()) {
        // Direct exe
        std::wstring workDir;
        size_t sep = launchGame.exePath.rfind(L'\\');
        if (sep != std::wstring::npos) workDir = launchGame.exePath.substr(0, sep);
        std::wstring exeArgs = ApplyLaunchOptions(launchGame.arguments, launchGame.launchOptions);
        ok = m_monitor.Launch(launchGame.exePath, exeArgs, workDir,
            [this, id = launchGame.id](uint64_t elapsed) {
                if (auto* g = m_library.FindById(id)) {
                    g->playtimeSeconds += elapsed;
                    g->lastPlayed = (int64_t)std::chrono::duration_cast<
                        std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                }
                PostMessageW(m_hwnd, WM_GAME_CLOSED, 0, 0);
            });
    }

    if (ok && m_config.Get().minimizeOnLaunch)
        ShowWindow(m_hwnd, SW_HIDE);  // hide to tray when a game launches
}

void App::DownloadGameInBackground(int visibleIdx) {
    if (visibleIdx < 0 || visibleIdx >= (int)m_visibleGames.size()) return;
    Game game = *m_visibleGames[visibleIdx];
    if (!game.serverBacked || game.installState == InstallState::Installed ||
        game.installState == InstallState::Downloading) {
        return;
    }

    std::wstring chosenRoot;
    if (!ChooseInstallRoot(game.title, chosenRoot)) return;  // cancelled

    std::wstring authErr;
    EnsureServerToken(authErr); // shared token ready before the worker starts

    {
        std::lock_guard<std::mutex> lk(m_downloadMutex);
        auto exists = std::find_if(m_downloadQueue.begin(), m_downloadQueue.end(),
            [&](const DownloadJob& j) {
                return j.gameId == game.id && !j.complete && !j.failed;
            });
        if (exists != m_downloadQueue.end()) {
            OpenDownloadStatus();
            return;
        }
        DownloadJob job{};
        job.game = game;
        job.gameId = game.id;
        job.title = game.title;
        job.installRoot = chosenRoot;
        m_downloadQueue.push_back(std::move(job));
    }

    if (auto* stored = m_library.FindById(game.id)) {
        stored->installState = InstallState::Downloading;
        stored->installProgressPermille = 0;
        game = *stored;
    }
    SaveAll();
    ApplyFilter();
    InvalidateRect(m_hwnd, nullptr, FALSE);

    OpenDownloadStatus();
    StartDownloadWorker();
}

void App::StartDownloadWorker() {
    std::lock_guard<std::mutex> lk(m_downloadMutex);
    if (m_downloadWorkerRunning) return;
    m_downloadWorkerRunning = true;
    std::thread([this]() { DownloadWorker(); }).detach();
}

void App::DownloadWorker() {
    for (;;) {
        std::wstring gameId;
        {
            std::lock_guard<std::mutex> lk(m_downloadMutex);
            auto it = std::find_if(m_downloadQueue.begin(), m_downloadQueue.end(),
                [](const DownloadJob& j) { return !j.complete && !j.failed && !j.running; });
            if (it == m_downloadQueue.end()) {
                m_downloadWorkerRunning = false;
                return;
            }
            it->running = true;
            gameId = it->gameId;
        }

        Game game;
        std::wstring jobRoot;
        bool found = false;
        {
            std::lock_guard<std::mutex> lk(m_downloadMutex);
            for (const auto& job : m_downloadQueue) {
                if (job.gameId == gameId) {
                    game = job.game;
                    jobRoot = job.installRoot;
                    found = true;
                    break;
                }
            }
        }

        ServerInstallResult result;
        if (!found) {
            result.error = L"Queued game is no longer in the library.";
        } else {
            ServerConfig sc = m_config.Get().server;
            sc.dolphinPath = m_config.Get().emulators.dolphinPath;
            // Per-job library folder override (Steam-style install location).
            if (!jobRoot.empty()) sc.installRoot = jobRoot;
            ServerClient client(sc);
            HWND hwnd = m_hwnd;
            auto lastPost = std::make_shared<std::chrono::steady_clock::time_point>(
                std::chrono::steady_clock::now() - std::chrono::seconds(1));
            auto lastPermille = std::make_shared<int>(-1);
            result = client.InstallGame(game, [hwnd, id = game.id, lastPost, lastPermille](uint64_t done, uint64_t total) {
                int permille = total > 0
                    ? (int)std::min<uint64_t>(1000, done * 1000 / total)
                    : 0;
                auto now = std::chrono::steady_clock::now();
                bool meaningfulStep = permille >= 1000 || *lastPermille < 0 || permille >= *lastPermille + 5;
                bool enoughTime = now - *lastPost >= std::chrono::milliseconds(250);
                if (!meaningfulStep && !enoughTime)
                    return;
                *lastPost = now;
                *lastPermille = permille;
                auto* payload = new ServerInstallProgressPayload{ id, done, total };
                if (!PostMessageW(hwnd, WM_SERVER_INSTALL_PROGRESS, 0, (LPARAM)payload))
                    delete payload;
            });
        }

        auto* payload = new ServerInstallDonePayload{ gameId, std::move(result) };
        if (!PostMessageW(m_hwnd, WM_SERVER_INSTALL_DONE, 0, (LPARAM)payload))
            delete payload;
    }
}

void App::SampleDownloadSpeed() {
    uint64_t totalDone = 0;
    bool anyRunning = false;
    {
        std::lock_guard<std::mutex> lk(m_downloadMutex);
        for (const auto& job : m_downloadQueue) {
            if (job.running && !job.complete && !job.failed) {
                totalDone += job.done;
                anyRunning = true;
            }
        }
    }
    ULONGLONG now = GetTickCount64();
    if (m_speedLastTick != 0 && now > m_speedLastTick) {
        double secs = (now - m_speedLastTick) / 1000.0;
        // Treat a drop (one job finished, the next reset its counter) as zero
        // rather than a negative spike.
        double delta = totalDone >= m_speedLastBytes ? (double)(totalDone - m_speedLastBytes) : 0.0;
        double bps = anyRunning && secs > 0 ? delta / secs : 0.0;
        // Exponential moving average so the readout and graph glide instead of
        // jittering with per-tick network variance. Seed directly on the first
        // sample after idle (m_curSpeedBps == 0) so there's no slow ramp-up.
        constexpr double kSmoothing = 0.35;
        m_curSpeedBps = m_curSpeedBps > 0.0
            ? kSmoothing * bps + (1.0 - kSmoothing) * m_curSpeedBps
            : bps;
        if (m_curSpeedBps > m_peakSpeedBps) m_peakSpeedBps = m_curSpeedBps;
        m_speedHistory.push_back((float)m_curSpeedBps);
        while (m_speedHistory.size() > 120) m_speedHistory.pop_front();
    }
    m_speedLastBytes = totalDone;
    m_speedLastTick = now;
    if (!anyRunning) m_curSpeedBps = 0.0;
}

// Owner-drawn, Steam-style filled line graph of recent download throughput.
static LRESULT CALLBACK DownloadGraphWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) {
        App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        int w = rc.right, h = rc.bottom;

        // Double-buffer to avoid flicker on the timer-driven repaints.
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP old = (HBITMAP)SelectObject(mem, bmp);

        HBRUSH bg = CreateSolidBrush(RGB(18, 22, 28));
        FillRect(mem, &rc, bg);
        DeleteObject(bg);

        // Grid lines.
        HPEN grid = CreatePen(PS_SOLID, 1, RGB(40, 48, 58));
        HPEN oldPen = (HPEN)SelectObject(mem, grid);
        for (int i = 1; i < 4; ++i) {
            int y = h * i / 4;
            MoveToEx(mem, 0, y, nullptr);
            LineTo(mem, w, y);
        }
        SelectObject(mem, oldPen);
        DeleteObject(grid);

        if (app && app->SpeedHistorySize() >= 2) {
            std::vector<float> hist = app->SpeedHistoryCopy();
            float peak = 1.0f;
            for (float v : hist) peak = std::max(peak, v);
            peak *= 1.15f; // headroom

            int n = (int)hist.size();
            auto px = [&](int i) { return (n <= 1) ? 0 : (LONG)((LONGLONG)i * (w - 1) / (n - 1)); };
            auto py = [&](float v) { return (LONG)(h - 1 - (v / peak) * (h - 2)); };

            // Filled area under the curve.
            std::vector<POINT> poly;
            poly.reserve(n + 2);
            poly.push_back({ 0, h });
            for (int i = 0; i < n; ++i) poly.push_back({ px(i), py(hist[i]) });
            poly.push_back({ px(n - 1), h });
            HBRUSH fill = CreateSolidBrush(RGB(28, 64, 96));
            HPEN noPen = (HPEN)GetStockObject(NULL_PEN);
            HBRUSH oldB = (HBRUSH)SelectObject(mem, fill);
            HPEN oldP2 = (HPEN)SelectObject(mem, noPen);
            Polygon(mem, poly.data(), (int)poly.size());
            SelectObject(mem, oldB);
            SelectObject(mem, oldP2);
            DeleteObject(fill);

            // The line itself.
            HPEN line = CreatePen(PS_SOLID, 2, RGB(76, 194, 255));
            HPEN op = (HPEN)SelectObject(mem, line);
            MoveToEx(mem, px(0), py(hist[0]), nullptr);
            for (int i = 1; i < n; ++i) LineTo(mem, px(i), py(hist[i]));
            SelectObject(mem, op);
            DeleteObject(line);
        }

        BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1; // handled in WM_PAINT
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK DownloadStatusWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_TIMER && wp == TIMER_DLSPEED && app) {
        app->TickDownloadStatus();
        return 0;
    }
    if (msg == WM_CLOSE) {
        KillTimer(hwnd, TIMER_DLSPEED);
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    if (LRESULT r; dark::OnCtlColor(msg, wp, lp, r)) return r;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void App::OpenDownloadStatus() {
    if (!m_downloadStatusWnd) {
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = DownloadStatusWndProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = dark::BgBrush();
            wc.lpszClassName = DOWNLOAD_STATUS_WNDCLASS;
            RegisterClassExW(&wc);

            WNDCLASSEXW gc{};
            gc.cbSize = sizeof(gc);
            gc.lpfnWndProc = DownloadGraphWndProc;
            gc.hInstance = GetModuleHandleW(nullptr);
            gc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            gc.hbrBackground = nullptr;
            gc.lpszClassName = DOWNLOAD_GRAPH_WNDCLASS;
            RegisterClassExW(&gc);

            registered = true;
        }
        RECT rc{};
        GetWindowRect(m_hwnd, &rc);
        RECT dswr = { 0, 0, 480, 528 };
        AdjustWindowRectEx(&dswr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX, FALSE, WS_EX_TOOLWINDOW);
        int dsW = dswr.right - dswr.left;
        int dsH = dswr.bottom - dswr.top;
        m_downloadStatusWnd = CreateWindowExW(WS_EX_TOOLWINDOW, DOWNLOAD_STATUS_WNDCLASS,
            L"Downloads", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX,
            rc.right - dsW - 20, rc.top + 90, dsW, dsH, m_hwnd, nullptr,
            GetModuleHandleW(nullptr), nullptr);
        SetWindowLongPtrW(m_downloadStatusWnd, GWLP_USERDATA, (LONG_PTR)this);

        m_downloadSummary = CreateWindowExW(0, WC_STATICW, L"No active downloads",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            14, 14, 452, 20, m_downloadStatusWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        m_downloadProgress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
            WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
            14, 42, 452, 18, m_downloadStatusWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        m_downloadSpeed = CreateWindowExW(0, WC_STATICW, L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            14, 68, 452, 20, m_downloadStatusWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        m_downloadGraph = CreateWindowExW(WS_EX_CLIENTEDGE, DOWNLOAD_GRAPH_WNDCLASS, nullptr,
            WS_CHILD | WS_VISIBLE,
            14, 94, 452, 150, m_downloadStatusWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SetWindowLongPtrW(m_downloadGraph, GWLP_USERDATA, (LONG_PTR)this);
        m_downloadList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
            14, 256, 452, 250, m_downloadStatusWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(m_downloadProgress, PBM_SETRANGE32, 0, 1000);
        dark::EnableTitleBar(m_downloadStatusWnd);
        dark::Apply(m_downloadStatusWnd);
    }
    // Reset sampling so the graph starts clean each time it is opened.
    m_speedLastTick = 0;
    m_speedLastBytes = 0;
    RefreshDownloadStatusWindow();
    ShowWindow(m_downloadStatusWnd, SW_SHOWNORMAL);
    SetTimer(m_downloadStatusWnd, TIMER_DLSPEED, 500, nullptr);
    SetWindowPos(m_downloadStatusWnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void App::TickDownloadStatus() {
    SampleDownloadSpeed();
    RefreshDownloadStatusWindow();
    if (m_downloadGraph) InvalidateRect(m_downloadGraph, nullptr, FALSE);
}

size_t App::SpeedHistorySize() const { return m_speedHistory.size(); }

std::vector<float> App::SpeedHistoryCopy() const {
    return std::vector<float>(m_speedHistory.begin(), m_speedHistory.end());
}

void App::RefreshDownloadStatusWindow() {
    if (!m_downloadStatusWnd) return;
    std::deque<DownloadJob> copy;
    {
        std::lock_guard<std::mutex> lk(m_downloadMutex);
        copy = m_downloadQueue;
    }

    SendMessageW(m_downloadList, LB_RESETCONTENT, 0, 0);
    uint64_t activeDone = 0, activeTotal = 0;
    std::wstring summary = L"No active downloads";
    for (const auto& job : copy) {
        std::wstring state = job.complete ? L"Done" :
            job.failed ? L"Failed" :
            job.running ? L"Downloading" : L"Queued";
        std::wstring line = state + L" - " + job.title;
        if (job.running && job.total > 0) {
            int pct = (int)std::min<uint64_t>(100, job.done * 100 / job.total);
            line += L" (" + std::to_wstring(pct) + L"%)";
            activeDone = job.done;
            activeTotal = job.total;
            summary = line;
        } else if (job.failed && !job.error.empty()) {
            line += L" - " + job.error;
        }
        SendMessageW(m_downloadList, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
    int pos = activeTotal > 0 ? (int)std::min<uint64_t>(1000, activeDone * 1000 / activeTotal) : 0;
    SendMessageW(m_downloadProgress, PBM_SETPOS, pos, 0);
    SetWindowTextW(m_downloadSummary, summary.c_str());

    if (m_downloadSpeed) {
        // Download and disk-write throughput are the same here: the client
        // streams each received byte straight to disk (write-through install).
        std::wstring speed = activeTotal > 0
            ? L"Download " + FormatSpeed(m_curSpeedBps) +
              L"   \x2022   Disk write " + FormatSpeed(m_curSpeedBps) +
              L"   \x2022   Peak " + FormatSpeed(m_peakSpeedBps)
            : L"Idle";
        SetWindowTextW(m_downloadSpeed, speed.c_str());
    }
}

void App::OpenDetail(int visibleIdx) {
    if (visibleIdx < 0 || visibleIdx >= (int)m_visibleGames.size()) return;
    m_renderState.detailOpen  = true;
    m_renderState.detailIndex = visibleIdx;
    m_renderState.detailScroll = 0.0f;
    RequestChangelogs(*m_visibleGames[visibleIdx]);
}

void App::RequestChangelogs(const Game& game) {
    // Reset to a clean slate for this game; a stale fetch for a different game
    // is dropped in the WM_CHANGELOGS_READY handler via the id guard.
    m_renderState.detailChangelogs.clear();
    m_renderState.detailChangelogGameId = game.id;
    m_renderState.detailChangelogsLoading = false;

    if (!game.serverBacked || game.serverGameId.empty() ||
        !m_config.Get().server.enabled)
        return;

    m_renderState.detailChangelogsLoading = true;
    ServerConfig sc = m_config.Get().server;
    std::wstring serverId = game.serverGameId;
    std::wstring localId  = game.id;
    HWND hwnd = m_hwnd;
    std::thread([sc, serverId, localId, hwnd]() {
        ServerClient client(sc);
        auto* payload = new ChangelogPayload();
        payload->gameId = localId;
        std::wstring err;
        client.FetchChangelogs(serverId, payload->entries, err);  // best-effort
        if (!PostMessageW(hwnd, App::WM_CHANGELOGS_READY, 0, (LPARAM)payload))
            delete payload;
    }).detach();
}

void App::ApplyFilter() {
    if (!m_renderState.searchQuery.empty()) {
        auto res = m_library.Search(m_renderState.searchQuery);
        m_visibleGames = res;
    } else if (m_renderState.libraryPage == LibraryPage::All || m_renderState.filterAll) {
        m_visibleGames.clear();
        for (auto& g : m_library.All()) m_visibleGames.push_back(&g);
    } else if (m_renderState.libraryPage == LibraryPage::Installed) {
        m_visibleGames.clear();
        for (auto& g : m_library.All()) {
            if (!g.serverBacked || g.installState == InstallState::Installed)
                m_visibleGames.push_back(&g);
        }
    } else if (m_renderState.libraryPage == LibraryPage::ReadyToDownload) {
        m_visibleGames.clear();
        for (auto& g : m_library.All()) {
            if (g.serverBacked && g.installState == InstallState::Missing)
                m_visibleGames.push_back(&g);
        }
    } else if (m_renderState.libraryPage == LibraryPage::BackgroundDownloads) {
        m_visibleGames.clear();
        std::vector<std::wstring> queueIds;
        {
            std::lock_guard<std::mutex> lk(m_downloadMutex);
            for (const auto& job : m_downloadQueue)
                queueIds.push_back(job.gameId);
        }
        for (const auto& id : queueIds) {
            if (auto* g = m_library.FindById(id))
                m_visibleGames.push_back(g);
        }
        for (auto& g : m_library.All()) {
            if (g.installState == InstallState::Downloading &&
                std::find(queueIds.begin(), queueIds.end(), g.id) == queueIds.end()) {
                m_visibleGames.push_back(&g);
            }
        }
    } else if (m_renderState.libraryPage == LibraryPage::Updates) {
        m_visibleGames.clear();
        for (auto& g : m_library.All()) {
            if (g.serverBacked && g.installState == InstallState::UpdateAvailable)
                m_visibleGames.push_back(&g);
        }
    } else if (m_renderState.libraryPage == LibraryPage::Collection) {
        m_visibleGames.clear();
        const std::wstring& name = m_renderState.filterCollection;
        for (auto& g : m_library.All()) {
            if (std::find(g.collections.begin(), g.collections.end(), name)
                    != g.collections.end())
                m_visibleGames.push_back(&g);
        }
    } else {
        m_visibleGames = m_library.Filter(m_renderState.filterPlatform);
    }

    // Apply the active sort mode. The (now hidden) download page keeps queue order.
    if (m_renderState.libraryPage != LibraryPage::BackgroundDownloads) {
        auto byTitle = [](const Game* a, const Game* b) {
            if (a->title != b->title) return a->title < b->title;
            return a->id < b->id;
        };
        switch (m_renderState.sortMode) {
        case SortMode::Title:
            std::sort(m_visibleGames.begin(), m_visibleGames.end(), byTitle);
            break;
        case SortMode::Platform:
            std::sort(m_visibleGames.begin(), m_visibleGames.end(),
                [&](const Game* a, const Game* b) {
                    if (a->platform != b->platform)
                        return PlatformName(a->platform) < PlatformName(b->platform);
                    return byTitle(a, b);
                });
            break;
        case SortMode::Rating:
            std::sort(m_visibleGames.begin(), m_visibleGames.end(),
                [&](const Game* a, const Game* b) {
                    if (a->igdbRating != b->igdbRating)
                        return a->igdbRating > b->igdbRating;
                    return byTitle(a, b);
                });
            break;
        case SortMode::Playtime:
            std::sort(m_visibleGames.begin(), m_visibleGames.end(),
                [&](const Game* a, const Game* b) {
                    if (a->playtimeSeconds != b->playtimeSeconds)
                        return a->playtimeSeconds > b->playtimeSeconds;
                    return byTitle(a, b);
                });
            break;
        case SortMode::Recent:
        default:
            std::sort(m_visibleGames.begin(), m_visibleGames.end(),
                [&](const Game* a, const Game* b) {
                    if (a->lastPlayed != b->lastPlayed) return a->lastPlayed > b->lastPlayed;
                    return byTitle(a, b);
                });
            break;
        }
    }

    m_renderState.selectedIndex = -1;
    m_renderState.scrollOffset  = 0;
    m_renderState.targetScroll  = 0;

    if (!m_renderState.selectedGameIds.empty()) {
        std::unordered_set<std::wstring> visibleIds;
        for (auto* g : m_visibleGames)
            visibleIds.insert(g->id);
        for (auto it = m_renderState.selectedGameIds.begin();
             it != m_renderState.selectedGameIds.end(); ) {
            if (visibleIds.count(*it) == 0)
                it = m_renderState.selectedGameIds.erase(it);
            else
                ++it;
        }
    }
}

void App::UpdateSidebarFlags() {
    auto& lib = m_config.Get().libraries;
    auto& emu = m_config.Get().emulators;
    m_renderState.showSteam   = true;
    m_renderState.showEpic    = true;
    m_renderState.showGog     = true;
    m_renderState.showDolphin = true;
    m_renderState.showGameCube = true;
    m_renderState.showWii     = true;
    m_renderState.showRyujinx = true;
    m_renderState.showRPCS3   = true;
    m_renderState.showN64     = true;
    m_renderState.showNES     = true;
    m_renderState.showSNES    = true;
    m_renderState.showPS1     = true;
    m_renderState.showPS2     = true;
    m_renderState.showXbox360 = true;
    m_renderState.showXbox    = true;
    m_renderState.showRepacks = true;
    m_renderState.collections = m_config.Get().collections;
    int count = Renderer::GetSidebarEntryCount(m_renderState);
    if (m_renderState.sidebarFocusIdx >= count)
        m_renderState.sidebarFocusIdx = count - 1;
    m_renderState.sidebarScroll =
        std::min(m_renderState.sidebarScroll, m_renderer.MaxSidebarScroll(m_renderState));
}

void App::ApplySidebarFilter(int idx) {
    auto entries = Renderer::BuildSidebarEntries(m_renderState);
    if (idx < 0 || idx >= (int)entries.size()) return;
    m_renderState.filterAll        = entries[idx].all;
    m_renderState.filterPlatform   = entries[idx].p;
    m_renderState.libraryPage      = entries[idx].page;
    m_renderState.filterCollection = entries[idx].collection;
    ApplyFilter();
}

void App::ScrollToSelected() {
    if (m_renderState.selectedIndex < 0 || m_visibleGames.empty()) return;
    RECT rc; GetClientRect(m_hwnd, &rc);
    m_renderState.targetScroll = m_renderer.ScrollForSelected(
        m_renderState.selectedIndex,
        m_renderState.targetScroll,
        (float)(rc.bottom - rc.top));
}

void App::OnRButtonDown(float x, float y) {
    if (m_renderState.detailOpen) return;

    int idx = m_renderer.HitTestGrid(x, y, m_renderState, m_visibleGames.size());
    if (idx < 0) return;

    // Select the right-clicked game so it's visually highlighted
    m_renderState.selectedIndex = idx;
    InvalidateRect(m_hwnd, nullptr, FALSE);

    const Game* hoveredGame = m_visibleGames[idx];
    bool canValidate = hoveredGame->serverBacked &&
        hoveredGame->installState == InstallState::Installed &&
        !hoveredGame->installRoot.empty();
    bool canUninstall = canValidate;
    bool canDownload = hoveredGame->serverBacked &&
        (hoveredGame->installState == InstallState::Missing ||
         hoveredGame->installState == InstallState::UpdateAvailable);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_LAUNCH, L"Launch");
    if (canDownload)
        AppendMenuW(menu, MF_STRING, IDM_DOWNLOAD_GAME, L"Download in Background");
    AppendMenuW(menu, MF_STRING, IDM_DOWNLOAD_STATUS, L"Download Queue...");
    if (canValidate)
        AppendMenuW(menu, MF_STRING, IDM_VALIDATE_GAME, L"Validate && Repair Installed Files");
    if (canUninstall)
        AppendMenuW(menu, MF_STRING, IDM_UNINSTALL_GAME, L"Uninstall Local Files");

    // Move an installed game between library folders (needs >1 destination).
    bool canMove = hoveredGame->serverBacked &&
        hoveredGame->installState == InstallState::Installed &&
        !hoveredGame->installRoot.empty() &&
        m_config.Get().server.libraryFolders.size() > 1;
    if (canMove)
        AppendMenuW(menu, MF_STRING, IDM_MOVE_GAME, L"Move Install Folder…");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_LAUNCH_OPTIONS, L"Set Launch Options…");

    // Collections submenu: toggle membership, or create a new collection.
    HMENU colMenu = CreatePopupMenu();
    AppendMenuW(colMenu, MF_STRING, IDM_COLLECTION_NEW, L"New Collection…");
    const auto& collections = m_config.Get().collections;
    if (!collections.empty()) AppendMenuW(colMenu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < collections.size(); ++i) {
        bool inIt = std::find(hoveredGame->collections.begin(),
                              hoveredGame->collections.end(),
                              collections[i]) != hoveredGame->collections.end();
        AppendMenuW(colMenu, MF_STRING | (inIt ? MF_CHECKED : 0),
                    IDM_COLLECTION_BASE + (UINT)i, collections[i].c_str());
    }
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)colMenu, L"Collections");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EDIT_TITLE, L"Edit Title…");
    UINT matchFlags = MF_STRING | (m_metaManager ? 0 : MF_GRAYED);
    AppendMenuW(menu, matchFlags, IDM_MATCH_META, L"Match Metadata…");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_PROPERTIES, L"Properties…");

    if (m_monitor.IsRunning())
        EnableMenuItem(menu, IDM_LAUNCH, MF_BYCOMMAND | MF_GRAYED);

    POINT pt = { (LONG)x, (LONG)y };
    ClientToScreen(m_hwnd, &pt);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
                              pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd == IDM_LAUNCH) {
        if (idx < (int)m_visibleGames.size())
            LaunchGame(*m_visibleGames[idx]);
    } else if (cmd == IDM_DOWNLOAD_GAME) {
        DownloadGameInBackground(idx);
    } else if (cmd == IDM_DOWNLOAD_STATUS) {
        OpenDownloadStatus();
    } else if (cmd == IDM_VALIDATE_GAME) {
        ValidateGame(idx);
    } else if (cmd == IDM_UNINSTALL_GAME) {
        UninstallGame(idx);
    } else if (cmd == IDM_EDIT_TITLE) {
        OpenEditTitle(idx);
    } else if (cmd == IDM_MATCH_META) {
        if (idx < (int)m_visibleGames.size())
            OpenMetadataPicker(m_visibleGames[idx]->id, m_visibleGames[idx]->title);
    } else if (cmd == IDM_MOVE_GAME) {
        MoveGame(idx);
    } else if (cmd == IDM_LAUNCH_OPTIONS) {
        SetLaunchOptions(idx);
    } else if (cmd == IDM_PROPERTIES) {
        OpenProperties(idx);
    } else if (cmd == IDM_COLLECTION_NEW) {
        NewCollectionForGame(idx);
    } else if (cmd >= (int)IDM_COLLECTION_BASE &&
               cmd < (int)IDM_COLLECTION_BASE + (int)m_config.Get().collections.size()) {
        ToggleGameCollection(idx, cmd - IDM_COLLECTION_BASE);
    }
}

void App::UninstallGame(int visibleIdx) {
    if (visibleIdx < 0 || visibleIdx >= (int)m_visibleGames.size()) return;
    const Game game = *m_visibleGames[visibleIdx];
    if (!game.serverBacked || game.installState != InstallState::Installed) return;

    std::wstring prompt = L"Remove the locally installed files for " + game.title + L"?\n\n"
        L"The game will stay in your server library and can be downloaded again.";
    if (MessageBoxW(m_hwnd, prompt.c_str(), L"Uninstall Game",
        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    std::wstring error;
    EnsureServerToken(error);
    ServerClient client(m_config.Get().server);
    if (!client.UninstallGame(game, error)) {
        MessageBoxW(m_hwnd, error.c_str(), L"Uninstall Failed", MB_OK | MB_ICONERROR);
        return;
    }

    if (auto* stored = m_library.FindById(game.id)) {
        stored->installState = InstallState::Missing;
        stored->installRoot.clear();
        stored->romPath.clear();
        stored->exePath.clear();
    }
    SaveAll();
    ApplyFilter();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::MoveGame(int visibleIdx) {
    if (visibleIdx < 0 || visibleIdx >= (int)m_visibleGames.size()) return;
    const Game game = *m_visibleGames[visibleIdx];
    if (!game.serverBacked || game.installState != InstallState::Installed ||
        game.installRoot.empty())
        return;

    ServerConfig& server = m_config.Get().server;
    std::wstring newLibRoot;
    if (!ShowInstallLocationDialog(m_hwnd, server, game.title, newLibRoot, true))
        return;
    SaveAll();  // dialog may have added a folder
    if (newLibRoot.empty()) return;

    // New per-game folder = newLibRoot \ <basename of current install folder>.
    std::wstring oldRoot = game.installRoot;
    std::wstring trimmed = oldRoot;
    while (!trimmed.empty() && (trimmed.back() == L'\\' || trimmed.back() == L'/'))
        trimmed.pop_back();
    size_t sl = trimmed.find_last_of(L"\\/");
    std::wstring leaf = (sl == std::wstring::npos) ? trimmed : trimmed.substr(sl + 1);
    std::wstring newRoot = newLibRoot;
    if (!newRoot.empty() && newRoot.back() != L'\\') newRoot += L'\\';
    newRoot += leaf;

    if (_wcsicmp(newRoot.c_str(), oldRoot.c_str()) == 0) return;  // already there
    if (fs::exists(newRoot)) {
        MessageBoxW(m_hwnd,
            L"The destination library already contains a folder for this game.",
            L"Move Failed", MB_OK | MB_ICONERROR);
        return;
    }

    CopyProgressDialog dialog;
    dialog.Create(m_hwnd, L"Moving " + game.title, L"Moving files");
    auto startedAt = std::chrono::steady_clock::now();
    std::atomic<uint64_t> copied{0}, total{0};
    std::atomic<bool> finished{false}, success{false};
    std::wstring moveErr;
    std::thread worker([&]() {
        try {
            // Same-volume rename is instant; cross-volume needs copy + delete.
            std::error_code ec;
            fs::rename(oldRoot, newRoot, ec);
            if (!ec) { success.store(true); finished.store(true); return; }
            for (auto& e : fs::recursive_directory_iterator(oldRoot))
                if (e.is_regular_file()) total += e.file_size();
            fs::create_directories(newRoot);
            for (auto& e : fs::recursive_directory_iterator(oldRoot)) {
                std::wstring rel = fs::relative(e.path(), oldRoot).wstring();
                fs::path dst = fs::path(newRoot) / rel;
                if (e.is_directory()) {
                    fs::create_directories(dst);
                } else if (e.is_regular_file()) {
                    fs::create_directories(dst.parent_path());
                    fs::copy_file(e.path(), dst, fs::copy_options::overwrite_existing);
                    copied += e.file_size();
                }
            }
            fs::remove_all(oldRoot);
            success.store(true);
        } catch (const std::exception& ex) {
            moveErr = ToWide(ex.what());
        }
        finished.store(true);
    });
    while (!finished.load()) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        uint64_t c = copied.load(), t = total.load();
        double secs = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - startedAt).count();
        double mbps = secs > 0.0 ? ((double)c / (1024.0 * 1024.0)) / secs : 0.0;
        dialog.SetProgress(c, t, mbps);
        Sleep(50);
    }
    worker.join();
    dialog.Close();

    if (!success.load()) {
        MessageBoxW(m_hwnd,
            moveErr.empty() ? L"Could not move the game files." : moveErr.c_str(),
            L"Move Failed", MB_OK | MB_ICONERROR);
        return;
    }

    // Re-point stored paths from the old install root to the new one.
    if (auto* stored = m_library.FindById(game.id)) {
        auto repath = [&](std::wstring& p) {
            if (p.size() >= oldRoot.size() &&
                _wcsnicmp(p.c_str(), oldRoot.c_str(), oldRoot.size()) == 0)
                p = newRoot + p.substr(oldRoot.size());
        };
        stored->installRoot = newRoot;
        repath(stored->romPath);
        repath(stored->exePath);
    }
    SaveAll();
    ApplyFilter();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::SetLaunchOptions(int visibleIdx) {
    if (visibleIdx < 0 || visibleIdx >= (int)m_visibleGames.size()) return;
    const Game game = *m_visibleGames[visibleIdx];
    std::wstring opts = game.launchOptions;
    std::wstring label =
        L"Custom launch options for " + game.title + L".\n"
        L"Passed to the game when it launches. Use %command% to reference the "
        L"default command line.";
    if (!ShowTextPrompt(m_hwnd, L"Launch Options", label, opts)) return;
    if (auto* stored = m_library.FindById(game.id))
        stored->launchOptions = opts;
    SaveAll();
}

void App::NewCollectionForGame(int visibleIdx) {
    if (visibleIdx < 0 || visibleIdx >= (int)m_visibleGames.size()) return;
    const std::wstring gameId = m_visibleGames[visibleIdx]->id;
    std::wstring name;
    if (!ShowTextPrompt(m_hwnd, L"New Collection", L"Collection name:", name)) return;
    while (!name.empty() && iswspace(name.front()))  name.erase(name.begin());
    while (!name.empty() && iswspace(name.back()))   name.pop_back();
    if (name.empty()) return;

    auto& cols = m_config.Get().collections;
    if (std::find(cols.begin(), cols.end(), name) == cols.end())
        cols.push_back(name);
    if (auto* stored = m_library.FindById(gameId)) {
        if (std::find(stored->collections.begin(), stored->collections.end(), name)
                == stored->collections.end())
            stored->collections.push_back(name);
    }
    SaveAll();
    UpdateSidebarFlags();
    ApplyFilter();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::ToggleGameCollection(int visibleIdx, int collectionIdx) {
    if (visibleIdx < 0 || visibleIdx >= (int)m_visibleGames.size()) return;
    auto& cols = m_config.Get().collections;
    if (collectionIdx < 0 || collectionIdx >= (int)cols.size()) return;
    std::wstring name = cols[collectionIdx];
    if (auto* stored = m_library.FindById(m_visibleGames[visibleIdx]->id)) {
        auto it = std::find(stored->collections.begin(), stored->collections.end(), name);
        if (it == stored->collections.end()) stored->collections.push_back(name);
        else                                 stored->collections.erase(it);
    }
    SaveAll();
    UpdateSidebarFlags();
    ApplyFilter();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::ValidateGame(int visibleIdx) {
    if (visibleIdx < 0 || visibleIdx >= (int)m_visibleGames.size()) return;
    const Game game = *m_visibleGames[visibleIdx];
    if (!game.serverBacked || game.installState != InstallState::Installed) return;

    CopyProgressDialog dialog;
    dialog.Create(m_hwnd, L"Validating " + game.title, L"Validating installed files");
    auto startedAt = std::chrono::steady_clock::now();

    std::wstring authErr;
    EnsureServerToken(authErr);

    // Hashing is CPU/IO heavy and the progress callback only fires once *per
    // file*, so verifying a single large file (Xbox 360 / PC repack) on the UI
    // thread froze the window solid ("not responding"). Run the hash loop on a
    // worker thread and keep the UI thread pumping messages + refreshing the
    // dialog from shared progress until it finishes.
    ServerConfig serverCfg = m_config.Get().server;
    std::atomic<uint64_t> progChecked{0};
    std::atomic<uint64_t> progTotal{0};
    std::atomic<bool> finished{false};
    ServerValidateResult result;
    std::thread worker([&]() {
        ServerClient client(serverCfg);
        result = client.ValidateGame(game, [&](uint64_t checked, uint64_t total) {
            progTotal.store(total, std::memory_order_relaxed);
            progChecked.store(checked, std::memory_order_relaxed);
        });
        finished.store(true, std::memory_order_release);
    });

    while (!finished.load(std::memory_order_acquire)) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        uint64_t checked = progChecked.load(std::memory_order_relaxed);
        uint64_t total = progTotal.load(std::memory_order_relaxed);
        double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - startedAt).count();
        double mbps = seconds > 0.0
            ? ((double)checked / (1024.0 * 1024.0)) / seconds
            : 0.0;
        dialog.SetProgress(checked, total, mbps);
        Sleep(50);
    }
    worker.join();
    dialog.Close();

    if (result.ok) {
        MessageBoxW(m_hwnd,
            (L"All installed files validated successfully.\n\nFiles checked: " +
             std::to_wstring(result.checkedFiles)).c_str(),
            L"Validation Complete", MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring details = result.error.empty() ? L"Validation failed." : result.error;
    auto appendList = [&](const wchar_t* title, const std::vector<std::wstring>& files) {
        if (files.empty()) return;
        details += L"\n\n";
        details += title;
        details += L":";
        size_t limit = std::min<size_t>(files.size(), 8);
        for (size_t i = 0; i < limit; ++i)
            details += L"\n- " + files[i];
        if (files.size() > limit)
            details += L"\n- ... " + std::to_wstring(files.size() - limit) + L" more";
    };
    appendList(L"Missing", result.missingFiles);
    appendList(L"Corrupt or changed", result.badFiles);

    size_t badCount = result.missingFiles.size() + result.badFiles.size();
    if (badCount == 0) {
        // Validation failed without identifiable files (e.g. manifest fetch
        // error) — nothing to repair, just report.
        MessageBoxW(m_hwnd, details.c_str(), L"Validation Failed", MB_OK | MB_ICONWARNING);
        return;
    }

    details += L"\n\nRe-download and repair these " + std::to_wstring(badCount) +
               L" file(s)?\n(Only the missing/corrupt files are fetched; valid files are kept.)";
    if (MessageBoxW(m_hwnd, details.c_str(), L"Validation Failed",
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON1) != IDYES) {
        return;
    }

    // InstallGame re-fetches only files that fail size+hash verification, so
    // queuing a (re)install acts as an efficient repair. QueueServerInstall is
    // not gated on installState==Installed (unlike DownloadGameInBackground),
    // so this works for an installed-but-corrupt game.
    QueueServerInstall(game, /*autoLaunch=*/false);
}

void App::OpenEditTitle(int visibleIdx) {
    if (visibleIdx < 0 || visibleIdx >= (int)m_visibleGames.size()) return;

    // FindById so we hold a stable pointer into the library (not the visible list)
    const Game* cv = m_visibleGames[visibleIdx];
    Game* g = m_library.FindById(cv->id);
    if (!g) return;

    bool isEmulated = (g->platform == Platform::Dolphin ||
                       g->platform == Platform::GameCube ||
                       g->platform == Platform::Wii     ||
                       g->platform == Platform::Ryujinx ||
                       g->platform == Platform::RPCS3   ||
                       g->platform == Platform::N64     ||
                       g->platform == Platform::NES     ||
                       g->platform == Platform::SNES    ||
                       g->platform == Platform::PS1     ||
                       g->platform == Platform::PS2     ||
                       g->platform == Platform::Xbox360 ||
                       g->platform == Platform::Xbox);

    GameEditDialog dlg;
    dlg.Show(m_hwnd, g->title, isEmulated, g->igdbPlatformId);
    if (!dlg.confirmed) return;

    // Update title and platform override
    g->title          = dlg.newTitle;
    g->igdbPlatformId = dlg.selectedIgdbPlatformId;

    // Rename ROM file on disk if requested
    if (dlg.renameFile && !g->romPath.empty()) {
        size_t sep = g->romPath.rfind(L'\\');
        size_t dot = g->romPath.rfind(L'.');
        std::wstring dir = (sep != std::wstring::npos) ?
                            g->romPath.substr(0, sep + 1) : L"";
        std::wstring ext = (dot != std::wstring::npos && dot > sep) ?
                            g->romPath.substr(dot) : L"";
        std::wstring newPath = dir + dlg.newTitle + ext;

        if (MoveFileW(g->romPath.c_str(), newPath.c_str())) {
            // Patch the quoted rom path in the launch arguments
            auto replaceInArgs = [](std::wstring& args,
                                    const std::wstring& oldP,
                                    const std::wstring& newP) {
                std::wstring oldQ = L"\"" + oldP + L"\"";
                std::wstring newQ = L"\"" + newP + L"\"";
                size_t pos = args.find(oldQ);
                if (pos != std::wstring::npos)
                    args.replace(pos, oldQ.size(), newQ);
            };
            replaceInArgs(g->arguments, g->romPath, newPath);
            g->romPath = newPath;
        } else {
            MessageBoxW(m_hwnd,
                (L"Could not rename file:\n" + g->romPath).c_str(),
                L"Rename Failed", MB_OK | MB_ICONWARNING);
        }
    }

    // Clear IGDB match so the new title triggers a fresh metadata search
    g->igdbMatched = false;
    if (m_metaManager) {
        m_metaManager->ScanGameAsync(g->id,
            [this](const std::wstring& id, bool /*matched*/) {
                PostMessageW(m_hwnd, WM_USER + 4, 0, (LPARAM)new std::wstring(id));
            });
    }

    ApplyFilter();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::OpenProperties(int visibleIdx) {
    if (visibleIdx < 0 || visibleIdx >= (int)m_visibleGames.size()) return;

    const Game* cv = m_visibleGames[visibleIdx];
    Game* g = m_library.FindById(cv->id);
    if (!g) return;

    bool isEmulated = (g->platform == Platform::Dolphin ||
                       g->platform == Platform::GameCube ||
                       g->platform == Platform::Wii     ||
                       g->platform == Platform::Ryujinx ||
                       g->platform == Platform::RPCS3   ||
                       g->platform == Platform::N64     ||
                       g->platform == Platform::NES     ||
                       g->platform == Platform::SNES    ||
                       g->platform == Platform::PS1     ||
                       g->platform == Platform::PS2     ||
                       g->platform == Platform::Xbox360 ||
                       g->platform == Platform::Xbox);

    // Read-only info block shown at the top of the modal.
    std::wstring info = L"Platform:  " + PlatformName(g->platform) + L"\r\n";
    if (g->serverBacked) {
        info += L"Source:  Server library\r\n";
        if (!g->serverVersion.empty())
            info += L"Version:  " + g->serverVersion + L"\r\n";
        const wchar_t* stStr =
            g->installState == InstallState::Installed       ? L"Installed" :
            g->installState == InstallState::UpdateAvailable ? L"Update available" :
            g->installState == InstallState::Downloading     ? L"Downloading" :
                                                               L"Not installed";
        info += std::wstring(L"Status:  ") + stStr + L"\r\n";
        info += L"Install:  " +
                (g->installRoot.empty() ? std::wstring(L"(not installed)") : g->installRoot);
    } else {
        info += L"Source:  Local\r\n";
        if (!g->romPath.empty())      info += L"ROM:  " + g->romPath;
        else if (!g->exePath.empty()) info += L"Exe:  " + g->exePath;
        else if (!g->launchUri.empty()) info += L"Launch:  " + g->launchUri;
    }

    GameEditDialog dlg;
    dlg.ShowProperties(m_hwnd, g->title, isEmulated, g->igdbPlatformId,
                       g->launchOptions, info);
    if (!dlg.confirmed) return;

    bool titleChanged = (g->title != dlg.newTitle);
    g->title          = dlg.newTitle;
    g->igdbPlatformId = dlg.selectedIgdbPlatformId;
    g->launchOptions  = dlg.newLaunchOptions;

    // Rename ROM file on disk if requested (emulated games only).
    if (dlg.renameFile && !g->romPath.empty()) {
        size_t sep = g->romPath.rfind(L'\\');
        size_t dot = g->romPath.rfind(L'.');
        std::wstring dir = (sep != std::wstring::npos) ? g->romPath.substr(0, sep + 1) : L"";
        std::wstring ext = (dot != std::wstring::npos && dot > sep) ? g->romPath.substr(dot) : L"";
        std::wstring newPath = dir + dlg.newTitle + ext;
        if (MoveFileW(g->romPath.c_str(), newPath.c_str())) {
            std::wstring oldQ = L"\"" + g->romPath + L"\"";
            std::wstring newQ = L"\"" + newPath + L"\"";
            size_t pos = g->arguments.find(oldQ);
            if (pos != std::wstring::npos) g->arguments.replace(pos, oldQ.size(), newQ);
            g->romPath = newPath;
        } else {
            MessageBoxW(m_hwnd, (L"Could not rename file:\n" + g->romPath).c_str(),
                        L"Rename Failed", MB_OK | MB_ICONWARNING);
        }
    }

    // A changed title invalidates the IGDB match — re-scan for fresh metadata.
    if (titleChanged) {
        g->igdbMatched = false;
        if (m_metaManager) {
            m_metaManager->ScanGameAsync(g->id,
                [this](const std::wstring& id, bool /*matched*/) {
                    PostMessageW(m_hwnd, WM_USER + 4, 0, (LPARAM)new std::wstring(id));
                });
        }
    }

    SaveAll();
    ApplyFilter();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::OpenMetadataPicker(const std::wstring& gameId, const std::wstring& gameTitle) {
    if (!m_metaManager || m_picker.IsOpen()) return;

    m_picker.Open(m_hwnd, gameId, gameTitle, *m_metaManager,
        [this](const std::wstring& id) {
            // Art is ready — reload on the render thread
            PostMessageW(m_hwnd, WM_USER + 4, 0, (LPARAM)new std::wstring(id));
        });
}

void App::OpenSettings(int startPage) {
    if (m_settings.IsOpen()) return;

    SaveAll();

    // Shared lambda to (re-)wire IGDB client whenever credentials may have changed.
    auto rewireIgdb = [this]() {
        auto& cfg = m_config.Get();
        if (!cfg.igdbClientId.empty()) {
            m_igdbClient.SetCredentials(cfg.igdbClientId, cfg.igdbClientSecret);
            if (!cfg.igdbAccessToken.empty())
                m_igdbClient.RestoreToken(cfg.igdbAccessToken, cfg.igdbTokenExpiry);
            if (!m_metaManager) {
                std::wstring artDir = GetAppDataPath() + L"\\art";
                m_metaManager = std::make_unique<MetadataManager>(
                    m_library, m_igdbClient, artDir);
            }
        }
    };

    auto metaProgressCb = [this](const std::wstring& id, bool /*matched*/) {
        PostMessageW(m_hwnd, WM_USER + 4, 0, (LPARAM)new std::wstring(id));
    };

    m_settings.Open(m_hwnd, m_config.Get(),
        /* onSave */ [this, rewireIgdb]() {
            rewireIgdb();
            SaveAll();
            m_renderer.LoadPlatformIcons(m_platformIcons, m_config.Get().emulators);
            UpdateSidebarFlags();
            std::thread([this]() { ScanAllPlatforms(); }).detach();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        },
        /* onRefreshMeta */ [this, rewireIgdb, metaProgressCb]() {
            rewireIgdb();
            if (m_metaManager)
                m_metaManager->ScanAllAsync(metaProgressCb);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        },
        /* onReacquireMeta */ [this, rewireIgdb, metaProgressCb]() {
            rewireIgdb();
            if (m_metaManager)
                m_metaManager->ForceRescanAllAsync(metaProgressCb);
            InvalidateRect(m_hwnd, nullptr, FALSE);
        },
        startPage,
        &m_igdbClient);
}

void App::ShowProfileMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_PROFILE_ACCOUNT, L"Account settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_PROFILE_LOGOUT, L"Log out");

    // Anchor the dropdown just under the circular profile button (top-right of
    // the topbar). The button spans client x = [w-50, w-14], y = [14, 50].
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    POINT pt{ rc.right - 50, 52 };
    ClientToScreen(m_hwnd, &pt);

    SetForegroundWindow(m_hwnd);
    UINT cmd = (UINT)TrackPopupMenuEx(menu,
        TPM_RETURNCMD | TPM_LEFTBUTTON | TPM_TOPALIGN | TPM_LEFTALIGN,
        pt.x, pt.y, m_hwnd, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
    case IDM_PROFILE_ACCOUNT: OpenAccountSettings(); break;
    case IDM_PROFILE_LOGOUT:  LogOut();              break;
    default: break;
    }
}

void App::OpenAccountSettings() {
    ServerConfig sc = m_config.Get().server;
    if (ShowAccountDialog(m_hwnd, sc)) {
        // Password (and cleared token) may have changed — persist.
        m_config.Get().server.password = sc.password;
        m_config.Get().server.authToken = sc.authToken;
        SaveAll();
    }
    // Avatar may have been changed/removed inside the dialog — refresh the
    // topbar picture regardless of the password-change return value.
    RefreshAvatar();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void App::LogOut() {
    // Steam-style sign-out: remove the stored account (token + credentials) and
    // close the launcher. The next launch shows only the sign-in window until
    // the user signs back in (see the login gate in Initialize).
    {
        std::lock_guard<std::mutex> lk(m_authMutex);
        auto& s = m_config.Get().server;
        s.authToken.clear();
        s.tokenFingerprint.clear();
        s.password.clear();
        s.username.clear();
    }
    m_renderer.ClearAvatar();
    SaveAll();

    DestroyWindow(m_hwnd);   // WM_DESTROY → PostQuitMessage exits the message loop
}

void App::RefreshAvatar() {
    const auto serverCfg = m_config.Get().server;
    if (!serverCfg.enabled || serverCfg.baseUrl.empty()) {
        m_renderer.ClearAvatar();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }
    // Fetch off the UI thread; hand decoded bytes back to the renderer via a
    // posted message so D2D/WIC work happens on the UI (render-target) thread.
    HWND hwnd = m_hwnd;
    ServerConfig cfg = serverCfg;
    std::thread([this, hwnd, cfg]() {
        ServerClient client(cfg);
        std::string bytes;
        std::wstring err;
        if (client.GetAvatar(bytes, err) && !bytes.empty()) {
            auto* payload = new std::string(std::move(bytes));
            PostMessageW(hwnd, WM_AVATAR_READY, 0, (LPARAM)payload);
        } else {
            PostMessageW(hwnd, WM_AVATAR_READY, 0, 0);
        }
    }).detach();
}

void App::SaveAll() {
    std::wstring appData = GetAppDataPath();
    CreateDirectoryW(appData.c_str(), nullptr);

    // Persist any refreshed IGDB token so we don't re-auth every launch
    if (m_igdbClient.IsAuthenticated()) {
        m_config.Get().igdbAccessToken = m_igdbClient.SavedToken();
        m_config.Get().igdbTokenExpiry = m_igdbClient.TokenExpiry();
    }

    m_library.Save(appData + L"\\library.json");
    m_config.Save(appData + L"\\config.json");
}

void App::LoadAll() {
    std::wstring appData = GetAppDataPath();
    CreateDirectoryW(appData.c_str(), nullptr);
    m_config.Load(appData + L"\\config.json");
    m_library.Load(appData + L"\\library.json");
    m_library.RemoveServerClientLocalEntries();
}

// ── Tray icon implementation ───────────────────────────────────────────────────

void App::CreateTrayIcon() {
    m_nid = {};
    m_nid.cbSize           = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd             = m_hwnd;
    m_nid.uID              = 1;
    m_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon            = LoadIconW(m_hInst, MAKEINTRESOURCEW(101));
    wcscpy_s(m_nid.szTip, L"ArcadeLauncher");
    Shell_NotifyIconW(NIM_ADD, &m_nid);
}

void App::RemoveTrayIcon() {
    if (m_nid.hWnd)
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
    m_nid.hWnd = nullptr;
}

void App::ShowToast(const std::wstring& title, const std::wstring& message) {
    if (!m_nid.hWnd) return;
    NOTIFYICONDATAW nid = m_nid;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    nid.uTimeout = 5000;  // honored only on older shells; modern toasts auto-dismiss
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, message.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::ShowWindow_(bool show) {
    if (show) {
        ShowWindow(m_hwnd, SW_SHOW);
        ShowWindow(m_hwnd, SW_RESTORE);
        SetForegroundWindow(m_hwnd);
    } else {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

bool App::IsStartupEnabled() const {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return false;
    DWORD size = 0;
    bool found = RegQueryValueExW(hKey, L"ArcadeLauncher",
                                  nullptr, nullptr, nullptr, &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return found;
}

void App::SetStartup(bool enable) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return;

    if (enable) {
        wchar_t exePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring val = std::wstring(L"\"") + exePath + L"\" --tray";
        RegSetValueExW(hKey, L"ArcadeLauncher", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(val.c_str()),
            static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, L"ArcadeLauncher");
    }
    RegCloseKey(hKey);
}

void App::ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();

    // Bold app-name header (owner-draw workaround: just use a grayed string)
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"ArcadeLauncher");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Show / Hide toggle
    bool visible = IsWindowVisible(m_hwnd) != FALSE;
    AppendMenuW(menu, MF_STRING, IDM_TRAY_SHOW, visible ? L"Hide" : L"Show");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Recent games — top 5 by lastPlayed
    m_trayRecentIds.clear();
    {
        std::vector<const Game*> recent;
        for (auto& g : m_library.All())
            if (g.lastPlayed > 0) recent.push_back(&g);
        std::sort(recent.begin(), recent.end(),
            [](const Game* a, const Game* b) { return a->lastPlayed > b->lastPlayed; });
        if (recent.size() > 5) recent.resize(5);

        if (!recent.empty()) {
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Recent");
            for (size_t i = 0; i < recent.size(); i++) {
                m_trayRecentIds.push_back(recent[i]->id);
                // Truncate long titles for the menu
                std::wstring lbl = recent[i]->title;
                if (lbl.size() > 40) { lbl.resize(37); lbl += L"..."; }
                AppendMenuW(menu, MF_STRING, IDM_TRAY_GAME0 + (UINT)i, lbl.c_str());
            }
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }
    }

    // Account (only when a server library is configured)
    if (m_config.Get().server.enabled) {
        AppendMenuW(menu, MF_STRING, IDM_TRAY_ACCOUNT, L"Account…");
    }

    // Settings
    AppendMenuW(menu, MF_STRING, IDM_TRAY_SETTINGS, L"Settings");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    // Exit
    AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    // SetForegroundWindow is required so the menu dismisses on click-away.
    SetForegroundWindow(m_hwnd);
    // TPM_RETURNCMD: get the selection back directly instead of via WM_COMMAND,
    // which has no handler in the main window proc.
    UINT cmd = (UINT)TrackPopupMenuEx(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
        pt.x, pt.y, m_hwnd, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
    case IDM_TRAY_SHOW:
        ShowWindow_(IsWindowVisible(m_hwnd) == FALSE);
        break;
    case IDM_TRAY_SETTINGS:
        ShowWindow_(true);
        OpenSettings();
        break;
    case IDM_TRAY_ACCOUNT: {
        ShowWindow_(true);
        ServerConfig sc = m_config.Get().server;
        if (ShowAccountDialog(m_hwnd, sc)) {
            // Password (and cleared token) may have changed — persist.
            m_config.Get().server.password = sc.password;
            m_config.Get().server.authToken = sc.authToken;
            SaveAll();
        }
        break;
    }
    case IDM_TRAY_EXIT:
        RemoveTrayIcon();
        DestroyWindow(m_hwnd);
        break;
    default:
        if (cmd >= IDM_TRAY_GAME0 && cmd < IDM_TRAY_GAME0 + 10) {
            int ri = (int)(cmd - IDM_TRAY_GAME0);
            if (ri < (int)m_trayRecentIds.size()) {
                const Game* g = m_library.FindById(m_trayRecentIds[ri]);
                if (g) { ShowWindow_(true); LaunchGame(*g); }
            }
        }
        break;
    }
}
