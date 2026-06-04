#pragma once
#include "pch.h"

// Posted to the settings window when a version check completes.
// WPARAM = page index, LPARAM = new EmuCheckResult* (receiver must delete).
static constexpr UINT WM_EMUCHECK_DONE = WM_USER + 52;

struct EmuCheckResult {
    std::wstring latestTag;
    bool         isError = false;
};

// Fires a background thread that fetches the latest release tag_name from
// https://api.github.com/repos/{githubRepo}/releases/latest and posts
// WM_EMUCHECK_DONE to hwnd.
void CheckEmulatorUpdateAsync(HWND hwnd, int pageIdx, const std::string& githubRepo);
