#include "pch.h"
#include "GameLibrary.h"
#include <set>
#include <unordered_set>

void GameLibrary::AddGame(Game game) {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto& g : m_games)
        if (g.id == game.id) { g = std::move(game); return; }
    m_games.push_back(std::move(game));
}

void GameLibrary::RemoveGame(const std::wstring& id) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_games.erase(std::remove_if(m_games.begin(), m_games.end(),
        [&](const Game& g){ return g.id == id; }), m_games.end());
}

void GameLibrary::RemoveServerClientLocalEntries() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_games.erase(std::remove_if(m_games.begin(), m_games.end(),
        [](const Game& g) {
            if (g.serverBacked) return false;
            return g.platform != Platform::Steam &&
                   g.platform != Platform::Epic &&
                   g.platform != Platform::GOG;
        }), m_games.end());
}

void GameLibrary::UpdateGame(const Game& game) {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto& g : m_games)
        if (g.id == game.id) { g = game; return; }
}

void GameLibrary::MergeGames(std::vector<Game> scanned) {
    std::lock_guard<std::mutex> lk(m_mutex);

    std::vector<Game> deduped;
    std::unordered_set<std::wstring> seenIds;
    deduped.reserve(scanned.size());
    for (auto& s : scanned) {
        if (seenIds.insert(s.id).second)
            deduped.push_back(std::move(s));
    }
    scanned = std::move(deduped);

    // Identify which platforms are covered by this scan.
    std::unordered_map<std::wstring, const Game*> oldById;
    std::set<int> scannedPlatforms;
    for (auto& s : scanned)
        scannedPlatforms.insert(static_cast<int>(s.platform));

    // Build an ID → old-game map for every entry in those platforms so we
    // can transplant playtime / IGDB metadata onto the fresh entries.
    for (auto& g : m_games)
        if (scannedPlatforms.count(static_cast<int>(g.platform)))
            oldById[g.id] = &g;

    // Transplant metadata from old entries into fresh scan results.
    for (auto& s : scanned) {
        auto it = oldById.find(s.id);
        if (it != oldById.end()) {
            const Game* old = it->second;
            s.playtimeSeconds = old->playtimeSeconds;
            s.lastPlayed      = old->lastPlayed;
            s.coverArtPath    = old->coverArtPath;
            s.igdbId          = old->igdbId;
            s.igdbMatched     = old->igdbMatched;
            s.summary         = old->summary;
            s.genres          = old->genres;
            s.igdbRating      = old->igdbRating;
            s.releaseDate     = old->releaseDate;
            s.igdbPlatformId  = old->igdbPlatformId;
            s.launchOptions   = old->launchOptions;
            s.collections     = old->collections;

            // Preserve local install / launch state for server-backed games.
            // FetchCatalog only knows the catalog (title, version, installRoot
            // by convention) — it has no idea about an in-flight download or the
            // resolved launch path (romPath/exePath come back EMPTY). Without
            // this, a re-sync (10-min timer or focus-regain) wipes those: a
            // downloading game reverts mid-flight and an installed game becomes
            // unlaunchable. Keep the worker's progress untouched.
            if (s.serverBacked && old->serverBacked) {
                if (!old->romPath.empty())     s.romPath   = old->romPath;
                if (!old->exePath.empty())     s.exePath   = old->exePath;
                if (!old->arguments.empty())   s.arguments = old->arguments;
                if (!old->installRoot.empty()) s.installRoot = old->installRoot;

                if (old->installState == InstallState::Downloading) {
                    // A background worker is mid-flight — never disturb it.
                    s.installState = InstallState::Downloading;
                    s.installProgressPermille = old->installProgressPermille;
                } else if (old->installState == InstallState::Installed ||
                           old->installState == InstallState::UpdateAvailable) {
                    // Keep it installed; flag an update if the catalog version
                    // moved past the locally-installed version.
                    bool updated = !s.serverVersion.empty() &&
                                   !old->serverVersion.empty() &&
                                   s.serverVersion != old->serverVersion;
                    s.installState = updated ? InstallState::UpdateAvailable
                                             : InstallState::Installed;
                    if (!updated) s.serverVersion = old->serverVersion;
                }
                // else: leave the fresh Missing/Local state as-is.
            }
        }
    }

    // Remove ALL stale entries for scanned platforms — including old alternates
    // and duplicate accumulations — then add the freshly-deduplicated results.
    m_games.erase(std::remove_if(m_games.begin(), m_games.end(),
        [&](const Game& g) {
            return scannedPlatforms.count(static_cast<int>(g.platform)) > 0;
        }), m_games.end());

    for (auto& s : scanned)
        m_games.push_back(std::move(s));
}

