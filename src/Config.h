#pragma once
#include "pch.h"

struct CustomLibraryConfig {
    std::wstring name    = L"Custom Library";
    std::vector<std::wstring> dirs;
};

struct LibraryConfig {
    std::wstring steamPath;                        // Steam install root override; empty = registry
    std::vector<std::wstring> steamExtraFolders;   // extra steamapps dirs beyond what VDF reports
    std::vector<std::wstring> epicManifestDirs;    // override list; empty = auto-detect

    std::vector<CustomLibraryConfig> customLibraries;
};

struct EmulatorConfig {
    std::wstring dolphinPath;
    std::wstring ryujinxPath;
    std::wstring rpcs3Path;
    std::wstring n64Path;
    std::wstring nesPath;
    std::wstring snesPath;
    std::wstring duckstationPath;
    std::wstring pcsx2Path;
    std::wstring xeniaPath;
    std::wstring xemuPath;

    std::wstring dolphinArgs;
    std::wstring ryujinxArgs;
    std::wstring rpcs3Args;
    std::wstring n64Args;
    std::wstring nesArgs;
    std::wstring snesArgs;
    std::wstring duckstationArgs;
    std::wstring pcsx2Args;
    std::wstring xeniaArgs;
    std::wstring xemuArgs;

    std::vector<std::wstring> dolphinRomDirs;
    std::vector<std::wstring> ryujinxRomDirs;
    std::vector<std::wstring> rpcs3RomDirs;
    std::vector<std::wstring> n64RomDirs;
    std::vector<std::wstring> nesRomDirs;
    std::vector<std::wstring> snesRomDirs;
    std::vector<std::wstring> duckstationRomDirs;
    std::vector<std::wstring> pcsx2RomDirs;
    std::vector<std::wstring> xeniaRomDirs;
    std::vector<std::wstring> xemuRomDirs;

    // Last downloaded release tag (empty = never downloaded via launcher)
    std::wstring dolphinTag;
    std::wstring ryujinxTag;
    std::wstring rpcs3Tag;
    std::wstring n64Tag;
    std::wstring nesTag;
    std::wstring snesTag;
    std::wstring duckstationTag;
    std::wstring pcsx2Tag;
    std::wstring xeniaTag;
    std::wstring xemuTag;
};

struct ServerConfig {
    bool enabled = false;
    std::wstring baseUrl;
    std::wstring username;
    std::wstring password;
    std::wstring totpCode;   // transient only — used during login, never persisted to disk
    std::wstring authToken;
    // Machine fingerprint the cached authToken was bound to. The token is
    // reused across launches and concurrent clients until this changes (a
    // major hardware or software change), at which point we re-authenticate.
    std::wstring tokenFingerprint;
    std::wstring installRoot;
    // Steam-style library folders: games may install to any of these roots. The
    // legacy single `installRoot` is migrated into libraryFolders[0] on load.
    std::vector<std::wstring> libraryFolders;
    int  defaultLibraryIndex = 0;          // which folder is offered by default
    bool alwaysAskInstallLocation = true;  // show the drive picker on each install
    // Runtime-only (not persisted): path to Dolphin.exe, used to locate the
    // Dolphin user directory when extracting GameCube/Wii custom texture packs.
    std::wstring dolphinPath;
};

struct AppConfig {
    bool        firstLaunchDone  = false;
    bool        startFullscreen  = false;
    bool        minimizeOnLaunch = true;
    // When on, the launcher keeps a Windows Defender exclusion on the PC games
    // install root (toggled in Settings -> General; applied via elevated
    // PowerShell Add-/Remove-MpPreference).
    bool        defenderExclusions = false;
    int         windowWidth  = 1280;
    int         windowHeight = 720;
    std::wstring steamGridDbApiKey;

    // IGDB / Twitch API credentials
    std::wstring igdbClientId;
    std::wstring igdbClientSecret;

    // Cached OAuth token
    std::wstring igdbAccessToken;
    int64_t      igdbTokenExpiry = 0;

    // Discord Rich Presence: shows "Playing <title>" with an elapsed timer while
    // a game runs. The ID is a public Application ID from
    // https://discord.com/developers/applications — the default is the shared
    // "ArcadeLauncher" application, so presence works out of the box; set your
    // own ID here to override the branding, or turn the toggle off to disable.
    bool         discordRichPresence = true;
    std::wstring discordClientId = L"1515119921795960882";

    // Background download speed cap in KB/s (0 = unlimited). Applied to the
    // process-wide DownloadControl at startup and when changed in Tools.
    int          downloadLimitKBps = 0;

    // User-defined collections (Steam-style). Games reference these by name.
    std::vector<std::wstring> collections;

    LibraryConfig  libraries;
    EmulatorConfig emulators;
    ServerConfig   server;
};

class Config {
public:
    void Load(const std::wstring& path);
    void Save(const std::wstring& path) const;
    AppConfig& Get() { return m_cfg; }
    const AppConfig& Get() const { return m_cfg; }

private:
    AppConfig m_cfg;

    static std::string Escape(const std::wstring& s);
    static std::wstring Unescape(const std::string& s);
};
