#include "pch.h"
#include "IgdbClient.h"

// ── MiniJson parser ───────────────────────────────────────────────────────────

static const JsonVal g_nullVal{};

const JsonVal& JsonVal::get(const std::string& k) const {
    if (type == Object) {
        auto it = obj.find(k);
        if (it != obj.end()) return it->second;
    }
    return g_nullVal;
}

namespace MiniJson {

static void SkipWs(const std::string& s, size_t& p) {
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n'))
        ++p;
}

static std::string ParseString(const std::string& s, size_t& p) {
    if (p >= s.size() || s[p] != '"') return {};
    ++p;
    std::string out;
    while (p < s.size() && s[p] != '"') {
        if (s[p] == '\\' && p + 1 < s.size()) {
            ++p;
            switch (s[p]) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'u': {
                // \uXXXX - minimal: decode as best effort
                if (p + 4 < s.size()) {
                    char hex[5] = { s[p+1],s[p+2],s[p+3],s[p+4],0 };
                    unsigned cp = (unsigned)strtoul(hex, nullptr, 16);
                    // Encode to UTF-8
                    if (cp < 0x80)        out += (char)cp;
                    else if (cp < 0x800)  { out += (char)(0xC0|(cp>>6)); out += (char)(0x80|(cp&0x3F)); }
                    else                  { out += (char)(0xE0|(cp>>12)); out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
                    p += 4;
                }
                break;
            }
            default: out += s[p]; break;
            }
        } else {
            out += s[p];
        }
        ++p;
    }
    if (p < s.size()) ++p; // closing "
    return out;
}

static JsonVal ParseValue(const std::string& s, size_t& p);

static JsonVal ParseArray(const std::string& s, size_t& p) {
    JsonVal v; v.type = JsonVal::Array;
    ++p; // [
    SkipWs(s, p);
    if (p < s.size() && s[p] == ']') { ++p; return v; }
    while (p < s.size()) {
        SkipWs(s, p);
        v.arr.push_back(ParseValue(s, p));
        SkipWs(s, p);
        if (p < s.size() && s[p] == ',') { ++p; continue; }
        break;
    }
    if (p < s.size() && s[p] == ']') ++p;
    return v;
}

static JsonVal ParseObject(const std::string& s, size_t& p) {
    JsonVal v; v.type = JsonVal::Object;
    ++p; // {
    SkipWs(s, p);
    if (p < s.size() && s[p] == '}') { ++p; return v; }
    while (p < s.size()) {
        SkipWs(s, p);
        if (s[p] != '"') break;
        std::string key = ParseString(s, p);
        SkipWs(s, p);
        if (p < s.size() && s[p] == ':') ++p;
        SkipWs(s, p);
        v.obj[key] = ParseValue(s, p);
        SkipWs(s, p);
        if (p < s.size() && s[p] == ',') { ++p; continue; }
        break;
    }
    if (p < s.size() && s[p] == '}') ++p;
    return v;
}

static JsonVal ParseValue(const std::string& s, size_t& p) {
    SkipWs(s, p);
    if (p >= s.size()) return {};
    if (s[p] == '{') return ParseObject(s, p);
    if (s[p] == '[') return ParseArray(s, p);
    if (s[p] == '"') { JsonVal v; v.type = JsonVal::String; v.str = ParseString(s, p); return v; }
    if (s.substr(p, 4) == "true")  { p += 4; JsonVal v; v.type = JsonVal::Bool; v.num = 1; return v; }
    if (s.substr(p, 5) == "false") { p += 5; JsonVal v; v.type = JsonVal::Bool; v.num = 0; return v; }
    if (s.substr(p, 4) == "null")  { p += 4; return {}; }
    // Number
    JsonVal v; v.type = JsonVal::Number;
    size_t start = p;
    if (p < s.size() && (s[p] == '-' || s[p] == '+')) ++p;
    while (p < s.size() && (isdigit((unsigned char)s[p]) || s[p] == '.' || s[p] == 'e' || s[p] == 'E' || s[p] == '+' || s[p] == '-'))
        ++p;
    v.num = atof(s.substr(start, p - start).c_str());
    return v;
}

JsonVal Parse(const std::string& text) {
    size_t p = 0;
    return ParseValue(text, p);
}

} // namespace MiniJson

// ── IgdbClient ────────────────────────────────────────────────────────────────

IgdbClient::IgdbClient(const std::wstring& clientId, const std::wstring& clientSecret)
    : m_clientId(clientId), m_clientSecret(clientSecret) {}

void IgdbClient::SetCredentials(const std::wstring& clientId, const std::wstring& clientSecret) {
    m_clientId = clientId;
    m_clientSecret = clientSecret;
    m_accessToken.clear();
    m_tokenExpiry = 0;
}

