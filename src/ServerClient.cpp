#include "pch.h"
#include "ServerClient.h"
#include "ArchiveExtractor.h"
#include <wincrypt.h>

#pragma comment(lib, "crypt32.lib")

static std::wstring TrimTrailingSlash(std::wstring s) {
    while (!s.empty() && (s.back() == L'/' || s.back() == L'\\')) s.pop_back();
    return s;
}

static std::wstring PercentEncode(const std::wstring& s) {
    std::string u = ToUtf8(s);
    std::wstring out;
    wchar_t buf[4]{};
    for (unsigned char c : u) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back((wchar_t)c);
        } else {
            swprintf_s(buf, L"%%%02X", c);
            out += buf;
        }
    }
    return out;
}

static std::string JsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    size_t p = json.find(search);
    if (p == std::string::npos) return {};
    p += search.size();
    std::string val;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) {
            ++p;
            if (json[p] == 'n') val += '\n';
            else val += json[p];
        } else {
            val += json[p];
        }
        ++p;
    }
    return val;
}

static uint64_t JsonNumber(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t p = json.find(search);
    if (p == std::string::npos) return 0;
    p += search.size();
    while (p < json.size() && json[p] == ' ') ++p;
    uint64_t v = 0;
    while (p < json.size() && isdigit((unsigned char)json[p]))
        v = v * 10 + (json[p++] - '0');
    return v;
}

static double JsonFloat(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t p = json.find(search);
    if (p == std::string::npos) return 0.0;
    p += search.size();
    while (p < json.size() && json[p] == ' ') ++p;
    return strtod(json.c_str() + p, nullptr);
}

static size_t FindObjectEnd(const std::string& raw, size_t start) {
    int depth = 0;
    bool inStr = false;
    for (size_t i = start; i < raw.size(); ++i) {
        char c = raw[i];
        if (inStr) {
            if (c == '\\') { ++i; continue; }
            if (c == '"') inStr = false;
        } else {
            if (c == '"') inStr = true;
            else if (c == '{') ++depth;
            else if (c == '}') { if (--depth == 0) return i; }
        }
    }
    return std::string::npos;
}

static std::vector<std::string> ObjectsInArray(const std::string& json, const std::string& key) {
    std::vector<std::string> out;
    std::string search = "\"" + key + "\":[";
    size_t p = json.find(search);
    if (p == std::string::npos) return out;
    p = json.find('{', p + search.size());
    while (p != std::string::npos) {
        size_t e = FindObjectEnd(json, p);
        if (e == std::string::npos) break;
        out.push_back(json.substr(p, e - p + 1));
        p = json.find('{', e + 1);
        size_t close = json.find(']', e + 1);
        if (close != std::string::npos && (p == std::string::npos || close < p)) break;
    }
    return out;
}

static Platform ParsePlatform(const std::wstring& p) {
    if (p == L"Steam") return Platform::Steam;
    if (p == L"Epic") return Platform::Epic;
    if (p == L"GOG") return Platform::GOG;
    if (p == L"Dolphin") return Platform::Dolphin;
    if (p == L"Ryujinx") return Platform::Ryujinx;
    if (p == L"RPCS3") return Platform::RPCS3;
    if (p == L"N64") return Platform::N64;
    if (p == L"NES") return Platform::NES;
    if (p == L"SNES") return Platform::SNES;
    if (p == L"PS1") return Platform::PS1;
    if (p == L"PS2") return Platform::PS2;
    if (p == L"Xbox360") return Platform::Xbox360;
    if (p == L"Xbox") return Platform::Xbox;
    return Platform::Repacks;
}

