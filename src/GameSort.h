#pragma once
// GameSort.h — portable (UTF-8) library sort ordering (Linux port L3d-c). The
// catalog sort modes (Title / Platform / Rating / Playtime / Recent) live here
// as one comparator so the Windows App and the Linux app order the grid the same
// way. Mirrors App.cpp's std::sort lambdas: Title ascending; Platform by
// platform name then title; Rating/Playtime/Recent descending then title; every
// mode tie-breaks by title then id for a stable, deterministic order.
//
// Pure of Win32: operates on a small UTF-8 key struct. Covered by sort_selfcheck.

#include <cstdint>
#include <string>

namespace gamesort {

enum class Mode { Title, Platform, Rating, Playtime, Recent };

// Map a 0..4 index (the Windows sortMode cycle) to a Mode; out-of-range -> Title.
Mode modeFromIndex(int i);

// The sort keys of one game (already narrowed to UTF-8 by the caller).
struct Key {
    std::string title;
    std::string id;
    std::string platform;          // PlatformName, for Mode::Platform
    float       rating = 0;         // igdbRating 0-100, for Mode::Rating
    uint64_t    playtimeSeconds = 0;
    int64_t     lastPlayed = 0;
};

// Strict-weak ordering: true when a should sort before b under mode.
bool less(const Key& a, const Key& b, Mode mode);

} // namespace gamesort
