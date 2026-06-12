#include "pch.h"
#include "ServerClient.h"
#include "ArchiveExtractor.h"
#include <wincrypt.h>
#include <thread>
#include <chrono>

#pragma comment(lib, "crypt32.lib")

static std::wstring TrimTrailingSlash(std::wstring s) {
    while (!s.empty() && (s.back() == L'/' || s.back() == L'\\')) s.pop_back();
    return s;
}

static bool IsAbsoluteHttpUrl(const std::wstring& s) {
    return s.rfind(L"http://", 0) == 0 || s.rfind(L"https://", 0) == 0;
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

// Extract a flat array of strings: "key":["a","b",...]. Handles backslash escapes
// for quotes; sufficient for IGDB image URLs (which contain no exotic escapes).
static std::vector<std::string> StringsInArray(const std::string& json, const std::string& key) {
    std::vector<std::string> out;
    std::string search = "\"" + key + "\":[";
    size_t p = json.find(search);
    if (p == std::string::npos) return out;
    p += search.size();
    while (p < json.size()) {
        if (json[p] == ']') break;
        if (json[p] != '"') { ++p; continue; }
        ++p; // past opening quote
        std::string s;
        while (p < json.size() && json[p] != '"') {
            if (json[p] == '\\' && p + 1 < json.size()) { s += json[p + 1]; p += 2; }
            else { s += json[p]; ++p; }
        }
        ++p; // past closing quote
        if (!s.empty()) out.push_back(s);
    }
    return out;
}

static Platform ParsePlatform(const std::wstring& p) {
    if (p == L"Steam") return Platform::Steam;
    if (p == L"Epic") return Platform::Epic;
    if (p == L"GOG") return Platform::GOG;
    if (p == L"Dolphin") return Platform::Dolphin;
    if (p == L"GameCube") return Platform::GameCube;
    if (p == L"Wii") return Platform::Wii;
    if (p == L"Ryujinx") return Platform::Ryujinx;
    if (p == L"RPCS3") return Platform::RPCS3;
    if (p == L"N64") return Platform::N64;
    if (p == L"NES") return Platform::NES;
    if (p == L"SNES") return Platform::SNES;
    if (p == L"PS1") return Platform::PS1;
    if (p == L"PS2") return Platform::PS2;
    if (p == L"Xbox360") return Platform::Xbox360;
    if (p == L"Xbox") return Platform::Xbox;
    if (p == L"PC") return Platform::Repacks;        // PC games (formerly "Repacks")
    return Platform::Repacks;                         // legacy "Repacks" + unknown
}

static std::wstring SafeFilePart(std::wstring s) {
    for (auto& c : s) {
        if (c == L'\\' || c == L'/' || c == L':' || c == L'*' ||
            c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|')
            c = L'_';
    }
    return s;
}

// Sanitize a single path component for the standardized install-folder name:
// strip the Windows-illegal characters (\ / : * ? " < > |) and control chars,
// collapse whitespace to underscores, trim leading/trailing dot/space/underscore
// (Windows rejects trailing dots/spaces), and bound the length.
static std::wstring SanitizeNameComponent(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' ||
            c == L'"' || c == L'<' || c == L'>' || c == L'|')
            continue;                       // drop illegal characters outright
        if (c < 0x20) continue;             // drop control characters
        if (c == L' ' || c == L'\t')        // spaces/tabs -> underscore
            { if (out.empty() || out.back() != L'_') out.push_back(L'_'); continue; }
        out.push_back(c);
    }
    while (!out.empty() && (out.back() == L'.' || out.back() == L'_' || out.back() == L' '))
        out.pop_back();
    while (!out.empty() && out.front() == L'_')
        out.erase(out.begin());
    if (out.size() > 80) out.resize(80);
    return out;
}

// Standardized install-folder name: "<sanitized game name>-<id>". The id is the
// server's unique catalog identifier (itself prefixed "<platform>-..."), so the
// folder is human-readable yet collision-free across platforms/titles.
static std::wstring InstallFolderName(const std::wstring& title, const std::wstring& id) {
    std::wstring name = SanitizeNameComponent(title);
    std::wstring safeId = SafeFilePart(id);
    return name.empty() ? safeId : (name + L"-" + safeId);
}

