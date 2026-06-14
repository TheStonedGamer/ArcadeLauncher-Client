#pragma once
// platform/Net.h — HTTP + WebSocket boundary (Linux port L1).
//
// Abstract interfaces the app talks to instead of WinHTTP directly. Windows
// wraps the existing ServerClient/WinHTTP pumps (L1); Linux implements these
// with libcurl + IXWebSocket (L2). All strings are UTF-8.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace platform {

struct HttpRequest {
    std::string method = "GET";              // GET/POST/PUT/DELETE
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;                        // raw bytes (may be binary)
    std::string range;                       // optional Range header value
    int timeoutMs = 30000;
};

struct HttpResponse {
    bool        ok = false;                  // transport succeeded (any status)
    int         status = 0;                  // HTTP status code
    std::string body;                        // raw bytes (binary-safe)
    std::map<std::string, std::string> headers;
    std::string error;                       // populated when !ok
};

// Synchronous HTTP client. Implementations must be safe to call from worker
// threads (the app issues downloads/REST off the UI thread).
class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual HttpResponse request(const HttpRequest& req) = 0;
};

// Minimal WebSocket client mirroring the social gateway's needs: text + binary
// frames, lifecycle callbacks. Callbacks fire on an internal worker thread.
class IWebSocket {
public:
    virtual ~IWebSocket() = default;

    using OnOpen    = std::function<void()>;
    using OnText    = std::function<void(const std::string&)>;
    using OnBinary  = std::function<void(const std::vector<uint8_t>&)>;
    using OnClose   = std::function<void(int code, const std::string& reason)>;

    virtual void setOnOpen(OnOpen) = 0;
    virtual void setOnText(OnText) = 0;
    virtual void setOnBinary(OnBinary) = 0;
    virtual void setOnClose(OnClose) = 0;

    // url is ws://|wss://; headers carry auth (the gateway accepts ?token= too).
    virtual bool connect(const std::string& url,
                         const std::map<std::string, std::string>& headers) = 0;
    virtual bool sendText(const std::string& msg) = 0;
    virtual bool sendBinary(const std::vector<uint8_t>& data) = 0;
    virtual void close() = 0;
};

std::unique_ptr<IHttpClient> makeHttpClient();   // provided by the platform impl

} // namespace platform
