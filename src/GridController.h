#pragma once
// GridController.h — portable catalog-grid interaction state (Linux port
// L3d-b-4). Pure logic, no drawing/GL/Win32: owns the scroll offset and the
// selected tile, and turns input (mouse wheel, clicks, arrow keys) into state
// changes using the shared GridLayout geometry. Both platforms can drive their
// grid through this; the Linux app uses it to become interactive, and Windows
// can adopt it later without touching its rich DrawCard.

#include "GridLayout.h"

namespace grid {

class Controller {
public:
    // Geometry / data the controller operates over.
    void setViewport(int width, int height);
    void setCount(int tileCount);

    int   count() const { return m_count; }
    float scrollOffset() const { return m_scroll; }
    int   selected() const { return m_selected; }
    const Metrics& metrics() const { return m_metrics; }

    // Largest valid scroll offset (0 if everything fits).
    float maxScroll() const;

    // Mouse wheel: positive dy scrolls down. Clamps to [0, maxScroll].
    void scrollBy(float dy);
    // Set an absolute scroll, clamped.
    void scrollTo(float y);

    // Map a point to a tile index (or -1), via GridLayout (scroll-aware).
    int hitTest(float x, float y) const;
    // Click: select the tile under the point (no-op selection if it's a gap).
    void clickAt(float x, float y);

    // Select an index (clamped to range, or -1 to clear) and scroll it visible.
    void selectIndex(int index);
    // Keyboard navigation by columns/rows; scrolls the new selection into view.
    void moveSelection(int dCols, int dRows);

private:
    void scrollSelectedIntoView();

    Metrics m_metrics;
    int   m_viewW = 0, m_viewH = 0;
    int   m_count = 0;
    int   m_selected = -1;
    float m_scroll = 0.0f;
};

} // namespace grid
