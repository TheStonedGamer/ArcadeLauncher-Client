#pragma once
// WebSocketClient.h - persistent client WebSocket over the WinHTTP WebSocket
// API (Windows 8+), so no third-party networking dependency is needed. The
// connection runs on its own worker thread; received UTF-8 text frames are
// delivered via a callback. Send is thread-safe and non-blocking from the
// caller's perspective (WinHttpWebSocketSend buffers). WinHTTP answers WS
// *control* ping/pong internally, but it does NOT surface them to a blocked
// WinHttpWebSocketReceive, so they don't keep the receive loop alive on an idle
// link — the SocialManager sends an application-level {"type":"ping"} for that.
// Note: pass an http(s):// URL here, not ws(s):// — WinHttpCrackUrl doesn't know
// the ws schemes; Run() maps wss->https / ws->http before the WebSocket upgrade.

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace social {

class WebSocketClient {
public:
    using MessageCallback = std::function<void(const std::string& utf8)>;
    using BinaryCallback  = std::function<void(const void* data, size_t len)>;
    using StateCallback   = std::function<void(bool connected)>;

    WebSocketClient() = default;
    ~WebSocketClient();

    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    // url: wss://host/ws/social?token=... (or ws:// for local). onMessage fires
    // on the worker thread; marshal to the UI thread in the handler. Connect
    // starts the worker and returns immediately; success/failure is reported via
    // onState. Safe to call once; use Close() then Connect() to reconnect.
    void Connect(const std::wstring& url, MessageCallback onMessage, StateCallback onState);

    // Optional binary frame sink (voice audio). Set before Connect; fires on the
    // worker thread. Separate from the text MessageCallback so control/chat JSON
    // and audio stay on distinct paths.
    void SetBinaryHandler(BinaryCallback onBinary) { m_onBinary = std::move(onBinary); }

    // Queue a UTF-8 text frame. Returns false if not currently connected.
    bool SendText(const std::string& utf8);

    // Queue a binary frame (voice audio). Returns false if not connected.
    bool SendBinary(const void* data, size_t len);

    bool IsConnected() const { return m_connected.load(); }

    // Tears down the socket and joins the worker thread. Idempotent.
    void Close();

private:
    void Run(std::wstring url, MessageCallback onMessage, StateCallback onState);

    BinaryCallback       m_onBinary;
    std::thread          m_worker;
    std::atomic<bool>    m_connected{ false };
    std::atomic<bool>    m_stop{ false };

    std::mutex           m_handleMtx;
    void*                m_hWebSocket = nullptr; // HINTERNET, guarded by m_handleMtx
    void*                m_hSession   = nullptr;
    void*                m_hConnect   = nullptr;
};

} // namespace social
