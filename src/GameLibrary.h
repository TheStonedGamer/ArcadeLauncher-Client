#pragma once
#include "pch.h"

enum class Platform {
    Steam,
    Epic,
    GOG,
    Dolphin,
    GameCube,
    Wii,
    Ryujinx,
    RPCS3,
    N64,
    NES,
    SNES,
    PS1,
    PS2,
    Xbox360,
    Xbox,
    Repacks   // formerly "Custom" — FitGirl repacks, cracked installs, manual .exe
};

enum class InstallState {
    Local,
    Missing,
    Downloading,
    Installed,
    UpdateAvailable
};

inline std::wstring PlatformName(Platform p) {
    switch (p) {
    case Platform::Steam:   return L"Steam";
    case Platform::Epic:    return L"Epic";
    case Platform::GOG:     return L"GOG";
    case Platform::Dolphin: return L"Dolphin";
    case Platform::GameCube: return L"GameCube";
    case Platform::Wii:     return L"Wii";
    case Platform::Ryujinx: return L"Ryujinx";
    case Platform::RPCS3:   return L"RPCS3";
    case Platform::N64:     return L"N64";
    case Platform::NES:     return L"NES";
    case Platform::SNES:    return L"SNES";
    case Platform::PS1:     return L"PS1";
    case Platform::PS2:     return L"PS2";
    case Platform::Xbox360: return L"Xbox360";
    case Platform::Xbox:    return L"Xbox";
    default:                return L"PC";
    }
}

inline D2D1_COLOR_F PlatformColor(Platform p) {
    switch (p) {
    case Platform::Steam:   return D2D1::ColorF(0x1B9CFC);  // Steam blue
    case Platform::Epic:    return D2D1::ColorF(0xC8C8C8);  // Epic silver
    case Platform::GOG:     return D2D1::ColorF(0xAC6EE8);  // GOG purple
    case Platform::Dolphin: return D2D1::ColorF(0x4ECDC4);  // teal
    case Platform::GameCube: return D2D1::ColorF(0x6A5ACD);  // GameCube indigo
    case Platform::Wii:     return D2D1::ColorF(0x7FB2E5);  // Wii light blue
    case Platform::Ryujinx: return D2D1::ColorF(0xFF4A4A);  // red
    case Platform::RPCS3:   return D2D1::ColorF(0x003791);  // PlayStation blue
    case Platform::N64:     return D2D1::ColorF(0x009B77);  // N64 green
    case Platform::NES:     return D2D1::ColorF(0xCC0000);  // NES red
    case Platform::SNES:    return D2D1::ColorF(0x7B43AC);  // SNES purple
    case Platform::PS1:     return D2D1::ColorF(0x1B6DB9);  // PS1 blue
    case Platform::PS2:     return D2D1::ColorF(0x002F6C);  // PS2 dark blue
    case Platform::Xbox360: return D2D1::ColorF(0x107C10);  // Xbox green
    case Platform::Xbox:    return D2D1::ColorF(0x52B043);  // original Xbox green (lighter)
    default:                return D2D1::ColorF(0xF5A623);  // FitGirl orange
    }
}

struct Game {
    std::wstring id;
    std::wstring title;
    Platform     platform = Platform::Repacks;

    // Launch info
    std::wstring launchUri;
    std::wstring exePath;
    std::wstring arguments;

    // Emulator specific
    std::wstring emulatorPath;
    std::wstring romPath;

    // True for entries the user added by hand (via AddGame) rather than ones
    // produced by a platform scan. Manual entries are never repopulated by a
    // rescan, so they must survive the startup purge and library merges.
    bool         manualEntry = false;

    // Server-backed install info
    bool         serverBacked = false;
    std::wstring serverGameId;
    std::wstring serverVersion;
    // Path of the game's content relative to the library root, as reported by
    // the server catalog (e.g. "games/Nintendo/NES/Crystalis (U) [a1].nes").
    // Used to label ROM dump variants that share a cleaned display title so the
    // grid can group them under one tile with a version picker.
    std::wstring contentPath;
    std::wstring installRoot;
    InstallState installState = InstallState::Local;
    int          installProgressPermille = 0;