// Resolve a game's install folder under `root`, preferring the standardized name
// but transparently keeping any pre-existing legacy "<id>" folder so installs
// made before this naming change are never orphaned/re-downloaded.
static std::wstring ResolveInstallFolder(const std::wstring& root,
                                         const std::wstring& title,
                                         const std::wstring& id) {
    std::wstring modern = root + L"\\" + InstallFolderName(title, id);
    if (fs::exists(modern)) return modern;
    std::wstring legacy = root + L"\\" + SafeFilePart(id);
    if (fs::exists(legacy)) return legacy;
    return modern;  // fresh install uses the standardized name
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

// Resolve Dolphin's user directory the same way Dolphin itself does on Windows:
// a portable install keeps its data in a "User" folder next to the exe (marked
// by portable.txt or an existing User dir); otherwise it lives under the user's
// Documents folder. `dolphinExe` may be empty, in which case we fall back to the
// Documents location.
static std::wstring DolphinUserDir(const std::wstring& dolphinExe) {
    if (!dolphinExe.empty()) {
        std::wstring exeDir = ParentPath(dolphinExe);
        if (!exeDir.empty()) {
            std::wstring portableMarker = exeDir + L"\\portable.txt";
            std::wstring userDir = exeDir + L"\\User";
            if (fs::exists(portableMarker) || fs::exists(userDir))
                return userDir;
        }
    }
    wchar_t profile[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::wstring(profile) + L"\\Documents\\Dolphin Emulator";
}

// Set HiresTextures = True under [Settings] in Dolphin's GFX.ini so extracted
// custom texture packs are actually loaded. Creates the file/section/key if
// missing and preserves everything else. Best-effort: returns false on I/O
// failure but never throws.
static bool EnableDolphinHiresTextures(const std::wstring& userDir) {
    if (userDir.empty()) return false;
    std::wstring configDir = userDir + L"\\Config";
    std::wstring gfxIni = configDir + L"\\GFX.ini";
    std::error_code ec;
    fs::create_directories(configDir, ec);

    std::vector<std::string> lines;
    {
        std::string content = ToUtf8(ReadTextFile(gfxIni));
        std::string cur;
        for (char c : content) {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else if (c != '\r') cur.push_back(c);
        }
        if (!cur.empty()) lines.push_back(cur);
    }

    auto trim = [](const std::string& s) {
        size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return std::string();
        size_t b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    };

    bool inSettings = false, done = false;
    int settingsHeaderIdx = -1;
    for (size_t i = 0; i < lines.size() && !done; ++i) {
        std::string t = trim(lines[i]);
        if (!t.empty() && t.front() == '[') {
            inSettings = (t == "[Settings]");
            if (inSettings) settingsHeaderIdx = (int)i;
            continue;
        }
        if (inSettings) {
            std::string key = trim(t.substr(0, t.find('=')));
            if (_stricmp(key.c_str(), "HiresTextures") == 0) {
                lines[i] = "HiresTextures = True";
                done = true;
            }
        }
    }
    if (!done) {
        if (settingsHeaderIdx >= 0) {
            lines.insert(lines.begin() + settingsHeaderIdx + 1, "HiresTextures = True");
        } else {
            if (!lines.empty()) lines.push_back("");
            lines.push_back("[Settings]");
            lines.push_back("HiresTextures = True");
        }
    }

    std::string out;
    for (const auto& l : lines) { out += l; out += "\r\n"; }
    return WriteTextFile(gfxIni, ToWide(out));
}

// Read the 6-character Dolphin Game ID from a GameCube/Wii disc image. Raw
// .iso/.gcm images store the ID as ASCII at offset 0; .wbfs images at 0x200.
// Compressed formats (.rvz/.gcz/.wia/.nkit) are not parsed here — those rely on
// a server-supplied gameId instead. Returns "" when the ID can't be determined.
static std::wstring DolphinGameIdFromRom(const std::wstring& romPath) {
    std::wstring ext;
    size_t dot = romPath.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        ext = romPath.substr(dot);
        for (auto& c : ext) c = (wchar_t)towlower(c);
    }
    LONGLONG offset;
    if (ext == L".iso" || ext == L".gcm") offset = 0;
    else if (ext == L".wbfs")             offset = 0x200;
    else return {};

    HANDLE h = CreateFileW(romPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER li; li.QuadPart = offset;
    char id[6]{};
    DWORD read = 0;
    bool ok = SetFilePointerEx(h, li, nullptr, FILE_BEGIN) &&
              ReadFile(h, id, sizeof(id), &read, nullptr) && read == sizeof(id);
    CloseHandle(h);
    if (!ok) return {};

    for (char c : id)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return {};
    return std::wstring(id, id + 6);
}

// If dir contains exactly one entry and it is a directory, return its path;
// otherwise return "". Used to unwrap a zip that already nests everything under
// a single top-level folder.
static std::wstring SingleChildDir(const std::wstring& dir) {
    std::error_code ec;
    std::wstring only;
    int count = 0;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (++count > 1) return {};
        if (!e.is_directory(ec)) return {};
        only = e.path().wstring();
    }
    return count == 1 ? only : std::wstring{};
}

// Move every child of srcDir into dstDir (which must exist). Uses rename and
// falls back to a recursive copy when src/dst straddle volumes (Dolphin's user
// directory often lives on a different drive than the install library).
static bool MoveDirChildrenInto(const std::wstring& srcDir, const std::wstring& dstDir) {
    std::error_code ec;
    bool ok = true;
    for (auto& e : fs::directory_iterator(srcDir, ec)) {
        std::wstring target = dstDir + L"\\" + e.path().filename().wstring();
        std::error_code rec;
        fs::remove_all(target, rec);
        fs::rename(e.path(), target, rec);
        if (rec) {
            std::error_code cec;
            fs::copy(e.path(), target,
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing, cec);
            if (cec) { ok = false; continue; }
            std::error_code dec; fs::remove_all(e.path(), dec);
        }
    }
    return ok;
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

// ─── Challenge-response crypto helpers ───────────────────────────────────────
// Mirror of the server: shared key = SHA-256(lower(username) 0x1f password),
// proof = HMAC-SHA256(key, nonce), token decrypted with HMAC-CTR keystream.
static std::vector<BYTE> Sha256Bytes(const BYTE* data, size_t len) {
    std::vector<BYTE> out;
    HCRYPTPROV prov = 0; HCRYPTHASH h = 0;
    if (!CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return out;
    if (!CryptCreateHash(prov, CALG_SHA_256, 0, 0, &h)) { CryptReleaseContext(prov, 0); return out; }
    if (len == 0 || CryptHashData(h, data, (DWORD)len, 0)) {
        BYTE d[32]{}; DWORD dl = 32;
        if (CryptGetHashParam(h, HP_HASHVAL, d, &dl, 0)) out.assign(d, d + dl);
    }
    CryptDestroyHash(h); CryptReleaseContext(prov, 0);
    return out;
}

static std::vector<BYTE> HmacSha256(const std::vector<BYTE>& key, const std::vector<BYTE>& msg) {
    std::vector<BYTE> k = key;
    if (k.size() > 64) k = Sha256Bytes(k.data(), k.size());
    k.resize(64, 0);
    std::vector<BYTE> inner(64), outer(64);
    for (int i = 0; i < 64; ++i) { inner[i] = k[i] ^ 0x36; outer[i] = k[i] ^ 0x5c; }
    inner.insert(inner.end(), msg.begin(), msg.end());
    std::vector<BYTE> ih = Sha256Bytes(inner.data(), inner.size());
    outer.insert(outer.end(), ih.begin(), ih.end());
    return Sha256Bytes(outer.data(), outer.size());
}

// True if a relative path contains a parent-directory ("..") component, i.e. it
// could escape the install root. Splits on both separators so "a/../b",
// "a\..\b", and a bare ".." are caught, while legitimate names like "foo..bar"
// (no standalone "..") are allowed.
static bool HasPathTraversal(const std::wstring& path) {
    std::wstring cur;
    auto check = [&]() {
        if (cur == L"..") return true;
        cur.clear();
        return false;
    };
    for (wchar_t c : path) {
        if (c == L'/' || c == L'\\') {
            if (check()) return true;
        } else {
            cur.push_back(c);
        }
    }
    return cur == L"..";
}

static std::string HexEncodeBytes(const std::vector<BYTE>& b) {
    static const char* hexd = "0123456789abcdef";
    std::string s; s.reserve(b.size() * 2);
    for (BYTE v : b) { s.push_back(hexd[v >> 4]); s.push_back(hexd[v & 0xf]); }
    return s;
}

static std::vector<BYTE> HexDecodeBytes(const std::string& s) {
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<BYTE> out;
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        int hi = nib(s[i]), lo = nib(s[i + 1]);
        if (hi < 0 || lo < 0) break;
        out.push_back((BYTE)((hi << 4) | lo));
    }
    return out;
}

static std::vector<BYTE> HmacCtrXor(const std::vector<BYTE>& key,
                                    const std::vector<BYTE>& iv,
                                    const std::vector<BYTE>& data) {
    std::vector<BYTE> out; out.reserve(data.size());
    uint32_t counter = 0;
    std::vector<BYTE> block;
    size_t bi = 32;
    for (BYTE d : data) {
        if (bi >= 32) {
            std::vector<BYTE> msg = iv;
            BYTE cb[4] = { (BYTE)(counter >> 24), (BYTE)(counter >> 16), (BYTE)(counter >> 8), (BYTE)counter };
            msg.insert(msg.end(), cb, cb + 4);
            block = HmacSha256(key, msg);
            ++counter; bi = 0;
        }
        out.push_back(d ^ block[bi++]);
    }
    return out;
}

ServerClient::ServerClient(ServerConfig cfg) : m_cfg(std::move(cfg)) {
    m_cfg.baseUrl = TrimTrailingSlash(m_cfg.baseUrl);
    // A base URL stored without a scheme (e.g. "arcade.orlandoaio.net") makes
    // WinHttpCrackUrl reject every request as "Invalid URL". Default to https,
    // except for bare LAN host:port forms which are plain http.
    if (!m_cfg.baseUrl.empty() && !IsAbsoluteHttpUrl(m_cfg.baseUrl)) {
        bool looksLocal = m_cfg.baseUrl.rfind(L"10.", 0) == 0 ||
                          m_cfg.baseUrl.rfind(L"192.168.", 0) == 0 ||
                          m_cfg.baseUrl.rfind(L"127.", 0) == 0 ||
                          m_cfg.baseUrl.rfind(L"localhost", 0) == 0;
        m_cfg.baseUrl = (looksLocal ? L"http://" : L"https://") + m_cfg.baseUrl;
    }
    if (m_cfg.installRoot.empty())
        m_cfg.installRoot = GetAppDataPath() + L"\\server_games";
}

// Translate a WinHTTP failure (GetLastError right after a failed Send/Receive)
// into a human-readable message so "Request failed" stops being a dead end.
static std::wstring WinHttpErrorText(const wchar_t* stage) {
    DWORD e = GetLastError();
    const wchar_t* msg = nullptr;
    switch (e) {
        case ERROR_WINHTTP_TIMEOUT:               msg = L"connection timed out"; break;
        case ERROR_WINHTTP_CANNOT_CONNECT:        msg = L"cannot connect to server"; break;
        case ERROR_WINHTTP_CONNECTION_ERROR:      msg = L"connection error"; break;
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:     msg = L"server name could not be resolved"; break;
        case ERROR_WINHTTP_SECURE_FAILURE:        msg = L"TLS/SSL handshake failed"; break;
        case ERROR_WINHTTP_INVALID_SERVER_RESPONSE: msg = L"invalid server response"; break;
        case ERROR_WINHTTP_OPERATION_CANCELLED:   msg = L"operation cancelled"; break;
        default: break;
    }
    std::wstring out = std::wstring(L"Request failed (") + stage + L": ";
    out += msg ? msg : L"WinHTTP error";
    out += L" " + std::to_wstring(e) + L")";
    return out;
}

std::wstring ServerClient::Url(const std::wstring& path) const {
    if (IsAbsoluteHttpUrl(path)) return path;
    if (!path.empty() && path.front() != L'/')
        return TrimTrailingSlash(m_cfg.baseUrl) + L"/" + path;
    return TrimTrailingSlash(m_cfg.baseUrl) + path;
}

bool ServerClient::HttpGet(const std::wstring& url,
                           std::string& body,
                           std::wstring& error,
                           const std::wstring& rangeHeader,
                           bool allowReauth) {
    if (url.find(L"/api/login") == std::wstring::npos &&
        url.find(L"/api/auth/") == std::wstring::npos &&
        !EnsureAuthenticated(error))
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
    // Explicit, generous timeouts. The defaults (30s send/receive) can trip a
    // long download when the server or proxy stalls briefly; a stalled read
    // then surfaces as a truncated file. Callers retry-with-resume on failure.
    WinHttpSetTimeouts(sess, 30000, 30000, 60000, 60000);

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
        error = WinHttpErrorText(L"GET");
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
        return false;
    }

    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &status, &statusSize, nullptr);
    // A stale/expired bearer token surfaces as 401. Rather than failing the
    // operation (forcing an app restart or a manual re-login), transparently drop
    // the token, re-authenticate with the stored credentials via challenge-
    // response, and retry the request exactly once. Auth endpoints are excluded so
    // a genuinely wrong password can't loop.
    if (status == 401 && allowReauth && !m_cfg.authToken.empty() &&
        url.find(L"/api/login") == std::wstring::npos &&
        url.find(L"/api/auth/") == std::wstring::npos) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
        m_cfg.authToken.clear();
        std::wstring reErr;
        if (!EnsureAuthenticated(reErr)) {
            error = L"Session expired and re-authentication failed: " + reErr;
            return false;
        }
        return HttpGet(url, body, error, rangeHeader, /*allowReauth=*/false);
    }
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
                                std::wstring& error,
                                bool withAuth,
                                bool allowReauth) {
    if (withAuth && !EnsureAuthenticated(error)) return false;
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
    // Explicit, generous timeouts. The defaults (30s send/receive) can trip a
    // long download when the server or proxy stalls briefly; a stalled read
    // then surfaces as a truncated file. Callers retry-with-resume on failure.
    WinHttpSetTimeouts(sess, 30000, 30000, 60000, 60000);
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
    if (withAuth && !m_cfg.authToken.empty())
        headers += L"Authorization: Bearer " + m_cfg.authToken + L"\r\n";
    BOOL ok = WinHttpSendRequest(req, headers.c_str(), (DWORD)-1,
        post.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)post.data(),
        (DWORD)post.size(), (DWORD)post.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);
    if (!ok) {
        error = WinHttpErrorText(L"POST");
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
        return false;
    }

    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &status, &statusSize, nullptr);

    // Read the body regardless of status so we can surface the server's JSON
    // {"error": "..."} message on a 4xx instead of a bare status code.
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

    // Transparent re-auth on a stale token (see HttpGet for rationale). Only for
    // authenticated POSTs — an unauthenticated login POST keeps its 401.
    if (status == 401 && withAuth && allowReauth && !m_cfg.authToken.empty()) {
        m_cfg.authToken.clear();
        std::wstring reErr;
        if (!EnsureAuthenticated(reErr)) {
            error = L"Session expired and re-authentication failed: " + reErr;
            return false;
        }
        return HttpPostForm(url, formBody, body, error, withAuth, /*allowReauth=*/false);
    }
    if (status < 200 || status >= 300) {
        std::string serverMsg = JsonString(body, "error");
        error = serverMsg.empty()
            ? L"Server returned HTTP " + std::to_wstring(status)
            : ToWide(serverMsg);
        return false;
    }
    return true;
}

