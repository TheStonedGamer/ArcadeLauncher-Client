#pragma once
#include "pch.h"
#include "GameLibrary.h"
#include "Config.h"
#include "Renderer.h"
#include "MetadataFetcher.h"
#include "ProcessMonitor.h"
#include "IgdbClient.h"
#include "MetadataManager.h"
#include "PlatformIcons.h"
#include "SettingsWindow.h"
#include "MetadataPickerDialog.h"
#include "GameEditDialog.h"
#include "FirstLaunchSetup.h"
#include "AppUpdater.h"
#include "RomDatabase.h"
#include "ServerClient.h"

class App {
public:
    App();
    ~App();

    bool Initialize(HINSTANCE hInstance, bool startInTray = false);
    int  Run();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void OnCreate(HWND hwnd);
    void OnDestroy();
    void OnSize(UINT w, UINT h);
    void OnPaint();
    void OnTimer(UINT timerId);
    void OnMouseMove(float x, float y);
    void OnLButtonDown(float x, float y);
    void OnLButtonDblClk(float x, float y);
    void OnLButtonUp(float x, float y);
    void OnRButtonDown(float x, float y);
    void OnChar(wchar_t ch);
    void OnKeyDown(WPARAM vk);
    void OnScroll(float delta);

    void ScanAllPlatforms();
    // Ensure m_config holds a valid, machine-bound server token, logging in
    // (once) only when there is no token or the machine fingerprint changed.
    // Thread-safe; cheap no-op after the first successful login.
    bool EnsureServerToken(std::wstring& error);
    // Drop the cached token so the next EnsureServerToken re-authenticates
    // (called when a server request is rejected with 401).
    void InvalidateServerToken();
    void LaunchGame(const Game& game);
    void LaunchInstalledGame(Game launchGame);
    void QueueServerInstall(const Game& game, bool autoLaunch);
    void DownloadGameInBackground(int visibleIdx);
    void OpenDownloadStatus();
    void RefreshDownloadStatusWindow();
    void StartDownloadWorker();
    void DownloadWorker();
    void ValidateGame(int visibleIdx);
    void UninstallGame(int visibleIdx);
    void RefreshArt(const Game& game);
    void ApplyFilter();
    void ApplySidebarFilter(int idx);
    void ScrollToSelected();
    void UpdateSidebarFlags();
    void OpenSettings(int startPage = 0);
    void OpenMetadataPicker(const std::wstring& gameId, const std::wstring& gameTitle);
    void OpenEditTitle(int visibleIdx);
    void ShowMenuBar();

    void SaveAll();
    void LoadAll();

    // ── Tray icon ──────────────────────────────────────────────────────────────
    void CreateTrayIcon();
    void RemoveTrayIcon();
    // Steam-style toast via the tray icon's balloon notification. Safe to call
    // even when the main window is hidden/minimized to tray.
    void ShowToast(const std::wstring& title, const std::wstring& message);
    void ShowTrayMenu();
    bool IsStartupEnabled() const;
    void SetStartup(bool enable);
    void ShowWindow_(bool show);  // show/restore or hide

    HWND             m_hwnd = nullptr;
    HINSTANCE        m_hInst = nullptr;
    bool             m_fullscreen = false;
    WINDOWPLACEMENT  m_savedPlacement{};

    Config           m_config;
    GameLibrary      m_library;
    Renderer         m_renderer;
    ProcessMonitor   m_monitor;
    std::unique_ptr<MetadataFetcher>  m_fetcher;

    IgdbClient                        m_igdbClient;
    std::unique_ptr<MetadataManager>  m_metaManager;
    PlatformIcons                     m_platformIcons;
    SettingsWindow                    m_settings;
    EmulatorSetupWindow               m_setup;
    MetadataPickerDialog              m_picker;

    RomDatabase      m_romDb;
    RenderState      m_renderState;
    std::vector<const Game*> m_visibleGames;

