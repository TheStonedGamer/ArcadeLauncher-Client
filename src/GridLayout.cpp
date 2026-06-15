// GridLayout.cpp — portable catalog-grid geometry (Linux port L3d-b).
// Each function mirrors its Renderer.cpp counterpart line-for-line so the
// Windows Direct2D grid and the Linux nanovg grid lay out identically.

#include "GridLayout.h"

#include <algorithm>
#include <cmath>

namespace grid {

Metrics Metrics::forViewport(int width, int height) {
    (void)height;  // height doesn't affect tile sizing, only culling/scroll
    Metrics m;
    const float w = (float)width;
    // Renderer::Resize:
    m.sidebarW = std::clamp(w * 0.22f, 176.0f, 220.0f);
    m.tileW    = std::clamp((w - m.sidebarW - 64.0f) / 4.0f, 150.0f, 190.0f);
    m.tileH    = m.tileW * 1.44f;
    const float gridW = w - m.sidebarW;
    m.cols = std::max(1, (int)((gridW + m.tileGap) / (m.tileW + m.tileGap)));
    return m;
}

Rect tileRect(const Metrics& m, int index, float scrollOffset) {
    // Renderer::DrawGrid:
    const float startX = m.sidebarW + m.tileGap;
    const float startY = m.topbarH + m.tileGap - scrollOffset;
    const int col = index % m.cols;
    const int row = index / m.cols;
    const float x = startX + col * (m.tileW + m.tileGap);
    const float y = startY + row * m.rowHeight();
    return Rect{x, y, m.tileW, m.tileH};
}

bool tileVisible(const Metrics& m, int index, float scrollOffset, int viewportH) {
    const Rect r = tileRect(m, index, scrollOffset);
    // Same cull test DrawGrid uses (account for the title strip below the cover).
    if (r.y + m.tileH + kTitleStrip < m.topbarH) return false;
    if (r.y > (float)viewportH) return false;
    return true;
}

int hitTest(const Metrics& m, float px, float py, float scrollOffset) {
    // Renderer::HitTestGrid:
    const float gx = px - m.sidebarW - m.tileGap;
    const float gy = py - m.topbarH - m.tileGap + scrollOffset;
    const float colPitch = m.tileW + m.tileGap;
    const float rowPitch = m.rowHeight();
    const int col = (int)(gx / colPitch);
    const int row = (int)(gy / rowPitch);
    if (col < 0 || col >= m.cols || row < 0) return -1;
    // Reject clicks that land in the gap rather than on a tile.
    const float localX = std::fmod(gx, colPitch);
    const float localY = std::fmod(gy, rowPitch);
    if (localX > m.tileW || localY > m.tileH) return -1;
    return row * m.cols + col;
}

float scrollForIndex(const Metrics& m, int index, float currentScroll,
                     float viewportH) {
    // Renderer::ScrollForSelected:
    if (index < 0 || m.cols <= 0) return currentScroll;
    const int row = index / m.cols;
    const float rowH = m.rowHeight();
    const float cardTop    = m.topbarH + m.tileGap + (float)row * rowH;
    const float cardBottom = cardTop + m.tileH;

    const float screenTop    = cardTop    - currentScroll;
    const float screenBottom = cardBottom - currentScroll;

    if (screenTop < m.topbarH + 4.0f)
        return std::max(0.0f, cardTop - m.topbarH - m.tileGap);
    if (screenBottom > viewportH - 4.0f)
        return cardBottom - viewportH + m.tileGap;
    return currentScroll;
}

} // namespace grid
