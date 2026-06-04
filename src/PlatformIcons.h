#pragma once
#include "pch.h"
#include "GameLibrary.h"
#include "Config.h"

// Loads 32x32 icons for each platform from installed application executables
// and caches them as D2D bitmaps.
class PlatformIcons {
public:
    PlatformIcons() = default;

    // Must be called after D2D render target is created.
    // Pass the current EmulatorConfig so configured exe paths are used for icons.
    void Load(const EmulatorConfig& emuCfg, ID2D1RenderTarget* rt, IWICImagingFactory* wic);

    // Returns the bitmap for a platform, or nullptr if not loaded.
    ID2D1Bitmap* Get(Platform p) const;

    // Auto-detect executable paths for each platform.
    static std::wstring FindSteamExe();
    static std::wstring FindEpicExe();
    static std::wstring FindGogExe();
    static std::wstring FindDolphinExe(const std::wstring& configuredPath);
    static std::wstring FindRyujinxExe(const std::wstring& configuredPath);

    // Download and cache the FitGirl/Repacks icon to appDataDir.
    // Returns path to the cached file, or empty on failure.
    static std::wstring DownloadRepacksIcon(const std::wstring& appDataDir);

    // Download PS1/PS2/Xbox/Xbox360 favicons to appDataDir if not cached yet.
    // Returns true if at least one icon was downloaded.
    static bool DownloadConsoleIcons(const std::wstring& appDataDir);

    // Called from the render thread after a background download completes.
    // Returns true if the bitmap was loaded and stored.
    bool TryDownloadAndLoadRepacks(ID2D1RenderTarget* rt, IWICImagingFactory* wic);

    // (Re-)load cached console icon files; call on the render thread.
    void TryLoadConsoleIcons(ID2D1RenderTarget* rt, IWICImagingFactory* wic);

private:
    ComPtr<ID2D1Bitmap> LoadFromExe(const std::wstring& exePath,
                                    ID2D1RenderTarget* rt,
                                    IWICImagingFactory* wic,
                                    int iconIndex = 0);
    ComPtr<ID2D1Bitmap> LoadFromFile(const std::wstring& filePath,
                                     ID2D1RenderTarget* rt,
                                     IWICImagingFactory* wic);
    ComPtr<ID2D1Bitmap> HIconToD2D(HICON hIcon, ID2D1RenderTarget* rt);
    ComPtr<ID2D1Bitmap> CreateGeneratedIcon(Platform platform, ID2D1RenderTarget* rt);

    static bool HttpDownload(const std::wstring& url, const std::wstring& dest);

    std::unordered_map<int, ComPtr<ID2D1Bitmap>> m_icons;
};
