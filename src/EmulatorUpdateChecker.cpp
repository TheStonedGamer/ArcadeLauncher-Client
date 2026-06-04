#include "pch.h"
#include "EmulatorUpdateChecker.h"

// ── WinHttp helpers ───────────────────────────────────────────────────────────

struct WHttpSess {
    HINTERNET h = nullptr;
    WHttpSess() {
        h = WinHttpOpen(L"GameLauncher/1.0",
                        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                        WINHTTP_NO_PROXY_NAME,
                        WINHTTP_NO_PROXY_BYPASS, 0);
    }
    ~WHttpSess() { if (h) WinHttpCloseHandle(h); }
};

static std::string HttpGetStr(const std::wstring& url) {
    WHttpSess sess;
    if (!sess.h) return {};

    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{}, path[1024]{};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath  = path; uc.dwUrlPathLength  = 1024;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return {};

    HINTERNET hConn = WinHttpConnect(sess.h, host, uc.nPort, 0);
    if (!hConn) return {};

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path,
                                        nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConn); return {}; }

    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));

    std::string body;
    if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, nullptr)) {
        DWORD status = 0, sz = sizeof(DWORD);
        WinHttpQueryHeaders(hReq,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &status, &sz, nullptr);
        if (status == 200) {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                std::string chunk(avail, '\0');
                DWORD read = 0;
                WinHttpReadData(hReq, chunk.data(), avail, &read);
                body.append(chunk.data(), read);
            }
        }
    }
    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    return body;
}

// ── Tag name parser ───────────────────────────────────────────────────────────

static std::wstring ParseTagName(const std::string& json) {
    const std::string key = "\"tag_name\":\"";
    size_t p = json.find(key);
    if (p == std::string::npos) return {};
    p += key.size();
    size_t end = json.find('"', p);
    if (end == std::string::npos) return {};
    return ToWide(json.substr(p, end - p));
}

// ── Worker thread ─────────────────────────────────────────────────────────────

static void CheckWorker(HWND hwnd, int pageIdx, std::string githubRepo) {
    auto* result = new EmuCheckResult{};

    std::wstring url = L"https://api.github.com/repos/" +
                       ToWide(githubRepo) + L"/releases/latest";
    std::string json = HttpGetStr(url);

    if (json.empty()) {
        result->isError = true;
    } else {
        result->latestTag = ParseTagName(json);
        if (result->latestTag.empty()) result->isError = true;
    }

    PostMessageW(hwnd, WM_EMUCHECK_DONE, (WPARAM)pageIdx, (LPARAM)result);
}

// ── Public API ────────────────────────────────────────────────────────────────

void CheckEmulatorUpdateAsync(HWND hwnd, int pageIdx, const std::string& githubRepo) {
    std::thread(CheckWorker, hwnd, pageIdx, githubRepo).detach();
}
