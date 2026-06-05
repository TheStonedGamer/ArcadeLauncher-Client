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
};

struct ServerInstallResult {
    bool ok = false;
    std::wstring error;
    std::wstring installRoot;
    std::wstring launchPath;
    std::wstring arguments;
    std::wstring version;
};

class ServerClient {
public:
    explicit ServerClient(ServerConfig cfg);

    bool FetchCatalog(std::vector<Game>& games, std::wstring& error);
    bool FetchManifest(const std::wstring& gameId, ServerGameManifest& manifest, std::wstring& error);
    ServerInstallResult InstallGame(const Game& game,
                                    std::function<void(uint64_t, uint64_t)> onProgress = {});

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
                      std::wstring& error);
    bool EnsureAuthenticated(std::wstring& error);
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
