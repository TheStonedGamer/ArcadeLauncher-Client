#include "pch.h"
#include "Renderer.h"
#include "Version.h"

static D2D1_RECT_F D2DInsetRect(D2D1_RECT_F r, float dx, float dy) {
    return D2D1::RectF(r.left + dx, r.top + dy, r.right - dx, r.bottom - dy);
}

// ── Color palette ─────────────────────────────────────────────────────────────
static const D2D1_COLOR_F C_BG         = D2D1::ColorF(0x0D1117);
static const D2D1_COLOR_F C_SIDEBAR    = D2D1::ColorF(0x13181E);
static const D2D1_COLOR_F C_TOPBAR     = D2D1::ColorF(0x161B22);
static const D2D1_COLOR_F C_CARD       = D2D1::ColorF(0x21262D);
static const D2D1_COLOR_F C_CARD_HOV   = D2D1::ColorF(0x30363D);
static const D2D1_COLOR_F C_ACCENT     = D2D1::ColorF(0x58A6FF);
static const D2D1_COLOR_F C_TEXT       = D2D1::ColorF(0xC9D1D9);
static const D2D1_COLOR_F C_SUBTEXT    = D2D1::ColorF(0x8B949E);
static const D2D1_COLOR_F C_SELECTED   = D2D1::ColorF(0x388BFD);
static const D2D1_COLOR_F C_WHITE      = D2D1::ColorF(D2D1::ColorF::White);
static const D2D1_COLOR_F C_OVERLAY    = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.65f);

#define HR(x) { HRESULT _hr = (x); assert(SUCCEEDED(_hr)); }

Renderer::~Renderer() {}

bool Renderer::Initialize(HWND hwnd) {
    m_hwnd = hwnd;

    // D2D factory
    HR(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, m_factory.GetAddressOf()));

    // WIC factory
    HR(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(m_wic.GetAddressOf())));

    // DirectWrite factory
    HR(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                           __uuidof(IDWriteFactory),
                           reinterpret_cast<IUnknown**>(m_dwFactory.GetAddressOf())));

    RECT rc;
    GetClientRect(hwnd, &rc);
    m_width  = rc.right;
    m_height = rc.bottom;

    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndRtp =
        D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(m_width, m_height));
    HR(m_factory->CreateHwndRenderTarget(rtp, hwndRtp, m_rt.GetAddressOf()));
    m_rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    // On Win8+/Win11 the Hwnd render target also implements ID2D1DeviceContext,
    // which lets us downscale logos with HIGH_QUALITY_CUBIC instead of the blocky
    // 2x2 LINEAR filter. Optional — fall back to LINEAR if the QI fails.
    m_rt.As(&m_dc);

    CreateBrushes();
    Resize(m_width, m_height);
    return true;
}

void Renderer::CreateBrushes() {
    auto mk = [&](const D2D1_COLOR_F& c, ComPtr<ID2D1SolidColorBrush>& b) {
        HR(m_rt->CreateSolidColorBrush(c, b.GetAddressOf()));
    };
    mk(C_BG,       m_brushBg);
    mk(C_SIDEBAR,  m_brushSidebar);
    mk(C_TOPBAR,   m_brushTopbar);
    mk(C_CARD,     m_brushCard);
    mk(C_CARD_HOV, m_brushCardHover);
    mk(C_ACCENT,   m_brushAccent);
    mk(C_TEXT,     m_brushText);
    mk(C_SUBTEXT,  m_brushSubtext);
    mk(C_WHITE,    m_brushWhite);
    mk(C_OVERLAY,  m_brushOverlay);
    mk(C_SELECTED, m_brushSelected);

    auto fmt = [&](const wchar_t* font, float size, DWRITE_FONT_WEIGHT weight,
                   ComPtr<IDWriteTextFormat>& tf) {
        HR(m_dwFactory->CreateTextFormat(font, nullptr, weight,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            size, L"en-us", tf.GetAddressOf()));
        tf->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        tf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        tf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    };

    fmt(L"Segoe UI", 22.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, m_fmtTitle);
    fmt(L"Segoe UI", 13.0f, DWRITE_FONT_WEIGHT_NORMAL,    m_fmtSmall);
    fmt(L"Segoe UI", 11.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, m_fmtCard);
    fmt(L"Segoe UI",  9.5f, DWRITE_FONT_WEIGHT_NORMAL,    m_fmtCardSub);
    fmt(L"Segoe UI", 28.0f, DWRITE_FONT_WEIGHT_BOLD,      m_fmtHeading);
    fmt(L"Segoe UI", 14.0f, DWRITE_FONT_WEIGHT_NORMAL,    m_fmtSearch);
    fmt(L"Segoe UI", 13.0f, DWRITE_FONT_WEIGHT_NORMAL,    m_fmtSidebar);
    fmt(L"Segoe UI", 16.0f, DWRITE_FONT_WEIGHT_NORMAL,    m_fmtDetail);

    // Summary text: wrapping, smaller
    HR(m_dwFactory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"en-us",
        m_fmtSummary.GetAddressOf()));
    m_fmtSummary->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    m_fmtSummary->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    m_fmtSummary->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    // MDL2 Assets icon font for toolbar buttons (gear, etc.)
    HR(m_dwFactory->CreateTextFormat(L"Segoe MDL2 Assets", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 18.0f, L"en-us",
        m_fmtIcon.GetAddressOf()));
    m_fmtIcon->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    m_fmtIcon->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    m_fmtIcon->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

void Renderer::LoadPlatformIcons(PlatformIcons& icons, const EmulatorConfig& emuCfg) {
    icons.Load(emuCfg, m_rt.Get(), m_wic.Get());
    m_platformIcons = &icons;
}

void Renderer::Resize(UINT w, UINT h) {
    m_width = w; m_height = h;
    if (m_rt) m_rt->Resize(D2D1::SizeU(w, h));

    m_sidebarW = std::clamp((float)w * 0.22f, 176.0f, 220.0f);
    m_tileW = std::clamp(((float)w - m_sidebarW - 64.0f) / 4.0f, 150.0f, 190.0f);
    m_tileH = m_tileW * 1.44f;

    // Recompute grid columns
    float gridW = (float)w - m_sidebarW;
    m_cols = std::max(1, (int)((gridW + m_tileGap) / (m_tileW + m_tileGap)));

    // Right-aligned topbar action buttons. The profile button occupies the
    // far-right slot; everything else is shifted left one slot (46px each) to
    // make room for it.
    float actionsLeft = (float)w - 286.0f;
    float searchLeft = std::max(m_sidebarW + 168.0f, (float)w * 0.34f);
    float searchRight = std::min(actionsLeft - 12.0f, (float)w * 0.66f);
    if (searchRight - searchLeft < 180.0f) {
        searchLeft = m_sidebarW + 12.0f;
        searchRight = std::max(searchLeft + 160.0f, actionsLeft - 12.0f);
    }
    m_searchRect = D2D1::RectF(searchLeft, 14.0f, searchRight, 50.0f);
    m_profileBtnRect    = D2D1::RectF((float)w - 50.0f,  14.0f, (float)w - 14.0f, 50.0f);
    m_settingsBtnRect   = D2D1::RectF((float)w - 96.0f,  16.0f, (float)w - 60.0f, 48.0f);
    m_selectModeBtnRect = D2D1::RectF((float)w - 142.0f, 16.0f, (float)w - 104.0f, 48.0f);
    m_downloadsBtnRect  = D2D1::RectF((float)w - 188.0f, 16.0f, (float)w - 150.0f, 48.0f);
    m_sortBtnRect       = D2D1::RectF((float)w - 234.0f, 16.0f, (float)w - 196.0f, 48.0f);
    m_friendsBtnRect    = D2D1::RectF((float)w - 280.0f, 16.0f, (float)w - 242.0f, 48.0f);
    m_bellBtnRect       = D2D1::RectF((float)w - 326.0f, 16.0f, (float)w - 288.0f, 48.0f);
    m_friendsPanelW = std::clamp((float)w * 0.26f, 280.0f, 340.0f);
    m_launchBtnRect = {}; // set during detail panel draw
}

// ── Main render ───────────────────────────────────────────────────────────────

void Renderer::Render(const std::vector<const Game*>& games, RenderState& state) {
    if (!m_rt) return;
    m_lastGameCount = (int)games.size();
    m_animTime = (float)(GetTickCount64() % 100000) / 1000.0f;
    m_rt->BeginDraw();

    DrawBackground();
    DrawTopBar(state);
    DrawSidebar(state);

    if (state.detailOpen && state.detailIndex >= 0 &&
        state.detailIndex < (int)games.size())
        DrawDetailPanel(games[state.detailIndex], state);
    else
        DrawGrid(games, state);

    if (state.showFriendsPanel)
        DrawFriendsPanel(state);

    if (state.chatOpen)
        DrawChatWindow(state);

    if (state.notifOpen)
        DrawNotifPanel(state);

    // Toasts render above everything else.
    DrawToasts();

    m_rt->EndDraw();
}

void Renderer::DrawBackground() {
    m_rt->Clear(C_BG);
}

void Renderer::DrawTopBar(const RenderState& state) {
    float w = (float)m_width;
    D2D1_RECT_F bar = D2D1::RectF(0, 0, w, m_topbarH);
    m_rt->FillRectangle(bar, m_brushTopbar.Get());

    float sweep = 0.5f + 0.5f * sinf(m_animTime * 0.75f);
    float accentW = 120.0f + 80.0f * sweep;
    D2D1_RECT_F accent = D2D1::RectF(m_sidebarW, m_topbarH - 2.0f,
                                      std::min(w, m_sidebarW + accentW), m_topbarH);
    m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, 0.55f));
    m_rt->FillRectangle(accent, m_brushAccent.Get());
    m_brushAccent->SetColor(C_ACCENT);

    // App name
    D2D1_RECT_F titleRect = D2D1::RectF(m_sidebarW + 8, 0, m_sidebarW + 200, m_topbarH);
    m_rt->DrawText(L"ArcadeLauncher", 14, m_fmtTitle.Get(), titleRect, m_brushAccent.Get());

    // Search box background
    bool searchFocused = (state.focusArea == FocusArea::Search);
    auto& sr = m_searchRect;
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(sr, 6, 6), m_brushCard.Get());
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(sr, 6, 6),
        searchFocused ? m_brushAccent.Get() : m_brushSubtext.Get(),
        searchFocused ? 2.0f : 1.0f);

    // Search text / placeholder + cursor when focused
    D2D1_RECT_F textRect = D2D1::RectF(sr.left + 10, sr.top, sr.right - 10, sr.bottom);
    if (!state.searchQuery.empty()) {
        std::wstring display = state.searchQuery + (searchFocused ? L"│" : L"");
        m_rt->DrawText(display.c_str(), (UINT32)display.size(),
                       m_fmtSearch.Get(), textRect, m_brushText.Get());
    } else if (searchFocused) {
        m_rt->DrawText(L"│", 1, m_fmtSearch.Get(), textRect, m_brushSubtext.Get());
    } else {
        m_rt->DrawText(L"Search games...", 15, m_fmtSearch.Get(),
                       textRect, m_brushSubtext.Get());
    }

    // Ctrl+F hint when not focused
    if (!searchFocused) {
        D2D1_RECT_F hintRect = D2D1::RectF(sr.right - 52, sr.top, sr.right - 6, sr.bottom);
        m_rt->DrawText(L"Ctrl+F", 6, m_fmtSmall.Get(), hintRect, m_brushSubtext.Get());
    }

    // Metadata scan status pill — shown while background fetch is running
    if (state.metaScanning) {
        static const wchar_t kScanText[] = L"Fetching metadata…";
        D2D1_RECT_F pill = D2D1::RectF(
            m_selectModeBtnRect.left - 166.0f, 18.0f,
            m_selectModeBtnRect.left - 8.0f,  46.0f);
        float pulse = 0.45f + 0.35f * (0.5f + 0.5f * sinf(m_animTime * 4.0f));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(pill, 6, 6), m_brushCard.Get());
        m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, pulse));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(pill, 6, 6),
                                   m_brushAccent.Get(), 1.0f);
        m_brushAccent->SetColor(C_ACCENT);
        D2D1_RECT_F pillText = D2D1::RectF(pill.left + 8, pill.top,
                                            pill.right - 8, pill.bottom);
        m_rt->DrawText(kScanText, (UINT32)wcslen(kScanText),
                       m_fmtSmall.Get(), pillText, m_brushSubtext.Get());
    }

    // Settings gear button (Segoe MDL2 Assets U+E713)
    auto& sel = m_selectModeBtnRect;
    if (state.selectionMode) {
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(sel, 6, 6), m_brushCardHover.Get());
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(sel, 6, 6), m_brushAccent.Get(), 1.5f);
    } else {
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(sel, 6, 6), m_brushCard.Get());
    }
    m_rt->DrawText(L"îœ¾", 1, m_fmtIcon.Get(),
                   D2D1::RectF(sel.left, sel.top, sel.right, sel.bottom),
                   state.selectionMode ? m_brushAccent.Get() : m_brushSubtext.Get());

    if (state.selectionMode && !state.selectedGameIds.empty()) {
        std::wstring count = std::to_wstring((int)state.selectedGameIds.size());
        D2D1_RECT_F badge = D2D1::RectF(sel.right - 10.0f, sel.top - 4.0f,
                                         sel.right + 10.0f, sel.top + 16.0f);
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F((badge.left + badge.right) * 0.5f,
                                                       (badge.top + badge.bottom) * 0.5f),
                                        10.0f, 10.0f), m_brushAccent.Get());
        m_rt->DrawText(count.c_str(), (UINT32)count.size(), m_fmtCardSub.Get(),
                       badge, m_brushBg.Get());
    }

    // Downloads button (Segoe MDL2 Assets U+E896 download glyph)
    auto& dl = m_downloadsBtnRect;
    bool dlActive = state.activeDownloadCount > 0;
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(dl, 6, 6),
                               dlActive ? m_brushCardHover.Get() : m_brushCard.Get());
    m_rt->DrawText(L"\xE896", 1, m_fmtIcon.Get(),
                   D2D1::RectF(dl.left, dl.top, dl.right, dl.bottom),
                   dlActive ? m_brushAccent.Get() : m_brushSubtext.Get());

    if (dlActive) {
        std::wstring count = std::to_wstring(state.activeDownloadCount);
        D2D1_RECT_F badge = D2D1::RectF(dl.right - 10.0f, dl.top - 4.0f,
                                         dl.right + 10.0f, dl.top + 16.0f);
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F((badge.left + badge.right) * 0.5f,
                                                       (badge.top + badge.bottom) * 0.5f),
                                        10.0f, 10.0f), m_brushAccent.Get());
        m_rt->DrawText(count.c_str(), (UINT32)count.size(), m_fmtCardSub.Get(),
                       badge, m_brushBg.Get());
    }

    // Sort button. Glyph changes with the active sort mode (see SortModeIcon);
    // accent-highlighted whenever ordering differs from the default (Recent).
    auto& so = m_sortBtnRect;
    bool sortActive = state.sortMode != SortMode::Recent;
    const wchar_t* sortGlyph = SortModeIcon(state.sortMode);
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(so, 6, 6),
                               sortActive ? m_brushCardHover.Get() : m_brushCard.Get());
    m_rt->DrawText(sortGlyph, 1, m_fmtIcon.Get(),
                   D2D1::RectF(so.left, so.top, so.right, so.bottom),
                   sortActive ? m_brushAccent.Get() : m_brushSubtext.Get());

    // Friends button (Segoe MDL2 Assets U+E902 "People"). Highlighted while the
    // panel is open; carries a badge for pending requests + unread DMs.
    auto& fr = m_friendsBtnRect;
    bool friendsActive = state.showFriendsPanel;
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(fr, 6, 6),
                               friendsActive ? m_brushCardHover.Get() : m_brushCard.Get());
    if (friendsActive) {
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(fr, 6, 6), m_brushAccent.Get(), 1.5f);
    }
    m_rt->DrawText(L"\xE902", 1, m_fmtIcon.Get(),
                   D2D1::RectF(fr.left, fr.top, fr.right, fr.bottom),
                   friendsActive ? m_brushAccent.Get() : m_brushSubtext.Get());
    int frBadge = state.pendingRequests + state.socialUnread;
    if (frBadge > 0) {
        std::wstring count = std::to_wstring(frBadge);
        D2D1_RECT_F badge = D2D1::RectF(fr.right - 10.0f, fr.top - 4.0f,
                                         fr.right + 10.0f, fr.top + 16.0f);
        m_brushAccent->SetColor(D2D1::ColorF(0xE5534B)); // red attention badge
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F((badge.left + badge.right) * 0.5f,
                                                       (badge.top + badge.bottom) * 0.5f),
                                        10.0f, 10.0f), m_brushAccent.Get());
        m_brushAccent->SetColor(C_ACCENT);
        m_rt->DrawText(count.c_str(), (UINT32)count.size(), m_fmtCardSub.Get(),
                       badge, m_brushWhite.Get());
    }

    // Notifications bell (Segoe MDL2 U+E7E7). Active while the dropdown is open;
    // red badge shows unread count.
    auto& bl = m_bellBtnRect;
    bool bellActive = state.notifOpen;
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(bl, 6, 6),
                               bellActive ? m_brushCardHover.Get() : m_brushCard.Get());
    if (bellActive)
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(bl, 6, 6), m_brushAccent.Get(), 1.5f);
    m_rt->DrawText(L"\xE7E7", 1, m_fmtIcon.Get(),
                   D2D1::RectF(bl.left, bl.top, bl.right, bl.bottom),
                   bellActive ? m_brushAccent.Get() : m_brushSubtext.Get());
    if (state.notifUnread > 0) {
        std::wstring count = std::to_wstring(state.notifUnread);
        D2D1_POINT_2F bc = D2D1::Point2F(bl.right - 2.0f, bl.top + 2.0f);
        m_brushAccent->SetColor(D2D1::ColorF(0xE5534B));
        m_rt->FillEllipse(D2D1::Ellipse(bc, 10.0f, 10.0f), m_brushAccent.Get());
        m_brushAccent->SetColor(C_ACCENT);
        m_rt->DrawText(count.c_str(), (UINT32)count.size(), m_fmtCardSub.Get(),
                       D2D1::RectF(bc.x - 10, bc.y - 10, bc.x + 10, bc.y + 10), m_brushWhite.Get());
    }

    auto& sb = m_settingsBtnRect;
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(sb, 6, 6), m_brushCard.Get());
    m_rt->DrawText(L"", 1, m_fmtIcon.Get(),
                   D2D1::RectF(sb.left, sb.top, sb.right, sb.bottom),
                   m_brushSubtext.Get());

    // Profile button — circular avatar (or a generic person glyph when no
    // picture is set). Clicking it opens the account dropdown.
    auto& pb = m_profileBtnRect;
    float pcx = (pb.left + pb.right) * 0.5f;
    float pcy = (pb.top + pb.bottom) * 0.5f;
    float pr  = (pb.right - pb.left) * 0.5f;
    D2D1_ELLIPSE ring = D2D1::Ellipse(D2D1::Point2F(pcx, pcy), pr, pr);
    if (m_avatar && m_factory) {
        ComPtr<ID2D1EllipseGeometry> geo;
        if (SUCCEEDED(m_factory->CreateEllipseGeometry(ring, geo.GetAddressOf()))) {
            ComPtr<ID2D1Layer> layer;
            m_rt->CreateLayer(nullptr, layer.GetAddressOf());
            m_rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), geo.Get()), layer.Get());
            // Cover-fit the avatar into the circle's bounding box.
            D2D1_SIZE_F bs = m_avatar->GetSize();
            float side = pr * 2.0f;
            float scale = std::max(side / bs.width, side / bs.height);
            float dw = bs.width * scale, dh = bs.height * scale;
            D2D1_RECT_F dst = D2D1::RectF(pcx - dw / 2, pcy - dh / 2, pcx + dw / 2, pcy + dh / 2);
            m_rt->DrawBitmap(m_avatar.Get(), dst);
            m_rt->PopLayer();
        }
    } else {
        m_rt->FillEllipse(ring, m_brushCard.Get());
        m_rt->DrawText(L"\xE77B", 1, m_fmtIcon.Get(),  // Contact (person) glyph
                       D2D1::RectF(pb.left, pb.top, pb.right, pb.bottom),
                       m_brushSubtext.Get());
    }
    m_rt->DrawEllipse(ring, m_brushAccent.Get(), 1.5f);
}

