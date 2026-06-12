#pragma once
#include "pch.h"
#include <thread>
#include <atomic>

// Background XInput poller that turns controller input into ordinary keyboard
// navigation messages posted to the launcher window. By synthesizing the same
// VK_* keys the keyboard handlers already understand, a gamepad drives the entire
// UI from the couch with no separate input model to maintain.
//
// The Start/Menu button instead posts WM_APP_GAMEPAD_BIGPICTURE so the app can
// toggle a fullscreen "Big Picture" presentation.
class GamepadInput {
public:
    // Posted to the window when Start/Menu is pressed (toggle Big Picture mode).
    static constexpr UINT WM_APP_GAMEPAD_BIGPICTURE = WM_USER + 11;

    GamepadInput() = default;
    ~GamepadInput();

    void Start(HWND hwnd);
    void Stop();

private:
    void Worker();

    HWND              m_hwnd = nullptr;
    std::thread       m_thread;
    std::atomic<bool> m_running{ false };
};
