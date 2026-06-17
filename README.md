# ArcadeLauncher (Client)

> **Retired (T10c):** This native C++/Win32 client is no longer maintained.
> Install the unified cross-platform client instead:
> [ArcadeLauncher-Unified-Client releases](https://github.com/TheStonedGamer/ArcadeLauncher-Unified-Client/releases/latest).
> Existing installs show a one-time migration notice on launch; the GitHub
> auto-update channel for this repo is disabled.

A native **C++17 / Win32 / Direct2D** game launcher for Windows. It presents a
unified, controller-friendly library across local emulators, PC storefronts
(Steam / Epic / GOG), and a private **ArcadeLauncher Server** catalog that streams
server-hosted games on demand.

No external runtime dependencies — Direct2D/DirectWrite/WIC, WinHTTP, and a
bundled LZMA SDK are all that's used, statically linked or shipped in the MSI.

## Features

- **Unified library** — emulator ROMs, PC storefront installs, and server-backed
  downloads in one Direct2D grid with cover art (IGDB-enriched).
- **Server downloads** — manifest + SHA-256 verified, HTTP byte-range resumable,
  run on a background worker so the UI never blocks. The install button queues a
  job; games are **not** auto-launched on completion (started manually).
- **Steam-style Downloads view** — a topbar Downloads button (count badge) opens
  a status window showing current download speed, disk-write speed, peak, a live
  throughput line graph, and the queue.
- **Per-platform tabs** — GameCube and Wii are surfaced as their own tabs (both
  launch through Dolphin); plus N64, SNES, NES, PS1/PS2, Xbox/Xbox360, Ryujinx,
  RPCS3, and PC repacks.
- **Periodic re-sync** — re-fetches the server catalog every 10 minutes and on
  window focus, preserving local install state.
- **Social** — friends list with requests, presence (online / away / in-game),
  direct messages, voice calls, and toast notifications, all over a persistent
  WebSocket gateway. Favorites, nicknames, and notification preferences are kept
  client-side. The panel reconnects automatically with backoff and an
  application-level heartbeat.

## Build

```powershell
# Build the exe only (fast iteration)
.\scripts\build.ps1 -SkipPackage

# Full build + MSI
.\scripts\build.ps1
```

Requires Visual Studio 2022 (Desktop C++ workload) and WiX v4 for packaging.
First-time setup pulls the LZMA SDK via `scripts\GetLzmaSDK.ps1`.

## Release pipeline

**Retired:** pushes to `main` no longer auto-release. The unified Tauri client
is the sole shipping product. To cut over existing native installs, dispatch
`server-client-release.yml` manually once so the EOL build (migration notice,
no self-update) reaches users on the old channel.

Tag-only and manual-dispatch releases still build the MSI when needed. The WiX
`UpgradeCode` (`DA9B3C2E-5F7A-4B8D-9C1E-0F2A3B4C5D6E`) is **permanent** — never
change it or in-place upgrades break.

## Server connection

The client talks to the ArcadeLauncher Server, typically via the reverse proxy
at `arcade.orlandoaio.net` (nginx on `10.0.0.203`) → upstream
`http://10.0.0.210:8721`. Auth is a bearer token obtained at login; downloads
send `Authorization: Bearer <token>` with `Range` headers.

## More

See [`DEVNOTES.md`](DEVNOTES.md) for the full architecture, source-file map,
the 9-step "add a platform" pattern, config/persistent-data layout, and known
gotchas.
