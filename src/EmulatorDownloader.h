#pragma once
#include "pch.h"

// Posted to the settings window when a download completes.
// WPARAM = page index, LPARAM = new EmuDownloadResult* (receiver must delete).
static constexpr UINT WM_EMUDOWNLOAD_DONE = WM_USER + 51;
static constexpr UINT WM_EMUDOWNLOAD_PROGRESS = WM_USER + 53;

struct EmuDownloadResult {
    std::wstring exePath;   // empty = exe not found after extract; L"ERR:..." = failure
    std::wstring tag;       // GitHub release tag_name; empty if unavailable
};

struct EmuDownloadProgress {
    uint64_t downloaded = 0;
    uint64_t total = 0;
};

struct EmulatorDownloadSpec {
    std::string  githubRepo;  // "owner/repo"
    std::wstring urlPattern;  // case-insensitive substring matched against asset download URL
    std::wstring exeName;     // exe filename to locate after extraction
    std::wstring destName;    // subfolder under %AppData%\GameLauncher\emulators
    std::wstring directUrl;    // optional direct archive/exe URL for non-GitHub release hosts
};

// Launches a background thread that fetches the latest GitHub release, downloads
// the matching asset, extracts it, and posts WM_EMUDOWNLOAD_DONE to hwnd.
void DownloadEmulatorAsync(HWND hwnd, int pageIdx, EmulatorDownloadSpec spec,
                           const std::wstring& appDataDir);

// Synchronous install used by the per-launch asset self-heal. Downloads,
// extracts and provisions extras for `spec`, returning the result directly
// (exePath set on success, or `L"ERR:..."`). Blocks the calling thread; run it
// off the UI thread. `onProgress` may be empty.
EmuDownloadResult InstallEmulatorSync(const EmulatorDownloadSpec& spec,
                                      const std::wstring& appDataDir,
                                      std::function<void(uint64_t, uint64_t)> onProgress = {});

// Provision xemu's Xbox firmware (bios.bin / mcpx.bin / hdd.qcow2) from the
// server and (re)write xemu.toml so the settings are deployed. `fwBaseUrl` is the
// firmware folder URL, e.g. http://host:port/emulators/xemu-firmware/. Missing
// files are downloaded (skip if present); the toml is rewritten every call.
void SetupXemuFirmware(const std::wstring& fwBaseUrl,
                       const std::wstring& xemuDestDir,
                       const std::function<void(uint64_t, uint64_t)>& onProgress = {});

// Synchronously download `url` to `destPath` over WinHTTP (http/https, ranged or
// whole-file). Returns true on success. Optional progress callback (done, total).
bool DownloadFile(const std::wstring& url, const std::wstring& destPath,
                  std::function<void(uint64_t, uint64_t)> onProgress = {});