// static
std::vector<Renderer::SidebarEntry> Renderer::BuildSidebarEntries(const RenderState& s) {
    std::vector<SidebarEntry> v;
    v.push_back({ L"All Games", true,  Platform::Repacks, LibraryPage::All });
    v.push_back({ L"Favorites", false, Platform::Repacks, LibraryPage::Favorites });
    v.push_back({ L"Recently Played", false, Platform::Repacks, LibraryPage::RecentlyPlayed });
    v.push_back({ L"Installed", false, Platform::Repacks, LibraryPage::Installed });
    v.push_back({ L"Ready to Download", false, Platform::Repacks, LibraryPage::ReadyToDownload });
    // "Background Downloads" tab removed — the topbar Downloads button opens the
    // live download-status window, which fully supersedes a static grid page.
    v.push_back({ L"Updates", false, Platform::Repacks, LibraryPage::Updates });
    if (s.showSteam)   v.push_back({ L"Steam",   false, Platform::Steam,   LibraryPage::Platform });
    if (s.showEpic)    v.push_back({ L"Epic",    false, Platform::Epic,    LibraryPage::Platform });
    if (s.showGog)     v.push_back({ L"GOG",     false, Platform::GOG,     LibraryPage::Platform });
    // Dolphin tab removed — GameCube and Wii are surfaced as their own tabs.
    if (s.showGameCube) v.push_back({ L"GameCube", false, Platform::GameCube, LibraryPage::Platform });
    if (s.showWii)     v.push_back({ L"Wii",     false, Platform::Wii,     LibraryPage::Platform });
    if (s.showRyujinx) v.push_back({ L"Ryujinx", false, Platform::Ryujinx, LibraryPage::Platform });
    if (s.showRPCS3)   v.push_back({ L"RPCS3",   false, Platform::RPCS3,   LibraryPage::Platform });
    if (s.showN64)     v.push_back({ L"N64",     false, Platform::N64,     LibraryPage::Platform });
    if (s.showNES)     v.push_back({ L"NES",      false, Platform::NES,     LibraryPage::Platform });
    if (s.showSNES)    v.push_back({ L"SNES",     false, Platform::SNES,    LibraryPage::Platform });
    if (s.showPS1)     v.push_back({ L"PS1",      false, Platform::PS1,     LibraryPage::Platform });
    if (s.showPS2)     v.push_back({ L"PS2",      false, Platform::PS2,     LibraryPage::Platform });
    if (s.showXbox360) v.push_back({ L"Xbox 360", false, Platform::Xbox360, LibraryPage::Platform });
    if (s.showXbox)    v.push_back({ L"Xbox",     false, Platform::Xbox,    LibraryPage::Platform });
    if (s.showRepacks) v.push_back({ L"PC",  false, Platform::Repacks, LibraryPage::Platform });
    for (const auto& c : s.collections)
        v.push_back({ c, false, Platform::Repacks, LibraryPage::Collection, c });
    // Only surface the Hidden tab when something is actually hidden, so the
    // sidebar stays clean for users who never use the feature.
    if (s.anyHidden)
        v.push_back({ L"Hidden", false, Platform::Repacks, LibraryPage::Hidden });
    return v;
}

void Renderer::DrawSidebar(const RenderState& state) {
    D2D1_RECT_F sbar = D2D1::RectF(0, 0, m_sidebarW, (float)m_height);
    m_rt->FillRectangle(sbar, m_brushSidebar.Get());

    // Separator line on right
    m_rt->DrawLine(D2D1::Point2F(m_sidebarW, 0), D2D1::Point2F(m_sidebarW, (float)m_height),
                   m_brushCard.Get(), 1.0f);

    auto entries = BuildSidebarEntries(state);

    bool sidebarKbFocus = (state.focusArea == FocusArea::Sidebar);
    float listTop = m_topbarH + 12.0f;
    float listBottom = std::max(listTop, (float)m_height - 86.0f);
    float contentH = (float)entries.size() * 42.0f;
    float maxScroll = std::max(0.0f, contentH - (listBottom - listTop));
    float scroll = std::clamp(state.sidebarScroll, 0.0f, maxScroll);

    m_rt->PushAxisAlignedClip(D2D1::RectF(0, listTop, m_sidebarW, listBottom),
                              D2D1_ANTIALIAS_MODE_ALIASED);

    float y = listTop - scroll;
    int entryIdx = 0;
    for (auto& e : entries) {
        bool active = (e.page == state.libraryPage) &&
                      (e.page != LibraryPage::Platform ||
                       state.filterPlatform == e.p) &&
                      (e.page != LibraryPage::Collection ||
                       state.filterCollection == e.collection);
        bool kbFocus = sidebarKbFocus && (entryIdx == state.sidebarFocusIdx);
        D2D1_RECT_F row = D2D1::RectF(0, y, m_sidebarW, y + 38.0f);

        if (active) {
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(6, y + 2, m_sidebarW - 6, y + 36), 6, 6),
                m_brushCardHover.Get());
            // Left accent bar
            m_rt->FillRectangle(D2D1::RectF(0, y + 6, 3, y + 32), m_brushAccent.Get());
        }

        // Keyboard focus ring (distinct from active selection)
        if (kbFocus && !active) {
            m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, 0.5f));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(6, y + 2, m_sidebarW - 6, y + 36), 6, 6),
                m_brushAccent.Get(), 1.5f);
            m_brushAccent->SetColor(C_ACCENT);
        } else if (kbFocus && active) {
            m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, 0.9f));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(6, y + 2, m_sidebarW - 6, y + 36), 6, 6),
                m_brushAccent.Get(), 2.0f);
            m_brushAccent->SetColor(C_ACCENT);
        }

        if (e.page == LibraryPage::Platform) {
            // Try to draw platform icon; fall back to colored dot
            ID2D1Bitmap* icon = m_platformIcons ? m_platformIcons->Get(e.p) : nullptr;
            if (icon) {
                float iconSz = 20.0f;
                float iconX = 8.0f, iconY = y + (38.0f - iconSz) / 2.0f;
                // Rounded chip behind the logo. Most console logos are dark
                // wordmarks (PlayStation, Switch, NES) that need a light tile to
                // show on the dark sidebar. A few logos are light/silver and wash
                // out on a light tile (the Xbox orbs, the thin light-blue PS2
                // mark), so those get a dark chip instead.
                bool lightLogo = (e.p == Platform::Xbox || e.p == Platform::Xbox360 ||
                                  e.p == Platform::PS2);
                D2D1_RECT_F chip = D2D1::RectF(iconX - 3.0f, iconY - 3.0f,
                                               iconX + iconSz + 3.0f, iconY + iconSz + 3.0f);
                if (lightLogo)
                    m_brushCard->SetColor(D2D1::ColorF(0.09f, 0.09f, 0.11f, active ? 1.0f : 0.85f));
                else
                    m_brushCard->SetColor(D2D1::ColorF(0.93f, 0.94f, 0.96f, active ? 1.0f : 0.78f));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(chip, 6.0f, 6.0f), m_brushCard.Get());
                // Inset 1px inside the chip so the logo doesn't touch the rounded edge.
                DrawIconFit(icon, D2D1::RectF(iconX + 1.0f, iconY + 1.0f,
                                              iconX + iconSz - 1.0f, iconY + iconSz - 1.0f),
                            active ? 1.0f : 0.85f);
            } else {
                D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(20.0f, y + 19.0f), 5, 5);
                m_brushAccent->SetColor(PlatformColor(e.p));
                m_rt->FillEllipse(dot, m_brushAccent.Get());
                m_brushAccent->SetColor(C_ACCENT);
            }
        } else if (e.page == LibraryPage::Collection) {
            // Small bookmark/star dot to distinguish user collections.
            D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(20.0f, y + 19.0f), 4, 4);
            m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b,
                                                 active ? 1.0f : 0.6f));
            m_rt->DrawEllipse(dot, m_brushAccent.Get(), 1.5f);
            m_brushAccent->SetColor(C_ACCENT);
        } else if (e.page == LibraryPage::BackgroundDownloads) {
            D2D1_RECT_F tray = D2D1::RectF(12.0f, y + 11.0f, 28.0f, y + 27.0f);
            m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, active ? 1.0f : 0.65f));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(tray, 3.0f, 3.0f), m_brushAccent.Get(), 1.5f);
            m_rt->DrawLine(D2D1::Point2F(tray.left + 4.0f, tray.top + 8.0f),
                           D2D1::Point2F(tray.left + 8.0f, tray.bottom - 4.0f),
                           m_brushAccent.Get(), 1.5f);
            m_rt->DrawLine(D2D1::Point2F(tray.left + 8.0f, tray.bottom - 4.0f),
                           D2D1::Point2F(tray.right - 4.0f, tray.top + 4.0f),
                           m_brushAccent.Get(), 1.5f);
            m_brushAccent->SetColor(C_ACCENT);
        }

        D2D1_RECT_F lbl = D2D1::RectF((e.all ? 16.0f : 34.0f), y, m_sidebarW - 8, y + 38);
        m_rt->DrawText(e.label.c_str(), (UINT32)e.label.size(), m_fmtSidebar.Get(), lbl,
                       (active || kbFocus) ? m_brushText.Get() : m_brushSubtext.Get());
        y += 42.0f;
        ++entryIdx;
    }
    m_rt->PopAxisAlignedClip();

    if (maxScroll > 0.0f) {
        float trackTop = listTop + 4.0f;
        float trackBottom = listBottom - 4.0f;
        float trackH = trackBottom - trackTop;
        float thumbH = std::max(28.0f, trackH * ((listBottom - listTop) / contentH));
        float thumbY = trackTop + (trackH - thumbH) * (scroll / maxScroll);
        D2D1_RECT_F track = D2D1::RectF(m_sidebarW - 7.0f, trackTop, m_sidebarW - 3.0f, trackBottom);
        D2D1_RECT_F thumb = D2D1::RectF(m_sidebarW - 8.0f, thumbY, m_sidebarW - 2.0f, thumbY + thumbH);
        m_brushOverlay->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.28f));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(track, 2.0f, 2.0f), m_brushOverlay.Get());
        m_brushOverlay->SetColor(C_OVERLAY);
        m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, 0.55f));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(thumb, 3.0f, 3.0f), m_brushAccent.Get());
        m_brushAccent->SetColor(C_ACCENT);
    }

    // Version number
    {
        static const std::wstring ver = L"v" ARCADE_VERSION_WSTR;
        D2D1_RECT_F vr = D2D1::RectF(8, (float)m_height - 76, m_sidebarW - 8, (float)m_height - 58);
        m_rt->DrawText(ver.c_str(), (UINT32)ver.size(), m_fmtSmall.Get(), vr, m_brushSubtext.Get());
    }

    // Tab hint at bottom of sidebar when not in sidebar focus
    if (!sidebarKbFocus) {
        D2D1_RECT_F hint = D2D1::RectF(8, (float)m_height - 56, m_sidebarW - 8, (float)m_height - 38);
        m_rt->DrawText(L"Tab to focus", 12, m_fmtSmall.Get(), hint, m_brushSubtext.Get());
    }

    // Footer: game count (passed via RenderState is not available here; caller sets it via games.size())
    // We store it in a member set each Render() call — use the cached count
    std::wstring footer = std::to_wstring((int)m_lastGameCount) + L" games";
    D2D1_RECT_F fr = D2D1::RectF(8, (float)m_height - 36, m_sidebarW - 8, (float)m_height - 8);
    m_rt->DrawText(footer.c_str(), (UINT32)footer.size(), m_fmtSmall.Get(), fr,
                   m_brushSubtext.Get());
}

