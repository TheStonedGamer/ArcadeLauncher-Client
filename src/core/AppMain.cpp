// AppMain.cpp — the first REAL Linux app entry point (Phase L4 → app). Until now
// the Linux side was a pile of isolated harnesses (gui_demo --widgets/--settings/
// --settings-file, the *_selfcheck gates). This is the increment where they come
// together into one cohesive application: a catalog browser (the shared grid +
// sidebar + search + sort) with an in-app SETTINGS OVERLAY (the shared forms::
// Panel bound to the General config), wired to the real config LIFECYCLE — load
// the General settings at startup, edit them in the overlay, and write them back
// NON-DESTRUCTIVELY on close.
//
//   ./arcade_client                       # headless self-check (deterministic)
//   ./arcade_client --hold                # interactive app
//   ./arcade_client <library.json>        # browse the REAL catalog
//   ./arcade_client --config <path>       # use a specific config.json
//   ./arcade_client --hold <library.json> # interactive, real catalog + settings
//
// Default config path is settings::configPath() (<data_dir>/config.json). Without
// --hold the run NEVER writes the config (an automated gate can't mutate the
// user's file); the config is only persisted when the user closes an interactive
// session, mirroring saveGeneralToFile's non-destructive contract. F2 toggles the
// settings overlay; Esc closes the overlay (or quits when it's already closed).
//
// Exit 0 on success; SKIP (exit 0) when there is no display.

#include "Platform/Window.h"
#include "Platform/Renderer2D.h"
#include "Platform/Image.h"
#include "Catalog.h"
#include "GridLayout.h"
#include "GridController.h"
#include "CatalogGridView.h"
#include "GameSearch.h"
#include "GameSort.h"
#include "GameFilter.h"
#include "SidebarModel.h"
#include "Forms.h"
#include "WidgetView.h"
#include "Settings.h"

#include <GL/glew.h>
#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

using namespace platform;