static std::wstring SafeFilePart(std::wstring s) {
    for (auto& c : s) {
        if (c == L'\\' || c == L'/' || c == L':' || c == L'*' ||
            c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|')
            c = L'_';
    }
    return s;
}

static std::wstring ParentPath(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"" : path.substr(0, slash);
}

static std::wstring FileNameOnly(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

static bool WriteTextFile(const std::wstring& path, const std::wstring& text) {
    fs::create_directories(ParentPath(path));
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    std::string utf8 = ToUtf8(text);
    out.write(utf8.data(), (std::streamsize)utf8.size());
    return (bool)out;
}

static std::wstring ReadTextFile(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return ToWide(text);
}

static bool IsLikelyInstallerExe(std::wstring name) {
    for (auto& c : name) c = (wchar_t)towlower(c);
    return name.find(L"setup") != std::wstring::npos ||
           name.find(L"install") != std::wstring::npos ||
           name.find(L"unins") != std::wstring::npos ||
           name.find(L"redist") != std::wstring::npos ||
           name.find(L"vcredist") != std::wstring::npos ||
           name.find(L"directx") != std::wstring::npos ||
           name.find(L"dxsetup") != std::wstring::npos;
}

static std::wstring FindPlayableExe(const std::wstring& dir) {
    std::wstring best;
    uint64_t bestSize = 0;
    try {
        if (!fs::exists(dir)) return {};
        for (const auto& entry : fs::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            auto path = entry.path();
            if (_wcsicmp(path.extension().c_str(), L".exe") != 0) continue;
            std::wstring name = path.filename().wstring();
            if (IsLikelyInstallerExe(name)) continue;
            uint64_t size = (uint64_t)entry.file_size();
            if (size > bestSize) {
                bestSize = size;
                best = path.wstring();
            }
        }
    } catch (...) {
        return {};
    }
    return best;
}

static bool Sha256File(const std::wstring& path, std::wstring& hex) {
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return false;
    if (!CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) {
        CryptReleaseContext(prov, 0);
        return false;
    }

    std::ifstream f(path, std::ios::binary);
    std::vector<char> buf(1024 * 1024);
    while (f) {
        f.read(buf.data(), buf.size());
        std::streamsize n = f.gcount();
        if (n > 0 && !CryptHashData(hash, (BYTE*)buf.data(), (DWORD)n, 0)) {
            CryptDestroyHash(hash);
            CryptReleaseContext(prov, 0);
            return false;
        }
    }

    BYTE digest[32]{};
    DWORD len = sizeof(digest);
    bool ok = CryptGetHashParam(hash, HP_HASHVAL, digest, &len, 0);
    CryptDestroyHash(hash);
    CryptReleaseContext(prov, 0);
    if (!ok) return false;

    wchar_t tmp[3]{};
    hex.clear();
    for (DWORD i = 0; i < len; ++i) {
        swprintf_s(tmp, L"%02x", digest[i]);
        hex += tmp;
    }
    return true;
}

ServerClient::ServerClient(ServerConfig cfg) : m_cfg(std::move(cfg)) {
    m_cfg.baseUrl = TrimTrailingSlash(m_cfg.baseUrl);
    if (m_cfg.installRoot.empty())
        m_cfg.installRoot = GetAppDataPath() + L"\\server_games";
}

std::wstring ServerClient::Url(const std::wstring& path) const {
    return TrimTrailingSlash(m_cfg.baseUrl) + path;
}

bool ServerClient::HttpGet(const std::wstring& url,
                           std::string& body,
                           std::wstring& error,
                           const std::wstring& rangeHeader) {
    if (url.find(L"/api/login") == std::wstring::npos && !EnsureAuthenticated(error))
        return false;
    URL_COMPONENTSW uc{ sizeof(uc) };
    wchar_t host[256]{}, path[2048]{};
    uc.lpszHostName = host; uc.dwHostNameLength = _countof(host);
    uc.lpszUrlPath = path; uc.dwUrlPathLength = _countof(path);
    uc.dwSchemeLength = 1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        error = L"Invalid URL";
        return false;
    }

    bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET sess = WinHttpOpen(L"ArcadeLauncher/ServerClient",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess) { error = L"WinHTTP open failed"; return false; }

    HINTERNET conn = WinHttpConnect(sess, std::wstring(host, uc.dwHostNameLength).c_str(),
        uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(sess); error = L"Server connect failed"; return false; }

    std::wstring urlPath(path, uc.dwUrlPathLength);
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", urlPath.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
        error = L"Request open failed";
        return false;
    }

    std::wstring headers;
    if (!m_cfg.authToken.empty())
        headers += L"Authorization: Bearer " + m_cfg.authToken + L"\r\n";
    if (!rangeHeader.empty())
        headers += L"Range: " + rangeHeader + L"\r\n";

    BOOL ok = WinHttpSendRequest(req,
        headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
        headers.empty() ? 0 : (DWORD)-1,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);
    if (!ok) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
        error = L"Request failed";
        return false;
    }

    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &status, &statusSize, nullptr);
    if (status < 200 || status >= 300) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
        error = L"Server returned HTTP " + std::to_wstring(status);
        return false;
    }

    body.clear();
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req, chunk.data(), avail, &read)) break;
        chunk.resize(read);
        body += chunk;
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(sess);
    return true;
}

