#pragma once
// CatalogGridView.h — portable catalog-grid *drawing* (Linux port L3d-b-3).
//
// Pairs with GridLayout (geometry) to give the catalog grid one cross-platform
// implementation: GridLayout says WHERE each tile goes, CatalogGridView says
// HOW a tile is painted — entirely through platform::IRenderer2D primitives
// (the same conceptual surface Renderer.cpp's Direct2D DrawCard uses). The
// Linux gui_demo draws through this today; Windows Renderer.cpp adopts it once
// its IRenderer2D wrapper lands (L1b), replacing the bespoke ID2D1 DrawCard.
//
// Pure of any platform/back-end specifics: it only calls the IRenderer2D
// interface and the portable GridLayout, so it links into both arcade_gui
// (Linux) and the Windows build.

#include "Platform/Renderer2D.h"
#include "GridLayout.h"

#include <string>
#include <vector>

namespace gridview {

// Palette + fonts the grid draws with. Mirrors Renderer.cpp's GitHub-dark
// constants; the caller supplies loaded font handles.
struct Theme {
    platform::Color bg, sidebar, topbar, card, cardHover, text, subtext,
        accent, selected, border;
    platform::FontId titleFont = 0;
    platform::FontId bodyFont  = 0;
};

// Install state, shown as a small corner dot. Mirrors GameLibrary's
// installState strings; use installFromString() to map.
enum class Install { Unknown, NotInstalled, Installed, UpdateAvailable };
Install installFromString(const std::string& s);

// One tile's display model. Cover art is an already-uploaded image handle (0 =
// none → draw the placeholder color instead).
struct Card {
    std::string title;
    std::string platform;
    platform::ImageId cover = 0;
    platform::Color   placeholder{0.13f, 0.15f, 0.18f, 1.0f};
    bool    hovered  = false;
    bool    selected = false;
    bool    favorite = false;
    Install install  = Install::Unknown;
    // ROM-variant count: when >1 the tile represents N dumps of one game and
    // draws a "N versions" badge (top-right), mirroring Renderer.cpp.
    int     variantCount = 1;
    // Multi-select: when the grid is in selection mode every card shows a
    // corner checkbox; `multiSelected` ticks it. Mirrors Renderer.cpp.
    bool    selectionMode = false;
    bool    multiSelected = false;
};

// The GitHub-dark theme Renderer.cpp uses (fonts left 0 — caller fills them in).
Theme darkTheme();

// Draw a single card into its cover rect. Hover/selection add a lift + accent
// ring, matching the Windows card treatment.
void drawCard(platform::IRenderer2D& r, const platform::Rect& cover,
              const Card& c, const Theme& th);

// Draw the whole grid: background, sidebar, top bar, and every visible tile
// (laid out + culled via GridLayout). `tabs` are the sidebar entries. Returns
// the metrics used (handy for hit-testing the same frame).
grid::Metrics drawGrid(platform::IRenderer2D& r, int w, int h, float scrollOffset,
                       const std::vector<Card>& cards,
                       const std::vector<std::string>& tabs, const Theme& th);

} // namespace gridview
