#include "pch.h"
#include "PlatformIcons.h"

static std::wstring RegRead(HKEY root, const wchar_t* subkey, const wchar_t* val) {
    HKEY hk; wchar_t buf[MAX_PATH]{};
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &hk) != ERROR_SUCCESS) return {};
    DWORD sz = sizeof(buf);
    RegQueryValueExW(hk, val, nullptr, nullptr, (LPBYTE)buf, &sz);
    RegCloseKey(hk);
    return buf;
}

// Check if a file exists
static bool FileExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring PlatformIcons::FindSteamExe() {
    std::wstring path = RegRead(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamExe");
    if (path.empty()) path = RegRead(HKEY_LOCAL_MACHINE, L"Software\\Valve\\Steam", L"InstallPath");
    if (!path.empty() && path.find(L".exe") == std::wstring::npos)
        path += L"\\Steam.exe";
    std::replace(path.begin(), path.end(), L'/', L'\\');
    return path;
}

std::wstring PlatformIcons::FindEpicExe() {
    // 1. HKCU EOS registry (most reliable — present even without launcher in PATH)
    std::wstring cmd = RegRead(HKEY_CURRENT_USER, L"SOFTWARE\\Epic Games\\EOS", L"ModSdkCommand");
    if (!cmd.empty()) {
        std::replace(cmd.begin(), cmd.end(), L'/', L'\\');
        if (FileExists(cmd)) return cmd;
    }

    // 2. HKLM AppDataPath walk-up
    std::wstring path = RegRead(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\WOW6432Node\\Epic Games\\EpicGamesLauncher", L"AppDataPath");
    if (!path.empty()) {
        for (int i = 0; i < 2; ++i) {
            size_t s = path.rfind(L'\\');
            if (s != std::wstring::npos) path = path.substr(0, s);
        }
        std::wstring exe = path + L"\\Portal\\Binaries\\Win64\\EpicGamesLauncher.exe";
        if (FileExists(exe)) return exe;
    }

    // 3. Common install paths (Program Files and Program Files (x86))
    for (auto* root : {
        L"C:\\Program Files\\Epic Games\\Launcher\\Portal\\Binaries\\Win64\\EpicGamesLauncher.exe",
        L"C:\\Program Files (x86)\\Epic Games\\Launcher\\Portal\\Binaries\\Win64\\EpicGamesLauncher.exe"
    }) {
        if (FileExists(root)) return root;
    }
    return {};
}

std::wstring PlatformIcons::FindGogExe() {
    std::wstring path = RegRead(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\WOW6432Node\\GOG.com\\GalaxyClient", L"path");
    if (!path.empty()) {
        std::wstring exe = path + L"\\GalaxyClient.exe";
        if (FileExists(exe)) return exe;
    }
    return {};
}

std::wstring PlatformIcons::FindDolphinExe(const std::wstring& cfg) {
    if (!cfg.empty() && FileExists(cfg)) return cfg;

    // Scan multiple drives for common Dolphin install patterns
    static const wchar_t* drives[] = { L"C:", L"D:", L"E:", L"F:", L"G:", nullptr };
    static const wchar_t* suffixes[] = {
        L"\\Dolphin\\Dolphin-x64\\Dolphin.exe",
        L"\\Dolphin\\Dolphin.exe",
        L"\\Dolphin Emulator\\Dolphin.exe",
        L"\\Emulators\\Dolphin\\Dolphin-x64\\Dolphin.exe",
        L"\\Emulators\\Dolphin\\Dolphin.exe",
        L"\\Games\\Dolphin\\Dolphin.exe",
        nullptr
    };
    for (auto** d = drives; *d; ++d) {
        for (auto** s = suffixes; *s; ++s) {
            std::wstring full = std::wstring(*d) + *s;
            if (FileExists(full)) return full;
        }
    }

    // Program Files fallback
    for (auto* p : {
        L"C:\\Program Files\\Dolphin Emulator\\Dolphin.exe",
        L"C:\\Program Files (x86)\\Dolphin Emulator\\Dolphin.exe"
    }) { if (FileExists(p)) return p; }

    return {};
}

std::wstring PlatformIcons::FindRyujinxExe(const std::wstring& cfg) {
    if (!cfg.empty() && FileExists(cfg)) return cfg;

    // Scan multiple drives
    static const wchar_t* drives[] = { L"C:", L"D:", L"E:", L"F:", L"G:", nullptr };
    static const wchar_t* suffixes[] = {
        L"\\ryujinx\\Ryujinx.exe",
        L"\\Ryujinx\\Ryujinx.exe",
        L"\\ryujinx\\publish\\Ryujinx.exe",
        L"\\Ryujinx\\publish\\Ryujinx.exe",
        L"\\Emulators\\Ryujinx\\Ryujinx.exe",
        L"\\Games\\Ryujinx\\Ryujinx.exe",
        nullptr
    };
    for (auto** d = drives; *d; ++d) {
        for (auto** s = suffixes; *s; ++s) {
            std::wstring full = std::wstring(*d) + *s;
            if (FileExists(full)) return full;
        }
    }

    // AppData BSManager / portable
    wchar_t appData[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData);
    std::wstring ap = appData;
    for (auto* suf : { L"\\Ryujinx\\Ryujinx.exe", L"\\Ryujinx\\publish\\Ryujinx.exe" }) {
        std::wstring full = ap + suf;
        if (FileExists(full)) return full;
    }
    return {};
}

// ── FitGirl / Repacks icon ────────────────────────────────────────────────────

std::wstring PlatformIcons::DownloadRepacksIcon(const std::wstring& appDataDir) {
    std::wstring dest = appDataDir + L"\\repacks_icon.png";
    if (FileExists(dest)) return dest;

    // Try a few FitGirl URLs in order
    static const wchar_t* urls[] = {
        L"https://fitgirl-repacks.site/wp-content/uploads/2016/04/cropped-fgicon.png",
        L"https://fitgirl-repacks.site/favicon.ico",
        nullptr
    };
    for (auto** url = urls; *url; ++url) {
        if (HttpDownload(*url, dest)) return dest;
    }
    return {};
}

bool PlatformIcons::HttpDownload(const std::wstring& url, const std::wstring& dest) {
    URL_COMPONENTSW uc{}; uc.dwStructSize = sizeof(uc);
    wchar_t host[512]{}, path[2048]{};
    uc.lpszHostName = host; uc.dwHostNameLength = 512;
    uc.lpszUrlPath  = path; uc.dwUrlPathLength  = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;

    HINTERNET hSess = WinHttpOpen(L"ArcadeLauncher/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) return false;
    // 5s connect/send/receive timeout so a slow site doesn't freeze first launch
    WinHttpSetTimeouts(hSess, 5000, 5000, 5000, 5000);

    HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return false; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    bool ok = false;
    if (hReq && WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
             && WinHttpReceiveResponse(hReq, nullptr)) {
        DWORD status = 0; DWORD sz = sizeof(status);
        WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            nullptr, &status, &sz, nullptr);
        if (status == 200) {
            std::vector<BYTE> data;
            DWORD read = 0; BYTE buf[4096];
            while (WinHttpReadData(hReq, buf, sizeof(buf), &read) && read > 0)
                data.insert(data.end(), buf, buf + read);
            if (!data.empty()) {
                HANDLE hf = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hf != INVALID_HANDLE_VALUE) {
                    DWORD written;
                    WriteFile(hf, data.data(), (DWORD)data.size(), &written, nullptr);
                    CloseHandle(hf);
                    ok = true;
                }
            }
        }
        WinHttpCloseHandle(hReq);
    }
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return ok;
}

// ── Console icon cache filenames and download URLs ────────────────────────────

struct ConsoleIconEntry {
    Platform    platform;
    const wchar_t* cacheFile;  // filename under AppData/ArcadeLauncher
    const wchar_t* primaryUrl;
    const wchar_t* fallbackUrl;
};

static const ConsoleIconEntry kConsoleIcons[] = {
    { Platform::PS1,
      L"icon_ps.ico",
      L"https://www.playstation.com/favicon.ico",
      L"https://www.sony.com/favicon.ico" },
    { Platform::PS2,
      L"icon_ps.ico",   // shares the PlayStation favicon with PS1
      L"https://www.playstation.com/favicon.ico",
      L"https://www.sony.com/favicon.ico" },
    { Platform::Xbox360,
      L"icon_xbox.ico",
      L"https://www.xbox.com/favicon.ico",
      L"https://www.microsoft.com/favicon.ico" },
    { Platform::Xbox,
      L"icon_xbox.ico", // shares Xbox favicon with Xbox 360
      L"https://www.xbox.com/favicon.ico",
      L"https://www.microsoft.com/favicon.ico" },
};

// ── Load all icons ────────────────────────────────────────────────────────────

void PlatformIcons::Load(const EmulatorConfig& emuCfg,
                          ID2D1RenderTarget* rt, IWICImagingFactory* wic) {
    struct PlatformExe { Platform p; std::wstring exe; };
    std::vector<PlatformExe> entries = {
        { Platform::Steam,   FindSteamExe() },
        { Platform::Epic,    FindEpicExe() },
        { Platform::GOG,     FindGogExe() },
        { Platform::Dolphin, FindDolphinExe(emuCfg.dolphinPath) },
        { Platform::Ryujinx, FindRyujinxExe(emuCfg.ryujinxPath) },
        { Platform::RPCS3,   emuCfg.rpcs3Path       },
        { Platform::N64,     emuCfg.n64Path         },
        { Platform::NES,     emuCfg.nesPath         },
        { Platform::SNES,    emuCfg.snesPath        },
        { Platform::PS1,     emuCfg.duckstationPath },
        { Platform::PS2,     emuCfg.pcsx2Path       },
        { Platform::Xbox360, emuCfg.xeniaPath       },
        { Platform::Xbox,    emuCfg.xemuPath        },
    };

    for (auto& e : entries) {
        if (e.exe.empty() || GetFileAttributesW(e.exe.c_str()) == INVALID_FILE_ATTRIBUTES)
            continue;
        auto bmp = LoadFromExe(e.exe, rt, wic);
        if (bmp) m_icons[(int)e.p] = std::move(bmp);
    }

    // Repacks: load from cache (non-blocking; async download via TryDownloadAndLoadRepacks)
    std::wstring appDir = GetAppDataPath();
    std::wstring repacksPath = appDir + L"\\repacks_icon.png";
    if (FileExists(repacksPath)) {
        auto bmp = LoadFromFile(repacksPath, rt, wic);
        if (bmp) m_icons[(int)Platform::Repacks] = std::move(bmp);
    }

    // Console platforms: if exe extraction didn't produce an icon, fall back to
    // the cached favicon (downloaded asynchronously via DownloadConsoleIcons).
    for (auto& ci : kConsoleIcons) {
        if (m_icons.count((int)ci.platform)) continue;  // already have it from exe
        std::wstring path = appDir + L"\\" + ci.cacheFile;
        if (!FileExists(path)) continue;
        auto bmp = LoadFromFile(path, rt, wic);
        if (bmp) m_icons[(int)ci.platform] = std::move(bmp);
    }

    // PS1 and PS2 previously shared a web favicon fallback, which made the
    // sidebar ambiguous and could leave both blank if the download failed.
    // Keep exe/cached icons when available; otherwise use deterministic badges.
    for (Platform p : { Platform::PS1, Platform::PS2 }) {
        if (m_icons.count((int)p)) continue;
        auto bmp = CreateGeneratedIcon(p, rt);
        if (bmp) m_icons[(int)p] = std::move(bmp);
    }
}

bool PlatformIcons::DownloadConsoleIcons(const std::wstring& appDataDir) {
    // PS1 and PS2 share one cached file; Xbox and Xbox360 share another.
    // Download each unique file only once.
    const std::wstring psFile   = appDataDir + L"\\icon_ps.ico";
    const std::wstring xboxFile = appDataDir + L"\\icon_xbox.ico";

    bool any = false;
    if (!FileExists(psFile)) {
        any |= HttpDownload(L"https://www.playstation.com/favicon.ico", psFile)
            || HttpDownload(L"https://www.sony.com/favicon.ico",        psFile);
    }
    if (!FileExists(xboxFile)) {
        any |= HttpDownload(L"https://www.xbox.com/favicon.ico",        xboxFile)
            || HttpDownload(L"https://www.microsoft.com/favicon.ico",   xboxFile);
    }
    return any;
}

void PlatformIcons::TryLoadConsoleIcons(ID2D1RenderTarget* rt, IWICImagingFactory* wic) {
    std::wstring appDir = GetAppDataPath();
    for (auto& ci : kConsoleIcons) {
        std::wstring path = appDir + L"\\" + ci.cacheFile;
        if (!FileExists(path)) continue;
        auto bmp = LoadFromFile(path, rt, wic);
        if (bmp) m_icons[(int)ci.platform] = std::move(bmp);
    }

    for (Platform p : { Platform::PS1, Platform::PS2 }) {
        if (m_icons.count((int)p)) continue;
        auto bmp = CreateGeneratedIcon(p, rt);
        if (bmp) m_icons[(int)p] = std::move(bmp);
    }
}

bool PlatformIcons::TryDownloadAndLoadRepacks(ID2D1RenderTarget* rt,
                                               IWICImagingFactory* wic) {
    std::wstring appDir = GetAppDataPath();
    CreateDirectoryW(appDir.c_str(), nullptr);
    std::wstring iconPath = DownloadRepacksIcon(appDir);
    if (iconPath.empty()) return false;
    auto bmp = LoadFromFile(iconPath, rt, wic);
    if (!bmp) return false;
    m_icons[(int)Platform::Repacks] = std::move(bmp);
    return true;
}

ID2D1Bitmap* PlatformIcons::Get(Platform p) const {
    auto it = m_icons.find((int)p);
    return (it != m_icons.end()) ? it->second.Get() : nullptr;
}

// ── Bitmap loaders ────────────────────────────────────────────────────────────

ComPtr<ID2D1Bitmap> PlatformIcons::LoadFromFile(const std::wstring& filePath,
                                                  ID2D1RenderTarget* rt,
                                                  IWICImagingFactory* wic) {
    if (!rt || !wic || filePath.empty()) return nullptr;

    ComPtr<IWICBitmapDecoder>     dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter>   conv;

    if (FAILED(wic->CreateDecoderFromFilename(filePath.c_str(), nullptr,
               GENERIC_READ, WICDecodeMetadataCacheOnLoad, dec.GetAddressOf())))
        return nullptr;
    if (FAILED(dec->GetFrame(0, frame.GetAddressOf()))) return nullptr;
    if (FAILED(wic->CreateFormatConverter(conv.GetAddressOf()))) return nullptr;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
               WICBitmapDitherTypeNone, nullptr, 0.0,
               WICBitmapPaletteTypeMedianCut))) return nullptr;

    ComPtr<ID2D1Bitmap> bmp;
    rt->CreateBitmapFromWicBitmap(conv.Get(), nullptr, bmp.GetAddressOf());
    return bmp;
}

