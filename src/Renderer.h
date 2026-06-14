#pragma once
#include "pch.h"
#include "GameLibrary.h"
#include "PlatformIcons.h"
#include "Config.h"
#include <unordered_set>

enum class FocusArea { Grid, Sidebar, Search, FriendsSearch };
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

// Lightweight, render-only view of a friend. App fills these from SocialManager
// each frame so the Renderer stays free of Social/ServerClient dependencies.
// presence/relation are ints mirroring social::PresenceState / FriendStatus to
// avoid pulling those headers into the renderer.
struct FriendRowView {
    uint64_t     accountId = 0;
    std::wstring username;
    int          presence = 0;   // 0 Offline,1 Online,2 Away,3 Busy,4 Invisible,5 InGame
    int          relation = 0;   // 0 None,1 RequestSent,2 RequestReceived,3 Accepted,4 Blocked
    std::wstring gameTitle;
    int          unread = 0;
    bool         favorite = false;
    std::wstring nickname;          // overrides username for display when set
    int64_t      lastInteract = 0;  // epoch secs — for "recently interacted" sort
};

// One notification row for the history dropdown (render-only view).
struct NotifRowView {
    int          kind = 6;     // mirrors social::NotifKind int
    uint64_t     accountId = 0;
    std::wstring title;
    std::wstring body;
    int64_t      ts = 0;       // epoch ms
    bool         read = false;
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

    // ── Social (friends panel) view state ────────────────────────────────────
    bool showFriendsPanel = false;
    int  gatewayState     = 0;   // 0 disconnected,1 connecting,2 connected,3 reconnecting
    int  socialUnread     = 0;   // total unread DMs (topbar badge)
    int  pendingRequests  = 0;   // incoming friend requests (topbar badge)
    float friendsScroll   = 0.0f;
    int  hoveredFriendId  = -1;  // accountId under the cursor, or -1
    std::vector<FriendRowView> friends;
    std::wstring friendsFilter;        // inline search/filter text
    int      friendsSortMode = 0;      // 0 = A–Z, 1 = Recently interacted
    uint32_t friendsCollapsedMask = 0; // bit g set => group g collapsed

    // ── Direct-message chat window ────────────────────────────────────────────
    // A single floating conversation window. App fills it each frame from the
    // SocialManager conversation for chatPeerId while chatOpen is true.
    struct ChatMsgView {
        bool         mine = false;
        std::wstring text;
        int64_t      ts = 0;        // epoch seconds
        bool         pending = false;
        bool         edited  = false;  // appended "(edited)" tag (1.2a)
        bool         deleted = false;  // render as italic "message deleted" (1.2a)
        bool         read    = false;  // my message has been read by the peer (1.2a)
        uint64_t     msgId   = 0;      // server message id (0 = pending/local)
        uint64_t     attachmentId = 0; // linked attachment (1.3), 0 = none
        std::wstring attachmentName;   // original filename, for the chip label
        std::string  attachmentContentType;  // drives image/video/file rendering
        // Reactions surfaced from the model (1.2b): emoji + count + did-I-react.
        struct Reaction { std::wstring emoji; int count = 0; bool mine = false; };
        std::vector<Reaction> reactions;
    };
    bool         chatOpen = false;
    uint64_t     chatPeerId = 0;
    std::wstring chatPeerName;
    int          chatPeerPresence = 0;  // mirrors PresenceState int
    std::wstring chatInput;             // current compose-box text
    float        chatScroll = 0.0f;     // message list scroll (px from bottom-anchored top)
    bool         chatPeerTyping = false;
    std::vector<ChatMsgView> chatMessages;
    uint64_t     chatEditingMsgId = 0;  // non-zero while editing that message in place
    float        mouseX = -1.0f, mouseY = -1.0f;  // cursor for chat row/toolbar hover

    // Voice call state surfaced to the chat window's call controls.
    int      voiceState = 0;   // 0 idle,1 connecting,2 negotiating(ringing),3 connected,...
    uint64_t voicePeer  = 0;
    bool     voiceMuted = false;