void Renderer::DrawGrid(const std::vector<const Game*>& games, RenderState& state) {
    if (games.empty()) {
        m_emptyStateBtnRect = {};   // reset; set below if we draw the button

        float cx = (m_sidebarW + m_width) / 2.0f;
        float cy = (m_topbarH + m_height) / 2.0f;

        // Dim message above the button
        const wchar_t* msg = state.libraryPage == LibraryPage::BackgroundDownloads
            ? L"No background downloads"
            : L"No games in this library";
        D2D1_RECT_F msgR = D2D1::RectF(m_sidebarW + 40, cy - 58.0f,
                                        (float)m_width - 40, cy - 16.0f);
        m_rt->DrawText(msg, (UINT32)wcslen(msg),
                       m_fmtDetail.Get(), msgR, m_brushSubtext.Get());

        // "Open Settings" button
        float bw = 196.0f, bh = 38.0f;
        m_emptyStateBtnRect = D2D1::RectF(cx - bw / 2, cy - 2.0f,
                                           cx + bw / 2, cy - 2.0f + bh);
        auto rr = D2D1::RoundedRect(m_emptyStateBtnRect, 8.0f, 8.0f);
        m_rt->FillRoundedRectangle(rr, m_brushCard.Get());
        m_rt->DrawRoundedRectangle(rr, m_brushAccent.Get(), 1.5f);

        static const wchar_t kBtn[] = L"Open Settings";
        m_rt->DrawText(kBtn, (UINT32)(sizeof(kBtn)/sizeof(wchar_t) - 1),
                       m_fmtSidebar.Get(), m_emptyStateBtnRect, m_brushAccent.Get());
        return;
    }
    m_emptyStateBtnRect = {};

    // Clipping to grid area
    D2D1_RECT_F clip = D2D1::RectF(m_sidebarW, m_topbarH,
                                    (float)m_width, (float)m_height);
    m_rt->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);

    float startX = m_sidebarW + m_tileGap;
    float startY = m_topbarH + m_tileGap - state.scrollOffset;

    int rows = ((int)games.size() + m_cols - 1) / m_cols;
    for (int i = 0; i < (int)games.size(); ++i) {
        int col = i % m_cols;
        int row = i / m_cols;
        float x = startX + col * (m_tileW + m_tileGap);
        float y = startY + row * (m_tileH + m_tileGap + 22.0f); // +22 for title below

        // Skip if off-screen
        if (y + m_tileH + 22 < m_topbarH || y > (float)m_height) continue;

        D2D1_RECT_F rect = D2D1::RectF(x, y, x + m_tileW, y + m_tileH);
        bool multiSelected = state.selectedGameIds.count(games[i]->id) > 0;
        DrawCard(*games[i], rect, state.hoveredIndex == i, state.selectedIndex == i,
                 state.selectionMode, multiSelected);
    }

    m_rt->PopAxisAlignedClip();
}

void Renderer::DrawCard(const Game& game, D2D1_RECT_F rect,
                         bool hovered, bool selected,
                         bool selectionMode, bool multiSelected) {
    float rnd = 8.0f;
    float hoverPulse = hovered ? (0.5f + 0.5f * sinf(m_animTime * 5.0f)) : 0.0f;
    if (hovered || selected || multiSelected) {
        float lift = selected ? 2.0f : 4.0f;
        rect.top -= lift;
        rect.bottom -= lift;
    }

    // Card shadow / glow
    if (hovered || selected || multiSelected) {
        D2D1_RECT_F shadow = D2D1::RectF(rect.left - 5, rect.top + 3,
                                          rect.right + 5, rect.bottom + 8);
        auto col = (selected || multiSelected) ? C_SELECTED : C_ACCENT;
        col.a = (selected || multiSelected) ? 0.34f : 0.24f;
        m_brushCard->SetColor(col);
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(shadow, rnd + 2, rnd + 2),
                                   m_brushCard.Get());
        m_brushCard->SetColor(C_CARD);
    }

    if (!hovered && !selected && !multiSelected) {
        D2D1_RECT_F shadow = D2D1::RectF(rect.left + 2, rect.top + 4,
                                          rect.right + 2, rect.bottom + 5);
        m_brushOverlay->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.24f));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(shadow, rnd, rnd),
                                   m_brushOverlay.Get());
        m_brushOverlay->SetColor(C_OVERLAY);
    }

    // Card background
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(rect, rnd, rnd),
                                hovered ? m_brushCardHover.Get() : m_brushCard.Get());
    if (hovered) {
        m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, 0.10f + hoverPulse * 0.07f));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(rect, rnd, rnd), m_brushAccent.Get());
        m_brushAccent->SetColor(C_ACCENT);
    }

    // Art / placeholder
    ID2D1Bitmap* bmp = GetArt(game.id);
    if (bmp) {
        D2D1_RECT_F src = D2D1::RectF(0, 0, (float)bmp->GetSize().width,
                                            (float)bmp->GetSize().height);
        // Use a layer with a rounded-rect geometry mask for clean art clipping
        ComPtr<ID2D1Layer> layer;
        if (SUCCEEDED(m_rt->CreateLayer(nullptr, layer.GetAddressOf()))) {
            D2D1_ROUNDED_RECT clipRR = D2D1::RoundedRect(rect, rnd, rnd);
            ComPtr<ID2D1RoundedRectangleGeometry> geom;
            m_factory->CreateRoundedRectangleGeometry(clipRR, geom.GetAddressOf());
            if (geom) {
                D2D1_LAYER_PARAMETERS lp = D2D1::LayerParameters(
                    D2D1::InfiniteRect(), geom.Get());
                m_rt->PushLayer(lp, layer.Get());
                m_rt->DrawBitmap(bmp, rect, 1.0f,
                                 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
                m_rt->PopLayer();
            } else {
                m_rt->DrawBitmap(bmp, rect, 1.0f,
                                 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
            }
        }
    } else {
        DrawPlaceholderArt(rect, game.platform);
    }

    // Bottom gradient overlay for title readability
    float gradH = 60.0f;
    D2D1_RECT_F gradRect = D2D1::RectF(rect.left, rect.bottom - gradH,
                                        rect.right, rect.bottom);
    m_brushOverlay->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.75f));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(gradRect, 0, 0), m_brushOverlay.Get());
    m_brushOverlay->SetColor(C_OVERLAY);

    // Game title inside card at bottom
    D2D1_RECT_F titleRect = D2D1::RectF(rect.left + 6, rect.bottom - 40,
                                         rect.right - 6, rect.bottom - 4);
    m_fmtCard->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    m_rt->DrawText(game.title.c_str(), (UINT32)game.title.size(),
                   m_fmtCard.Get(), titleRect, m_brushWhite.Get());
    m_fmtCard->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

    // Platform badge (top-right corner)
    DrawPlatformBadge(game.platform,
                      D2D1::Point2F(rect.right - 14, rect.top + 14));

    // ROM-variant count badge: this tile represents N dumps of one game.
    if (game.variantCount > 1) {
        std::wstring vt = std::to_wstring(game.variantCount) + L" versions";
        float w = 16.0f + vt.size() * 6.5f;
        D2D1_RECT_F vp = D2D1::RectF(rect.right - 8 - w, rect.top + 30,
                                     rect.right - 8, rect.top + 49);
        m_brushOverlay->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.68f));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(vp, 4, 4), m_brushOverlay.Get());
        m_brushOverlay->SetColor(C_OVERLAY);
        m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, 0.85f));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(vp, 4, 4), m_brushAccent.Get(), 1.0f);
        m_brushAccent->SetColor(C_ACCENT);
        D2D1_RECT_F vtxt = D2D1::RectF(vp.left + 5, vp.top + 1, vp.right, vp.bottom);
        m_rt->DrawText(vt.c_str(), (UINT32)vt.size(), m_fmtDetail.Get(), vtxt, m_brushWhite.Get());
    }

    if (game.serverBacked) {
        std::wstring state = game.installState == InstallState::Installed ? L"Installed" :
                             game.installState == InstallState::UpdateAvailable ? L"Update" :
                             game.installState == InstallState::Downloading ? L"Downloading" :
                             L"Download";
        D2D1_RECT_F pill = D2D1::RectF(rect.left + 8, rect.top + 8, rect.left + 86, rect.top + 28);
        m_brushOverlay->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.68f));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(pill, 4, 4), m_brushOverlay.Get());
        m_brushOverlay->SetColor(C_OVERLAY);
        if (game.installState == InstallState::Downloading) {
            float pulse = 0.35f + 0.25f * (0.5f + 0.5f * sinf(m_animTime * 6.0f));
            m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, pulse));
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2DInsetRect(pill, -1.0f, -1.0f), 5, 5),
                                       m_brushAccent.Get(), 2.0f);
            m_brushAccent->SetColor(C_ACCENT);

            D2D1_RECT_F track = D2D1::RectF(rect.left + 8.0f, rect.top + 34.0f,
                                            rect.right - 8.0f, rect.top + 39.0f);
            m_brushOverlay->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.60f));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(track, 2.0f, 2.0f), m_brushOverlay.Get());
            float pct = std::clamp(game.installProgressPermille / 1000.0f, 0.0f, 1.0f);
            if (pct <= 0.0f) {
                pct = 0.20f + 0.18f * (0.5f + 0.5f * sinf(m_animTime * 4.0f));
            }
            D2D1_RECT_F fill = track;
            fill.right = fill.left + (track.right - track.left) * pct;
            m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, 0.88f));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(fill, 2.0f, 2.0f), m_brushAccent.Get());
            m_brushAccent->SetColor(C_ACCENT);
        }
        m_rt->DrawText(state.c_str(), (UINT32)state.size(), m_fmtSmall.Get(), pill,
                       game.installState == InstallState::Installed ? m_brushAccent.Get() : m_brushWhite.Get());
    }

    // Hover-only "⋯" overflow button (lower-right). Clicking it opens the same
    // context menu as a right-click. Hidden in selection mode, where the corner
    // checkbox owns the interaction. Geometry mirrored in HitTestCardMenuButton.
    if (hovered && !selectionMode) {
        D2D1_POINT_2F bc = D2D1::Point2F(rect.right - 16.0f, rect.bottom - 16.0f);
        D2D1_ELLIPSE be = D2D1::Ellipse(bc, 14.0f, 14.0f);
        m_brushOverlay->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.80f));
        m_rt->FillEllipse(be, m_brushOverlay.Get());
        m_brushOverlay->SetColor(C_OVERLAY);
        m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, 0.9f));
        m_rt->DrawEllipse(be, m_brushAccent.Get(), 1.2f);
        m_brushAccent->SetColor(C_ACCENT);
        for (int k = -1; k <= 1; ++k)
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(bc.x + k * 5.0f, bc.y), 1.8f, 1.8f),
                              m_brushWhite.Get());
    }

    // Selected border
    if (selectionMode) {
        D2D1_RECT_F box = D2D1::RectF(rect.left + 10.0f, rect.top + 10.0f,
                                      rect.left + 32.0f, rect.top + 32.0f);
        m_brushOverlay->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.62f));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(box, 4.0f, 4.0f), m_brushOverlay.Get());
        m_brushOverlay->SetColor(C_OVERLAY);
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(box, 4.0f, 4.0f),
                                   multiSelected ? m_brushAccent.Get() : m_brushSubtext.Get(),
                                   multiSelected ? 2.0f : 1.25f);
        if (multiSelected) {
            m_rt->DrawLine(D2D1::Point2F(box.left + 5.0f, box.top + 12.0f),
                           D2D1::Point2F(box.left + 10.0f, box.bottom - 5.0f),
                           m_brushAccent.Get(), 2.0f);
            m_rt->DrawLine(D2D1::Point2F(box.left + 10.0f, box.bottom - 5.0f),
                           D2D1::Point2F(box.right - 5.0f, box.top + 6.0f),
                           m_brushAccent.Get(), 2.0f);
        }
    }

    if (selected || multiSelected) {
        m_brushSelected->SetColor(C_SELECTED);
        m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, 0.22f));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2DInsetRect(rect, -2.0f, -2.0f), rnd + 2, rnd + 2),
                                   m_brushAccent.Get(), 4.0f);
        m_brushAccent->SetColor(C_ACCENT);
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(rect, rnd, rnd),
                                   m_brushSelected.Get(), selected ? 2.0f : 1.5f);
        m_brushSelected->SetColor(C_SELECTED);
    } else if (hovered) {
        m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, 0.6f));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(rect, rnd, rnd),
                                   m_brushAccent.Get(), 1.5f);
        m_brushAccent->SetColor(C_ACCENT);
    }
}

