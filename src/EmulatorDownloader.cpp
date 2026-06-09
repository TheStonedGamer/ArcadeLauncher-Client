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

static bool DownloadFile(const std::wstring& url, const std::wstring& destPath,
                         std::function<void(uint64_t, uint64_t)> onProgress = {}) {
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
            uint64_t total = 0;
            wchar_t lenBuf[64]{};
            DWORD lenSize = sizeof(lenBuf);
            if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_LENGTH,
                                    nullptr, lenBuf, &lenSize, nullptr)) {
                total = _wtoi64(lenBuf);
            }
            HANDLE hFile = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE) {
                written = true;
                uint64_t downloaded = 0;
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                    std::string chunk(avail, '\0');
                    DWORD read = 0, wrote = 0;
                    WinHttpReadData(hReq, chunk.data(), avail, &read);
                    if (!WriteFile(hFile, chunk.data(), read, &wrote, nullptr) || wrote != read) {
                        written = false; break;
                    }
                    downloaded += read;
                    if (onProgress) onProgress(downloaded, total);
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

// ── xemu firmware provisioning ────────────────────────────────────────────────
// Original Xbox emulation needs the console firmware (MCPX boot ROM, flash BIOS,
// a formatted HDD image) and a config pointing at them before xemu can boot any
// disc. We host those files on the server next to the emulator archives
// (`/emulators/xemu-firmware/`) and pull them in once, right after xemu itself
// is downloaded, then write xemu's `xemu.toml` so games launch out of the box.

// (UTF-8 <-> wide conversions use the global ToWide/ToUtf8 from pch.h.)

// True if `key` appears as a `key = ...` assignment anywhere in the document.
static bool TomlHas(const std::wstring& doc, const std::wstring& key) {
    size_t pos = 0;
    while (pos <= doc.size()) {
        size_t nl = doc.find(L'\n', pos);
        std::wstring line = doc.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
        size_t a = line.find_first_not_of(L" \t\r");
        if (a != std::wstring::npos && line.compare(a, key.size(), key) == 0) {
            size_t j = a + key.size();
            while (j < line.size() && (line[j] == L' ' || line[j] == L'\t')) j++;
            if (j < line.size() && line[j] == L'=') return true;
        }
        if (nl == std::wstring::npos) break;
        pos = nl + 1;
    }
    return false;
}

// Set `key = 'value'` under [section], replacing an existing assignment in place
// or creating the section/key as needed. Minimal TOML editing tuned to xemu's
// flat layout; uses single-quoted literal strings so Windows backslash paths
// need no escaping.
static void TomlSet(std::wstring& doc, const std::wstring& section,
                    const std::wstring& key, const std::wstring& value) {
    const std::wstring newLine = key + L" = '" + value + L"'";

    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= doc.size()) {
        size_t nl = doc.find(L'\n', start);
        if (nl == std::wstring::npos) { lines.push_back(doc.substr(start)); break; }
        lines.push_back(doc.substr(start, nl - start));
        start = nl + 1;
    }

    auto trimmed = [](const std::wstring& s) -> std::wstring {
        size_t a = s.find_first_not_of(L" \t\r");
        return (a == std::wstring::npos) ? std::wstring() : s.substr(a);
    };
    const std::wstring header = L"[" + section + L"]";

    int secStart = -1, secEnd = (int)lines.size();
    for (int i = 0; i < (int)lines.size(); ++i) {
        std::wstring t = trimmed(lines[i]);
        if (secStart < 0) {
            if (t == header) secStart = i;
        } else if (!t.empty() && t[0] == L'[') {
            secEnd = i;
            break;
        }
    }

    auto rejoin = [&]() {
        std::wstring out;
        for (size_t k = 0; k < lines.size(); ++k) {
            out += lines[k];
            if (k + 1 < lines.size()) out += L"\n";
        }
        doc = out;
    };

    if (secStart < 0) {
        if (!doc.empty() && doc.back() != L'\n') doc += L"\n";
        doc += header + L"\n" + newLine + L"\n";
        return;
    }
    for (int i = secStart + 1; i < secEnd; ++i) {
        std::wstring t = trimmed(lines[i]);
        if (t.compare(0, key.size(), key) == 0) {
            size_t j = key.size();
            while (j < t.size() && (t[j] == L' ' || t[j] == L'\t')) j++;
            if (j < t.size() && t[j] == L'=') { lines[i] = newLine; rejoin(); return; }
        }
    }
    lines.insert(lines.begin() + secStart + 1, newLine);
    rejoin();
}

