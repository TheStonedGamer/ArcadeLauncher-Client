#include "pch.h"
#include "AppUpdater.h"
#include "Version.h"

// ── WinHTTP helpers ───────────────────────────────────────────────────────────

static HINTERNET OpenSession() {
    return WinHttpOpen(L"ArcadeLauncher/" ARCADE_VERSION_WSTR,
                       WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                       WINHTTP_NO_PROXY_NAME,
                       WINHTTP_NO_PROXY_BYPASS, 0);
}

// GETs url and returns the response body as a string. Returns empty on error.
static std::string HttpGetStr(const std::wstring& url) {
    HINTERNET hSess = OpenSession();
    if (!hSess) return {};

    URL_COMPONENTSW uc{};
    uc.dwStructSize      = sizeof(uc);
    wchar_t host[256]{}, path[2048]{};
    uc.lpszHostName      = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath       = path; uc.dwUrlPathLength  = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        WinHttpCloseHandle(hSess); return {};
    }

    HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return {}; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path,
                                        nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return {}; }

    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));
    // GitHub API requires Accept header
    WinHttpAddRequestHeaders(hReq,
        L"Accept: application/vnd.github+json\r\n",
        (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

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
    WinHttpCloseHandle(hSess);
    return body;
}

// Downloads url to destPath, following redirects. Returns true on success.
// If progressHwnd is set, posts WM_APP_UPDATE_PROGRESS (percent) as bytes arrive.
static bool HttpDownloadToFile(const std::wstring& url, const std::wstring& destPath,
                               HWND progressHwnd = nullptr) {
    // GitHub release assets redirect: github.com -> objects.githubusercontent.com.
    // We re-crack the URL after each redirect manually because WinHTTP's built-in
    // redirect policy only follows same-host redirects for HTTPS -> HTTPS.
    std::wstring currentUrl = url;
    const int MAX_REDIRECTS = 5;

    HINTERNET hSess = OpenSession();
    if (!hSess) return false;

    bool success = false;
    for (int redirect = 0; redirect <= MAX_REDIRECTS; ++redirect) {
        URL_COMPONENTSW uc{};
        uc.dwStructSize      = sizeof(uc);
        wchar_t host[512]{}, path[4096]{};
        uc.lpszHostName      = host; uc.dwHostNameLength = 512;
        uc.lpszUrlPath       = path; uc.dwUrlPathLength  = 4096;
        if (!WinHttpCrackUrl(currentUrl.c_str(), 0, 0, &uc)) break;

        HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
        if (!hConn) break;

        DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path,
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        bool reqOk = false;
        if (hReq) {
            // Do NOT auto-follow redirects — we handle them manually so we can
            // re-crack the new URL and open a fresh connection to the new host.
            DWORD noRedir = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            WinHttpSetOption(hReq, WINHTTP_OPTION_REDIRECT_POLICY, &noRedir, sizeof(noRedir));

            if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hReq, nullptr)) {
                DWORD status = 0, szStatus = sizeof(DWORD);
                WinHttpQueryHeaders(hReq,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    nullptr, &status, &szStatus, nullptr);

                if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
                    // Grab Location header and loop
                    wchar_t location[4096]{};
                    DWORD locSz = sizeof(location);
                    if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_LOCATION,
                                           nullptr, location, &locSz, nullptr)) {
                        currentUrl = location;
                        reqOk = true; // will loop
                    }
                } else if (status == 200) {
                    // Stream to file
                    HANDLE hFile = CreateFileW(destPath.c_str(),
                        GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        // Total size for progress (GitHub asset responses carry it).
                        DWORD total = 0, szTotal = sizeof(total);
                        WinHttpQueryHeaders(hReq,
                            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            nullptr, &total, &szTotal, nullptr);
                        uint64_t got = 0;
                        int lastPct = -1;
                        DWORD avail = 0;
                        success = true;
                        while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                            std::vector<char> buf(avail);
                            DWORD read = 0;
                            if (!WinHttpReadData(hReq, buf.data(), avail, &read)) {
                                success = false; break;
                            }
                            DWORD written = 0;
                            WriteFile(hFile, buf.data(), read, &written, nullptr);
                            got += read;
                            if (progressHwnd && total > 0) {
                                int pct = (int)((got * 100) / total);
                                if (pct != lastPct) {
                                    lastPct = pct;
                                    PostMessageW(progressHwnd, WM_APP_UPDATE_PROGRESS, (WPARAM)pct, 0);
                                }
                            }
                        }
                        CloseHandle(hFile);
                        if (!success) DeleteFileW(destPath.c_str());
                        reqOk = true;
                        redirect = MAX_REDIRECTS; // stop looping
                    }
                }
            }
            WinHttpCloseHandle(hReq);
        }
        WinHttpCloseHandle(hConn);
        if (!reqOk) break;
    }

    WinHttpCloseHandle(hSess);
    return success;
}