bool ServerClient::HttpPostForm(const std::wstring& url,
                                const std::wstring& formBody,
                                std::string& body,
                                std::wstring& error) {
    URL_COMPONENTSW uc{ sizeof(uc) };
    wchar_t host[256]{}, path[2048]{};
    uc.lpszHostName = host; uc.dwHostNameLength = _countof(host);
    uc.lpszUrlPath = path; uc.dwUrlPathLength = _countof(path);
    uc.dwSchemeLength = 1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        error = L"Invalid URL";
        return false;
    }

    bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET sess = WinHttpOpen(L"ArcadeLauncher/ServerClient",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess) { error = L"WinHTTP open failed"; return false; }
    HINTERNET conn = WinHttpConnect(sess, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(sess); error = L"Server connect failed"; return false; }
    HINTERNET req = WinHttpOpenRequest(conn, L"POST", std::wstring(path, uc.dwUrlPathLength).c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
        error = L"Request open failed";
        return false;
    }

    std::string post = ToUtf8(formBody);
    std::wstring headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
    BOOL ok = WinHttpSendRequest(req, headers.c_str(), (DWORD)-1,
        post.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)post.data(),
        (DWORD)post.size(), (DWORD)post.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);
    if (!ok) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
        error = L"Request failed";
        return false;
    }

    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &status, &statusSize, nullptr);
    if (status < 200 || status >= 300) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
        error = L"Server returned HTTP " + std::to_wstring(status);
        return false;
    }

    body.clear();
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req, chunk.data(), avail, &read)) break;
        chunk.resize(read);
        body += chunk;
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(sess);
    return true;
}

bool ServerClient::EnsureAuthenticated(std::wstring& error) {
    if (!m_cfg.authToken.empty()) return true;
    if (m_cfg.username.empty() || m_cfg.password.empty()) return true;

    std::wstring form = L"username=" + PercentEncode(m_cfg.username) +
        L"&password=" + PercentEncode(m_cfg.password);
    if (!m_cfg.totpCode.empty())
        form += L"&totpCode=" + PercentEncode(m_cfg.totpCode);
    std::string body;
    if (!HttpPostForm(Url(L"/api/login"), form, body, error))
        return false;
    std::wstring token = ToWide(JsonString(body, "token"));
    if (token.empty()) {
        error = L"Server login did not return a token";
        return false;
    }
    m_cfg.authToken = token;
    return true;
}

bool ServerClient::FetchCatalog(std::vector<Game>& games, std::wstring& error) {
    std::string body;
    if (!HttpGet(Url(L"/api/catalog"), body, error)) return false;

    games.clear();
    for (const auto& obj : ObjectsInArray(body, "games")) {
        std::wstring id = ToWide(JsonString(obj, "id"));
        if (id.empty()) continue;

        Game g;
        g.id = L"Server_" + id;
        g.serverBacked = true;
        g.serverGameId = id;
        g.title = ToWide(JsonString(obj, "title"));
        g.platform = ParsePlatform(ToWide(JsonString(obj, "platform")));
        g.coverArtUrl = ToWide(JsonString(obj, "coverArtUrl"));
        g.igdbId = (int64_t)JsonNumber(obj, "igdbId");
        g.igdbMatched = g.igdbId > 0;
        g.summary = ToWide(JsonString(obj, "summary"));
        g.genres = ToWide(JsonString(obj, "genres"));
        g.igdbRating = (float)JsonFloat(obj, "igdbRating");
        g.releaseDate = (int64_t)JsonNumber(obj, "releaseDate");
        g.serverVersion = ToWide(JsonString(obj, "version"));
        g.installRoot = m_cfg.installRoot + L"\\" + SafeFilePart(id);
        g.installState = fs::exists(g.installRoot) ? InstallState::Installed : InstallState::Missing;
        games.push_back(std::move(g));
    }
    return true;
}

