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
    void LaunchGame(const Game& game);
    void DownloadGameInBackground(int visibleIdx);
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
    void DeleteRom(int visibleIdx);
    void ShowMenuBar();

    void SaveAll();
    void LoadAll();

    // ── Tray icon ──────────────────────────────────────────────────────────────
    void CreateTrayIcon();
    void RemoveTrayIcon();
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

    // Window messages
    static constexpr UINT WM_TRAYICON    = WM_USER + 100;
    static constexpr UINT WM_GAME_CLOSED = WM_USER + 101;
    static constexpr UINT WM_ROMDB_READY = WM_USER + 102;
    static constexpr UINT WM_SERVER_INSTALL_DONE = WM_USER + 103;

    // Context menu command IDs
    static constexpr UINT IDM_LAUNCH      = 5001;
    static constexpr UINT IDM_MATCH_META  = 5002;
    static constexpr UINT IDM_EDIT_TITLE  = 5003;
    static constexpr UINT IDM_DELETE_ROM  = 5004;
    static constexpr UINT IDM_VALIDATE_GAME = 5005;
    static constexpr UINT IDM_UNINSTALL_GAME = 5006;
    static constexpr UINT IDM_DOWNLOAD_GAME = 5007;

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
    static constexpr UINT IDM_TRAY_STARTUP = 7003;
    static constexpr UINT IDM_TRAY_EXIT    = 7004;
    static constexpr UINT IDM_TRAY_GAME0   = 7100;  // + 0..4 for 5 recent games
};
