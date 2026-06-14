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
#include "DiscordPresence.h"
#include "GamepadInput.h"
#include "RomDatabase.h"
#include "ServerClient.h"
#include "Social/SocialManager.h"

class App {
public:
    App();
    ~App();

    bool Initialize(HINSTANCE hInstance, bool startInTray = false);
    int  Run();

    // Set by wWinMain when invoked as `ArcadeLauncher.exe --launch <id>` (from a
    // game shortcut). The game is launched once the UI is up.
    void SetPendingLaunchId(const std::wstring& id) { m_pendingLaunchId = id; }
    // Launch (or install) a game by its catalog id. No-op if unknown.
    void LaunchById(const std::wstring& id);

    // WM_COPYDATA tag used to forward a `--launch <id>` request from a second
    // instance to the already-running one.
    static constexpr ULONG_PTR kCopyDataLaunchId = 0xA5CADE01;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void OnCreate(HWND hwnd);
    void OnDestroy();
    void OnSize(UINT w, UINT h);
    void OnPaint();
    void SyncSocialRenderState();
    // Friends panel: handle a click on a friend row (opens an action context
    // menu) and the Add-Friend (+) button. Returns true if the panel consumed
    // the click.
    bool HandleFriendsPanelClick(float x, float y);
    void ShowFriendContextMenu(uint64_t accountId);
    void HandleNotifAction(int notifKind, uint64_t accountId); // toast/notif-row click
    void PromptAddFriend();
    // Direct-message chat window. OpenChat loads history + focuses the compose
    // box; HandleChatClick routes the chat window's controls; ChatInputChar
    // feeds typed characters into the compose box while the window is open.
    void OpenChat(uint64_t peerId, const std::wstring& name);
    void CloseChat();
    bool HandleChatClick(float x, float y);
    bool ChatInputChar(wchar_t ch);     // returns true if consumed
    void ChatSendCurrent();
    void ChatPickAndSendAttachment();   // file picker → upload → send (1.3)
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
    // Interactive re-authentication: shows the sign-in modal (reusing the
    // startup login dialog) and refreshes the token in place. Returns true if a
    // valid token was obtained. Must be called on the UI thread. Used by the
    // 401 interceptor so a revoked/expired session can be recovered without
    // restarting the application.
    bool PromptReAuth();
    void LaunchGame(const Game& game);
    void LaunchInstalledGame(Game launchGame);
    void QueueServerInstall(const Game& game, bool autoLaunch);
    // Resolve the install location (library folder) for a server game. Returns
    // false if the user cancelled the picker. outRoot is empty when no library
    // folders are configured (the server default installRoot is then used).
    bool ChooseInstallRoot(const std::wstring& title, std::wstring& outRoot,
                           bool* outDesktopShortcut = nullptr,
                           bool* outStartMenuShortcut = nullptr);
    void DownloadGameInBackground(int visibleIdx);
    void OpenDownloadStatus();
    void RefreshDownloadStatusWindow();
    void StartDownloadWorker();
    void DownloadWorker();
    void ValidateGame(int visibleIdx);
    void UninstallGame(int visibleIdx);
    void MoveGame(int visibleIdx);              // relocate install between libraries
    void SetLaunchOptions(int visibleIdx);      // per-game custom launch args
    void SetLaunchHooks(int visibleIdx);        // per-game pre/post-launch commands
    void SetCloudSaveDir(int visibleIdx);       // per-game cloud-save folder
    void SyncSavesDown(const Game& game);       // pull server-newer saves before launch
    void NewCollectionForGame(int visibleIdx);  // create + assign a collection
    void ToggleGameCollection(int visibleIdx, int collectionIdx);
    void RefreshArt(const Game& game);
    void ApplyFilter();
    // Open the rich detail dashboard for a visible-grid index and kick off the
    // async changelog fetch for server-backed games.
    void OpenDetail(int visibleIdx);
    void RequestChangelogs(const Game& game);
    void ApplySidebarFilter(int idx);
    void ScrollToSelected();
    void UpdateSidebarFlags();
    void OpenSettings(int startPage = 0);
    // Topbar profile button → dropdown (Account settings / Log out) and helpers.
    void ShowProfileMenu();
    void OpenAccountSettings();
    void LogOut();
    // Fetch the current account avatar from the server (off the UI thread) and
    // hand the bytes to the renderer. Safe no-op when no server account.
    void RefreshAvatar();
    void OpenMetadataPicker(const std::wstring& gameId, const std::wstring& gameTitle);
    void OpenEditTitle(int visibleIdx);
    // Consolidated per-game Properties modal (title, platform override, launch
    // options + read-only install/version info).
    void OpenProperties(int visibleIdx);
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
    // Placement captured just before the window is hidden (game launch / tray),
    // so a later restore can re-maximize a window that was maximized rather than
    // forcing it down to a normal-sized SW_RESTORE. Valid only while m_hiddenPlacementValid.
    WINDOWPLACEMENT  m_hiddenPlacement{};
    bool             m_hiddenPlacementValid = false;

    Config           m_config;
    GameLibrary      m_library;
    Renderer         m_renderer;
    ProcessMonitor   m_monitor;
    DiscordPresence  m_discord;
    GamepadInput     m_gamepad;
    social::SocialManager m_social;
    uint64_t m_lastTypingSentMs = 0;   // throttles outbound typing notifications
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