void IgdbClient::RestoreToken(const std::wstring& token, int64_t expiry) {
    m_accessToken = token;
    m_tokenExpiry = expiry;
}

bool IgdbClient::IsAuthenticated() const {
    if (m_accessToken.empty()) return false;
    int64_t now = (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return now < m_tokenExpiry - 60; // 60s safety margin
}

bool IgdbClient::Authenticate() {
    if (IsAuthenticated()) return true;
    if (!HasCredentials()) return false;

    // POST https://id.twitch.tv/oauth2/token
    std::string body =
        "client_id=" + ToUtf8(m_clientId) +
        "&client_secret=" + ToUtf8(m_clientSecret) +
        "&grant_type=client_credentials";

    std::string response;
    if (!Post(L"id.twitch.tv",
              L"/oauth2/token",
              body,
              { L"Content-Type: application/x-www-form-urlencoded" },
              response))
        return false;

    auto json = MiniJson::Parse(response);
    if (!json.has("access_token")) return false;

    m_accessToken = ToWide(json.get("access_token").str);
    int64_t expiresIn = json.has("expires_in") ? json.get("expires_in").i64() : 3600;
    int64_t now = (int64_t)std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_tokenExpiry = now + expiresIn;
    return true;
}

std::vector<IgdbGame> IgdbClient::Search(const std::wstring& title, int limit,
                                          const std::vector<int>& platformIds) {
    if (!IsAuthenticated() && !Authenticate()) return {};

    // Escape double quotes in title
    std::string t = ToUtf8(title);
    std::string escaped;
    for (char c : t) { if (c == '"') escaped += '\\'; escaped += c; }

    std::string body =
        "search \"" + escaped + "\";"
        "fields name,summary,rating,rating_count,first_release_date,cover.image_id,genres.name;";

    // Use release_dates.platform (Retrom's approach) — more reliable than the
    // top-level `platforms` array, which is often missing or stale in IGDB data.
    if (!platformIds.empty()) {
        body += "where release_dates.platform = (";
        for (size_t i = 0; i < platformIds.size(); ++i) {
            if (i) body += ",";
            body += std::to_string(platformIds[i]);
        }
        body += ");";
    }

    body += "limit " + std::to_string(limit) + ";";

    std::string response;
    if (!Post(L"api.igdb.com", L"/v4/games", body, {}, response)) return {};

    auto root = MiniJson::Parse(response);
    if (root.type != JsonVal::Array) return {};

    std::vector<IgdbGame> results;
    for (auto& item : root.arr) {
        if (item.type != JsonVal::Object) continue;
        results.push_back(ParseGameObject(item));
    }
    return results;
}

std::vector<IgdbGame> IgdbClient::FetchGamesByPlatform(int platformId,
                                                       int offset,
                                                       int limit) {
    if (!IsAuthenticated() && !Authenticate()) return {};
    if (limit > 500) limit = 500;

    // Only main releases (version_parent = null excludes DLC / special editions).
    std::string body =
        "fields id,name;"
        "where platforms = (" + std::to_string(platformId) + ")"
        " & version_parent = null;"
        "sort id asc;"
        "limit "  + std::to_string(limit)  + ";"
        "offset " + std::to_string(offset) + ";";

    std::string response;
    if (!Post(L"api.igdb.com", L"/v4/games", body, {}, response)) return {};

    auto root = MiniJson::Parse(response);
    if (root.type != JsonVal::Array) return {};

    std::vector<IgdbGame> results;
    results.reserve(root.arr.size());
    for (auto& item : root.arr) {
        if (item.type != JsonVal::Object) continue;
        IgdbGame g;
        g.id   = item.has("id")   ? item.get("id").i64()   : 0;
        g.name = item.has("name") ? item.get("name").wstr() : L"";
        if (g.id > 0 && !g.name.empty()) results.push_back(std::move(g));
    }
    return results;
}

IgdbGame IgdbClient::FetchById(int64_t id) {
    if (!IsAuthenticated() && !Authenticate()) return {};

    std::string body =
        "fields name,summary,rating,rating_count,first_release_date,cover.image_id,genres.name;"
        "where id = " + std::to_string(id) + ";";

    std::string response;
    if (!Post(L"api.igdb.com", L"/v4/games", body, {}, response)) return {};

    auto root = MiniJson::Parse(response);
    if (root.type != JsonVal::Array || root.arr.empty()) return {};

    return ParseGameObject(root.arr[0]);
}

IgdbGame IgdbClient::ParseGameObject(const JsonVal& obj) {
    IgdbGame g;
    g.id   = obj.has("id")   ? obj.get("id").i64() : 0;
    g.name = obj.has("name") ? obj.get("name").wstr() : L"";
    g.summary = obj.has("summary") ? obj.get("summary").wstr() : L"";
    g.rating  = obj.has("rating")  ? obj.get("rating").num : 0.0;
    g.ratingCount = obj.has("rating_count") ? (int)obj.get("rating_count").num : 0;
    g.firstReleaseDate = obj.has("first_release_date") ?
                         obj.get("first_release_date").i64() : 0;

    // cover.image_id is requested as an embedded object
    if (obj.has("cover")) {
        auto& cv = obj.get("cover");
        if (cv.type == JsonVal::Object && cv.has("image_id"))
            g.coverImageId = cv.get("image_id").wstr();
    }

    // genres
    if (obj.has("genres")) {
        for (auto& gv : obj.get("genres").arr) {
            if (gv.has("name")) g.genres.push_back(gv.get("name").wstr());
        }
    }
    return g;
}


std::wstring IgdbClient::CoverUrl(const std::wstring& imageId, const std::string& size) const {
    return L"https://images.igdb.com/igdb/image/upload/t_" +
           ToWide(size) + L"/" + imageId + L".jpg";
}

bool IgdbClient::DownloadCover(const std::wstring& imageId, const std::wstring& destPath) {
    return Download(CoverUrl(imageId, "cover_big"), destPath);
}

// ── HTTP helpers ──────────────────────────────────────────────────────────────

bool IgdbClient::Post(const std::wstring& host, const std::wstring& path,
                      const std::string& body,
                      const std::vector<std::wstring>& extraHeaders,
                      std::string& response) {
    HINTERNET hSess = WinHttpOpen(L"ArcadeLauncher/1.0",
                                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) return false;

    HINTERNET hConn = WinHttpConnect(hSess, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return false; }

    HINTERNET hReq = WinHttpOpenRequest(hConn, L"POST", path.c_str(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        WINHTTP_FLAG_SECURE);

    bool ok = false;
    if (hReq) {
        // Build headers — no default Content-Type so callers set their own.
        // IGDB query endpoint works without Content-Type; auth endpoint needs
        // application/x-www-form-urlencoded passed explicitly via extraHeaders.
        std::wstring hdrs;
        if (!m_accessToken.empty()) {
            hdrs += L"Client-ID: " + m_clientId + L"\r\n";
            hdrs += L"Authorization: Bearer " + m_accessToken + L"\r\n";
        }
        for (auto& h : extraHeaders) hdrs += h + L"\r\n";

        if (WinHttpSendRequest(hReq,
                               hdrs.c_str(), (DWORD)hdrs.size(),
                               (LPVOID)body.c_str(), (DWORD)body.size(),
                               (DWORD)body.size(), 0)
            && WinHttpReceiveResponse(hReq, nullptr)) {
            DWORD status = 0; DWORD sz = sizeof(status);
            WinHttpQueryHeaders(hReq,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                nullptr, &status, &sz, nullptr);
            if (status == 200) {
                DWORD read = 0;
                char buf[4096];
                while (WinHttpReadData(hReq, buf, sizeof(buf), &read) && read > 0)
                    response.append(buf, read);
                ok = true;
            }
        }
        WinHttpCloseHandle(hReq);
    }
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return ok;
}

bool IgdbClient::Download(const std::wstring& url, const std::wstring& dest) {
    URL_COMPONENTSW uc{}; uc.dwStructSize = sizeof(uc);
    wchar_t host[512]{}, path[2048]{};
    uc.lpszHostName = host; uc.dwHostNameLength = 512;
    uc.lpszUrlPath  = path; uc.dwUrlPathLength  = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    HINTERNET hSess = WinHttpOpen(L"ArcadeLauncher/1.0",
                                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) return false;
    HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return false; }
    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path, nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    bool ok = false;
    if (hReq && WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
             && WinHttpReceiveResponse(hReq, nullptr)) {
        DWORD status = 0; DWORD sz = sizeof(status);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            nullptr, &status, &sz, nullptr);
        if (status == 200) {
            std::vector<BYTE> data;
            DWORD read = 0; BYTE buf[4096];
            while (WinHttpReadData(hReq, buf, sizeof(buf), &read) && read > 0)
                data.insert(data.end(), buf, buf + read);
            if (!data.empty()) {
                HANDLE hf = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr,
                                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hf != INVALID_HANDLE_VALUE) {
                    DWORD written;
                    WriteFile(hf, data.data(), (DWORD)data.size(), &written, nullptr);
                    CloseHandle(hf);
                    ok = true;
                }
            }
        }
        WinHttpCloseHandle(hReq);
    }
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return ok;
}
