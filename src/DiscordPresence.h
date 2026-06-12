#pragma once
#include "pch.h"
#include <string>
#include <mutex>

// Lightweight Discord Rich Presence over the local IPC named pipe
// (\\.\pipe\discord-ipc-N). No SDK and no external dependencies — it speaks the
// documented Discord-IPC v1 frame protocol directly (8-byte little-endian header
// {opcode,length} + JSON body).
//
// Every method is a safe no-op when Discord isn't running, the pipe drops, or no
// client ID is configured, so callers can fire-and-forget. Connection is lazy and
// re-established automatically if Discord starts later or restarts.
class DiscordPresence {
public:
    DiscordPresence() = default;
    ~DiscordPresence();

    // clientId: the Application ID from the Discord developer portal. Empty means
    // disabled. The application's name + uploaded art assets are what users see.
    void Start(const std::wstring& clientId);
    void Stop();

    // Show "Playing <title>" with an elapsed timer starting now. `details` is an
    // optional second line (e.g. the platform).
    void SetPlaying(const std::wstring& title, const std::wstring& details = L"");

    // Idle state shown while browsing the library.
    void SetIdle();

private:
    bool EnsureConnected();              // connect + handshake if needed
    bool WriteFrame(uint32_t opcode, const std::string& json);
    void DrainReplies();                 // best-effort discard of server frames
    void Disconnect();
    bool SendActivity(const std::string& activityJson);

    HANDLE      m_pipe = INVALID_HANDLE_VALUE;
    std::string m_clientId;              // UTF-8 application id
    bool        m_handshaked = false;
    int64_t     m_startEpoch = 0;        // current activity start (0 = idle)
    uint32_t    m_nonce = 0;
    std::mutex  m_mutex;
};
