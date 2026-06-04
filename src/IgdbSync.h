#pragma once
#include "pch.h"
#include "IgdbClient.h"

// Posted to the settings window when a sync finishes.
// WPARAM = total games synced (0 on failure), LPARAM = 0.
static constexpr UINT WM_IGDBSYNC_DONE = WM_USER + 60;

// ── IgdbSync ──────────────────────────────────────────────────────────────────
//
// Downloads the complete game catalogue for every emulated platform from
// IGDB, normalises the titles, and writes romdb.sqlite for RomDatabase.
//
// Normalisation: lowercase + strip everything except a-z, 0-9 and spaces,
// collapse multiple spaces.  This makes "Super Mario Bros." match
// "super mario bros" (No-Intro stripped), "GoldenEye 007" match
// "goldeneye 007", etc.
//
// The sync runs entirely on a background thread; only the WM_IGDBSYNC_DONE
// notification is posted back to hwnd when finished.
// ─────────────────────────────────────────────────────────────────────────────

class IgdbSync {
public:
    // Start a background sync using a thread-local IGDB client snapshot.
    // Posts WM_IGDBSYNC_DONE to `hwnd` when done.
    static void StartAsync(HWND hwnd, IgdbClient& client,
                           const std::wstring& dbPath);

    // Normalise a game title for use as a lookup key:
    //   "Super Mario Bros."   → "super mario bros"
    //   "The Legend of Zelda: Ocarina of Time" → "the legend of zelda ocarina of time"
    //   "GoldenEye 007"       → "goldeneye 007"
    static std::wstring Normalise(const std::wstring& title);

private:
    static void Worker(HWND hwnd, IgdbClient client, std::wstring dbPath);
};
