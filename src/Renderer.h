#pragma once
#include "pch.h"
#include "GameLibrary.h"
#include "PlatformIcons.h"
#include "Config.h"
#include <unordered_set>

enum class FocusArea { Grid, Sidebar, Search };
enum class LibraryPage { All, Installed, ReadyToDownload, BackgroundDownloads, Updates, Platform, Collection, Favorites, RecentlyPlayed, Hidden };

// Grid ordering applied by App::ApplyFilter. Recent is the historical default
// (last played, then title). Cycled via the topbar sort button.
enum class SortMode { Recent, Title, Platform, Rating, Playtime };

inline const wchar_t* SortModeName(SortMode m) {
    switch (m) {
    case SortMode::Recent:   return L"Recent";
    case SortMode::Title:    return L"Title (A-Z)";
    case SortMode::Platform: return L"Platform";
    case SortMode::Rating:   return L"Rating";
    case SortMode::Playtime: return L"Playtime";
    default:                 return L"Recent";
    }
}

// Segoe MDL2 Assets glyph for the current sort mode, so the topbar button
// reflects the active ordering at a glance (replaces the old sort toast).
inline const wchar_t* SortModeIcon(SortMode m) {
    switch (m) {
    case SortMode::Recent:   return L"\xE81C"; // History
    case SortMode::Title:    return L"\xE8CB"; // Sort (A-Z)
    case SortMode::Platform: return L"\xE71D"; // AllApps
    case SortMode::Rating:   return L"\xE735"; // FavoriteStarFill
    case SortMode::Playtime: return L"\xE916"; // Stopwatch
    default:                 return L"\xE81C";
    }
}

// A single changelog entry for display in the detail dashboard. Mirrors
// ServerClient::ChangelogEntry but keeps Renderer free of ServerClient deps.
struct ChangelogView {
    std::wstring version;
    std::wstring title;
    std::wstring body;
    int64_t      createdAt = 0;
};

struct RenderState {
    int   hoveredIndex  = -1;
    int   selectedIndex = -1;
    float scrollOffset  = 0.0f;
    float targetScroll  = 0.0f;
    bool  detailOpen    = false;
    int   detailIndex   = -1;

    // Changelogs for the detail dashboard, fetched async from the server when a
    // server-backed game's detail panel opens. Keyed to detailChangelogGameId
    // (the local Game::id) so a late-arriving fetch for a closed/other game is
    // ignored.
    std::vector<ChangelogView> detailChangelogs;
    std::wstring detailChangelogGameId;
    bool detailChangelogsLoading = false;
    float detailScroll = 0.0f;   // dashboard right-pane scroll (summary+changelogs)
    std::wstring searchQuery;
    Platform filterPlatform = Platform::Repacks;
    bool filterAll = true;
    LibraryPage libraryPage = LibraryPage::All;
    SortMode sortMode = SortMode::Recent;
    std::wstring filterCollection;             // active when libraryPage==Collection
    std::vector<std::wstring> collections;     // user collections shown in sidebar
    bool anyHidden = false;                    // any games hidden -> show the Hidden tab
    FocusArea focusArea     = FocusArea::Grid;
    int sidebarFocusIdx     = 0;
    bool selectionMode      = false;
    std::unordered_set<std::wstring> selectedGameIds;
    float sidebarScroll     = 0.0f;

    // Which platform entries appear in the main sidebar (driven by enabled flags in config)
    bool showSteam   = true;
    bool showEpic    = true;
    bool showGog     = true;
    bool showDolphin = true;
    bool showGameCube = true;
    bool showWii     = true;
    bool showRyujinx = true;
    bool showRPCS3   = true;
    bool showN64     = true;
    bool showNES     = true;
    bool showSNES    = true;
    bool showPS1     = true;
    bool showPS2     = true;
    bool showXbox360 = true;
    bool showXbox    = true;
    bool showRepacks = true;

    bool metaScanning = false;

