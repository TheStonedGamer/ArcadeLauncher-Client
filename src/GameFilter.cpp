// GameFilter.cpp — portable sidebar tab / library-page filtering (Linux port
// L3d-c). Mirrors App::ApplyFilter's per-page rules on a UTF-8 item struct.

#include "GameFilter.h"

#include <algorithm>

namespace gamefilter {

Install installFromString(const std::string& s) {
    // Accept both the lowercase API spellings and the capitalized names
    // GameLibrary::Save writes ("Installed"/"UpdateAvailable"/"Missing"/…).
    if (s == "installed" || s == "Installed") return Install::Installed;
    if (s == "updateAvailable" || s == "UpdateAvailable") return Install::UpdateAvailable;
    if (s == "notInstalled" || s == "Missing") return Install::Missing;
    if (s == "downloading" || s == "Downloading") return Install::Downloading;
    if (s == "local" || s == "Local") return Install::Local;
    return Install::Unknown;
}

TabSel tabSelect(const std::string& label) {
    if (label == "All Games" || label == "All")      return {Page::All, {}};
    if (label == "Favorites")                         return {Page::Favorites, {}};
    if (label == "Recently Played")                   return {Page::RecentlyPlayed, {}};
    if (label == "Installed")                         return {Page::Installed, {}};
    if (label == "Ready to Download")                 return {Page::ReadyToDownload, {}};
    if (label == "Updates")                           return {Page::Updates, {}};
    if (label == "Hidden")                            return {Page::Hidden, {}};
    // Anything else is a platform tab (BuildSidebarEntries tags it Platform).
    return {Page::Platform, label};
}

bool passes(const Item& it, const TabSel& sel) {
    // Universal rule: hidden games appear only on the Hidden page.
    if (sel.page != Page::Hidden && it.hidden) return false;

    switch (sel.page) {
        case Page::All:
            return true;
        case Page::Favorites:
            return it.favorite;
        case Page::RecentlyPlayed:
            return it.lastPlayed > 0;
        case Page::Installed:
            // Local/manual entries always count as "installed"; server-backed
            // entries only when actually installed.
            return !it.serverBacked || it.install == Install::Installed;
        case Page::ReadyToDownload:
            return it.serverBacked && it.install == Install::Missing;
        case Page::Updates:
            return it.serverBacked && it.install == Install::UpdateAvailable;
        case Page::Hidden:
            return it.hidden;
        case Page::Collection:
            return std::find(it.collections.begin(), it.collections.end(), sel.arg)
                   != it.collections.end();
        case Page::Platform:
        default:
            return it.platform == sel.arg;
    }
}

} // namespace gamefilter
