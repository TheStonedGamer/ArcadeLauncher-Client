#include "pch.h"
#include "AccountDialog.h"
#include "ServerClient.h"
#include "QrCode.h"

#include <windows.h>
#include <string>

// ── shared helpers ──────────────────────────────────────────────────────────

namespace {

const wchar_t* kAccountClass = L"ArcadeLauncherAccountWnd";
const wchar_t* kTotpClass    = L"ArcadeLauncherTotpWnd";
const wchar_t* kForceClass   = L"ArcadeLauncherForcePwWnd";

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
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = name;
    RegisterClassExW(&wc);
    registered.push_back(name);
}

// Centered modal window + standard IsDialogMessage loop.
HWND CreateCenteredModal(const wchar_t* cls, const wchar_t* title, HWND owner,
                         int w, int h, void* param) {
    RECT rc{};
    if (owner) GetWindowRect(owner, &rc);
    else rc = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    return CreateWindowExW(WS_EX_DLGMODALFRAME, cls, title,
        WS_CAPTION | WS_POPUP | WS_SYSMENU,
        rc.left + ((rc.right - rc.left) - w) / 2,
        rc.top + ((rc.bottom - rc.top) - h) / 2,
        w, h, owner, nullptr, GetModuleHandleW(nullptr), param);
}

