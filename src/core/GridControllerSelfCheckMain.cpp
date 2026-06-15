// GridControllerSelfCheckMain.cpp — Phase L3d-b-4 headless KAT for the portable
// grid interaction controller. Pure CPU; gates in CI. Drives scroll clamping,
// hit-testing, click-to-select, select-into-view, and keyboard navigation
// against hand-computed values at a known viewport. Exit 0 on success.

#include "GridController.h"
#include "GridLayout.h"

#include <cmath>
#include <cstdio>

namespace {
int g_rc = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("grid controller: FAIL — %s\n", what); g_rc = 1; }
}
bool approx(float a, float b, float eps = 0.1f) { return std::fabs(a - b) < eps; }
} // namespace

int main() {
    using namespace grid;

    // 1024x680, 20 tiles: cols=4, rowHeight=304.4, rows=5,
    //   contentH = 64 + 16 + 5*304.4 = 1602, maxScroll = 1602-680 = 922.
    Controller c;
    c.setViewport(1024, 680);
    c.setCount(20);
    check(c.metrics().cols == 4, "cols=4");
    check(approx(c.maxScroll(), 922.0f), "maxScroll=922");

    // Wheel scroll clamps to [0, maxScroll].
    c.scrollBy(2000);
    check(approx(c.scrollOffset(), 922.0f), "scroll clamps to max");
    c.scrollBy(-5000);
    check(approx(c.scrollOffset(), 0.0f), "scroll clamps to 0");

    // Hit-test tile centers at scroll 0.
    Rect t0 = tileRect(c.metrics(), 0, 0.0f);
    Rect t5 = tileRect(c.metrics(), 5, 0.0f);
    check(c.hitTest(t0.x + t0.w / 2, t0.y + t0.h / 2) == 0, "hit tile0");
    check(c.hitTest(t5.x + t5.w / 2, t5.y + t5.h / 2) == 5, "hit tile5");
    check(c.hitTest(50, 200) == -1, "sidebar/gap -> -1");

    // Click selects the tile under the cursor.
    c.clickAt(t5.x + t5.w / 2, t5.y + t5.h / 2);
    check(c.selected() == 5, "click selects tile5");

    // Selecting a far-down tile scrolls it into view.
    //   index 19 (row 4): cardBottom=1564 > viewport bottom -> scroll = 1564-680+16 = 900.
    c.selectIndex(19);
    check(c.selected() == 19, "selectIndex(19)");
    check(approx(c.scrollOffset(), 900.0f), "select scrolls into view (900)");

    // Selecting tile 0 from a scrolled state scrolls back to top.
    c.selectIndex(0);
    check(c.selected() == 0, "selectIndex(0)");
    check(approx(c.scrollOffset(), 0.0f), "select tile0 scrolls to top");

    // Keyboard nav: from 0, right -> 1, down -> 5 (cols=4).
    c.moveSelection(1, 0);
    check(c.selected() == 1, "move right -> 1");
    c.moveSelection(0, 1);
    check(c.selected() == 5, "move down -> 5");
    // Left at column 0 is clamped (no wrap): from 4 (row1,col0) left stays 4.
    c.selectIndex(4);
    c.moveSelection(-1, 0);
    check(c.selected() == 4, "left at col0 clamps");

    // Short last row: 18 tiles, last row holds indices 16,17. Navigating past
    // the end clamps to the last existing tile.
    c.setCount(18);
    c.selectIndex(19);
    check(c.selected() == 17, "selectIndex past end clamps to 17");
    c.selectIndex(16);          // row4, col0
    c.moveSelection(3, 0);       // col -> 3, but only 16,17 exist -> clamp to 17
    check(c.selected() == 17, "move into missing last-row cell clamps");

    // Empty catalog: no selection, no scroll, no hits.
    c.setCount(0);
    check(c.selected() == -1, "empty -> no selection");
    check(approx(c.maxScroll(), 0.0f), "empty -> maxScroll 0");
    check(c.hitTest(t0.x + t0.w / 2, t0.y + t0.h / 2) == -1, "empty -> no hit");

    if (g_rc == 0) std::printf("grid controller: OK\n");
    return g_rc;
}
