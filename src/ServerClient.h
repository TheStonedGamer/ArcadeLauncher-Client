#pragma once
#include "pch.h"
#include "Config.h"
#include "GameLibrary.h"

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

    // ── Account self-service (#21 / #22) ────────────────────────────────────
    struct AccountInfo {
        std::wstring username;
        std::wstring email;
        bool isAdmin = false;
        bool totpEnabled = false;
        bool mustChangePassword = false;
    };
    bool GetAccount(AccountInfo& info, std::wstring& error);
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
                 const std::wstring& rangeHeader = L"");
    bool HttpPostForm(const std::wstring& url,
                      const std::wstring& formBody,
                      std::string& body,
                      std::wstring& error,
                      bool withAuth = false);
    bool EnsureAuthenticated(std::wstring& error);
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