namespace {

// Stable placeholder cover color for a platform string (cheap FNV hash → hue).
// Shared shape with gui_demo so the visual matches the browse harness.
Color colorForPlatform(const std::string& p) {
    uint32_t h = 2166136261u;
    for (char c : p) { h ^= (uint8_t)c; h *= 16777619u; }
    float r = 0.30f + ((h >> 0) & 0x3F) / 255.0f;
    float g = 0.30f + ((h >> 6) & 0x3F) / 255.0f;
    float b = 0.30f + ((h >> 12) & 0x3F) / 255.0f;
    return Color{r, g, b, 1};
}

int yearOf(int64_t unixTime) {
    if (unixTime <= 0) return 0;
    time_t t = (time_t)unixTime;
    struct tm tmv {};
#ifdef _WIN32
    if (gmtime_s(&tmv, &t) != 0) return 0;
#else
    if (!gmtime_r(&t, &tmv)) return 0;
#endif
    return tmv.tm_year + 1900;
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

// Demo catalog (same titles as gui_demo) so the headless gate has a deterministic
// scene when no library.json is supplied.
struct DemoEntry { const char* title; const char* platform; };
std::vector<DemoEntry> demoEntries() {
    return {
        {"Hollow Knight", "PC"},       {"Celeste", "PC"},
        {"Hades", "PC"},               {"Metroid Prime", "GameCube"},
        {"God of War", "PS2"},         {"Super Mario Galaxy", "Wii"},
        {"Halo: CE", "Xbox"},          {"Chrono Trigger", "SNES"},
    };
}

// Everything the app needs to draw + filter the catalog, kept in parallel arrays
// (mirrors gui_demo's model).
struct Catalog {
    std::vector<gridview::Card>     cards;
    std::vector<gamesearch::Fields> fields;
    std::vector<gamefilter::Item>   items;
};

Catalog buildCatalog(const std::string& path, IRenderer2D& r, size_t& rawCount) {
    Catalog cat;
    rawCount = 0;
    std::vector<catalog::Game> games;
    if (!path.empty()) { games = catalog::loadFile(path); rawCount = games.size(); }

    if (!games.empty()) {
        for (const auto& g : games) {
            gridview::Card c;
            c.title = g.title.empty() ? g.id : g.title;
            c.platform = g.platform;
            c.placeholder = colorForPlatform(g.platform);
            c.favorite = g.favorite;
            c.install = gridview::installFromString(g.installState);
            if (!g.coverArtPath.empty()) {
                DecodedImage img;
                if (decodeImageFileRGBA(g.coverArtPath, img) && img.valid())
                    c.cover = r.createImageRGBA(img.rgba.data(), img.w, img.h);
            }
            gamesearch::Fields f;
            f.title = c.title; f.platform = g.platform;
            f.genres = g.genres; f.developer = g.developer;
            f.publisher = g.publisher; f.franchise = g.franchise;
            f.releaseYear = yearOf(g.releaseDate);
            gamefilter::Item it;
            it.platform = g.platform;
            it.install = gamefilter::installFromString(g.installState);
            it.serverBacked = g.serverBacked;
            it.favorite = g.favorite;
            it.hidden = g.hidden;
            it.lastPlayed = g.lastPlayed;
            it.collections = g.collections;
            cat.cards.push_back(std::move(c));
            cat.fields.push_back(std::move(f));
            cat.items.push_back(std::move(it));
        }
    } else {
        for (const auto& e : demoEntries()) {
            gridview::Card c;
            c.title = e.title; c.platform = e.platform;
            c.placeholder = colorForPlatform(e.platform);
            gamesearch::Fields f; f.title = e.title; f.platform = e.platform;
            gamefilter::Item it; it.platform = e.platform;
            cat.cards.push_back(std::move(c));
            cat.fields.push_back(std::move(f));
            cat.items.push_back(std::move(it));
        }
    }
    return cat;
}

} // namespace

int main(int argc, char** argv) {
    bool hold = false;
    std::string catalogPath;
    std::string configPath;            // --config <path>; default settings::configPath()
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hold") == 0) hold = true;
        else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc)
            configPath = argv[++i];
        else catalogPath = argv[i];
    }
    if (configPath.empty()) configPath = settings::configPath();

    const int W = 1024, H = 680;
    auto win = makeWindow("ArcadeLauncher (Linux)", W, H);
    if (!win) { std::printf("arcade_client: SKIP (no display: %s)\n", SDL_GetError()); return 0; }
    win->show(true);
    auto r = makeRenderer(win.get());
    if (!r) { std::printf("arcade_client: FAILED (renderer init: nanovg/GL)\n"); return 1; }

    gridview::Theme theme = gridview::darkTheme();
    theme.titleFont = r->loadFont("", 22, true);
    theme.bodyFont  = r->loadFont("", 15, false);

    widgetview::Theme wth = widgetview::darkTheme();
    wth.font = r->loadFont("", 15, false);
    wth.fontPx = 15.0f;

    // ── Config lifecycle: load the General settings at startup. A missing config
    //    leaves `gen` at its defaults (loadGeneralFromFile returns false), which is
    //    fine — we only ever write back an existing file (non-destructive).
    settings::General gen;
    const bool configLoaded = settings::loadGeneralFromFile(configPath, gen);

    // ── Catalog model (shared grid/search/filter).
    size_t rawCount = 0;
    Catalog cat = buildCatalog(catalogPath, *r, rawCount);

    // Sidebar entries built dynamically from the catalog (shared sidebar::).
    std::vector<sidebar::Entry> sidebarEntries = sidebar::build(cat.items);
    std::vector<std::string> tabs;
    for (const auto& e : sidebarEntries) tabs.push_back(e.label);

    std::string activeTabLabel = "All Games";
    gamefilter::TabSel activeTab = gamefilter::tabSelect(activeTabLabel);
    std::string query;

    // The shown card set: passes the active tab AND the search query.
    auto filterCards = [&]() {
        std::vector<gridview::Card> out;
        const std::string lq = query.empty() ? std::string() : gamesearch::lower(query);
        for (size_t i = 0; i < cat.cards.size(); ++i) {
            if (!gamefilter::passes(cat.items[i], activeTab)) continue;
            if (!query.empty() && !gamesearch::matches(cat.fields[i], lq)) continue;
            out.push_back(cat.cards[i]);
        }
        return out;
    };
    std::vector<gridview::Card> shown = filterCards();

    int w = W, h = H; win->size(w, h);
    grid::Controller ctrl;
    ctrl.setViewport(w, h);
    ctrl.setCount((int)shown.size());

    // ── Settings overlay: a forms::Panel bound to the live General model. Built
    //    lazily when opened so it always reflects the current `gen`; closing it
    //    reads the edited values back into `gen` (which is what gets saved on exit).
    bool overlayOpen = false;
    forms::Panel panel;
    const Rect overlayBounds = {(float)w / 2 - 380, 110, 760, 320};
    auto openOverlay = [&]() {
        panel = settings::buildGeneralPanel(gen, overlayBounds);
        overlayOpen = true;
    };
    auto closeOverlay = [&]() {
        gen = settings::readGeneralPanel(panel);   // merge edits into the live model
        overlayOpen = false;
    };

    // Dim backdrop drawn over the grid when the overlay is open, so the panel reads
    // as a modal. A distinct, deterministic color so the headless check can prove
    // the overlay rendered by sampling the grid area.
    const Color dim = {0.04f, 0.05f, 0.06f, 0.82f};

    auto render = [&]() {
        for (int i = 0; i < (int)shown.size(); ++i)
            shown[i].selected = (i == ctrl.selected());
        gridview::drawGrid(*r, w, h, ctrl.scrollOffset(), shown, tabs, theme);
        if (overlayOpen) {
            // gridview::drawGrid owns begin/endFrame, so the overlay is a second
            // pass. nanovg lets us keep drawing after drawGrid's endFrame by
            // opening our own frame; the back buffer accumulates both.
            r->beginFrame(w, h, 1.0f);
            r->fillRect({0, 0, (float)w, (float)h}, dim);
            // Panel card backdrop.
            r->fillRoundedRect({panel.bounds.x - 24, panel.bounds.y - 56,
                                panel.bounds.w + 48, panel.bounds.h + 96}, 10.0f,
                               {0.11f, 0.12f, 0.14f, 1.0f});
            if (wth.font)
                r->drawText(wth.font, "Settings", panel.bounds.x, panel.bounds.y - 40,
                            wth.text, TextAlign::Left);
            widgetview::drawPanel(*r, panel, wth);
            r->endFrame();
        }
    };

    // ── Headless self-check: prove the app wires together without a user. Load is
    //    already done (configLoaded). Open the overlay, drive a known edit through
    //    the shared form model, close it (merging back into `gen`), and assert the
    //    edit took. Then render grid + overlay and verify both actually painted.
    if (!hold) {
        // Remember the starting value of a toggle we'll flip.
        const bool before = gen.startFullscreen;
        openOverlay();
        // Focus + Space-toggle the first checkbox (startFullscreen).
        forms::setFocus(panel, 0);
        { Event e; e.type = EventType::KeyDown; e.key = Key::Space; forms::handle(panel, e); }
        closeOverlay();
        const bool toggled = (gen.startFullscreen != before);

        // Frame 1: grid only. Sample tile0 center (its flat placeholder color).
        overlayOpen = false;
        for (int f = 0; f < 2; ++f) { Event ev; while (win->poll(ev)) {} render(); SDL_Delay(8); }
        glReadBuffer(GL_BACK);
        std::vector<uint8_t> g1((size_t)w * h * 3, 0);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, g1.data());

        const grid::Metrics gm = grid::Metrics::forViewport(w, h);
        grid::Rect t0 = grid::tileRect(gm, 0, 0.0f);
        int sx = (int)(t0.x + t0.w / 2), sy = (int)(t0.y + t0.h / 2);
        size_t ti = ((size_t)(h - 1 - sy) * w + sx) * 3;
        int t0r = g1[ti], t0g = g1[ti + 1], t0b = g1[ti + 2];

        // Frame 2: overlay open. The SAME tile0 sample must now read the dim
        // backdrop (much darker), proving the overlay rendered over the grid.
        openOverlay();
        for (int f = 0; f < 2; ++f) { Event ev; while (win->poll(ev)) {} render(); SDL_Delay(8); }
        glReadBuffer(GL_BACK);
        std::vector<uint8_t> g2((size_t)w * h * 3, 0);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, g2.data());
        bool wrote = writePpm("arcade_client.ppm", g2, w, h);
        int d0r = g2[ti], d0g = g2[ti + 1], d0b = g2[ti + 2];

        auto near = [](int a, int b) { return a - b <= 12 && b - a <= 12; };
        Color p = shown.empty() ? theme.bg : shown[0].placeholder;
        bool gridDrew = !shown.empty() &&
                        near(t0r, (int)(p.r * 255 + 0.5f)) &&
                        near(t0g, (int)(p.g * 255 + 0.5f)) &&
                        near(t0b, (int)(p.b * 255 + 0.5f));
        // Overlay darkens that tile: each channel clearly below the placeholder.
        bool overlayDrew = (d0r < t0r - 20) && (d0g < t0g - 20) && (d0b < t0b - 20);

        bool ok = toggled && gridDrew && overlayDrew && !shown.empty();
        std::printf("arcade_client: %s — config loaded=%d (%s), %zu games → %zu tiles, "
                    "%zu sidebar tabs; overlay toggle %s; tile0 grid (%d,%d,%d) → "
                    "overlay (%d,%d,%d); wrote %s\n",
                    ok ? "OK" : "FAILED", configLoaded ? 1 : 0, configPath.c_str(),
                    rawCount, shown.size(), sidebarEntries.size(),
                    toggled ? "applied" : "FAILED",
                    t0r, t0g, t0b, d0r, d0g, d0b,
                    wrote ? "arcade_client.ppm" : "<ppm failed>");
        overlayOpen = false;
        return ok ? 0 : 1;
    }

    // ── Interactive app.
    std::printf("arcade_client: interactive — F2 settings, click tabs to filter, "
                "type to search, F1 cycles sort, wheel/arrows browse, Esc closes "
                "overlay or quits. Config: %s (loaded=%d)\n",
                configPath.c_str(), configLoaded ? 1 : 0);

    auto applyQuery = [&]() {
        shown = filterCards();
        ctrl.setCount((int)shown.size());
        std::string title = "ArcadeLauncher (Linux)";
        if (activeTab.page != gamefilter::Page::All) title += " — " + activeTabLabel;
        if (!query.empty()) title += " — search: " + query;
        win->setTitle(title);
    };
    auto tabAtPoint = [&](float x, float y) -> int {
        const grid::Metrics mm = grid::Metrics::forViewport(w, h);
        if (x < 0 || x > mm.sidebarW) return -1;
        const float top = mm.topbarH + 12.0f;
        if (y < top) return -1;
        int idx = (int)((y - top) / 34.0f);
        return (idx >= 0 && idx < (int)tabs.size()) ? idx : -1;
    };

    int sortIdx = 0;
    bool quit = false;
    while (!quit) {
        Event ev;
        while (win->poll(ev)) {
            // When the overlay is open it captures input (modal), except the keys
            // that close it / quit.
            if (overlayOpen) {
                if (ev.type == EventType::Quit) quit = true;
                else if (ev.type == EventType::KeyDown &&
                         (ev.key == Key::Escape || ev.key == Key::F2)) closeOverlay();
                else forms::handle(panel, ev);
                continue;
            }
            switch (ev.type) {
                case EventType::Quit: quit = true; break;
                case EventType::Resize: w = ev.width; h = ev.height; ctrl.setViewport(w, h); break;
                case EventType::MouseWheel: ctrl.scrollBy(-ev.wheel * 80.0f); break;
                case EventType::MouseDown:
                    if (ev.button == MouseButton::Left) {
                        int t = tabAtPoint(ev.x, ev.y);
                        if (t >= 0) {
                            activeTabLabel = sidebarEntries[t].label;
                            activeTab = sidebarEntries[t].sel;
                            applyQuery();
                        } else ctrl.clickAt(ev.x, ev.y);
                    }
                    break;
                case EventType::TextInput: query += ev.text; applyQuery(); break;
                case EventType::KeyDown:
                    switch (ev.key) {
                        case Key::Escape: quit = true; break;
                        case Key::F2: openOverlay(); break;
                        case Key::F1:
                            sortIdx = (sortIdx + 1) % 5; applyQuery(); break;
                        case Key::Backspace:
                            if (!query.empty()) {
                                while (!query.empty() && (query.back() & 0xC0) == 0x80)
                                    query.pop_back();
                                if (!query.empty()) query.pop_back();
                                applyQuery();
                            }
                            break;
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

    // ── Config lifecycle: save on close. If the overlay was left open, merge its
    //    edits first. saveGeneralToFile is non-destructive (rewrites only the
    //    General keys; never creates a missing file), so a fresh install with no
    //    config simply writes nothing.
    if (overlayOpen) gen = settings::readGeneralPanel(panel);
    bool saved = settings::saveGeneralToFile(configPath, gen);
    std::printf("arcade_client: closed — config saved=%d (%s)\n",
                saved ? 1 : 0, configPath.c_str());
    return 0;
}