// ── JSON helpers ──────────────────────────────────────────────────────────────

static std::string JsonStr(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    size_t p = json.find(search);
    if (p == std::string::npos) return {};
    p += search.size();
    std::string val;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) { ++p; }
        val += json[p++];
    }
    return val;
}

// ── Version comparison ────────────────────────────────────────────────────────

static bool IsNewerThanCurrent(const std::wstring& tag) {
    // tag is e.g. L"client-v1.2.3" for this server-connected client.
    std::wstring v = tag;
    const std::wstring prefix = L"client-v";
    if (v.rfind(prefix, 0) == 0) v = v.substr(prefix.size());
    if (!v.empty() && (v[0] == L'v' || v[0] == L'V')) v = v.substr(1);

    int maj = 0, min_ = 0, pat = 0;
    swscanf_s(v.c_str(), L"%d.%d.%d", &maj, &min_, &pat);

    if (maj != ARCADE_VERSION_MAJOR) return maj > ARCADE_VERSION_MAJOR;
    if (min_ != ARCADE_VERSION_MINOR) return min_ > ARCADE_VERSION_MINOR;
    return pat > ARCADE_VERSION_PATCH;
}

// ── Update-attempt marker (loop breaker) ──────────────────────────────────────
// The per-machine MSI replaces the running exe, so an update requires us to exit,
// install, then relaunch. If the install ever fails to apply (declined UAC, MSI
// error, etc.) the relaunched-but-still-old binary would re-detect the same
// release and try again forever. We persist the tag we last attempted; if the
// same tag is still "newer" a short time later, we stop auto-attempting so the
// app stays usable instead of flickering through an endless update loop.

static constexpr long long ATTEMPT_COOLDOWN_100NS = 30LL * 60 * 10'000'000; // 30 min

static std::wstring AttemptMarkerPath() {
    return GetAppDataPath() + L"\\update_attempt.txt";
}

// Returns true if we attempted this exact tag within the cooldown window.
static bool RecentlyAttempted(const std::wstring& tag) {
    std::wstring path = AttemptMarkerPath();

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return false;

    HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return false;
    char buf[256]{}; DWORD read = 0;
    ReadFile(hf, buf, sizeof(buf) - 1, &read, nullptr);
    CloseHandle(hf);
    std::wstring stored = ToWide(std::string(buf, read));
    if (stored != tag) return false;

    FILETIME now{}; GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER n{ fad.ftLastWriteTime.dwLowDateTime, fad.ftLastWriteTime.dwHighDateTime };
    ULARGE_INTEGER t{ now.dwLowDateTime, now.dwHighDateTime };
    return (long long)(t.QuadPart - n.QuadPart) < ATTEMPT_COOLDOWN_100NS;
}

