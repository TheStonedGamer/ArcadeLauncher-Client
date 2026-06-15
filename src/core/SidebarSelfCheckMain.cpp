// SidebarSelfCheckMain.cpp — headless KAT for the portable dynamic sidebar
// entry model (Linux port L3d-c-6). Locks sidebar::build against the Windows
// Renderer::BuildSidebarEntries ordering: fixed special tabs, then platform
// tabs in canonical order (present only), then collection tabs (unique,
// sorted), then Hidden (only when something is hidden). Also checks that each
// platform/collection entry carries the matching gamefilter::TabSel. Exit 0 on
// success.

#include "SidebarModel.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {
using gamefilter::Install;
using gamefilter::Item;
using gamefilter::Page;

int g_fail = 0;

// Join the entry labels with '|', e.g. "All Games|Favorites|...".
std::string labels(const std::vector<sidebar::Entry>& v) {
    std::string out;
    for (const auto& e : v) {
        if (!out.empty()) out += '|';
        out += e.label;
    }
    return out;
}

void eq(const char* what, const std::string& got, const std::string& want) {
    if (got != want) {
        std::printf("  FAIL %s:\n    got  \"%s\"\n    want \"%s\"\n", what,
                    got.c_str(), want.c_str());
        ++g_fail;
    }
}

Item mk(const char* platform, bool hidden, std::vector<std::string> cols) {
    Item it;
    it.platform = platform;
    it.hidden = hidden;
    it.collections = std::move(cols);
    return it;
}
} // namespace

int main() {
    const std::string fixed =
        "All Games|Favorites|Recently Played|Installed|Ready to Download|Updates";

    // 1. Platforms appear in canonical order regardless of catalog order, and
    //    only the ones present. Here: PC, NES, GameCube present (PS2 absent).
    //    Canonical order puts GameCube before NES before PC.
    {
        std::vector<Item> items = {
            mk("PC", false, {}),
            mk("NES", false, {}),
            mk("GameCube", false, {}),
            mk("PC", false, {}),  // duplicate platform -> single tab
        };
        eq("platforms canonical order",
           labels(sidebar::build(items)),
           fixed + "|GameCube|NES|PC");
    }

    // 2. Collections: unique + sorted, after platforms; Hidden only when hidden.
    {
        std::vector<Item> items = {
            mk("PC", false, {"RPGs", "Shooters"}),
            mk("NES", false, {"RPGs"}),           // dup collection -> one tab
            mk("PC", true,  {"Hidden Gems"}),     // hidden game -> Hidden tab
        };
        eq("collections sorted + hidden",
           labels(sidebar::build(items)),
           fixed + "|NES|PC|Hidden Gems|RPGs|Shooters|Hidden");
    }

    // 3. No platforms/collections/hidden -> just the fixed tabs.
    eq("empty catalog", labels(sidebar::build({})), fixed);

    // 4. An unknown (non-canonical) platform is appended after the canonical
    //    ones, sorted among other unknowns.
    {
        std::vector<Item> items = {
            mk("Dreamcast", false, {}),  // not in canonical order
            mk("PC", false, {}),
            mk("Amiga", false, {}),      // not in canonical order
        };
        eq("unknown platforms appended sorted",
           labels(sidebar::build(items)),
           fixed + "|PC|Amiga|Dreamcast");
    }

    // 5. A platform that exists only as a hidden game still gets its tab.
    {
        std::vector<Item> items = { mk("SNES", true, {}) };
        eq("hidden-only platform keeps tab",
           labels(sidebar::build(items)),
           fixed + "|SNES|Hidden");
    }

    // 6. Each entry carries a TabSel that filters correctly: platform/collection
    //    entries match their arg & page; fixed entries map to their page.
    {
        std::vector<Item> items = {
            mk("NES", false, {"Faves"}),
        };
        auto v = sidebar::build(items);
        for (const auto& e : v) {
            if (e.label == "NES") {
                if (e.sel.page != Page::Platform || e.sel.arg != "NES") {
                    std::printf("  FAIL NES entry sel mismatch\n");
                    ++g_fail;
                }
            } else if (e.label == "Faves") {
                if (e.sel.page != Page::Collection || e.sel.arg != "Faves") {
                    std::printf("  FAIL Faves entry sel mismatch\n");
                    ++g_fail;
                }
            } else if (e.label == "All Games" && e.sel.page != Page::All) {
                std::printf("  FAIL All Games entry sel mismatch\n");
                ++g_fail;
            }
        }
        // The platform entry's predicate must actually pass the NES item.
        gamefilter::TabSel nesSel{Page::Platform, "NES"};
        if (!gamefilter::passes(items[0], nesSel)) {
            std::printf("  FAIL NES predicate did not pass NES item\n");
            ++g_fail;
        }
    }

    if (g_fail == 0)
        std::printf("sidebar self-check: OK — all dynamic-sidebar KATs passed\n");
    else
        std::printf("sidebar self-check: FAILED (%d)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
