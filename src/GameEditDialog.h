#pragma once
#include "pch.h"

// Simple blocking dialog for editing a game's display title.
// For emulated games an optional checkbox renames the ROM file on disk too.
// The platform combobox lets the user pin an IGDB platform ID for metadata.
class GameEditDialog {
public:
    bool         confirmed  = false;
    std::wstring newTitle;
    bool         renameFile = false;       // only meaningful if isEmulated was true
    int          selectedIgdbPlatformId = 0;  // 0 = auto
    std::wstring newLaunchOptions;            // only meaningful in properties mode

    // Blocks until the user confirms or cancels.
    // Pass the current igdbPlatformId so the combobox starts on the right entry.
    void Show(HWND parent, const std::wstring& currentTitle,
              bool isEmulated, int currentIgdbPlatformId = 0);

    // Consolidated per-game "Properties" modal: title + IGDB platform override +
    // launch options, with a read-only info block (platform / version / install
    // location / state). Blocks until confirmed or cancelled.
    void ShowProperties(HWND parent, const std::wstring& currentTitle,
                        bool isEmulated, int currentIgdbPlatformId,
                        const std::wstring& launchOptions,
                        const std::wstring& infoText);

private:
    enum CtrlId : int {
        ID_EDIT   = 10,
        ID_CHK    = 11,
        ID_OK     = 12,
        ID_CANCEL = 13,
        ID_COMBO  = 14,
        ID_LAUNCH = 15,
    };

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMsg(HWND, UINT, WPARAM, LPARAM);

    void Run(HWND parent, const std::wstring& currentTitle,
             bool isEmulated, int currentIgdbPlatformId);
    void BuildControls(HWND hwnd, bool isEmulated);
    void Commit();

    HWND m_hwnd      = nullptr;
    HWND m_edit      = nullptr;
    HWND m_chk       = nullptr;
    HWND m_combo     = nullptr;
    HWND m_launch    = nullptr;   // launch options edit (properties mode)
    HWND m_btnOk     = nullptr;
    HWND m_btnCancel = nullptr;
    bool m_done      = false;

    // Properties-mode inputs (set by ShowProperties).
    bool         m_props = false;
    std::wstring m_inLaunch;
    std::wstring m_infoText;

    static constexpr int W = 440;
    static constexpr wchar_t WNDCLS[] = L"ArcadeGameEdit";
};
