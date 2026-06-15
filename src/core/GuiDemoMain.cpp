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
//   ./gui_demo --sidebar             # gate the dynamic sidebar:: entry model
//   ./gui_demo --tab <label>         # gate one sidebar tab's filter
//   ./gui_demo --widgets             # gate the retained widget set (L4 button)
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
#include "GameSearch.h"
#include "GameSort.h"
#include "GameFilter.h"
#include "SidebarModel.h"
#include "Widgets.h"
#include "WidgetView.h"

#include <GL/glew.h>
#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <numeric>
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
    std::string searchQuery;   // --search <q>: filter the grid (shared gamesearch::)
    bool doSort = false;       // --sort <mode>: order the grid (shared gamesort::)
    gamesort::Mode sortMode = gamesort::Mode::Title;
    bool doTab = false;        // --tab <label>: sidebar filter (shared gamefilter::)
    std::string tabLabel;
    bool doSidebar = false;    // --sidebar: gate the dynamic sidebar entry model
    bool doWidgets = false;    // --widgets: gate the retained widget set (L4)
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hold") == 0) hold = true;
        else if (std::strcmp(argv[i], "--widgets") == 0) doWidgets = true;
        else if (std::strcmp(argv[i], "--sidebar") == 0) doSidebar = true;
        else if (std::strcmp(argv[i], "--search") == 0 && i + 1 < argc)
            searchQuery = argv[++i];
        else if (std::strcmp(argv[i], "--tab") == 0 && i + 1 < argc) {
            doTab = true; tabLabel = argv[++i];
        }
        else if (std::strcmp(argv[i], "--sort") == 0 && i + 1 < argc) {
            doSort = true;
            std::string m = argv[++i];
            if (m == "platform") sortMode = gamesort::Mode::Platform;
            else if (m == "rating") sortMode = gamesort::Mode::Rating;
            else if (m == "playtime") sortMode = gamesort::Mode::Playtime;
            else if (m == "recent") sortMode = gamesort::Mode::Recent;
            else sortMode = gamesort::Mode::Title;
        }
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

    // L4-a: isolated widget-set scene. Drives a push button through the SHARED
    // widgets:: state machine and draws it via widgetview::, separate from the
    // grid gates. Verifies the pure click contract (enabled OK clicks, disabled
    // Cancel does not) AND that the button fill actually rendered.
    if (doWidgets) {
        widgetview::Theme wth = widgetview::darkTheme();
        wth.font = r->loadFont("", 15, false);
        wth.fontPx = 15.0f;

        widgets::Button ok;
        ok.bounds = {80, 90, 160, 44};
        ok.label = "OK";
        widgets::Button cancel;
        cancel.bounds = {260, 90, 160, 44};
        cancel.label = "Cancel";
        cancel.enabled = false;

        widgets::Checkbox chk;
        chk.bounds = {80, 160, 240, 28};
        chk.label = "Enable fancy mode";

        widgets::TextEdit field;
        field.bounds = {80, 210, 340, 34};
        field.focused = true;  // focused so the gate can type into it

        widgets::Combo combo;
        combo.bounds = {80, 260, 200, 30};
        combo.options = {"Low", "Medium", "High"};

        // Drive copies through a synthetic click (no display needed for the
        // logic): press+release inside each. OK should click; disabled Cancel
        // must not. The drawn `ok`/`cancel` stay in their resting state so the
        // pixel check sees a predictable fill.
        auto synthClick = [](widgets::Button b) {
            const float cx = b.bounds.x + b.bounds.w * 0.5f;
            const float cy = b.bounds.y + b.bounds.h * 0.5f;
            Event d; d.type = EventType::MouseDown; d.x = cx; d.y = cy;
            Event u; u.type = EventType::MouseUp;   u.x = cx; u.y = cy;
            widgets::handle(b, d);
            return widgets::handle(b, u).clicked;
        };
        const bool okClicked = synthClick(ok);
        const bool cancelClicked = synthClick(cancel);

        // Drive a copy of the checkbox through one click; it must end checked.
        bool chkToggledOn = false;
        {
            widgets::Checkbox cc = chk;
            const float cx = cc.bounds.x + 10, cy = cc.bounds.y + cc.bounds.h * 0.5f;
            Event d; d.type = EventType::MouseDown; d.x = cx; d.y = cy;
            Event u; u.type = EventType::MouseUp;   u.x = cx; u.y = cy;
            widgets::handle(cc, d);
            widgets::handle(cc, u);
            chkToggledOn = cc.checked;
        }

        // Type into a copy of the field through the shared editing model.
        bool fieldTyped = false;
        {
            widgets::TextEdit f = field;
            for (const char* s : {"H", "e", "l", "l", "o"}) {
                Event e; e.type = EventType::TextInput; e.text = s;
                widgets::handle(f, e);
            }
            fieldTyped = (f.text == "Hello" && f.caret == 5);
            field.text = f.text;  // show the typed text in the drawn field
            field.caret = f.caret;
            field.anchor = f.caret;
        }

        // Drive a copy of the combo: open, then commit row 2 ("High").
        bool comboPicked = false;
        {
            widgets::Combo cc = combo;
            const float hx = cc.bounds.x + 10, hy = cc.bounds.y + cc.bounds.h * 0.5f;
            Event openEv; openEv.type = EventType::MouseDown; openEv.x = hx; openEv.y = hy;
            widgets::handle(cc, openEv);
            const platform::Rect row2 = widgets::comboItemRect(cc, 2);
            Event pick; pick.type = EventType::MouseDown;
            pick.x = row2.x + 10; pick.y = row2.y + row2.h * 0.5f;
            widgets::handle(cc, pick);
            comboPicked = (cc.selected == 2 && !cc.open);
            combo.selected = cc.selected;  // show the choice in the drawn combo
        }

        int w = W, h = H; win->size(w, h);
        const Color bg = {0.07f, 0.08f, 0.09f, 1.0f};
        auto drawScene = [&]() {
            r->beginFrame(w, h, 1.0f);
            r->fillRect({0, 0, (float)w, (float)h}, bg);
            widgetview::drawButton(*r, ok, wth);
            widgetview::drawButton(*r, cancel, wth);
            widgetview::drawCheckbox(*r, chk, wth);
            widgetview::drawTextEdit(*r, field, wth);
            widgetview::drawCombo(*r, combo, wth);  // last: open list overlays
            r->endFrame();
        };
        bool wquit = false;
        for (int f = 0; f < 3 && !wquit; ++f) {
            Event ev; while (win->poll(ev)) if (ev.type == EventType::Quit) wquit = true;
            drawScene();
            SDL_Delay(16);
        }

        glReadBuffer(GL_BACK);
        std::vector<uint8_t> buf((size_t)w * h * 3, 0);
        glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, buf.data());
        bool wrote = writePpm("gui_demo.ppm", buf, w, h);

        // Sample inside the OK button but left of its centered label (no glyph).
        int sx = (int)(ok.bounds.x + 10), sy = (int)(ok.bounds.y + ok.bounds.h / 2);
        size_t idx = ((size_t)(h - 1 - sy) * w + sx) * 3;
        int gr = buf[idx], gg = buf[idx + 1], gb = buf[idx + 2];
        auto near = [](int a, int b) { return a - b <= 10 && b - a <= 10; };
        int er = (int)(wth.normal.r * 255 + 0.5f), eg = (int)(wth.normal.g * 255 + 0.5f),
            eb = (int)(wth.normal.b * 255 + 0.5f);
        bool rendered = near(gr, er) && near(gg, eg) && near(gb, eb);
        bool wok = okClicked && !cancelClicked && chkToggledOn && fieldTyped &&
                   comboPicked && rendered;
        std::printf("gui demo: %s — widgets: OK clicked=%d, Cancel(disabled) clicked=%d, "
                    "checkbox toggled=%d, field typed=%d (\"%s\"), combo picked=%d (\"%s\"); "
                    "OK fill (%d,%d,%d) expected ~(%d,%d,%d); wrote %s\n",
                    wok ? "OK" : "FAILED", okClicked ? 1 : 0, cancelClicked ? 1 : 0,
                    chkToggledOn ? 1 : 0, fieldTyped ? 1 : 0, field.text.c_str(),
                    comboPicked ? 1 : 0, widgets::selectedText(combo).c_str(),
                    gr, gg, gb, er, eg, eb, wrote ? "gui_demo.ppm" : "<ppm failed>");

        if (hold) {
            std::printf("gui demo: interactive widgets — click OK (Cancel is "
                        "disabled), Esc/close to exit.\n");
            win->setTitle("ArcadeLauncher (Linux) — widgets demo");
            while (!wquit) {
                Event ev;
                while (win->poll(ev)) {
                    if (ev.type == EventType::Quit) wquit = true;
                    else if (ev.type == EventType::KeyDown && ev.key == Key::Escape)
                        wquit = true;
                    else if (ev.type == EventType::Resize) { w = ev.width; h = ev.height; }
                    else {
                        // Buttons/checkbox track hover + clicks.
                        if (widgets::handle(ok, ev).clicked) std::printf("OK clicked\n");
                        widgets::handle(cancel, ev);
                        if (widgets::handle(chk, ev).clicked)
                            std::printf("checkbox -> %s\n", chk.checked ? "on" : "off");
                        // A left click focuses the field (placing the caret via
                        // glyph metrics) or blurs it.
                        if (ev.type == EventType::MouseDown &&
                            ev.button == MouseButton::Left) {
                            if (widgets::contains(field.bounds, ev.x, ev.y)) {
                                field.focused = true;
                                size_t ci = widgetview::caretIndexFromX(*r, field, wth, ev.x);
                                widgets::setCaret(field, ci, ev.shift);
                            } else {
                                field.focused = false;
                            }
                        }
                        // Combo gets mouse + keyboard (open list, pick, arrows).
                        if (widgets::handle(combo, ev).clicked)
                            std::printf("combo -> %s\n", widgets::selectedText(combo).c_str());
                        // Keyboard/text routing: the focused field consumes it
                        // (handle ignores mouse events).
                        widgets::handle(field, ev);
                    }
                }
                drawScene();
                SDL_Delay(16);
            }
        }
        return wok ? 0 : 1;
    }

    // L3d: render the REAL catalog when a library.json is supplied; otherwise the
    // built-in demo scene (which has the deterministic pixel anchor). We keep the
    // full set (allCards) plus a parallel searchable Fields list (allFields) so
    // --search can filter through the SHARED gamesearch:: predicate.
    std::vector<gridview::Card>       allCards;
    std::vector<gamesearch::Fields>   allFields;
    std::vector<gamesort::Key>        allKeys;
    std::vector<gamefilter::Item>     allItems;   // parallel: sidebar tab filtering
    bool fromCatalog = false;

    // gridview::Install (card overlay) → gamefilter::Install (tab predicate).
    auto toFilterInstall = [](gridview::Install gi) {
        switch (gi) {
            case gridview::Install::Installed:       return gamefilter::Install::Installed;
            case gridview::Install::UpdateAvailable: return gamefilter::Install::UpdateAvailable;
            case gridview::Install::NotInstalled:    return gamefilter::Install::Missing;
            default:                                 return gamefilter::Install::Unknown;
        }
    };
    size_t catalogCount = 0;

    // releaseDate (unix) -> Gregorian year for the search predicate.
    auto yearOf = [](int64_t unixTime) -> int {
        if (unixTime <= 0) return 0;
        time_t t = (time_t)unixTime;
        struct tm tmv {};
#ifdef _WIN32
        if (gmtime_s(&tmv, &t) != 0) return 0;
#else
        if (!gmtime_r(&t, &tmv)) return 0;
#endif
        return tmv.tm_year + 1900;
    };

    if (!catalogPath.empty()) {
        auto games = catalog::loadFile(catalogPath);
        catalogCount = games.size();
        if (!games.empty()) {
            fromCatalog = true;
            // Keep hidden games in the model; the shared tab predicate hides them
            // off every page except "Hidden" (mirrors App::ApplyFilter), so the
            // Hidden tab actually works instead of them being dropped here.
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
                        c.cover = r->createImageRGBA(img.rgba.data(), img.w, img.h);
                }
                gamesearch::Fields f;
                f.title = c.title;  f.platform = g.platform;
                f.genres = g.genres; f.developer = g.developer;
                f.publisher = g.publisher; f.franchise = g.franchise;
                f.releaseYear = yearOf(g.releaseDate);
                gamesort::Key k;
                k.title = c.title; k.id = g.id; k.platform = g.platform;
                k.playtimeSeconds = g.playtimeSeconds;  // rating absent
                k.lastPlayed = g.lastPlayed;
                gamefilter::Item it;
                it.platform = g.platform;
                it.install = gamefilter::installFromString(g.installState);
                it.serverBacked = g.serverBacked;
                it.favorite = g.favorite;
                it.hidden = g.hidden;
                it.lastPlayed = g.lastPlayed;
                it.collections = g.collections;
                allCards.push_back(std::move(c));
                allFields.push_back(std::move(f));
                allKeys.push_back(std::move(k));
                allItems.push_back(std::move(it));
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
            // Exercise the corner overlays on a few tiles (not tile 0, which
            // stays the plain center-pixel anchor). Corners don't touch center.
            if (i > 0) {
                c.favorite = (i % 3 == 0);
                gridview::Install st[] = {gridview::Install::Installed,
                                          gridview::Install::NotInstalled,
                                          gridview::Install::UpdateAvailable};
                c.install = st[i % 3];
                if (i % 4 == 0) c.variantCount = i + 1;   // count-badge overlay
                if (i % 2 == 0) {                          // selection-mode checkbox
                    c.selectionMode = true;
                    c.multiSelected = (i % 4 == 0);
                }
            }
            // Tile 0 keeps a flat placeholder as the deterministic verification
            // anchor; the rest get real decoded cover art (PNG round-tripped).
            if (i > 0) {
                DecodedImage img = makeCoverImage(entries[i].cover);
                if (img.valid())
                    c.cover = r->createImageRGBA(img.rgba.data(), img.w, img.h);
            }
            gamesearch::Fields f;
            f.title = c.title; f.platform = c.platform;
            gamesort::Key k;
            k.title = entries[i].title;
            k.id = std::to_string(i);
            k.platform = entries[i].platform;
            // Synthetic, distinct metrics so each sort mode has a clear order.
            k.rating = (float)(60 + (i * 7) % 40);
            k.playtimeSeconds = (uint64_t)((i * 37) % 100) * 60;
            k.lastPlayed = 1000 + (int64_t)((i * 53) % 100);
            gamefilter::Item it;
            it.platform = entries[i].platform;
            it.install = toFilterInstall(c.install);
            it.serverBacked = false;       // demo entries are all "local"
            it.favorite = c.favorite;
            it.hidden = false;
            it.lastPlayed = k.lastPlayed;
            allCards.push_back(std::move(c));
            allFields.push_back(std::move(f));
            allKeys.push_back(std::move(k));
            allItems.push_back(std::move(it));
        }
    }

    // Apply the sort (shared gamesort::) by permuting the parallel arrays.
    auto sortAll = [&](gamesort::Mode m) {
        if (allCards.empty()) return;
        std::vector<size_t> order(allCards.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](size_t i, size_t j) {
            return gamesort::less(allKeys[i], allKeys[j], m);
        });
        std::vector<gridview::Card>     sc; sc.reserve(order.size());
        std::vector<gamesearch::Fields> sf; sf.reserve(order.size());
        std::vector<gamesort::Key>      sk; sk.reserve(order.size());
        std::vector<gamefilter::Item>   si; si.reserve(order.size());
        for (size_t idx : order) {
            sc.push_back(std::move(allCards[idx]));
            sf.push_back(std::move(allFields[idx]));
            sk.push_back(std::move(allKeys[idx]));
            si.push_back(std::move(allItems[idx]));
        }
        allCards.swap(sc); allFields.swap(sf); allKeys.swap(sk); allItems.swap(si);
    };
    if (doSort) sortAll(sortMode);

    // Build the sidebar entry list dynamically from the catalog via the SHARED
    // sidebar:: model (mirrors Renderer::BuildSidebarEntries): fixed tabs, then
    // the platforms/collections actually present, then Hidden if anything is
    // hidden. Each entry carries its own gamefilter::TabSel, so a live click can
    // filter platform AND collection tabs without re-guessing from the label.
    // Sorting permutes the items but not the platform/collection set, so the
    // entry list is the same either way. `tabs` is the label list drawGrid draws.
    std::vector<sidebar::Entry> sidebarEntries = sidebar::build(allItems);
    std::vector<std::string> tabs;
    tabs.reserve(sidebarEntries.size());
    for (const auto& e : sidebarEntries) tabs.push_back(e.label);

    // Active sidebar tab (shared gamefilter::). Default is "All Games"; --tab sets
    // it for the deterministic gate, and a click sets it live in --hold.
    std::string activeTabLabel = doTab ? tabLabel : "All Games";
    gamefilter::TabSel activeTab = gamefilter::tabSelect(activeTabLabel);

    // Produce the shown set: keep cards that pass the active sidebar tab (shared
    // gamefilter::) AND the search query (shared gamesearch::). With the default
    // "All Games" tab and an empty query this is every non-hidden card, preserving
    // the deterministic tile-0 anchor.
    auto filterCards = [&](const std::string& query) {
        std::vector<gridview::Card> out;
        const std::string lq = query.empty() ? std::string() : gamesearch::lower(query);
        for (size_t i = 0; i < allCards.size(); ++i) {
            if (!gamefilter::passes(allItems[i], activeTab)) continue;
            if (!query.empty() && !gamesearch::matches(allFields[i], lq)) continue;
            out.push_back(allCards[i]);
        }
        return out;
    };
    std::vector<gridview::Card> cards = filterCards(searchQuery);

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
    if (doSidebar) {
        // Dynamic sidebar gate: assert the SHARED sidebar:: model produced the
        // expected structure for this catalog — the six fixed tabs first (none of
        // them platform/collection rows), then platform/collection rows whose
        // filter arg equals their label — and that the grid rendered.
        static const std::vector<std::string> kFixed = {
            "All Games", "Favorites", "Recently Played", "Installed",
            "Ready to Download", "Updates"};
        bool fixedOk = sidebarEntries.size() >= kFixed.size();
        for (size_t i = 0; fixedOk && i < kFixed.size(); ++i)
            if (sidebarEntries[i].label != kFixed[i] ||
                sidebarEntries[i].sel.page == gamefilter::Page::Platform ||
                sidebarEntries[i].sel.page == gamefilter::Page::Collection)
                fixedOk = false;
        bool selOk = true;
        for (const auto& e : sidebarEntries)
            if ((e.sel.page == gamefilter::Page::Platform ||
                 e.sel.page == gamefilter::Page::Collection) &&
                e.sel.arg != e.label)
                selOk = false;
        const Color bg = theme.bg;
        bool drew = !(near(gr, (int)(bg.r * 255)) && near(gg, (int)(bg.g * 255)) &&
                      near(gb, (int)(bg.b * 255)));
        ok = fixedOk && selOk && !cards.empty() && drew;
        std::string joined;
        for (const auto& e : sidebarEntries) {
            if (!joined.empty()) joined += '|';
            joined += e.label;
        }
        std::printf("gui demo: %s — sidebar %zu entries [%s]; %zu tiles; "
                    "tile0 (%d,%d,%d); wrote %s\n",
                    ok ? "OK" : "FAILED", sidebarEntries.size(), joined.c_str(),
                    cards.size(), gr, gg, gb,
                    wrote ? "gui_demo.ppm" : "<ppm failed>");
    } else if (!searchQuery.empty()) {
        // Search path: assert the shared predicate produced a correct, non-empty,
        // strict subset, and that its first tile actually rendered.
        const std::string lq = gamesearch::lower(searchQuery);
        size_t matchCount = 0;
        for (const auto& fl : allFields)
            if (gamesearch::matches(fl, lq)) ++matchCount;
        bool rendered;
        if (!cards.empty() && cards[0].cover == 0) {
            Color p = cards[0].placeholder;
            int er = (int)(p.r * 255 + 0.5f), eg = (int)(p.g * 255 + 0.5f),
                eb = (int)(p.b * 255 + 0.5f);
            rendered = near(gr, er) && near(gg, eg) && near(gb, eb);
        } else {
            const Color bg = theme.bg;
            rendered = !cards.empty() &&
                       !(near(gr, (int)(bg.r * 255)) && near(gg, (int)(bg.g * 255)) &&
                         near(gb, (int)(bg.b * 255)));
        }
        ok = !cards.empty() && cards.size() == matchCount &&
             cards.size() < allCards.size() && rendered;
        std::printf("gui demo: %s — search \"%s\" → %zu/%zu tiles; "
                    "tile0 (%d,%d,%d); wrote %s\n",
                    ok ? "OK" : "FAILED", searchQuery.c_str(), cards.size(),
                    allCards.size(), gr, gg, gb,
                    wrote ? "gui_demo.ppm" : "<ppm failed>");
    } else if (doTab) {
        // Tab path: assert the shared sidebar predicate produced exactly the games
        // it should for this tab, that the set is non-empty, and the first tile
        // actually rendered.
        size_t matchCount = 0;
        for (const auto& it : allItems)
            if (gamefilter::passes(it, activeTab)) ++matchCount;
        bool rendered;
        if (!cards.empty() && cards[0].cover == 0) {
            Color p = cards[0].placeholder;
            int er = (int)(p.r * 255 + 0.5f), eg = (int)(p.g * 255 + 0.5f),
                eb = (int)(p.b * 255 + 0.5f);
            rendered = near(gr, er) && near(gg, eg) && near(gb, eb);
        } else {
            const Color bg = theme.bg;
            rendered = !cards.empty() &&
                       !(near(gr, (int)(bg.r * 255)) && near(gg, (int)(bg.g * 255)) &&
                         near(gb, (int)(bg.b * 255)));
        }
        ok = !cards.empty() && cards.size() == matchCount && rendered;
        std::printf("gui demo: %s — tab \"%s\" → %zu/%zu tiles; "
                    "tile0 (%d,%d,%d); wrote %s\n",
                    ok ? "OK" : "FAILED", activeTabLabel.c_str(), cards.size(),
                    allCards.size(), gr, gg, gb,
                    wrote ? "gui_demo.ppm" : "<ppm failed>");
    } else if (doSort) {
        // Sort path: assert the shown order is non-decreasing under the shared
        // comparator, and the first tile rendered (non-bg).
        bool sorted = std::is_sorted(allKeys.begin(), allKeys.end(),
            [&](const gamesort::Key& x, const gamesort::Key& y) {
                return gamesort::less(x, y, sortMode);
            });
        const Color bg = theme.bg;
        bool rendered = !cards.empty() &&
                        !(near(gr, (int)(bg.r * 255)) && near(gg, (int)(bg.g * 255)) &&
                          near(gb, (int)(bg.b * 255)));
        ok = sorted && rendered;
        std::printf("gui demo: %s — sort: %zu tiles ordered; first \"%s\"; "
                    "tile0 (%d,%d,%d); wrote %s\n",
                    ok ? "OK" : "FAILED", cards.size(),
                    cards.empty() ? "" : cards[0].title.c_str(), gr, gg, gb,
                    wrote ? "gui_demo.ppm" : "<ppm failed>");
    } else if (fromCatalog) {
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
        std::printf("gui demo: interactive — click a sidebar tab to filter, type "
                    "to search, F1 cycles sort, wheel scrolls, arrows select, "
                    "Esc/close to exit.\n");
        std::string liveQuery = searchQuery;
        int sortIdx = 0;
        // Re-run the shared filter and re-sync the controller to the new count.
        auto applyQuery = [&]() {
            cards = filterCards(liveQuery);
            ctrl.setCount((int)cards.size());
            std::string title = "ArcadeLauncher (Linux)";
            if (activeTab.page != gamefilter::Page::All)
                title += " — " + activeTabLabel;
            if (!liveQuery.empty()) title += " — search: " + liveQuery;
            win->setTitle(title);
        };

        // Hit-test the sidebar tab list drawn by gridview::drawGrid (rows start at
        // topbarH+12, 34px pitch, within the sidebar width). Returns tab index or -1.
        auto tabAtPoint = [&](float x, float y) -> int {
            const grid::Metrics mm = grid::Metrics::forViewport(w, h);
            if (x < 0 || x > mm.sidebarW) return -1;
            const float top = mm.topbarH + 12.0f;
            if (y < top) return -1;
            int idx = (int)((y - top) / 34.0f);
            return (idx >= 0 && idx < (int)tabs.size()) ? idx : -1;
        };
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
                        if (ev.button == MouseButton::Left) {
                            int ti = tabAtPoint(ev.x, ev.y);
                            if (ti >= 0) {
                                // Switch to the clicked entry's own TabSel (shared
                                // sidebar::/gamefilter::) — this carries the page
                                // AND arg, so platform AND collection tabs filter
                                // correctly without re-guessing from the label.
                                activeTabLabel = sidebarEntries[ti].label;
                                activeTab = sidebarEntries[ti].sel;
                                applyQuery();
                            } else {
                                ctrl.clickAt(ev.x, ev.y);
                            }
                        }
                        break;
                    case EventType::TextInput:
                        // Live incremental search through the shared predicate.
                        liveQuery += ev.text;
                        applyQuery();
                        break;
                    case EventType::KeyDown:
                        switch (ev.key) {
                            case Key::Escape: quit = true; break;
                            case Key::F1:
                                // Cycle the shared sort modes (Title→Platform→
                                // Rating→Playtime→Recent) and re-apply the query.
                                sortIdx = (sortIdx + 1) % 5;
                                sortAll(gamesort::modeFromIndex(sortIdx));
                                applyQuery();
                                break;
                            case Key::Backspace:
                                if (!liveQuery.empty()) {
                                    // Drop one UTF-8 code point (trailing
                                    // continuation bytes, then the lead byte).
                                    while (!liveQuery.empty() &&
                                           (liveQuery.back() & 0xC0) == 0x80)
                                        liveQuery.pop_back();
                                    if (!liveQuery.empty()) liveQuery.pop_back();
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
    }
    return ok ? 0 : 1;
}