void RunModalLoop(HWND hwnd, HWND owner, bool& done) {
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

// ── TOTP enrollment dialog ──────────────────────────────────────────────────

struct TotpState {
    ServerConfig* cfg = nullptr;
    HWND owner = nullptr;
    HWND hwnd = nullptr;
    bool done = false;
    bool enabled = false;   // result: TOTP successfully enabled

    HWND password = nullptr;
    HWND beginBtn = nullptr;
    HWND secretLabel = nullptr;
    HWND code = nullptr;
    HWND verifyBtn = nullptr;
    HWND status = nullptr;

    std::wstring secret;
    std::wstring otpauthUri;
    QrCode qr;
    bool haveQr = false;
};

void PaintTotpQr(HWND hwnd, TotpState* st) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    if (st->haveQr) {
        const int origin = 250;     // left edge of QR area
        const int top = 70;
        const int target = 210;     // pixels
        int n = st->qr.Size();
        int quiet = 4;
        int total = n + quiet * 2;
        int scale = target / total;
        if (scale < 1) scale = 1;
        int dim = total * scale;
        // white background (includes quiet zone)
        RECT bg{ origin, top, origin + dim, top + dim };
        FillRect(hdc, &bg, (HBRUSH)GetStockObject(WHITE_BRUSH));
        HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
        for (int y = 0; y < n; y++) {
            for (int x = 0; x < n; x++) {
                if (!st->qr.Module(x, y)) continue;
                RECT m{
                    origin + (x + quiet) * scale,
                    top + (y + quiet) * scale,
                    origin + (x + quiet + 1) * scale,
                    top + (y + quiet + 1) * scale };
                FillRect(hdc, &m, black);
            }
        }
    }
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK TotpWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<TotpState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<TotpState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        st->hwnd = hwnd;

        MakeLabel(hwnd, L"Enable Two-Factor Authentication", 22, 18, 400, 24);
        MakeLabel(hwnd, L"Confirm your password to begin enrollment.", 22, 50, 400);
        MakeLabel(hwnd, L"Password", 22, 84, 90);
        st->password = MakeEdit(hwnd, 201, 120, 82, 110, ES_PASSWORD);
        st->beginBtn = MakeButton(hwnd, 202, L"Begin", 240, 81, 80, 26, BS_DEFPUSHBUTTON);

        MakeLabel(hwnd, L"Scan this QR code with your authenticator app:", 22, 122, 220);
        st->secretLabel = MakeLabel(hwnd, L"", 22, 300, 220, 40);
        MakeLabel(hwnd, L"Enter code", 22, 350, 90);
        st->code = MakeEdit(hwnd, 203, 120, 348, 110);
        st->verifyBtn = MakeButton(hwnd, 204, L"Verify && Enable", 240, 346, 130, 26);
        st->status = MakeLabel(hwnd, L"", 22, 382, 440, 36);

        // QR-dependent controls start hidden until enrollment begins.
        ShowWindow(st->secretLabel, SW_HIDE);
        ShowWindow(st->code, SW_HIDE);
        ShowWindow(st->verifyBtn, SW_HIDE);

        EnsureFonts();
        ApplyFont(hwnd, g_uiFont, g_titleFont);
        SetFocus(st->password);
        return 0;
    }
    if (!st) return DefWindowProcW(hwnd, msg, wp, lp);

    if (msg == WM_PAINT) { PaintTotpQr(hwnd, st); return 0; }

    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == 202) {  // Begin enrollment
            std::wstring pw = TextOf(st->password);
            if (pw.empty()) { SetWindowTextW(st->status, L"Enter your password."); return 0; }
            ServerConfig cfg = *st->cfg;
            ServerClient client(cfg);
            std::wstring err;
            if (client.TotpSetup(pw, st->secret, st->otpauthUri, err)) {
                st->haveQr = st->qr.Encode(ToUtf8(st->otpauthUri));
                std::wstring sl = L"If you can't scan, enter this key manually:\n" + st->secret;
                SetWindowTextW(st->secretLabel, sl.c_str());
                ShowWindow(st->secretLabel, SW_SHOW);
                ShowWindow(st->code, SW_SHOW);
                ShowWindow(st->verifyBtn, SW_SHOW);
                EnableWindow(st->password, FALSE);
                EnableWindow(st->beginBtn, FALSE);
                SetWindowTextW(st->status, L"Scan the code, then enter the 6-digit code to confirm.");
                InvalidateRect(hwnd, nullptr, TRUE);
                SetFocus(st->code);
            } else {
                SetWindowTextW(st->status, err.empty() ? L"Could not start enrollment." : err.c_str());
            }
            return 0;
        }
        if (id == 204) {  // Verify & enable
            std::wstring c = TextOf(st->code);
            if (c.empty()) { SetWindowTextW(st->status, L"Enter the 6-digit code."); return 0; }
            ServerConfig cfg = *st->cfg;
            ServerClient client(cfg);
            std::wstring err;
            if (client.TotpEnable(c, err)) {
                st->enabled = true;
                st->done = true;
                DestroyWindow(hwnd);
            } else {
                SetWindowTextW(st->status, err.empty() ? L"Invalid code, try again." : err.c_str());
            }
            return 0;
        }
        if (id == IDCANCEL) { st->done = true; DestroyWindow(hwnd); return 0; }
    } else if (msg == WM_CLOSE) {
        st->done = true; DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool ShowTotpEnroll(HWND owner, ServerConfig& cfg) {
    RegisterModalClass(kTotpClass, TotpWndProc);
    TotpState st;
    st.cfg = &cfg;
    st.owner = owner;
    HWND hwnd = CreateCenteredModal(kTotpClass, L"Enable Two-Factor Authentication",
                                    owner, 500, 470, &st);
    if (!hwnd) return false;
    RunModalLoop(hwnd, owner, st.done);
    return st.enabled;
}

// ── TOTP disable prompt ─────────────────────────────────────────────────────

struct DisableState {
    ServerConfig* cfg = nullptr;
    HWND hwnd = nullptr;
    bool done = false;
    bool disabled = false;
    HWND password = nullptr;
    HWND code = nullptr;
    HWND status = nullptr;
};

LRESULT CALLBACK DisableWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<DisableState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<DisableState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        st->hwnd = hwnd;
        MakeLabel(hwnd, L"Disable Two-Factor Authentication", 22, 18, 400, 24);
        MakeLabel(hwnd, L"Confirm with your password or a current 2FA code.", 22, 50, 420);
        MakeLabel(hwnd, L"Password", 22, 86, 90);
        st->password = MakeEdit(hwnd, 301, 120, 84, 220, ES_PASSWORD);
        MakeLabel(hwnd, L"2FA code", 22, 122, 90);
        st->code = MakeEdit(hwnd, 302, 120, 120, 110);
        MakeButton(hwnd, IDOK, L"Disable 2FA", 120, 158, 120, 28, BS_DEFPUSHBUTTON);
        MakeButton(hwnd, IDCANCEL, L"Cancel", 250, 158, 90, 28);
        st->status = MakeLabel(hwnd, L"", 22, 196, 420, 36);
        EnsureFonts();
        ApplyFont(hwnd, g_uiFont, g_titleFont);
        SetFocus(st->password);
        return 0;
    }
    if (!st) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDOK) {
            std::wstring pw = TextOf(st->password);
            std::wstring code = TextOf(st->code);
            if (pw.empty() && code.empty()) {
                SetWindowTextW(st->status, L"Enter your password or a 2FA code.");
                return 0;
            }
            ServerConfig cfg = *st->cfg;
            ServerClient client(cfg);
            std::wstring err;
            if (client.TotpDisable(pw, code, err)) {
                st->disabled = true; st->done = true; DestroyWindow(hwnd);
            } else {
                SetWindowTextW(st->status, err.empty() ? L"Could not disable 2FA." : err.c_str());
            }
            return 0;
        }
        if (id == IDCANCEL) { st->done = true; DestroyWindow(hwnd); return 0; }
    } else if (msg == WM_CLOSE) {
        st->done = true; DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool ShowTotpDisable(HWND owner, ServerConfig& cfg) {
    const wchar_t* cls = L"ArcadeLauncherTotpDisableWnd";
    RegisterModalClass(cls, DisableWndProc);
    DisableState st;
    st.cfg = &cfg;
    HWND hwnd = CreateCenteredModal(cls, L"Disable Two-Factor Authentication",
                                    owner, 480, 290, &st);
    if (!hwnd) return false;
    RunModalLoop(hwnd, owner, st.done);
    return st.disabled;
}

// ── Forced password change dialog ───────────────────────────────────────────

struct ForceState {
    ServerConfig* cfg = nullptr;
    HWND hwnd = nullptr;
    bool done = false;
    bool changed = false;
    HWND current = nullptr;
    HWND newPw = nullptr;
    HWND confirm = nullptr;
    HWND status = nullptr;
};

LRESULT CALLBACK ForceWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<ForceState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<ForceState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        st->hwnd = hwnd;
        MakeLabel(hwnd, L"Password Change Required", 22, 18, 400, 24);
        MakeLabel(hwnd, L"An administrator requires you to set a new password.", 22, 50, 430);
        MakeLabel(hwnd, L"Current password", 22, 88, 140);
        st->current = MakeEdit(hwnd, 401, 180, 86, 240, ES_PASSWORD);
        MakeLabel(hwnd, L"New password", 22, 124, 140);
        st->newPw = MakeEdit(hwnd, 402, 180, 122, 240, ES_PASSWORD);
        MakeLabel(hwnd, L"Confirm new password", 22, 160, 140);
        st->confirm = MakeEdit(hwnd, 403, 180, 158, 240, ES_PASSWORD);
        MakeButton(hwnd, IDOK, L"Change Password", 180, 196, 150, 28, BS_DEFPUSHBUTTON);
        st->status = MakeLabel(hwnd, L"", 22, 232, 430, 36);
        // Pre-fill the current password from the just-used login credentials.
        if (st->cfg && !st->cfg->password.empty())
            SetWindowTextW(st->current, st->cfg->password.c_str());
        EnsureFonts();
        ApplyFont(hwnd, g_uiFont, g_titleFont);
        SetFocus(st->newPw);
        return 0;
    }
    if (!st) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == IDOK) {
            std::wstring cur = TextOf(st->current);
            std::wstring np = TextOf(st->newPw);
            std::wstring cf = TextOf(st->confirm);
            if (np.size() < 10) { SetWindowTextW(st->status, L"New password must be at least 10 characters."); return 0; }
            if (np != cf) { SetWindowTextW(st->status, L"New passwords do not match."); return 0; }
            ServerConfig cfg = *st->cfg;
            ServerClient client(cfg);
            std::wstring err;
            if (client.ChangePassword(cur, np, err)) {
                st->cfg->password = np;
                st->cfg->authToken.clear();   // force fresh auth with the new password
                st->changed = true; st->done = true; DestroyWindow(hwnd);
            } else {
                SetWindowTextW(st->status, err.empty() ? L"Could not change password." : err.c_str());
            }
            return 0;
        }
        if (id == IDCANCEL) { st->done = true; DestroyWindow(hwnd); return 0; }
    } else if (msg == WM_CLOSE) {
        st->done = true; DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Main account dialog ─────────────────────────────────────────────────────

struct AccountState {
    ServerConfig* cfg = nullptr;
    HWND owner = nullptr;
    HWND hwnd = nullptr;
    bool done = false;
    bool changed = false;   // password changed → caller should save

    HWND identity = nullptr;
    HWND current = nullptr;
    HWND newPw = nullptr;
    HWND confirm = nullptr;
    HWND changeBtn = nullptr;
    HWND pwStatus = nullptr;
    HWND totpStatus = nullptr;
    HWND totpBtn = nullptr;

    bool totpEnabled = false;
};

void RefreshAccount(AccountState* st) {
    ServerConfig cfg = *st->cfg;
    ServerClient client(cfg);
    ServerClient::AccountInfo info;
    std::wstring err;
    if (client.GetAccount(info, err)) {
        std::wstring who = info.username;
        if (!info.email.empty()) who += L"  <" + info.email + L">";
        if (info.isAdmin) who += L"   (admin)";
        SetWindowTextW(st->identity, who.c_str());
        st->totpEnabled = info.totpEnabled;
    } else {
        SetWindowTextW(st->identity, (L"Signed in as " + st->cfg->username).c_str());
    }
    SetWindowTextW(st->totpStatus, st->totpEnabled
        ? L"Two-factor authentication is ON."
        : L"Two-factor authentication is OFF.");
    SetWindowTextW(st->totpBtn, st->totpEnabled ? L"Disable 2FA…" : L"Enable 2FA…");
}

LRESULT CALLBACK AccountWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<AccountState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<AccountState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
        st->hwnd = hwnd;

        MakeLabel(hwnd, L"Account", 22, 18, 400, 24);
        st->identity = MakeLabel(hwnd, L"", 22, 50, 460);

        MakeLabel(hwnd, L"Change Password", 22, 86, 200, 20);
        MakeLabel(hwnd, L"Current", 22, 116, 120);
        st->current = MakeEdit(hwnd, 501, 150, 114, 330, ES_PASSWORD);
        MakeLabel(hwnd, L"New", 22, 150, 120);
        st->newPw = MakeEdit(hwnd, 502, 150, 148, 330, ES_PASSWORD);
        MakeLabel(hwnd, L"Confirm", 22, 184, 120);
        st->confirm = MakeEdit(hwnd, 503, 150, 182, 330, ES_PASSWORD);
        st->changeBtn = MakeButton(hwnd, 504, L"Change Password", 150, 216, 150, 28);
        st->pwStatus = MakeLabel(hwnd, L"", 22, 250, 460, 18);

        MakeLabel(hwnd, L"Two-Factor Authentication", 22, 286, 300, 20);
        st->totpStatus = MakeLabel(hwnd, L"", 22, 314, 460, 18);
        st->totpBtn = MakeButton(hwnd, 505, L"Enable 2FA…", 22, 340, 150, 28);

        MakeButton(hwnd, IDCANCEL, L"Close", 388, 388, 92, 28);

        EnsureFonts();
        ApplyFont(hwnd, g_uiFont, g_titleFont);
        RefreshAccount(st);
        SetFocus(st->current);
        return 0;
    }
    if (!st) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == 504) {  // change password
            std::wstring cur = TextOf(st->current);
            std::wstring np = TextOf(st->newPw);
            std::wstring cf = TextOf(st->confirm);
            if (np.size() < 10) { SetWindowTextW(st->pwStatus, L"New password must be at least 10 characters."); return 0; }
            if (np != cf) { SetWindowTextW(st->pwStatus, L"New passwords do not match."); return 0; }
            ServerConfig cfg = *st->cfg;
            ServerClient client(cfg);
            std::wstring err;
            if (client.ChangePassword(cur, np, err)) {
                st->cfg->password = np;
                st->cfg->authToken.clear();
                st->changed = true;
                SetWindowTextW(st->pwStatus, L"Password changed.");
                SetWindowTextW(st->current, L"");
                SetWindowTextW(st->newPw, L"");
                SetWindowTextW(st->confirm, L"");
            } else {
                SetWindowTextW(st->pwStatus, err.empty() ? L"Could not change password." : err.c_str());
            }
            return 0;
        }
        if (id == 505) {  // toggle TOTP
            if (st->totpEnabled) {
                if (ShowTotpDisable(hwnd, *st->cfg))
                    SetWindowTextW(st->pwStatus, L"Two-factor authentication disabled.");
            } else {
                if (ShowTotpEnroll(hwnd, *st->cfg))
                    SetWindowTextW(st->pwStatus, L"Two-factor authentication enabled.");
            }
            RefreshAccount(st);
            return 0;
        }
        if (id == IDCANCEL) { st->done = true; DestroyWindow(hwnd); return 0; }
    } else if (msg == WM_CLOSE) {
        st->done = true; DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

bool ShowAccountDialog(HWND owner, ServerConfig& cfg) {
    RegisterModalClass(kAccountClass, AccountWndProc);
    AccountState st;
    st.cfg = &cfg;
    st.owner = owner;
    HWND hwnd = CreateCenteredModal(kAccountClass, L"Account", owner, 520, 450, &st);
    if (!hwnd) return false;
    RunModalLoop(hwnd, owner, st.done);
    return st.changed;
}

bool ShowForcedPasswordChange(HWND owner, ServerConfig& cfg) {
    RegisterModalClass(kForceClass, ForceWndProc);
    ForceState st;
    st.cfg = &cfg;
    HWND hwnd = CreateCenteredModal(kForceClass, L"Password Change Required", owner, 500, 320, &st);
    if (!hwnd) return false;
    RunModalLoop(hwnd, owner, st.done);
    return st.changed;
}
