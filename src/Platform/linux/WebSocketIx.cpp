// WebSocketIx.cpp — IXWebSocket implementation of platform::IWebSocket (Linux
// port L2b). Drives the social gateway (wss://…/ws/social): text/binary frames
// and lifecycle callbacks, with TLS through nginx. Replaces the WinHTTP
// WebSocket pump on Linux; the Windows build keeps its own impl.
//
// Only compiled on non-Windows builds where IXWebSocket was made available
// (see CMakeLists.txt).
//
// Notes that mirror the Windows client's hard-won gotchas (CLAUDE.md):
//   * The app-level heartbeat ({"type":"ping"} every ~20s) lives in
//     SocialManager, NOT here — this class is a dumb frame pipe, same as the
//     Windows WebSocketClient. We do not invent our own ping here.
//   * Auth rides on the query string (?token=) or an Authorization header; both
//     are accepted by the gateway. Callers pass whichever they have.

#include "Platform/Net.h"

#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXNetSystem.h>

#include <atomic>
#include <mutex>

namespace platform {
namespace {

// IXWebSocket needs one-time network init (a no-op on Linux, real on Windows;
// harmless to call here and keeps the impl portable if ever reused).
void ensureNetInit() {
    static std::once_flag once;
    std::call_once(once, [] { ix::initNetSystem(); });
}

class IxWebSocket final : public IWebSocket {
public:
    IxWebSocket() { ensureNetInit(); }
    ~IxWebSocket() override { close(); }

    void setOnOpen(OnOpen cb) override   { onOpen_   = std::move(cb); }
    void setOnText(OnText cb) override   { onText_   = std::move(cb); }
    void setOnBinary(OnBinary cb) override { onBinary_ = std::move(cb); }
    void setOnClose(OnClose cb) override { onClose_  = std::move(cb); }

    bool connect(const std::string& url,
                 const std::map<std::string, std::string>& headers) override {
        ws_.setUrl(url);

        if (!headers.empty()) {
            ix::WebSocketHttpHeaders h;
            for (const auto& kv : headers) h[kv.first] = kv.second;
            ws_.setExtraHeaders(h);
        }

        // The gateway keepalive is application-level ({"type":"ping"} from
        // SocialManager); we leave IXWebSocket's own protocol ping off so we
        // exactly mirror the Windows pump's behavior. (The server also sends a
        // data-frame heartbeat that wakes the receive loop.)
        ws_.disablePerMessageDeflate();

        ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
            switch (msg->type) {
            case ix::WebSocketMessageType::Open:
                if (onOpen_) onOpen_();
                break;
            case ix::WebSocketMessageType::Message:
                if (msg->binary) {
                    if (onBinary_) {
                        std::vector<uint8_t> data(msg->str.begin(), msg->str.end());
                        onBinary_(data);
                    }
                } else {
                    if (onText_) onText_(msg->str);
                }
                break;
            case ix::WebSocketMessageType::Close:
                if (onClose_)
                    onClose_((int)msg->closeInfo.code, msg->closeInfo.reason);
                break;
            case ix::WebSocketMessageType::Error:
                // Surface a failed upgrade/handshake (e.g. 401, TLS, DNS) as a
                // close with the HTTP status as the code so callers can react.
                if (onClose_)
                    onClose_(msg->errorInfo.http_status, msg->errorInfo.reason);
                break;
            default:
                break;
            }
        });

        // Background, auto-reconnecting connection (matches the Windows pump,
        // whose reconnect/backoff is owned by SocialManager — IXWebSocket's
        // built-in retry is left at defaults; SocialManager still drives the
        // higher-level state machine).
        ws_.start();
        return true;
    }

    bool sendText(const std::string& msg) override {
        return ws_.sendText(msg).success;
    }

    bool sendBinary(const std::vector<uint8_t>& data) override {
        return ws_.sendBinary(std::string(data.begin(), data.end())).success;
    }

    void close() override {
        ws_.stop();
    }

private:
    ix::WebSocket ws_;
    OnOpen   onOpen_;
    OnText   onText_;
    OnBinary onBinary_;
    OnClose  onClose_;
};

} // namespace

std::unique_ptr<IWebSocket> makeWebSocket() {
    return std::make_unique<IxWebSocket>();
}

} // namespace platform
