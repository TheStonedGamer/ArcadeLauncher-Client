// CatalogGridView.cpp — portable catalog-grid drawing (Linux port L3d-b-3).
// Draws through platform::IRenderer2D only; geometry comes from GridLayout.

#include "CatalogGridView.h"

namespace gridview {

using platform::Color;
using platform::Rect;
using platform::TextAlign;

namespace {
// 0xRRGGBB → Color (matches Renderer.cpp / GuiDemoMain hex()).
constexpr Color hex(unsigned v) {
    return Color{((v >> 16) & 0xFF) / 255.f, ((v >> 8) & 0xFF) / 255.f,
                 (v & 0xFF) / 255.f, 1.0f};
}
} // namespace

Install installFromString(const std::string& s) {
    if (s == "installed") return Install::Installed;
    if (s == "updateAvailable") return Install::UpdateAvailable;
    if (s == "notInstalled") return Install::NotInstalled;
    return Install::Unknown;
}

Theme darkTheme() {
    Theme th;
    th.bg        = hex(0x0D1117);  // C_BG
    th.sidebar   = hex(0x13181E);  // C_SIDEBAR
    th.topbar    = hex(0x161B22);  // C_TOPBAR
    th.card      = hex(0x21262D);  // C_CARD
    th.cardHover = hex(0x30363D);  // C_CARD_HOV
    th.text      = hex(0xC9D1D9);  // C_TEXT
    th.subtext   = hex(0x8B949E);  // C_SUBTEXT
    th.accent    = hex(0x58A6FF);  // C_ACCENT
    th.selected  = hex(0x388BFD);  // C_SELECTED
    th.border    = Color{1, 1, 1, 0.08f};
    return th;
}

void drawCard(platform::IRenderer2D& r, const Rect& cover, const Card& c,
              const Theme& th) {
    // Hovered/selected cards lift a couple px and get an accent ring.
    const float lift = c.selected ? 2.0f : (c.hovered ? 4.0f : 0.0f);
    Rect rect{cover.x, cover.y - lift, cover.w, cover.h};

    if (c.cover != 0) {
        // Real cover art: card backing, then the image clipped to the rounded card.
        r.fillRoundedRect(rect, 8, th.card);
        r.pushClip(rect);
        r.drawImage(c.cover, rect, 1.0f);
        r.popClip();
    } else {
        r.fillRoundedRect(rect, 8, c.placeholder);
    }

    // Border / selection ring.
    if (c.selected)
        r.strokeRoundedRect(rect, 8, th.accent, 2.0f);
    else if (c.hovered)
        r.strokeRoundedRect(rect, 8, th.selected, 1.5f);
    else
        r.strokeRoundedRect(rect, 8, th.border, 1.0f);

    // Platform pill (top-left). Suppressed in selection mode, where the corner
    // checkbox owns the top-left (mirrors Renderer.cpp).
    if (!c.platform.empty() && !c.selectionMode) {
        Rect pill{rect.x + 8, rect.y + 8, 56, 18};
        r.fillRoundedRect(pill, 9, Color{0, 0, 0, 0.45f});
        r.drawText(th.bodyFont, c.platform, pill.x + pill.w / 2, pill.y + 3,
                   th.text, TextAlign::Center);
    }

    // ROM-variant count badge (top-right, below the favorite disc): this tile
    // represents N dumps of one game. (Corner overlay — center untouched.)
    if (c.variantCount > 1) {
        const std::string vt = std::to_string(c.variantCount) + " versions";
        float tw = r.measureText(th.bodyFont, vt);
        float bw = tw + 12.0f;
        Rect vp{rect.x + rect.w - 8 - bw, rect.y + 30, bw, 19};
        r.fillRoundedRect(vp, 4, Color{0, 0, 0, 0.68f});
        r.strokeRoundedRect(vp, 4, Color{th.accent.r, th.accent.g, th.accent.b, 0.85f}, 1.0f);
        r.drawText(th.bodyFont, vt, vp.x + vp.w / 2, vp.y + 2, th.text, TextAlign::Center);
    }

    // Favorite star (top-right) — a gold disc on a dark scrim so it reads over
    // any cover. (Corner overlay: doesn't touch the card center.)
    if (c.favorite) {
        float fx = rect.x + rect.w - 16, fy = rect.y + 16;
        r.fillEllipse(fx, fy, 9, 9, Color{0, 0, 0, 0.45f});
        r.fillEllipse(fx, fy, 5, 5, Color{0.98f, 0.79f, 0.20f, 1.0f});  // gold
    }

    // Install-state dot (bottom-right): green=installed, blue=update, gray=not.
    if (c.install != Install::Unknown) {
        Color dot = c.install == Install::Installed
                        ? Color{0.22f, 0.78f, 0.35f, 1.0f}
                        : c.install == Install::UpdateAvailable
                              ? th.accent
                              : Color{0.55f, 0.58f, 0.62f, 1.0f};
        float dx = rect.x + rect.w - 14, dy = rect.y + rect.h - 14;
        r.fillEllipse(dx, dy, 7, 7, Color{0, 0, 0, 0.45f});
        r.fillEllipse(dx, dy, 4, 4, dot);
    }

    // Multi-select checkbox (top-left) — shown only in selection mode; a check
    // is drawn when this card is in the selection. Mirrors Renderer.cpp.
    if (c.selectionMode) {
        Rect box{rect.x + 10, rect.y + 10, 22, 22};
        r.fillRoundedRect(box, 4, Color{0, 0, 0, 0.62f});
        const Color edge = c.multiSelected ? th.accent : th.subtext;
        r.strokeRoundedRect(box, 4, edge, c.multiSelected ? 2.0f : 1.25f);
        if (c.multiSelected) {
            r.drawLine(box.x + 5, box.y + 12, box.x + 10, box.y + box.h - 5,
                       th.accent, 2.0f);
            r.drawLine(box.x + 10, box.y + box.h - 5, box.x + box.w - 5, box.y + 6,
                       th.accent, 2.0f);
        }
    }

    // Title under the cover.
    r.drawText(th.bodyFont, c.title, rect.x, rect.y + rect.h + 6, th.text,
               TextAlign::Left);
}

grid::Metrics drawGrid(platform::IRenderer2D& r, int w, int h, float scrollOffset,
                       const std::vector<Card>& cards,
                       const std::vector<std::string>& tabs, const Theme& th) {
    const grid::Metrics m = grid::Metrics::forViewport(w, h);
    r.beginFrame(w, h, 1.0f);

    // Chrome.
    r.fillRect(Rect{0, 0, (float)w, (float)h}, th.bg);
    r.fillRect(Rect{0, 0, m.sidebarW, (float)h}, th.sidebar);
    r.linearGradientRect(Rect{0, 0, (float)w, m.topbarH}, th.accent, th.selected, false);
    r.drawText(th.titleFont, "ArcadeLauncher", 16, 18, th.text, TextAlign::Left);

    float ty = m.topbarH + 20;
    for (const std::string& t : tabs) {
        r.drawText(th.bodyFont, t, 20, ty, th.subtext, TextAlign::Left);
        ty += 34;
    }

    // Tiles (placed + culled via the shared geometry).
    for (int i = 0; i < (int)cards.size(); ++i) {
        if (!grid::tileVisible(m, i, scrollOffset, h)) continue;
        grid::Rect g = grid::tileRect(m, i, scrollOffset);
        drawCard(r, Rect{g.x, g.y, g.w, g.h}, cards[i], th);
    }

    r.endFrame();
    return m;
}

} // namespace gridview
