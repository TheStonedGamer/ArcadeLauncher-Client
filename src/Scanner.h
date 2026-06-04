#pragma once
#include "GameLibrary.h"

class IScanner {
public:
    virtual ~IScanner() = default;
    virtual std::vector<Game> Scan() = 0;
    virtual Platform GetPlatform() const = 0;
};
