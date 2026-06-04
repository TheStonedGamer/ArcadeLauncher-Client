#pragma once
#include "Scanner.h"

class SteamScanner : public IScanner {
public:
    explicit SteamScanner(std::wstring pathOverride = {},
                          std::vector<std::wstring> extraFolders = {})
        : m_pathOverride(std::move(pathOverride))
        , m_extraFolders(std::move(extraFolders)) {}

    std::vector<Game> Scan() override;
    Platform GetPlatform() const override { return Platform::Steam; }

private:
    std::wstring              m_pathOverride;
    std::vector<std::wstring> m_extraFolders;
    std::wstring GetSteamInstallPath();
    std::vector<std::wstring> GetLibraryFolders(const std::wstring& steamPath);
    std::vector<Game> ScanLibraryFolder(const std::wstring& folder);

    // Minimal VDF parser: returns flat key-value map for a VDF block
    struct VdfNode {
        std::unordered_map<std::wstring, std::wstring> values;
        std::unordered_map<std::wstring, VdfNode>      children;
    };
    VdfNode ParseVdf(const std::wstring& text, size_t& pos);
    VdfNode ParseVdfFile(const std::wstring& path);
    std::wstring ReadVdfString(const std::wstring& text, size_t& pos);
};