    // Number of active (running or queued) background downloads, shown as a
    // badge on the Downloads topbar button.
    int activeDownloadCount = 0;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    bool Initialize(HWND hwnd);
    void Resize(UINT width, UINT height);
    void Render(const std::vector<const Game*>& games, RenderState& state);
    bool LoadGameArt(const std::wstring& gameId, const std::wstring& path);
    void UnloadGameArt(const std::wstring& gameId);

    // CPU pixel buffer produced by an off-thread decode. The expensive WIC decode
    // is done on a worker thread (DecodeImageFile); the render thread then turns
    // the buffer into a D2D bitmap cheaply via StoreDecodedArt — so loading box
    // art never blocks the UI.
    struct DecodedImage {
        std::wstring        gameId;
        UINT                width = 0;
        UINT                height = 0;
        std::vector<BYTE>   pixels;   // 32bpp PBGRA, stride = width*4
    };
    // Decode an image file to a CPU BGRA buffer. Safe to call off the render
    // thread; the calling thread must have COM initialized. Returns false on error.
    static bool DecodeImageFile(const std::wstring& path, DecodedImage& out);
    // Upload an already-decoded buffer as the game's art bitmap (render thread only).
    bool StoreDecodedArt(const DecodedImage& img);

    // Call after Initialize() to load and attach platform icons.
    void LoadPlatformIcons(PlatformIcons& icons, const EmulatorConfig& emuCfg);

    ID2D1RenderTarget*  GetRT()  const { return m_rt.Get(); }
    IWICImagingFactory* GetWIC() const { return m_wic.Get(); }

    // Returns game index at screen position, or -1.
    int HitTestGrid(float x, float y, const RenderState& state,
                    size_t gameCount) const;

    // Sidebar hit test: returns platform or filterAll flag.
    bool HitTestSidebar(float x, float y, const RenderState& state,
                        Platform& outPlatform, bool& outAll,
                        LibraryPage& outPage, std::wstring& outCollection) const;
    bool HitTestSearch(float x, float y) const;
    bool HitTestLaunchBtn(float x, float y) const;
    bool HitTestSettingsBtn(float x, float y) const;
    bool HitTestSelectModeBtn(float x, float y) const;
    bool HitTestDownloadsBtn(float x, float y) const;
    bool HitTestSortBtn(float x, float y) const;
    bool HitTestProfileBtn(float x, float y) const;
    bool HitTestEmptyStateBtn(float x, float y) const;

    // Profile picture (server account avatar) shown in the topbar profile button.
    // Decoded from in-memory image bytes on the render-target thread. Pass empty
    // to clear (falls back to a generic person glyph).
    void SetAvatarFromMemory(const void* data, size_t size);
    void ClearAvatar();
    bool HasAvatar() const { return m_avatar != nullptr; }

    // Returns the recommended targetScroll so that game at idx is fully visible.
    float ScrollForSelected(int idx, float currentScroll, float viewportH) const;

    int GetCols() const { return m_cols; }
    float GridRowHeight() const { return m_tileH + m_tileGap + 22.0f; }

    struct SidebarEntry {
        std::wstring label; bool all; Platform p; LibraryPage page;
        std::wstring collection;  // set only for LibraryPage::Collection entries
    };
    static std::vector<SidebarEntry> BuildSidebarEntries(const RenderState& s);
    static int GetSidebarEntryCount(const RenderState& s) {
        return (int)BuildSidebarEntries(s).size();
    }
    float MaxSidebarScroll(const RenderState& s) const;
    float SidebarWidth() const { return m_sidebarW; }

private:
    void DrawBackground();
    void DrawTopBar(const RenderState& state);
    void DrawSidebar(const RenderState& state);
    void DrawGrid(const std::vector<const Game*>& games, RenderState& state);
    void DrawCard(const Game& game, D2D1_RECT_F rect, bool hovered, bool selected,
                  bool selectionMode, bool multiSelected);
    void DrawDetailPanel(const Game* game, RenderState& state);
    // Draws a word-wrapped block at (x,y) constrained to width w and returns its
    // rendered height. Pass draw=false to measure without drawing.
    float DrawWrapped(const std::wstring& text, IDWriteTextFormat* fmt,
                      float x, float y, float w, ID2D1Brush* brush, bool draw = true);
    void DrawPlatformBadge(Platform p, D2D1_POINT_2F center);
    void DrawPlaceholderArt(D2D1_RECT_F rect, Platform p);
    // Draws a bitmap centered inside `box`, preserving aspect ratio (so wordmark
    // logos aren't squished into a square) and using high-quality cubic scaling
    // when available. `opacity` 0..1.
    void DrawIconFit(ID2D1Bitmap* bmp, D2D1_RECT_F box, float opacity);

