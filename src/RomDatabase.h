#pragma once
#include "pch.h"
#include "GameLibrary.h"

// ── RomDatabase ───────────────────────────────────────────────────────────────
//
// Maps (Platform, stripped_rom_title) → canonical display title + IGDB ID.
//
// The stripped title is what EmulatorScanner::StripRomTags produces after
// removing region/revision/dump tags from the ROM filename — e.g.
//   "Super Mario Bros. (World)" → "Super Mario Bros."
//   "Contra (USA)"              → "Contra"
//
// Lookup is case-insensitive (keys are stored lowercase).
//
// Backed by %AppData%\ArcadeLauncher\romdb.sqlite. The SQLite table stores:
//   platform, key, title, igdb_id
// where key is IgdbSync::Normalise(title). The in-memory map is rebuilt from
// SQLite on load and used by EmulatorScanner during ROM scans.
// ─────────────────────────────────────────────────────────────────────────────

class RomDatabase {
public:
    struct Entry {
        std::wstring title;      // canonical display title
        int64_t      igdbId = 0; // 0 = not mapped
    };

    // Load database from a SQLite file. Creates the schema if missing.
    bool Load(const std::wstring& dbPath);

    // Lookup a game by platform and stripped ROM title.
    // Returns nullptr if not found.
    const Entry* Lookup(Platform platform, const std::wstring& strippedTitle) const;

    bool IsLoaded() const { return !m_db.empty(); }
    int  EntryCount() const;

private:
    // (int)Platform → (lowercase_stripped_title → Entry)
    std::unordered_map<int, std::unordered_map<std::wstring, Entry>> m_db;
};
