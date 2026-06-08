# ArcadeLauncher (Client)

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

Pushing to `main` triggers the GitHub Actions workflow, which auto-bumps
`src/Version.h`, builds, and produces the MSI. The WiX `UpgradeCode`
(`DA9B3C2E-5F7A-4B8D-9C1E-0F2A3B4C5D6E`) is **permanent** — never change it or
in-place upgrades break.

## Server connection

The client talks to the ArcadeLauncher Server, typically via the reverse proxy
at `arcade.orlandoaio.net` (nginx on `10.0.0.203`) → upstream
`http://10.0.0.210:8721`. Auth is a bearer token obtained at login; downloads
send `Authorization: Bearer <token>` with `Range` headers.

## More

See [`DEVNOTES.md`](DEVNOTES.md) for the full architecture, source-file map,
the 9-step "add a platform" pattern, config/persistent-data layout, and known
gotchas.