    // ROM dump variant grouping. When enabled, ApplyFilter collapses games that
    // share a Game::VariantKey (same cleaned title + platform — e.g. multiple
    // dumps of one NES game) down to a single representative in m_visibleGames.
    // m_variantGroups maps the representative's id to every variant in its group
    // (including itself, best-pick first) so the UI can show a "N versions" badge
    // and offer a picker. A group of size 1 is never stored.
    bool m_groupVariants = true;
    std::unordered_map<std::wstring, std::vector<const Game*>> m_variantGroups;
    // Returns the variant list for a representative id, or nullptr if it isn't a
    // grouped tile.
    const std::vector<const Game*>* VariantGroupFor(const std::wstring& repId) const {
        auto it = m_variantGroups.find(repId);
        return it == m_variantGroups.end() ? nullptr : &it->second;
    }

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
        std::wstring installRoot; // chosen library folder (empty = server default)
        bool makeDesktopShortcut = false;   // create shortcuts once installed
        bool makeStartMenuShortcut = false;
        std::wstring error;
    };
    std::wstring m_pendingLaunchId;  // --launch <id> from a shortcut, run at startup
    std::wstring m_pendingPostExitCmd;  // post-exit hook of the running game
    // Cloud-save upload after the running game exits (raw server id + folder).
    std::wstring m_pendingSaveSyncGameId;
    std::wstring m_pendingSaveSyncDir;

    // Off-thread box-art decode. WIC decoding is expensive; doing it on the UI
    // thread froze the launcher when many covers loaded at once. A single worker
    // thread decodes queued (id,path) pairs into CPU buffers and posts
    // WM_ART_DECODED so the render thread only does the cheap D2D upload.
    void QueueArtDecode(const std::wstring& gameId, const std::wstring& path);
    void StartArtDecodeWorker();
    void StopArtDecodeWorker();

    // Download + decode a game's IGDB screenshots into the art cache (under the
    // synthetic Game::ScreenshotKey ids) so the detail panel can render a strip.
    void EnsureScreenshots(const Game& game);
    std::unordered_set<std::wstring> m_screenshotsRequested;
    std::thread                                          m_artThread;
    std::mutex                                           m_artQueueMutex;
    std::condition_variable                              m_artQueueCv;
    std::deque<std::pair<std::wstring, std::wstring>>    m_artQueue;
    bool                                                 m_artStop = false;

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
    HWND m_downloadPauseBtn = nullptr;

    static constexpr int IDC_DL_PAUSE  = 9001;
    static constexpr int IDC_DL_CANCEL = 9002;

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
    // Pause/Resume + Cancel Selected buttons in the download status window.
    void DownloadStatusCommand(int id);
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

    static constexpr int  HOTKEY_SUMMON = 1;   // Ctrl+Alt+A global summon

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
    static constexpr UINT WM_CHANGELOGS_READY = WM_USER + 105;
    // Avatar bytes fetched off-thread; lParam = new std::string* (or null = clear)
    static constexpr UINT WM_AVATAR_READY = WM_USER + 106;
    // Box art decoded off-thread; lParam = new Renderer::DecodedImage* (UI thread owns/deletes)
    static constexpr UINT WM_ART_DECODED = WM_USER + 107;

    // Context menu command IDs
    static constexpr UINT IDM_LAUNCH      = 5001;
    static constexpr UINT IDM_MATCH_META  = 5002;
    static constexpr UINT IDM_EDIT_TITLE  = 5003;
    static constexpr UINT IDM_VALIDATE_GAME = 5005;
    static constexpr UINT IDM_UNINSTALL_GAME = 5006;
    static constexpr UINT IDM_DOWNLOAD_GAME = 5007;
    static constexpr UINT IDM_DOWNLOAD_STATUS = 5008;
    static constexpr UINT IDM_MOVE_GAME    = 5009;
    static constexpr UINT IDM_LAUNCH_OPTIONS = 5010;
    static constexpr UINT IDM_PROPERTIES   = 5011;
    static constexpr UINT IDM_FAVORITE     = 5012;
    static constexpr UINT IDM_HIDE_GAME    = 5013;
    static constexpr UINT IDM_LAUNCH_HOOKS = 5014;
    static constexpr UINT IDM_SAVE_DIR     = 5015;
    // Collections submenu: "New collection…" then one id per existing collection.
    static constexpr UINT IDM_COLLECTION_NEW  = 5200;
    static constexpr UINT IDM_COLLECTION_BASE = 5201;  // + collection index
    // "Play version" submenu for grouped ROM-dump tiles: one id per variant.
    static constexpr UINT IDM_VARIANT_BASE    = 5300;  // + variant index

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
    static constexpr UINT IDM_TOOL_LIBRARY = 6011;
    static constexpr UINT IDM_TOOL_UPDATE  = 6012;
    static constexpr UINT IDM_TOOL_SPEEDLIMIT = 6013;

    // Tray menu command IDs
    static constexpr UINT IDM_TRAY_SHOW    = 7001;
    static constexpr UINT IDM_TRAY_SETTINGS = 7002;
    static constexpr UINT IDM_TRAY_ACCOUNT = 7005;
    static constexpr UINT IDM_TRAY_STARTUP = 7003;
    static constexpr UINT IDM_TRAY_EXIT    = 7004;
    static constexpr UINT IDM_TRAY_GAME0   = 7100;  // + 0..4 for 5 recent games

    // Topbar profile dropdown command IDs
    static constexpr UINT IDM_PROFILE_ACCOUNT = 7200;
    static constexpr UINT IDM_PROFILE_LOGOUT  = 7201;
};
