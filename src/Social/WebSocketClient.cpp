#include "pch.h"
#include "WebSocketClient.h"
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace social {

WebSocketClient::~WebSocketClient() {
    Close();
}

void WebSocketClient::Connect(const std::wstring& url, MessageCallback onMessage, StateCallback onState) {
    Close();
    m_stop.store(false);
    m_worker = std::thread(&WebSocketClient::Run, this, url, std::move(onMessage), std::move(onState));
}

bool WebSocketClient::SendText(const std::string& utf8) {
    std::lock_guard<std::mutex> lk(m_handleMtx);
    if (!m_hWebSocket || !m_connected.load()) return false;
    DWORD rc = WinHttpWebSocketSend(
        (HINTERNET)m_hWebSocket,
        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        (PVOID)utf8.data(),
        (DWORD)utf8.size());
    return rc == NO_ERROR;
}

bool WebSocketClient::SendBinary(const void* data, size_t len) {
    std::lock_guard<std::mutex> lk(m_handleMtx);
    if (!m_hWebSocket || !m_connected.load()) return false;
    DWORD rc = WinHttpWebSocketSend(
        (HINTERNET)m_hWebSocket,
        WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
        (PVOID)data,
        (DWORD)len);
    return rc == NO_ERROR;
}

void WebSocketClient::Close() {
    m_stop.store(true);
    {
        std::lock_guard<std::mutex> lk(m_handleMtx);
        if (m_hWebSocket) {
            WinHttpWebSocketClose((HINTERNET)m_hWebSocket,
                                  WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
            WinHttpCloseHandle((HINTERNET)m_hWebSocket);
            m_hWebSocket = nullptr;
        }
        if (m_hConnect) { WinHttpCloseHandle((HINTERNET)m_hConnect); m_hConnect = nullptr; }
        if (m_hSession) { WinHttpCloseHandle((HINTERNET)m_hSession); m_hSession = nullptr; }
    }
    if (m_worker.joinable()) {
        // Avoid self-join if Close() is somehow invoked from the worker.
        if (m_worker.get_id() != std::this_thread::get_id())
            m_worker.join();
        else
            m_worker.detach();
    }
    m_connected.store(false);
}

void WebSocketClient::Run(std::wstring url, MessageCallback onMessage, StateCallback onState) {
    auto fail = [&]() {
        m_connected.store(false);
        if (onState) onState(false);
    };

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {};
    wchar_t path[1024] = {};
    uc.lpszHostName = host;  uc.dwHostNameLength = 255;
    uc.lpszUrlPath  = path;  uc.dwUrlPathLength  = 1023;
    uc.dwSchemeLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { fail(); return; }
    bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;

    HINTERNET session = WinHttpOpen(L"ArcadeLauncher/SocialWS",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { fail(); return; }

    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    if (!connect) { WinHttpCloseHandle(session); fail(); return; }

    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        WinHttpCloseHandle(connect); WinHttpCloseHandle(session); fail(); return;
    }

    // Request a WebSocket upgrade.
    if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
        !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session);
        fail(); return;
    }

    HINTERNET websocket = WinHttpWebSocketCompleteUpgrade(request, 0);
    WinHttpCloseHandle(request); // request handle no longer needed after upgrade
    if (!websocket) {
        WinHttpCloseHandle(connect); WinHttpCloseHandle(session); fail(); return;
    }

    {
        std::lock_guard<std::mutex> lk(m_handleMtx);
        m_hSession = session;
        m_hConnect = connect;
        m_hWebSocket = websocket;
    }
    m_connected.store(true);
    if (onState) onState(true);

    // Receive loop. Reassembles fragmented UTF-8 messages, dispatches each
    // complete text frame. WinHTTP handles control ping/pong transparently.
    std::string accum;
    std::vector<char> binAccum;
    std::vector<char> buf(8192);
    while (!m_stop.load()) {
        DWORD read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        DWORD rc = WinHttpWebSocketReceive(websocket, buf.data(), (DWORD)buf.size(), &read, &type);
        if (rc != NO_ERROR) break;
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
        if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
            type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) {
            accum.append(buf.data(), read);
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                if (onMessage && !accum.empty()) onMessage(accum);
                accum.clear();
            }
        } else if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
                   type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE) {
            // Voice audio: [u64 LE sender-id][payload]. Reassemble fragments.
            binAccum.insert(binAccum.end(), buf.data(), buf.data() + read);
            if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
                if (m_onBinary && !binAccum.empty())
                    m_onBinary(binAccum.data(), binAccum.size());
                binAccum.clear();
            }
        }
    }

    m_connected.store(false);
    if (onState) onState(false);
    // Note: handle cleanup happens in Close(); the receive break here just ends
    // the loop. The owning SocialManager calls Close() (or destructs) to free.
}

} // namespace social
