// FilterSelfCheckMain.cpp — headless KAT for the portable sidebar tab / library-
// page filter (Linux port L3d-c). Locks gamefilter::passes against the Windows
// App::ApplyFilter per-page rules: All / Favorites / Recently Played / Installed
// / Ready to Download / Updates / Hidden / Platform / Collection, plus the
// universal rule that hidden games appear only on the Hidden page. Also checks
// tabSelect()'s label→page mapping. Exit 0 on success.

#include "GameFilter.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {
using gamefilter::Install;
using gamefilter::Item;
using gamefilter::Page;
using gamefilter::TabSel;

int g_fail = 0;

// Concatenate the ids of items that pass `sel`, in input order, e.g. "ac".
std::string sel(const std::vector<std::pair<std::string, Item>>& v, const TabSel& s) {
    std::string out;
    for (const auto& kv : v)
        if (gamefilter::passes(kv.second, s)) out += kv.first;
    return out;
}

void eq(const char* what, const std::string& got, const std::string& want) {
    if (got != want) {
        std::printf("  FAIL %s: got \"%s\" want \"%s\"\n", what, got.c_str(),
                    want.c_str());
        ++g_fail;
    }
}

void eqPage(const char* what, Page got, Page want) {
    if (got != want) {
        std::printf("  FAIL %s: page mismatch (%d vs %d)\n", what, (int)got, (int)want);
        ++g_fail;
    }
}
} // namespace

int main() {
    // id, platform, install, serverBacked, favorite, hidden, lastPlayed, collections
    std::vector<std::pair<std::string, Item>> v;

    auto add = [&](const char* id, const char* platform, Install inst, bool server,
                   bool fav, bool hidden, int64_t played,
                   std::vector<std::string> cols) {
        Item it;
        it.platform = platform;
        it.install = inst;
        it.serverBacked = server;
        it.favorite = fav;
        it.hidden = hidden;
        it.lastPlayed = played;
        it.collections = std::move(cols);
        v.push_back({id, std::move(it)});
    };

    // a: server PC, installed, favorite, played, in "RPGs"
    add("a", "PC",   Install::Installed,      true,  true,  false, 100, {"RPGs"});
    // b: server NES, missing (ready to download), in "RPGs"
    add("b", "NES",  Install::Missing,        true,  false, false, 0,   {"RPGs"});
    // c: server PC, update available, played
    add("c", "PC",   Install::UpdateAvailable,true,  false, false, 50,  {});
    // d: local manual NES, never played
    add("d", "NES",  Install::Unknown,        false, false, false, 0,   {});
    // e: server PC, installed, HIDDEN (only on Hidden page)
    add("e", "PC",   Install::Installed,      true,  true,  true,  200, {"RPGs"});

    eq("All",        sel(v, {Page::All, {}}),             "abcd");   // e hidden out
    eq("Favorites",  sel(v, {Page::Favorites, {}}),       "a");      // e hidden out
    eq("Recent",     sel(v, {Page::RecentlyPlayed, {}}),  "ac");     // e hidden out
    eq("Installed",  sel(v, {Page::Installed, {}}),       "ad");     // a installed, d local
    eq("ReadyDL",    sel(v, {Page::ReadyToDownload, {}}), "b");      // server+missing
    eq("Updates",    sel(v, {Page::Updates, {}}),         "c");      // server+update
    eq("Hidden",     sel(v, {Page::Hidden, {}}),          "e");      // only hidden
    eq("Platform.PC",sel(v, {Page::Platform, "PC"}),      "ac");     // e hidden out
    eq("Platform.NES",sel(v, {Page::Platform, "NES"}),    "bd");
    eq("Collection", sel(v, {Page::Collection, "RPGs"}),  "ab");     // e hidden out

    // tabSelect label mapping.
    eqPage("tab.All",      gamefilter::tabSelect("All Games").page,        Page::All);
    eqPage("tab.Fav",      gamefilter::tabSelect("Favorites").page,        Page::Favorites);
    eqPage("tab.Recent",   gamefilter::tabSelect("Recently Played").page,  Page::RecentlyPlayed);
    eqPage("tab.Installed",gamefilter::tabSelect("Installed").page,        Page::Installed);
    eqPage("tab.ReadyDL",  gamefilter::tabSelect("Ready to Download").page,Page::ReadyToDownload);
    eqPage("tab.Updates",  gamefilter::tabSelect("Updates").page,          Page::Updates);
    eqPage("tab.Hidden",   gamefilter::tabSelect("Hidden").page,           Page::Hidden);
    {
        TabSel ps = gamefilter::tabSelect("PC");   // unknown label -> platform tab
        eqPage("tab.Platform", ps.page, Page::Platform);
        if (ps.arg != "PC") { std::printf("  FAIL tab.Platform arg \"%s\"\n", ps.arg.c_str()); ++g_fail; }
    }

    // installFromString accepts both capitalizations.
    if (gamefilter::installFromString("Installed") != Install::Installed ||
        gamefilter::installFromString("installed") != Install::Installed ||
        gamefilter::installFromString("Missing")   != Install::Missing   ||
        gamefilter::installFromString("updateAvailable") != Install::UpdateAvailable) {
        std::printf("  FAIL installFromString mapping\n");
        ++g_fail;
    }

    if (g_fail == 0)
        std::printf("filter self-check: OK — all tab-filter KATs passed\n");
    else
        std::printf("filter self-check: FAILED (%d)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