    // Transient (UI-only, never serialized): when this game is the representative
    // of a collapsed ROM-variant group, the number of dumps in that group. 0/1
    // means it's a standalone tile. Set by App::ApplyFilter each rebuild.
    int          variantCount = 0;

    // Art
    std::wstring coverArtPath;
    std::wstring coverArtUrl;

    // Per-game custom launch options (Steam-style). Appended to the launch
    // command line; "%command%" if present is replaced by the base target.
    std::wstring launchOptions;

    // Optional shell commands run around the play session (Playnite-style):
    // preLaunchCmd fires just before the game starts, postExitCmd just after
    // it exits. Run hidden via cmd /C; failures are silently ignored.
    std::wstring preLaunchCmd;
    std::wstring postExitCmd;

    // Cloud saves: local folder whose files are synced to the server before
    // launch (server-newer pulled) and after exit (local-newer pushed). Empty
    // means cloud saves are off for this game. Server-backed games only.
    std::wstring saveDir;

    // Collections this game belongs to (names matching AppConfig::collections).
    std::vector<std::wstring> collections;

    // Steam-style favorite star and hide-from-library flags. Hidden games are
    // excluded from every page except the dedicated Hidden sidebar entry.
    bool favorite = false;
    bool hidden   = false;

    // Stats
    uint64_t playtimeSeconds = 0;
    int64_t  lastPlayed = 0;

    // Platform IDs
    std::wstring steamAppId;
    std::wstring epicAppName;
    std::wstring gogGameId;

    // IGDB metadata
    int64_t      igdbId = 0;
    bool         igdbMatched = false;
    std::wstring summary;
    std::wstring genres;
    float        igdbRating = 0.0f;   // 0-100, 0 = no rating
    int64_t      releaseDate = 0;     // unix timestamp
    int          igdbPlatformId = 0;  // 0 = auto; >0 = IGDB platform override

    // Company / franchise metadata from the server catalog (IGDB).
    std::wstring developer;
    std::wstring publisher;
    std::wstring franchise;

    // Screenshot / artwork URLs from the server catalog (IGDB). Rendered as a
    // thumbnail strip in the detail panel; downloaded + decoded lazily.
    std::vector<std::wstring> screenshots;

    // Stable art-cache key for screenshot #i (reuses the cover-art decode cache).
    static std::wstring ScreenshotKey(const std::wstring& id, int i) {
        return id + L"#s" + std::to_wstring(i);
    }

    std::wstring LaunchTarget() const {
        if (!launchUri.empty())    return launchUri;
        if (!emulatorPath.empty()) return emulatorPath;
        return exePath;
    }

    std::wstring PlaytimeStr() const {
        if (playtimeSeconds == 0) return L"Never played";
        uint64_t h = playtimeSeconds / 3600;
        uint64_t m = (playtimeSeconds % 3600) / 60;
        if (h > 0) return std::to_wstring(h) + L"h " + std::to_wstring(m) + L"m";
        return std::to_wstring(m) + L"m";
    }

    // Rating as a coloured display string
    std::wstring RatingStr() const {
        if (igdbRating < 1.0f) return {};
        return std::to_wstring((int)igdbRating) + L"/100";
    }

    D2D1_COLOR_F RatingColor() const {
        if (igdbRating >= 75.0f) return D2D1::ColorF(0x3FB950);  // green
        if (igdbRating >= 50.0f) return D2D1::ColorF(0xD29922);  // yellow
        return D2D1::ColorF(0xF85149);                            // red
    }

    // ── ROM dump variant grouping ─────────────────────────────────────────────
    // Different dumps of the same game (Crystalis (U), [a1], [a2], (Prototype),
    // SMB3 (PRG 1) [a2], …) clean down to an identical display title. These
    // helpers let the grid collapse them under one tile and offer a picker.

