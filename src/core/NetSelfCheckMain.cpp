// NetSelfCheckMain.cpp — Phase L2 headless network self-check.
//
// Proves the libcurl-backed platform::IHttpClient works on Linux end-to-end by
// hitting the server's public health endpoint. No UI, no auth required.
//
//   ./net_selfcheck [base_url]
//
// base_url defaults to the production server. Exit 0 iff the transport
// succeeded AND the server returned 2xx for GET {base}/api/health.

#include "Platform/Net.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    std::string base = (argc > 1) ? argv[1] : "https://arcade.orlandoaio.net";
    // Trim a trailing slash so we don't emit a double slash.
    while (!base.empty() && base.back() == '/') base.pop_back();
    const std::string url = base + "/api/health";

    auto http = platform::makeHttpClient();
    platform::HttpRequest req;
    req.method = "GET";
    req.url = url;
    req.headers["Accept"] = "application/json";
    req.timeoutMs = 15000;

    platform::HttpResponse resp = http->request(req);

    if (!resp.ok) {
        std::printf("net self-check: TRANSPORT FAILED for %s (%s)\n",
                    url.c_str(), resp.error.c_str());
        return 1;
    }
    if (resp.status < 200 || resp.status >= 300) {
        std::printf("net self-check: HTTP %d for %s\n", resp.status, url.c_str());
        return 2;
    }

    std::printf("net self-check: OK (HTTP %d, %zu bytes) %s\n",
                resp.status, resp.body.size(), url.c_str());
    return 0;
}