void Renderer::DrawIconFit(ID2D1Bitmap* bmp, D2D1_RECT_F box, float opacity) {
    if (!bmp) return;
    D2D1_SIZE_F sz = bmp->GetSize();
    if (sz.width <= 0 || sz.height <= 0) return;

    // Fit (contain) within box, preserving aspect ratio.
    float boxW = box.right - box.left, boxH = box.bottom - box.top;
    float scale = (std::min)(boxW / sz.width, boxH / sz.height);
    float w = sz.width * scale, h = sz.height * scale;
    float cx = (box.left + box.right) * 0.5f, cy = (box.top + box.bottom) * 0.5f;
    D2D1_RECT_F dst = D2D1::RectF(cx - w * 0.5f, cy - h * 0.5f,
                                  cx + w * 0.5f, cy + h * 0.5f);

    if (m_dc) {
        m_dc->DrawBitmap(bmp, dst, opacity,
                         D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr);
    } else {
        m_rt->DrawBitmap(bmp, dst, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
}

void Renderer::DrawPlatformBadge(Platform p, D2D1_POINT_2F center) {
    D2D1_ELLIPSE e = D2D1::Ellipse(center, 13.0f, 13.0f);
    m_brushCard->SetColor(D2D1::ColorF(0.05f, 0.05f, 0.08f, 0.85f));
    m_rt->FillEllipse(e, m_brushCard.Get());

    ID2D1Bitmap* icon = m_platformIcons ? m_platformIcons->Get(p) : nullptr;
    if (icon) {
        D2D1_RECT_F dst = D2D1::RectF(center.x - 9.0f, center.y - 9.0f,
                                      center.x + 9.0f, center.y + 9.0f);
        DrawIconFit(icon, dst, 1.0f);
    } else {
        m_brushAccent->SetColor(PlatformColor(p));
        D2D1_ELLIPSE inner = D2D1::Ellipse(center, 5.0f, 5.0f);
        m_rt->FillEllipse(inner, m_brushAccent.Get());
    }

    m_brushAccent->SetColor(PlatformColor(p));
    m_rt->DrawEllipse(e, m_brushAccent.Get(), 1.0f);
    m_brushCard->SetColor(C_CARD);
    m_brushAccent->SetColor(C_ACCENT);
}

void Renderer::DrawPlaceholderArt(D2D1_RECT_F rect, Platform p) {
    // Gradient placeholder using platform color
    D2D1_COLOR_F col = PlatformColor(p);
    col.a = 0.15f;
    m_brushCard->SetColor(col);
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(rect, 8, 8), m_brushCard.Get());
    m_brushCard->SetColor(C_CARD);

    // Platform name centered
    m_brushSubtext->SetColor(D2D1::ColorF(col.r, col.g, col.b, 0.5f));
    std::wstring label = PlatformName(p);
    m_rt->DrawText(label.c_str(), (UINT32)label.size(), m_fmtTitle.Get(),
                   rect, m_brushSubtext.Get());
    m_brushSubtext->SetColor(C_SUBTEXT);
}

float Renderer::DrawWrapped(const std::wstring& text, IDWriteTextFormat* fmt,
                            float x, float y, float w, ID2D1Brush* brush, bool draw) {
    if (text.empty() || w <= 1.0f) return 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(m_dwFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(),
            fmt, w, 100000.0f, layout.GetAddressOf())))
        return 0.0f;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    if (draw)
        m_rt->DrawTextLayout(D2D1::Point2F(x, y), layout.Get(), brush,
                             D2D1_DRAW_TEXT_OPTIONS_CLIP);
    return tm.height;
}

void Renderer::DrawDetailPanel(const Game* game, RenderState& state) {
    if (!game) return;
    float w = (float)m_width, h = (float)m_height;

    // Dark overlay
    m_brushOverlay->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.90f));
    m_rt->FillRectangle(D2D1::RectF(0, 0, w, h), m_brushOverlay.Get());
    m_brushOverlay->SetColor(C_OVERLAY);

    // Panel (Steam-style dashboard: art on the left, rich info + changelog right)
    float panelW = std::min(1000.0f, w - 80);
    float panelH = std::min(620.0f, h - 90);
    float px = (w - panelW) / 2;
    float py = (h - panelH) / 2;
    D2D1_RECT_F panel = D2D1::RectF(px, py, px + panelW, py + panelH);
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(panel, 12, 12), m_brushSidebar.Get());

    // ── Hero banner (Steam-style) ──────────────────────────────────────────────
    // Use the first screenshot, falling back to box art, as a wide cover-cropped
    // backdrop across the panel header. Clipped to the panel's rounded corners via
    // a geometry layer and heavily darkened (solid scrim + bottom fade into the
    // panel colour) so the white title/chips drawn afterwards stay legible.
    ID2D1Bitmap* hero = nullptr;
    if (!game->screenshots.empty()) hero = GetArt(Game::ScreenshotKey(game->id, 0));
    if (!hero) hero = GetArt(game->id);
    if (hero) {
        const float heroH = std::min(190.0f, panelH * 0.4f);
        D2D1_RECT_F heroBand = D2D1::RectF(px, py, px + panelW, py + heroH);

        ComPtr<ID2D1RoundedRectangleGeometry> clipGeo;
        m_factory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(panel, 12, 12),
                                                  clipGeo.GetAddressOf());
        if (clipGeo) {
            m_rt->PushLayer(D2D1::LayerParameters(D2D1::InfiniteRect(), clipGeo.Get()),
                            nullptr);

            // Cover-crop the source to the band's aspect ratio (no stretching).
            float bw = (float)hero->GetSize().width, bh = (float)hero->GetSize().height;
            float bandAR = (heroBand.right - heroBand.left) / heroH;
            float srcW = bw, srcH = bw / bandAR;
            if (srcH > bh) { srcH = bh; srcW = bh * bandAR; }
            float sx0 = (bw - srcW) / 2.0f, sy0 = (bh - srcH) * 0.30f;  // bias toward top
            D2D1_RECT_F src = D2D1::RectF(sx0, sy0, sx0 + srcW, sy0 + srcH);
            m_rt->DrawBitmap(hero, heroBand, 1.0f,
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);

            // Darkening scrim so foreground text reads cleanly.
            m_brushOverlay->SetColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.55f));
            m_rt->FillRectangle(heroBand, m_brushOverlay.Get());
            m_brushOverlay->SetColor(C_OVERLAY);

            // Bottom fade into the panel colour for a seamless blend.
            ComPtr<ID2D1GradientStopCollection> stops;
            D2D1_GRADIENT_STOP gs[2] = {
                { 0.0f, D2D1::ColorF(C_SIDEBAR.r, C_SIDEBAR.g, C_SIDEBAR.b, 0.0f) },
                { 1.0f, D2D1::ColorF(C_SIDEBAR.r, C_SIDEBAR.g, C_SIDEBAR.b, 1.0f) },
            };
            if (SUCCEEDED(m_rt->CreateGradientStopCollection(gs, 2, stops.GetAddressOf()))) {
                ComPtr<ID2D1LinearGradientBrush> fade;
                if (SUCCEEDED(m_rt->CreateLinearGradientBrush(
                        D2D1::LinearGradientBrushProperties(
                            D2D1::Point2F(px, heroBand.bottom - heroH * 0.55f),
                            D2D1::Point2F(px, heroBand.bottom)),
                        stops.Get(), fade.GetAddressOf())))
                    m_rt->FillRectangle(heroBand, fade.Get());
            }
            m_rt->PopLayer();
        }
    }

    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(panel, 12, 12), m_brushCard.Get(), 1.5f);

    // Art on left
    float artW = 240, artH = std::min(artW * 1.4f, panelH - 120.0f);
    D2D1_RECT_F artRect = D2D1::RectF(px + 24, py + 24, px + 24 + artW, py + 24 + artH);
    ID2D1Bitmap* bmp = GetArt(game->id);
    if (bmp) {
        D2D1_RECT_F src = D2D1::RectF(0, 0, (float)bmp->GetSize().width,
                                            (float)bmp->GetSize().height);
        m_rt->DrawBitmap(bmp, artRect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
    } else {
        DrawPlaceholderArt(artRect, game->platform);
    }

    // Hero/platform badge under the art
    DrawPlatformBadge(game->platform,
                      D2D1::Point2F(artRect.right - 14, artRect.top + 14));

    // Info on right
    float ix = px + 24 + artW + 28;
    float iy = py + 28;
    float iw = panelW - artW - 80;

    // Title
    D2D1_RECT_F titleR = D2D1::RectF(ix, iy, ix + iw - 44, iy + 60);
    m_fmtHeading->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    m_rt->DrawText(game->title.c_str(), (UINT32)game->title.size(),
                   m_fmtHeading.Get(), titleR, m_brushText.Get());
    m_fmtHeading->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    iy += 62;

    // ── Metadata chips (cleaner than a stack of "Label: value" rows) ───────────
    // Pill-shaped chips for the at-a-glance facts that apply to *every* platform,
    // wrapping to new rows as needed. Dynamic per-session stats (playtime, last
    // played, version) follow underneath as a compact key/value block.
    const float chipH = 26.0f, chipPadX = 11.0f, chipGap = 8.0f;
    float chipX = ix, chipY = iy;
    bool anyChip = false;
    auto chip = [&](const std::wstring& text, ID2D1Brush* textBrush, bool outline) {
        if (text.empty()) return;
        anyChip = true;
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(m_dwFactory->CreateTextLayout(text.c_str(), (UINT32)text.size(),
                m_fmtDetail.Get(), 4000.0f, chipH, layout.GetAddressOf())))
            return;
        DWRITE_TEXT_METRICS tm{}; layout->GetMetrics(&tm);
        float cw = tm.widthIncludingTrailingWhitespace + chipPadX * 2;
        if (chipX > ix && chipX + cw > ix + iw) {   // wrap to next chip row
            chipX = ix; chipY += chipH + chipGap;
        }
        D2D1_RECT_F r = D2D1::RectF(chipX, chipY, chipX + cw, chipY + chipH);
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(r, chipH / 2, chipH / 2), m_brushCard.Get());
        if (outline)
            m_rt->DrawRoundedRectangle(D2D1::RoundedRect(r, chipH / 2, chipH / 2), textBrush, 1.2f);
        m_rt->DrawTextLayout(D2D1::Point2F(chipX + chipPadX, chipY + (chipH - tm.height) / 2.0f),
                             layout.Get(), textBrush);
        chipX += cw + chipGap;
    };

    chip(PlatformName(game->platform), m_brushSubtext.Get(), false);

    if (game->releaseDate > 0) {
        SYSTEMTIME rst; FILETIME rft;
        LONGLONG rll = (LONGLONG)(game->releaseDate) * 10000000LL + 116444736000000000LL;
        rft.dwLowDateTime = (DWORD)(rll & 0xFFFFFFFF); rft.dwHighDateTime = (DWORD)(rll >> 32);
        FileTimeToSystemTime(&rft, &rst);
        chip(std::to_wstring(rst.wYear), m_brushSubtext.Get(), false);
    }

    if (!game->RatingStr().empty()) {
        m_brushAccent->SetColor(game->RatingColor());
        chip(L"\x2605 " + game->RatingStr(), m_brushAccent.Get(), true);   // ★ rating
        m_brushAccent->SetColor(C_ACCENT);
    }

    if (game->serverBacked) {
        const wchar_t* stxt =
            game->installState == InstallState::Installed ? L"Installed" :
            game->installState == InstallState::UpdateAvailable ? L"Update available" :
            game->installState == InstallState::Downloading ? L"Downloading" :
            L"Not installed";
        bool good = game->installState == InstallState::Installed;
        ID2D1Brush* b = good ? m_brushAccent.Get() : m_brushSubtext.Get();
        chip(stxt, b, good);
    }

    // Genre list → one chip per genre (split on commas).
    if (!game->genres.empty()) {
        std::wstring g = game->genres;
        size_t start = 0;
        while (start < g.size()) {
            size_t comma = g.find(L',', start);
            std::wstring one = g.substr(start, comma == std::wstring::npos ? std::wstring::npos : comma - start);
            size_t a = one.find_first_not_of(L" \t");
            size_t z = one.find_last_not_of(L" \t");
            if (a != std::wstring::npos) chip(one.substr(a, z - a + 1), m_brushSubtext.Get(), false);
            if (comma == std::wstring::npos) break;
            start = comma + 1;
        }
    }

    iy = chipY + (anyChip ? chipH + 16.0f : 0.0f);

    // Compact key/value stats block.
    auto stat = [&](const std::wstring& label, const std::wstring& value) {
        m_rt->DrawText(label.c_str(), (UINT32)label.size(), m_fmtDetail.Get(),
                       D2D1::RectF(ix, iy, ix + 130, iy + 22), m_brushSubtext.Get());
        m_rt->DrawText(value.c_str(), (UINT32)value.size(), m_fmtDetail.Get(),
                       D2D1::RectF(ix + 130, iy, ix + iw, iy + 22), m_brushText.Get());
        iy += 24;
    };

    stat(L"Playtime", game->PlaytimeStr());

    if (game->lastPlayed > 0) {
        SYSTEMTIME st; FILETIME ft;
        LONGLONG ll = (LONGLONG)(game->lastPlayed) * 10000000LL + 116444736000000000LL;
        ft.dwLowDateTime = (DWORD)(ll & 0xFFFFFFFF); ft.dwHighDateTime = (DWORD)(ll >> 32);
        FileTimeToSystemTime(&ft, &st);
        wchar_t dateBuf[64];
        swprintf_s(dateBuf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
        stat(L"Last played", dateBuf);
    }

    if (!game->serverVersion.empty())
        stat(L"Version", game->serverVersion);

    if (!game->developer.empty()) stat(L"Developer", game->developer);
    if (!game->publisher.empty()) stat(L"Publisher", game->publisher);
    if (!game->franchise.empty()) stat(L"Franchise", game->franchise);

    // ── Scrollable content pane: Summary + What's New (changelog) ──────────────
    float btnY = py + panelH - 70.0f;
    float paneTop = iy + 8.0f;
    float paneBottom = btnY - 14.0f;
    float paneH = std::max(0.0f, paneBottom - paneTop);

    if (paneH > 24.0f) {
        m_rt->PushAxisAlignedClip(D2D1::RectF(ix, paneTop, ix + iw, paneBottom),
                                  D2D1_ANTIALIAS_MODE_ALIASED);
        float cy = paneTop - state.detailScroll;

        auto heading = [&](const std::wstring& s) {
            m_rt->DrawText(s.c_str(), (UINT32)s.size(), m_fmtDetail.Get(),
                           D2D1::RectF(ix, cy, ix + iw, cy + 24), m_brushText.Get());
            cy += 28.0f;
        };

        // Screenshot / artwork strip (IGDB). Thumbnails are decoded into the art
        // cache under Game::ScreenshotKey ids by App::EnsureScreenshots; any not
        // yet downloaded simply render as an empty placeholder tile.
        if (!game->screenshots.empty()) {
            heading(L"Screenshots");
            const float shotH = 92.0f, gap = 8.0f, shotW = shotH * (16.0f / 9.0f);
            float sx = ix, sy = cy, rowBottom = cy + shotH;
            int n = (int)game->screenshots.size();
            if (n > 8) n = 8;
            for (int i = 0; i < n; ++i) {
                if (sx > ix && sx + shotW > ix + iw) { sx = ix; sy += shotH + gap; }
                D2D1_RECT_F r = D2D1::RectF(sx, sy, sx + shotW, sy + shotH);
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(r, 6, 6), m_brushCard.Get());
                if (ID2D1Bitmap* sb = GetArt(Game::ScreenshotKey(game->id, i))) {
                    D2D1_RECT_F src = D2D1::RectF(0, 0, (float)sb->GetSize().width,
                                                        (float)sb->GetSize().height);
                    m_rt->DrawBitmap(sb, r, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
                }
                sx += shotW + gap;
                rowBottom = sy + shotH;
            }
            cy = rowBottom + 18.0f;
        }

        if (!game->summary.empty()) {
            heading(L"Summary");
            cy += DrawWrapped(game->summary, m_fmtSummary.Get(), ix, cy, iw,
                              m_brushSubtext.Get()) + 16.0f;
        }

        heading(L"What's New");
        if (state.detailChangelogsLoading && state.detailChangelogs.empty()) {
            m_rt->DrawText(L"Loading changelogs…", 19, m_fmtSummary.Get(),
                           D2D1::RectF(ix, cy, ix + iw, cy + 22), m_brushSubtext.Get());
            cy += 26.0f;
        } else if (state.detailChangelogs.empty()) {
            const wchar_t* none = game->serverBacked
                ? L"No changelog entries yet."
                : L"Changelogs are available for server-backed games.";
            m_rt->DrawText(none, (UINT32)wcslen(none), m_fmtSummary.Get(),
                           D2D1::RectF(ix, cy, ix + iw, cy + 22), m_brushSubtext.Get());
            cy += 26.0f;
        } else {
            for (const auto& e : state.detailChangelogs) {
                std::wstring hdr;
                if (!e.version.empty()) hdr += L"v" + e.version;
                if (!e.title.empty())
                    hdr += (hdr.empty() ? L"" : L"  —  ") + e.title;
                if (e.createdAt > 0) {
                    SYSTEMTIME cst; FILETIME cft;
                    LONGLONG cll = (LONGLONG)(e.createdAt) * 10000000LL + 116444736000000000LL;
                    cft.dwLowDateTime = (DWORD)(cll & 0xFFFFFFFF);
                    cft.dwHighDateTime = (DWORD)(cll >> 32);
                    FileTimeToSystemTime(&cft, &cst);
                    wchar_t db[32];
                    swprintf_s(db, L"   (%04d-%02d-%02d)", cst.wYear, cst.wMonth, cst.wDay);
                    hdr += db;
                }
                if (hdr.empty()) hdr = L"Update";
                m_brushAccent->SetColor(C_ACCENT);
                cy += DrawWrapped(hdr, m_fmtDetail.Get(), ix, cy, iw,
                                  m_brushAccent.Get()) + 2.0f;
                if (!e.body.empty())
                    cy += DrawWrapped(e.body, m_fmtSummary.Get(), ix, cy, iw,
                                      m_brushSubtext.Get());
                cy += 16.0f;
            }
        }

        m_rt->PopAxisAlignedClip();

        // Clamp the scroll offset to the actual content height (for next frame).
        float contentH = (cy + state.detailScroll) - paneTop;
        float maxScroll = std::max(0.0f, contentH - paneH);
        state.detailScroll = std::clamp(state.detailScroll, 0.0f, maxScroll);

        // Scrollbar thumb when the content overflows.
        if (maxScroll > 0.0f) {
            float trackTop = paneTop, trackBottom = paneBottom;
            float trackH = trackBottom - trackTop;
            float thumbH = std::max(28.0f, trackH * (paneH / contentH));
            float thumbY = trackTop + (trackH - thumbH) * (state.detailScroll / maxScroll);
            D2D1_RECT_F thumb = D2D1::RectF(ix + iw - 5.0f, thumbY, ix + iw - 1.0f, thumbY + thumbH);
            m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, 0.5f));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(thumb, 2.0f, 2.0f), m_brushAccent.Get());
            m_brushAccent->SetColor(C_ACCENT);
        }
    }

    // Launch / Install button
    float btnW = 170, btnH = 44;
    float btnX = ix;
    m_launchBtnRect = D2D1::RectF(btnX, btnY, btnX + btnW, btnY + btnH);
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(m_launchBtnRect, 8, 8), m_brushAccent.Get());
    const wchar_t* btnText = (game->serverBacked && game->installState != InstallState::Installed)
        ? L"Install  [Enter]"
        : L"Play  [Enter]";
    m_rt->DrawText(btnText, (UINT32)wcslen(btnText), m_fmtSmall.Get(), m_launchBtnRect, m_brushBg.Get());

    // Keyboard hints row (bottom-right of panel)
    D2D1_RECT_F hintR = D2D1::RectF(btnX + btnW + 12, btnY + 4, px + panelW - 10, btnY + btnH - 4);
    static const wchar_t kHint[] = L"\x2190 \x2192 navigate  \x2022  P properties  \x2022  scroll changelog  \x2022  Esc close";
    m_rt->DrawText(kHint, (UINT32)wcslen(kHint), m_fmtSmall.Get(), hintR, m_brushSubtext.Get());

    // Close X in top-right corner
    D2D1_RECT_F closeR = D2D1::RectF(px + panelW - 40, py + 8, px + panelW - 8, py + 36);
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(closeR, 4, 4), m_brushCard.Get());
    m_rt->DrawText(L"\x2715", 1, m_fmtSmall.Get(), closeR, m_brushSubtext.Get());
}

