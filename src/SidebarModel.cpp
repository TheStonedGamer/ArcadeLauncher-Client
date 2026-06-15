// SidebarModel.cpp — portable dynamic sidebar entry model (Linux port L3d-c-6).
// Mirrors Renderer::BuildSidebarEntries ordering on the UTF-8 item list.

#include "SidebarModel.h"

#include <algorithm>
#include <set>

namespace sidebar {

const std::vector<std::string>& platformOrder() {
    // Same order as BuildSidebarEntries' platform rows. Note Dolphin is absent
    // (GameCube and Wii are surfaced separately) and the stored catalog string
    // for Xbox 360 is "Xbox360" (PlatformName), which is what we match on.
    static const std::vector<std::string> kOrder = {
        "Steam", "Epic", "GOG", "GameCube", "Wii", "Ryujinx", "RPCS3",
        "N64", "NES", "SNES", "PS1", "PS2", "Xbox360", "Xbox", "PC",
    };
    return kOrder;
}

std::vector<Entry> build(const std::vector<gamefilter::Item>& items) {
    using gamefilter::Page;
    using gamefilter::TabSel;

    std::vector<Entry> v;
    auto fixed = [&](const char* label, Page p) {
        v.push_back({label, TabSel{p, {}}});
    };

    // Fixed special tabs (always present), in BuildSidebarEntries order. The
    // "Background Downloads" tab is intentionally absent (superseded by the live
    // download-status window) — matching both Windows and gamefilter's scope.
    fixed("All Games", Page::All);
    fixed("Favorites", Page::Favorites);
    fixed("Recently Played", Page::RecentlyPlayed);
    fixed("Installed", Page::Installed);
    fixed("Ready to Download", Page::ReadyToDownload);
    fixed("Updates", Page::Updates);

    // Collect what the catalog actually contains. Hidden items still count, so a
    // platform/collection that exists only as hidden games keeps its tab (its
    // games surface once the Hidden tab is active).
    std::set<std::string> platforms;
    std::set<std::string> collections;
    bool anyHidden = false;
    for (const auto& it : items) {
        if (!it.platform.empty()) platforms.insert(it.platform);
        for (const auto& c : it.collections)
            if (!c.empty()) collections.insert(c);
        if (it.hidden) anyHidden = true;
    }

    // Platform tabs: canonical order first (present only), then any unknown
    // platform appended in sorted order for a stable, future-proof tab.
    auto addPlatform = [&](const std::string& name) {
        v.push_back({name, TabSel{Page::Platform, name}});
    };
    for (const auto& name : platformOrder()) {
        auto found = platforms.find(name);
        if (found != platforms.end()) {
            addPlatform(name);
            platforms.erase(found);
        }
    }
    for (const auto& name : platforms)  // leftovers: std::set is already sorted
        addPlatform(name);

    // Collection tabs: unique, sorted (std::set ordering).
    for (const auto& c : collections)
        v.push_back({c, TabSel{Page::Collection, c}});

    // Hidden tab last, only when something is hidden.
    if (anyHidden)
        v.push_back({"Hidden", TabSel{Page::Hidden, {}}});

    return v;
}

} // namespace sidebar
