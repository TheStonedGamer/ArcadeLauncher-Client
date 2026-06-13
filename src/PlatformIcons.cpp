#include "pch.h"
#include "PlatformIcons.h"
#include "PlatformLogoRes.h"

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

// ── PC Games (Repacks) icon ───────────────────────────────────────────────────
// The PC Games platform is rendered with a neutral, natively-drawn vector tile
// (see CreateGeneratedIcon → Platform::Repacks). There is deliberately no branded
// third-party asset download here: the launcher must not pull marks from, or make
// network requests to, any specific repack site.

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
        // GameCube/Wii intentionally omitted here — they use the actual console
        // logos (downloaded via DownloadConsoleIcons), not the Dolphin exe icon.
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

    // Real console logos bundled in the exe. These take precedence over the
    // emulator exe icons above so each console tab shows the actual platform
    // logo (e.g. the N64 wordmark instead of the Gopher64 emulator icon).
    struct LogoRes { Platform p; int id; };
    static const LogoRes kEmbeddedLogos[] = {
        { Platform::GameCube, IDR_LOGO_GAMECUBE },
        { Platform::Wii,      IDR_LOGO_WII      },
        { Platform::N64,      IDR_LOGO_N64      },
        { Platform::NES,      IDR_LOGO_NES      },
        { Platform::SNES,     IDR_LOGO_SNES     },
        { Platform::PS1,      IDR_LOGO_PS1      },
        { Platform::PS2,      IDR_LOGO_PS2      },
        { Platform::RPCS3,    IDR_LOGO_PS3      },
        { Platform::Xbox360,  IDR_LOGO_XBOX360  },
        { Platform::Ryujinx,  IDR_LOGO_SWITCH   },
        { Platform::Xbox,     IDR_LOGO_XBOX     },
    };
    for (auto& l : kEmbeddedLogos) {
        auto bmp = LoadFromResource(l.id, rt, wic);
        if (bmp) m_icons[(int)l.p] = std::move(bmp);
    }

    // PC Games (Repacks) intentionally has no cached/branded bitmap — it falls
    // through to the natively-drawn CreateGeneratedIcon tile below.
    std::wstring appDir = GetAppDataPath();

    // Console platforms: if exe extraction didn't produce an icon, fall back to
    // the cached favicon (downloaded asynchronously via DownloadConsoleIcons).
    for (auto& ci : kConsoleIcons) {
        if (m_icons.count((int)ci.platform)) continue;  // already have it from exe
        std::wstring path = appDir + L"\\" + ci.cacheFile;
        if (!FileExists(path)) continue;
        auto bmp = LoadFromFile(path, rt, wic);
        if (bmp) m_icons[(int)ci.platform] = std::move(bmp);
    }

    // Keep exe/cached icons when available; otherwise use deterministic badges.
    for (Platform p : {
        Platform::Steam, Platform::Epic, Platform::GOG, Platform::Dolphin,
        Platform::GameCube, Platform::Wii,
        Platform::Ryujinx, Platform::RPCS3, Platform::N64, Platform::NES,
        Platform::SNES, Platform::PS1, Platform::PS2, Platform::Xbox360,
        Platform::Xbox, Platform::Repacks
    }) {
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
        // Never override a bundled console logo (loaded in Load) with the lower-
        // quality favicon fallback — only fill platforms that have no icon yet.
        if (m_icons.count((int)ci.platform)) continue;
        std::wstring path = appDir + L"\\" + ci.cacheFile;
        if (!FileExists(path)) continue;
        auto bmp = LoadFromFile(path, rt, wic);
        if (bmp) m_icons[(int)ci.platform] = std::move(bmp);
    }

    for (Platform p : {
        Platform::Dolphin, Platform::GameCube, Platform::Wii,
        Platform::Ryujinx, Platform::RPCS3, Platform::N64,
        Platform::NES, Platform::SNES, Platform::PS1, Platform::PS2,
        Platform::Xbox360, Platform::Xbox
    }) {
        if (m_icons.count((int)p)) continue;
        auto bmp = CreateGeneratedIcon(p, rt);
        if (bmp) m_icons[(int)p] = std::move(bmp);
    }
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