    // The filename leaf of contentPath without extension; falls back to title.
    std::wstring VariantFileStem() const {
        std::wstring s = contentPath;
        size_t slash = s.find_last_of(L"\\/");
        if (slash != std::wstring::npos) s = s.substr(slash + 1);
        size_t dot = s.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0) s = s.substr(0, dot);
        return s.empty() ? title : s;
    }

    // Games sharing a VariantKey are dumps of the same logical game. The cleaned
    // title is already identical across dumps, so platform+title suffices.
    std::wstring VariantKey() const {
        std::wstring t = title;
        for (auto& ch : t) ch = (wchar_t)towlower(ch);
        return std::to_wstring((int)platform) + L"|" + t;
    }

    // Short human label distinguishing one dump from its siblings, derived from
    // the tags in the filename, e.g. "Verified", "Alt 1", "Prototype", "PRG 1",
    // "Eng patch". Empty for a plain base dump.
    std::wstring VariantLabel() const {
        std::wstring stem = VariantFileStem();
        std::wstring low = stem;
        for (auto& ch : low) ch = (wchar_t)towlower(ch);
        std::vector<std::wstring> parts;
        auto has = [&](const wchar_t* s) { return low.find(s) != std::wstring::npos; };
        if (has(L"[!]"))                       parts.push_back(L"Verified");
        if (has(L"prototype") || has(L"proto")) parts.push_back(L"Prototype");
        if (has(L"prg 1") || has(L"prg1"))     parts.push_back(L"PRG 1");
        if (has(L"trad-en") || has(L"t-en") || has(L"[t+en") || has(L"[tr en"))
                                               parts.push_back(L"Eng patch");
        // Alt-dump index [a1]/[a2]/…
        size_t ap = low.find(L"[a");
        if (ap != std::wstring::npos && ap + 2 < low.size() && iswdigit(low[ap + 2]))
            parts.push_back(std::wstring(L"Alt ") + (wchar_t)low[ap + 2]);
        if (has(L"[b"))                        parts.push_back(L"Bad dump");
        if (has(L"[h"))                        parts.push_back(L"Hack");
        if (has(L"[p"))                        parts.push_back(L"Pirate");
        std::wstring out;
        for (auto& p : parts) { if (!out.empty()) out += L", "; out += p; }
        return out;
    }

    // Lower score = better default pick for the grouped tile. Prefers verified
    // good dumps and clean base dumps; demotes alternates, prototypes, bad/hack
    // dumps. Installed copies win outright so a grouped tile reflects what's on
    // disk.
    int VariantScore() const {
        int s = 100;
        if (serverBacked && installState == InstallState::Installed) s -= 1000;
        std::wstring low = VariantFileStem();
        for (auto& ch : low) ch = (wchar_t)towlower(ch);
        auto has = [&](const wchar_t* x) { return low.find(x) != std::wstring::npos; };
        if (has(L"[!]")) s -= 50;
        if (has(L"[a")) s += 15;
        if (has(L"prototype") || has(L"proto")) s += 40;
        if (has(L"[b")) s += 60;
        if (has(L"[h") || has(L"[p") || has(L"[t")) s += 30;
        if (has(L"(u)") || has(L"(usa)")) s -= 5;   // mild US-region preference
        return s;
    }
};

class GameLibrary {
public:
    void AddGame(Game game);
    void RemoveGame(const std::wstring& id);
    void RemoveServerClientLocalEntries();
    void UpdateGame(const Game& game);
    void MergeGames(std::vector<Game> scanned);

    const std::vector<Game>& All() const { return m_games; }
    std::vector<Game>& AllMutable() { return m_games; }
    std::vector<const Game*> Filter(Platform p) const;
    std::vector<const Game*> Search(const std::wstring& query) const;
    Game* FindById(const std::wstring& id);

    void Load(const std::wstring& path);
    void Save(const std::wstring& path) const;

private:
    std::vector<Game> m_games;
    mutable std::mutex m_mutex;
};
