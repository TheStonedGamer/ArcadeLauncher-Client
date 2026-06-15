#pragma once
// GameFilter.h — portable (UTF-8) sidebar tab / library-page filtering (Linux
// port L3d-c). The Windows App::ApplyFilter chooses which games a sidebar tab
// shows (All / Favorites / Recently Played / Installed / Ready to Download /
// Updates / Hidden / a platform / a collection). That selection logic lives
// here as one predicate so the Windows App and the Linux app filter the grid the
// same way, and one KAT (filter_selfcheck) locks the two in agreement.
//
// Mirrors App.cpp's per-page rules exactly, including the universal rule that
// hidden games appear ONLY on the dedicated Hidden page. The one page that is
// NOT expressible as a pure per-game predicate — Background Downloads, which
// depends on the live download queue — is intentionally out of scope here.
//
// Pure of Win32: operates on a small UTF-8 item struct. Used by gui_demo today;
// the Windows side keeps its wstring ApplyFilter and stays locked to this by KAT.

#include <cstdint>
#include <string>
#include <vector>

namespace gamefilter {

// The filterable sidebar pages (Background Downloads excluded — see header note).
enum class Page {
    All,
    Favorites,
    RecentlyPlayed,
    Installed,
    ReadyToDownload,
    Updates,
    Hidden,
    Platform,    // arg = platform name ("PC", "NES", …)
    Collection,  // arg = collection name
};

// Install state, mirroring GameLibrary's InstallState. installFromString accepts
// both the lowercase API spellings and the capitalized names Save writes.
enum class Install { Unknown, Missing, Installed, UpdateAvailable, Downloading, Local };
Install installFromString(const std::string& s);

// One game's filter-relevant fields (already narrowed to UTF-8 by the caller).
struct Item {
    std::string platform;                  // PlatformName, for Page::Platform
    Install     install      = Install::Unknown;
    bool        serverBacked = false;
    bool        favorite     = false;
    bool        hidden       = false;
    int64_t     lastPlayed   = 0;           // unix time; >0 ⇒ Recently Played
    std::vector<std::string> collections;   // for Page::Collection
};

// A resolved tab selection: which page, plus the platform/collection name when
// the page needs one.
struct TabSel {
    Page        page = Page::All;
    std::string arg;   // platform name (Page::Platform) or collection (Collection)
};

// Map a sidebar tab label to a TabSel. The fixed special labels map to their
// page; any other label is treated as a platform tab (TabSel{Platform, label}),
// matching how BuildSidebarEntries tags every non-special row LibraryPage::
// Platform. Collections can't be told apart from platforms by label alone, so
// callers that have a known collection name should construct TabSel directly.
TabSel tabSelect(const std::string& label);

// True when `it` should appear on `sel`'s page. Honors the universal rule:
// hidden games show only on Page::Hidden.
bool passes(const Item& it, const TabSel& sel);

} // namespace gamefilter