ComPtr<ID2D1Bitmap> PlatformIcons::LoadFromExe(const std::wstring& exePath,
                                                ID2D1RenderTarget* rt,
                                                IWICImagingFactory*,
                                                int iconIndex) {
    HICON hIcon = nullptr;
    ExtractIconExW(exePath.c_str(), iconIndex, nullptr, &hIcon, 1);
    if (!hIcon) ExtractIconExW(exePath.c_str(), iconIndex, &hIcon, nullptr, 1);
    if (!hIcon) return nullptr;

    auto bmp = HIconToD2D(hIcon, rt);
    DestroyIcon(hIcon);
    return bmp;
}

ComPtr<ID2D1Bitmap> PlatformIcons::HIconToD2D(HICON hIcon, ID2D1RenderTarget* rt) {
    if (!hIcon) return nullptr;

    ICONINFO ii{};
    if (!GetIconInfo(hIcon, &ii)) return nullptr;

    BITMAP bm{};
    GetObject(ii.hbmColor ? ii.hbmColor : ii.hbmMask, sizeof(bm), &bm);
    int w = bm.bmWidth;
    int h = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;
    if (w <= 0 || h <= 0) {
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask)  DeleteObject(ii.hbmMask);
        return nullptr;
    }

    HDC hdc = CreateCompatibleDC(nullptr);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!hBmp) {
        DeleteDC(hdc);
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask)  DeleteObject(ii.hbmMask);
        return nullptr;
    }

    HBITMAP oldBmp = (HBITMAP)SelectObject(hdc, hBmp);
    RECT rc{ 0, 0, w, h };
    FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    DrawIconEx(hdc, 0, 0, hIcon, w, h, 0, nullptr, DI_NORMAL);
    GdiFlush();
    SelectObject(hdc, oldBmp);
    DeleteDC(hdc);

    // Fix alpha channel if DrawIconEx didn't set it
    DWORD* px = (DWORD*)pixels;
    bool hasAlpha = false;
    for (int i = 0; i < w * h; ++i)
        if ((px[i] >> 24) != 0) { hasAlpha = true; break; }
    if (!hasAlpha) {
        for (int i = 0; i < w * h; ++i)
            if (px[i] & 0x00FFFFFF) px[i] |= 0xFF000000;
    }

    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap> bmp;
    rt->CreateBitmap(D2D1::SizeU(w, h), pixels, w * 4, props, bmp.GetAddressOf());

    DeleteObject(hBmp);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask)  DeleteObject(ii.hbmMask);
    return bmp;
}

