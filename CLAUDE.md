# ArcadeLauncher Client — working notes for Claude

Native **C++17 / Win32 / Direct2D** Windows launcher. Hand-rolled everything:
JSON parsers, WinHTTP downloads, WIC image loading, wincrypt SHA-256. Read
[`DEVNOTES.md`](DEVNOTES.md) for the full architecture before large changes.

## Build & release
- Build exe only: `.\scripts\build.ps1 -SkipPackage`. Full MSI: `.\scripts\build.ps1`.
- **Pushing to `main` triggers GitHub Actions**, which auto-bumps `src/Version.h`
  and builds the MSI. Because Actions pushes a version-bump commit, local pushes
  are often rejected non-fast-forward — `git pull --rebase origin main` then push.
- **Bump type comes from the commit message**: default is patch; `[minor]` in the
  subject bumps minor and resets patch, `[major]` bumps major. Actions then tags
  `client-vX.Y.Z` and publishes a GitHub release with the MSI attached. For a
  coordinated release, push matching `[minor]`/`[major]` commits to BOTH repos.
- **Version lockstep**: client and server must share the same **major.minor**
  (patch floats). `ServerClient::CheckServerVersion` (called from
  `EnsureAuthenticated`, the chokepoint for every server op) reads
  `/api/health`'s `version` and refuses to connect on mismatch. Consequence:
  after a minor/major bump, the production server must be redeployed before
  clients update, or they are locked out.
- Repo: `github.com/TheStonedGamer/ArcadeLauncher-Client`.
- WiX `UpgradeCode` `DA9B3C2E-5F7A-4B8D-9C1E-0F2A3B4C5D6E` is **PERMANENT**.

## Server / networking
- Talks to the ArcadeLauncher Server via reverse proxy `arcade.orlandoaio.net`
  (nginx on `10.0.0.203`) → upstream `http://10.0.0.210:8721`.
- Downloads send `Authorization: Bearer <token>` + `Range`. `InstallGame` pulls
  each file as a **single resumable ranged GET** of `/files/<id>/<rel>` (one
  connection per file, streamed to `.part`, full-file SHA-256 verified after).
  The per-chunk `/chunks/` path (`DownloadChunkedFile`) is a fallback only when a
  manifest file has no `url`. Manifest paths with a `..` component are rejected
  before any write (`HasPathTraversal`).

## Key code locations
- `App.cpp` — message loop, `QueueServerInstall` (install button → background
  queue, **autoLaunch=false**), `DownloadWorker`, `OpenDownloadStatus` /
  `RefreshDownloadStatusWindow` (Steam-style speed graph + queue), `OnPaint`
  (sets `activeDownloadCount` badge).
- `Renderer.cpp` — topbar buttons. Downloads button rect is left of select-mode;
  both draw accent count badges. `BuildSidebarEntries` lists platform tabs
  (Dolphin tab removed; GameCube/Wii are separate, both → `dolphinPath`).
- `ServerClient.cpp` — `ParsePlatform` maps `"PC"`→`Platform::Repacks`.
  `InstallGame` streams files to disk (write-through, so download speed == disk
  write speed) then extracts pc_archive installs. `igdbMatched = igdbId > 0`
  caused the missing-cover bug; mitigated in `App.cpp` WM_USER+1 by re-arming
  IGDB search for games missing both cover URL and local path.

## Conventions / gotchas
- The user runs catalog scans/rescans **manually** — do not trigger them.
- Report build results honestly. The download status window's "disk write speed"
  intentionally equals download speed (write-through install); extraction is a
  separate phase but not instrumented for progress.
- Adding a platform follows a documented 9-step pattern in `DEVNOTES.md`.
