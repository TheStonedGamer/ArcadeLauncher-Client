// GuiDemoMain.cpp — Phase L3b/L3d GUI: the catalog grid drawn through the
// SHARED, portable view code (gridview::drawGrid → IRenderer2D), with tiles
// placed by the shared GridLayout. This is the same drawing path Renderer.cpp
// adopts on Windows once its IRenderer2D wrapper lands — gui_demo just exercises
// it on Linux (nanovg/GL) and verifies real pixels.
//
//   ./gui_demo                       # built-in demo scene (deterministic check)
//   ./gui_demo --hold                # interactive: keep the window open
//   ./gui_demo <library.json>        # render the REAL catalog (L3d), real covers
//   ./gui_demo --hold <library.json> # interactive, real catalog
//
// With no path it draws the demo scene and verifies a known tile color. With a
// library.json it parses the real catalog (catalog::loadFile), decodes cover art
// from coverArtPath, and verifies a non-empty frame rendered. Exit 0 on success;
// SKIP (exit 0) if there is no display.

#include "Platform/Window.h"
#include "Platform/Renderer2D.h"
#include "Platform/Image.h"
#include "Catalog.h"
#include "GridLayout.h"
#include "GridController.h"
#include "CatalogGridView.h"

#include <GL/glew.h>
#include <SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace platform;

