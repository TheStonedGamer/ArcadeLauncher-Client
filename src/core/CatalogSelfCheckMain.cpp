// CatalogSelfCheckMain.cpp — Phase L3d headless catalog-parse self-check.
//
// Parses a sample library.json in the exact format GameLibrary::Save emits —
// including the hard cases the bespoke parser must survive: a '}' inside a
// quoted string value, and escaped quotes/newlines. Pure CPU; gates in CI.
// Exit 0 on success.

#include "Catalog.h"

#include <cstdio>
#include <string>

int main() {
    // Two entries. The first summary contains a literal '}' and an escaped quote
    // to exercise findObjectEnd + readField escape handling.
    const std::string json = R"JSON([
  {"id":"g1","title":"Hollow Knight","platform":"PC","installState":"installed",
   "coverArtPath":"covers/g1.png","developer":"Team Cherry","publisher":"Team Cherry",
   "franchise":"Hollow Knight","genres":"Metroidvania","contentPath":"games/PC/Hollow Knight/hk.exe",
   "releaseDate":1488499200,
   "summary":"A bug \"epic\" with a } brace inside","playtimeSeconds":3600,
   "favorite":true,"hidden":false},
  {"id":"g2","title":"Metroid Prime","platform":"GameCube","installState":"notInstalled",
   "coverArtPath":"","developer":"Retro Studios","publisher":"Nintendo",
   "playtimeSeconds":0,"favorite":false,"hidden":true}
]
)JSON";

    auto games = catalog::parse(json);
    int rc = 0;
    auto check = [&](bool cond, const char* what) {
        if (!cond) { std::printf("catalog self-check: FAIL — %s\n", what); rc = 1; }
    };

    check(games.size() == 2, "expected 2 games");
    if (games.size() == 2) {
        check(games[0].id == "g1", "g1 id");
        check(games[0].title == "Hollow Knight", "g1 title");
        check(games[0].platform == "PC", "g1 platform");
        check(games[0].installState == "installed", "g1 installState");
        check(games[0].coverArtPath == "covers/g1.png", "g1 cover");
        check(games[0].developer == "Team Cherry", "g1 developer");
        check(games[0].franchise == "Hollow Knight", "g1 franchise");
        check(games[0].genres == "Metroidvania", "g1 genres");
        check(games[0].contentPath == "games/PC/Hollow Knight/hk.exe", "g1 contentPath");
        check(games[0].releaseDate == 1488499200, "g1 releaseDate");
        check(games[0].playtimeSeconds == 3600, "g1 playtime");
        check(games[0].favorite && !games[0].hidden, "g1 flags");
        // The '}' inside the summary must not have truncated the object, so g2
        // (which comes after it) must still parse.
        check(games[1].id == "g2", "g2 id (brace-in-string survived)");
        check(games[1].platform == "GameCube", "g2 platform");
        check(games[1].installState == "notInstalled", "g2 installState");
        check(!games[1].favorite && games[1].hidden, "g2 flags");
    }

    if (rc == 0)
        std::printf("catalog self-check: OK (%zu games parsed)\n", games.size());
    return rc;
}
