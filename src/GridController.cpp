// GridController.cpp — portable catalog-grid interaction state (L3d-b-4).

#include "GridController.h"

#include <algorithm>

namespace grid {

void Controller::setViewport(int width, int height) {
    m_viewW = width;
    m_viewH = height;
    m_metrics = Metrics::forViewport(width, height);
    scrollTo(m_scroll);  // re-clamp against the new geometry
}

void Controller::setCount(int tileCount) {
    m_count = std::max(0, tileCount);
    if (m_selected >= m_count) m_selected = m_count - 1;  // -1 when empty
    scrollTo(m_scroll);  // re-clamp (content height changed)
}

float Controller::maxScroll() const {
    if (m_count <= 0 || m_metrics.cols <= 0) return 0.0f;
    const int rows = (m_count + m_metrics.cols - 1) / m_metrics.cols;
    // Total content height: top bar + top gap + `rows` full row pitches.
    const float contentH =
        m_metrics.topbarH + m_metrics.tileGap + rows * m_metrics.rowHeight();
    return std::max(0.0f, contentH - (float)m_viewH);
}

void Controller::scrollTo(float y) {
    m_scroll = std::clamp(y, 0.0f, maxScroll());
}

void Controller::scrollBy(float dy) {
    scrollTo(m_scroll + dy);
}

int Controller::hitTest(float x, float y) const {
    // Sidebar/top-bar clicks are not grid hits (mirrors Renderer::HitTestGrid's
    // guard — without it, a small negative offset truncates onto tile 0).
    if (x < m_metrics.sidebarW || y < m_metrics.topbarH) return -1;
    int idx = grid::hitTest(m_metrics, x, y, m_scroll);
    return (idx >= 0 && idx < m_count) ? idx : -1;
}

void Controller::clickAt(float x, float y) {
    int idx = hitTest(x, y);
    if (idx >= 0) m_selected = idx;
}

void Controller::selectIndex(int index) {
    if (index < -1) index = -1;
    if (index >= m_count) index = m_count - 1;
    m_selected = index;
    scrollSelectedIntoView();
}

void Controller::moveSelection(int dCols, int dRows) {
    if (m_count <= 0) return;
    if (m_selected < 0) { selectIndex(0); return; }
    const int cols = std::max(1, m_metrics.cols);
    int col = m_selected % cols, row = m_selected / cols;
    col = std::clamp(col + dCols, 0, cols - 1);
    row = std::max(0, row + dRows);
    int idx = row * cols + col;
    if (idx >= m_count) idx = m_count - 1;  // clamp into the (possibly short) last row
    selectIndex(idx);
}

void Controller::scrollSelectedIntoView() {
    if (m_selected < 0) return;
    float s = scrollForIndex(m_metrics, m_selected, m_scroll, (float)m_viewH);
    scrollTo(s);
}

} // namespace grid
