// WindowSdl.cpp — SDL2 implementation of platform::IWindow (Linux port L3a).
//
// Owns an SDL window + GL context and translates SDL events into the platform-
// neutral Event stream the app's loop consumes. This replaces the Win32
// HWND/WndProc on Linux. The renderer (L3b, nanovg/GL) binds to the GL context
// via nativeHandle() (an SDL_Window*) and presents with SDL_GL_SwapWindow.
//
// Only compiled on non-Windows builds where SDL2 was found (see CMakeLists.txt).

#include "Platform/Window.h"

#include <SDL.h>

namespace platform {
namespace {

Key translateKey(SDL_Keycode k) {
    switch (k) {
    case SDLK_ESCAPE:    return Key::Escape;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:  return Key::Enter;
    case SDLK_TAB:       return Key::Tab;
    case SDLK_BACKSPACE: return Key::Backspace;
    case SDLK_DELETE:    return Key::Delete;
    case SDLK_SPACE:     return Key::Space;
    case SDLK_LEFT:      return Key::Left;
    case SDLK_RIGHT:     return Key::Right;
    case SDLK_UP:        return Key::Up;
    case SDLK_DOWN:      return Key::Down;
    case SDLK_PAGEUP:    return Key::PageUp;
    case SDLK_PAGEDOWN:  return Key::PageDown;
    case SDLK_HOME:      return Key::Home;
    case SDLK_END:       return Key::End;
    case SDLK_F1:        return Key::F1;
    case SDLK_F2:        return Key::F2;
    case SDLK_F3:        return Key::F3;
    case SDLK_F4:        return Key::F4;
    case SDLK_F5:        return Key::F5;
    case SDLK_F11:       return Key::F11;
    default:             return Key::Unknown;
    }
}

MouseButton translateButton(Uint8 b) {
    switch (b) {
    case SDL_BUTTON_RIGHT:  return MouseButton::Right;
    case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
    default:                return MouseButton::Left;
    }
}

void fillMods(Event& e) {
    const SDL_Keymod m = SDL_GetModState();
    e.ctrl  = (m & KMOD_CTRL)  != 0;
    e.shift = (m & KMOD_SHIFT) != 0;
    e.alt   = (m & KMOD_ALT)   != 0;
}

class SdlWindow final : public IWindow {
public:
    bool create(const std::string& title, int w, int h) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;

        // Request a GL 3.2 core-ish context; llvmpipe/Mesa provides it. nanovg's
        // GL3 backend (L3b) targets this.
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);  // nanovg uses the stencil

        win_ = SDL_CreateWindow(
            title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
            SDL_WINDOW_HIDDEN);
        if (!win_) return false;

        gl_ = SDL_GL_CreateContext(win_);
        if (!gl_) { SDL_DestroyWindow(win_); win_ = nullptr; return false; }

        SDL_GL_MakeCurrent(win_, gl_);
        SDL_GL_SetSwapInterval(1);  // vsync
        SDL_StartTextInput();
        return true;
    }

    ~SdlWindow() override {
        if (gl_)  SDL_GL_DeleteContext(gl_);
        if (win_) SDL_DestroyWindow(win_);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    bool poll(Event& out) override {
        SDL_Event ev;
        if (!SDL_PollEvent(&ev)) return false;
        out = Event{};
        switch (ev.type) {
        case SDL_QUIT:
            out.type = EventType::Quit;
            break;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                out.type = EventType::Resize;
                out.width = ev.window.data1;
                out.height = ev.window.data2;
            } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                out.type = EventType::FocusGained;
            } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                out.type = EventType::FocusLost;
            }
            break;
        case SDL_MOUSEMOTION:
            out.type = EventType::MouseMove;
            out.x = (float)ev.motion.x; out.y = (float)ev.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            out.type = (ev.type == SDL_MOUSEBUTTONDOWN) ? EventType::MouseDown
                                                        : EventType::MouseUp;
            out.x = (float)ev.button.x; out.y = (float)ev.button.y;
            out.button = translateButton(ev.button.button);
            fillMods(out);
            break;
        case SDL_MOUSEWHEEL:
            out.type = EventType::MouseWheel;
            out.wheel = (float)ev.wheel.y;
            fillMods(out);
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            out.type = (ev.type == SDL_KEYDOWN) ? EventType::KeyDown
                                                : EventType::KeyUp;
            out.key = translateKey(ev.key.keysym.sym);
            fillMods(out);
            break;
        case SDL_TEXTINPUT:
            out.type = EventType::TextInput;
            out.text = ev.text.text;  // already composed UTF-8
            break;
        default:
            // Unmapped event — report a no-op so the caller keeps draining.
            out.type = EventType::None;
            break;
        }
        return true;
    }

    void size(int& w, int& h) const override {
        w = h = 0;
        if (win_) SDL_GL_GetDrawableSize(win_, &w, &h);
    }
    void setTitle(const std::string& utf8) override {
        if (win_) SDL_SetWindowTitle(win_, utf8.c_str());
    }
    void show(bool visible) override {
        if (!win_) return;
        if (visible) SDL_ShowWindow(win_); else SDL_HideWindow(win_);
    }
    void requestRedraw() override { /* immediate-mode loop redraws each frame */ }

    std::string clipboardText() const override {
        char* t = SDL_GetClipboardText();
        std::string s = t ? t : "";
        if (t) SDL_free(t);
        return s;
    }
    void setClipboardText(const std::string& utf8) override {
        SDL_SetClipboardText(utf8.c_str());
    }

    void* nativeHandle() const override { return win_; }

private:
    SDL_Window*   win_ = nullptr;
    SDL_GLContext gl_  = nullptr;
};

} // namespace

std::unique_ptr<IWindow> makeWindow(const std::string& title, int width, int height) {
    auto w = std::make_unique<SdlWindow>();
    if (!w->create(title, width, height)) return nullptr;
    return w;
}

} // namespace platform
