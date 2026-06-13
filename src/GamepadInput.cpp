#include "pch.h"
#include "GamepadInput.h"
#include <xinput.h>

// xinput9_1_0 ships with Windows 8+ and needs no redistributable, unlike the
// SDK-versioned xinput.lib.
#pragma comment(lib, "xinput9_1_0.lib")

GamepadInput::~GamepadInput() {
    Stop();
}

void GamepadInput::Start(HWND hwnd) {
    if (m_running.exchange(true)) return;
    m_hwnd = hwnd;
    m_thread = std::thread([this] { Worker(); });
}

void GamepadInput::Stop() {
    if (!m_running.exchange(false)) return;
    if (m_thread.joinable()) m_thread.join();
}

namespace {

// A logical navigation action the poller can emit. Directionals auto-repeat while
// held; the rest fire once per press (edge-triggered).
enum class NavKey { None, Left, Right, Up, Down, Accept, Back, PageUp, PageDown };

WPARAM VkFor(NavKey k) {
    switch (k) {
    case NavKey::Left:     return VK_LEFT;
    case NavKey::Right:    return VK_RIGHT;
    case NavKey::Up:       return VK_UP;
    case NavKey::Down:     return VK_DOWN;
    case NavKey::Accept:   return VK_RETURN;
    case NavKey::Back:     return VK_ESCAPE;
    case NavKey::PageUp:   return VK_PRIOR;
    case NavKey::PageDown: return VK_NEXT;
    default:               return 0;
    }
}

} // namespace

void GamepadInput::Worker() {
    // Edge state for the buttons that fire once per press.
    WORD prevButtons = 0;
    bool haveController = false;

    // Direction auto-repeat timing (ms): a longer delay before the first repeat,
    // then a steady rate — the familiar key-repeat feel.
    const DWORD kFirstRepeat = 380, kRepeat = 110;
    NavKey heldDir = NavKey::None;
    DWORD  nextDirFire = 0;

    auto postKey = [&](WPARAM vk) {
        if (!vk || !m_hwnd) return;
        PostMessageW(m_hwnd, WM_KEYDOWN, vk, 0);
        PostMessageW(m_hwnd, WM_KEYUP,   vk, 0);
    };

    while (m_running.load()) {
        XINPUT_STATE state{};
        DWORD result = ERROR_DEVICE_NOT_CONNECTED;
        // Use the first connected controller (slots 0..3).
        for (DWORD i = 0; i < 4; ++i) {
            if (XInputGetState(i, &state) == ERROR_SUCCESS) { result = ERROR_SUCCESS; break; }
        }
        if (result != ERROR_SUCCESS) {
            // No controller — reset edge state so a reconnect starts clean.
            haveController = false;
            prevButtons = 0;
            heldDir = NavKey::None;
            Sleep(250);
            continue;
        }

        // Focus gate: only drive the launcher while it is the foreground window.
        // When a launched game is running (launcher hidden/background) or any
        // other app is focused, controller input belongs to that app — synthesising
        // key presses into our hidden window would scroll an unseen grid and could
        // leak through as stray nav. Re-read the live state each poll but suppress
        // posting; reset edge/repeat state so regaining focus starts clean and the
        // currently-held buttons aren't replayed as fresh presses.
        if (GetForegroundWindow() != m_hwnd) {
            prevButtons = state.Gamepad.wButtons;  // adopt current buttons as baseline
            haveController = false;                 // ignore held state on refocus
            heldDir = NavKey::None;
            Sleep(64);
            continue;
        }

        WORD btn = state.Gamepad.wButtons;
        SHORT lx = state.Gamepad.sThumbLX, ly = state.Gamepad.sThumbLY;
        const SHORT DZ = 16000;   // generous deadzone for discrete nav

        // Resolve the current directional intent from D-pad OR left stick.
        NavKey dir = NavKey::None;
        if      (btn & XINPUT_GAMEPAD_DPAD_LEFT  || lx < -DZ) dir = NavKey::Left;
        else if (btn & XINPUT_GAMEPAD_DPAD_RIGHT || lx >  DZ) dir = NavKey::Right;
        else if (btn & XINPUT_GAMEPAD_DPAD_UP    || ly >  DZ) dir = NavKey::Up;
        else if (btn & XINPUT_GAMEPAD_DPAD_DOWN  || ly < -DZ) dir = NavKey::Down;

        DWORD nowMs = GetTickCount();
        if (dir != NavKey::None) {
            if (dir != heldDir) {           // new direction: fire immediately
                postKey(VkFor(dir));
                heldDir = dir;
                nextDirFire = nowMs + kFirstRepeat;
            } else if ((LONG)(nowMs - nextDirFire) >= 0) {
                postKey(VkFor(dir));        // auto-repeat while held
                nextDirFire = nowMs + kRepeat;
            }
        } else {
            heldDir = NavKey::None;
        }

        // Edge-triggered buttons: act only on the press (not while held).
        WORD pressed = btn & ~prevButtons;
        if (!haveController) { pressed = 0; haveController = true; }   // ignore initial state

        if (pressed & XINPUT_GAMEPAD_A)              postKey(VK_RETURN);
        if (pressed & XINPUT_GAMEPAD_B)              postKey(VK_ESCAPE);
        if (pressed & XINPUT_GAMEPAD_LEFT_SHOULDER)  postKey(VK_PRIOR);
        if (pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER) postKey(VK_NEXT);
        // Start/Menu → toggle Big Picture (reuses the window's F11 fullscreen path).
        if (pressed & XINPUT_GAMEPAD_START)          postKey(VK_F11);
        if (pressed & XINPUT_GAMEPAD_BACK)           postKey(VK_ESCAPE);

        prevButtons = btn;
        Sleep(16);   // ~60 Hz poll
    }
}
