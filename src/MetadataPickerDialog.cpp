#include "pch.h"
#include "MetadataPickerDialog.h"

#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

// ── Helpers ───────────────────────────────────────────────────────────────────

static int TimestampYear(int64_t ts) {
    if (ts <= 0) return 0;
    time_t t = (time_t)ts;
    struct tm tm{};
    gmtime_s(&tm, &t);
    return tm.tm_year + 1900;
}

// Subclass proc: routes Enter key in the edit box to a Search button click.
static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                          UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN)
        SendMessageW(GetParent(hwnd), WM_COMMAND,
                     MAKEWPARAM(MetadataPickerDialog::ID_SEARCH, BN_CLICKED), 0);
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ── Open / Close ──────────────────────────────────────────────────────────────

void MetadataPickerDialog::Open(HWND parent,
                                 const std::wstring& gameId,
                                 const std::wstring& gameTitle,
                                 MetadataManager& mm,
                                 ArtReadyCallback artReady) {
    if (m_hwnd) return;

    m_parent    = parent;
    m_gameId    = gameId;
    m_gameTitle = gameTitle;
    m_mm        = &mm;
    m_artReady  = std::move(artReady);

    // Register window class once
    static bool registered = false;
    if (!registered) {
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&icc);

        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = WNDCLS;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    // Center over parent
    RECT pr;
    GetWindowRect(parent, &pr);
    // W/H are the desired client size; inflate by the frame so the caption and
    // borders don't eat into the right/bottom padding (controls are laid out in
    // client coordinates).
    RECT wr = { 0, 0, W, H };
    AdjustWindowRectEx(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;
    int px = pr.left + (pr.right  - pr.left - winW) / 2;
    int py = pr.top  + (pr.bottom - pr.top  - winH) / 2;

    std::wstring title = L"Match Metadata — “" + gameTitle + L"”";

    m_hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        WNDCLS, title.c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        px, py, winW, winH,
        parent, nullptr, GetModuleHandleW(nullptr), this);

    if (!m_hwnd) return;

    EnableWindow(parent, FALSE);
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);

    // Pre-search with game title
    DoSearch();
}

void MetadataPickerDialog::Close() {
    if (!m_hwnd) return;
    HWND parent = m_parent;
    DestroyWindow(m_hwnd);
    m_hwnd    = nullptr;
    m_parent  = nullptr;
    m_mm      = nullptr;
    m_candidates.clear();
    if (parent) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }
}

// ── WndProc ───────────────────────────────────────────────────────────────────