namespace {

// Synthesize a cover image (a diagonal gradient with a known center color) and
// encode→decode it through the platform codec, so the tile shows a *real*
// decoded image, exercising decodeImageRGBA + createImageRGBA + drawImage.
DecodedImage makeCoverImage(Color tint) {
    const int W = 64, H = 84;
    std::vector<uint8_t> rgba((size_t)W * H * 4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            float t = (float)(x + y) / (W + H);
            size_t i = ((size_t)y * W + x) * 4;
            rgba[i + 0] = (uint8_t)(tint.r * 255 * (0.5f + 0.5f * t));
            rgba[i + 1] = (uint8_t)(tint.g * 255 * (0.5f + 0.5f * t));
            rgba[i + 2] = (uint8_t)(tint.b * 255 * (0.5f + 0.5f * t));
            rgba[i + 3] = 255;
        }
    std::vector<uint8_t> png = encodePngRGBA(rgba.data(), W, H);
    DecodedImage img;
    if (!png.empty()) decodeImageRGBA(png.data(), png.size(), img);
    return img;
}

// Demo data: title, platform, flat cover color.
struct DemoEntry { const char* title; const char* platform; Color cover; };
std::vector<DemoEntry> demoEntries() {
    return {
        {"Hollow Knight",        "PC",       {0.16f, 0.42f, 0.55f, 1}},
        {"Celeste",             "PC",       {0.85f, 0.35f, 0.45f, 1}},
        {"Hades",               "PC",       {0.80f, 0.30f, 0.20f, 1}},
        {"Metroid Prime",       "GameCube", {0.20f, 0.55f, 0.35f, 1}},
        {"God of War",          "PS2",      {0.65f, 0.18f, 0.18f, 1}},
        {"Super Mario Galaxy",  "Wii",      {0.20f, 0.35f, 0.75f, 1}},
        {"Halo: CE",            "Xbox",     {0.25f, 0.50f, 0.25f, 1}},
        {"Chrono Trigger",      "SNES",     {0.45f, 0.30f, 0.70f, 1}},
    };
}

// Stable placeholder cover color for a platform string (cheap FNV hash → hue).
Color colorForPlatform(const std::string& p) {
    uint32_t h = 2166136261u;
    for (char c : p) { h ^= (uint8_t)c; h *= 16777619u; }
    float r = 0.30f + ((h >> 0) & 0x3F) / 255.0f;
    float g = 0.30f + ((h >> 6) & 0x3F) / 255.0f;
    float b = 0.30f + ((h >> 12) & 0x3F) / 255.0f;
    return Color{r, g, b, 1};
}

bool writePpm(const char* path, const std::vector<uint8_t>& rgb, int w, int h) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y)
        std::fwrite(&rgb[(size_t)y * w * 3], 1, (size_t)w * 3, f);
    std::fclose(f);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    bool hold = false;
    std::string catalogPath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hold") == 0) hold = true;
        else catalogPath = argv[i];
    }
    const int W = 1024, H = 680;

    auto win = makeWindow("ArcadeLauncher (Linux) — catalog grid", W, H);
    if (!win) { std::printf("gui demo: SKIP (no display: %s)\n", SDL_GetError()); return 0; }
    win->show(true);

    auto r = makeRenderer(win.get());
    if (!r) { std::printf("gui demo: FAILED (renderer init: nanovg/GL)\n"); return 1; }

    gridview::Theme theme = gridview::darkTheme();
    theme.titleFont = r->loadFont("", 22, true);
    theme.bodyFont  = r->loadFont("", 15, false);

    const std::vector<std::string> tabs = {
        "All Games", "PC", "Consoles", "Favorites", "Downloads"};

    // L3d: render the REAL catalog when a library.json is supplied; otherwise the
    // built-in demo scene (which has the deterministic pixel anchor).
    std::vector<gridview::Card> cards;
    bool fromCatalog = false;
    size_t catalogCount = 0;

    if (!catalogPath.empty()) {
        auto games = catalog::loadFile(catalogPath);
        catalogCount = games.size();
        if (!games.empty()) {
            fromCatalog = true;
            for (const auto& g : games) {
                if (g.hidden) continue;
                gridview::Card c;
                c.title = g.title.empty() ? g.id : g.title;
                c.platform = g.platform;
                c.placeholder = colorForPlatform(g.platform);
                if (!g.coverArtPath.empty()) {
                    DecodedImage img;
                    if (decodeImageFileRGBA(g.coverArtPath, img) && img.valid())
                        c.cover = r->createImageRGBA(img.rgba.data(), img.w, img.h);
                }
                cards.push_back(std::move(c));
            }
        }
    }
    if (!fromCatalog) {
        auto entries = demoEntries();
        for (int i = 0; i < (int)entries.size(); ++i) {
            gridview::Card c;
            c.title = entries[i].title;
            c.platform = entries[i].platform;
            c.placeholder = entries[i].cover;
            // Tile 0 keeps a flat placeholder as the deterministic verification
            // anchor; the rest get real decoded cover art (PNG round-tripped).
            if (i > 0) {
                DecodedImage img = makeCoverImage(entries[i].cover);
                if (img.valid())
                    c.cover = r->createImageRGBA(img.rgba.data(), img.w, img.h);
            }
            cards.push_back(std::move(c));
        }
    }

    int w = W, h = H; win->size(w, h);

    // Interactive state (scroll + selection) via the shared portable controller.
    grid::Controller ctrl;
    ctrl.setViewport(w, h);
    ctrl.setCount((int)cards.size());

    auto render = [&]() {
        for (int i = 0; i < (int)cards.size(); ++i)
            cards[i].selected = (i == ctrl.selected());
        gridview::drawGrid(*r, w, h, ctrl.scrollOffset(), cards, tabs, theme);
    };

    bool quit = false;
    // Render a few identical frames; the back buffer then holds a full scene we
    // can read deterministically (front is undefined under a compositor). Scroll
    // is 0 and nothing is selected here, so tile0 stays the verification anchor.
    for (int f = 0; f < 3 && !quit; ++f) {
        Event ev; while (win->poll(ev)) if (ev.type == EventType::Quit) quit = true;
        render();
        SDL_Delay(16);
    }

    glReadBuffer(GL_BACK);
    std::vector<uint8_t> buf((size_t)w * h * 3, 0);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, buf.data());
    bool wrote = writePpm("gui_demo.ppm", buf, w, h);

    const grid::Metrics gm = grid::Metrics::forViewport(w, h);
    auto sampleTile = [&](int i, int& gr, int& gg, int& gb) {
        grid::Rect c = grid::tileRect(gm, i, 0.0f);
        int sx = (int)(c.x + c.w / 2), sy = (int)(c.y + c.h / 2);
        // glReadPixels rows are bottom-up; flip the top-down y.
        size_t idx = ((size_t)(h - 1 - sy) * w + sx) * 3;
        gr = buf[idx]; gg = buf[idx + 1]; gb = buf[idx + 2];
    };
    auto near = [](int a, int b) { return a - b <= 10 && b - a <= 10; };

    bool ok;
    int gr, gg, gb;
    sampleTile(0, gr, gg, gb);
    if (fromCatalog) {
        const Color bg = theme.bg;
        const int bgr = (int)(bg.r * 255), bgg = (int)(bg.g * 255),
                  bgb = (int)(bg.b * 255);
        bool drew = !(near(gr, bgr) && near(gg, bgg) && near(gb, bgb));
        ok = !cards.empty() && drew;
        std::printf("gui demo: %s — %dx%d, catalog %zu games → %zu tiles; "
                    "tile0 center (%d,%d,%d) != bg; wrote %s\n",
                    ok ? "OK" : "FAILED", w, h, catalogCount, cards.size(),
                    gr, gg, gb, wrote ? "gui_demo.ppm" : "<ppm failed>");
    } else {
        // Demo: deterministic — tile0 center must equal its flat placeholder color.
        Color p = cards[0].placeholder;
        int er = (int)(p.r * 255 + 0.5f), eg = (int)(p.g * 255 + 0.5f),
            eb = (int)(p.b * 255 + 0.5f);
        ok = near(gr, er) && near(gg, eg) && near(gb, eb);
        std::printf("gui demo: %s — %dx%d, %zu tiles; tile0 center (%d,%d,%d) "
                    "expected ~(%d,%d,%d); wrote %s\n",
                    ok ? "OK" : "FAILED", w, h, cards.size(), gr, gg, gb,
                    er, eg, eb, wrote ? "gui_demo.ppm" : "<ppm failed>");
    }

    if (hold) {
        std::printf("gui demo: interactive — wheel scrolls, click/arrows select, "
                    "Esc/close to exit.\n");
        while (!quit) {
            Event ev;
            while (win->poll(ev)) {
                switch (ev.type) {
                    case EventType::Quit: quit = true; break;
                    case EventType::Resize:
                        w = ev.width; h = ev.height; ctrl.setViewport(w, h);
                        break;
                    case EventType::MouseWheel:
                        ctrl.scrollBy(-ev.wheel * 80.0f);  // wheel up scrolls up
                        break;
                    case EventType::MouseDown:
                        if (ev.button == MouseButton::Left) ctrl.clickAt(ev.x, ev.y);
                        break;
                    case EventType::KeyDown:
                        switch (ev.key) {
                            case Key::Escape: quit = true; break;
                            case Key::Left:  ctrl.moveSelection(-1, 0); break;
                            case Key::Right: ctrl.moveSelection(1, 0);  break;
                            case Key::Up:    ctrl.moveSelection(0, -1); break;
                            case Key::Down:  ctrl.moveSelection(0, 1);  break;
                            case Key::PageDown: ctrl.scrollBy(h * 0.9f); break;
                            case Key::PageUp:   ctrl.scrollBy(-h * 0.9f); break;
                            default: break;
                        }
                        break;
                    default: break;
                }
            }
            render();
            SDL_Delay(16);
        }
    }
    return ok ? 0 : 1;
}