bool ServerClient::FetchManifest(const std::wstring& gameId,
                                 ServerGameManifest& manifest,
                                 std::wstring& error) {
    std::string body;
    if (!HttpGet(Url(L"/api/games/" + PercentEncode(gameId) + L"/manifest"), body, error))
        return false;

    manifest = {};
    manifest.id = ToWide(JsonString(body, "id"));
    manifest.title = ToWide(JsonString(body, "title"));
    manifest.platform = ToWide(JsonString(body, "platform"));
    manifest.installType = ToWide(JsonString(body, "installType"));
    manifest.version = ToWide(JsonString(body, "version"));

    size_t launchPos = body.find("\"launch\":");
    if (launchPos != std::string::npos) {
        size_t objStart = body.find('{', launchPos);
        size_t objEnd = objStart == std::string::npos ? std::string::npos : FindObjectEnd(body, objStart);
        if (objEnd != std::string::npos) {
            std::string launch = body.substr(objStart, objEnd - objStart + 1);
            manifest.launchTarget = ToWide(JsonString(launch, "target"));
            manifest.launchArguments = ToWide(JsonString(launch, "arguments"));
        }
    }

    for (const auto& obj : ObjectsInArray(body, "files")) {
        ServerFileEntry f;
        f.path = ToWide(JsonString(obj, "path"));
        f.url = ToWide(JsonString(obj, "url"));
        f.sha256 = ToWide(JsonString(obj, "sha256"));
        f.size = JsonNumber(obj, "size");
        for (const auto& chunkObj : ObjectsInArray(obj, "chunks")) {
            ServerFileEntry::Chunk c;
            c.index = (int)JsonNumber(chunkObj, "index");
            c.offset = JsonNumber(chunkObj, "offset");
            c.size = JsonNumber(chunkObj, "size");
            c.url = ToWide(JsonString(chunkObj, "url"));
            c.sha256 = ToWide(JsonString(chunkObj, "sha256"));
            c.compression = ToWide(JsonString(chunkObj, "compression"));
            if (!c.url.empty() && c.compression == L"none")
                f.chunks.push_back(std::move(c));
        }
        if (!f.path.empty()) manifest.files.push_back(std::move(f));
    }
    return !manifest.id.empty();
}

bool ServerClient::DownloadFile(const std::wstring& url,
                                const std::wstring& dest,
                                uint64_t expectedSize,
                                std::function<void(uint64_t)> onFileProgress,
                                std::wstring& error) {
    std::wstring part = dest + L".part";
    fs::create_directories(ParentPath(dest));

    uint64_t existing = 0;
    if (fs::exists(part)) existing = (uint64_t)fs::file_size(part);
    if (existing > expectedSize) {
        DeleteFileW(part.c_str());
        existing = 0;
    }

    std::wstring range;
    if (existing > 0) range = L"bytes=" + std::to_wstring(existing) + L"-";

    URL_COMPONENTSW uc{ sizeof(uc) };
    wchar_t host[256]{}, path[2048]{};
    uc.lpszHostName = host; uc.dwHostNameLength = _countof(host);
    uc.lpszUrlPath = path; uc.dwUrlPathLength = _countof(path);
    uc.dwSchemeLength = 1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        error = L"Invalid file URL";
        return false;
    }

    bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET sess = WinHttpOpen(L"ArcadeLauncher/ServerClient",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess) { error = L"WinHTTP open failed"; return false; }
    HINTERNET conn = WinHttpConnect(sess, std::wstring(host, uc.dwHostNameLength).c_str(),
        uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(sess); error = L"Server connect failed"; return false; }
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", std::wstring(path, uc.dwUrlPathLength).c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) {
        WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
        error = L"Request open failed";
        return false;
    }

    std::wstring headers;
    if (!m_cfg.authToken.empty())
        headers += L"Authorization: Bearer " + m_cfg.authToken + L"\r\n";
    if (!range.empty())
        headers += L"Range: " + range + L"\r\n";

    BOOL ok = WinHttpSendRequest(req,
        headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
        headers.empty() ? 0 : (DWORD)-1,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);
    if (!ok) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
        error = L"Download request failed";
        return false;
    }

    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &status, &statusSize, nullptr);
    if (status == 200 && existing > 0) {
        DeleteFileW(part.c_str());
        existing = 0;
    } else if (status != 200 && status != 206) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
        error = L"Download returned HTTP " + std::to_wstring(status);
        return false;
    }

    std::ofstream out(part, std::ios::binary | std::ios::app);
    uint64_t downloaded = existing;
    std::vector<char> buffer(1024 * 1024);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
        while (avail > 0) {
            DWORD toRead = std::min<DWORD>(avail, (DWORD)buffer.size());
            DWORD read = 0;
            if (!WinHttpReadData(req, buffer.data(), toRead, &read) || read == 0) {
                avail = 0;
                break;
            }
            out.write(buffer.data(), read);
            downloaded += read;
            avail -= read;
            if (onFileProgress) onFileProgress(downloaded);
        }
    }
    out.close();
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(sess);

    if ((uint64_t)fs::file_size(part) != expectedSize) {
        error = L"Downloaded size mismatch";
        return false;
    }

    MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    return true;
}