bool ServerClient::HttpSendRaw(const std::wstring& verb,
                               const std::wstring& url,
                               const std::wstring& contentType,
                               const std::string& data,
                               std::string& body,
                               std::wstring& error) {
    if (!EnsureAuthenticated(error)) return false;
    URL_COMPONENTSW uc{ sizeof(uc) };
    wchar_t host[256]{}, path[2048]{};
    uc.lpszHostName = host; uc.dwHostNameLength = _countof(host);
    uc.lpszUrlPath = path; uc.dwUrlPathLength = _countof(path);
    uc.dwSchemeLength = 1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { error = L"Invalid URL"; return false; }

    bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET sess = WinHttpOpen(L"ArcadeLauncher/ServerClient",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess) { error = L"WinHTTP open failed"; return false; }
    WinHttpSetTimeouts(sess, 30000, 30000, 60000, 60000);
    HINTERNET conn = WinHttpConnect(sess, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
    if (!conn) { WinHttpCloseHandle(sess); error = L"Server connect failed"; return false; }
    HINTERNET req = WinHttpOpenRequest(conn, verb.c_str(), std::wstring(path, uc.dwUrlPathLength).c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { WinHttpCloseHandle(conn); WinHttpCloseHandle(sess); error = L"Request open failed"; return false; }

    std::wstring headers;
    if (!contentType.empty())
        headers += L"Content-Type: " + contentType + L"\r\n";
    if (!m_cfg.authToken.empty())
        headers += L"Authorization: Bearer " + m_cfg.authToken + L"\r\n";

    BOOL ok = WinHttpSendRequest(req,
        headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
        headers.empty() ? 0 : (DWORD)-1,
        data.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)data.data(),
        (DWORD)data.size(), (DWORD)data.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);
    if (!ok) {
        error = WinHttpErrorText(verb.c_str());
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(sess);
        return false;
    }

    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        nullptr, &status, &statusSize, nullptr);
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

    if (status < 200 || status >= 300) {
        std::string serverMsg = JsonString(body, "error");
        error = serverMsg.empty()
            ? L"Server returned HTTP " + std::to_wstring(status)
            : ToWide(serverMsg);
        return false;
    }
    return true;
}

