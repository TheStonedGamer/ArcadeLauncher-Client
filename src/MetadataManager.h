#pragma once
#include "pch.h"
#include "GameLibrary.h"
#include "IgdbClient.h"

// Enriches the game library with IGDB metadata.
// Runs asynchronously; calls onProgress after each game is updated.
class MetadataManager {
public:
    using ProgressCallback = std::function<void(const std::wstring& gameId,
                                                bool matched)>;

    MetadataManager(GameLibrary& library, IgdbClient& client,
                    const std::wstring& artCacheDir);
    ~MetadataManager();

    // Queue all games that have no IGDB match yet.
    void ScanAllAsync(ProgressCallback cb);

    // Clear all existing IGDB matches and re-queue every game (nuclear re-fetch).
    void ForceRescanAllAsync(ProgressCallback cb);

    // Re-scan one specific game (e.g. after manual override).
    void ScanGameAsync(const std::wstring& gameId, ProgressCallback cb);

    void Shutdown();
    bool IsRunning() const { return m_running.load(); }

    // Manual override: bind a game to a specific IGDB id.
    bool MatchManual(const std::wstring& gameId, int64_t igdbId, ProgressCallback cb);

    // Returns candidates for a game title (for a picker UI).
    std::vector<IgdbGame> GetCandidates(const std::wstring& title, int limit = 8);

    // Title normalization used for matching (exposed for testing/display).
    static std::wstring Normalize(const std::wstring& title);

    // Simple similarity score [0,1] between two normalized titles.
    static float Similarity(const std::wstring& a, const std::wstring& b);

private:
    struct WorkItem {
        std::wstring      gameId;
        int64_t           forceIgdbId = 0; // 0 = auto-match
        ProgressCallback  callback;
    };

    void WorkerThread();
    void ProcessItem(const WorkItem& item);
    bool DownloadAndCacheArt(Game& game, const IgdbGame& meta);

    GameLibrary&   m_library;
    IgdbClient&    m_client;
    std::wstring   m_artCacheDir;

    std::vector<WorkItem>    m_queue;
    std::mutex               m_mutex;
    std::condition_variable  m_cv;
    std::thread              m_worker;
    std::atomic<bool>        m_running{ false };
    std::atomic<bool>        m_shutdown{ false };
};
