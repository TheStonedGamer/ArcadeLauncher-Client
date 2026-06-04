#include "pch.h"
#include "EpicScanner.h"

std::vector<Game> EpicScanner::Scan() {
    // Build the list of dirs to scan: override list or auto-detected fallback
    std::vector<std::wstring> dirs;
    if (!m_manifestDirs.empty()) {
        for (auto& d : m_manifestDirs)
            if (GetFileAttributesW(d.c_str()) != INVALID_FILE_ATTRIBUTES)
                dirs.push_back(d);
    }
    if (dirs.empty()) {
        std::wstring autoDir = GetManifestDir();
        if (!autoDir.empty()) dirs.push_back(autoDir);
    }

    std::vector<Game> games;
    for (auto& manifestDir : dirs) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((manifestDir + L"\\*.item").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            Game g = ParseManifest(manifestDir + L"\\" + fd.cFileName);
            if (!g.id.empty()) games.push_back(std::move(g));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return games;
}

std::wstring EpicScanner::GetManifestDir() {
    // Common path; also check registry for launcher data path
    wchar_t programData[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, programData);
    std::wstring base = std::wstring(programData) +
                        L"\\Epic\\EpicGamesLauncher\\Data\\Manifests";
    if (GetFileAttributesW(base.c_str()) != INVALID_FILE_ATTRIBUTES)
        return base;

    // Fallback: check registry for custom launcher data dir
    HKEY hk;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\WOW6432Node\\Epic Games\\EpicGamesLauncher",
        0, KEY_READ, &hk) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH] = {};
        DWORD sz = sizeof(buf);
        RegQueryValueExW(hk, L"AppDataPath", nullptr, nullptr, (LPBYTE)buf, &sz);
        RegCloseKey(hk);
        if (buf[0]) return std::wstring(buf) + L"\\Manifests";
    }
    return {};
}

Game EpicScanner::ParseManifest(const std::wstring& path) {
    std::ifstream f(path);
    if (!f) return {};
    std::string json((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());

    std::wstring appName   = JsonString(json, "AppName");
    std::wstring display   = JsonString(json, "DisplayName");
    std::wstring installLoc = JsonString(json, "InstallLocation");
    std::wstring launchExe = JsonString(json, "LaunchExecutable");
    std::wstring ns        = JsonString(json, "CatalogNamespace");

    if (appName.empty() || display.empty()) return {};
    // Skip engine/tools
    if (appName.find(L"UE_") != std::wstring::npos) return {};
    if (display.find(L"Unreal Engine") != std::wstring::npos) return {};

    Game g;
    g.id          = L"epic_" + appName;
    g.title       = display;
    g.platform    = Platform::Epic;
    g.epicAppName = appName;
    g.launchUri   = L"com.epicgames.launcher://apps/" + appName + L"?action=launch&silent=true";

    // Fallback direct exe if URI doesn't work
    if (!installLoc.empty() && !launchExe.empty()) {
        std::replace(installLoc.begin(), installLoc.end(), L'/', L'\\');
        g.exePath = installLoc + L"\\" + launchExe;
    }
    return g;
}

std::wstring EpicScanner::JsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\"";
    size_t kp = json.find(search);
    if (kp == std::string::npos) return {};
    size_t colon = json.find(':', kp + search.size());
    if (colon == std::string::npos) return {};
    size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return {};
    size_t q2 = q1 + 1;
    std::string val;
    while (q2 < json.size() && json[q2] != '"') {
        if (json[q2] == '\\' && q2 + 1 < json.size()) {
            char esc = json[q2 + 1];
            if (esc == 'n') val += '\n';
            else if (esc == 't') val += '\t';
            else val += esc;
            q2 += 2;
        } else {
            val += json[q2++];
        }
    }
    return ToWide(val);
}
