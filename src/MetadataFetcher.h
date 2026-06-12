#pragma once
#include "GameLibrary.h"

// Downloads and caches game cover art.
// Steam games: uses Steam CDN (no API key needed).
// Others: uses SteamGridDB API if key is configured.
class MetadataFetcher {
public:
    explicit MetadataFetcher(const std::wstring& cacheDir, const std::wstring& sgdbApiKey = {});
    ~MetadataFetcher();

    // Async: fetches art for game, writes to cacheDir/{id}.jpg, calls cb on completion.
    void FetchArtAsync(const Game& game, std::function<void(const std::wstring& localPath)> cb);

    // Returns local cached path if it exists, else empty.
    std::wstring CachedPath(const std::wstring& gameId) const;

    // Synchronously download an arbitrary image URL to `dest` (used for the
    // detail-panel screenshot gallery). Safe to call from a worker thread.
    bool DownloadImage(const std::wstring& url, const std::wstring& dest) {
        return DownloadFile(url, dest);
    }

    void Shutdown();

private:
    struct WorkItem {
        Game game;
        std::function<void(const std::wstring&)> callback;
    };

    void WorkerThread();
    bool DownloadFile(const std::wstring& url, const std::wstring& dest);
    std::wstring SteamArtUrl(const std::wstring& appId) const;
    std::wstring SgdbArtUrl(const std::wstring& title) const;

    std::wstring m_cacheDir;
    std::wstring m_sgdbKey;

    std::vector<WorkItem>   m_queue;
    std::mutex              m_queueMutex;
    std::condition_variable m_cv;
    std::thread             m_worker;
    std::atomic<bool>       m_shutdown{ false };
};
