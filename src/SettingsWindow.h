#pragma once
#include "pch.h"
#include "Config.h"
#include "IgdbSync.h"
#include "IgdbClient.h"

class SettingsWindow {
public:
    SettingsWindow() = default;
    ~SettingsWindow() { Close(); }

    // Sidebar page indices — public so callers can request a specific start page.
    static constexpr int PAGE_GENERAL = 0;
    static constexpr int PAGE_STEAM   = 1;
    static constexpr int PAGE_EPIC    = 2;
    static constexpr int PAGE_GOG     = 3;
    static constexpr int PAGE_DOLPHIN = 4;
    static constexpr int PAGE_RYUJINX = 5;
    static constexpr int PAGE_RPCS3   = 6;
    static constexpr int PAGE_N64     = 7;
    static constexpr int PAGE_NES     = 8;
    static constexpr int PAGE_SNES    = 9;
    static constexpr int PAGE_PS1     = 10;
    static constexpr int PAGE_PS2     = 11;
    static constexpr int PAGE_XBOX360 = 12;
    static constexpr int PAGE_XBOX    = 13;
    static constexpr int PAGE_CUSTOM0 = 14;

    void Open(HWND parent, AppConfig& cfg,
              std::function<void()> onSave,
              std::function<void()> onRefreshMeta    = {},
              std::function<void()> onReacquireMeta  = {},
              int startPage = PAGE_GENERAL,
              IgdbClient* igdbClient = nullptr);
    void Close();
    bool IsOpen() const;

private:

    enum CtrlId : int {
        ID_SIDEBAR = 10,
        ID_ADD_LIB = 11,
        ID_SAVE    = 12,
        ID_CANCEL  = 13,
        ID_APPLY   = 14,
        // Per-page generic IDs (only one page visible at a time)
        ID_P_CHK1  = 100, ID_P_CHK2  = 101, ID_P_CHK3 = 102, ID_P_CHK4 = 103,
        ID_P_EDIT1 = 110, ID_P_EDIT2 = 111, ID_P_EDIT3 = 112, ID_P_EDIT4 = 113, ID_P_EDIT5 = 114, ID_P_EDIT6 = 115, ID_P_EDIT7 = 116,
        ID_P_LIST1 = 120,
        ID_P_BTN1  = 130, ID_P_BTN2  = 131, ID_P_BTN3  = 132,
        ID_P_BTN4  = 133, ID_P_BTN5  = 134, ID_P_BTN6  = 136,
        ID_P_STAT1 = 135,   // version status label on emulator pages
        ID_P_STAT2 = 137,   // secondary status label (e.g. DB sync state)
        ID_P_PROG1 = 138,

        // Dolphin emulator-config controls (Dolphin page only). Distinct range
        // so they never collide with the shared per-page generic IDs above.
        ID_D_BACKEND = 300,
        ID_D_RES,
        ID_D_ASPECT,
        ID_D_MSAA,
        ID_D_ANISO,
        ID_D_FULLSCREEN,
        ID_D_VSYNC,
        ID_D_SSAA,
        ID_D_DUALCORE,
        ID_D_CHEATS,
        ID_D_OCEN,
        ID_D_OCPCT,
        ID_D_LANG,
        ID_D_AUDIO,
        ID_D_VOLUME,
        ID_D_DSPHLE,
        ID_D_CTRL_PRESET,
        ID_D_CTRL_WIIMOTE,
        ID_D_CTRL_OPEN,

        // Generic emulator-config controls reused across the other emulator
        // pages (only one page is built at a time, so the meaning is per-page).
        ID_EC_C1 = 340, ID_EC_C2, ID_EC_C3, ID_EC_C4, ID_EC_C5, ID_EC_C6,  // combos
        ID_EC_K1 = 360, ID_EC_K2, ID_EC_K3, ID_EC_K4,
        ID_EC_K5, ID_EC_K6, ID_EC_K7, ID_EC_K8,                            // checks
        ID_EC_E1 = 370, ID_EC_E2, ID_EC_E3, ID_EC_E4,                      // edits
        ID_EC_B1 = 380, ID_EC_B2,                                          // buttons
    };

