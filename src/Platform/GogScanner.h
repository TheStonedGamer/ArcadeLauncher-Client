#pragma once
#include "Scanner.h"

class GogScanner : public IScanner {
public:
    std::vector<Game> Scan() override;
    Platform GetPlatform() const override { return Platform::GOG; }

private:
    std::wstring RegReadString(HKEY root, const std::wstring& subkey,
                               const std::wstring& valueName);
};
