#pragma once
// WebSocketClient.h - persistent client WebSocket over the WinHTTP WebSocket
// API (Windows 8+), so no third-party networking dependency is needed. The
// connection runs on its own worker thread; received UTF-8 text frames are
// delivered via a callback. Send is thread-safe and non-blocking from the
// caller's perspective (WinHttpWebSocketSend buffers). Control ping/pong is
// handled internally by WinHTTP, keeping proxy keep-alives alive.

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

namespace social {

class WebSocketClient {
public:
    using MessageCallback = std::function<void(const std::string& utf8)>;
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

    // Queue a UTF-8 text frame. Returns false if not currently connected.
    bool SendText(const std::string& utf8);

    bool IsConnected() const { return m_connected.load(); }

    // Tears down the socket and joins the worker thread. Idempotent.
    void Close();

private:
    void Run(std::wstring url, MessageCallback onMessage, StateCallback onState);

    std::thread          m_worker;
    std::atomic<bool>    m_connected{ false };
    std::atomic<bool>    m_stop{ false };

    std::mutex           m_handleMtx;
    void*                m_hWebSocket = nullptr; // HINTERNET, guarded by m_handleMtx
    void*                m_hSession   = nullptr;
    void*                m_hConnect   = nullptr;
};

} // namespace social