bool ServerClient::DownloadChunkedFile(const ServerFileEntry& file,
                                       const std::wstring& dest,
                                       std::function<void(uint64_t)> onFileProgress,
                                       std::wstring& error) {
    if (file.chunks.empty()) {
        error = L"No chunks available";
        return false;
    }

    std::wstring part = dest + L".part";
    fs::create_directories(ParentPath(dest));
    DeleteFileW(part.c_str());

    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = L"Could not create partial download";
        return false;
    }

    uint64_t written = 0;
    for (const auto& chunk : file.chunks) {
        if (chunk.compression != L"none") {
            error = L"Unsupported chunk compression: " + chunk.compression;
            return false;
        }

        std::wstring chunkPath = part + L".chunk";
        if (!DownloadFile(chunk.url, chunkPath, chunk.size, {}, error))
            return false;

        std::wstring hash;
        if (!Sha256File(chunkPath, hash) || _wcsicmp(hash.c_str(), chunk.sha256.c_str()) != 0) {
            DeleteFileW(chunkPath.c_str());
            error = L"Chunk verification failed: " + file.path;
            return false;
        }

        std::ifstream in(chunkPath, std::ios::binary);
        if (!in) {
            DeleteFileW(chunkPath.c_str());
            error = L"Could not read downloaded chunk";
            return false;
        }
        out << in.rdbuf();
        out.flush();
        DeleteFileW(chunkPath.c_str());

        written += chunk.size;
        if (onFileProgress) onFileProgress(written);
    }

    out.close();
    if ((uint64_t)fs::file_size(part) != file.size) {
        error = L"Chunked download size mismatch";
        return false;
    }
    MoveFileExW(part.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    return true;
}

