#include "pch.h"
#include "MetadataFetcher.h"

MetadataFetcher::MetadataFetcher(const std::wstring& cacheDir, const std::wstring& sgdbApiKey)
    : m_cacheDir(cacheDir), m_sgdbKey(sgdbApiKey)
{
    CreateDirectoryW(cacheDir.c_str(), nullptr);
    m_worker = std::thread(&MetadataFetcher::WorkerThread, this);
}

MetadataFetcher::~MetadataFetcher() { Shutdown(); }

void MetadataFetcher::Shutdown() {
    m_shutdown.store(true);
    m_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();
}

std::wstring MetadataFetcher::CachedPath(const std::wstring& gameId) const {
    // Replace chars not valid in filenames
    std::wstring safe = gameId;
    for (auto& c : safe)
        if (c == L'/' || c == L'\\' || c == L':' || c == L'*' || c == L'?' ||
            c == L'"' || c == L'<' || c == L'>' || c == L'|') c = L'_';
    std::wstring path = m_cacheDir + L"\\" + safe + L".jpg";
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        return path;
    path = m_cacheDir + L"\\" + safe + L".png";
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        return path;
    return {};
}

void MetadataFetcher::FetchArtAsync(const Game& game,
                                     std::function<void(const std::wstring&)> cb) {
    // Don't re-fetch if already cached
    if (!CachedPath(game.id).empty()) {
        if (cb) cb(CachedPath(game.id));
        return;
    }
    std::lock_guard<std::mutex> lk(m_queueMutex);
    m_queue.push_back({ game, std::move(cb) });
    m_cv.notify_one();
}

void MetadataFetcher::WorkerThread() {
    while (!m_shutdown.load()) {
        WorkItem item;
        {
            std::unique_lock<std::mutex> lk(m_queueMutex);
            m_cv.wait(lk, [this]{ return !m_queue.empty() || m_shutdown.load(); });
            if (m_shutdown.load()) break;
            item = std::move(m_queue.back());
            m_queue.pop_back();
        }

        std::wstring safe = item.game.id;
        for (auto& c : safe)
            if (c == L'/' || c == L'\\' || c == L':' || c == L'*' || c == L'?' ||
                c == L'"' || c == L'<' || c == L'>' || c == L'|') c = L'_';

        std::wstring dest = m_cacheDir + L"\\" + safe + L".jpg";

        // Try provided URL first
        bool ok = false;
        if (!item.game.coverArtUrl.empty())
            ok = DownloadFile(item.game.coverArtUrl, dest);

        // Steam CDN fallback
        if (!ok && !item.game.steamAppId.empty())
            ok = DownloadFile(SteamArtUrl(item.game.steamAppId), dest);

        // SteamGridDB fallback (requires API key)
        if (!ok && !m_sgdbKey.empty())
            ok = DownloadFile(SgdbArtUrl(item.game.title), dest);

        if (ok && item.callback)
            item.callback(dest);
    }
}

bool MetadataFetcher::DownloadFile(const std::wstring& url, const std::wstring& dest) {
    // Parse URL
    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[512]{}, path[2048]{};
    uc.lpszHostName    = host; uc.dwHostNameLength    = 512;
    uc.lpszUrlPath     = path; uc.dwUrlPathLength     = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    HINTERNET hSession = WinHttpOpen(L"GameLauncher/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConn = WinHttpConnect(hSession, host, uc.nPort, 0);
    if (!hConn) { WinHttpCloseHandle(hSession); return false; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path, nullptr,
                                         WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    bool ok = false;
    if (hReq && WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
             && WinHttpReceiveResponse(hReq, nullptr)) {
        // Check status
        DWORD status = 0; DWORD sz = sizeof(status);
        WinHttpQueryHeaders(hReq,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            nullptr, &status, &sz, nullptr);

        if (status == 200) {
            std::vector<BYTE> data;
            DWORD read = 0;
            BYTE buf[4096];
            while (WinHttpReadData(hReq, buf, sizeof(buf), &read) && read > 0)
                data.insert(data.end(), buf, buf + read);

            if (!data.empty()) {
                HANDLE hFile = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr,
                                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile != INVALID_HANDLE_VALUE) {
                    DWORD written;
                    WriteFile(hFile, data.data(), (DWORD)data.size(), &written, nullptr);
                    CloseHandle(hFile);
                    ok = true;
                }
            }
        }
        WinHttpCloseHandle(hReq);
    }
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSession);
    return ok;
}

std::wstring MetadataFetcher::SteamArtUrl(const std::wstring& appId) const {
    return L"https://cdn.steamstatic.com/steam/apps/" + appId + L"/library_600x900.jpg";
}

std::wstring MetadataFetcher::SgdbArtUrl(const std::wstring& title) const {
    // SteamGridDB v2 search — returns poster art
    // Real impl needs a proper API call; this is a placeholder showing the URL pattern
    (void)title;
    return {};
}
