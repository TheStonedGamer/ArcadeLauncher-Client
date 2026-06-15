// WsSelfCheckMain.cpp — Phase L2b headless social-gateway self-check.
//
// Proves the IXWebSocket-backed platform::IWebSocket performs a real WSS
// handshake against the social gateway through nginx/TLS. Two modes:
//
//   Authenticated (preferred): set ARCADE_USER + ARCADE_PASS (and optionally
//   ARCADE_TOTP). The check logs in via {base}/api/login to obtain a token,
//   opens wss://{host}/ws/social?token=…, and succeeds when it receives the
//   server's first frame: {"type":"hello",…}.
//
//   Unauthenticated (fallback, no creds): opens the gateway with no token. The
//   gateway answers 401, so success here means the handshake *reached* the
//   server and got a definitive response — distinguishing a working transport
//   from DNS/TLS/connection failure.
//
//   ./ws_selfcheck [base_url]
//
// base_url defaults to the production server. Exit 0 on success.

#include "Platform/Net.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

namespace {

std::string envOr(const char* key, const std::string& fallback = "") {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : fallback;
}

// Tiny extractor for "token":"<value>" out of the login JSON. Good enough for a
// self-check; the real client parses with SocialJson.
std::string extractToken(const std::string& json) {
    const std::string key = "\"token\"";
    auto k = json.find(key);
    if (k == std::string::npos) return "";
    auto colon = json.find(':', k + key.size());
    if (colon == std::string::npos) return "";
    auto q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return "";
    auto q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return json.substr(q1 + 1, q2 - q1 - 1);
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}

// Map https://host -> wss://host, http://host -> ws://host.
std::string toWsScheme(std::string base) {
    if (base.rfind("https://", 0) == 0) return "wss://" + base.substr(8);
    if (base.rfind("http://", 0) == 0)  return "ws://"  + base.substr(7);
    return "wss://" + base;  // schemeless -> secure default
}

} // namespace

int main(int argc, char** argv) {
    std::string base = (argc > 1) ? argv[1] : "https://arcade.orlandoaio.net";
    while (!base.empty() && base.back() == '/') base.pop_back();

    const std::string user = envOr("ARCADE_USER");
    const std::string pass = envOr("ARCADE_PASS");
    const std::string totp = envOr("ARCADE_TOTP");
    const bool authed = !user.empty() && !pass.empty();

    std::string token;
    if (authed) {
        auto http = platform::makeHttpClient();
        platform::HttpRequest req;
        req.method = "POST";
        req.url = base + "/api/login";
        req.headers["Content-Type"] = "application/x-www-form-urlencoded";
        req.body = "username=" + urlEncode(user) + "&password=" + urlEncode(pass);
        if (!totp.empty()) req.body += "&totp_code=" + urlEncode(totp);
        auto resp = http->request(req);
        if (!resp.ok || resp.status < 200 || resp.status >= 300) {
            std::printf("ws self-check: LOGIN FAILED (ok=%d status=%d %s)\n",
                        resp.ok, resp.status, resp.error.c_str());
            return 1;
        }
        token = extractToken(resp.body);
        if (token.empty()) {
            std::printf("ws self-check: no token in login response\n");
            return 1;
        }
    }

    std::string url = toWsScheme(base) + "/ws/social";
    if (!token.empty()) url += "?token=" + urlEncode(token);

    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    int  rc = 99;
    std::string detail;

    auto ws = platform::makeWebSocket();
    auto finish = [&](int code, std::string d) {
        std::lock_guard<std::mutex> lk(m);
        if (done) return;
        rc = code; detail = std::move(d); done = true;
        cv.notify_all();
    };

    ws->setOnOpen([&] {
        if (!authed) finish(0, "handshake opened (unauthenticated)");
        // authed: wait for the hello frame below.
    });
    ws->setOnText([&](const std::string& msg) {
        if (msg.find("\"hello\"") != std::string::npos)
            finish(0, "received hello frame");
    });
    ws->setOnClose([&](int code, const std::string& reason) {
        if (!authed) {
            // 401 (or any definitive server response) means the transport
            // reached the gateway — that's the success criterion here.
            if (code == 401 || code == 1000 || code == 1008 || code > 0)
                finish(0, "gateway responded (code " + std::to_string(code) +
                          (reason.empty() ? "" : ": " + reason) + ")");
            else
                finish(2, "closed without server response (code " +
                          std::to_string(code) + ")");
        } else {
            finish(2, "closed before hello (code " + std::to_string(code) +
                      (reason.empty() ? "" : ": " + reason) + ")");
        }
    });

    ws->connect(url, {});

    {
        std::unique_lock<std::mutex> lk(m);
        cv.wait_for(lk, std::chrono::seconds(15), [&] { return done; });
        if (!done) { rc = 3; detail = "timed out waiting for gateway"; }
    }
    ws->close();

    std::printf("ws self-check: %s — %s (%s)\n",
                rc == 0 ? "OK" : "FAILED",
                detail.c_str(),
                authed ? "authenticated" : "unauthenticated");
    return rc;
}
