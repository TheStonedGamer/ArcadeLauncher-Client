#pragma once
// platform/Window.h — window surface + input events (Linux port L1).
//
// The app's message loop talks to IWindow instead of HWND/WndProc. Windows
// wraps the existing Win32 window (L1); Linux implements with SDL2 (L3). Pixel
// coordinates are physical; text input arrives pre-composed as UTF-8.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace platform {

enum class EventType {
    None, Quit, Resize, MouseMove, MouseDown, MouseUp, MouseWheel,
    KeyDown, KeyUp, TextInput, FocusGained, FocusLost,
};

enum class MouseButton { Left, Right, Middle };

// Platform-neutral key codes (subset the UI actually uses). Printable keys come
// through TextInput; these cover navigation/control keys.
enum class Key {
    Unknown, Escape, Enter, Tab, Backspace, Delete, Space,
    Left, Right, Up, Down, PageUp, PageDown, Home, End,
    F1, F2, F3, F4, F5, F11,
};

struct Event {
    EventType   type = EventType::None;
    // Mouse
    float       x = 0, y = 0;            // cursor position (MouseMove/Down/Up)
    MouseButton button = MouseButton::Left;
    float       wheel = 0;              // MouseWheel: +up / -down
    // Keyboard
    Key         key = Key::Unknown;
    bool        ctrl = false, shift = false, alt = false;
    std::string text;                  // TextInput: one composed UTF-8 grapheme
    // Resize
    int         width = 0, height = 0;
};

class IWindow {
public:
    virtual ~IWindow() = default;

    // Pull the next pending event; returns false when the queue is empty.
    virtual bool poll(Event& out) = 0;

    virtual void size(int& w, int& h) const = 0;
    virtual void setTitle(const std::string& utf8) = 0;
    virtual void show(bool visible) = 0;
    virtual void requestRedraw() = 0;

    virtual std::string clipboardText() const = 0;
    virtual void setClipboardText(const std::string& utf8) = 0;

    // Native handle escape hatch (HWND on Windows, SDL_Window* on Linux) for the
    // renderer to bind a drawing surface. Cast at the impl boundary only.
    virtual void* nativeHandle() const = 0;
};

} // namespace platform
