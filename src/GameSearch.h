#pragma once
// GameSearch.h — portable (UTF-8) library search-match predicate (Linux port
// L3d-c). The rule that decides whether a game matches a search query —
// title/genre/platform/developer/publisher/franchise substring, plus a 4-digit
// release-year match — lives here so the Windows GameLibrary::Search and the
// Linux catalog search share one implementation.
//
// Pure of Win32: operates on UTF-8 std::string. The Windows side narrows its
// std::wstring fields (and computes the release year via gmtime) at the call
// boundary. Covered by search_selfcheck.

#include <string>

namespace gamesearch {

// The searchable fields of one game. releaseYear is the Gregorian year (e.g.
// 2008) or 0 when unknown — the caller derives it from its own timestamp.
struct Fields {
    std::string title;
    std::string genres;
    std::string platform;     // PlatformName: "Steam", "Wii", "PC", …
    std::string developer;
    std::string publisher;
    std::string franchise;
    int         releaseYear = 0;
};

// ASCII-lowercase a query once, so a caller can lower it before looping over
// many games (mirrors GameLibrary::Search hoisting the lowered query).
std::string lower(const std::string& s);

// True when the game matches the ALREADY-LOWERCASED query. Substring test is
// case-insensitive on every text field; a 4-digit query also matches the
// release year. An empty query matches everything (as a substring of all).
bool matches(const Fields& f, const std::string& loweredQuery);

} // namespace gamesearch