// Loads an embedded RT_RCDATA blob (PNG or ICO) by resource id and decodes it
// with WIC. These are the bundled console logos that ship inside the exe.
ComPtr<ID2D1Bitmap> PlatformIcons::LoadFromResource(int resId,
                                                     ID2D1RenderTarget* rt,
                                                     IWICImagingFactory* wic) {
    if (!rt || !wic) return nullptr;

    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!hRes) return nullptr;
    HGLOBAL hData = LoadResource(nullptr, hRes);
    if (!hData) return nullptr;
    void* p = LockResource(hData);
    DWORD  sz = SizeofResource(nullptr, hRes);
    if (!p || sz == 0) return nullptr;

    ComPtr<IWICStream> stream;
    if (FAILED(wic->CreateStream(stream.GetAddressOf()))) return nullptr;
    if (FAILED(stream->InitializeFromMemory((BYTE*)p, sz))) return nullptr;

    ComPtr<IWICBitmapDecoder>     dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter>   conv;
    if (FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr,
               WICDecodeMetadataCacheOnLoad, dec.GetAddressOf()))) return nullptr;
    if (FAILED(dec->GetFrame(0, frame.GetAddressOf()))) return nullptr;
    if (FAILED(wic->CreateFormatConverter(conv.GetAddressOf()))) return nullptr;
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
               WICBitmapDitherTypeNone, nullptr, 0.0,
               WICBitmapPaletteTypeMedianCut))) return nullptr;

    ComPtr<ID2D1Bitmap> bmp;
    rt->CreateBitmapFromWicBitmap(conv.Get(), nullptr, bmp.GetAddressOf());
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

    auto glyphFor = [](wchar_t ch) -> const char** {
        static const char* A[7]={"01110","10001","10001","11111","10001","10001","10001"};
        static const char* D[7]={"11110","10001","10001","10001","10001","10001","11110"};
        static const char* E[7]={"11111","10000","10000","11110","10000","10000","11111"};
        static const char* G[7]={"01111","10000","10000","10011","10001","10001","01110"};
        static const char* N[7]={"10001","11001","10101","10011","10001","10001","10001"};
        static const char* P[7]={"11110","10001","10001","11110","10000","10000","10000"};
        static const char* R[7]={"11110","10001","10001","11110","10100","10010","10001"};
        static const char* S[7]={"01111","10000","10000","01110","00001","00001","11110"};
        static const char* W[7]={"10001","10001","10001","10101","10101","10101","01010"};
        static const char* X[7]={"10001","10001","01010","00100","01010","10001","10001"};
        static const char* Y[7]={"10001","10001","01010","00100","00100","00100","00100"};
        static const char* Z[7]={"11111","00001","00010","00100","01000","10000","11111"};
        static const char* One[7]={"00100","01100","00100","00100","00100","00100","01110"};
        static const char* Two[7]={"01110","10001","00001","00010","00100","01000","11111"};
        static const char* Three[7]={"11110","00001","00001","01110","00001","00001","11110"};
        static const char* Four[7]={"10010","10010","10010","11111","00010","00010","00010"};
        static const char* Six[7]={"00110","01000","10000","11110","10001","10001","01110"};
        switch (towupper(ch)) {
        case L'A': return A; case L'D': return D; case L'E': return E; case L'G': return G;
        case L'N': return N; case L'P': return P; case L'R': return R; case L'S': return S;
        case L'W': return W; case L'X': return X; case L'Y': return Y; case L'Z': return Z;
        case L'1': return One; case L'2': return Two; case L'3': return Three;
        case L'4': return Four; case L'6': return Six;
        default: return A;
        }
    };
    auto labelFor = [](Platform p) -> std::wstring {
        switch (p) {
        case Platform::Steam: return L"S";
        case Platform::Epic: return L"E";
        case Platform::GOG: return L"G";
        case Platform::Dolphin: return L"D";
        case Platform::GameCube: return L"GC";
        case Platform::Wii: return L"Wii";
        case Platform::Ryujinx: return L"R";
        case Platform::RPCS3: return L"P3";
        case Platform::N64: return L"64";
        case Platform::NES: return L"N";
        case Platform::SNES: return L"SN";
        case Platform::PS1: return L"P1";
        case Platform::PS2: return L"P2";
        case Platform::Xbox360: return L"36";
        case Platform::Xbox: return L"X";
        case Platform::Repacks: return L"PC";
        }
        return L"A";
    };

    std::wstring label = labelFor(platform);
    int scale = label.size() > 1 ? 2 : 3;
    int glyphW = 5 * scale;
    int gap = scale;
    int totalW = (int)label.size() * glyphW + ((int)label.size() - 1) * gap;
    int glyphH = 7 * scale;
    int ox = (W - totalW) / 2;
    int oy = (H - glyphH) / 2;

    for (size_t li = 0; li < label.size(); ++li) {
        const char** glyph = glyphFor(label[li]);
        int lx = ox + (int)li * (glyphW + gap);
        for (int gy = 0; gy < 7; ++gy) {
            for (int gx = 0; gx < 5; ++gx) {
                if (glyph[gy][gx] != '1') continue;
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        int px = lx + gx * scale + sx;
                        int py = oy + gy * scale + sy;
                        if (px >= 0 && px < W && py >= 0 && py < H)
                            pixels[py * W + px] = pack(255, 255, 255, 255);
                    }
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
