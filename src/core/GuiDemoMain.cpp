// GuiDemoMain.cpp — Phase L3b GUI demo: a real catalog grid drawn entirely
// through platform::IRenderer2D (nanovg/GL on Linux). Proves the renderer
// boundary end-to-end — rounded-rect tiles, gradients, text, a sidebar and top
// bar — the same primitives Renderer.cpp uses on Windows via Direct2D.
//
//   ./gui_demo            # render a few frames, verify pixels, dump PPM, exit
//   ./gui_demo --hold     # interactive: keep the window open until closed
//
// Verifies deterministically: each tile cover is a flat known color, so we read
// the back buffer and assert the first tile's center matches. Exit 0 on success;
// SKIP (exit 0) if there is no display.

#include "Platform/Window.h"
#include "Platform/Renderer2D.h"
#include "Platform/Image.h"

#include <GL/glew.h>
#include <SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace platform;

namespace {

struct Tile { std::string title; std::string platform; Color cover; ImageId art = 0; };

// Hex 0xRRGGBB → Color, matching Renderer.cpp's production GitHub-dark palette.
constexpr Color hex(unsigned v) {
    return Color{((v >> 16) & 0xFF) / 255.f, ((v >> 8) & 0xFF) / 255.f,
                 (v & 0xFF) / 255.f, 1.0f};
}
const Color kBg      = hex(0x0D1117);  // C_BG
const Color kPanel   = hex(0x13181E);  // C_SIDEBAR
const Color kTopbarC = hex(0x161B22);  // C_TOPBAR
const Color kCard    = hex(0x21262D);  // C_CARD
const Color kText    = hex(0xC9D1D9);  // C_TEXT
const Color kMuted   = hex(0x8B949E);  // C_SUBTEXT
const Color kAccentA = hex(0x58A6FF);  // C_ACCENT
const Color kAccentB = hex(0x388BFD);  // C_SELECTED

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
    // Round-trip through PNG to prove the real decode path, not just raw upload.
    std::vector<uint8_t> png = encodePngRGBA(rgba.data(), W, H);
    DecodedImage img;
    if (!png.empty()) decodeImageRGBA(png.data(), png.size(), img);
    return img;
}

std::vector<Tile> demoTiles() {
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

bool writePpm(const char* path, const std::vector<uint8_t>& rgb, int w, int h) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y)
        std::fwrite(&rgb[(size_t)y * w * 3], 1, (size_t)w * 3, f);
    std::fclose(f);
    return true;
}

constexpr int kSidebar = 180;
constexpr int kTopbar  = 56;
constexpr float kCols  = 4;
constexpr float kPad   = 20;

// Returns the cover rect of tile index i within the content area.
Rect tileCover(int i, int winW, float& outTileW, float& outTileH) {
    const float areaX = kSidebar + kPad;
    const float areaW = winW - kSidebar - 2 * kPad;
    const float tileW = (areaW - (kCols - 1) * kPad) / kCols;
    const float coverH = tileW * 1.3f;        // portrait cover
    outTileW = tileW; outTileH = coverH + 28; // + title strip
    const int col = i % (int)kCols;
    const int row = i / (int)kCols;
    const float x = areaX + col * (tileW + kPad);
    const float y = kTopbar + kPad + row * (coverH + 28 + kPad);
    return Rect{x, y, tileW, coverH};
}

void renderScene(IRenderer2D& r, int w, int h, FontId fTitle, FontId fBody,
                 const std::vector<Tile>& tiles) {
    r.beginFrame(w, h, 1.0f);

    // Background.
    r.fillRect(Rect{0, 0, (float)w, (float)h}, kBg);
    // Sidebar.
    r.fillRect(Rect{0, 0, (float)kSidebar, (float)h}, kPanel);
    // Top bar with brand gradient.
    r.linearGradientRect(Rect{0, 0, (float)w, (float)kTopbar}, kAccentA, kAccentB, false);
    r.drawText(fTitle, "ArcadeLauncher", 16, 18, kText, TextAlign::Left);

    // Sidebar entries.
    const char* tabs[] = {"All Games", "PC", "Consoles", "Favorites", "Downloads"};
    float ty = kTopbar + 20;
    for (const char* t : tabs) {
        r.drawText(fBody, t, 20, ty, kMuted, TextAlign::Left);
        ty += 34;
    }

    // Game grid.
    for (int i = 0; i < (int)tiles.size(); ++i) {
        float tw, th;
        Rect cover = tileCover(i, w, tw, th);
        if (tiles[i].art != 0) {
            // Real decoded cover art: card backing, then the image clipped to it.
            r.fillRoundedRect(cover, 8, kCard);
            r.pushClip(cover);
            r.drawImage(tiles[i].art, cover, 1.0f);
            r.popClip();
        } else {
            r.fillRoundedRect(cover, 8, tiles[i].cover);
        }
        r.strokeRoundedRect(cover, 8, Color{1, 1, 1, 0.08f}, 1.0f);
        // Platform pill.
        Rect pill{cover.x + 8, cover.y + 8, 56, 18};
        r.fillRoundedRect(pill, 9, Color{0, 0, 0, 0.45f});
        r.drawText(fBody, tiles[i].platform, pill.x + 28, pill.y + 3, kText,
                   TextAlign::Center);
        // Title under the cover.
        r.drawText(fBody, tiles[i].title, cover.x, cover.y + cover.h + 6, kText,
                   TextAlign::Left);
    }

    r.endFrame();
}

} // namespace

