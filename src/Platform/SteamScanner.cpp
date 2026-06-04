#include "pch.h"
#include "SteamScanner.h"
#include <set>

std::vector<Game> SteamScanner::Scan() {
    std::wstring steamPath = GetSteamInstallPath();

    std::vector<std::wstring> folders;
    if (!steamPath.empty())
        folders = GetLibraryFolders(steamPath);

    // Append user-specified extra folders (already steamapps paths)
    for (auto& extra : m_extraFolders)
        if (GetFileAttributesW(extra.c_str()) != INVALID_FILE_ATTRIBUTES)
            folders.push_back(extra);

    std::vector<std::wstring> uniqueFolders;
    std::set<std::wstring> seenFolders;
    for (auto folder : folders) {
        std::replace(folder.begin(), folder.end(), L'/', L'\\');
        while (!folder.empty() && (folder.back() == L'\\' || folder.back() == L'/'))
            folder.pop_back();

        std::wstring key = folder;
        for (auto& c : key) c = towlower(c);
        if (seenFolders.insert(key).second)
            uniqueFolders.push_back(std::move(folder));
    }

    std::vector<Game> result;
    std::set<std::wstring> seenGames;
    for (auto& folder : uniqueFolders) {
        auto games = ScanLibraryFolder(folder);
        for (auto& game : games) {
            if (seenGames.insert(game.id).second)
                result.push_back(std::move(game));
        }
    }
    return result;
}

std::wstring SteamScanner::GetSteamInstallPath() {
    if (!m_pathOverride.empty()) {
        std::wstring p = m_pathOverride;
        std::replace(p.begin(), p.end(), L'/', L'\\');
        return p;
    }
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &hk) != ERROR_SUCCESS)
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Valve\\Steam", 0, KEY_READ, &hk) != ERROR_SUCCESS)
            return {};

    wchar_t buf[MAX_PATH] = {};
    DWORD sz = sizeof(buf);
    RegQueryValueExW(hk, L"SteamPath", nullptr, nullptr, (LPBYTE)buf, &sz);
    RegCloseKey(hk);

    std::wstring path(buf);
    // Steam stores forward slashes; normalize
    std::replace(path.begin(), path.end(), L'/', L'\\');
    return path;
}

std::vector<std::wstring> SteamScanner::GetLibraryFolders(const std::wstring& steamPath) {
    std::vector<std::wstring> folders;
    folders.push_back(steamPath + L"\\steamapps");

    std::wstring vdfPath = steamPath + L"\\steamapps\\libraryfolders.vdf";
    VdfNode root = ParseVdfFile(vdfPath);

    auto libIt = root.children.find(L"libraryfolders");
    if (libIt != root.children.end()) {
        auto& libNode = libIt->second;

        // Modern format: root["libraryfolders"][N] is an object with a "path" key
        for (auto& [key, node] : libNode.children) {
            auto pathIt = node.values.find(L"path");
            if (pathIt != node.values.end()) {
                std::wstring p = pathIt->second;
                std::replace(p.begin(), p.end(), L'/', L'\\');
                folders.push_back(p + L"\\steamapps");
            }
        }

        // Legacy format: root["libraryfolders"]["1"] = "E:\\path"  (numeric key, path value)
        for (auto& [key, val] : libNode.values) {
            bool numeric = !key.empty();
            for (wchar_t c : key) if (!iswdigit(c)) { numeric = false; break; }
            if (!numeric || val.empty()) continue;
            std::wstring p = val;
            std::replace(p.begin(), p.end(), L'/', L'\\');
            std::wstring candidate = p + L"\\steamapps";
            if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES)
                folders.push_back(candidate);
        }
    }
    return folders;
}

std::vector<Game> SteamScanner::ScanLibraryFolder(const std::wstring& folder) {
    std::vector<Game> games;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((folder + L"\\*.acf").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return games;

    do {
        std::wstring acfPath = folder + L"\\" + fd.cFileName;
        VdfNode acf = ParseVdfFile(acfPath);

        auto stateIt = acf.children.find(L"appstate");
        if (stateIt == acf.children.end()) {
            // Try lowercase
            stateIt = acf.children.find(L"AppState");
            if (stateIt == acf.children.end()) {
                // Try finding any child
                if (!acf.children.empty())
                    stateIt = acf.children.begin();
                else continue;
            }
        }

        auto& vals = stateIt->second.values;
        std::wstring appId   = vals.count(L"appid")   ? vals.at(L"appid")   :
                               vals.count(L"AppID")   ? vals.at(L"AppID")   : L"";
        std::wstring name    = vals.count(L"name")    ? vals.at(L"name")    :
                               vals.count(L"Name")    ? vals.at(L"Name")    : L"";
        std::wstring stateFlags = vals.count(L"StateFlags") ? vals.at(L"StateFlags") :
                                  vals.count(L"stateflags") ? vals.at(L"stateflags") : L"0";

        if (appId.empty() || name.empty()) continue;
        // StateFlags == 4 means fully installed
        int flags = _wtoi(stateFlags.c_str());
        if ((flags & 4) == 0) continue;

        Game g;
        g.id         = L"steam_" + appId;
        g.title      = name;
        g.platform   = Platform::Steam;
        g.steamAppId = appId;
        g.launchUri  = L"steam://rungameid/" + appId;
        g.coverArtUrl = L"https://cdn.steamstatic.com/steam/apps/" + appId + L"/library_600x900.jpg";
        games.push_back(std::move(g));

    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return games;
}

// ── VDF Parser ────────────────────────────────────────────────────────────────

SteamScanner::VdfNode SteamScanner::ParseVdfFile(const std::wstring& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::string raw((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    std::wstring text = ToWide(raw);
    size_t pos = 0;
    return ParseVdf(text, pos);
}

std::wstring SteamScanner::ReadVdfString(const std::wstring& t, size_t& pos) {
    // Skip whitespace
    while (pos < t.size() && iswspace(t[pos])) ++pos;
    if (pos >= t.size()) return {};

    if (t[pos] == L'"') {
        ++pos;
        std::wstring s;
        while (pos < t.size() && t[pos] != L'"') {
            if (t[pos] == L'\\' && pos + 1 < t.size()) { ++pos; }
            s += t[pos++];
        }
        if (pos < t.size()) ++pos; // closing "
        return s;
    }
    // Unquoted token
    std::wstring s;
    while (pos < t.size() && !iswspace(t[pos]) && t[pos] != L'{' && t[pos] != L'}')
        s += t[pos++];
    return s;
}

SteamScanner::VdfNode SteamScanner::ParseVdf(const std::wstring& t, size_t& pos) {
    VdfNode node;
    while (pos < t.size()) {
        // Skip whitespace and comments
        while (pos < t.size() && iswspace(t[pos])) ++pos;
        if (pos >= t.size() || t[pos] == L'}') break;
        if (t[pos] == L'/' && pos + 1 < t.size() && t[pos+1] == L'/') {
            while (pos < t.size() && t[pos] != L'\n') ++pos;
            continue;
        }

        std::wstring key = ReadVdfString(t, pos);
        std::wstring lkey = key;
        for (auto& c : lkey) c = towlower(c);

        while (pos < t.size() && iswspace(t[pos])) ++pos;

        if (pos < t.size() && t[pos] == L'{') {
            ++pos; // consume {
            node.children[lkey] = ParseVdf(t, pos);
            if (pos < t.size() && t[pos] == L'}') ++pos;
        } else {
            std::wstring val = ReadVdfString(t, pos);
            node.values[lkey] = val;
        }
    }
    return node;
}
