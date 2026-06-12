#pragma once
#include "pch.h"
#include "Config.h"
#include "GameLibrary.h"

// Process-wide controls for the (single) active background download. The UI
// thread flips these; DownloadFile's read loop honors them between buffer
// reads. Cancel leaves the .part file intact so a re-queue resumes.
namespace DownloadControl {
    inline std::atomic<bool>& Paused()    { static std::atomic<bool> v{ false }; return v; }
    inline std::atomic<bool>& Cancelled() { static std::atomic<bool> v{ false }; return v; }
    inline std::atomic<int>&  LimitKBps() { static std::atomic<int>  v{ 0 };     return v; } // 0 = unlimited
    inline const wchar_t* kCancelledError = L"Download cancelled";
}

struct ServerFileEntry {
    std::wstring path;
    std::wstring url;
    std::wstring sha256;
    uint64_t size = 0;
    struct Chunk {
        int index = 0;
        uint64_t offset = 0;
        uint64_t size = 0;
        std::wstring url;
        std::wstring sha256;
        std::wstring compression;
    };
    std::vector<Chunk> chunks;
};

struct ServerGameManifest {
    std::wstring id;
    std::wstring title;
    std::wstring platform;
    std::wstring installType;
    std::wstring version;
    std::wstring launchTarget;
    std::wstring launchArguments;
    std::vector<ServerFileEntry> files;

    // Optional Dolphin custom-texture pack (GameCube/Wii only). When present the
    // client downloads the .zip and extracts it into Dolphin's Load/Textures.
    struct TexturePack {
        std::wstring url;
        std::wstring sha256;
        uint64_t size = 0;
        // Optional 6-character Dolphin Game ID (e.g. "GZLE01"). When supplied by
        // the server the client uses it to place textures under the canonical
        // Load/Textures/<GameID>/ folder Dolphin expects; otherwise it is read
        // from the ROM header at install time.
        std::wstring gameId;
    };
    bool hasTexturePack = false;
    TexturePack texturePack;
};

struct ServerInstallResult {
    bool ok = false;
    std::wstring error;
    std::wstring installRoot;
    std::wstring launchPath;
    std::wstring arguments;
    std::wstring version;
};

struct ServerValidateResult {
    bool ok = false;
    std::wstring error;
    uint64_t checkedBytes = 0;
    int checkedFiles = 0;
    std::vector<std::wstring> missingFiles;
    std::vector<std::wstring> badFiles;
};

class ServerClient {
public:
    explicit ServerClient(ServerConfig cfg);

    bool FetchCatalog(std::vector<Game>& games, std::wstring& error);
    bool FetchManifest(const std::wstring& gameId, ServerGameManifest& manifest, std::wstring& error);

    // Per-game changelog entries (newest first) from GET /api/games/:id/changelogs.
    struct ChangelogEntry {
        std::wstring version;
        std::wstring title;
        std::wstring body;
        int64_t      createdAt = 0;   // epoch seconds
    };
    bool FetchChangelogs(const std::wstring& gameId,
                         std::vector<ChangelogEntry>& out, std::wstring& error);

    // ── Cloud saves ──────────────────────────────────────────────────────────
    // Per-user save files stored server-side, keyed by raw server game id and
    // a forward-slash relative path. mtime is local file mtime (epoch seconds).
    struct SaveFileInfo {
        std::wstring path;     // relative, forward slashes
        int64_t      mtime = 0;
        uint64_t     size = 0;
    };
    bool ListSaves(const std::wstring& serverGameId,
                   std::vector<SaveFileInfo>& out, std::wstring& error);
    bool DownloadSaveFile(const std::wstring& serverGameId,
                          const std::wstring& relPath,
                          std::string& bytes, std::wstring& error);
    bool UploadSaveFile(const std::wstring& serverGameId,
                        const std::wstring& relPath, int64_t mtime,
                        const std::string& bytes, std::wstring& error);

    // ── Account self-service (#21 / #22) ────────────────────────────────────
    struct AccountInfo {
        std::wstring username;
        std::wstring email;
        bool isAdmin = false;
        bool totpEnabled = false;
        bool mustChangePassword = false;
        int64_t avatarVersion = 0;   // 0 = no avatar; bumps each time it changes
    };
    bool GetAccount(AccountInfo& info, std::wstring& error);

    // Profile picture (server-synced avatar). GetAvatar returns raw image bytes
    // (PNG/JPEG/…); UploadAvatar sends raw bytes with an image/* content type;
    // DeleteAvatar removes it.
    bool GetAvatar(std::string& bytes, std::wstring& error);
    bool UploadAvatar(const std::string& bytes, const std::wstring& contentType,
                      std::wstring& error);
    bool DeleteAvatar(std::wstring& error);
    bool ChangePassword(const std::wstring& currentPassword,
                        const std::wstring& newPassword, std::wstring& error);
    // Begin TOTP enrollment: returns the base32 secret + otpauth:// URI for QR.
    bool TotpSetup(const std::wstring& password, std::wstring& secret,
                   std::wstring& otpauthUri, std::wstring& error);
    bool TotpEnable(const std::wstring& code, std::wstring& error);
    bool TotpDisable(const std::wstring& password, const std::wstring& code,
                     std::wstring& error);
    ServerInstallResult InstallGame(const Game& game,
                                    std::function<void(uint64_t, uint64_t)> onProgress = {});
    ServerValidateResult ValidateGame(const Game& game,
                                      std::function<void(uint64_t, uint64_t)> onProgress = {});
    bool UninstallGame(const Game& game, std::wstring& error);

    // Authenticate now (logs in only if no token is cached) and expose the
    // resulting token so the caller can persist and share it across clients.
    bool Authenticate(std::wstring& error);
    const std::wstring& AuthToken() const { return m_cfg.authToken; }

private:
    ServerConfig m_cfg;

    std::wstring Url(const std::wstring& path) const;
    bool HttpGet(const std::wstring& url,
                 std::string& body,
                 std::wstring& error,
                 const std::wstring& rangeHeader = L"",
                 bool allowReauth = true);
    bool HttpPostForm(const std::wstring& url,
                      const std::wstring& formBody,
                      std::string& body,
                      std::wstring& error,
                      bool withAuth = false,
                      bool allowReauth = true);
    // Generic authenticated request with a raw (possibly binary) body and an
    // explicit verb/content-type. Used for avatar upload/delete.
    bool HttpSendRaw(const std::wstring& verb,
                     const std::wstring& url,
                     const std::wstring& contentType,
                     const std::string& data,
                     std::string& body,
                     std::wstring& error);
    bool EnsureAuthenticated(std::wstring& error);
    // Major.minor of client and server must match (patch floats freely — it
    // auto-bumps every commit). Checked once per instance via /api/health;
    // mismatch blocks every server operation. 0 = unchecked, 1 = ok, -1 = bad.
    bool CheckServerVersion(std::wstring& error);
    int  m_versionState = 0;
    std::wstring m_versionError;
    bool TryChallengeResponse(std::wstring& error);
    bool DownloadFile(const std::wstring& url,
                      const std::wstring& dest,
                      uint64_t expectedSize,
                      std::function<void(uint64_t)> onFileProgress,
                      std::wstring& error);
    bool DownloadChunkedFile(const ServerFileEntry& file,
                             const std::wstring& dest,
                             std::function<void(uint64_t)> onFileProgress,
                             std::wstring& error);
};