std::vector<const Game*> GameLibrary::Filter(Platform p) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    std::vector<const Game*> out;
    for (auto& g : m_games)
        if (g.platform == p) out.push_back(&g);
    return out;
}

std::vector<const Game*> GameLibrary::Search(const std::wstring& query) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    std::wstring lq = query;
    for (auto& c : lq) c = towlower(c);

    std::vector<const Game*> out;
    for (auto& g : m_games) {
        std::wstring lt = g.title;
        for (auto& c : lt) c = towlower(c);
        if (lt.find(lq) != std::wstring::npos)
            out.push_back(&g);
    }
    return out;
}

Game* GameLibrary::FindById(const std::wstring& id) {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto& g : m_games)
        if (g.id == id) return &g;
    return nullptr;
}

// ── Persistence (hand-rolled JSON) ───────────────────────────────────────────

static std::wstring JEsc(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        if (c == L'"')  { o += L"\\\""; }
        else if (c == L'\\') { o += L"\\\\"; }
        else if (c == L'\n') { o += L"\\n"; }
        else o += c;
    }
    return o;
}

static std::wstring JField(const std::wstring& key, const std::wstring& val) {
    return L"\"" + key + L"\":\"" + JEsc(val) + L"\"";
}

static std::wstring JFieldN(const std::wstring& key, uint64_t val) {
    return L"\"" + key + L"\":" + std::to_wstring(val);
}

static std::wstring JFieldF(const std::wstring& key, float val) {
    // Write as integer (IGDB ratings are 0-100)
    return L"\"" + key + L"\":" + std::to_wstring((int)(val * 100)) + L"e-2";
}

static std::wstring JFieldB(const std::wstring& key, bool val) {
    return L"\"" + key + L"\":" + (val ? L"true" : L"false");
}

static std::wstring InstallStateName(InstallState s) {
    switch (s) {
    case InstallState::Missing:         return L"Missing";
    case InstallState::Downloading:     return L"Downloading";
    case InstallState::Installed:       return L"Installed";
    case InstallState::UpdateAvailable: return L"UpdateAvailable";
    default:                            return L"Local";
    }
}

static InstallState ParseInstallState(const std::string& s) {
    if (s == "Missing")         return InstallState::Missing;
    if (s == "Downloading")     return InstallState::Downloading;
    if (s == "Installed")       return InstallState::Installed;
    if (s == "UpdateAvailable") return InstallState::UpdateAvailable;
    return InstallState::Local;
}

static std::string ReadJsonField(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    size_t p = json.find(search);
    if (p == std::string::npos) return {};
    p += search.size();
    std::string val;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) {
            ++p;
            if (json[p] == 'n') val += '\n';
            else if (json[p] == '\\') val += '\\';
            else if (json[p] == '"') val += '"';
            else val += json[p];
        } else val += json[p];
        ++p;
    }
    return val;
}