bool ServerClient::TryChallengeResponse(std::wstring& error) {
    // 1. Request a single-use nonce for this user.
    std::string chBody;
    std::wstring chUrl = Url(L"/api/auth/challenge?username=" + PercentEncode(m_cfg.username));
    if (!HttpGet(chUrl, chBody, error)) return false;
    std::string nonce = JsonString(chBody, "nonce");
    if (nonce.empty()) { error = L"Server did not issue an auth challenge"; return false; }

    // 2. Derive the shared key: SHA-256(lower(username) 0x1f password).
    std::string un = ToUtf8(m_cfg.username);
    // trim + ASCII-lowercase to match server's normalization
    size_t a = un.find_first_not_of(" \t\r\n");
    size_t b = un.find_last_not_of(" \t\r\n");
    un = (a == std::string::npos) ? std::string() : un.substr(a, b - a + 1);
    for (char& c : un) if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    std::string pw = ToUtf8(m_cfg.password);
    std::vector<BYTE> keymat(un.begin(), un.end());
    keymat.push_back(0x1f);
    keymat.insert(keymat.end(), pw.begin(), pw.end());
    std::vector<BYTE> key = Sha256Bytes(keymat.data(), keymat.size());

    // 3. proof = HMAC-SHA256(key, nonce)
    std::vector<BYTE> nbytes(nonce.begin(), nonce.end());
    std::string proof = HexEncodeBytes(HmacSha256(key, nbytes));

    // 4. Submit the proof.
    std::wstring form = L"username=" + PercentEncode(m_cfg.username) +
        L"&proof=" + ToWide(proof);
    if (!m_cfg.totpCode.empty())
        form += L"&totpCode=" + PercentEncode(m_cfg.totpCode);
    std::string body;
    if (!HttpPostForm(Url(L"/api/auth/verify"), form, body, error)) return false;

    // 5. Decrypt the returned token.
    std::string ivHex = JsonString(body, "iv");
    std::string tokHex = JsonString(body, "token");
    if (ivHex.empty() || tokHex.empty()) { error = L"Auth verify did not return a token"; return false; }
    std::vector<BYTE> iv = HexDecodeBytes(ivHex);
    std::vector<BYTE> cipher = HexDecodeBytes(tokHex);
    std::vector<BYTE> plain = HmacCtrXor(key, iv, cipher);
    std::string token(plain.begin(), plain.end());
    if (token.empty()) { error = L"Failed to decrypt auth token"; return false; }
    m_cfg.authToken = ToWide(token);
    return true;
}