    ID2D1Bitmap* GetArt(const std::wstring& id) const;
    ComPtr<ID2D1Bitmap> LoadBitmapFromFile(const std::wstring& path);
    void CreateBrushes();

    HWND   m_hwnd = nullptr;
    UINT   m_width = 0, m_height = 0;

    ComPtr<ID2D1Factory>          m_factory;
    ComPtr<ID2D1HwndRenderTarget> m_rt;
    ComPtr<ID2D1DeviceContext>    m_dc;  // QI of m_rt; enables high-quality cubic bitmap scaling
    ComPtr<IDWriteFactory>        m_dwFactory;
    ComPtr<IWICImagingFactory>    m_wic;

    // Brushes
    ComPtr<ID2D1SolidColorBrush> m_brushBg;
    ComPtr<ID2D1SolidColorBrush> m_brushSidebar;
    ComPtr<ID2D1SolidColorBrush> m_brushTopbar;
    ComPtr<ID2D1SolidColorBrush> m_brushCard;
    ComPtr<ID2D1SolidColorBrush> m_brushCardHover;
    ComPtr<ID2D1SolidColorBrush> m_brushAccent;
    ComPtr<ID2D1SolidColorBrush> m_brushText;
    ComPtr<ID2D1SolidColorBrush> m_brushSubtext;
    ComPtr<ID2D1SolidColorBrush> m_brushWhite;
    ComPtr<ID2D1SolidColorBrush> m_brushOverlay;
    ComPtr<ID2D1SolidColorBrush> m_brushSelected;

    // Text formats
    ComPtr<IDWriteTextFormat> m_fmtTitle;
    ComPtr<IDWriteTextFormat> m_fmtSmall;
    ComPtr<IDWriteTextFormat> m_fmtCard;
    ComPtr<IDWriteTextFormat> m_fmtCardSub;
    ComPtr<IDWriteTextFormat> m_fmtHeading;
    ComPtr<IDWriteTextFormat> m_fmtSearch;
    ComPtr<IDWriteTextFormat> m_fmtSidebar;
    ComPtr<IDWriteTextFormat> m_fmtDetail;
    ComPtr<IDWriteTextFormat> m_fmtSummary;  // wrapping small text for detail panel
    ComPtr<IDWriteTextFormat> m_fmtIcon;     // Segoe MDL2 Assets for toolbar icons

    // Platform icons (not owned — pointer set by App after renderer init)
    PlatformIcons* m_platformIcons = nullptr;

    // Cached game art bitmaps keyed by game id
    mutable std::mutex m_artMutex;
    std::unordered_map<std::wstring, ComPtr<ID2D1Bitmap>> m_artCache;

    // Layout constants (computed in Resize)
    int   m_lastGameCount = 0;
    float m_animTime = 0.0f;
    float m_sidebarW = 200.0f;
    float m_topbarH  = 64.0f;
    float m_tileW    = 180.0f;
    float m_tileH    = 260.0f;
    float m_tileGap  = 16.0f;
    int   m_cols     = 5;

    D2D1_RECT_F m_searchRect{};
    D2D1_RECT_F m_launchBtnRect{};
    D2D1_RECT_F m_settingsBtnRect{};
    D2D1_RECT_F m_selectModeBtnRect{};
    D2D1_RECT_F m_downloadsBtnRect{};
    D2D1_RECT_F m_sortBtnRect{};
    D2D1_RECT_F m_profileBtnRect{};
    D2D1_RECT_F m_emptyStateBtnRect{};  // non-zero only when the empty-state button is visible

    // Current user's profile picture (circular avatar). Null = show person glyph.
    ComPtr<ID2D1Bitmap> m_avatar;
};
