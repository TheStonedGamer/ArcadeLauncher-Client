#include "pch.h"
#include "MetadataManager.h"

MetadataManager::MetadataManager(GameLibrary& library, IgdbClient& client,
                                  const std::wstring& artCacheDir)
    : m_library(library), m_client(client), m_artCacheDir(artCacheDir)
{
    CreateDirectoryW(artCacheDir.c_str(), nullptr);
    m_running.store(false);
    m_worker = std::thread(&MetadataManager::WorkerThread, this);
}

MetadataManager::~MetadataManager() { Shutdown(); }

void MetadataManager::Shutdown() {
    m_shutdown.store(true);
    m_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();
}

void MetadataManager::ScanAllAsync(ProgressCallback cb) {
    std::lock_guard<std::mutex> lk(m_mutex);
    for (auto& g : m_library.All()) {
        if (g.igdbMatched) continue;
        m_queue.push_back({ g.id, 0, cb });
    }
    m_running.store(true);
    m_cv.notify_one();
}

void MetadataManager::ForceRescanAllAsync(ProgressCallback cb) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_queue.clear();
    for (auto& g : m_library.All()) {
        // Clear match flag so ProcessItem will actually re-search
        if (Game* gm = m_library.FindById(g.id)) gm->igdbMatched = false;
        m_queue.push_back({ g.id, 0, cb });
    }
    m_running.store(true);
    m_cv.notify_one();
}

void MetadataManager::ScanGameAsync(const std::wstring& gameId, ProgressCallback cb) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_queue.push_back({ gameId, 0, cb });
    m_running.store(true);
    m_cv.notify_one();
}

bool MetadataManager::MatchManual(const std::wstring& gameId, int64_t igdbId, ProgressCallback cb) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_queue.insert(m_queue.begin(), { gameId, igdbId, cb });
    m_running.store(true);
    m_cv.notify_one();
    return true;
}

std::vector<IgdbGame> MetadataManager::GetCandidates(const std::wstring& title, int limit) {
    m_client.Authenticate();
    return m_client.Search(title, limit);
}

void MetadataManager::WorkerThread() {
    while (!m_shutdown.load()) {
        WorkItem item;
        {
            std::unique_lock<std::mutex> lk(m_mutex);
            m_cv.wait(lk, [this] { return !m_queue.empty() || m_shutdown.load(); });
            if (m_shutdown.load()) break;
            item = std::move(m_queue.front());
            m_queue.erase(m_queue.begin());
            if (m_queue.empty()) m_running.store(false);
        }
        ProcessItem(item);
        // Small delay between API calls to avoid rate limiting
        Sleep(250);
    }
}

void MetadataManager::ProcessItem(const WorkItem& item) {
    Game* g = m_library.FindById(item.gameId);
    if (!g) return;

    if (!m_client.IsAuthenticated() && !m_client.Authenticate()) return;

    IgdbGame meta;
    bool matched = false;

    // IGDB platform IDs for emulated games only.
    // PC platform (6) is deliberately NOT used — IGDB's PC tagging is too
    // inconsistent; filtering by it causes many valid games to return nothing.
    static const auto igdbPlatforms = [](Platform p) -> std::vector<int> {
        switch (p) {
        case Platform::Dolphin: return { 21, 5 };  // GameCube, Wii
        case Platform::GameCube: return { 21 };    // GameCube
        case Platform::Wii:     return { 5 };      // Wii
        case Platform::Ryujinx: return { 130 };    // Nintendo Switch
        case Platform::RPCS3:   return { 9 };      // PS3
        case Platform::N64:     return { 4 };      // Nintendo 64
        case Platform::NES:     return { 18 };     // NES
        case Platform::SNES:    return { 19 };     // SNES
        case Platform::PS1:     return { 7 };      // PlayStation
        case Platform::PS2:     return { 8 };      // PlayStation 2
        case Platform::Xbox360: return { 12 };     // Xbox 360
        case Platform::Xbox:    return { 11 };     // Xbox
        default:                return {};
        }
    };

    if (item.forceIgdbId > 0) {
        meta = m_client.FetchById(item.forceIgdbId);
        matched = (meta.id > 0);
    } else {
        // Auto-match: search by title, pick best candidate
        std::wstring normTitle = Normalize(g->title);
        // User-specified platform override takes precedence over the auto mapping
        auto platformFilter = (g->igdbPlatformId > 0)
            ? std::vector<int>{ g->igdbPlatformId }
            : igdbPlatforms(g->platform);
        auto candidates = m_client.Search(normTitle, 8, platformFilter);

        // If platform-filtered search returned nothing, retry without the filter.
        // Happens when IGDB lacks the specific platform tag for a game.
        if (candidates.empty() && !platformFilter.empty())
            candidates = m_client.Search(normTitle, 8);

        float bestScore = 0.0f;
        for (auto& c : candidates) {
            float score = Similarity(normTitle, Normalize(c.name));
            if (score > bestScore) {
                bestScore = score;
                meta = c;
            }
        }
        // Require at least 60% similarity to accept
        matched = (bestScore >= 0.60f && meta.id > 0);
    }

    if (matched) {
        // If the search result didn't include cover art, fetch full details by id.
        // This happens when FetchById is used directly (forceIgdbId path).
        if (meta.coverImageId.empty() && meta.id > 0)
            meta = m_client.FetchById(meta.id);

        g->igdbId      = meta.id;
        g->igdbMatched = true;
        g->summary     = meta.summary;
        g->igdbRating  = (float)meta.rating;
        g->releaseDate = meta.firstReleaseDate;

        // Build genres string
        std::wstring genres;
        for (size_t i = 0; i < meta.genres.size(); ++i) {
            if (i) genres += L", ";
            genres += meta.genres[i];
        }
        g->genres = genres;

        // Download cover art (prefer IGDB over Steam CDN if we got one)
        if (!meta.coverImageId.empty()) {
            if (DownloadAndCacheArt(*g, meta)) {
                // coverArtPath set inside
            }
        }
    } else {
        // Still mark as attempted so we don't keep retrying
        g->igdbMatched = true; // matched = false but don't retry
        g->igdbId      = 0;
    }

    if (item.callback) item.callback(g->id, matched && meta.id > 0);
}

