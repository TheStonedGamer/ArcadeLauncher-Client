#include "pch.h"
#include "RomDatabase.h"
#include "IgdbSync.h"
#include "sqlite/sqlite3.h"

static Platform PlatformFromDbKey(const std::string& key) {
    if (key == "NES")     return Platform::NES;
    if (key == "SNES")    return Platform::SNES;
    if (key == "N64")     return Platform::N64;
    if (key == "PS1")     return Platform::PS1;
    if (key == "PS2")     return Platform::PS2;
    if (key == "Xbox")    return Platform::Xbox;
    if (key == "Xbox360") return Platform::Xbox360;
    return Platform::Repacks;
}

static bool Exec(sqlite3* db, const char* sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
    return rc == SQLITE_OK;
}

bool RomDatabase::Load(const std::wstring& dbPath) {
    sqlite3* db = nullptr;
    if (sqlite3_open16(dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return false;
    }

    const char* schema =
        "CREATE TABLE IF NOT EXISTS games ("
        "platform TEXT NOT NULL,"
        "key TEXT NOT NULL,"
        "title TEXT NOT NULL,"
        "igdb_id INTEGER NOT NULL,"
        "PRIMARY KEY(platform,key)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_games_lookup ON games(platform,key);";
    if (!Exec(db, schema)) {
        sqlite3_close(db);
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* query = "SELECT platform,key,title,igdb_id FROM games;";
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return false;
    }

    m_db.clear();
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* platformText = sqlite3_column_text(stmt, 0);
        const unsigned char* keyText      = sqlite3_column_text(stmt, 1);
        const unsigned char* titleText    = sqlite3_column_text(stmt, 2);
        if (!platformText || !keyText || !titleText) continue;

        Entry e;
        e.title  = ToWide(reinterpret_cast<const char*>(titleText));
        e.igdbId = sqlite3_column_int64(stmt, 3);

        Platform platform = PlatformFromDbKey(reinterpret_cast<const char*>(platformText));
        std::wstring key  = ToWide(reinterpret_cast<const char*>(keyText));
        if (platform != Platform::Repacks && !key.empty())
            m_db[(int)platform][key] = std::move(e);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return !m_db.empty();
}

const RomDatabase::Entry* RomDatabase::Lookup(Platform platform,
                                               const std::wstring& strippedTitle) const {
    auto pit = m_db.find((int)platform);
    if (pit == m_db.end()) return nullptr;

    std::wstring key = IgdbSync::Normalise(strippedTitle);
    auto it = pit->second.find(key);
    return (it != pit->second.end()) ? &it->second : nullptr;
}

int RomDatabase::EntryCount() const {
    int n = 0;
    for (auto& [p, m] : m_db) n += (int)m.size();
    return n;
}