// ── Art cache ─────────────────────────────────────────────────────────────────

bool Renderer::LoadGameArt(const std::wstring& gameId, const std::wstring& path) {
    ComPtr<ID2D1Bitmap> bmp = LoadBitmapFromFile(path);
    if (!bmp) return false;
    std::lock_guard<std::mutex> lk(m_artMutex);
    m_artCache[gameId] = std::move(bmp);
    return true;
}

void Renderer::UnloadGameArt(const std::wstring& gameId) {
    std::lock_guard<std::mutex> lk(m_artMutex);
    m_artCache.erase(gameId);
}

ID2D1Bitmap* Renderer::GetArt(const std::wstring& id) const {
    std::lock_guard<std::mutex> lk(m_artMutex);
    auto it = m_artCache.find(id);
    return (it != m_artCache.end()) ? it->second.Get() : nullptr;
}

ComPtr<ID2D1Bitmap> Renderer::LoadBitmapFromFile(const std::wstring& path) {
    if (path.empty()) return nullptr;

    ComPtr<IWICBitmapDecoder>    dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter>  conv;

    if (FAILED(m_wic->CreateDecoderFromFilename(path.c_str(), nullptr,
               GENERIC_READ, WICDecodeMetadataCacheOnLoad, dec.GetAddressOf())))
        return nullptr;
    if (FAILED(dec->GetFrame(0, frame.GetAddressOf()))) return nullptr;
    if (FAILED(m_wic->CreateFormatConverter(conv.GetAddressOf()))) return nullptr;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
               WICBitmapDitherTypeNone, nullptr, 0.0,
               WICBitmapPaletteTypeMedianCut))) return nullptr;

    ComPtr<ID2D1Bitmap> bmp;
    m_rt->CreateBitmapFromWicBitmap(conv.Get(), nullptr, bmp.GetAddressOf());
    return bmp;
}

bool Renderer::DecodeImageFile(const std::wstring& path, DecodedImage& out) {
    if (path.empty()) return false;

    // Use a private WIC factory so this is independent of the render thread's
    // m_wic and safe to run concurrently on a worker thread.
    ComPtr<IWICImagingFactory>    wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(wic.GetAddressOf()))))
        return false;

    ComPtr<IWICBitmapDecoder>     dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter>   conv;
    if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
               WICDecodeMetadataCacheOnLoad, dec.GetAddressOf())))
        return false;
    if (FAILED(dec->GetFrame(0, frame.GetAddressOf()))) return false;
    if (FAILED(wic->CreateFormatConverter(conv.GetAddressOf()))) return false;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
               WICBitmapDitherTypeNone, nullptr, 0.0,
               WICBitmapPaletteTypeMedianCut)))
        return false;

    UINT w = 0, h = 0;
    if (FAILED(conv->GetSize(&w, &h)) || w == 0 || h == 0) return false;

    out.width  = w;
    out.height = h;
    out.pixels.resize((size_t)w * h * 4);
    WICRect rc{ 0, 0, (INT)w, (INT)h };
    if (FAILED(conv->CopyPixels(&rc, w * 4, (UINT)out.pixels.size(), out.pixels.data())))
        return false;
    return true;
}

bool Renderer::StoreDecodedArt(const DecodedImage& img) {
    if (!m_rt || img.pixels.empty() || img.width == 0 || img.height == 0) return false;
    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap> bmp;
    if (FAILED(m_rt->CreateBitmap(D2D1::SizeU(img.width, img.height),
               img.pixels.data(), img.width * 4, props, bmp.GetAddressOf())))
        return false;
    std::lock_guard<std::mutex> lk(m_artMutex);
    m_artCache[img.gameId] = std::move(bmp);
    return true;
}

// ── Hit testing ───────────────────────────────────────────────────────────────

int Renderer::HitTestGrid(float x, float y, const RenderState& state,
                           size_t gameCount) const {
    if (x < m_sidebarW || y < m_topbarH) return -1;
    float gx = x - m_sidebarW - m_tileGap;
    float gy = y - m_topbarH - m_tileGap + state.scrollOffset;
    int col = (int)(gx / (m_tileW + m_tileGap));
    int row = (int)(gy / (m_tileH + m_tileGap + 22.0f));
    if (col < 0 || col >= m_cols || row < 0) return -1;
    // Check we're actually inside a tile (not in the gap)
    float localX = fmodf(gx, m_tileW + m_tileGap);
    float localY = fmodf(gy, m_tileH + m_tileGap + 22.0f);
    if (localX > m_tileW || localY > m_tileH) return -1;
    int idx = row * m_cols + col;
    return (idx < (int)gameCount) ? idx : -1;
}

int Renderer::HitTestCardMenuButton(float x, float y, const RenderState& state,
                                     size_t gameCount) const {
    int idx = HitTestGrid(x, y, state, gameCount);
    if (idx < 0 || idx != state.hoveredIndex || state.selectionMode) return -1;
    int col = idx % m_cols, row = idx / m_cols;
    float startX = m_sidebarW + m_tileGap;
    float startY = m_topbarH + m_tileGap - state.scrollOffset;
    float rRight  = startX + col * (m_tileW + m_tileGap) + m_tileW;
    float lift    = (idx == state.selectedIndex) ? 2.0f : 4.0f;  // hovered cards lift
    float rBottom = startY + row * (m_tileH + m_tileGap + 22.0f) + m_tileH - lift;
    float cx = rRight - 16.0f, cy = rBottom - 16.0f, r = 15.0f;
    return (fabsf(x - cx) <= r && fabsf(y - cy) <= r) ? idx : -1;
}

bool Renderer::HitTestSidebar(float x, float y, const RenderState& state,
                               Platform& outPlatform, bool& outAll,
                               LibraryPage& outPage, std::wstring& outCollection) const {
    if (x >= m_sidebarW) return false;
    auto entries = BuildSidebarEntries(state);
    float listTop = m_topbarH + 12.0f;
    float listBottom = std::max(listTop, (float)m_height - 86.0f);
    if (y < listTop || y > listBottom) return false;

    float ey = listTop - std::clamp(state.sidebarScroll, 0.0f, MaxSidebarScroll(state));
    for (auto& e : entries) {
        if (y >= ey && y <= ey + 38.0f) {
            outAll        = e.all;
            outPlatform   = e.p;
            outPage       = e.page;
            outCollection = e.collection;
            return true;
        }
        ey += 42.0f;
    }
    return false;
}

float Renderer::MaxSidebarScroll(const RenderState& s) const {
    float listTop = m_topbarH + 12.0f;
    float listBottom = std::max(listTop, (float)m_height - 86.0f);
    float contentH = (float)BuildSidebarEntries(s).size() * 42.0f;
    return std::max(0.0f, contentH - (listBottom - listTop));
}

bool Renderer::HitTestSearch(float x, float y) const {
    return x >= m_searchRect.left && x <= m_searchRect.right &&
           y >= m_searchRect.top  && y <= m_searchRect.bottom;
}

bool Renderer::HitTestLaunchBtn(float x, float y) const {
    return x >= m_launchBtnRect.left && x <= m_launchBtnRect.right &&
           y >= m_launchBtnRect.top  && y <= m_launchBtnRect.bottom;
}

bool Renderer::HitTestSettingsBtn(float x, float y) const {
    return x >= m_settingsBtnRect.left && x <= m_settingsBtnRect.right &&
           y >= m_settingsBtnRect.top  && y <= m_settingsBtnRect.bottom;
}

bool Renderer::HitTestDownloadsBtn(float x, float y) const {
    return x >= m_downloadsBtnRect.left && x <= m_downloadsBtnRect.right &&
           y >= m_downloadsBtnRect.top  && y <= m_downloadsBtnRect.bottom;
}

bool Renderer::HitTestSelectModeBtn(float x, float y) const {
    return x >= m_selectModeBtnRect.left && x <= m_selectModeBtnRect.right &&
           y >= m_selectModeBtnRect.top  && y <= m_selectModeBtnRect.bottom;
}

bool Renderer::HitTestSortBtn(float x, float y) const {
    return x >= m_sortBtnRect.left && x <= m_sortBtnRect.right &&
           y >= m_sortBtnRect.top  && y <= m_sortBtnRect.bottom;
}

bool Renderer::HitTestProfileBtn(float x, float y) const {
    return x >= m_profileBtnRect.left && x <= m_profileBtnRect.right &&
           y >= m_profileBtnRect.top  && y <= m_profileBtnRect.bottom;
}

bool Renderer::HitTestFriendsBtn(float x, float y) const {
    return x >= m_friendsBtnRect.left && x <= m_friendsBtnRect.right &&
           y >= m_friendsBtnRect.top  && y <= m_friendsBtnRect.bottom;
}

// ── Friends panel ─────────────────────────────────────────────────────────────

// Presence dot colors, indexed by the int presence code in FriendRowView.
static D2D1_COLOR_F PresenceColor(int p) {
    switch (p) {
        case 5: return D2D1::ColorF(0x57AB5A); // InGame  - green (rich)
        case 1: return D2D1::ColorF(0x58A6FF); // Online  - blue
        case 2: return D2D1::ColorF(0xD29922); // Away    - amber
        case 3: return D2D1::ColorF(0xE5534B); // Busy    - red
        default: return D2D1::ColorF(0x6E7681); // Offline/Invisible - grey
    }
}

bool Renderer::PointInFriendsPanel(float x, float y) const {
    return x >= m_friendsPanelRect.left && x <= m_friendsPanelRect.right &&
           y >= m_friendsPanelRect.top  && y <= m_friendsPanelRect.bottom;
}

float Renderer::MaxFriendsScroll(const RenderState& s) const {
    float viewH = (float)m_height - m_topbarH;
    return std::max(0.0f, m_friendsContentH - viewH);
}

Renderer::FriendsHit Renderer::HitTestFriendsPanel(float x, float y) const {
    FriendsHit hit;
    auto in = [&](const D2D1_RECT_F& r) {
        return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; };
    if (in(m_friendsAddBtnRect)) { hit.kind = FriendsHit::AddFriend; return hit; }
    if (in(m_friendsSearchRect)) { hit.kind = FriendsHit::Search;    return hit; }
    if (in(m_friendsSortRect))   { hit.kind = FriendsHit::SortToggle; return hit; }
    for (const auto& gr : m_friendGroupHdrRects)
        if (in(gr.first)) { hit.kind = FriendsHit::GroupHeader; hit.groupIndex = gr.second; return hit; }
    // Inline request actions take priority over the row they sit on.
    for (const auto& rr : m_friendAcceptRects) {
        const D2D1_RECT_F& r = rr.first;
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
            hit.kind = FriendsHit::AcceptRequest;
            hit.accountId = rr.second;
            return hit;
        }
    }
    for (const auto& rr : m_friendDeclineRects) {
        const D2D1_RECT_F& r = rr.first;
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
            hit.kind = FriendsHit::DeclineRequest;
            hit.accountId = rr.second;
            return hit;
        }
    }
    for (const auto& rr : m_friendRowRects) {
        const D2D1_RECT_F& r = rr.first;
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) {
            hit.kind = FriendsHit::Row;
            hit.accountId = rr.second;
            return hit;
        }
    }
    return hit;
}