bool MetadataManager::DownloadAndCacheArt(Game& game, const IgdbGame& meta) {
    if (meta.coverImageId.empty()) return false;

    // Build safe filename
    std::wstring safe = game.id;
    for (auto& c : safe)
        if (c == L'/' || c == L'\\' || c == L':' || c == L'*' || c == L'?'
            || c == L'"' || c == L'<' || c == L'>' || c == L'|') c = L'_';

    std::wstring dest = m_artCacheDir + L"\\" + safe + L"_igdb.jpg";

    // Skip if already cached
    if (GetFileAttributesW(dest.c_str()) != INVALID_FILE_ATTRIBUTES) {
        game.coverArtPath = dest;
        return true;
    }

    if (m_client.DownloadCover(meta.coverImageId, dest)) {
        game.coverArtPath = dest;
        return true;
    }
    return false;
}

// ── Title normalization + similarity ─────────────────────────────────────────

std::wstring MetadataManager::Normalize(const std::wstring& title) {
    std::wstring s = title;

    // Lowercase
    for (auto& c : s) c = towlower(c);

    // Handle ROM-style article suffix: "legend of zelda, the" → "the legend of zelda"
    static const struct { const wchar_t* sfx; size_t sfxLen; const wchar_t* pfx; } kArticles[] = {
        { L", the", 5, L"the " },
        { L", an",  4, L"an "  },
        { L", a",   3, L"a "   },
    };
    for (auto& art : kArticles) {
        if (s.size() > art.sfxLen &&
            s.compare(s.size() - art.sfxLen, art.sfxLen, art.sfx) == 0) {
            s = art.pfx + s.substr(0, s.size() - art.sfxLen);
            break;
        }
    }

    // Strip edition/version suffixes (longest first)
    static const wchar_t* suffixes[] = {
        L": definitive edition", L" - definitive edition",
        L": game of the year edition", L" game of the year edition",
        L" goty edition", L" goty",
        L": remastered", L" remastered",
        L": enhanced edition", L" enhanced edition",
        L": complete edition", L" complete edition",
        L" - complete edition",
        L": anniversary edition",
        L" gold edition", L" deluxe edition",
        L" director's cut",
        L": pc edition",
        nullptr
    };
    for (auto** suf = suffixes; *suf; ++suf) {
        size_t p = s.rfind(*suf);
        if (p != std::wstring::npos) { s = s.substr(0, p); break; }
    }

    // Remove trademark/copyright symbols
    std::wstring clean;
    for (wchar_t c : s)
        if (c != L'™' && c != L'®' && c != L'©') clean += c;
    s = clean;

    // Strip parenthetical groups
    for (;;) {
        size_t lp = s.rfind(L'(');
        if (lp == std::wstring::npos) break;
        size_t rp = s.find(L')', lp);
        if (rp == std::wstring::npos) break;
        s = s.substr(0, lp) + s.substr(rp + 1);
    }

    // Strip punctuation — keep only letters, digits, and spaces.
    // This normalises "zelda:" == "zelda", "yoshi's" == "yoshis", "-" disappears, etc.
    std::wstring nopunct;
    for (wchar_t c : s)
        if (iswalnum(c) || iswspace(c)) nopunct += c;
    s = nopunct;

    // Collapse whitespace and trim
    std::wstring ws;
    bool prevSpace = false;
    for (wchar_t c : s) {
        if (iswspace(c)) { if (!prevSpace && !ws.empty()) ws += L' '; prevSpace = true; }
        else             { ws += c; prevSpace = false; }
    }
    while (!ws.empty() && iswspace(ws.back()))  ws.pop_back();
    while (!ws.empty() && iswspace(ws.front())) ws.erase(ws.begin());
    return ws;
}

float MetadataManager::Similarity(const std::wstring& a, const std::wstring& b) {
    if (a.empty() && b.empty()) return 1.0f;
    if (a.empty() || b.empty()) return 0.0f;

    // Exact match
    if (a == b) return 1.0f;

    // One contains the other
    if (a.find(b) != std::wstring::npos || b.find(a) != std::wstring::npos)
        return 0.92f;

    // Word-overlap Jaccard
    auto words = [](const std::wstring& s) {
        std::unordered_map<std::wstring, int> wc;
        std::wstring w;
        for (wchar_t c : s) {
            if (iswspace(c)) { if (!w.empty()) { wc[w]++; w.clear(); } }
            else w += c;
        }
        if (!w.empty()) wc[w]++;
        return wc;
    };

    auto wa = words(a), wb = words(b);
    int intersect = 0, total = 0;
    for (auto& [word, cnt] : wa) {
        auto it = wb.find(word);
        if (it != wb.end()) intersect += std::min(cnt, it->second);
        total += cnt;
    }
    for (auto& [word, cnt] : wb) total += cnt;
    total -= intersect; // union = a + b - intersection
    if (total == 0) return 1.0f;
    return (float)intersect / (float)total;
}