bool ServerClient::Authenticate(std::wstring& error) {
    return EnsureAuthenticated(error);
}

// Parse a JSON boolean field ("key": true/false). Returns false if absent.
static bool JsonBool(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t p = json.find(search);
    if (p == std::string::npos) return false;
    p += search.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    return json.compare(p, 4, "true") == 0;
}

// Parse a JSON integer field ("key": 123). Returns 0 if absent/non-numeric.
static int64_t JsonInt64(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t p = json.find(search);
    if (p == std::string::npos) return 0;
    p += search.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    size_t start = p;
    if (p < json.size() && (json[p] == '-' || json[p] == '+')) ++p;
    while (p < json.size() && json[p] >= '0' && json[p] <= '9') ++p;
    if (p == start) return 0;
    try { return std::stoll(json.substr(start, p - start)); } catch (...) { return 0; }
}

bool ServerClient::GetAccount(AccountInfo& info, std::wstring& error) {
    if (!EnsureAuthenticated(error)) return false;
    std::string body;
    if (!HttpGet(Url(L"/api/account"), body, error)) return false;
    info.username = ToWide(JsonString(body, "username"));
    info.email = ToWide(JsonString(body, "email"));
    info.isAdmin = JsonBool(body, "isAdmin");
    info.totpEnabled = JsonBool(body, "totpEnabled");
    info.mustChangePassword = JsonBool(body, "mustChangePassword");
    info.avatarVersion = JsonInt64(body, "avatarVersion");
    return true;
}

bool ServerClient::GetAvatar(std::string& bytes, std::wstring& error) {
    if (!EnsureAuthenticated(error)) return false;
    // HttpGet stores the response body verbatim (binary-safe) and reports any
    // non-2xx (incl. 404 "no avatar") as an error the caller can treat as empty.
    return HttpGet(Url(L"/api/account/avatar"), bytes, error);
}

bool ServerClient::UploadAvatar(const std::string& bytes, const std::wstring& contentType,
                                std::wstring& error) {
    if (!EnsureAuthenticated(error)) return false;
    std::string body;
    return HttpSendRaw(L"POST", Url(L"/api/account/avatar"),
                       contentType.empty() ? L"application/octet-stream" : contentType,
                       bytes, body, error);
}

bool ServerClient::DeleteAvatar(std::wstring& error) {
    if (!EnsureAuthenticated(error)) return false;
    std::string body;
    return HttpSendRaw(L"DELETE", Url(L"/api/account/avatar"), L"", {}, body, error);
}

bool ServerClient::ChangePassword(const std::wstring& currentPassword,
                                  const std::wstring& newPassword,
                                  std::wstring& error) {
    if (!EnsureAuthenticated(error)) return false;
    std::wstring form = L"current_password=" + PercentEncode(currentPassword) +
                        L"&new_password=" + PercentEncode(newPassword);
    std::string body;
    if (!HttpPostForm(Url(L"/api/account/password"), form, body, error, true)) return false;
    // The password just changed: any cached token derived from the old password
    // (challenge-response) stays valid, but force a clean re-auth next time so
    // the stored credentials and token stay consistent.
    m_cfg.password = newPassword;
    return true;
}

bool ServerClient::TotpSetup(const std::wstring& password, std::wstring& secret,
                             std::wstring& otpauthUri, std::wstring& error) {
    if (!EnsureAuthenticated(error)) return false;
    std::wstring form = L"password=" + PercentEncode(password);
    std::string body;
    if (!HttpPostForm(Url(L"/api/account/totp/setup"), form, body, error, true)) return false;
    secret = ToWide(JsonString(body, "secret"));
    otpauthUri = ToWide(JsonString(body, "otpauthUri"));
    if (otpauthUri.empty()) { error = L"Server did not return a setup URI"; return false; }
    return true;
}