void Renderer::DrawFriendsPanel(RenderState& state) {
    float w = (float)m_width;
    float panelL = w - m_friendsPanelW;
    D2D1_RECT_F panel = D2D1::RectF(panelL, m_topbarH, w, (float)m_height);
    m_friendsPanelRect = panel;

    // Backing + left edge separator.
    m_rt->FillRectangle(panel, m_brushSidebar.Get());
    m_rt->DrawLine(D2D1::Point2F(panelL, m_topbarH), D2D1::Point2F(panelL, (float)m_height),
                   m_brushCard.Get(), 1.0f);

    // Header: title + gateway status dot + Add Friend (+) button.
    float pad = 14.0f;
    D2D1_RECT_F hdr = D2D1::RectF(panelL + pad, m_topbarH + 12.0f, w - pad, m_topbarH + 40.0f);
    m_rt->DrawText(L"Friends", 7, m_fmtHeading.Get(), hdr, m_brushText.Get());

    // Gateway status dot (green connected, amber connecting/reconnecting, grey off)
    D2D1_COLOR_F gw = state.gatewayState == 2 ? D2D1::ColorF(0x57AB5A)
                     : (state.gatewayState == 1 || state.gatewayState == 3)
                           ? D2D1::ColorF(0xD29922)
                           : D2D1::ColorF(0x6E7681);
    m_brushWhite->SetColor(gw);
    m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(panelL + 78.0f, m_topbarH + 25.0f), 4.0f, 4.0f),
                      m_brushWhite.Get());
    m_brushWhite->SetColor(C_WHITE);

    // Add Friend button (top-right of header).
    m_friendsAddBtnRect = D2D1::RectF(w - pad - 30.0f, m_topbarH + 10.0f, w - pad, m_topbarH + 40.0f);
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(m_friendsAddBtnRect, 6, 6), m_brushCard.Get());
    m_rt->DrawText(L"\xE710", 1, m_fmtIcon.Get(), m_friendsAddBtnRect, m_brushAccent.Get()); // Add (+)

    // Search box + sort toggle row.
    float srTop = m_topbarH + 46.0f, srBot = srTop + 28.0f;
    float sortW = 64.0f;
    m_friendsSearchRect = D2D1::RectF(panelL + pad, srTop, w - pad - sortW - 8.0f, srBot);
    m_friendsSortRect   = D2D1::RectF(w - pad - sortW, srTop, w - pad, srBot);
    bool searchFocused = (state.focusArea == FocusArea::FriendsSearch);
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(m_friendsSearchRect, 6, 6), m_brushCard.Get());
    if (searchFocused)
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(m_friendsSearchRect, 6, 6), m_brushAccent.Get(), 1.0f);
    D2D1_RECT_F mag = D2D1::RectF(m_friendsSearchRect.left + 6, srTop, m_friendsSearchRect.left + 28, srBot);
    m_rt->DrawText(L"\xE721", 1, m_fmtSmall.Get(), mag, m_brushSubtext.Get()); // magnifier
    D2D1_RECT_F sret = D2D1::RectF(m_friendsSearchRect.left + 30, srTop, m_friendsSearchRect.right - 8, srBot);
    if (state.friendsFilter.empty() && !searchFocused) {
        m_rt->DrawText(L"Search friends", 14, m_fmtCardSub.Get(), sret, m_brushSubtext.Get());
    } else {
        std::wstring shown = state.friendsFilter;
        if (searchFocused && ((GetTickCount64() / 500) % 2)) shown += L"|";
        m_rt->DrawText(shown.c_str(), (UINT32)shown.size(), m_fmtCardSub.Get(), sret, m_brushText.Get());
    }
    // Sort toggle: shows current mode; click cycles.
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(m_friendsSortRect, 6, 6), m_brushCard.Get());
    const wchar_t* sortLbl = state.friendsSortMode == 1 ? L"Recent" : L"A–Z";
    m_rt->DrawText(sortLbl, (UINT32)wcslen(sortLbl), m_fmtCardSub.Get(),
                   m_friendsSortRect, m_brushSubtext.Get());

    // Scrollable list region.
    float listTop = m_topbarH + 84.0f;
    m_rt->PushAxisAlignedClip(D2D1::RectF(panelL, listTop, w, (float)m_height),
                              D2D1_ANTIALIAS_MODE_ALIASED);

    m_friendRowRects.clear();
    m_friendAcceptRects.clear();
    m_friendDeclineRects.clear();
    m_friendGroupHdrRects.clear();

    // Lower-cased filter; matches against username + nickname (substring).
    std::wstring filt = state.friendsFilter;
    for (auto& c : filt) c = towlower(c);
    bool filtering = !filt.empty();
    auto matches = [&](const FriendRowView& f) {
        if (!filtering) return true;
        auto has = [&](const std::wstring& s) {
            std::wstring l = s; for (auto& c : l) c = towlower(c);
            return l.find(filt) != std::wstring::npos;
        };
        return has(f.username) || (!f.nickname.empty() && has(f.nickname));
    };

    // Bucket friends into Steam-like groups. Pending received first (actionable),
    // then Favorites, In-Game, Online, Away/Busy, Offline, sent requests, blocked.
    auto bucket = [](const FriendRowView& f) -> int {
        if (f.relation == 2) return 0;           // RequestReceived
        if (f.relation == 1) return 6;           // RequestSent
        if (f.relation == 4) return 7;           // Blocked
        if (f.favorite) return 1;                // Favorites pinned above the rest
        switch (f.presence) {                     // Accepted -> by presence
            case 5: return 2;                     // InGame
            case 1: return 3;                     // Online
            case 2: case 3: return 4;             // Away/Busy
            default: return 5;                    // Offline/Invisible
        }
    };
    const wchar_t* groupNames[8] = {
        L"PENDING REQUESTS", L"FAVORITES", L"IN-GAME", L"ONLINE", L"AWAY",
        L"OFFLINE", L"REQUEST SENT", L"BLOCKED"
    };

    // Presence rank for within-group ordering (favorites group mixes states).
    auto presRank = [](int p) { switch (p) { case 5: return 0; case 1: return 1;
        case 2: case 3: return 2; default: return 3; } };

    float y = listTop + 6.0f - state.friendsScroll;
    int hoveredId = state.hoveredFriendId;

    for (int g = 0; g < 8; ++g) {
        if (groupNames[g][0] == L'\0') continue;
        // Collect this group's members (honoring the active filter).
        std::vector<const FriendRowView*> rows;
        for (const auto& f : state.friends)
            if (bucket(f) == g && matches(f)) rows.push_back(&f);
        if (rows.empty()) continue;

        // Order: A–Z (default) or recently-interacted. Favorites group keeps
        // presence as the primary key so in-game friends surface first.
        std::sort(rows.begin(), rows.end(), [&](const FriendRowView* a, const FriendRowView* b) {
            auto disp = [](const FriendRowView* f) {
                return f->nickname.empty() ? f->username : f->nickname; };
            if (g == 1) { int ra = presRank(a->presence), rb = presRank(b->presence);
                          if (ra != rb) return ra < rb; }
            if (state.friendsSortMode == 1 && a->lastInteract != b->lastInteract)
                return a->lastInteract > b->lastInteract;   // most recent first
            std::wstring da = disp(a), db = disp(b);
            for (auto& c : da) c = towlower(c);
            for (auto& c : db) c = towlower(c);
            return da < db;
        });

        // Collapsed state (ignored while filtering so matches always show).
        bool collapsed = !filtering && (state.friendsCollapsedMask & (1u << g));

        // Group header (chevron + label + count). Clickable to collapse/expand.
        m_friendGroupHdrRects.push_back({ D2D1::RectF(panelL, y - 2.0f, w, y + 20.0f), g });
        const wchar_t* chev = collapsed ? L"\xE76C" : L"\xE70D";   // ChevronRight / ChevronDown
        m_rt->DrawText(chev, 1, m_fmtSmall.Get(),
                       D2D1::RectF(panelL + pad, y, panelL + pad + 14, y + 18), m_brushSubtext.Get());
        D2D1_RECT_F ghLbl = D2D1::RectF(panelL + pad + 16, y, w - pad, y + 20.0f);
        std::wstring hdrTxt = std::wstring(groupNames[g]) + L"  (" + std::to_wstring(rows.size()) + L")";
        m_rt->DrawText(hdrTxt.c_str(), (UINT32)hdrTxt.size(), m_fmtSmall.Get(), ghLbl, m_brushSubtext.Get());
        y += 24.0f;

        if (collapsed) { y += 4.0f; continue; }

        for (const FriendRowView* fp : rows) {
            const FriendRowView& f = *fp;
            float rowH = 44.0f;
            D2D1_RECT_F row = D2D1::RectF(panelL + 6.0f, y, w - 6.0f, y + rowH);
            bool hovered = ((int64_t)f.accountId == (int64_t)hoveredId);
            if (hovered) {
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(row, 6, 6), m_brushCardHover.Get());
            }
            m_friendRowRects.push_back({ row, f.accountId });

            // Avatar placeholder circle with first initial + presence dot.
            float cx = panelL + 26.0f, cy = y + rowH * 0.5f;
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 15.0f, 15.0f), m_brushCard.Get());
            if (!f.username.empty()) {
                std::wstring initial(1, towupper(f.username[0]));
                D2D1_RECT_F ir = D2D1::RectF(cx - 15, cy - 11, cx + 15, cy + 11);
                m_rt->DrawText(initial.c_str(), 1, m_fmtCard.Get(), ir, m_brushSubtext.Get());
            }
            // Presence dot (bottom-right of avatar).
            m_brushWhite->SetColor(PresenceColor(f.presence));
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 11.0f, cy + 11.0f), 5.0f, 5.0f),
                              m_brushSidebar.Get());
            m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 11.0f, cy + 11.0f), 3.5f, 3.5f),
                              m_brushWhite.Get());
            m_brushWhite->SetColor(C_WHITE);

            // Display name (nickname overrides username) + status/game.
            float tx = panelL + 50.0f;
            const std::wstring& disp = f.nickname.empty() ? f.username : f.nickname;
            D2D1_RECT_F nameR = D2D1::RectF(tx, y + 5.0f, w - 30.0f, y + 24.0f);
            m_rt->DrawText(disp.c_str(), (UINT32)disp.size(),
                           m_fmtCard.Get(), nameR, m_brushText.Get());
            // Favorite star (gold) at the row's right edge for accepted friends.
            if (f.favorite && f.relation != 2) {
                m_brushWhite->SetColor(D2D1::ColorF(0xE3B341));
                D2D1_RECT_F starR = D2D1::RectF(w - 28.0f, y + 4.0f, w - 10.0f, y + 22.0f);
                m_rt->DrawText(L"\xE735", 1, m_fmtSmall.Get(), starR, m_brushWhite.Get());
                m_brushWhite->SetColor(C_WHITE);
            }

            std::wstring sub;
            if (f.relation == 2)      sub = L"wants to be friends";
            else if (f.relation == 1) sub = L"request sent";
            else if (f.relation == 4) sub = L"blocked";
            else if (f.presence == 5) sub = f.gameTitle.empty() ? L"In-Game" : f.gameTitle;
            else if (f.presence == 1) sub = L"Online";
            else if (f.presence == 2) sub = L"Away";
            else if (f.presence == 3) sub = L"Busy";
            else                       sub = L"Offline";
            D2D1_RECT_F subR = D2D1::RectF(tx, y + 23.0f, w - 30.0f, y + 42.0f);
            ID2D1Brush* subBrush = (f.presence == 5) ? m_brushAccent.Get() : m_brushSubtext.Get();
            if (f.presence == 5) m_brushAccent->SetColor(D2D1::ColorF(0x57AB5A));
            m_rt->DrawText(sub.c_str(), (UINT32)sub.size(), m_fmtCardSub.Get(), subR, subBrush);
            if (f.presence == 5) m_brushAccent->SetColor(C_ACCENT);

            // Incoming friend request: inline Accept (✓) / Decline (✕) buttons.
            if (f.relation == 2) {
                float bs = 28.0f;                 // button size
                float by = cy - bs * 0.5f;
                D2D1_RECT_F decl = D2D1::RectF(w - 6.0f - bs, by, w - 6.0f, by + bs);
                D2D1_RECT_F acc  = D2D1::RectF(decl.left - 6.0f - bs, by, decl.left - 6.0f, by + bs);
                // Accept (green).
                m_brushWhite->SetColor(D2D1::ColorF(0x238636));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(acc, 6, 6), m_brushWhite.Get());
                m_brushWhite->SetColor(C_WHITE);
                m_rt->DrawText(L"\xE73E", 1, m_fmtIcon.Get(), acc, m_brushWhite.Get()); // ✓ glyph
                // Decline (red).
                m_brushWhite->SetColor(D2D1::ColorF(0xDA3633));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(decl, 6, 6), m_brushWhite.Get());
                m_brushWhite->SetColor(C_WHITE);
                m_rt->DrawText(L"\xE711", 1, m_fmtIcon.Get(), decl, m_brushWhite.Get()); // ✕ glyph
                m_friendAcceptRects.push_back({ acc, f.accountId });
                m_friendDeclineRects.push_back({ decl, f.accountId });
            }

            // Unread badge on the right.
            if (f.relation != 2 && f.unread > 0) {
                std::wstring n = std::to_wstring(f.unread);
                D2D1_POINT_2F bc = D2D1::Point2F(w - 24.0f, cy);
                m_brushAccent->SetColor(D2D1::ColorF(0xE5534B));
                m_rt->FillEllipse(D2D1::Ellipse(bc, 9.0f, 9.0f), m_brushAccent.Get());
                m_brushAccent->SetColor(C_ACCENT);
                D2D1_RECT_F br = D2D1::RectF(bc.x - 9, bc.y - 9, bc.x + 9, bc.y + 9);
                m_rt->DrawText(n.c_str(), (UINT32)n.size(), m_fmtCardSub.Get(), br, m_brushWhite.Get());
            }
            y += rowH + 2.0f;
        }
        y += 8.0f;
    }

    // Empty state — context-aware (no matches / connecting / genuinely empty).
    bool showEmpty = m_friendGroupHdrRects.empty() || (filtering && m_friendRowRects.empty());
    if (showEmpty) {
        std::wstring msgStr;
        if (filtering) msgStr = L"No friends match “" + state.friendsFilter + L"”.";
        else {
            const wchar_t* m =
                state.gatewayState == 2 ? L"No friends yet.\nUse + to add someone by username."
              : state.gatewayState == 1 ? L"Connecting to social…"
              : state.gatewayState == 3 ? L"Reconnecting…\nYour friends will appear shortly."
                                         : L"Social is offline.\nCheck your server connection.";
            msgStr = m;
        }
        D2D1_RECT_F er = D2D1::RectF(panelL + pad, listTop + 30.0f, w - pad, listTop + 90.0f);
        m_rt->DrawText(msgStr.c_str(), (UINT32)msgStr.size(), m_fmtSummary.Get(), er, m_brushSubtext.Get());
    }

    m_rt->PopAxisAlignedClip();
    m_friendsContentH = (y + state.friendsScroll) - listTop;
}

// ── Toast notifications ───────────────────────────────────────────────────────

// Per-kind accent color + Segoe MDL2 glyph for toasts and history rows.
static D2D1_COLOR_F NotifAccent(int kind) {
    switch (kind) {
        case 0: return D2D1::ColorF(0x3FB950); // FriendRequest  green
        case 1: return D2D1::ColorF(0x3FB950); // FriendAccepted green
        case 2: return D2D1::ColorF(0x58A6FF); // FriendOnline   blue
        case 3: return D2D1::ColorF(0x57AB5A); // FriendInGame   game-green
        case 4: return D2D1::ColorF(0x8957E5); // Message        purple
        case 5: return D2D1::ColorF(0xD29922); // VoiceInvite    amber
        default: return D2D1::ColorF(0x6E7681);
    }
}
static const wchar_t* NotifGlyph(int kind) {
    switch (kind) {
        case 0: return L"\xE8FA"; // add friend
        case 1: return L"\xE8FB"; // accept
        case 2: return L"\xE768"; // play/online
        case 3: return L"\xE7FC"; // game
        case 4: return L"\xE8BD"; // message
        case 5: return L"\xE717"; // phone
        default: return L"\xE7E7"; // bell
    }
}

