// HttpClientCurl.cpp — libcurl implementation of platform::IHttpClient (Linux
// port L2). Synchronous, thread-safe (one CURL easy handle per request), and
// binary-safe for both request and response bodies. Replaces the WinHTTP REST
// pump on Linux; the Windows build keeps using its own impl.
//
// Only compiled on non-Windows builds where CURL was found (see CMakeLists.txt).

#include "Platform/Net.h"

#include <curl/curl.h>

#include <atomic>
#include <cctype>
#include <mutex>

namespace platform {
namespace {

// One-time global init. curl_global_init is not thread-safe, so guard it and
// run it before the first easy handle is created on any thread.
void ensureCurlGlobalInit() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

size_t writeBody(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    const size_t n = size * nmemb;
    out->append(ptr, n);
    return n;
}

size_t writeHeader(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    const size_t n = size * nmemb;
    std::string line(ptr, n);
    // Header lines look like "Key: value\r\n"; the status line and the blank
    // separator have no colon — skip those.
    const auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        auto trim = [](std::string& s) {
            size_t b = s.find_first_not_of(" \t\r\n");
            size_t e = s.find_last_not_of(" \t\r\n");
            if (b == std::string::npos) { s.clear(); return; }
            s = s.substr(b, e - b + 1);
        };
        trim(key);
        trim(val);
        // Lower-case the key for predictable lookups, matching how the rest of
        // the client treats response headers.
        for (char& c : key) c = static_cast<char>(std::tolower((unsigned char)c));
        if (!key.empty()) (*headers)[key] = val;
    }
    return n;
}

class CurlHttpClient final : public IHttpClient {
public:
    CurlHttpClient() { ensureCurlGlobalInit(); }

    HttpResponse request(const HttpRequest& req) override {
        HttpResponse resp;

        CURL* curl = curl_easy_init();
        if (!curl) {
            resp.error = "curl_easy_init failed";
            return resp;
        }

        curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req.method.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)req.timeoutMs);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);   // thread-safe timeouts
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // allow gzip/deflate

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeHeader);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);

        // Request body (POST/PUT/DELETE with payload). Use COPYPOSTFIELDS so
        // curl owns the buffer; SIZE_LARGE keeps it binary-safe (embedded NULs).
        if (!req.body.empty() || req.method == "POST" || req.method == "PUT") {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                             (curl_off_t)req.body.size());
            curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, req.body.data());
        }

        struct curl_slist* headerList = nullptr;
        for (const auto& kv : req.headers) {
            std::string h = kv.first + ": " + kv.second;
            headerList = curl_slist_append(headerList, h.c_str());
        }
        // CURLOPT_COPYPOSTFIELDS sets Content-Type to form-urlencoded by
        // default; if the caller didn't override it, strip that assumption so
        // JSON bodies aren't mislabeled. Callers set Content-Type explicitly.
        if (req.headers.find("Content-Type") == req.headers.end() &&
            req.headers.find("content-type") == req.headers.end()) {
            headerList = curl_slist_append(headerList, "Content-Type:");
        }
        if (headerList)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);

        if (!req.range.empty())
            curl_easy_setopt(curl, CURLOPT_RANGE, req.range.c_str());

        const CURLcode rc = curl_easy_perform(curl);
        if (rc == CURLE_OK) {
            long status = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            resp.ok = true;
            resp.status = (int)status;
        } else {
            resp.ok = false;
            resp.error = curl_easy_strerror(rc);
        }

        if (headerList) curl_slist_free_all(headerList);
        curl_easy_cleanup(curl);
        return resp;
    }
};

} // namespace

std::unique_ptr<IHttpClient> makeHttpClient() {
    return std::make_unique<CurlHttpClient>();
}

} // namespace platform
