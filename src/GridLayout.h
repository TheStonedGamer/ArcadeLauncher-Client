#pragma once
// GridLayout.h — portable catalog-grid geometry (Linux port L3d-b). Pure CPU,
// no Win32/Direct2D/nanovg — just the math the catalog grid uses to lay out
// game tiles, hit-test clicks, cull off-screen rows, and scroll a selection
// into view.
//
// This is extracted verbatim from Renderer.cpp's Direct2D grid (Resize /
// DrawGrid / HitTestGrid / ScrollForSelected) so both platforms share ONE
// source of truth for the layout. Windows' Renderer.cpp can adopt these
// functions later; until then it keeps its own copy and this is verified
// against it by grid_selfcheck. The Linux gui_demo already drives its grid
// through here, so the portable renderer lays tiles out exactly like Windows.
//
// Coordinates are top-down pixels (y grows downward), matching the renderer.

namespace grid {

// Layout constants — the fixed metrics from Renderer.h. Tile width/height and
// column count are derived from the viewport in Metrics::forViewport().
constexpr float kTopbarH   = 64.0f;   // m_topbarH
constexpr float kTileGap   = 16.0f;   // m_tileGap
constexpr float kTitleStrip = 22.0f;  // title text drawn below each cover

struct Rect { float x, y, w, h; };

// Derived metrics for a given window size. Mirrors Renderer::Resize exactly.
struct Metrics {
    float sidebarW = 200.0f;
    float topbarH  = kTopbarH;
    float tileW    = 180.0f;
    float tileH    = 260.0f;
    float tileGap  = kTileGap;
    int   cols     = 5;

    // Row pitch: cover height + gap + the title strip below the cover.
    float rowHeight() const { return tileH + tileGap + kTitleStrip; }

    // Compute the metrics for a window of `width` x `height` px.
    static Metrics forViewport(int width, int height);
};

// Cover rect of tile `index` given a vertical scroll offset (px scrolled down).
Rect tileRect(const Metrics& m, int index, float scrollOffset);

// True if tile `index` is at all within the viewport (used to cull draws).
bool tileVisible(const Metrics& m, int index, float scrollOffset, int viewportH);

// Map a point (px,py) to a game index, or -1 if it isn't on a tile. Mirrors
// Renderer::HitTestGrid (rejects clicks in the inter-tile gaps).
int hitTest(const Metrics& m, float px, float py, float scrollOffset);

// Scroll offset that brings tile `index` fully into view, clamped at 0.
// Returns currentScroll unchanged if it is already visible.
float scrollForIndex(const Metrics& m, int index, float currentScroll,
                     float viewportH);

} // namespace grid