// Wall-clock epoch milliseconds (matches SocialManager's NowMs).
static int64_t NowEpochMs() {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime; // 100ns since 1601
    return (int64_t)(t / 10000ULL) - 11644473600000LL;                    // → ms since 1970
}
// "now", "5m", "3h", "2d" — compact relative age for the history dropdown.
static std::wstring RelativeAge(int64_t tsMs) {
    int64_t s = (NowEpochMs() - tsMs) / 1000;
    if (s < 5)      return L"now";
    if (s < 60)     return std::to_wstring(s) + L"s";
    if (s < 3600)   return std::to_wstring(s / 60) + L"m";
    if (s < 86400)  return std::to_wstring(s / 3600) + L"h";
    return std::to_wstring(s / 86400) + L"d";
}

void Renderer::PushToast(int kind, uint64_t accountId,
                         const std::wstring& title, const std::wstring& body) {
    ActiveToast t;
    t.kind = kind; t.accountId = accountId;
    t.title = title; t.body = body;
    t.spawnTick = GetTickCount64();
    // Cap simultaneous toasts; drop the oldest non-dismissing one.
    if (m_toasts.size() >= 4 && !m_toasts.empty()) m_toasts.erase(m_toasts.begin());
    m_toasts.push_back(std::move(t));
}

void Renderer::DismissToast(uint64_t accountId, int kind) {
    uint64_t now = GetTickCount64();
    for (auto& t : m_toasts)
        if (t.accountId == accountId && t.kind == kind && !t.dismissing) {
            t.dismissing = true; t.dismissTick = now; break;
        }
}

bool Renderer::UpdateToasts(float mouseX, float mouseY) {
    uint64_t now = GetTickCount64();
    uint64_t delta = m_lastToastTick ? std::min<uint64_t>(now - m_lastToastTick, 250) : 0;
    m_lastToastTick = now;
    for (auto it = m_toasts.begin(); it != m_toasts.end(); ) {
        ActiveToast& t = *it;
        // Hover-pause: while the cursor is over a settled toast, push its spawn
        // tick forward so the hold timer freezes (a manual dismiss still runs).
        bool hovered = mouseX >= t.rect.left && mouseX <= t.rect.right &&
                       mouseY >= t.rect.top  && mouseY <= t.rect.bottom;
        if (hovered && !t.dismissing && (now - t.spawnTick) > 200)
            t.spawnTick += delta;
        uint64_t fadeOut = t.dismissing ? t.dismissTick : (t.spawnTick + t.lifeMs);
        if (now >= fadeOut + 280) it = m_toasts.erase(it);
        else ++it;
    }
    return !m_toasts.empty();
}

void Renderer::DrawToasts() {
    if (m_toasts.empty()) return;
    uint64_t now = GetTickCount64();
    const float cardW = 320.0f, cardH = 64.0f, gap = 10.0f, margin = 18.0f;
    float right = (float)m_width - margin;
    float y = (float)m_height - margin - cardH;

    // Draw newest at the bottom, older stacking upward.
    for (auto it = m_toasts.rbegin(); it != m_toasts.rend(); ++it) {
        ActiveToast& t = *it;
        // Slide-in (first 200ms) and fade-out (last 280ms) easing.
        float inA = std::min(1.0f, (now - t.spawnTick) / 200.0f);
        uint64_t fadeStart = t.dismissing ? t.dismissTick : (t.spawnTick + t.lifeMs);
        float outA = now >= fadeStart ? std::min(1.0f, (now - fadeStart) / 280.0f) : 0.0f;
        float alpha = inA * (1.0f - outA);
        float slide = (1.0f - inA) * 40.0f + outA * 40.0f;  // ease from/to the right
        float left = right - cardW + slide;
        D2D1_RECT_F card = D2D1::RectF(left, y, left + cardW, y + cardH);
        t.rect = card;

        // Card shadow + fill + left accent bar.
        m_brushWhite->SetColor(D2D1::ColorF(0, 0.35f * alpha));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(card.left + 3, card.top + 4, card.right + 3, card.bottom + 4), 8, 8),
            m_brushWhite.Get());
        m_brushWhite->SetColor(C_WHITE);
        ID2D1SolidColorBrush* bg = m_brushCard.Get();
        float prevBgA = bg->GetOpacity(); bg->SetOpacity(alpha);
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(card, 8, 8), bg);
        bg->SetOpacity(prevBgA);

        D2D1_COLOR_F ac = NotifAccent(t.kind); ac.a = alpha;
        m_brushWhite->SetColor(ac);
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(card.left, card.top + 6, card.left + 4, card.bottom - 6), 2, 2),
            m_brushWhite.Get());
        // Glyph chip.
        D2D1_RECT_F gr = D2D1::RectF(card.left + 12, card.top, card.left + 48, card.bottom);
        m_rt->DrawText(NotifGlyph(t.kind), 1, m_fmtIcon.Get(), gr, m_brushWhite.Get());
        m_brushWhite->SetColor(C_WHITE);

        float prevTA = m_brushText->GetOpacity(); m_brushText->SetOpacity(alpha);
        float prevSA = m_brushSubtext->GetOpacity(); m_brushSubtext->SetOpacity(alpha);
        D2D1_RECT_F tr = D2D1::RectF(card.left + 52, card.top + 9, card.right - 28, card.top + 30);
        m_rt->DrawText(t.title.c_str(), (UINT32)t.title.size(), m_fmtCard.Get(), tr, m_brushText.Get());
        D2D1_RECT_F br = D2D1::RectF(card.left + 52, card.top + 31, card.right - 12, card.bottom - 8);
        m_rt->DrawText(t.body.c_str(), (UINT32)t.body.size(), m_fmtCardSub.Get(), br, m_brushSubtext.Get());
        // Dismiss ✕.
        t.closeRect = D2D1::RectF(card.right - 26, card.top + 6, card.right - 6, card.top + 26);
        m_rt->DrawText(L"\xE711", 1, m_fmtSmall.Get(), t.closeRect, m_brushSubtext.Get());
        m_brushText->SetOpacity(prevTA);
        m_brushSubtext->SetOpacity(prevSA);

        y -= cardH + gap;
    }
}

Renderer::ToastHit Renderer::HitTestToasts(float x, float y) const {
    ToastHit hit;
    for (const auto& t : m_toasts) {
        const D2D1_RECT_F& c = t.closeRect;
        if (x >= c.left && x <= c.right && y >= c.top && y <= c.bottom) {
            hit.kind = ToastHit::Dismiss; hit.accountId = t.accountId; hit.toastKind = t.kind;
            return hit;
        }
    }
    for (const auto& t : m_toasts) {
        const D2D1_RECT_F& c = t.rect;
        if (x >= c.left && x <= c.right && y >= c.top && y <= c.bottom) {
            hit.kind = ToastHit::Action; hit.accountId = t.accountId; hit.toastKind = t.kind;
            return hit;
        }
    }
    return hit;
}

// ── Notifications history dropdown ────────────────────────────────────────────

bool Renderer::HitTestBellBtn(float x, float y) const {
    return x >= m_bellBtnRect.left && x <= m_bellBtnRect.right &&
           y >= m_bellBtnRect.top  && y <= m_bellBtnRect.bottom;
}

bool Renderer::PointInNotifPanel(float x, float y) const {
    return x >= m_notifPanelRect.left && x <= m_notifPanelRect.right &&
           y >= m_notifPanelRect.top  && y <= m_notifPanelRect.bottom;
}

void Renderer::DrawNotifPanel(RenderState& state) {
    float w = (float)m_width;
    float panelW = 340.0f, panelH = std::min(440.0f, (float)m_height - m_topbarH - 24.0f);
    float pl = w - panelW - 14.0f, pt = m_topbarH + 4.0f;
    D2D1_RECT_F panel = D2D1::RectF(pl, pt, pl + panelW, pt + panelH);
    m_notifPanelRect = panel;

    // Shadow + body.
    m_brushWhite->SetColor(D2D1::ColorF(0, 0.4f));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(
        D2D1::RectF(pl + 4, pt + 5, pl + panelW + 4, pt + panelH + 5), 10, 10), m_brushWhite.Get());
    m_brushWhite->SetColor(C_WHITE);
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(panel, 10, 10), m_brushSidebar.Get());
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(panel, 10, 10), m_brushCard.Get(), 1.0f);

    // Header with title + actions.
    float pad = 14.0f;
    D2D1_RECT_F hdr = D2D1::RectF(pl + pad, pt + 10, pl + panelW - pad, pt + 34);
    m_rt->DrawText(L"Notifications", 13, m_fmtHeading.Get(), hdr, m_brushText.Get());
    m_notifClearRect   = D2D1::RectF(pl + panelW - 58, pt + 12, pl + panelW - pad, pt + 32);
    m_notifMarkAllRect = D2D1::RectF(pl + panelW - 132, pt + 12, pl + panelW - 64, pt + 32);
    m_rt->DrawText(L"Read", 4, m_fmtSmall.Get(), m_notifMarkAllRect, m_brushAccent.Get());
    m_rt->DrawText(L"Clear", 5, m_fmtSmall.Get(), m_notifClearRect, m_brushSubtext.Get());

    float footerH = 38.0f;
    float listTop = pt + 42.0f, listBot = pt + panelH - footerH;
    m_rt->PushAxisAlignedClip(D2D1::RectF(pl, listTop, pl + panelW, listBot),
                              D2D1_ANTIALIAS_MODE_ALIASED);
    m_notifRowRects.clear();
    float y = listTop + 4.0f - state.notifScroll;

    if (state.notifs.empty()) {
        D2D1_RECT_F er = D2D1::RectF(pl + pad, listTop + 30, pl + panelW - pad, listTop + 70);
        m_rt->DrawText(L"You're all caught up.", 21, m_fmtSummary.Get(), er, m_brushSubtext.Get());
    }
    for (const auto& n : state.notifs) {
        float rowH = 56.0f;
        D2D1_RECT_F row = D2D1::RectF(pl + 6, y, pl + panelW - 6, y + rowH);
        if (!n.read) {
            D2D1_COLOR_F hl = NotifAccent(n.kind); hl.a = 0.10f;
            m_brushWhite->SetColor(hl);
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(row, 6, 6), m_brushWhite.Get());
            m_brushWhite->SetColor(C_WHITE);
        }
        m_notifRowRects.push_back({ row, n.accountId });
        // Glyph.
        D2D1_COLOR_F ac = NotifAccent(n.kind);
        m_brushWhite->SetColor(ac);
        D2D1_RECT_F gr = D2D1::RectF(pl + 12, y + 6, pl + 44, y + 38);
        m_rt->DrawText(NotifGlyph(n.kind), 1, m_fmtIcon.Get(), gr, m_brushWhite.Get());
        m_brushWhite->SetColor(C_WHITE);
        // Title + relative timestamp (right) + body.
        std::wstring age = RelativeAge(n.ts);
        D2D1_RECT_F ageR = D2D1::RectF(pl + panelW - 52, y + 8, pl + panelW - 12, y + 26);
        m_rt->DrawText(age.c_str(), (UINT32)age.size(), m_fmtCardSub.Get(), ageR, m_brushSubtext.Get());
        D2D1_RECT_F tr = D2D1::RectF(pl + 48, y + 7, pl + panelW - 56, y + 28);
        m_rt->DrawText(n.title.c_str(), (UINT32)n.title.size(), m_fmtCard.Get(), tr, m_brushText.Get());
        D2D1_RECT_F br = D2D1::RectF(pl + 48, y + 28, pl + panelW - 12, y + rowH - 4);
        m_rt->DrawText(n.body.c_str(), (UINT32)n.body.size(), m_fmtCardSub.Get(), br, m_brushSubtext.Get());
        y += rowH + 2.0f;
    }
    m_rt->PopAxisAlignedClip();
    m_notifContentH = (y + state.notifScroll) - listTop;

    // Footer: settings toggles (Sound, Online alerts). Pill = on (accent).
    m_rt->DrawLine(D2D1::Point2F(pl, listBot), D2D1::Point2F(pl + panelW, listBot),
                   m_brushCard.Get(), 1.0f);
    float fy = listBot + 7.0f, fyb = fy + 22.0f;
    auto pill = [&](D2D1_RECT_F& rect, float left, const wchar_t* label, bool on) {
        float tw = 64.0f;
        rect = D2D1::RectF(left, fy, left + tw, fyb);
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(rect, 11, 11),
                                   on ? m_brushAccent.Get() : m_brushCard.Get());
        m_rt->DrawText(label, (UINT32)wcslen(label), m_fmtCardSub.Get(), rect,
                       on ? m_brushWhite.Get() : m_brushSubtext.Get());
    };
    pill(m_notifSoundRect,  pl + pad,        L"Sound",  state.notifSoundOn);
    pill(m_notifOnlineRect, pl + pad + 72.0f, L"Online", state.notifOnlineAlerts);
}

Renderer::NotifHit Renderer::HitTestNotifPanel(float x, float y) const {
    NotifHit hit;
    auto in = [&](const D2D1_RECT_F& r) {
        return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    };
    if (in(m_notifMarkAllRect)) { hit.kind = NotifHit::MarkAll; return hit; }
    if (in(m_notifClearRect))   { hit.kind = NotifHit::Clear;   return hit; }
    if (in(m_notifSoundRect))   { hit.kind = NotifHit::ToggleSound;  return hit; }
    if (in(m_notifOnlineRect))  { hit.kind = NotifHit::ToggleOnline; return hit; }
    for (const auto& rr : m_notifRowRects)
        if (in(rr.first)) { hit.kind = NotifHit::Row; hit.accountId = rr.second; return hit; }
    return hit;
}

// ── Direct-message chat window ────────────────────────────────────────────────

float Renderer::MaxChatScroll(const RenderState& s) const {
    // listView height is derived in DrawChatWindow; recompute the same geometry.
    float winH = std::min(600.0f, (float)m_height - 100.0f);
    float listH = winH - 48.0f - 56.0f;   // minus header + input bar
    return std::max(0.0f, m_chatContentH - listH);
}

bool Renderer::PointInChatWindow(float x, float y) const {
    return x >= m_chatRect.left && x <= m_chatRect.right &&
           y >= m_chatRect.top  && y <= m_chatRect.bottom;
}

Renderer::ChatHit Renderer::HitTestChatWindow(float x, float y) const {
    auto in = [&](const D2D1_RECT_F& r) {
        return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    };
    ChatHit h;
    if (in(m_chatCloseRect))   { h.kind = ChatHit::Close;   return h; }
    if (in(m_chatAcceptRect))  { h.kind = ChatHit::Accept;  return h; }
    if (in(m_chatDeclineRect)) { h.kind = ChatHit::Decline; return h; }
    if (in(m_chatEndRect))     { h.kind = ChatHit::EndCall; return h; }
    if (in(m_chatMuteRect))    { h.kind = ChatHit::Mute;    return h; }
    if (in(m_chatCallRect))    { h.kind = ChatHit::Call;    return h; }
    if (in(m_chatSendRect))    { h.kind = ChatHit::Send;    return h; }
    if (in(m_chatInputRect))   { h.kind = ChatHit::Input;   return h; }
    return h;
}