ServerInstallResult ServerClient::InstallGame(const Game& game,
    std::function<void(uint64_t, uint64_t)> onProgress) {
    ServerInstallResult result;
    if (!game.serverBacked) {
        result.error = L"Game is not server-backed";
        return result;
    }

    std::wstring error;
    ServerGameManifest manifest;
    if (!FetchManifest(game.serverGameId, manifest, error)) {
        result.error = error.empty() ? L"Manifest fetch failed" : error;
        return result;
    }

    std::wstring installRoot = m_cfg.installRoot + L"\\" + SafeFilePart(game.serverGameId);
    bool pcArchive = manifest.installType == L"pc_archive";
    std::wstring extractedRoot = installRoot + L"\\installed";
    std::wstring markerPath = installRoot + L"\\.arcadelauncher-version";

    if (pcArchive && ReadTextFile(markerPath) == manifest.version) {
        std::wstring exe = FindPlayableExe(extractedRoot);
        if (!exe.empty()) {
            result.ok = true;
            result.installRoot = installRoot;
            result.launchPath = exe;
            result.arguments.clear();
            result.version = manifest.version;
            return result;
        }
    }

    uint64_t total = 0, done = 0;
    for (const auto& f : manifest.files) total += f.size;

    for (const auto& f : manifest.files) {
        std::wstring dest = installRoot + L"\\" + f.path;
        std::replace(dest.begin(), dest.end(), L'/', L'\\');

        bool needDownload = true;
        if (fs::exists(dest) && (uint64_t)fs::file_size(dest) == f.size) {
            std::wstring hash;
            if (Sha256File(dest, hash) && _wcsicmp(hash.c_str(), f.sha256.c_str()) == 0)
                needDownload = false;
        }

        if (needDownload) {
            auto progress = [&](uint64_t fileDone) {
                if (onProgress) onProgress(done + fileDone, total);
            };
            bool downloaded = !f.chunks.empty()
                ? DownloadChunkedFile(f, dest, progress, error)
                : DownloadFile(f.url, dest, f.size, progress, error);
            if (!downloaded) {
                result.error = error;
                return result;
            }
            std::wstring hash;
            if (!Sha256File(dest, hash) || _wcsicmp(hash.c_str(), f.sha256.c_str()) != 0) {
                result.error = L"SHA-256 verification failed: " + f.path;
                return result;
            }
        }

        done += f.size;
        if (onProgress) onProgress(done, total);
    }

    if (pcArchive) {
        fs::remove_all(extractedRoot);
        fs::create_directories(extractedRoot);
        for (const auto& f : manifest.files) {
            std::wstring archivePath = installRoot + L"\\" + f.path;
            std::replace(archivePath.begin(), archivePath.end(), L'/', L'\\');
            if (!ExtractArchive(archivePath, extractedRoot)) {
                result.error = L"Archive extraction failed: " + FileNameOnly(archivePath);
                return result;
            }
        }

        std::wstring exe = FindPlayableExe(extractedRoot);
        if (exe.empty()) {
            result.error = L"No playable EXE found after extracting the archive";
            return result;
        }

        WriteTextFile(markerPath, manifest.version);
        result.ok = true;
        result.installRoot = installRoot;
        result.launchPath = exe;
        result.arguments.clear();
        result.version = manifest.version;
        return result;
    }

    result.ok = true;
    result.installRoot = installRoot;
    result.launchPath = installRoot + L"\\" + manifest.launchTarget;
    std::replace(result.launchPath.begin(), result.launchPath.end(), L'/', L'\\');
    result.arguments = manifest.launchArguments.empty() ? L"{rom}" : manifest.launchArguments;
    size_t ph = result.arguments.find(L"{rom}");
    if (ph != std::wstring::npos)
        result.arguments.replace(ph, 5, L"\"" + result.launchPath + L"\"");
    else
        result.arguments += L" \"" + result.launchPath + L"\"";
    result.version = manifest.version;
    return result;
}

bool ServerClient::UninstallGame(const Game& game, std::wstring& error) {
    if (!game.serverBacked) {
        error = L"Game is not server-backed";
        return false;
    }
    std::wstring root = game.installRoot.empty()
        ? m_cfg.installRoot + L"\\" + SafeFilePart(game.serverGameId)
        : game.installRoot;
    if (root.empty() || !fs::exists(root))
        return true;
    try {
        fs::remove_all(root);
        return true;
    } catch (...) {
        error = L"Could not remove installed game files: " + root;
        return false;
    }
}

ServerValidateResult ServerClient::ValidateGame(const Game& game,
    std::function<void(uint64_t, uint64_t)> onProgress) {
    ServerValidateResult result;
    if (!game.serverBacked) {
        result.error = L"Game is not server-backed";
        return result;
    }

    std::wstring error;
    ServerGameManifest manifest;
    if (!FetchManifest(game.serverGameId, manifest, error)) {
        result.error = error.empty() ? L"Manifest fetch failed" : error;
        return result;
    }

    std::wstring installRoot = m_cfg.installRoot + L"\\" + SafeFilePart(game.serverGameId);
    uint64_t total = 0, done = 0;
    for (const auto& f : manifest.files) total += f.size;

    for (const auto& f : manifest.files) {
        std::wstring dest = installRoot + L"\\" + f.path;
        std::replace(dest.begin(), dest.end(), L'/', L'\\');

        bool bad = false;
        if (!fs::exists(dest)) {
            result.missingFiles.push_back(f.path);
        } else if ((uint64_t)fs::file_size(dest) != f.size) {
            bad = true;
        } else {
            std::wstring hash;
            if (!Sha256File(dest, hash) || _wcsicmp(hash.c_str(), f.sha256.c_str()) != 0)
                bad = true;
        }

        if (bad)
            result.badFiles.push_back(f.path);

        result.checkedFiles++;
        result.checkedBytes += f.size;
        done += f.size;
        if (onProgress) onProgress(done, total);
    }

    result.ok = result.missingFiles.empty() && result.badFiles.empty();
    if (!result.ok) {
        result.error = std::to_wstring(result.missingFiles.size()) + L" missing, " +
            std::to_wstring(result.badFiles.size()) + L" failed validation";
    }
    return result;
}
