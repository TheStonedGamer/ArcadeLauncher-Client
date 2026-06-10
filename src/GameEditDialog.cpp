#include "pch.h"
#include "GameEditDialog.h"
#include "IgdbPlatforms.h"
#include "DarkTheme.h"

void GameEditDialog::Show(HWND parent,
                           const std::wstring& currentTitle,
                           bool isEmulated,
                           int currentIgdbPlatformId) {
    m_props    = false;
    m_inLaunch.clear();
    m_infoText.clear();
    Run(parent, currentTitle, isEmulated, currentIgdbPlatformId);
}

void GameEditDialog::ShowProperties(HWND parent,
                                    const std::wstring& currentTitle,
                                    bool isEmulated,
                                    int currentIgdbPlatformId,
                                    const std::wstring& launchOptions,
                                    const std::wstring& infoText) {
    m_props    = true;
    m_inLaunch = launchOptions;
    m_infoText = infoText;
    newLaunchOptions = launchOptions;
    Run(parent, currentTitle, isEmulated, currentIgdbPlatformId);
}

void GameEditDialog::Run(HWND parent,
                         const std::wstring& currentTitle,
                         bool isEmulated,
                         int currentIgdbPlatformId) {
    confirmed             = false;
    renameFile            = false;
    newTitle              = currentTitle;
    selectedIgdbPlatformId = currentIgdbPlatformId;
    m_done                = false;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hbrBackground = dark::BgBrush();
        wc.lpszClassName = WNDCLS;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    // CLIENT height = lowest control + a ~22px bottom margin (BuildControls lays
    // controls out top-down in client coordinates).
    // + 30 if emulated (rename checkbox row). Properties mode is taller to fit
    // the read-only info block and the launch-options editor.
    int H;
    const wchar_t* caption;
    if (m_props) {
        H = isEmulated ? 343 : 313;
        caption = L"Game Properties";
    } else {
        H = isEmulated ? 165 : 135;
        caption = L"Edit Game";
    }

    // W/H are the desired client size; inflate by the frame so the caption and
    // borders don't eat into the right/bottom padding.
    RECT wr = { 0, 0, W, H };
    AdjustWindowRectEx(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;

    RECT pr;
    GetWindowRect(parent, &pr);
    int px = pr.left + (pr.right  - pr.left - winW) / 2;
    int py = pr.top  + (pr.bottom - pr.top  - winH) / 2;

    m_hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        WNDCLS, caption,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        px, py, winW, winH,
        parent, nullptr, GetModuleHandleW(nullptr), this);

    if (!m_hwnd) return;

    BuildControls(m_hwnd, isEmulated);
    dark::EnableTitleBar(m_hwnd);
    dark::Apply(m_hwnd);

    // Pre-fill edit box and select all text
    SetWindowTextW(m_edit, currentTitle.c_str());
    SendMessageW(m_edit, EM_SETSEL, 0, -1);

    // Pre-fill launch options (properties mode)
    if (m_launch) SetWindowTextW(m_launch, m_inLaunch.c_str());

    // Pre-select the current platform in the combobox
    if (m_combo) {
        int selIdx = 0;
        for (int i = 0; i < kIgdbPlatformCount; ++i) {
            if (kIgdbPlatforms[i].id == currentIgdbPlatformId) { selIdx = i; break; }
        }
        SendMessageW(m_combo, CB_SETCURSEL, selIdx, 0);
    }

    EnableWindow(parent, FALSE);
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);

    // Nested message loop — runs until m_done
    MSG msg;
    while (!m_done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessage(m_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (IsWindow(m_hwnd)) DestroyWindow(m_hwnd);
    m_hwnd = nullptr;
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
}

// ── WndProc ───────────────────────────────────────────────────────────────────

LRESULT CALLBACK GameEditDialog::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    GameEditDialog* dlg = nullptr;
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        dlg = reinterpret_cast<GameEditDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)dlg);
    } else {
        dlg = reinterpret_cast<GameEditDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (dlg) return dlg->HandleMsg(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT GameEditDialog::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (LRESULT r; dark::OnCtlColor(msg, wp, lp, r)) return r;
    switch (msg) {
    case WM_CLOSE:
        m_done = true;
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == ID_OK)     { Commit(); return 0; }
        if (id == ID_CANCEL) { m_done = true; return 0; }
        break;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { m_done = true; return 0; }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Controls ──────────────────────────────────────────────────────────────────

void GameEditDialog::BuildControls(HWND hwnd, bool isEmulated) {
    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HINSTANCE hi = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);

    auto mk = [&](const wchar_t* cls, DWORD style, int x, int y, int w, int h, int id) {
        HWND c = CreateWindowExW(0, cls, L"", WS_CHILD | WS_VISIBLE | style,
                                 x, y, w, h, hwnd, (HMENU)(INT_PTR)id, hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        return c;
    };

    auto lbl = [&](const wchar_t* text, int x, int y, int w, int h) {
        HWND c = CreateWindowExW(0, WC_STATICW, text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                 x, y, w, h, hwnd, nullptr, hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        return c;
    };

    int y = 13;

    // Properties mode — read-only info block (platform / version / install info)
    if (m_props && !m_infoText.empty()) {
        HWND info = CreateWindowExW(0, WC_STATICW, m_infoText.c_str(),
                                    WS_CHILD | WS_VISIBLE | SS_LEFT,
                                    12, y, W - 28, 86, hwnd, nullptr, hi, nullptr);
        SendMessageW(info, WM_SETFONT, (WPARAM)f, TRUE);
        y += 96;
    }

    // Row 1 — "Game title:" label + edit box
    lbl(L"Game title:", 12, y + 3, 80, 18);
    m_edit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"",
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                              98, y, W - 110, 22, hwnd,
                              (HMENU)(INT_PTR)ID_EDIT, hi, nullptr);
    SendMessageW(m_edit, WM_SETFONT, (WPARAM)f, TRUE);
    SetWindowSubclass(m_edit,
        [](HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) -> LRESULT {
            if (m == WM_KEYDOWN && w == VK_RETURN)
                SendMessageW(GetParent(h), WM_COMMAND, MAKEWPARAM(ID_OK, BN_CLICKED), 0);
            return DefSubclassProc(h, m, w, l);
        }, 0, 0);
    y += 32;

    // Row 2 — "IGDB platform:" label + combobox
    lbl(L"IGDB platform:", 12, y + 3, 90, 18);
    m_combo = CreateWindowExW(0, WC_COMBOBOXW, L"",
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST,
                               108, y, W - 120, 200, hwnd,
                               (HMENU)(INT_PTR)ID_COMBO, hi, nullptr);
    SendMessageW(m_combo, WM_SETFONT, (WPARAM)f, TRUE);
    for (int i = 0; i < kIgdbPlatformCount; ++i)
        SendMessageW(m_combo, CB_ADDSTRING, 0, (LPARAM)kIgdbPlatforms[i].name);
    y += 34;

    // Row 3 — "rename file" checkbox (emulated games only)
    if (isEmulated) {
        m_chk = mk(WC_BUTTONW, BS_AUTOCHECKBOX, 12, y, W - 24, 20, ID_CHK);
        SetWindowTextW(m_chk,
            L"Also rename the ROM file on disk to match the new title");
        y += 30;
    }

    // Row 4 — launch options (properties mode only)
    if (m_props) {
        lbl(L"Launch options (use %command% for the default):", 12, y, W - 28, 18);
        y += 20;
        m_launch = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"",
                                   WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE |
                                   ES_AUTOVSCROLL | ES_WANTRETURN,
                                   12, y, W - 28, 54, hwnd,
                                   (HMENU)(INT_PTR)ID_LAUNCH, hi, nullptr);
        SendMessageW(m_launch, WM_SETFONT, (WPARAM)f, TRUE);
        y += 62;
    }

    // Buttons
    m_btnOk     = mk(WC_BUTTONW, BS_DEFPUSHBUTTON, W - 196, y + 6, 88, 28, ID_OK);
    m_btnCancel = mk(WC_BUTTONW, 0,                W - 100, y + 6, 88, 28, ID_CANCEL);
    SetWindowTextW(m_btnOk,     L"OK");
    SetWindowTextW(m_btnCancel, L"Cancel");

    SetFocus(m_edit);
}

// ── Commit ────────────────────────────────────────────────────────────────────

void GameEditDialog::Commit() {
    wchar_t buf[512];
    GetWindowTextW(m_edit, buf, 512);
    std::wstring t(buf);

    // Trim whitespace
    while (!t.empty() && iswspace(t.front())) t.erase(t.begin());
    while (!t.empty() && iswspace(t.back()))  t.pop_back();

    if (t.empty()) {
        MessageBoxW(m_hwnd, L"Title cannot be empty.", L"Edit Game",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    newTitle   = t;
    renameFile = m_chk && (SendMessageW(m_chk, BM_GETCHECK, 0, 0) == BST_CHECKED);

    if (m_combo) {
        LRESULT sel = SendMessageW(m_combo, CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < kIgdbPlatformCount)
            selectedIgdbPlatformId = kIgdbPlatforms[sel].id;
    }

    if (m_launch) {
        int len = GetWindowTextLengthW(m_launch);
        std::wstring s(len, L'\0');
        if (len) {
            GetWindowTextW(m_launch, &s[0], len + 1);
            s.resize(len);
        }
        // Trim surrounding whitespace/newlines.
        while (!s.empty() && iswspace(s.front())) s.erase(s.begin());
        while (!s.empty() && iswspace(s.back()))  s.pop_back();
        newLaunchOptions = s;
    }

    confirmed = true;
    m_done    = true;
}
