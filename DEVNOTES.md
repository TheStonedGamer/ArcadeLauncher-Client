# ArcadeLauncher — Developer Notes

Native C++17 Win32/Direct2D game launcher for Windows. No external runtime dependencies — everything is statically linked or bundled.

---

## Table of Contents

1. [Project Structure](#project-structure)
2. [Architecture Overview](#architecture-overview)
3. [Source File Map](#source-file-map)
4. [Platform Support](#platform-support)
5. [Config & Persistent Data](#config--persistent-data)
6. [Key Subsystems](#key-subsystems)
7. [Renderer & UI](#renderer--ui)
8. [Adding a New Emulator / Platform](#adding-a-new-emulator--platform)
9. [Build System](#build-system)
10. [Installer & Release Pipeline](#installer--release-pipeline)
11. [Version Management](#version-management)
12. [Known Gotchas](#known-gotchas)

---

## Project Structure

```
GameLauncher/
├── .github/workflows/
│   └── release.yml          # CI: build + MSI on version tag push
├── installer/
│   ├── ArcadeLauncher.wxs   # WiX v4 installer definition
│   └── License.rtf          # Shown in the installer UI
├── resources/
│   ├── ArcadeLauncher.rc    # VERSIONINFO + manifest reference
│   └── ArcadeLauncher.manifest  # DPI awareness + Common Controls v6
├── scripts/
│   ├── build.ps1            # Full build + MSI packaging script
│   └── GetLzmaSDK.ps1       # One-time: downloads LZMA SDK C sources
├── src/
│   ├── lzma/                # LZMA SDK (public domain, Igor Pavlov)
│   ├── Platform/            # Per-store scanners
│   ├── App.h / App.cpp      # Main application class + message loop
│   ├── Config.h / Config.cpp
│   ├── GameLibrary.h / .cpp
│   ├── Renderer.h / .cpp
│   ├── SettingsWindow.h / .cpp
│   ├── FirstLaunchSetup.h / .cpp
│   ├── PlatformIcons.h / .cpp
│   ├── EmulatorDownloader.h / .cpp
│   ├── EmulatorUpdateChecker.h / .cpp
│   ├── ArchiveExtractor.h / .cpp
│   ├── IgdbClient.h / .cpp
│   ├── MetadataManager.h / .cpp
│   ├── MetadataFetcher.h / .cpp
│   ├── MetadataPickerDialog.h / .cpp
│   ├── GameEditDialog.h / .cpp
│   ├── ProcessMonitor.h / .cpp
│   ├── Version.h            # Single source of truth for app version
│   ├── pch.h / pch.cpp      # Precompiled header + shared helpers
│   └── main.cpp
├── GameLauncher.sln
├── GameLauncher.vcxproj
└── DEVNOTES.md              # You are here
```

Output: `bin\Release\ArcadeLauncher.exe` / `bin\Debug\ArcadeLauncher.exe`  
Installer: `dist\ArcadeLauncher-x64.msi`

---

## Architecture Overview

```
main.cpp
  └── App (message pump)
        ├── Config          — load/save JSON settings
        ├── GameLibrary     — in-memory game list + JSON cache
        ├── Renderer (D2D)  — all drawing; hit-test; layout
        ├── SettingsWindow  — Win32 dialog (child HWND)
        ├── FirstLaunchSetup— first-run emulator downloader dialog
        ├── PlatformIcons   — D2D bitmaps extracted from exe icons
        ├── IgdbClient      — IGDB/Twitch REST API
        ├── MetadataManager — matches games → IGDB, writes to GameLibrary
        ├── MetadataFetcher — downloads cover art from SteamGridDB
        └── Scanners (background thread)
              ├── SteamScanner
              ├── EpicScanner
              ├── GogScanner
              └── EmulatorScanner (Dolphin, Ryujinx, RPCS3, N64, NES, SNES)
```

**Threading model**

| Thread | Responsibility |
|--------|---------------|
| Main (UI) | Win32 message loop, D2D rendering, all HWND access |
| Scan thread | `ScanAllPlatforms()` — fills `GameLibrary` then posts `WM_USER+1` |
| Download threads | `EmulatorDownloader`, `MetadataFetcher`, IGDB fetches |

All background threads communicate back via `PostMessageW` to the main HWND. Direct D2D/HWND calls from background threads are bugs.

### Server Branch Direction

`ArcadeLauncher-Server` adds a private backend track alongside the existing local scanner model. The goal is not streaming games from SMB; the launcher should install/download games locally, verify files, then launch them using the same emulator/direct-exe paths it already understands.

Initial backend prototype lives in `server/`:

- `server/arcade_server.py` serves a private catalog and generated per-game manifests.
- `server/catalog.example.json` documents the catalog shape.
- `server/README.md` documents setup and endpoints.

Core contract:

- `GET /api/catalog` returns library entries without file hashes.
- `GET /api/games/{id}/manifest` returns install files, byte sizes, SHA-256 hashes, download URLs, and launch target metadata.
- `GET /files/{id}/{relative-path}` serves files and supports HTTP byte ranges for resumable downloads.

Client-side work should preserve the current local library behavior. Server games should become another source of `Game` records with install states like missing, downloading, installed, and update available. Launch should only use local installed paths.

---

## Source File Map

| File | Role |
|------|------|
| `main.cpp` | Entry point; creates `App`, runs message loop |
| `App.h/.cpp` | Top-level coordinator; owns all subsystems; handles WM_* |
| `Config.h/.cpp` | `AppConfig` struct + hand-rolled JSON save/load |
| `GameLibrary.h/.cpp` | `Game` struct; `GameLibrary` owns the game list; JSON cache |
| `Renderer.h/.cpp` | All Direct2D drawing; hit-test helpers; scroll math |
| `SettingsWindow.h/.cpp` | Modal settings dialog built with raw Win32 controls |
| `FirstLaunchSetup.h/.cpp` | First-launch emulator download wizard |
| `PlatformIcons.h/.cpp` | Extracts platform icons from exe files → D2D bitmaps |
| `EmulatorDownloader.h/.cpp` | GitHub Releases download → extract → post result |
| `EmulatorUpdateChecker.h/.cpp` | GitHub Releases version check (background) |
| `ArchiveExtractor.h/.cpp` | 7z archive extraction using bundled LZMA SDK |
| `IgdbClient.h/.cpp` | Twitch/IGDB OAuth + game search REST calls via WinHTTP |
| `MetadataManager.h/.cpp` | Matches games to IGDB entries; updates GameLibrary |
| `MetadataFetcher.h/.cpp` | Downloads cover art from SteamGridDB |
| `MetadataPickerDialog.h/.cpp` | UI for manually picking an IGDB match |
| `GameEditDialog.h/.cpp` | Edit game title / launch path dialog |
| `ProcessMonitor.h/.cpp` | Watches launched game process; re-shows launcher on exit |
| `Scanner.h` | `IScanner` interface |
| `Platform/SteamScanner.cpp` | Parses `libraryfolders.vdf` + `appmanifest_*.acf` |
| `Platform/EpicScanner.cpp` | Parses `.item` manifest JSON files |
| `Platform/GogScanner.cpp` | Reads GOG uninstall registry keys |
| `Platform/EmulatorScanner.cpp` | Walks ROM dirs, associates with emulator exe |
| `pch.h` | Windows/D2D/WIC includes; `GetAppDataPath()`; string helpers |
| `Version.h` | App version constants — **the only place to bump the version** |

---

## Platform Support

| Platform | Scanner | Source | Enabled by default |
|----------|---------|--------|--------------------|
| Steam | `SteamScanner` | VDF manifests | Yes |
| Epic Games | `EpicScanner` | `.item` JSON manifests | Yes |
| GOG Galaxy | `GogScanner` | Registry | Yes |
| Dolphin (GC/Wii) | `EmulatorScanner` | ROM dirs | Yes (if exe found) |
| Ryujinx (Switch) | `EmulatorScanner` | ROM dirs | Yes (if exe found) |
| RPCS3 (PS3) | `EmulatorScanner` | ROM dirs | Yes (if exe found) |
| Gopher64 (N64) | `EmulatorScanner` | ROM dirs | Yes (if exe found) |
| Mesen2 (NES) | `EmulatorScanner` | ROM dirs | Yes (if exe found) |
| Mesen2 (SNES) | `EmulatorScanner` | ROM dirs | Yes (if exe found) |
| Custom libraries | `EmulatorScanner` | User-defined dirs | Per-library toggle |
| Repacks / FitGirl | — | Custom library | Per-library toggle |

---

## Config & Persistent Data

All persistent data lives in `%APPDATA%\ArcadeLauncher\`.

```
%APPDATA%\ArcadeLauncher\
├── config.json       — AppConfig (settings, emulator paths, IGDB creds)
├── library.json      — GameLibrary cache (all scanned games + metadata)
├── art\              — Cover art images (PNG), keyed by game ID
├── repacks_icon.png  — Cached FitGirl icon (downloaded on first launch)
├── tools\            — Optional: 7za.exe / 7z.exe for archive extraction
├── rpcs3\            — Downloaded RPCS3 installation (if auto-downloaded)
├── gopher64\         — Downloaded Gopher64 installation
└── mesen2\           — Downloaded Mesen2 installation
```

`GetAppDataPath()` in `pch.h` is the single definition of this path.

### AppConfig fields (Config.h)

```cpp
struct EmulatorConfig {
    bool         dolphinEnabled, ryujinxEnabled, rpcs3Enabled;
    bool         n64Enabled, nesEnabled, snesEnabled;
    std::wstring dolphinPath, ryujinxPath, rpcs3Path;
    std::wstring n64Path,     nesPath,    snesPath;
    std::wstring dolphinTag,  ryujinxTag, rpcs3Tag;   // installed version tags
    std::wstring n64Tag,      nesTag,     snesTag;
    std::vector<std::wstring> dolphinRomDirs, ryujinxRomDirs, rpcs3RomDirs;
    std::vector<std::wstring> n64RomDirs,     nesRomDirs,     snesRomDirs;
};

struct AppConfig {
    bool         startFullscreen   = false;
    bool         minimizeOnLaunch  = false;
    bool         firstLaunchDone   = false;
    std::wstring igdbClientId, igdbClientSecret;
    std::wstring igdbAccessToken;
    int64_t      igdbTokenExpiry   = 0;
    std::wstring steamGridDbApiKey;
    LibraryConfig  libraries;
    EmulatorConfig emulators;
};
```

Config is saved as hand-rolled JSON (no third-party parser). `Config::Save()` / `Config::Load()` are in `Config.cpp`.

---

## Key Subsystems

### Emulator Download Flow

1. User clicks "Download latest" in Settings (or first-launch wizard runs).
2. `DownloadEmulatorAsync(hwnd, page, spec, appDataDir)` starts a background thread.
3. Thread calls GitHub Releases API → finds asset matching `spec.urlPattern`.
4. Downloads to `%APPDATA%\ArcadeLauncher\<destName>\`.
5. If asset is `.exe` → uses it directly. Otherwise → extracts with LZMA SDK.
6. Posts `WM_EMUDOWNLOAD_DONE` (WM_USER+51) with `EmuDownloadResult*` to hwnd.
7. Handler updates the edit control, calls `SaveCurrentPage()` to persist path.

```cpp
struct EmulatorDownloadSpec {
    std::string  githubRepo;   // e.g. "gopher64/gopher64"
    std::wstring urlPattern;   // substring to match asset filename
    std::wstring exeName;      // expected exe name inside archive
    std::wstring destName;     // subdirectory under appDataDir
};
```

### Archive Extraction

`ArchiveExtractor.cpp` tries two strategies:
1. **Bundled LZMA SDK** — handles `.7z` archives in-process (no external tools needed).
2. **External 7-Zip** — falls back to `7za.exe` / `7z.exe` found via PATH, exe directory, or `%APPDATA%\ArcadeLauncher\tools\`.

If the downloaded asset ends in `.exe`, extraction is skipped entirely (Gopher64 ships as a plain exe).

### Version Check (Update Checker)

`CheckEmulatorUpdateAsync(hwnd, page, repo)` fetches the latest GitHub release tag and posts `WM_EMUCHECK_DONE` (WM_USER+52) with an `EmuCheckResult*`. The Settings page's `SetVersionLabel()` displays installed vs. latest.

### IGDB Metadata

1. User enters Twitch Client ID + Secret in Settings → General.
2. `IgdbClient` authenticates via Twitch OAuth (`/oauth2/token`).
3. On scan completion, `MetadataManager` fuzzy-matches each game title against IGDB.
4. Ambiguous matches show `MetadataPickerDialog`. Confirmed matches are written back to `GameLibrary`.
5. `MetadataFetcher` downloads cover art from SteamGridDB to `art\<gameId>.png`.

---

## Renderer & UI

The entire UI is drawn with Direct2D (`ID2D1HwndRenderTarget`). There are no Win32 controls in the main window — only in the modal dialogs (Settings, FirstLaunchSetup, GameEdit).

### Layout constants (Renderer.cpp)

```
┌──────────────────────────────────────────┐
│  Top bar  (m_topbarH = 64px)             │
├──────────┬───────────────────────────────┤
│ Sidebar  │  Game grid                   │
│ (200px)  │  Tiles: 180×260, gap 16px    │
│          │  Scrolls vertically          │
├──────────┴───────────────────────────────┤
│  Detail panel  (slides in from right)   │
└──────────────────────────────────────────┘
```

### Hit testing

All interaction uses `Renderer::HitTestGrid`, `HitTestSidebar`, `HitTestSearch`, `HitTestLaunchBtn`, `HitTestSettingsBtn`. Never read control state from HWND — query `RenderState`.

### RenderState

`RenderState` (in `Renderer.h`) is the single source of UI state: hovered/selected index, scroll offset, search query, active filter platform, focus area (Grid / Sidebar / Search), and sidebar visibility flags. The main message handler in `App.cpp` mutates this and calls `InvalidateRect` to trigger a repaint.

---

## Adding a New Emulator / Platform

1. **Config.h** — add `bool xyzEnabled`, `std::wstring xyzPath`, `std::wstring xyzTag`, `std::vector<std::wstring> xyzRomDirs` to `EmulatorConfig`.
2. **Config.cpp** — add Save/Load lines for the new fields.
3. **GameLibrary.h** — add `Platform::XYZ` to the `Platform` enum.
4. **Renderer.h** — add `showXyz = true` to `RenderState`; add `showXyz` to `BuildSidebarEntries()`.
5. **PlatformIcons.cpp** — add icon loading in `Load()`.
6. **Platform/EmulatorScanner.cpp** — handle `Platform::XYZ` in `Scan()`.
7. **App.cpp** — add `xyzEnabled` to `ScanAllPlatforms()` and `UpdateSidebarFlags()`.
8. **SettingsWindow.cpp** — add `PAGE_XYZ`, `BuildXyzPage()`, `LoadXyzPage()`, `SaveXyzPage()`, sidebar entry, download spec, command handler.
9. **FirstLaunchSetup.cpp** — optionally add to the auto-download entries.

---

## Build System

### Prerequisites

| Tool | Where to get |
|------|-------------|
| Visual Studio 2022 | visualstudio.microsoft.com (Desktop development with C++) |
| .NET SDK 8+ | dotnet.microsoft.com (needed for WiX CLI only) |
| WiX v4 CLI | `dotnet tool install --global wix --version 4.0.5` |

### Building in Visual Studio

Open `GameLauncher.sln` → set configuration to **Release \| x64** → Build.

Output: `bin\Release\ArcadeLauncher.exe`

### Building from the command line

```powershell
# Full build + MSI
.\scripts\build.ps1

# Build only (no installer)
.\scripts\build.ps1 -SkipPackage

# Package only (re-run installer on existing exe)
.\scripts\build.ps1 -SkipBuild

# With code signing
$env:SIGN_THUMBPRINT = "YOUR_CERT_THUMBPRINT"
.\scripts\build.ps1 -Sign
```

### LZMA SDK (one-time setup)

If `src\lzma\` is missing (fresh clone), run:

```powershell
.\scripts\GetLzmaSDK.ps1
```

This downloads the LZMA SDK C sources from the 7-Zip GitHub repository. They are compiled directly into the exe — no runtime 7-zip required.

---

## Installer & Release Pipeline

### WiX v4 installer (`installer\ArcadeLauncher.wxs`)

- Installs to `%ProgramFiles%\ArcadeLauncher\`
- Creates a Start-menu shortcut and (optional) desktop shortcut
- `MajorUpgrade` element: installing a newer MSI automatically uninstalls the old version
- `UpgradeCode` GUID is **fixed** — never change it (`DA9B3C2E-5F7A-4B8D-9C1E-0F2A3B4C5D6E`)
- Product `Version` is read from the exe's VERSIONINFO via `!(bind.FileVersion.MainExe)` — no manual version string needed in the WiX file

### Manual build

```powershell
# Install WiX once
dotnet tool install --global wix --version 4.0.5
wix extension add WixToolset.UI.wixext/4.0.5 --global

# Build the MSI
wix build installer\ArcadeLauncher.wxs `
    -ext WixToolset.UI.wixext `
    -d "BinDir=bin\Release" `
    -out "dist\ArcadeLauncher-x64.msi" `
    -arch x64
```

### GitHub Actions (`.github/workflows/release.yml`)

Triggers on `v*.*.*` tag pushes. Steps:
1. Checkout
2. Set up MSBuild + .NET + WiX v4
3. `msbuild GameLauncher.sln /p:Configuration=Release /p:Platform=x64`
4. `wix build ...` → `dist\ArcadeLauncher-x64.msi`
5. Create GitHub Release with MSI + bare exe as assets

### Installing / upgrading

- **Fresh install**: run `ArcadeLauncher-x64.msi`; choose install directory; creates Start-menu shortcut
- **Upgrade**: run the new `ArcadeLauncher-x64.msi`; Windows Installer detects the same `UpgradeCode` and replaces the previous installation automatically
- **Uninstall**: Add/Remove Programs → ArcadeLauncher, or `msiexec /x ArcadeLauncher-x64.msi`
- **Silent install** (e.g. for scripting): `msiexec /i ArcadeLauncher-x64.msi /quiet /qn`

---

## Version Management

All version numbers flow from a single file: **`src/Version.h`**.

```
src/Version.h
  → resources/ArcadeLauncher.rc  (VERSIONINFO stamped into exe)
    → installer/ArcadeLauncher.wxs  (reads version from exe VERSIONINFO)
      → GitHub Release tag
```

### Releasing a new version

1. Edit `src/Version.h` — bump `ARCADE_VERSION_MAJOR/MINOR/PATCH`.
2. Commit: `git commit -am "Bump version to 1.2.3"`
3. Tag: `git tag v1.2.3`
4. Push: `git push origin main v1.2.3`
5. GitHub Actions builds the MSI and creates the release automatically.

The MSI filename is always `ArcadeLauncher-x64.msi` regardless of version; the version is embedded in the MSI metadata and the exe's VERSIONINFO.

---

## Known Gotchas

**D2D bitmap creation must stay on the render thread.**  
D2D render targets are single-threaded. All `ID2D1Bitmap` creation (including `PlatformIcons::Load`, `Renderer::LoadGameArt`) must happen on the main/UI thread. Background threads signal completion via `PostMessageW`; the main thread creates the bitmap in the message handler.

**WM_EMUDOWNLOAD_DONE caller must `delete result`.**  
The `EmuDownloadResult*` in lParam is heap-allocated by the background thread and owned by the receiver. Always `delete result` at the end of the handler.

**`SaveCurrentPage()` before navigating away.**  
`SwitchPage()` calls `SaveCurrentPage()` → `BuildNewPage()` → `LoadCurrentPage()`. If you mutate `m_work` directly (e.g., in the download handler), it sticks. If you only call `SetWindowTextW` on an edit control, navigate away, and come back, `LoadCurrentPage()` re-reads from `m_work` and loses the change — which is why the download handler calls `SaveCurrentPage()` immediately after updating the edit control.

**`ARCADE_VERSION_WSTR` macro concatenation.**  
The `_AV_STR` macro expansion is numeric, so adjacent wide-string literal concatenation works: `L"v" ARCADE_VERSION_WSTR`. Do not mix with runtime `std::to_wstring` calls; use the macro form at compile time only.

**WiX `UpgradeCode` is permanent.**  
Changing the `UpgradeCode` GUID makes the new MSI appear as a completely different product to Windows Installer. Old installations are left behind. Never change it.

**rc.exe does not accept `#include "pch.h"`.**  
The resource compiler is not a C++ compiler. `ArcadeLauncher.rc` includes only `Version.h`, which is plain `#define`s with no C++ constructs.

**Gopher64 ships as a bare `.exe` asset, not an archive.**  
`EmulatorDownloader::Worker()` checks the asset filename extension and skips extraction if it ends in `.exe`. The `urlPattern` for Gopher64 is `windows-x86_64.exe` to avoid matching the `aarch64` variant.
