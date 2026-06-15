// GuiSmokeMain.cpp — Phase L3a headless GUI smoke ("first pixels").
//
// Opens a real platform window via platform::makeWindow (SDL2 + GL on Linux),
// renders a known clear color through the GL context, reads the framebuffer
// back to PROVE actual pixels were produced (not just that calls returned), and
// writes gui_smoke.ppm for visual inspection. Then it pumps the IWindow event
// loop for a few frames and exits.
//
//   ./gui_smoke            # render a few frames, verify, exit (default)
//   ./gui_smoke --hold     # keep the window open until closed (manual look)
//
// Exit 0 on success. If no display is available (e.g. CI without xvfb) the
// window can't be created; the check prints SKIP and still exits 0 so it never
// falsely fails a headless box. Run under xvfb-run to exercise it for real.

#include "Platform/Window.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Target clear color (dark slate, matching the app's dark theme), in 0..1.
constexpr float kR = 32.0f / 255.0f;
constexpr float kG = 34.0f / 255.0f;
constexpr float kB = 48.0f / 255.0f;

bool writePpm(const char* path, const std::vector<uint8_t>& rgb, int w, int h) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    // glReadPixels returns bottom-up rows; flip to top-down for a normal image.
    for (int y = h - 1; y >= 0; --y)
        std::fwrite(&rgb[(size_t)y * w * 3], 1, (size_t)w * 3, f);
    std::fclose(f);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const bool hold = (argc > 1 && std::strcmp(argv[1], "--hold") == 0);

    auto win = platform::makeWindow("ArcadeLauncher (Linux) — L3a first pixels",
                                    960, 600);
    if (!win) {
        std::printf("gui smoke: SKIP (no display: %s)\n", SDL_GetError());
        return 0;  // headless box without xvfb — not a failure
    }
    win->show(true);

    int w = 0, h = 0;
    win->size(w, h);
    if (w <= 0 || h <= 0) { w = 960; h = 600; }

    // Render a handful of frames so the window manager actually maps/paints it.
    auto* sdlWin = static_cast<SDL_Window*>(win->nativeHandle());
    bool quit = false;
    for (int frame = 0; frame < 5 && !quit; ++frame) {
        platform::Event ev;
        while (win->poll(ev))
            if (ev.type == platform::EventType::Quit) quit = true;

        glViewport(0, 0, w, h);
        glClearColor(kR, kG, kB, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // (Shapes/text arrive in L3b via the nanovg Renderer2D.)
        SDL_GL_SwapWindow(sdlWin);
        SDL_Delay(16);
    }

    // Draw one more frame and read it straight out of the BACK buffer *before*
    // swapping — the front buffer is undefined under a compositor, so back is
    // the reliable source of truth for what we just rendered.
    glViewport(0, 0, w, h);
    glClearColor(kR, kG, kB, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    glReadBuffer(GL_BACK);

    uint8_t px[3] = {0, 0, 0};
    glReadPixels(w / 2, h / 2, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, px);

    std::vector<uint8_t> buf((size_t)w * h * 3, 0);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, buf.data());
    const char* ppm = "gui_smoke.ppm";
    bool wrote = writePpm(ppm, buf, w, h);

    SDL_GL_SwapWindow(sdlWin);  // present the verified frame

    const int ER = (int)(kR * 255 + 0.5f), EG = (int)(kG * 255 + 0.5f),
              EB = (int)(kB * 255 + 0.5f);
    auto near = [](int a, int b) { return (a - b <= 6) && (b - a <= 6); };
    const bool ok = near(px[0], ER) && near(px[1], EG) && near(px[2], EB);

    std::printf("gui smoke: %s — %dx%d window, center pixel (%d,%d,%d) "
                "expected ~(%d,%d,%d); wrote %s%s\n",
                ok ? "OK" : "FAILED", w, h, px[0], px[1], px[2], ER, EG, EB,
                wrote ? ppm : "<ppm failed>", hold ? "" : "");

    if (hold) {
        std::printf("gui smoke: holding window open — close it to exit.\n");
        while (!quit) {
            platform::Event ev;
            while (win->poll(ev))
                if (ev.type == platform::EventType::Quit ||
                    (ev.type == platform::EventType::KeyDown &&
                     ev.key == platform::Key::Escape))
                    quit = true;
            glViewport(0, 0, w, h);
            glClearColor(kR, kG, kB, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            SDL_GL_SwapWindow(sdlWin);
            SDL_Delay(16);
        }
    }

    return ok ? 0 : 1;
}
