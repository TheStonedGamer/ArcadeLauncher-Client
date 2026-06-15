#pragma once
// Catalog.h — portable (UTF-8) reader for the client's library.json (Linux port
// L3d). The Windows GameLibrary stores everything as std::wstring; this is a
// std::string/UTF-8 view of the same on-disk format so the Linux app shell can
// read the real catalog without pulling in the wstring/Win32 GameLibrary.
//
// Same file, same field names, same escaping as GameLibrary::Save — verified by
// catalog_selfcheck. As GameLibrary migrates to UTF-8 (later L3d), this becomes
// the shared model and the wstring duplication goes away.

#include <cstdint>
#include <string>
#include <vector>

namespace catalog {

// Display-oriented subset of a library entry. Extend as the Linux UI grows.
struct Game {
    std::string id;
    std::string title;
    std::string platform;        // "PC", "GameCube", … (PlatformName on Windows)
    std::string installState;    // "installed", "notInstalled", "updateAvailable", …
    std::string coverArtPath;    // local path (may be relative to library root)
    std::string coverArtUrl;
    std::string developer;
    std::string publisher;
    std::string franchise;
    std::string genres;
    std::string contentPath;       // server content path; ROM-variant grouping
    int64_t     releaseDate = 0;   // unix timestamp (0 = unknown)
    uint64_t    playtimeSeconds = 0;
    bool        favorite = false;
    bool        hidden   = false;
};

// Parse the JSON array text produced by GameLibrary::Save into UTF-8 Games.
std::vector<Game> parse(const std::string& json);

// Read + parse library.json from disk (UTF-8 path). Empty vector if missing.
std::vector<Game> loadFile(const std::string& path);

} // namespace catalog
