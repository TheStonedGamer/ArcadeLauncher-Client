#pragma once
#include "Scanner.h"

class EpicScanner : public IScanner {
public:
    explicit EpicScanner(std::vector<std::wstring> manifestDirs = {})
        : m_manifestDirs(std::move(manifestDirs)) {}

    std::vector<Game> Scan() override;
    Platform GetPlatform() const override { return Platform::Epic; }

private:
    std::vector<std::wstring> m_manifestDirs;
    std::wstring GetManifestDir();
    Game ParseManifest(const std::wstring& path);

    // Minimal JSON string extractor (Epic manifests are simple flat JSON)
    std::wstring JsonString(const std::string& json, const std::string& key);
};