void Renderer::DrawChatWindow(RenderState& state) {
    float winW = std::min(480.0f, (float)m_width - 60.0f);
    float winH = std::min(600.0f, (float)m_height - 100.0f);
    float left = ((float)m_width - winW) * 0.5f;
    float top  = m_topbarH + ((float)m_height - m_topbarH - winH) * 0.5f;
    D2D1_RECT_F win = D2D1::RectF(left, top, left + winW, top + winH);
    m_chatRect = win;

    // Dim the rest of the screen so the conversation reads as a focused surface.
    m_rt->FillRectangle(D2D1::RectF(0, m_topbarH, (float)m_width, (float)m_height),
                        m_brushOverlay.Get());

    m_rt->FillRoundedRectangle(D2D1::RoundedRect(win, 10, 10), m_brushSidebar.Get());
    m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, 0.35f));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(win, 10, 10), m_brushAccent.Get(), 1.0f);
    m_brushAccent->SetColor(C_ACCENT);

    // ── Header ────────────────────────────────────────────────────────────────
    float hdrH = 48.0f;
    D2D1_RECT_F hdr = D2D1::RectF(left, top, left + winW, top + hdrH);
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left, top, left + winW, top + hdrH + 8),
                                                 10, 10), m_brushCard.Get());

    // Peer avatar initial + presence dot.
    float cx = left + 28.0f, cy = top + hdrH * 0.5f;
    m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 15.0f, 15.0f), m_brushSidebar.Get());
    if (!state.chatPeerName.empty()) {
        std::wstring initial(1, towupper(state.chatPeerName[0]));
        D2D1_RECT_F ir = D2D1::RectF(cx - 15, cy - 11, cx + 15, cy + 11);
        m_rt->DrawText(initial.c_str(), 1, m_fmtCard.Get(), ir, m_brushSubtext.Get());
    }
    m_brushWhite->SetColor(PresenceColor(state.chatPeerPresence));
    m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 11.0f, cy + 11.0f), 5.0f, 5.0f), m_brushCard.Get());
    m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + 11.0f, cy + 11.0f), 3.5f, 3.5f), m_brushWhite.Get());
    m_brushWhite->SetColor(C_WHITE);

    D2D1_RECT_F nameR = D2D1::RectF(left + 50.0f, top + 7.0f, left + winW - 90.0f, top + 27.0f);
    m_rt->DrawText(state.chatPeerName.c_str(), (UINT32)state.chatPeerName.size(),
                   m_fmtCard.Get(), nameR, m_brushText.Get());
    const wchar_t* psub = state.chatPeerPresence == 5 ? L"In-Game"
                        : state.chatPeerPresence == 1 ? L"Online"
                        : state.chatPeerPresence == 2 ? L"Away"
                        : state.chatPeerPresence == 3 ? L"Busy" : L"Offline";
    D2D1_RECT_F psubR = D2D1::RectF(left + 50.0f, top + 25.0f, left + winW - 90.0f, top + 44.0f);
    m_rt->DrawText(psub, (UINT32)wcslen(psub), m_fmtCardSub.Get(), psubR, m_brushSubtext.Get());

    // Call + close buttons.
    bool callActive = (state.voicePeer == state.chatPeerId) && state.voiceState != 0;
    m_chatCallRect  = D2D1::RectF(left + winW - 78.0f, top + 10.0f, left + winW - 48.0f, top + 38.0f);
    m_chatCloseRect = D2D1::RectF(left + winW - 40.0f, top + 10.0f, left + winW - 10.0f, top + 38.0f);
    if (!callActive) {
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(m_chatCallRect, 6, 6), m_brushSidebar.Get());
        m_rt->DrawText(L"\xE717", 1, m_fmtIcon.Get(), m_chatCallRect, m_brushAccent.Get()); // phone
    } else {
        m_chatCallRect = D2D1::RectF(0, 0, 0, 0); // hidden while in-call (controls in strip)
    }
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(m_chatCloseRect, 6, 6), m_brushSidebar.Get());
    m_rt->DrawText(L"\xE711", 1, m_fmtIcon.Get(), m_chatCloseRect, m_brushSubtext.Get()); // close

    float listTop = top + hdrH + 8.0f;

    // ── In-call / incoming strip ────────────────────────────────────────────────
    m_chatMuteRect = m_chatEndRect = m_chatAcceptRect = m_chatDeclineRect = D2D1::RectF(0, 0, 0, 0);
    if (callActive) {
        float stripH = 40.0f;
        D2D1_RECT_F strip = D2D1::RectF(left + 8, listTop, left + winW - 8, listTop + stripH);
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(strip, 6, 6), m_brushCard.Get());
        bool incoming = state.voiceState == 2;  // Negotiating == ringing (callee side)
        const wchar_t* label = incoming ? L"Incoming voice call…"
                             : state.voiceState == 3 ? L"●  Voice connected"
                             : L"Calling…";
        D2D1_RECT_F lr = D2D1::RectF(left + 20, listTop + 10, left + winW - 160, listTop + 32);
        if (state.voiceState == 3) m_brushAccent->SetColor(D2D1::ColorF(0x57AB5A));
        m_rt->DrawText(label, (UINT32)wcslen(label), m_fmtCard.Get(), lr,
                       state.voiceState == 3 ? m_brushAccent.Get() : m_brushText.Get());
        if (state.voiceState == 3) m_brushAccent->SetColor(C_ACCENT);

        if (incoming) {
            m_chatAcceptRect  = D2D1::RectF(left + winW - 150, listTop + 6, left + winW - 84, listTop + 34);
            m_chatDeclineRect = D2D1::RectF(left + winW - 78,  listTop + 6, left + winW - 14, listTop + 34);
            m_brushAccent->SetColor(D2D1::ColorF(0x2EA043));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(m_chatAcceptRect, 6, 6), m_brushAccent.Get());
            m_brushAccent->SetColor(C_ACCENT);
            m_rt->DrawText(L"Accept", 6, m_fmtCardSub.Get(), m_chatAcceptRect, m_brushWhite.Get());
            m_brushAccent->SetColor(D2D1::ColorF(0xDA3633));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(m_chatDeclineRect, 6, 6), m_brushAccent.Get());
            m_brushAccent->SetColor(C_ACCENT);
            m_rt->DrawText(L"Decline", 7, m_fmtCardSub.Get(), m_chatDeclineRect, m_brushWhite.Get());
        } else {
            m_chatMuteRect = D2D1::RectF(left + winW - 150, listTop + 6, left + winW - 84, listTop + 34);
            m_chatEndRect  = D2D1::RectF(left + winW - 78,  listTop + 6, left + winW - 14, listTop + 34);
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(m_chatMuteRect, 6, 6), m_brushSidebar.Get());
            m_rt->DrawText(state.voiceMuted ? L"Unmute" : L"Mute",
                           state.voiceMuted ? 6 : 4, m_fmtCardSub.Get(), m_chatMuteRect,
                           state.voiceMuted ? m_brushAccent.Get() : m_brushText.Get());
            m_brushAccent->SetColor(D2D1::ColorF(0xDA3633));
            m_rt->FillRoundedRectangle(D2D1::RoundedRect(m_chatEndRect, 6, 6), m_brushAccent.Get());
            m_brushAccent->SetColor(C_ACCENT);
            m_rt->DrawText(L"End", 3, m_fmtCardSub.Get(), m_chatEndRect, m_brushWhite.Get());
        }
        listTop += stripH + 6.0f;
    }

    // ── Message list ────────────────────────────────────────────────────────────
    float inputH = 48.0f;
    float listBottom = top + winH - inputH;
    m_rt->PushAxisAlignedClip(D2D1::RectF(left, listTop, left + winW, listBottom),
                              D2D1_ANTIALIAS_MODE_ALIASED);

    float bubbleMax = winW * 0.66f;
    float pad = 12.0f;
    // First measure total content height for bottom anchoring.
    float total = 6.0f;
    std::vector<float> heights;
    heights.reserve(state.chatMessages.size());
    for (const auto& m : state.chatMessages) {
        float h = DrawWrapped(m.text, m_fmtSummary.Get(), 0, 0, bubbleMax - 20.0f, m_brushText.Get(), false);
        h = std::max(h, 18.0f) + 14.0f;   // bubble padding
        heights.push_back(h);
        total += h + 6.0f;
    }
    if (state.chatPeerTyping) total += 22.0f;
    m_chatContentH = total;

    float listViewH = listBottom - listTop;
    // Anchor to bottom: start so the last message sits just above the input.
    float startY = listTop + std::min(0.0f, listViewH - total) + 6.0f + state.chatScroll;
    float y = startY;
    for (size_t i = 0; i < state.chatMessages.size(); ++i) {
        const auto& m = state.chatMessages[i];
        float h = heights[i];
        float bw = bubbleMax;
        D2D1_RECT_F bub;
        if (m.mine)
            bub = D2D1::RectF(left + winW - pad - bw, y, left + winW - pad, y + h);
        else
            bub = D2D1::RectF(left + pad, y, left + pad + bw, y + h);
        // Only draw if visible.
        if (y + h >= listTop && y <= listBottom) {
            if (m.mine) {
                m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b,
                                                     m.pending ? 0.45f : 0.85f));
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(bub, 8, 8), m_brushAccent.Get());
                m_brushAccent->SetColor(C_ACCENT);
                DrawWrapped(m.text, m_fmtSummary.Get(), bub.left + 10, y + 7, bw - 20.0f,
                            m_brushWhite.Get(), true);
            } else {
                m_rt->FillRoundedRectangle(D2D1::RoundedRect(bub, 8, 8), m_brushCard.Get());
                DrawWrapped(m.text, m_fmtSummary.Get(), bub.left + 10, y + 7, bw - 20.0f,
                            m_brushText.Get(), true);
            }
        }
        y += h + 6.0f;
    }
    if (state.chatPeerTyping) {
        D2D1_RECT_F tr = D2D1::RectF(left + pad, y, left + winW - pad, y + 20.0f);
        std::wstring t = state.chatPeerName + L" is typing…";
        m_rt->DrawText(t.c_str(), (UINT32)t.size(), m_fmtCardSub.Get(), tr, m_brushSubtext.Get());
    }
    m_rt->PopAxisAlignedClip();

    // Empty conversation hint.
    if (state.chatMessages.empty() && !state.chatPeerTyping) {
        D2D1_RECT_F er = D2D1::RectF(left + pad, listTop + 16, left + winW - pad, listTop + 60);
        std::wstring hint = L"Say hi to " + state.chatPeerName + L".";
        m_rt->DrawText(hint.c_str(), (UINT32)hint.size(), m_fmtSummary.Get(), er, m_brushSubtext.Get());
    }

    // ── Input bar ───────────────────────────────────────────────────────────────
    float ib = top + winH - inputH;
    m_chatSendRect  = D2D1::RectF(left + winW - 70.0f, ib + 8.0f, left + winW - 10.0f, ib + inputH - 8.0f);
    m_chatInputRect = D2D1::RectF(left + 10.0f, ib + 8.0f, m_chatSendRect.left - 8.0f, ib + inputH - 8.0f);
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(m_chatInputRect, 6, 6), m_brushCard.Get());
    m_brushAccent->SetColor(D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, 0.5f));
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(m_chatInputRect, 6, 6), m_brushAccent.Get(), 1.0f);
    m_brushAccent->SetColor(C_ACCENT);

    D2D1_RECT_F tr = D2D1::RectF(m_chatInputRect.left + 10, m_chatInputRect.top,
                                 m_chatInputRect.right - 8, m_chatInputRect.bottom);
    bool blink = ((GetTickCount64() / 500) % 2) == 0;
    std::wstring shown = state.chatInput;
    if (shown.empty()) {
        m_rt->DrawText(L"Type a message…", 15, m_fmtSearch.Get(), tr, m_brushSubtext.Get());
    } else {
        if (blink) shown += L"|";
        m_rt->DrawText(shown.c_str(), (UINT32)shown.size(), m_fmtSearch.Get(), tr, m_brushText.Get());
    }

    bool canSend = !state.chatInput.empty();
    m_brushAccent->SetColor(canSend ? C_ACCENT
                                    : D2D1::ColorF(C_ACCENT.r, C_ACCENT.g, C_ACCENT.b, 0.35f));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(m_chatSendRect, 6, 6), m_brushAccent.Get());
    m_brushAccent->SetColor(C_ACCENT);
    m_rt->DrawText(L"Send", 4, m_fmtCardSub.Get(), m_chatSendRect, m_brushWhite.Get());
}

void Renderer::ClearAvatar() { m_avatar.Reset(); }

void Renderer::SetAvatarFromMemory(const void* data, size_t size) {
    if (!data || size == 0 || !m_rt || !m_wic) { m_avatar.Reset(); return; }

    ComPtr<IWICStream>            stream;
    ComPtr<IWICBitmapDecoder>     dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter>   conv;

    if (FAILED(m_wic->CreateStream(stream.GetAddressOf()))) return;
    if (FAILED(stream->InitializeFromMemory((BYTE*)data, (DWORD)size))) return;
    if (FAILED(m_wic->CreateDecoderFromStream(stream.Get(), nullptr,
               WICDecodeMetadataCacheOnLoad, dec.GetAddressOf()))) return;
    if (FAILED(dec->GetFrame(0, frame.GetAddressOf()))) return;
    if (FAILED(m_wic->CreateFormatConverter(conv.GetAddressOf()))) return;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
               WICBitmapDitherTypeNone, nullptr, 0.0,
               WICBitmapPaletteTypeMedianCut))) return;

    ComPtr<ID2D1Bitmap> bmp;
    if (SUCCEEDED(m_rt->CreateBitmapFromWicBitmap(conv.Get(), nullptr, bmp.GetAddressOf())))
        m_avatar = std::move(bmp);
}

bool Renderer::HitTestEmptyStateBtn(float x, float y) const {
    if (m_emptyStateBtnRect.right == m_emptyStateBtnRect.left) return false;
    return x >= m_emptyStateBtnRect.left && x <= m_emptyStateBtnRect.right &&
           y >= m_emptyStateBtnRect.top  && y <= m_emptyStateBtnRect.bottom;
}

float Renderer::ScrollForSelected(int idx, float currentScroll, float viewportH) const {
    if (idx < 0 || m_cols <= 0) return currentScroll;
    int row = idx / m_cols;
    float rowH   = m_tileH + m_tileGap + 22.0f;
    // On-screen top of this card:  m_topbarH + m_tileGap + row*rowH - scroll
    float cardTop    = m_topbarH + m_tileGap + (float)row * rowH;
    float cardBottom = cardTop + m_tileH;

    float screenTop    = cardTop    - currentScroll;
    float screenBottom = cardBottom - currentScroll;

    if (screenTop < m_topbarH + 4.0f) {
        // Card is above viewport — scroll up so it appears at top
        return std::max(0.0f, cardTop - m_topbarH - m_tileGap);
    }
    if (screenBottom > viewportH - 4.0f) {
        // Card is below viewport — scroll down so it appears at bottom
        return cardBottom - viewportH + m_tileGap;
    }
    return currentScroll;
}
