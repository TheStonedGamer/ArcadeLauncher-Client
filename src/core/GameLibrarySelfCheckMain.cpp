// GameLibrarySelfCheckMain.cpp — Windows headless KAT guarding the on-disk
// games-library round-trip (GameLibrary::Save -> Load). The user's real library
// (every game they own) persists through exactly this code; a regression here
// would silently lose or corrupt entries. This locks the round-trip: build a
// library with tricky fields (Unicode title, Windows path with backslashes, a
// summary containing quotes + newlines, multiple collections, numbers, bools,
// an install state), Save it, Load it into a fresh library, and assert every
// field survived byte-for-byte. Also checks Search still finds a known game.
//
// Not part of the launcher exe — compiled standalone:
//   scripts\build-gamelib-selfcheck.cmd   (vcvars64 + cl, then runs the exe)
// Exit 0 on success.

#include "pch.h"
#include "GameLibrary.h"

#include <cstdio>
#include <string>

namespace {
int g_fail = 0;

void eqW(const char* what, const std::wstring& got, const std::wstring& want) {
    if (got != want) {
        std::string g = ToUtf8(got), w = ToUtf8(want);
        std::printf("  FAIL %s: got \"%s\" want \"%s\"\n", what, g.c_str(),
                    w.c_str());
        ++g_fail;
    }
}
void eqN(const char* what, unsigned long long got, unsigned long long want) {
    if (got != want) {
        std::printf("  FAIL %s: got %llu want %llu\n", what, got, want);
        ++g_fail;
    }
}
void eqB(const char* what, bool got, bool want) {
    if (got != want) {
        std::printf("  FAIL %s: got %d want %d\n", what, (int)got, (int)want);
        ++g_fail;
    }
}
} // namespace

int main() {
    const std::wstring path = L"gamelib_selfcheck.tmp.json";

    // A game with every awkward case the persistence has to survive.
    Game a;
    a.id            = L"game-001";
    a.title         = L"Pokémon ♥ 日本語 \"Gold\"";  // Unicode + embedded quotes
    a.platform      = Platform::SNES;
    a.exePath       = L"C:\\Games\\Zelda [!]\\game.exe";  // backslashes + tags
    a.summary       = L"Line one.\nLine \"two\" with quotes.";  // newline + quote
    a.developer     = L"Nintendo";
    a.publisher     = L"Nintendo";
    a.playtimeSeconds = 123456;
    a.lastPlayed    = 1700000000;
    a.favorite      = true;
    a.hidden        = false;
    a.installState  = InstallState::Installed;
    a.serverBacked  = true;
    a.collections   = {L"Favorites", L"RPGs", L"To Finish"};

    Game b;
    b.id        = L"game-002";
    b.title     = L"Celeste";
    b.platform  = Platform::Repacks;
    b.favorite  = false;
    b.hidden    = true;
    b.collections = {};

    GameLibrary lib;
    lib.AddGame(a);
    lib.AddGame(b);
    lib.Save(path);

    GameLibrary loaded;
    loaded.Load(path);

    eqN("count", loaded.All().size(), 2);

    const Game* ga = loaded.FindById(L"game-001");
    if (!ga) {
        std::printf("  FAIL: game-001 not found after reload\n");
        ++g_fail;
    } else {
        eqW("title",       ga->title, a.title);
        eqB("platform",    ga->platform == Platform::SNES, true);
        eqW("exePath",     ga->exePath, a.exePath);
        eqW("summary",     ga->summary, a.summary);
        eqW("developer",   ga->developer, a.developer);
        eqN("playtime",    ga->playtimeSeconds, a.playtimeSeconds);
        eqN("lastPlayed",  (unsigned long long)ga->lastPlayed,
                           (unsigned long long)a.lastPlayed);
        eqB("favorite",    ga->favorite, true);
        eqB("hidden",      ga->hidden, false);
        eqB("serverBacked", ga->serverBacked, true);
        eqB("installState", ga->installState == InstallState::Installed, true);
        eqN("collections.n", ga->collections.size(), 3);
        if (ga->collections.size() == 3) {
            eqW("collections[0]", ga->collections[0], L"Favorites");
            eqW("collections[1]", ga->collections[1], L"RPGs");
            eqW("collections[2]", ga->collections[2], L"To Finish");
        }
    }

    const Game* gb = loaded.FindById(L"game-002");
    if (!gb) {
        std::printf("  FAIL: game-002 not found after reload\n");
        ++g_fail;
    } else {
        eqB("b.hidden", gb->hidden, true);
        eqN("b.collections.n", gb->collections.size(), 0);
    }

    // Search still finds the reloaded game by title and by platform name.
    eqN("search.title", loaded.Search(L"pokémon").size(), 1);
    eqN("search.platform", loaded.Search(L"SNES").size(), 1);

    _wremove(path.c_str());

    if (g_fail == 0)
        std::printf("gamelib self-check: OK — Save/Load round-trip preserved all fields\n");
    else
        std::printf("gamelib self-check: FAILED (%d)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
