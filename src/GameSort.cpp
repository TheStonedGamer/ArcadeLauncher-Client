// GameSort.cpp — portable library sort ordering (Linux port L3d-c).
// Mirrors App.cpp's sort comparators on a UTF-8 key struct.

#include "GameSort.h"

namespace gamesort {

Mode modeFromIndex(int i) {
    switch (i) {
        case 1: return Mode::Platform;
        case 2: return Mode::Rating;
        case 3: return Mode::Playtime;
        case 4: return Mode::Recent;
        default: return Mode::Title;
    }
}

namespace {
// Tie-breaker shared by every mode: title ascending, then id (App.cpp byTitle).
bool byTitle(const Key& a, const Key& b) {
    if (a.title != b.title) return a.title < b.title;
    return a.id < b.id;
}
} // namespace

bool less(const Key& a, const Key& b, Mode mode) {
    switch (mode) {
        case Mode::Platform:
            if (a.platform != b.platform) return a.platform < b.platform;
            return byTitle(a, b);
        case Mode::Rating:
            if (a.rating != b.rating) return a.rating > b.rating;
            return byTitle(a, b);
        case Mode::Playtime:
            if (a.playtimeSeconds != b.playtimeSeconds)
                return a.playtimeSeconds > b.playtimeSeconds;
            return byTitle(a, b);
        case Mode::Recent:
            if (a.lastPlayed != b.lastPlayed) return a.lastPlayed > b.lastPlayed;
            return byTitle(a, b);
        case Mode::Title:
        default:
            return byTitle(a, b);
    }
}

} // namespace gamesort