ComPtr<ID2D1Bitmap> PlatformIcons::CreateGeneratedIcon(Platform platform,
                                                        ID2D1RenderTarget* rt) {
    if (!rt) return nullptr;

    constexpr int W = 32;
    constexpr int H = 32;
    std::vector<DWORD> pixels(W * H, 0);

    auto pack = [](BYTE r, BYTE g, BYTE b, BYTE a) -> DWORD {
        return ((DWORD)a << 24) | ((DWORD)r << 16) | ((DWORD)g << 8) | b;
    };
    auto toByte = [](float v) -> BYTE {
        v = std::max(0.0f, std::min(1.0f, v));
        return (BYTE)(v * 255.0f + 0.5f);
    };

    D2D1_COLOR_F c = PlatformColor(platform);
    BYTE baseR = toByte(c.r), baseG = toByte(c.g), baseB = toByte(c.b);
    BYTE darkR = (BYTE)(baseR * 0.45f);
    BYTE darkG = (BYTE)(baseG * 0.45f);
    BYTE darkB = (BYTE)(baseB * 0.45f);

    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float dx = (float)x - 15.5f;
            float dy = (float)y - 15.5f;
            float dist2 = dx * dx + dy * dy;
            if (dist2 > 15.5f * 15.5f) continue;

            bool rim = dist2 > 13.8f * 13.8f;
            float t = (float)y / (float)(H - 1);
            BYTE r = rim ? (BYTE)255 : (BYTE)(baseR * (1.0f - t) + darkR * t);
            BYTE g = rim ? (BYTE)255 : (BYTE)(baseG * (1.0f - t) + darkG * t);
            BYTE b = rim ? (BYTE)255 : (BYTE)(baseB * (1.0f - t) + darkB * t);
            BYTE a = rim ? (BYTE)220 : (BYTE)255;
            pixels[y * W + x] = pack(r, g, b, a);
        }
    }

    static const char* kOne[7] = {
        "0110",
        "1110",
        "0110",
        "0110",
        "0110",
        "0110",
        "1111",
    };
    static const char* kTwo[7] = {
        "1110",
        "0001",
        "0001",
        "0110",
        "1000",
        "1000",
        "1111",
    };
    const char** glyph = (platform == Platform::PS2) ? kTwo : kOne;
    int scale = 3;
    int glyphW = 4 * scale;
    int glyphH = 7 * scale;
    int ox = (W - glyphW) / 2;
    int oy = (H - glyphH) / 2;

    for (int gy = 0; gy < 7; ++gy) {
        for (int gx = 0; gx < 4; ++gx) {
            if (glyph[gy][gx] != '1') continue;
            for (int sy = 0; sy < scale; ++sy) {
                for (int sx = 0; sx < scale; ++sx) {
                    int px = ox + gx * scale + sx;
                    int py = oy + gy * scale + sy;
                    if (px >= 0 && px < W && py >= 0 && py < H)
                        pixels[py * W + px] = pack(255, 255, 255, 255);
                }
            }
        }
    }

    D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                          D2D1_ALPHA_MODE_PREMULTIPLIED));

    ComPtr<ID2D1Bitmap> bmp;
    rt->CreateBitmap(D2D1::SizeU(W, H), pixels.data(), W * sizeof(DWORD),
                     props, bmp.GetAddressOf());
    return bmp;
}