static void RecordAttempt(const std::wstring& tag) {
    std::wstring dir = GetAppDataPath();
    CreateDirectoryW(dir.c_str(), nullptr);
    std::string body = ToUtf8(tag);
    HANDLE hf = CreateFileW(AttemptMarkerPath().c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(hf, body.data(), (DWORD)body.size(), &written, nullptr);
    CloseHandle(hf);
}

// ── Background workers ────────────────────────────────────────────────────────

static void CheckWorker(HWND hwnd) {
    std::string json = HttpGetStr(
        L"https://api.github.com/repos/TheStonedGamer/ArcadeLauncher-Client/releases/latest");
    if (json.empty()) return;

    std::wstring tag = ToWide(JsonStr(json, "tag_name"));
    if (tag.empty() || !IsNewerThanCurrent(tag)) return;

    // Loop breaker: if we already tried (and apparently failed to apply) this same
    // release very recently, don't keep relaunching into the updater.
    if (RecentlyAttempted(tag)) return;

    // Construct the predictable MSI asset URL
    std::wstring msiUrl =
        L"https://github.com/TheStonedGamer/ArcadeLauncher-Client/releases/download/"
        + tag + L"/ArcadeLauncher-Server-Client-x64.msi";

    auto* info = new AppUpdateInfo{ tag, msiUrl };
    PostMessageW(hwnd, WM_APP_UPDATE_FOUND, 0, (LPARAM)info);
}

static void DownloadWorker(HWND hwnd, std::wstring tag, std::wstring msiUrl) {
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    std::wstring dest = std::wstring(tempDir) + L"ArcadeLauncher-update.msi";

    if (!HttpDownloadToFile(msiUrl, dest, hwnd)) {
        PostMessageW(hwnd, WM_APP_UPDATE_READY, 1 /*failure*/, 0);
        return;
    }

    // Record the attempt up-front so a failed install can't loop (see above).
    RecordAttempt(tag);

    // A per-machine MSI must run elevated, and `msiexec /quiet` cannot raise its
    // own UAC prompt — so we elevate ONCE here (the only prompt the user sees),
    // then everything is silent. We can't install while our exe is locked, so we
    // hand the work to an elevated PowerShell helper that:
    //   1. waits for this process to exit (releasing the exe),
    //   2. runs msiexec fully silently,
    //   3. relaunches the freshly-installed binary.
    // Short (8.3) paths avoid embedded-space quoting headaches inside -Command,
    // so the helper uses only single quotes (no -ExecutionPolicy override needed).
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wchar_t shortMsi[MAX_PATH]{}, shortExe[MAX_PATH]{};
    if (!GetShortPathNameW(dest.c_str(), shortMsi, MAX_PATH))
        wcscpy_s(shortMsi, dest.c_str());
    if (!GetShortPathNameW(exePath, shortExe, MAX_PATH))
        wcscpy_s(shortExe, exePath);

    DWORD pid = GetCurrentProcessId();
    std::wstring psCmd =
        L"try { Wait-Process -Id " + std::to_wstring(pid) + L" -Timeout 120 } catch {}; "
        L"Start-Process -FilePath 'msiexec.exe' -ArgumentList '/i','" + shortMsi +
        L"','/quiet','/norestart' -Wait; "
        L"Start-Process -FilePath '" + shortExe + L"'";
    std::wstring psArgs =
        L"-NoProfile -WindowStyle Hidden -Command \"" + psCmd + L"\"";

    SHELLEXECUTEINFOW sei{ sizeof(sei) };
    sei.fMask  = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";  // elevate the helper; msiexec then installs silently
    sei.lpFile = L"powershell.exe";
    sei.lpParameters = psArgs.c_str();
    sei.nShow  = SW_HIDE;

    if (!ShellExecuteExW(&sei)) {
        // User declined elevation (or it failed). Do NOT exit/relaunch — staying
        // on the current version avoids the flicker loop; the cooldown marker
        // keeps us from immediately retrying.
        PostMessageW(hwnd, WM_APP_UPDATE_READY, 1 /*failure*/, 0);
        return;
    }
    if (sei.hProcess) CloseHandle(sei.hProcess);

    // Helper is running elevated; tell the UI to exit so the install can proceed.
    PostMessageW(hwnd, WM_APP_UPDATE_READY, 0 /*success*/, 0);
}

// ── Public API ────────────────────────────────────────────────────────────────

void CheckForAppUpdateAsync(HWND hwnd) {
    std::thread(CheckWorker, hwnd).detach();
}

void DownloadAndInstallAsync(HWND hwnd, std::wstring tag, std::wstring msiUrl) {
    std::thread(DownloadWorker, hwnd, std::move(tag), std::move(msiUrl)).detach();
}
