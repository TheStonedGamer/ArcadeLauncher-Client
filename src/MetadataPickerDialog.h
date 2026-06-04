#pragma once
#include "pch.h"
#include "MetadataManager.h"

// Modal-style Win32 window for manually matching a game to an IGDB entry.
// Disables the parent window while open; re-enables it on close.
class MetadataPickerDialog {
public:
    // Called from the worker thread once art is downloaded; post WM_USER+4 there.
    using ArtReadyCallback = std::function<void(const std::wstring& gameId)>;

    MetadataPickerDialog() = default;
    ~MetadataPickerDialog() { Close(); }

    void Open(HWND parent, const std::wstring& gameId, const std::wstring& gameTitle,
              MetadataManager& mm, ArtReadyCallback artReady);
    void Close();

    bool IsOpen()    const { return m_hwnd != nullptr; }
    HWND GetHwnd()   const { return m_hwnd; }

    // Public so the file-scope edit subclass proc can reference ID_SEARCH
    enum CtrlId : int {
        ID_EDIT   = 10,
        ID_SEARCH = 11,
        ID_LIST   = 12,
        ID_SUMMARY= 13,
        ID_MATCH  = 14,
        ID_CANCEL = 15,
    };

private:

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    void BuildControls(HWND hwnd);
    void DoSearch();
    void PopulateList();
    void UpdateSummary(int idx);
    void CommitMatch();

    HWND m_hwnd      = nullptr;
    HWND m_parent    = nullptr;
    HWND m_edit      = nullptr;
    HWND m_list      = nullptr;
    HWND m_lblSummary= nullptr;
    HWND m_btnSearch = nullptr;
    HWND m_btnMatch  = nullptr;
    HWND m_btnCancel = nullptr;

    std::wstring      m_gameId;
    std::wstring      m_gameTitle;
    MetadataManager*  m_mm      = nullptr;
    ArtReadyCallback  m_artReady;

    std::vector<IgdbGame> m_candidates;

    static constexpr int W = 650, H = 510;
    static constexpr wchar_t WNDCLS[] = L"ArcadeMetaPicker";
};
