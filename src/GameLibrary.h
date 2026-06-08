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

    // Server-backed install info
    bool         serverBacked = false;
    std::wstring serverGameId;
    std::wstring serverVersion;
    std::wstring installRoot;
    InstallState installState = InstallState::Local;
    int          installProgressPermille = 0;

    // Art
    std::wstring coverArtPath;
    std::wstring coverArtUrl;

    // Per-game custom launch options (Steam-style). Appended to the launch
    // command line; "%command%" if present is replaced by the base target.
    std::wstring launchOptions;

    // Collections this game belongs to (names matching AppConfig::collections).
    std::vector<std::wstring> collections;

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
};

class GameLibrary {
public:
    void AddGame(Game game);
    void RemoveGame(const std::wstring& id);
    void RemoveServerClientLocalEntries();
    void UpdateGame(const Game& game);
    void MergeGames(std::vector<Game> scanned);

    const std::vector<Game>& All() const { return m_games; }
    std::vector<const Game*> Filter(Platform p) const;
    std::vector<const Game*> Search(const std::wstring& query) const;
    Game* FindById(const std::wstring& id);

    void Load(const std::wstring& path);
    void Save(const std::wstring& path) const;

private:
    std::vector<Game> m_games;
    mutable std::mutex m_mutex;
};
