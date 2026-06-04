#include "pch.h"
#include "EmulatorDownloader.h"
#include "ArchiveExtractor.h"

// ── WinHttp wrappers ──────────────────────────────────────────────────────────

struct WHttpSession {
    HINTERNET h = nullptr;
    WHttpSession() {
        h = WinHttpOpen(L"GameLauncher/1.0",
                        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                        WINHTTP_NO_PROXY_NAME,
                        WINHTTP_NO_PROXY_BYPASS, 0);
    }
    ~WHttpSession() { if (h) WinHttpCloseHandle(h); }
};

static std::string HttpGet(const std::wstring& url) {
    WHttpSession sess;
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

static bool DownloadFile(const std::wstring& url, const std::wstring& destPath) {
    WHttpSession sess;
    if (!sess.h) return false;

    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{}, path[2048]{};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath  = path; uc.dwUrlPathLength  = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    HINTERNET hConn = WinHttpConnect(sess.h, host, uc.nPort, 0);
    if (!hConn) return false;

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path,
                                        nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConn); return false; }

    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));

    bool written = false;
    if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, nullptr)) {
        DWORD status = 0, sz = sizeof(DWORD);
        WinHttpQueryHeaders(hReq,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &status, &sz, nullptr);
        if (status == 200) {
            HANDLE hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE) {
                written = true;
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                    std::string chunk(avail, '\0');
                    DWORD read = 0, wrote = 0;
                    WinHttpReadData(hReq, chunk.data(), avail, &read);
                    if (!WriteFile(hFile, chunk.data(), read, &wrote, nullptr) || wrote != read) {
                        written = false; break;
                    }
                }
                CloseHandle(hFile);
                if (!written) DeleteFileW(destPath.c_str());
            }
        }
    }
    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    return written;
}

// ── GitHub release asset URL finder ──────────────────────────────────────────

static std::wstring FindAssetUrl(const std::string& json, const std::wstring& pattern) {
    static const std::wstring skipWords[] = {
        L"debug", L"pdb", L"source", L"symbols", L"-src", L"_src",
        L".sha256", L".sha512", L".sig", L".asc",
        L"linux", L"macos", L"osx", L"android"
    };
    const std::string key = "\"browser_download_url\":\"";
    size_t pos = 0;
    while ((pos = json.find(key, pos)) != std::string::npos) {
        pos += key.size();
        size_t end = json.find('"', pos);
        if (end == std::string::npos) break;
        std::wstring url = ToWide(json.substr(pos, end - pos));
        pos = end + 1;

        std::wstring lower = url;
        for (auto& c : lower) c = towlower(c);

        bool skip = false;
        for (auto& sw : skipWords)
            if (lower.find(sw) != std::wstring::npos) { skip = true; break; }
        if (skip) continue;

        std::wstring pat = pattern;
        for (auto& c : pat) c = towlower(c);
        if (lower.find(pat) != std::wstring::npos) return url;
    }
    return {};
}

// ── Recursive exe finder ──────────────────────────────────────────────────────

static std::wstring FindExeInDir(const std::wstring& dir, const std::wstring& exeName) {
    std::wstring nameLower = exeName;
    for (auto& c : nameLower) c = towlower(c);

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};

    std::wstring found;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            found = FindExeInDir(full, exeName);
            if (!found.empty()) break;
        } else {
            std::wstring fn(fd.cFileName);
            for (auto& c : fn) c = towlower(c);
            if (fn == nameLower) { found = full; break; }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
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

static void Worker(HWND hwnd, int pageIdx, EmulatorDownloadSpec spec,
                   std::wstring appDataDir) {
    auto post = [&](EmuDownloadResult* result) {
        PostMessageW(hwnd, WM_EMUDOWNLOAD_DONE, (WPARAM)pageIdx, (LPARAM)result);
    };

    // 1. Fetch GitHub latest release JSON
    std::wstring apiUrl = L"https://api.github.com/repos/" +
                          ToWide(spec.githubRepo) + L"/releases/latest";
    std::string json = HttpGet(apiUrl);
    if (json.empty()) {
        post(new EmuDownloadResult{ L"ERR:Could not reach GitHub API. Check your internet connection." });
        return;
    }

    std::wstring tag = ParseTagName(json);

    // 2. Find matching asset URL
    std::wstring assetUrl = FindAssetUrl(json, spec.urlPattern);
    if (assetUrl.empty()) {
        post(new EmuDownloadResult{ L"ERR:No matching download found for \"" + spec.urlPattern + L"\"." });
        return;
    }

    // 3. Download archive
    std::wstring destDir = appDataDir + L"\\emulators\\" + spec.destName;
    CreateDirectoryW((appDataDir + L"\\emulators").c_str(), nullptr);
    CreateDirectoryW(destDir.c_str(), nullptr);

    std::wstring fileName = assetUrl;
    auto sl = fileName.rfind(L'/');
    if (sl != std::wstring::npos) fileName = fileName.substr(sl + 1);
    auto q  = fileName.find(L'?');
    if (q  != std::wstring::npos) fileName = fileName.substr(0, q);

    std::wstring archivePath = destDir + L"\\" + fileName;
    if (!DownloadFile(assetUrl, archivePath)) {
        post(new EmuDownloadResult{ L"ERR:File download failed." });
        return;
    }

    // 4. If the asset is already an executable, use it directly — no extraction.
    std::wstring fnLower = fileName;
    for (auto& c : fnLower) c = towlower(c);
    if (fnLower.size() >= 4 && fnLower.compare(fnLower.size() - 4, 4, L".exe") == 0) {
        post(new EmuDownloadResult{ archivePath, tag });
        return;
    }

    // 5. Extract (ZIP via PowerShell; 7z via LZMA SDK)
    if (!ExtractArchive(archivePath, destDir)) {
        DeleteFileW(archivePath.c_str());
        post(new EmuDownloadResult{ L"ERR:Extraction failed. The downloaded archive may be corrupt." });
        return;
    }
    DeleteFileW(archivePath.c_str());

    // 6. Locate the executable
    std::wstring exePath = FindExeInDir(destDir, spec.exeName);
    post(new EmuDownloadResult{ exePath, tag });
}

// ── Public API ────────────────────────────────────────────────────────────────

void DownloadEmulatorAsync(HWND hwnd, int pageIdx, EmulatorDownloadSpec spec,
                           const std::wstring& appDataDir) {
    std::thread(Worker, hwnd, pageIdx, std::move(spec), appDataDir).detach();
}
