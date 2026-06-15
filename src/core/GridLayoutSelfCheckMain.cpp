// GridLayoutSelfCheckMain.cpp — Phase L3d-b headless KAT for the portable
// catalog-grid geometry (GridLayout). Pure CPU; gates in CI. Asserts the
// derived metrics, tile rects, hit-testing (including gap rejection), culling,
// and scroll-into-view against hand-computed values at a known viewport, so the
// Linux grid provably matches Renderer.cpp's Direct2D math. Exit 0 on success.

#include "GridLayout.h"

#include <cmath>
#include <cstdio>

namespace {
int g_rc = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("grid self-check: FAIL — %s\n", what); g_rc = 1; }
}
bool approx(float a, float b, float eps = 0.05f) { return std::fabs(a - b) < eps; }
} // namespace

int main() {
    using namespace grid;

    // At 1024x680 (the gui_demo size):
    //   sidebarW = clamp(1024*0.22=225.28, 176,220) = 220
    //   tileW    = clamp((1024-220-64)/4 = 185, 150,190) = 185
    //   tileH    = 185*1.44 = 266.4
    //   cols     = max(1, (int)((804+16)/(185+16))) = (int)(820/201) = 4
    //   rowH     = 266.4 + 16 + 22 = 304.4
    Metrics m = Metrics::forViewport(1024, 680);
    check(approx(m.sidebarW, 220.0f), "sidebarW=220 (clamped)");
    check(approx(m.tileW, 185.0f), "tileW=185");
    check(approx(m.tileH, 266.4f), "tileH=266.4");
    check(m.cols == 4, "cols=4");
    check(approx(m.rowHeight(), 304.4f), "rowHeight=304.4");

    // Tile 0 at scroll 0: x=236 (220+16), y=80 (64+16).
    Rect t0 = tileRect(m, 0, 0.0f);
    check(approx(t0.x, 236.0f) && approx(t0.y, 80.0f), "tile0 origin (236,80)");
    check(approx(t0.w, 185.0f) && approx(t0.h, 266.4f), "tile0 size");

    // Tile 5 = col 1, row 1: x=437 (236+201), y=384.4 (80+304.4).
    Rect t5 = tileRect(m, 5, 0.0f);
    check(approx(t5.x, 437.0f) && approx(t5.y, 384.4f), "tile5 (437,384.4)");

    // Scroll shifts tiles up by the offset.
    Rect t0s = tileRect(m, 0, 100.0f);
    check(approx(t0s.y, -20.0f), "tile0 y shifts with scroll");

    // Hit-test the center of tile 0 → 0.
    check(hitTest(m, t0.x + t0.w / 2, t0.y + t0.h / 2, 0.0f) == 0, "hit tile0 center");
    // Hit-test the center of tile 5 → 5.
    check(hitTest(m, t5.x + t5.w / 2, t5.y + t5.h / 2, 0.0f) == 5, "hit tile5 center");
    // A point in the horizontal gap between tile 0 and tile 1 → -1.
    check(hitTest(m, 428.0f, 213.0f, 0.0f) == -1, "gap between cols rejected");
    // A point left of the whole grid (col index goes negative) → -1. NOTE: like
    // Renderer::HitTestGrid, this only rejects once the offset is a full column
    // pitch left of the grid origin — a click *inside* the sidebar band is the
    // caller's responsibility to route (App.cpp tests the sidebar first), so we
    // don't assert -1 for a shallow sidebar click here.
    check(hitTest(m, 10.0f, 213.0f, 0.0f) == -1, "far-left click rejected");

    // Culling: tile 0 visible at scroll 0; a far-down tile not visible.
    check(tileVisible(m, 0, 0.0f, 680), "tile0 visible");
    check(!tileVisible(m, 20, 0.0f, 680), "tile20 culled when far below");

    // Scroll-into-view: bringing tile 20 (row 5) into a 680px viewport.
    //   cardTop = 64+16+5*304.4 = 1602; cardBottom = 1868.4
    //   below viewport → scroll = cardBottom - 680 + 16 = 1204.4
    float s = scrollForIndex(m, 20, 0.0f, 680.0f);
    check(approx(s, 1204.4f), "scrollForIndex(20) = 1204.4");
    // After that scroll, tile 20 is visible.
    check(tileVisible(m, 20, s, 680), "tile20 visible after scroll");
    // An already-visible tile leaves the scroll unchanged.
    check(approx(scrollForIndex(m, 0, 0.0f, 680.0f), 0.0f), "visible tile keeps scroll");

    // Narrow window clamps tile width up and column count down.
    Metrics narrow = Metrics::forViewport(640, 480);
    check(narrow.cols >= 1, "narrow cols >= 1");
    check(narrow.tileW >= 150.0f && narrow.tileW <= 190.0f, "narrow tileW clamped");

    if (g_rc == 0) std::printf("grid self-check: OK\n");
    return g_rc;
}