LRESULT CALLBACK MetadataPickerDialog::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MetadataPickerDialog* dlg = nullptr;
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        dlg = reinterpret_cast<MetadataPickerDialog*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)dlg);
    } else {
        dlg = reinterpret_cast<MetadataPickerDialog*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (dlg) return dlg->HandleMsg(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MetadataPickerDialog::HandleMsg(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_CREATE:
        BuildControls(hwnd);
        return 0;

    case WM_CLOSE:
        Close();
        return 0;

    case WM_COMMAND: {
        int id   = LOWORD(wp);
        int code = HIWORD(wp);
        (void)code;
        if (id == ID_SEARCH) { DoSearch();    return 0; }
        if (id == ID_MATCH)  { CommitMatch(); return 0; }
        if (id == ID_CANCEL) { Close();       return 0; }
        break;
    }

    case WM_NOTIFY: {
        auto* hdr = reinterpret_cast<NMHDR*>(lp);
        if (hdr->idFrom == ID_LIST) {
            if (hdr->code == LVN_ITEMCHANGED) {
                auto* nmlv = reinterpret_cast<NMLISTVIEW*>(lp);
                if (nmlv->uNewState & LVIS_SELECTED)
                    UpdateSummary(nmlv->iItem);
            } else if (hdr->code == NM_DBLCLK) {
                // Double-click on a result commits immediately
                auto* nmi = reinterpret_cast<NMITEMACTIVATE*>(lp);
                if (nmi->iItem >= 0) {
                    UpdateSummary(nmi->iItem);
                    CommitMatch();
                }
            }
        }
        break;
    }

    case WM_USER + 1: {
        // Search results back from worker thread
        auto* results = reinterpret_cast<std::vector<IgdbGame>*>(lp);
        m_candidates = std::move(*results);
        delete results;
        PopulateList();
        EnableWindow(m_btnSearch, TRUE);
        SetWindowTextW(m_btnSearch, L"Search");
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { Close(); return 0; }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Control building ──────────────────────────────────────────────────────────

void MetadataPickerDialog::BuildControls(HWND hwnd) {
    HFONT sysFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);

    auto mkCtrl = [&](const wchar_t* cls, DWORD style,
                      int x, int y, int w, int h, int id) -> HWND {
        HWND c = CreateWindowExW(0, cls, L"",
                                 WS_CHILD | WS_VISIBLE | style,
                                 x, y, w, h, hwnd, (HMENU)(INT_PTR)id, hInst, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)sysFont, TRUE);
        return c;
    };

    // Row 1: search edit + button
    m_edit      = mkCtrl(WC_EDITW, ES_AUTOHSCROLL | WS_BORDER, 10, 12, 504, 26, ID_EDIT);
    m_btnSearch = mkCtrl(WC_BUTTONW, BS_DEFPUSHBUTTON, 524, 12, 112, 26, ID_SEARCH);
    SetWindowTextW(m_btnSearch, L"Search");
    SetWindowTextW(m_edit, m_gameTitle.c_str());
    SendMessageW(m_edit, EM_SETSEL, 0, -1); // select all

    // Route Enter in the edit to the Search button
    SetWindowSubclass(m_edit, EditSubclassProc, 0, 0);

    // Row 2: ListView of candidates
    m_list = mkCtrl(WC_LISTVIEWW,
                    LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                    10, 48, 622, 262, ID_LIST);
    ListView_SetExtendedListViewStyle(m_list,
        LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP);

    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    col.fmt  = LVCFMT_LEFT;

    col.pszText = const_cast<LPWSTR>(L"Title");  col.cx = 362;
    ListView_InsertColumn(m_list, 0, &col);
    col.pszText = const_cast<LPWSTR>(L"Year");   col.cx = 72;
    ListView_InsertColumn(m_list, 1, &col);
    col.pszText = const_cast<LPWSTR>(L"Score");  col.cx = 76;
    ListView_InsertColumn(m_list, 2, &col);

    // Summary header label
    HWND lblHdr = CreateWindowExW(0, WC_STATICW, L"Description:",
                                   WS_CHILD | WS_VISIBLE | SS_LEFT,
                                   10, 318, 200, 18, hwnd, nullptr, hInst, nullptr);
    SendMessageW(lblHdr, WM_SETFONT, (WPARAM)sysFont, TRUE);

    // Summary text (multi-line, wrapping)
    m_lblSummary = CreateWindowExW(WS_EX_CLIENTEDGE, WC_STATICW, L"",
                                    WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
                                    10, 338, 622, 96, hwnd, (HMENU)(INT_PTR)ID_SUMMARY,
                                    hInst, nullptr);
    SendMessageW(m_lblSummary, WM_SETFONT, (WPARAM)sysFont, TRUE);

    // Bottom buttons
    m_btnCancel = mkCtrl(WC_BUTTONW, 0, 440, 450, 90, 28, ID_CANCEL);
    SetWindowTextW(m_btnCancel, L"Cancel");
    m_btnMatch  = mkCtrl(WC_BUTTONW, 0, 542, 450, 98, 28, ID_MATCH);
    SetWindowTextW(m_btnMatch, L"Match");
    EnableWindow(m_btnMatch, FALSE);

    SetFocus(m_edit);
}

// ── Search ────────────────────────────────────────────────────────────────────

void MetadataPickerDialog::DoSearch() {
    if (!m_mm) return;

    wchar_t buf[512];
    GetWindowTextW(m_edit, buf, 512);
    std::wstring query(buf);
    if (query.empty()) return;

    EnableWindow(m_btnSearch, FALSE);
    SetWindowTextW(m_btnSearch, L"Searching…");
    ListView_DeleteAllItems(m_list);
    SetWindowTextW(m_lblSummary, L"");
    EnableWindow(m_btnMatch, FALSE);

    HWND hwnd   = m_hwnd;
    MetadataManager* mm = m_mm;

    std::thread([hwnd, mm, query]() {
        // No platform filter in the picker — user is manually searching
        auto* results = new std::vector<IgdbGame>(mm->GetCandidates(query, 15));
        // PostMessage returns FALSE if hwnd is already destroyed (dialog closed while
        // searching). In that case we leak the vector intentionally — it's a rare edge
        // case and small enough to ignore for process lifetime.
        if (!PostMessageW(hwnd, WM_USER + 1, 0, (LPARAM)results))
            delete results;
    }).detach();
}

void MetadataPickerDialog::PopulateList() {
    ListView_DeleteAllItems(m_list);

    for (int i = 0; i < (int)m_candidates.size(); ++i) {
        auto& c = m_candidates[i];

        LVITEMW lvi{};
        lvi.mask     = LVIF_TEXT;
        lvi.iItem    = i;
        lvi.iSubItem = 0;
        lvi.pszText  = const_cast<LPWSTR>(c.name.c_str());
        ListView_InsertItem(m_list, &lvi);

        // Year
        int year = TimestampYear(c.firstReleaseDate);
        std::wstring ys = year > 0 ? std::to_wstring(year) : L"";
        ListView_SetItemText(m_list, i, 1, const_cast<LPWSTR>(ys.c_str()));

        // Score (round to int)
        std::wstring rs = c.rating >= 1.0 ? std::to_wstring((int)c.rating) : L"";
        ListView_SetItemText(m_list, i, 2, const_cast<LPWSTR>(rs.c_str()));
    }

    EnableWindow(m_btnMatch, FALSE);
    SetWindowTextW(m_lblSummary, m_candidates.empty() ? L"No results found." : L"");
}

void MetadataPickerDialog::UpdateSummary(int idx) {
    if (idx < 0 || idx >= (int)m_candidates.size()) {
        SetWindowTextW(m_lblSummary, L"");
        EnableWindow(m_btnMatch, FALSE);
        return;
    }
    auto& c = m_candidates[idx];
    std::wstring text = c.summary.empty() ? L"(No description available.)" : c.summary;
    SetWindowTextW(m_lblSummary, text.c_str());
    EnableWindow(m_btnMatch, c.id > 0 ? TRUE : FALSE);
}

// ── Commit ────────────────────────────────────────────────────────────────────

void MetadataPickerDialog::CommitMatch() {
    int sel = ListView_GetNextItem(m_list, -1, LVNI_SELECTED);
    if (sel < 0 || sel >= (int)m_candidates.size()) return;

    int64_t igdbId = m_candidates[sel].id;
    if (igdbId <= 0) return;

    std::wstring gameId   = m_gameId;
    ArtReadyCallback done = m_artReady;

    m_mm->MatchManual(gameId, igdbId,
        [done, gameId](const std::wstring& id, bool matched) {
            if (matched && done) done(id);
        });

    Close();
}