static void SetupXemuFirmware(const std::wstring& archiveUrl,
                              const std::wstring& xemuDestDir,
                              const std::function<void(uint64_t, uint64_t)>& onProgress) {
    // Derive the server base from the emulator archive URL:
    //   http://host:port/emulators/xemu-...zip  ->  http://host:port
    size_t ep = archiveUrl.find(L"/emulators/");
    if (ep == std::wstring::npos) return;
    std::wstring fwBase = archiveUrl.substr(0, ep) + L"/emulators/xemu-firmware/";

    std::wstring fwDir = xemuDestDir + L"\\firmware";
    CreateDirectoryW(fwDir.c_str(), nullptr);

    struct FwFile { const wchar_t* name; bool big; };
    const FwFile files[] = {
        { L"bios.bin",  false },
        { L"mcpx.bin",  false },
        { L"hdd.qcow2", true  },
    };
    for (const auto& f : files) {
        std::wstring dest = fwDir + L"\\" + f.name;
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        if (GetFileAttributesExW(dest.c_str(), GetFileExInfoStandard, &fa)) {
            ULARGE_INTEGER sz; sz.LowPart = fa.nFileSizeLow; sz.HighPart = fa.nFileSizeHigh;
            if (sz.QuadPart > 0) continue;  // already present — don't re-download
        }
        DownloadFile(fwBase + f.name, dest,
                     f.big ? onProgress : std::function<void(uint64_t, uint64_t)>{});
    }

    // Write/patch xemu's config (Roaming AppData) to point at the firmware.
    wchar_t appdata[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH)) return;
    std::wstring cfgDir = std::wstring(appdata) + L"\\xemu\\xemu";
    CreateDirectoryW((std::wstring(appdata) + L"\\xemu").c_str(), nullptr);
    CreateDirectoryW(cfgDir.c_str(), nullptr);
    std::wstring tomlPath = cfgDir + L"\\xemu.toml";

    std::wstring doc;
    HANDLE hr = CreateFileW(tomlPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hr != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER sz{}; GetFileSizeEx(hr, &sz);
        std::string buf((size_t)sz.QuadPart, '\0');
        DWORD rd = 0;
        if (!buf.empty()) ReadFile(hr, buf.data(), (DWORD)buf.size(), &rd, nullptr);
        CloseHandle(hr);
        doc = ToWide(buf);
    }

    TomlSet(doc, L"sys.files", L"bootrom_path",  fwDir + L"\\mcpx.bin");
    TomlSet(doc, L"sys.files", L"flashrom_path", fwDir + L"\\bios.bin");
    TomlSet(doc, L"sys.files", L"hdd_path",      fwDir + L"\\hdd.qcow2");
    // Leave an existing EEPROM untouched (xemu auto-creates one on first boot);
    // only point at a default path when none is configured yet.
    if (!TomlHas(doc, L"eeprom_path"))
        TomlSet(doc, L"sys.files", L"eeprom_path", cfgDir + L"\\eeprom.bin");

    HANDLE hw = CreateFileW(tomlPath.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hw != INVALID_HANDLE_VALUE) {
        std::string out = ToUtf8(doc);
        DWORD wrote = 0;
        WriteFile(hw, out.data(), (DWORD)out.size(), &wrote, nullptr);
        CloseHandle(hw);
    }
}

// ── Worker thread ─────────────────────────────────────────────────────────────

static void Worker(HWND hwnd, int pageIdx, EmulatorDownloadSpec spec,
                   std::wstring appDataDir) {
    auto post = [&](EmuDownloadResult* result) {
        PostMessageW(hwnd, WM_EMUDOWNLOAD_DONE, (WPARAM)pageIdx, (LPARAM)result);
    };
    auto postProgress = [&](uint64_t downloaded, uint64_t total) {
        auto* progress = new EmuDownloadProgress{ downloaded, total };
        if (!PostMessageW(hwnd, WM_EMUDOWNLOAD_PROGRESS, (WPARAM)pageIdx, (LPARAM)progress))
            delete progress;
    };

    std::wstring tag;
    std::wstring assetUrl = spec.directUrl;
    if (assetUrl.empty()) {
        // 1. Fetch GitHub latest release JSON
        std::wstring apiUrl = L"https://api.github.com/repos/" +
                              ToWide(spec.githubRepo) + L"/releases/latest";
        std::string json = HttpGet(apiUrl);
        if (json.empty()) {
            post(new EmuDownloadResult{ L"ERR:Could not reach GitHub API. Check your internet connection." });
            return;
        }

        tag = ParseTagName(json);

        // 2. Find matching asset URL
        assetUrl = FindAssetUrl(json, spec.urlPattern);
        if (assetUrl.empty()) {
            post(new EmuDownloadResult{ L"ERR:No matching download found for \"" + spec.urlPattern + L"\"." });
            return;
        }
    } else if (tag.empty()) {
        tag = spec.urlPattern;
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
    if (!DownloadFile(assetUrl, archivePath, postProgress)) {
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

    // 7. For xemu, provision the Xbox firmware (BIOS/MCPX/HDD) from the server
    //    and write xemu.toml so original Xbox games boot without manual setup.
    if (spec.destName == L"xemu" && !exePath.empty())
        SetupXemuFirmware(assetUrl, destDir, postProgress);

    // 8. Make Dolphin self-contained: a portable.txt next to Dolphin.exe makes
    //    Dolphin keep its config/saves in a "User" folder beside the exe, so the
    //    launcher-managed copy is fully owned by the launcher and isolated from
    //    any manually installed Dolphin. The in-launcher config tab and texture
    //    delivery both resolve this same User dir.
    if (spec.destName == L"dolphin" && !exePath.empty()) {
        std::wstring exeDir = exePath;
        auto sl = exeDir.rfind(L'\\');
        if (sl != std::wstring::npos) exeDir.resize(sl);
        std::wstring marker = exeDir + L"\\portable.txt";
        if (GetFileAttributesW(marker.c_str()) == INVALID_FILE_ATTRIBUTES) {
            HANDLE hf = CreateFileW(marker.c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hf != INVALID_HANDLE_VALUE) CloseHandle(hf);
        }
        CreateDirectoryW((exeDir + L"\\User").c_str(), nullptr);
    }

    post(new EmuDownloadResult{ exePath, tag });
}

// ── Public API ────────────────────────────────────────────────────────────────

void DownloadEmulatorAsync(HWND hwnd, int pageIdx, EmulatorDownloadSpec spec,
                           const std::wstring& appDataDir) {
    std::thread(Worker, hwnd, pageIdx, std::move(spec), appDataDir).detach();
}