bool ServerClient::TotpEnable(const std::wstring& code, std::wstring& error) {
    if (!EnsureAuthenticated(error)) return false;
    std::wstring form = L"code=" + PercentEncode(code);
    std::string body;
    return HttpPostForm(Url(L"/api/account/totp/enable"), form, body, error, true);
}

bool ServerClient::TotpDisable(const std::wstring& password, const std::wstring& code,
                               std::wstring& error) {
    if (!EnsureAuthenticated(error)) return false;
    std::wstring form = L"password=" + PercentEncode(password) +
                        L"&code=" + PercentEncode(code);
    std::string body;
    return HttpPostForm(Url(L"/api/account/totp/disable"), form, body, error, true);
}

bool ServerClient::EnsureAuthenticated(std::wstring& error) {
    if (!m_cfg.authToken.empty()) return true;
    if (m_cfg.username.empty() || m_cfg.password.empty()) return true;

    // Prefer challenge-response (password never crosses the wire); fall back to
    // the legacy password login for accounts created before the auth_key column.
    std::wstring chError;
    if (TryChallengeResponse(chError)) return true;

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
        for (const auto& url : StringsInArray(obj, "screenshots"))
            g.screenshots.push_back(ToWide(url));
        g.developer = ToWide(JsonString(obj, "developer"));
        g.publisher = ToWide(JsonString(obj, "publisher"));
        g.franchise = ToWide(JsonString(obj, "franchise"));
        g.installRoot = ResolveInstallFolder(m_cfg.installRoot, g.title, id);
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

    // Optional Dolphin texture pack (GameCube/Wii). Promoted server-side out of
    // the installable file list into its own object, served via /textures/<id>.
    size_t tpPos = body.find("\"texturePack\":");
    if (tpPos != std::string::npos) {
        size_t objStart = body.find('{', tpPos);
        size_t objEnd = objStart == std::string::npos ? std::string::npos : FindObjectEnd(body, objStart);
        if (objEnd != std::string::npos) {
            std::string tp = body.substr(objStart, objEnd - objStart + 1);
            manifest.texturePack.url = ToWide(JsonString(tp, "url"));
            manifest.texturePack.sha256 = ToWide(JsonString(tp, "sha256"));
            manifest.texturePack.size = JsonNumber(tp, "size");
            manifest.texturePack.gameId = ToWide(JsonString(tp, "gameId"));
            manifest.hasTexturePack = !manifest.texturePack.url.empty();
        }
    }
    return !manifest.id.empty();
}

bool ServerClient::FetchChangelogs(const std::wstring& gameId,
                                   std::vector<ChangelogEntry>& out,
                                   std::wstring& error) {
    out.clear();
    std::string body;
    if (!HttpGet(Url(L"/api/games/" + PercentEncode(gameId) + L"/changelogs"),
                 body, error))
        return false;

    for (const auto& obj : ObjectsInArray(body, "changelogs")) {
        ChangelogEntry e;
        e.version   = ToWide(JsonString(obj, "version"));
        e.title     = ToWide(JsonString(obj, "title"));
        e.body      = ToWide(JsonString(obj, "body"));
        e.createdAt = (int64_t)JsonNumber(obj, "createdAt");
        out.push_back(std::move(e));
    }
    return true;
}

bool ServerClient::ListSaves(const std::wstring& serverGameId,
                             std::vector<SaveFileInfo>& out,
                             std::wstring& error) {
    out.clear();
    std::string body;
    if (!HttpGet(Url(L"/api/saves/" + PercentEncode(serverGameId)), body, error))
        return false;
    for (const auto& obj : ObjectsInArray(body, "files")) {
        SaveFileInfo f;
        f.path  = ToWide(JsonString(obj, "path"));
        f.mtime = (int64_t)JsonNumber(obj, "mtime");
        f.size  = (uint64_t)JsonNumber(obj, "size");
        if (!f.path.empty()) out.push_back(std::move(f));
    }
    return true;
}

bool ServerClient::DownloadSaveFile(const std::wstring& serverGameId,
                                    const std::wstring& relPath,
                                    std::string& bytes, std::wstring& error) {
    return HttpGet(Url(L"/api/saves/" + PercentEncode(serverGameId) +
                       L"/file?path=" + PercentEncode(relPath)),
                   bytes, error);
}

bool ServerClient::UploadSaveFile(const std::wstring& serverGameId,
                                  const std::wstring& relPath, int64_t mtime,
                                  const std::string& bytes, std::wstring& error) {
    std::string body;
    return HttpSendRaw(L"PUT",
                       Url(L"/api/saves/" + PercentEncode(serverGameId) +
                           L"/file?path=" + PercentEncode(relPath) +
                           L"&mtime=" + std::to_wstring(mtime)),
                       L"application/octet-stream", bytes, body, error);
}