static uint64_t ReadJsonNum(const std::string& json, const std::string& key) {
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

void GameLibrary::Save(const std::wstring& path) const {
    std::lock_guard<std::mutex> lk(m_mutex);
    std::wstring out = L"[\n";
    for (size_t i = 0; i < m_games.size(); ++i) {
        auto& g = m_games[i];
        out += L"  {";
        out += JField(L"id", g.id) + L",";
        out += JField(L"title", g.title) + L",";
        out += JField(L"platform", PlatformName(g.platform)) + L",";
        out += JField(L"launchUri", g.launchUri) + L",";
        out += JField(L"exePath", g.exePath) + L",";
        out += JField(L"arguments", g.arguments) + L",";
        out += JField(L"emulatorPath", g.emulatorPath) + L",";
        out += JField(L"romPath", g.romPath) + L",";
        out += JFieldB(L"serverBacked", g.serverBacked) + L",";
        out += JField(L"serverGameId", g.serverGameId) + L",";
        out += JField(L"serverVersion", g.serverVersion) + L",";
        out += JField(L"installRoot", g.installRoot) + L",";
        out += JField(L"installState", InstallStateName(g.installState)) + L",";
        out += JField(L"coverArtPath", g.coverArtPath) + L",";
        out += JField(L"coverArtUrl", g.coverArtUrl) + L",";
        out += JField(L"steamAppId", g.steamAppId) + L",";
        out += JField(L"epicAppName", g.epicAppName) + L",";
        out += JField(L"gogGameId", g.gogGameId) + L",";
        out += JFieldN(L"playtimeSeconds", g.playtimeSeconds) + L",";
        out += JFieldN(L"lastPlayed", (uint64_t)g.lastPlayed) + L",";
        out += JFieldN(L"igdbId", (uint64_t)g.igdbId) + L",";
        out += JFieldB(L"igdbMatched", g.igdbMatched) + L",";
        out += JField(L"summary", g.summary) + L",";
        out += JField(L"genres", g.genres) + L",";
        out += JFieldF(L"igdbRating", g.igdbRating) + L",";
        out += JFieldN(L"releaseDate", (uint64_t)g.releaseDate) + L",";
        out += JFieldN(L"igdbPlatformId", (uint64_t)g.igdbPlatformId) + L",";
        out += JField(L"launchOptions", g.launchOptions) + L",";
        // Collections joined by newline (JEsc escapes \n; ReadJsonField reverses).
        std::wstring colJoined;
        for (size_t c = 0; c < g.collections.size(); ++c) {
            if (c) colJoined += L"\n";
            colJoined += g.collections[c];
        }
        out += JField(L"collections", colJoined);
        out += L"}";
        if (i + 1 < m_games.size()) out += L",";
        out += L"\n";
    }
    out += L"]\n";

    // Write as UTF-8
    std::string utf8 = ToUtf8(out);
    std::ofstream f(path, std::ios::binary);
    f.write(utf8.data(), utf8.size());
}

// Find the matching closing '}' for the '{' at `start`, correctly
// skipping braces that appear inside quoted JSON strings.
static size_t FindObjectEnd(const std::string& raw, size_t start) {
    int depth = 0;
    bool inStr = false;
    for (size_t i = start; i < raw.size(); ++i) {
        char c = raw[i];
        if (inStr) {
            if (c == '\\') { ++i; continue; }  // skip escaped char
            if (c == '"')  inStr = false;
        } else {
            if      (c == '"') inStr = true;
            else if (c == '{') ++depth;
            else if (c == '}') { if (--depth == 0) return i; }
        }
    }
    return std::string::npos;
}

void GameLibrary::Load(const std::wstring& path) {
    std::ifstream f(path);
    if (!f) return;
    std::string raw((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());

    std::lock_guard<std::mutex> lk(m_mutex);
    m_games.clear();

    // Walk the JSON array one top-level object at a time.
    // FindObjectEnd handles '}' characters inside string values
    // (e.g. from IGDB summaries), which raw find('}') could not.
    size_t pos = 0;
    while (true) {
        size_t start = raw.find('{', pos);
        if (start == std::string::npos) break;
        size_t end = FindObjectEnd(raw, start);
        if (end == std::string::npos) break;
        std::string obj = raw.substr(start, end - start + 1);
        pos = end + 1;

        Game g;
        g.id           = ToWide(ReadJsonField(obj, "id"));
        if (g.id.empty()) continue;
        g.title        = ToWide(ReadJsonField(obj, "title"));
        g.launchUri    = ToWide(ReadJsonField(obj, "launchUri"));
        g.exePath      = ToWide(ReadJsonField(obj, "exePath"));
        g.arguments    = ToWide(ReadJsonField(obj, "arguments"));
        g.emulatorPath = ToWide(ReadJsonField(obj, "emulatorPath"));
        g.romPath      = ToWide(ReadJsonField(obj, "romPath"));
        g.serverBacked = false;
        {
            std::string search = "\"serverBacked\":";
            size_t bp = obj.find(search);
            if (bp != std::string::npos) {
                bp += search.size();
                while (bp < obj.size() && obj[bp] == ' ') ++bp;
                g.serverBacked = (obj.substr(bp, 4) == "true");
            }
        }
        g.serverGameId  = ToWide(ReadJsonField(obj, "serverGameId"));
        g.serverVersion = ToWide(ReadJsonField(obj, "serverVersion"));
        g.installRoot   = ToWide(ReadJsonField(obj, "installRoot"));
        g.installState  = ParseInstallState(ReadJsonField(obj, "installState"));
        g.coverArtPath = ToWide(ReadJsonField(obj, "coverArtPath"));
        g.coverArtUrl  = ToWide(ReadJsonField(obj, "coverArtUrl"));
        g.steamAppId   = ToWide(ReadJsonField(obj, "steamAppId"));
        g.epicAppName  = ToWide(ReadJsonField(obj, "epicAppName"));
        g.gogGameId    = ToWide(ReadJsonField(obj, "gogGameId"));
        g.playtimeSeconds = ReadJsonNum(obj, "playtimeSeconds");
        g.lastPlayed      = (int64_t)ReadJsonNum(obj, "lastPlayed");
        g.igdbId          = (int64_t)ReadJsonNum(obj, "igdbId");
        g.summary         = ToWide(ReadJsonField(obj, "summary"));
        g.genres          = ToWide(ReadJsonField(obj, "genres"));
        g.releaseDate     = (int64_t)ReadJsonNum(obj, "releaseDate");
        g.igdbPlatformId  = (int)ReadJsonNum(obj, "igdbPlatformId");
        g.launchOptions   = ToWide(ReadJsonField(obj, "launchOptions"));
        {
            std::wstring col = ToWide(ReadJsonField(obj, "collections"));
            size_t cp = 0;
            while (cp < col.size()) {
                size_t nl = col.find(L'\n', cp);
                if (nl == std::wstring::npos) nl = col.size();
                std::wstring one = col.substr(cp, nl - cp);
                if (!one.empty()) g.collections.push_back(one);
                cp = nl + 1;
            }
        }

        // igdbMatched — look for boolean true/false
        {
            std::string search = "\"igdbMatched\":";
            size_t bp = obj.find(search);
            if (bp != std::string::npos) {
                bp += search.size();
                while (bp < obj.size() && obj[bp] == ' ') ++bp;
                g.igdbMatched = (obj.substr(bp, 4) == "true");
            }
        }

        // igdbRating stored as integer * 100 with e-2 suffix
        {
            std::string search = "\"igdbRating\":";
            size_t rp = obj.find(search);
            if (rp != std::string::npos) {
                rp += search.size();
                while (rp < obj.size() && obj[rp] == ' ') ++rp;
                int iv = 0;
                while (rp < obj.size() && isdigit((unsigned char)obj[rp]))
                    iv = iv * 10 + (obj[rp++] - '0');
                g.igdbRating = (float)iv / 100.0f;
            }
        }

        std::string plat = ReadJsonField(obj, "platform");
        if      (plat == "Steam")   g.platform = Platform::Steam;
        else if (plat == "Epic")    g.platform = Platform::Epic;
        else if (plat == "GOG")     g.platform = Platform::GOG;
        else if (plat == "Dolphin") g.platform = Platform::Dolphin;
        else if (plat == "GameCube") g.platform = Platform::GameCube;
        else if (plat == "Wii")     g.platform = Platform::Wii;
        else if (plat == "Ryujinx") g.platform = Platform::Ryujinx;
        else if (plat == "RPCS3")   g.platform = Platform::RPCS3;
        else if (plat == "N64")     g.platform = Platform::N64;
        else if (plat == "NES")     g.platform = Platform::NES;
        else if (plat == "SNES")    g.platform = Platform::SNES;
        else if (plat == "PS1")     g.platform = Platform::PS1;
        else if (plat == "PS2")     g.platform = Platform::PS2;
        else if (plat == "Xbox360") g.platform = Platform::Xbox360;
        else if (plat == "Xbox")    g.platform = Platform::Xbox;
        else if (plat == "PC")      g.platform = Platform::Repacks;  // PC (formerly "Repacks")
        else                        g.platform = Platform::Repacks;  // legacy + unknown

        m_games.push_back(std::move(g));
    }
}
