#include "pch.h"
#include "IgdbSync.h"
#include "sqlite/sqlite3.h"

std::wstring IgdbSync::Normalise(const std::wstring& title) {
    std::wstring out;
    out.reserve(title.size());
    for (wchar_t c : title) {
        if (iswalpha(c) || iswdigit(c)) {
            out += towlower(c);
        } else if (iswspace(c) || c == L'-') {
            if (!out.empty() && out.back() != L' ')
                out += L' ';
        }
    }
    while (!out.empty() && out.back() == L' ')
        out.pop_back();
    return out;
}

struct PlatformSync {
    const char* dbKey;
    int igdbId;
};

static const PlatformSync kPlatforms[] = {
    { "NES",     18 },
    { "SNES",    19 },
    { "N64",      4 },
    { "PS1",      7 },
    { "PS2",      8 },
    { "Xbox",    11 },
    { "Xbox360", 12 },
};

static bool Exec(sqlite3* db, const char* sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
    return rc == SQLITE_OK;
}

static bool BindText(sqlite3_stmt* stmt, int index, const std::string& value) {
    return sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT) == SQLITE_OK;
}

void IgdbSync::Worker(HWND hwnd, IgdbClient client, std::wstring dbPath) {
    if (!client.HasCredentials()) {
        PostMessageW(hwnd, WM_IGDBSYNC_DONE, 0, 0);
        return;
    }
    if (!client.IsAuthenticated() && !client.Authenticate()) {
        PostMessageW(hwnd, WM_IGDBSYNC_DONE, 0, 0);
        return;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open16(dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        PostMessageW(hwnd, WM_IGDBSYNC_DONE, 0, 0);
        return;
    }

    const char* schema =
        "PRAGMA journal_mode=WAL;"
        "CREATE TABLE IF NOT EXISTS games ("
        "platform TEXT NOT NULL,"
        "key TEXT NOT NULL,"
        "title TEXT NOT NULL,"
        "igdb_id INTEGER NOT NULL,"
        "PRIMARY KEY(platform,key)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_games_lookup ON games(platform,key);";

    bool ok = Exec(db, schema) && Exec(db, "BEGIN IMMEDIATE;") && Exec(db, "DELETE FROM games;");
    sqlite3_stmt* insert = nullptr;
    if (ok) {
        const char* sql =
            "INSERT OR REPLACE INTO games(platform,key,title,igdb_id) VALUES(?,?,?,?);";
        ok = sqlite3_prepare_v2(db, sql, -1, &insert, nullptr) == SQLITE_OK;
    }

    int totalGames = 0;
    if (ok) {
        for (auto& plat : kPlatforms) {
            int offset = 0;
            const int pageSize = 500;
            int platformGames = 0;

            while (ok) {
                auto games = client.FetchGamesByPlatform(plat.igdbId, offset, pageSize);
                if (games.empty()) {
                    if (offset == 0) ok = false;
                    break;
                }

                for (auto& g : games) {
                    if (g.id == 0 || g.name.empty()) continue;

                    std::wstring key = IgdbSync::Normalise(g.name);
                    if (key.empty()) continue;

                    sqlite3_reset(insert);
                    sqlite3_clear_bindings(insert);

                    ok = BindText(insert, 1, plat.dbKey)
                      && BindText(insert, 2, ToUtf8(key))
                      && BindText(insert, 3, ToUtf8(g.name))
                      && sqlite3_bind_int64(insert, 4, g.id) == SQLITE_OK
                      && sqlite3_step(insert) == SQLITE_DONE;
                    if (!ok) break;

                    ++totalGames;
                    ++platformGames;
                }

                if (!ok || (int)games.size() < pageSize) break;
                offset += (int)games.size();
                Sleep(260);
            }
            if (!ok || platformGames == 0) {
                ok = false;
                break;
            }
        }
    }

    if (insert) sqlite3_finalize(insert);
    if (totalGames == 0) ok = false;
    if (ok) ok = Exec(db, "COMMIT;");
    else Exec(db, "ROLLBACK;");
    sqlite3_close(db);

    PostMessageW(hwnd, WM_IGDBSYNC_DONE, ok ? (WPARAM)totalGames : 0, 0);
}

void IgdbSync::StartAsync(HWND hwnd, IgdbClient& client,
                           const std::wstring& dbPath) {
    IgdbClient workerClient(client.ClientId(), client.ClientSecret());
    workerClient.RestoreToken(client.SavedToken(), client.TokenExpiry());
    std::thread(Worker, hwnd, std::move(workerClient), dbPath).detach();
}