bool ServerClient::DownloadFile(const std::wstring& url,
                                const std::wstring& dest,
                                uint64_t expectedSize,
                                std::function<void(uint64_t)> onFileProgress,
                                std::wstring& error) {
    std::wstring requestUrl = Url(url);
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
    if (!WinHttpCrackUrl(requestUrl.c_str(), 0, 0, &uc)) {
        error = L"Invalid file URL";
        return false;
    }

    bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET sess = WinHttpOpen(L"ArcadeLauncher/ServerClient",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess) { error = L"WinHTTP open failed"; return false; }
    // Explicit, generous timeouts. The defaults (30s send/receive) can trip a
    // long download when the server or proxy stalls briefly; a stalled read
    // then surfaces as a truncated file. Callers retry-with-resume on failure.
    WinHttpSetTimeouts(sess, 30000, 30000, 60000, 60000);
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
    // Throttle bookkeeping: bytes moved this session vs. wall time elapsed.
    ULONGLONG throttleStart = GetTickCount64();
    uint64_t sessionBytes = 0;
    bool cancelled = false;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
        while (avail > 0) {
            if (DownloadControl::Cancelled().load(std::memory_order_relaxed)) {
                cancelled = true;
                avail = 0;
                break;
            }
            while (DownloadControl::Paused().load(std::memory_order_relaxed) &&
                   !DownloadControl::Cancelled().load(std::memory_order_relaxed)) {
                Sleep(150);
                // Reset the throttle window so the pause itself doesn't count
                // as "time available" for a post-resume burst.
                throttleStart = GetTickCount64();
                sessionBytes = 0;
            }
            DWORD toRead = std::min<DWORD>(avail, (DWORD)buffer.size());
            DWORD read = 0;
            if (!WinHttpReadData(req, buffer.data(), toRead, &read) || read == 0) {
                avail = 0;
                break;
            }
            out.write(buffer.data(), read);
            downloaded += read;
            sessionBytes += read;
            avail -= read;
            if (onFileProgress) onFileProgress(downloaded);

            int limit = DownloadControl::LimitKBps().load(std::memory_order_relaxed);
            if (limit > 0) {
                // Sleep just enough that sessionBytes / elapsed stays at the cap.
                double expectedMs = (double)sessionBytes / ((double)limit * 1024.0) * 1000.0;
                double actualMs = (double)(GetTickCount64() - throttleStart);
                if (expectedMs > actualMs)
                    Sleep((DWORD)std::min(expectedMs - actualMs, 1000.0));
            }
        }
        if (cancelled) break;
    }
    bool writeOk = out.good();
    out.close();
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(sess);

    if (cancelled) {
        // Keep the .part so a later retry resumes where it left off.
        error = DownloadControl::kCancelledError;
        return false;
    }
    if (!writeOk) {
        // A write failure (disk full, etc.) only flips the stream's fail bit —
        // surface it plainly instead of letting it masquerade as a network
        // "size mismatch". The partial .part is left intact so a retry resumes.
        error = L"Disk write failed (out of space?)";
        return false;
    }
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
    uint64_t written = 0;
    size_t startChunk = 0;
    if (fs::exists(part)) {
        uint64_t partialSize = (uint64_t)fs::file_size(part);
        uint64_t boundary = 0;
        bool aligned = partialSize == 0;
        for (size_t i = 0; i < file.chunks.size(); ++i) {
            boundary += file.chunks[i].size;
            if (partialSize == boundary) {
                written = partialSize;
                startChunk = i + 1;
                aligned = true;
                break;
            }
            if (partialSize < boundary) {
                break;
            }
        }
        if (!aligned || partialSize > file.size) {
            DeleteFileW(part.c_str());
            written = 0;
            startChunk = 0;
        }
    }
    if (onFileProgress) onFileProgress(written);

    std::ofstream out(part, std::ios::binary | std::ios::app);
    if (!out) {
        error = L"Could not create partial download";
        return false;
    }

    for (size_t i = startChunk; i < file.chunks.size(); ++i) {
        const auto& chunk = file.chunks[i];
        if (chunk.compression != L"none") {
            error = L"Unsupported chunk compression: " + chunk.compression;
            return false;
        }

        std::wstring chunkPath = part + L".chunk";
        if (!DownloadFile(chunk.url, chunkPath, chunk.size, {}, error)) {
            // A failed chunk download can leave a partial temp file (and its
            // own ".part" resume file) behind. Remove both so the install tree
            // never accumulates strays — Xenia counts every file in a GOD/SVOD
            // ".data" directory and rejects the title when the count is off.
            DeleteFileW(chunkPath.c_str());
            DeleteFileW((chunkPath + L".part").c_str());
            return false;
        }

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

    std::wstring installRoot = ResolveInstallFolder(m_cfg.installRoot, game.title, game.serverGameId);
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
        // Defensive: a manifest path must never escape the install root. The
        // server is trusted, but a compromised/buggy server shouldn't be able to
        // write arbitrary files via "../" components in f.path.
        if (HasPathTraversal(f.path)) {
            result.error = L"Refusing unsafe file path in manifest: " + f.path;
            return result;
        }

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
            // Prefer a single resumable ranged GET of the whole file: one
            // connection (keep-alive friendly), streamed straight to .part, with
            // the authoritative full-file SHA-256 verified below. The per-chunk
            // path opens a fresh HTTPS connection and triple-buffers each chunk to
            // disk, so it's only a fallback for when the server omits a file URL.
            // Retry-with-resume: a transient network/proxy stall truncates the
            // stream, which DownloadFile reports as a size mismatch but leaves
            // the .part intact. Each retry issues a ranged GET that resumes from
            // the partial bytes, so a long download survives brief blips instead
            // of failing the whole install.
            bool downloaded = false;
            for (int attempt = 0; attempt < 4 && !downloaded; ++attempt) {
                if (attempt > 0)
                    std::this_thread::sleep_for(std::chrono::seconds(2 * attempt));
                error.clear();
                downloaded = !f.url.empty()
                    ? DownloadFile(f.url, dest, f.size, progress, error)
                    : DownloadChunkedFile(f, dest, progress, error);
            }
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

    // Every real file is now downloaded and SHA-verified. Sweep any orphaned
    // ".part"/".part.chunk" temp files an interrupted-then-retried download may
    // have left in the tree. Xenia (and other tools) count files in a GOD/SVOD
    // ".data" directory; a single stray file makes the container look corrupt
    // ("expecting N data fragments, but N+k are present"). This also self-heals
    // installs polluted by an older client when the user re-runs Validate&Repair.
    try {
        if (fs::exists(installRoot)) {
            std::vector<fs::path> strays;
            for (const auto& entry : fs::recursive_directory_iterator(installRoot)) {
                if (!entry.is_regular_file()) continue;
                const std::wstring name = entry.path().filename().wstring();
                bool isPart = name.size() >= 5 && name.compare(name.size() - 5, 5, L".part") == 0;
                bool isChunk = name.size() >= 11 && name.compare(name.size() - 11, 11, L".part.chunk") == 0;
                if (isPart || isChunk) strays.push_back(entry.path());
            }
            for (const auto& p : strays) {
                std::error_code ec;
                fs::remove(p, ec);
            }
        }
    } catch (...) {
        // A sweep failure is non-fatal: the real files are intact and verified.
    }

    // Dolphin custom textures (GameCube/Wii). The server advertises an optional
    // texture pack zip; download it, verify, extract into Dolphin's
    // Load/Textures, and flip HiresTextures on so it's actually used. The pack
    // is keyed to the game's version so it re-installs when the pack changes.
    // Failures here are non-fatal — the game itself is already installed.
    if (manifest.hasTexturePack && !manifest.texturePack.url.empty()) {
        const auto& tp = manifest.texturePack;
        std::wstring texMarker = installRoot + L"\\.arcadelauncher-textures";
        std::wstring stamp = manifest.version + L"|" + tp.sha256;
        if (ReadTextFile(texMarker) != stamp) {
            std::wstring zipDest = installRoot + L"\\textures.zip";
            std::wstring texError;
            bool gotZip = false;
            for (int attempt = 0; attempt < 4 && !gotZip; ++attempt) {
                if (attempt > 0)
                    std::this_thread::sleep_for(std::chrono::seconds(2 * attempt));
                texError.clear();
                gotZip = DownloadFile(tp.url, zipDest, tp.size, {}, texError);
            }
            std::wstring zipHash;
            bool zipOk = gotZip && Sha256File(zipDest, zipHash) &&
                         _wcsicmp(zipHash.c_str(), tp.sha256.c_str()) == 0;
            if (zipOk) {
                std::wstring userDir = DolphinUserDir(m_cfg.dolphinPath);
                std::wstring loadTextures = userDir.empty() ? L"" : userDir + L"\\Load\\Textures";
                if (!loadTextures.empty()) {
                    std::error_code ec;
                    fs::create_directories(loadTextures);

                    // Extract to a scratch dir first so we can normalise the
                    // layout into Load/Textures/<GameID>/ (the structure Dolphin
                    // requires) regardless of how the zip is packaged.
                    std::wstring tmpDir = installRoot + L"\\.tex-extract";
                    fs::remove_all(tmpDir, ec);
                    fs::create_directories(tmpDir);

                    if (ExtractArchive(zipDest, tmpDir)) {
                        // Resolve the 6-char Game ID: prefer the server-supplied
                        // value, else read it from the installed ROM header.
                        std::wstring gameId = tp.gameId;
                        if (gameId.empty()) {
                            std::wstring rom = installRoot + L"\\" + manifest.launchTarget;
                            std::replace(rom.begin(), rom.end(), L'/', L'\\');
                            gameId = DolphinGameIdFromRom(rom);
                        }

                        bool placed = false;
                        if (!gameId.empty()) {
                            std::wstring dest = loadTextures + L"\\" + gameId;
                            fs::remove_all(dest, ec);
                            fs::create_directories(dest);
                            // Unwrap a single wrapping folder if the zip already
                            // nested everything (e.g. an existing <GameID>/ dir).
                            std::wstring wrap = SingleChildDir(tmpDir);
                            placed = MoveDirChildrenInto(wrap.empty() ? tmpDir : wrap, dest);
                        } else {
                            // Unknown ID: trust the zip's own layout (it should
                            // already contain the <GameID>/ folder(s)).
                            placed = MoveDirChildrenInto(tmpDir, loadTextures);
                        }

                        if (placed) {
                            EnableDolphinHiresTextures(userDir);
                            WriteTextFile(texMarker, stamp);
                        }
                    }
                    fs::remove_all(tmpDir, ec);
                }
            }
            std::error_code ec;
            fs::remove(zipDest, ec); // cleanup regardless of outcome
        }
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
    bool pcFolder = manifest.installType == L"pc_folder";
    result.arguments = manifest.launchArguments.empty()
        ? (pcFolder ? L"" : L"{rom}")
        : manifest.launchArguments;
    size_t exePh = result.arguments.find(L"{exe}");
    if (exePh != std::wstring::npos) {
        result.arguments.replace(exePh, 5, L"\"" + result.launchPath + L"\"");
    } else {
        size_t romPh = result.arguments.find(L"{rom}");
        if (romPh != std::wstring::npos)
            result.arguments.replace(romPh, 5, L"\"" + result.launchPath + L"\"");
        else if (!pcFolder)
            result.arguments += L" \"" + result.launchPath + L"\"";
    }
    result.version = manifest.version;
    return result;
}

bool ServerClient::UninstallGame(const Game& game, std::wstring& error) {
    if (!game.serverBacked) {
        error = L"Game is not server-backed";
        return false;
    }
    std::wstring root = game.installRoot.empty()
        ? ResolveInstallFolder(m_cfg.installRoot, game.title, game.serverGameId)
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

    std::wstring installRoot = ResolveInstallFolder(m_cfg.installRoot, game.title, game.serverGameId);
    uint64_t total = 0, done = 0;
    for (const auto& f : manifest.files) total += f.size;

    for (const auto& f : manifest.files) {
        if (HasPathTraversal(f.path)) {
            result.badFiles.push_back(f.path);
            result.checkedFiles++;
            continue;
        }
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
