#include "pch.h"
#include "AssetEnsure.h"
#include "EmulatorDownloader.h"   // DownloadFile, InstallEmulatorSync, SetupXemuFirmware
#include "DuckStationConfig.h"    // DuckBiosDir, DuckConfigureBiosIni

#include <atomic>
#include <thread>
#include <vector>
#include <filesystem>

// ── Small helpers ─────────────────────────────────────────────────────────────

static bool FileExists(const std::wstring& p) {
    return !p.empty() && GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Directory containing an executable (no trailing slash), or empty.
static std::wstring DirOf(const std::wstring& exe) {
    auto sl = exe.find_last_of(L"\\/");
    return (sl == std::wstring::npos) ? std::wstring() : exe.substr(0, sl);
}

static std::wstring TrimSlash(std::wstring v) {
    while (!v.empty() && (v.back() == L'/' || v.back() == L'\\')) v.pop_back();
    return v;
}

std::wstring EmulatorArchiveUrl(const AppConfig& cfg, const wchar_t* archiveName) {
    std::wstring base = TrimSlash(cfg.server.baseUrl.empty()
        ? L"https://arcade.orlandoaio.net"
        : cfg.server.baseUrl);
    return base + L"/emulators/" + archiveName;
}

// Run a command line and wait (bounded) for it to finish. Returns true if the
// process started; the exit code is not inspected.
static bool RunAndWait(const std::wstring& cmdLine, DWORD timeoutMs) {
    std::wstring mutableCmd = cmdLine;  // CreateProcessW may modify the buffer
    STARTUPINFOW si{ sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, timeoutMs);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// ── Worker ────────────────────────────────────────────────────────────────────

static std::atomic_flag s_running = ATOMIC_FLAG_INIT;

static void Worker(HWND hwnd, AppConfig cfg, std::wstring appDataDir) {
    auto toast = [&](const wchar_t* title, const wchar_t* msg) {
        auto* t = new AssetToast{ title, msg };
        if (!PostMessageW(hwnd, WM_APP_ASSETS_TOAST, 0, (LPARAM)t)) delete t;
    };

    bool announced = false;
    int  changed   = 0;
    auto announce = [&]() {
        if (!announced) {
            announced = true;
            toast(L"Emulator setup", L"Downloading missing emulator files…");
        }
    };

    // ── 1. Emulators ──────────────────────────────────────────────────────────
    // Mirrors the FirstLaunchSetup spec table; all are server-hosted (directUrl),
    // so a missing exe is re-fetched from the server. `curPath` is mutated as we
    // go so the extras pass below sees a freshly installed emulator.
    struct EmuEntry {
        EmuSlot       slot;
        const wchar_t* archive;
        const wchar_t* exeName;
        const wchar_t* destName;
        std::wstring*  curPath;   // into our local cfg copy
    };
    EmuEntry emus[] = {
        { EmuSlot::Dolphin,     L"dolphin-x64.7z",                       L"Dolphin.exe",                       L"dolphin",      &cfg.emulators.dolphinPath },
        { EmuSlot::Ryujinx,     L"ryujinx-win-x64.zip",                  L"Ryujinx.exe",                       L"ryujinx",      &cfg.emulators.ryujinxPath },
        { EmuSlot::Rpcs3,       L"rpcs3-win64.7z",                       L"rpcs3.exe",                         L"rpcs3",        &cfg.emulators.rpcs3Path },
        { EmuSlot::N64,         L"gopher64-windows-x86_64.exe",          L"gopher64.exe",                      L"gopher64",     &cfg.emulators.n64Path },
        { EmuSlot::NesSnes,     L"mesen-windows.zip",                    L"Mesen.exe",                         L"mesen2",       &cfg.emulators.nesPath },
        { EmuSlot::Duckstation, L"duckstation-windows-x64.zip",          L"duckstation-qt-x64-ReleaseLTCG.exe", L"duckstation", &cfg.emulators.duckstationPath },
        { EmuSlot::Pcsx2,       L"pcsx2-windows-x64.7z",                 L"pcsx2-qt.exe",                      L"pcsx2",        &cfg.emulators.pcsx2Path },
        { EmuSlot::Xenia,       L"xenia-canary-windows.zip",             L"xenia_canary.exe",                  L"xenia-canary", &cfg.emulators.xeniaPath },
        { EmuSlot::Xemu,        L"xemu-win-x86_64-release.zip",          L"xemu.exe",                          L"xemu",         &cfg.emulators.xemuPath },
    };

    for (auto& e : emus) {
        if (FileExists(*e.curPath)) continue;   // already installed
        announce();

        EmulatorDownloadSpec spec;
        spec.urlPattern = L"server";            // tag stored as "server" (mirrors first-launch)
        spec.exeName    = e.exeName;
        spec.destName   = e.destName;
        spec.directUrl  = EmulatorArchiveUrl(cfg, e.archive);

        EmuDownloadResult res = InstallEmulatorSync(spec, appDataDir);
        // Failures come back as exePath = L"ERR:..."; skip silently (server may
        // be down or the asset missing) — never block or error the user.
        if (res.exePath.empty() || res.exePath.compare(0, 4, L"ERR:") == 0)
            continue;

        *e.curPath = res.exePath;               // so extras below use the new exe
        ++changed;
        auto* r = new AssetEmuResult{ e.slot, res.exePath, res.tag };
        if (!PostMessageW(hwnd, WM_APP_ASSETS_EMU, 0, (LPARAM)r)) delete r;
    }

    // ── 2. Extras ─────────────────────────────────────────────────────────────

    // PS1 BIOS (DuckStation) — download if missing, then always point the ini.
    if (FileExists(cfg.emulators.duckstationPath)) {
        std::wstring biosDir = DuckBiosDir(cfg.emulators.duckstationPath);
        if (!biosDir.empty()) {
            std::wstring dest = biosDir + L"\\scph1001.bin";
            if (!FileExists(dest)) {
                announce();
                namespace fs = std::filesystem;
                std::error_code ec; fs::create_directories(biosDir, ec);
                if (DownloadFile(EmulatorArchiveUrl(cfg, L"scph1001.bin"), dest))
                    ++changed;
            }
            if (FileExists(dest))
                DuckConfigureBiosIni(cfg.emulators.duckstationPath, L"scph1001.bin");
        }
    }

    // OG Xbox / xemu firmware — download any missing file and always rewrite
    // xemu.toml so settings are deployed. SetupXemuFirmware skips present files
    // (hdd.qcow2 is ~1 GB, one-time).
    if (FileExists(cfg.emulators.xemuPath)) {
        std::wstring xemuDestDir = appDataDir + L"\\emulators\\xemu";
        std::wstring fwDir = xemuDestDir + L"\\firmware";
        bool fwMissing = !FileExists(fwDir + L"\\bios.bin")
                      || !FileExists(fwDir + L"\\mcpx.bin")
                      || !FileExists(fwDir + L"\\hdd.qcow2");
        if (fwMissing) { announce(); ++changed; }
        SetupXemuFirmware(EmulatorArchiveUrl(cfg, L"xemu-firmware/"), xemuDestDir);
    }

    // PS3 firmware (RPCS3) — not bundled in the archive. If dev_flash hasn't been
    // populated, fetch PS3UPDAT.PUP and install it headless via `--installfw`.
    if (FileExists(cfg.emulators.rpcs3Path)) {
        std::wstring rpcs3Dir = DirOf(cfg.emulators.rpcs3Path);
        std::wstring flag = rpcs3Dir + L"\\dev_flash\\vsh\\etc\\version.txt";
        if (!rpcs3Dir.empty() && !FileExists(flag)) {
            announce();
            std::wstring pup = appDataDir + L"\\emulators\\PS3UPDAT.PUP";
            if (DownloadFile(EmulatorArchiveUrl(cfg, L"PS3UPDAT.PUP"), pup)) {
                std::wstring cmd = L"\"" + cfg.emulators.rpcs3Path +
                                   L"\" --installfw \"" + pup + L"\"";
                RunAndWait(cmd, 5 * 60 * 1000);   // up to 5 min
                DeleteFileW(pup.c_str());
                if (FileExists(flag)) ++changed;
            }
        }
    }

    if (changed > 0)
        toast(L"Emulator setup", L"All emulator files are ready");

    s_running.clear();
}

void EnsureServerAssetsAsync(HWND hwnd, AppConfig cfg, std::wstring appDataDir) {
    // Guard against a second pass (e.g. a focus/re-sync event) starting while one
    // is already running.
    if (s_running.test_and_set()) return;
    std::thread(Worker, hwnd, std::move(cfg), std::move(appDataDir)).detach();
}
