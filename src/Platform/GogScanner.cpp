#include "pch.h"
#include "GogScanner.h"

std::vector<Game> GogScanner::Scan() {
    std::vector<Game> games;
    const wchar_t* regPath = L"SOFTWARE\\WOW6432Node\\GOG.com\\Games";
    HKEY hkGog;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, regPath, 0, KEY_READ, &hkGog) != ERROR_SUCCESS)
        return games;

    wchar_t subkeyName[256];
    DWORD idx = 0;
    while (RegEnumKeyW(hkGog, idx++, subkeyName, 256) == ERROR_SUCCESS) {
        HKEY hkGame;
        std::wstring gamePath = std::wstring(regPath) + L"\\" + subkeyName;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, gamePath.c_str(), 0, KEY_READ, &hkGame) != ERROR_SUCCESS)
            continue;

        std::wstring name   = RegReadString(hkGame, L"", L"gameName");
        std::wstring exe    = RegReadString(hkGame, L"", L"exe");
        std::wstring gameId = RegReadString(hkGame, L"", L"gameID");
        std::wstring path   = RegReadString(hkGame, L"", L"path");

        // Also try "EXEFILE" for older entries
        if (exe.empty()) exe = RegReadString(hkGame, L"", L"EXEFILE");
        if (name.empty()) name = RegReadString(hkGame, L"", L"GAMENAME");
        RegCloseKey(hkGame);

        if (name.empty() || exe.empty()) continue;

        Game g;
        g.id       = L"gog_" + (gameId.empty() ? std::wstring(subkeyName) : gameId);
        g.title    = name;
        g.platform = Platform::GOG;
        g.exePath  = exe;
        g.gogGameId = gameId;
        games.push_back(std::move(g));
    }
    RegCloseKey(hkGog);
    return games;
}

std::wstring GogScanner::RegReadString(HKEY root, const std::wstring& subkey,
                                        const std::wstring& valueName) {
    HKEY hk = root;
    bool opened = false;
    if (!subkey.empty()) {
        if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &hk) != ERROR_SUCCESS)
            return {};
        opened = true;
    }
    wchar_t buf[1024] = {};
    DWORD sz = sizeof(buf);
    DWORD type = 0;
    LONG res = RegQueryValueExW(hk, valueName.c_str(), nullptr, &type, (LPBYTE)buf, &sz);
    if (opened) RegCloseKey(hk);
    if (res != ERROR_SUCCESS) return {};
    return std::wstring(buf);
}