    // ── Notifications history dropdown (bell button in the topbar) ────────────
    bool notifOpen   = false;
    int  notifUnread = 0;
    std::vector<NotifRowView> notifs;  // newest first; filled by App when open
    float notifScroll = 0.0f;
    bool notifSoundOn = true;          // mirror of SocialManager prefs (for toggles)
    bool notifOnlineAlerts = true;
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
        uint64_t            attachmentId = 0;   // non-zero => a DM attachment (1.3)
        UINT                width = 0;
        UINT                height = 0;
        std::vector<BYTE>   pixels;   // 32bpp PBGRA, stride = width*4
    };
    // Decode an image file to a CPU BGRA buffer. Safe to call off the render
    // thread; the calling thread must have COM initialized. Returns false on error.
    static bool DecodeImageFile(const std::wstring& path, DecodedImage& out);
    // Decode an in-memory encoded image (PNG/JPG/GIF/BMP/…) to a CPU BGRA buffer.
    // Same threading rules as DecodeImageFile. Used for DM attachment previews.
    static bool DecodeImageMemory(const BYTE* data, size_t size, DecodedImage& out);
    // Upload an already-decoded buffer as the game's art bitmap (render thread only).
    bool StoreDecodedArt(const DecodedImage& img);
    // Upload a decoded attachment image, cached by attachment id (render thread).
    bool StoreDecodedAttachmentImage(const DecodedImage& img);
    // Cached attachment bitmap, or nullptr if not decoded yet.
    ID2D1Bitmap* GetAttachmentImage(uint64_t attachmentId) const;

    // Call after Initialize() to load and attach platform icons.
    void LoadPlatformIcons(PlatformIcons& icons, const EmulatorConfig& emuCfg);

    ID2D1RenderTarget*  GetRT()  const { return m_rt.Get(); }
    IWICImagingFactory* GetWIC() const { return m_wic.Get(); }

    // Returns game index at screen position, or -1.
    int HitTestGrid(float x, float y, const RenderState& state,
                    size_t gameCount) const;

    // Returns the game index whose hover-only "⋯" overflow button contains the
    // point, or -1. Only succeeds for the currently hovered card (the button is
    // only drawn there). Geometry mirrors DrawCard.
    int HitTestCardMenuButton(float x, float y, const RenderState& state,
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
    bool HitTestFriendsBtn(float x, float y) const;

    // Friends panel hit testing. Returns what was clicked; for a friend row,
    // outAccountId is set. Geometry comes from rects cached during the last draw.
    struct FriendsHit { enum Kind { None, Row, AddFriend, AcceptRequest, DeclineRequest,
                                    Search, SortToggle, GroupHeader } kind = None;
                        uint64_t accountId = 0; int groupIndex = -1; };
    FriendsHit HitTestFriendsPanel(float x, float y) const;
    bool       PointInFriendsPanel(float x, float y) const;
    float      FriendsPanelWidth() const { return m_friendsPanelW; }
    float      MaxFriendsScroll(const RenderState& s) const;
    bool HitTestEmptyStateBtn(float x, float y) const;

    // Chat window hit testing. Geometry comes from rects cached during the last
    // DrawChatWindow. Kind tells App which control was clicked.
    struct ChatHit {
        enum Kind { None, Close, Input, Send, Call, Mute, EndCall, Accept, Decline,
                    Attach, Attachment,
                    React, Edit, Delete, Copy, Reaction } kind = None;
        uint64_t attachmentId = 0;   // set when kind == Attachment
        uint64_t msgId = 0;          // set for React/Edit/Delete/Copy/Reaction
        std::wstring emoji;          // set when kind == Reaction (toggle target)
    };
    ChatHit HitTestChatWindow(float x, float y) const;
    bool    PointInChatWindow(float x, float y) const;
    float   MaxChatScroll(const RenderState& s) const;

    // ── Toast notifications (bottom-right, animated, auto-expiring) ────────────
    // App pushes toasts (drained from SocialManager); the renderer owns their
    // animation + lifetime. UpdateToasts() advances timers and returns true while
    // any toast is still on screen (so the host keeps the 60fps repaint going).
    void PushToast(int kind, uint64_t accountId,
                   const std::wstring& title, const std::wstring& body);
    // Advances animation/expiry timers. Pass the cursor so a hovered toast pauses
    // its auto-dismiss (Steam-style). Returns true while any toast is on screen.
    bool UpdateToasts(float mouseX, float mouseY);
    bool HasActiveToasts() const { return !m_toasts.empty(); }
    void DismissToast(uint64_t accountId, int kind);  // start the fade-out now
    struct ToastHit { enum Kind { None, Dismiss, Action } kind = None;
                      uint64_t accountId = 0; int toastKind = -1; };
    ToastHit HitTestToasts(float x, float y) const;

    // ── Notifications history dropdown (bell button) ──────────────────────────
    bool HitTestBellBtn(float x, float y) const;
    struct NotifHit { enum Kind { None, Row, MarkAll, Clear, Close,
                                  ToggleSound, ToggleOnline } kind = None;
                      uint64_t accountId = 0; };
    NotifHit HitTestNotifPanel(float x, float y) const;
    bool PointInNotifPanel(float x, float y) const;

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
    void DrawFriendsPanel(RenderState& state);
    void DrawChatWindow(RenderState& state);
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
    // Cached DM attachment image previews keyed by attachment id (1.3).
    std::unordered_map<uint64_t, ComPtr<ID2D1Bitmap>> m_attachmentImages;

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
    D2D1_RECT_F m_friendsBtnRect{};
    D2D1_RECT_F m_profileBtnRect{};

    // Friends panel layout/state. Row rects are cached during draw so hit-testing
    // doesn't re-derive grouped layout. Mutated only on the render thread.
    float m_friendsPanelW = 300.0f;
    D2D1_RECT_F m_friendsPanelRect{};
    D2D1_RECT_F m_friendsAddBtnRect{};
    std::vector<std::pair<D2D1_RECT_F, uint64_t>> m_friendRowRects;
    std::vector<std::pair<D2D1_RECT_F, uint64_t>> m_friendAcceptRects;  // incoming-request ✓
    std::vector<std::pair<D2D1_RECT_F, uint64_t>> m_friendDeclineRects; // incoming-request ✕
    std::vector<std::pair<D2D1_RECT_F, int>>      m_friendGroupHdrRects; // (rect, group idx)
    D2D1_RECT_F m_friendsSearchRect{};
    D2D1_RECT_F m_friendsSortRect{};
    float m_friendsContentH = 0.0f;   // total content height for scroll clamping
    D2D1_RECT_F m_emptyStateBtnRect{};  // non-zero only when the empty-state button is visible

    // Chat window layout rects (cached during DrawChatWindow for hit-testing).
    D2D1_RECT_F m_chatRect{};
    D2D1_RECT_F m_chatCloseRect{};
    D2D1_RECT_F m_chatInputRect{};
    D2D1_RECT_F m_chatSendRect{};
    D2D1_RECT_F m_chatCallRect{};
    D2D1_RECT_F m_chatMuteRect{};
    D2D1_RECT_F m_chatEndRect{};
    D2D1_RECT_F m_chatAcceptRect{};
    D2D1_RECT_F m_chatDeclineRect{};
    D2D1_RECT_F m_chatAttachRect{};   // "+" upload button in the input bar (1.3)
    // Clickable attachment chips drawn this frame -> their attachment id (1.3).
    std::vector<std::pair<D2D1_RECT_F, uint64_t>> m_chatAttachmentHits;
    // Per-message hover toolbar buttons drawn this frame (Discord-style).
    struct ChatActionHit { D2D1_RECT_F rect; ChatHit::Kind kind; uint64_t msgId; };
    std::vector<ChatActionHit> m_chatActionHits;
    // Reaction pills drawn this frame -> (rect, msgId, emoji) for toggling.
    struct ChatReactionHit { D2D1_RECT_F rect; uint64_t msgId; std::wstring emoji; };
    std::vector<ChatReactionHit> m_chatReactionHits;
    float m_chatContentH = 0.0f;

    // Toast notifications (bottom-right). Animation timers in ms via GetTickCount64.
    struct ActiveToast {
        int          kind = 6;
        uint64_t     accountId = 0;
        std::wstring title, body;
        uint64_t     spawnTick = 0;   // shifted forward while hovered (pause)
        uint64_t     lifeMs = 5200;
        bool         dismissing = false;
        uint64_t     dismissTick = 0;
        D2D1_RECT_F  rect{};          // last drawn card rect (for hit-test)
        D2D1_RECT_F  closeRect{};
    };
    std::vector<ActiveToast> m_toasts;
    uint64_t m_lastToastTick = 0;   // for hover-pause delta accounting
    void DrawToasts();

    // Notifications history dropdown layout.
    D2D1_RECT_F m_bellBtnRect{};
    D2D1_RECT_F m_notifPanelRect{};
    D2D1_RECT_F m_notifMarkAllRect{};
    D2D1_RECT_F m_notifClearRect{};
    D2D1_RECT_F m_notifSoundRect{};
    D2D1_RECT_F m_notifOnlineRect{};
    std::vector<std::pair<D2D1_RECT_F, uint64_t>> m_notifRowRects;
    float m_notifContentH = 0.0f;
    void DrawNotifPanel(RenderState& state);

    // Current user's profile picture (circular avatar). Null = show person glyph.
    ComPtr<ID2D1Bitmap> m_avatar;
};