int main(int argc, char** argv) {
    const bool hold = (argc > 1 && std::strcmp(argv[1], "--hold") == 0);
    const int W = 1024, H = 680;

    auto win = makeWindow("ArcadeLauncher (Linux) — L3c catalog grid", W, H);
    if (!win) { std::printf("gui demo: SKIP (no display: %s)\n", SDL_GetError()); return 0; }
    win->show(true);

    auto r = makeRenderer(win.get());
    if (!r) { std::printf("gui demo: FAILED (renderer init: nanovg/GL)\n"); return 1; }

    FontId fTitle = r->loadFont("", 22, true);
    FontId fBody  = r->loadFont("", 15, false);
    auto tiles = demoTiles();

    // Tile 0 keeps a flat cover as the deterministic verification anchor; the
    // rest get real decoded cover art (PNG round-tripped) drawn via drawImage.
    for (int i = 1; i < (int)tiles.size(); ++i) {
        DecodedImage img = makeCoverImage(tiles[i].cover);
        if (img.valid())
            tiles[i].art = r->createImageRGBA(img.rgba.data(), img.w, img.h);
    }

    int w = W, h = H; win->size(w, h);

    bool quit = false;
    // Render a few identical frames; the back buffer then holds a full scene we
    // can read deterministically (front is undefined under a compositor).
    for (int f = 0; f < 3 && !quit; ++f) {
        Event ev; while (win->poll(ev)) if (ev.type == EventType::Quit) quit = true;
        renderScene(*r, w, h, fTitle, fBody, tiles);
        SDL_Delay(16);
    }

    glReadBuffer(GL_BACK);
    std::vector<uint8_t> buf((size_t)w * h * 3, 0);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, buf.data());
    bool wrote = writePpm("gui_demo.ppm", buf, w, h);

    // Verify the first tile's center pixel == its cover color (within tolerance).
    float tw, th; Rect c0 = tileCover(0, w, tw, th);
    int sx = (int)(c0.x + c0.w / 2), sy = (int)(c0.y + c0.h / 2);
    // glReadPixels rows are bottom-up; flip the top-down y to index the buffer.
    size_t idx = ((size_t)(h - 1 - sy) * w + sx) * 3;
    int gr = buf[idx], gg = buf[idx + 1], gb = buf[idx + 2];
    int er = (int)(tiles[0].cover.r * 255 + 0.5f),
        eg = (int)(tiles[0].cover.g * 255 + 0.5f),
        eb = (int)(tiles[0].cover.b * 255 + 0.5f);
    auto near = [](int a, int b) { return a - b <= 10 && b - a <= 10; };
    bool ok = near(gr, er) && near(gg, eg) && near(gb, eb);

    std::printf("gui demo: %s — %dx%d, %zu tiles; tile0 center (%d,%d,%d) "
                "expected ~(%d,%d,%d); wrote %s\n",
                ok ? "OK" : "FAILED", w, h, tiles.size(), gr, gg, gb, er, eg, eb,
                wrote ? "gui_demo.ppm" : "<ppm failed>");

    if (hold) {
        std::printf("gui demo: holding window open — close it (or Esc) to exit.\n");
        while (!quit) {
            Event ev;
            while (win->poll(ev))
                if (ev.type == EventType::Quit ||
                    (ev.type == EventType::KeyDown && ev.key == Key::Escape))
                    quit = true;
            renderScene(*r, w, h, fTitle, fBody, tiles);
            SDL_Delay(16);
        }
    }
    return ok ? 0 : 1;
}
