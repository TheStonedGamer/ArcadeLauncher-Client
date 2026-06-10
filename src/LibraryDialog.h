#pragma once
#include "pch.h"
#include "Config.h"

// Steam-style library-folder + install-location dialogs (#29 / #30 / #31).

// Library Folders manager, launched from the Tools menu. Edits
// cfg.libraryFolders / defaultLibraryIndex / alwaysAskInstallLocation in place.
// Returns true if the config was modified and should be saved by the caller.
bool ShowLibraryFoldersDialog(HWND owner, ServerConfig& cfg);

// Install-location picker shown before an install (and reused for "move").
// Lists the configured library folders with free/total disk space; the user
// picks one or browses for a new folder (added to cfg). On OK, outRoot receives
// the chosen install root and the function returns true; false on cancel.
// `moving` switches the wording to a move operation. cfg may be modified (new
// folder added / default / always-ask changed) — caller should persist.
// When installing (moving == false), the dialog also offers "create a desktop
// shortcut" and "create a Start Menu shortcut" checkboxes; if the corresponding
// out-pointer is non-null it receives the user's choice (so the caller can make
// the shortcuts once the install finishes). Ignored for moves.
bool ShowInstallLocationDialog(HWND owner, ServerConfig& cfg,
                               const std::wstring& gameTitle,
                               std::wstring& outRoot, bool moving,
                               bool* outDesktopShortcut = nullptr,
                               bool* outStartMenuShortcut = nullptr);

// Generic single-line text prompt (launch options, new collection name, …).
// `value` is seeded with the initial text and receives the result. Returns true
// if OK was pressed, false on cancel.
bool ShowTextPrompt(HWND owner, const std::wstring& title,
                    const std::wstring& label, std::wstring& value);