    // Layout
    static constexpr int SB_W  = 172;   // sidebar width
    static constexpr int CX    = 186;   // content left  (SB_W + 14)
    static constexpr int CW    = 590;   // content width
    static constexpr int WIN_W = 810;   // CX + CW + 34
    static constexpr int WIN_H = 640;
    static constexpr int BOT_Y = WIN_H - 50;  // bottom bar top

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    void CreateChrome(HWND hwnd);
    void RebuildSidebarItems();
    void SwitchPage(int idx);
    void SaveCurrentPage();
    void LoadCurrentPage();

    void BuildGeneralPage();
    void BuildSteamPage();
    void BuildEpicPage();
    void BuildGogPage();
    void BuildDolphinPage();
    void BuildRyujinxPage();
    void BuildRpcs3Page();
    void BuildN64Page();
    void BuildNesPage();
    void BuildSnesPage();
    void BuildPS1Page();
    void BuildPS2Page();
    void BuildXbox360Page();
    void BuildXboxPage();
    void BuildCustomPage(int libIdx);

    void LoadGeneralPage();   void SaveGeneralPage();
    void LoadSteamPage();     void SaveSteamPage();
    void LoadEpicPage();      void SaveEpicPage();
    void LoadGogPage();       void SaveGogPage();
    void LoadDolphinPage();   void SaveDolphinPage();
    void LoadRyujinxPage();   void SaveRyujinxPage();
    void LoadRpcs3Page();     void SaveRpcs3Page();
    void LoadN64Page();       void SaveN64Page();
    void LoadNesPage();       void SaveNesPage();
    void LoadSnesPage();      void SaveSnesPage();
    void LoadPS1Page();       void SavePS1Page();
    void LoadPS2Page();       void SavePS2Page();
    void LoadXbox360Page();   void SaveXbox360Page();
    void LoadXboxPage();      void SaveXboxPage();
    void LoadCustomPage(int); void SaveCustomPage(int);

    void HandlePageCommand(int id);

    // Version check helpers
    void        SetVersionLabel(const std::wstring& installed, const std::wstring& latest);
    void        AddEmulatorProgressBar(int x, int y, int w);
    void        SaveTagForPage(int page, const std::wstring& tag);
    void        SetPathForPage(int page, const std::wstring& exePath);
    std::wstring InstalledTagForPage(int page) const;
    std::string  GithubRepoForPage(int page) const;

    HWND AddPC(HWND h);
    HWND PC(int id) const { return GetDlgItem(m_hwnd, id); }
    void DestroyPageControls();

    void         ListAddPath(HWND lb);
    void         ListRemoveSel(HWND lb);
    void         ListToVec(HWND lb, std::vector<std::wstring>& out) const;
    void         VecToList(HWND lb, const std::vector<std::wstring>& v) const;
    std::wstring BrowseExe(const std::wstring& initial);
    std::wstring BrowseFolder();

    void DrawSidebarItem(DRAWITEMSTRUCT* dis);

    HWND m_hwnd    = nullptr;
    HWND m_parent  = nullptr;
    HWND m_sidebar = nullptr;

    int  m_currentPage = PAGE_GENERAL;
    std::vector<HWND> m_pageControls;

    AppConfig  m_work;
    AppConfig* m_cfg   = nullptr;
    std::function<void()> m_onSave;
    std::function<void()> m_onRefreshMeta;
    std::function<void()> m_onReacquireMeta;

    HBRUSH       m_sidebarBrush = nullptr;
    int          m_startPage    = PAGE_GENERAL;
    IgdbClient*  m_igdbClient   = nullptr;  // not owned

    static constexpr wchar_t WNDCLASS[] = L"ArcadeLauncherSettings2";
};