    struct DownloadJob {
        Game game;
        std::wstring gameId;
        std::wstring title;
        uint64_t done = 0;
        uint64_t total = 0;
        bool running = false;
        bool complete = false;
        bool failed = false;
        bool autoLaunch = false;  // launch the game when this install completes
        std::wstring error;
    };
    std::mutex m_downloadMutex;
    std::mutex m_authMutex;  // serializes server token refresh
    std::deque<DownloadJob> m_downloadQueue;
    bool m_downloadWorkerRunning = false;
    HWND m_downloadStatusWnd = nullptr;
    HWND m_downloadList = nullptr;
    HWND m_downloadProgress = nullptr;
    HWND m_downloadSummary = nullptr;
    HWND m_downloadSpeed = nullptr;   // current / disk-write / peak readout
    HWND m_downloadGraph = nullptr;   // owner-drawn Steam-style speed graph

    // Throughput sampling for the download status window. All accessed on the
    // UI thread only (timer, progress messages, paint), so no extra locking.
    uint64_t  m_speedLastBytes = 0;
    ULONGLONG m_speedLastTick  = 0;
    double    m_curSpeedBps    = 0.0;   // current throughput (bytes/sec)
    double    m_peakSpeedBps   = 0.0;
    std::deque<float> m_speedHistory;   // recent throughput samples (bytes/sec)
    void SampleDownloadSpeed();

public:
    // Driven by the download status window's 500ms timer + its graph child.
    void TickDownloadStatus();
    size_t SpeedHistorySize() const;
    std::vector<float> SpeedHistoryCopy() const;
private:

    bool             m_dragging = false;
    float            m_lastMouseX = 0, m_lastMouseY = 0;

    // Auto-hidden menu bar
    bool             m_menuActive = false;

    // Tray state
    NOTIFYICONDATAW          m_nid{};
    std::vector<std::wstring> m_trayRecentIds;  // game IDs shown in last tray menu

    static constexpr UINT TIMER_ANIM   = 1;
    static constexpr UINT TIMER_SCROLL = 2;
    static constexpr UINT TIMER_SAVE   = 3;
    static constexpr UINT TIMER_RESYNC = 4;

    // Periodic / focus-driven server re-sync
    void TriggerResync();
    std::atomic<bool> m_resyncRunning{ false };
    ULONGLONG m_lastResyncTick = 0;

    // Window messages
    static constexpr UINT WM_TRAYICON    = WM_USER + 100;
    static constexpr UINT WM_GAME_CLOSED = WM_USER + 101;
    static constexpr UINT WM_ROMDB_READY = WM_USER + 102;
    static constexpr UINT WM_SERVER_INSTALL_DONE = WM_USER + 103;
    static constexpr UINT WM_SERVER_INSTALL_PROGRESS = WM_USER + 104;

    // Context menu command IDs
    static constexpr UINT IDM_LAUNCH      = 5001;
    static constexpr UINT IDM_MATCH_META  = 5002;
    static constexpr UINT IDM_EDIT_TITLE  = 5003;
    static constexpr UINT IDM_VALIDATE_GAME = 5005;
    static constexpr UINT IDM_UNINSTALL_GAME = 5006;
    static constexpr UINT IDM_DOWNLOAD_GAME = 5007;
    static constexpr UINT IDM_DOWNLOAD_STATUS = 5008;

    // Tools menu command IDs
    static constexpr UINT IDM_TOOL_DOLPHIN = 6001;
    static constexpr UINT IDM_TOOL_RYUJINX = 6002;
    static constexpr UINT IDM_TOOL_RPCS3   = 6003;
    static constexpr UINT IDM_TOOL_N64     = 6004;
    static constexpr UINT IDM_TOOL_NES     = 6005;
    static constexpr UINT IDM_TOOL_SNES    = 6006;
    static constexpr UINT IDM_TOOL_PS1     = 6007;
    static constexpr UINT IDM_TOOL_PS2     = 6008;
    static constexpr UINT IDM_TOOL_XBOX360 = 6009;
    static constexpr UINT IDM_TOOL_XBOX    = 6010;

    // Tray menu command IDs
    static constexpr UINT IDM_TRAY_SHOW    = 7001;
    static constexpr UINT IDM_TRAY_SETTINGS = 7002;
    static constexpr UINT IDM_TRAY_ACCOUNT = 7005;
    static constexpr UINT IDM_TRAY_STARTUP = 7003;
    static constexpr UINT IDM_TRAY_EXIT    = 7004;
    static constexpr UINT IDM_TRAY_GAME0   = 7100;  // + 0..4 for 5 recent games
};
