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
    float actionsLeft = (float)w - 240.0f;
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
                m_rt->DrawBitmap(icon, D2D1::RectF(iconX, iconY, iconX + iconSz, iconY + iconSz),
                                 active ? 1.0f : 0.65f,
                                 D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
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

void Renderer::DrawPlatformBadge(Platform p, D2D1_POINT_2F center) {
    D2D1_ELLIPSE e = D2D1::Ellipse(center, 13.0f, 13.0f);
    m_brushCard->SetColor(D2D1::ColorF(0.05f, 0.05f, 0.08f, 0.85f));
    m_rt->FillEllipse(e, m_brushCard.Get());

    ID2D1Bitmap* icon = m_platformIcons ? m_platformIcons->Get(p) : nullptr;
    if (icon) {
        D2D1_RECT_F dst = D2D1::RectF(center.x - 9.0f, center.y - 9.0f,
                                      center.x + 9.0f, center.y + 9.0f);
        m_rt->DrawBitmap(icon, dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
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

    auto fact = [&](const std::wstring& s, ID2D1Brush* brush) {
        m_rt->DrawText(s.c_str(), (UINT32)s.size(), m_fmtDetail.Get(),
                       D2D1::RectF(ix, iy, ix + iw, iy + 24), brush);
        iy += 26;
    };

    fact(L"Platform: " + PlatformName(game->platform), m_brushSubtext.Get());

    // Version (server-backed games carry an authoritative content version)
    if (!game->serverVersion.empty())
        fact(L"Version: " + game->serverVersion, m_brushSubtext.Get());

    if (game->serverBacked) {
        std::wstring st = L"Status: ";
        st += game->installState == InstallState::Installed ? L"Installed" :
              game->installState == InstallState::UpdateAvailable ? L"Update available" :
              game->installState == InstallState::Downloading ? L"Downloading" :
              L"Not installed";
        fact(st, m_brushSubtext.Get());
    }

    fact(L"Playtime: " + game->PlaytimeStr(), m_brushSubtext.Get());

    if (game->lastPlayed > 0) {
        SYSTEMTIME st; FILETIME ft;
        LONGLONG ll = (LONGLONG)(game->lastPlayed) * 10000000LL + 116444736000000000LL;
        ft.dwLowDateTime = (DWORD)(ll & 0xFFFFFFFF); ft.dwHighDateTime = (DWORD)(ll >> 32);
        FileTimeToSystemTime(&ft, &st);
        wchar_t dateBuf[64];
        swprintf_s(dateBuf, L"Last played: %04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
        fact(dateBuf, m_brushSubtext.Get());
    }

    if (game->releaseDate > 0) {
        SYSTEMTIME rst; FILETIME rft;
        LONGLONG rll = (LONGLONG)(game->releaseDate) * 10000000LL + 116444736000000000LL;
        rft.dwLowDateTime = (DWORD)(rll & 0xFFFFFFFF); rft.dwHighDateTime = (DWORD)(rll >> 32);
        FileTimeToSystemTime(&rft, &rst);
        fact(L"Released: " + std::to_wstring(rst.wYear), m_brushSubtext.Get());
    }

    if (!game->RatingStr().empty()) {
        m_brushAccent->SetColor(game->RatingColor());
        fact(L"Rating: " + game->RatingStr(), m_brushAccent.Get());
        m_brushAccent->SetColor(C_ACCENT);
    }

    if (!game->genres.empty())
        fact(L"Genres: " + game->genres, m_brushSubtext.Get());

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
