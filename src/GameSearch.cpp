// GameSearch.cpp — portable library search-match predicate (Linux port L3d-c).
// Mirrors GameLibrary::Search on UTF-8 std::string.

#include "GameSearch.h"

#include <cctype>

namespace gamesearch {

namespace {
bool containsLower(const std::string& hay, const std::string& loweredNeedle) {
    std::string h = hay;
    for (char& c : h) c = (char)std::tolower((unsigned char)c);
    return h.find(loweredNeedle) != std::string::npos;
}

bool allDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!std::isdigit((unsigned char)c)) return false;
    return true;
}
} // namespace

std::string lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

bool matches(const Fields& f, const std::string& lq) {
    bool hit = containsLower(f.title, lq) ||
               (!f.genres.empty()    && containsLower(f.genres, lq)) ||
               containsLower(f.platform, lq) ||
               (!f.developer.empty() && containsLower(f.developer, lq)) ||
               (!f.publisher.empty() && containsLower(f.publisher, lq)) ||
               (!f.franchise.empty() && containsLower(f.franchise, lq));
    if (!hit && f.releaseYear > 0 && lq.size() == 4 && allDigits(lq))
        hit = (std::to_string(f.releaseYear) == lq);
    return hit;
}

} // namespace gamesearch
