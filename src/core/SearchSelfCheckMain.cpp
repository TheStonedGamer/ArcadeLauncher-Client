// SearchSelfCheckMain.cpp — headless KAT for the portable library search-match
// predicate (Linux port L3d-c). Locks gamesearch::matches against the Windows
// GameLibrary::Search behaviour: case-insensitive substring over
// title/genre/platform/dev/publisher/franchise, plus a 4-digit release-year
// match. Exit 0 on success.

#include "GameSearch.h"

#include <cstdio>
#include <string>

namespace {
int g_fail = 0;

void expect(const char* what, bool got, bool want) {
    if (got != want) {
        std::printf("  FAIL %s: got %s want %s\n", what, got ? "true" : "false",
                    want ? "true" : "false");
        ++g_fail;
    }
}

bool m(const gamesearch::Fields& f, const char* q) {
    return gamesearch::matches(f, gamesearch::lower(q));
}
} // namespace

int main() {
    gamesearch::Fields hk;
    hk.title = "Hollow Knight";
    hk.genres = "Metroidvania";
    hk.platform = "PC";
    hk.developer = "Team Cherry";
    hk.publisher = "Team Cherry";
    hk.releaseYear = 2017;

    expect("title",        m(hk, "hollow"),  true);
    expect("title.case",   m(hk, "HOLLOW"),  true);
    expect("genre.sub",    m(hk, "METROID"), true);  // substring of "Metroidvania"
    expect("platform",     m(hk, "pc"),      true);
    expect("developer",    m(hk, "cherry"),  true);
    expect("year.exact",   m(hk, "2017"),    true);
    expect("year.miss",    m(hk, "2018"),    false);
    expect("no.match",     m(hk, "xyzzy"),   false);
    expect("empty.matches", m(hk, ""),       true);   // empty is a substring of all
    expect("year.needs4",  m(hk, "201"),     false);  // 3 digits -> no year match
    expect("text4.ok",     m(hk, "team"),    true);   // 4-char text hit still works

    gamesearch::Fields noYear;
    noYear.title = "Mystery Game";
    noYear.platform = "PC";
    noYear.releaseYear = 0;
    expect("year0.nomatch", m(noYear, "2017"), false);
    expect("year0.title",   m(noYear, "mystery"), true);

    if (g_fail == 0)
        std::printf("search self-check: OK — all search KATs passed\n");
    else
        std::printf("search self-check: FAILED (%d)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
