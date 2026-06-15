#pragma once
// SidebarModel.h — portable (UTF-8) dynamic sidebar entry model (Linux port
// L3d-c-6). The Windows Renderer::BuildSidebarEntries decides which tabs the
// sidebar shows and in what order: the fixed special tabs, then one tab per
// platform that is present, then one per collection, then Hidden (only when
// something is hidden). That entry-list logic lives here as one builder so the
// Windows app and the Linux app grow the same sidebar, and one KAT
// (sidebar_selfcheck) locks the two in agreement.
//
// The list is built from the catalog itself (the platforms / collections that
// actually occur, and whether anything is hidden), so it is genuinely dynamic:
// adding a game on a new platform adds its tab; emptying a collection removes
// it. Each entry carries a gamefilter::TabSel, so the same predicate that backs
// the filter (gamefilter::passes) drives the tab — model and filter stay locked.
//
// Pure of Win32: operates on the UTF-8 gamefilter::Item list the app already
// builds for filtering. The Windows side keeps its wstring BuildSidebarEntries
// (which surfaces every configured platform/collection regardless of contents)
// and stays pinned to this ordering by the KAT.

#include "GameFilter.h"

#include <string>
#include <vector>

namespace sidebar {

// One sidebar row: its display label plus the tab selection it activates. For
// platform/collection rows the label is also the filter arg (the catalog
// platform string / collection name), so label and filter can never disagree.
struct Entry {
    std::string        label;
    gamefilter::TabSel sel;
};

// The canonical platform ordering BuildSidebarEntries uses (Dolphin folded into
// GameCube/Wii, so it is intentionally absent). Platform tabs appear in this
// order; any platform present but not listed here is appended afterwards,
// sorted, so a future platform still gets a stable, deterministic tab.
const std::vector<std::string>& platformOrder();

// Build the sidebar entry list from the catalog items, mirroring
// BuildSidebarEntries' order: All Games, Favorites, Recently Played, Installed,
// Ready to Download, Updates, then platform tabs (canonical order, present
// only), then collection tabs (unique, sorted), then Hidden (only if any item
// is hidden). Hidden items still count toward the platform/collection sets so a
// platform that exists only as hidden games still gets its tab.
std::vector<Entry> build(const std::vector<gamefilter::Item>& items);

} // namespace sidebar
