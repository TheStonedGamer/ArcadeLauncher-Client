# ArcadeLauncher — Unified Client Roadmap v2

> **Decision (2026-06-15):** Stop maintaining two native UI codebases (Windows
> C++/Direct2D + Linux C++/nanovg). Build **one** cross-platform client on
> **Tauri v2** (Rust core + web UI) that runs on Windows and Linux from a single
> codebase, with a **Steam-style install model**: admin needed at most once,
> updates never need admin.
>
> **Driver:** the native Linux port (L0–L5) proved out the architecture, but the
> ongoing cost is maintaining the UI, voice, and networking twice. A webview-based
> client gets rendering, WebRTC voice, and WebSockets *for free*, collapsing the
> two stacks into one.

---

## Hard rules (carried over, non-negotiable)

1. **Never ship a build that isn't compiled clean and tested green on BOTH
   Windows AND Linux.** The installed launcher auto-updates onto the user's real
   machine; a broken release breaks their actual computer.
2. **The current C++ launcher stays the live, shipping product** until the Tauri
   client reaches feature parity. We do **not** touch the C++ auto-update channel
   during the rewrite. Only when parity + both-OS green is proven do we flip the
   update channel to the Tauri build.
3. **Step by step.** Every increment is shippable and verified before the next.
4. The **server / social backend, WebSocket protocol, `library.json` catalog
   format, assets, auth/TOTP flow stay unchanged.** The new client is a new
   *frontend* to the same backend.

---

## Why Tauri (not Electron / native / Flutter)

| Concern | Tauri v2 |
|---|---|
| Binary size | Tiny — uses the OS webview, no bundled Chromium (Electron ships ~150MB) |
| Auto-update | **Built-in signed updater plugin**, cross-platform |
| Install model | NSIS **per-user** mode on Windows + per-user on Linux → admin-free updates (the Steam model) |
| Rendering | HTML/CSS — one grid layout for both OSes |
| Voice | `getUserMedia` + WebRTC in the webview — replaces WASAPI + miniaudio |
| Social | native `WebSocket` — replaces IXWebSocket |
| OS-touching code | Rust core: launch games (`std::process::Command`), file scan, tray, global hotkey |
| Frontend | React (matches existing web tooling) |

Electron remains the heavier fallback if Tauri hits a wall on any specific need.

---

## What carries over vs. gets rebuilt

**Carries over unchanged**
- Server / social backend (Proxmox CT 10.0.0.210), MinIO (10.0.0.220)
- WebSocket message protocol + REST endpoints
- `library.json` catalog format + cover-art / hero assets
- Auth + TOTP flow (note: `totpCode` still never persisted to disk)
- Versioning/release CI conventions (Windows job owns version bump/tag)

**Gets rebuilt — but simpler in the webview**
- Catalog grid, search, filter, sort, settings (HTML/CSS/React)
- Voice (→ WebRTC)
- Tray, global hotkey, file pickers (→ Rust core)
- Installer + auto-update (→ Tauri updater, per-user)

---

## Phases (T-series, new repo `ArcadeLauncher-Tauri`)

Each phase ends green on **both** Windows and Linux before the next begins.

### ✅ T0 — Scaffold + the thesis proof  *(done — CI green both OSes)*
- New repo `ArcadeLauncher-Tauri` (Tauri v2 + React + TypeScript).
- **Per-user install + updater wired from day one** — proves the admin-free
  Steam model immediately.
- Catalog grid reads the **real `library.json`** and **launches one game**.
- CI: build Windows + Linux artifacts, smoke-run both.
- ✅ Exit criteria: one codebase, runs on both OSes, admin-free update path
  demonstrated, a game launches.

### ✅ T1 — Catalog parity  *(done — query/search/sort/sidebar, detail panel, settings)*
- Full grid: cover art, hero art, install-state badges, favorite star.
- Search (genre/platform/year/dev), sort modes, sidebar tab filter, collections.
- Game detail panel (dev/publisher/franchise, screenshots).
- Settings page (General) with file-backed, non-destructive config round-trip.

### ✅ T2 — Launch + library management  *(done — ROM-variant grouping/picker, pre/post hooks, playtime tracking)*
- ROM-variant handling, pre/post-launch hooks per game.
- Playtime tracking → existing `game_stats` endpoints.
- Catalog scan/rescan (note: **user runs scans manually** — client never
  auto-triggers them).

### T3 — Social client
- Connect to existing WebSocket backend: friends, presence, DMs.
- Message features already on server: reactions, replies, edit/delete, read
  receipts, attachments (MinIO), offline queue, privacy/ignore.
- Profiles (banner, bio, level/XP), friend groups/notes, custom status/DND/idle.

### T4 — Voice (the big simplification)
- WebRTC capture/playback in the webview; signaling over existing backend.
- Retire the C++ WASAPI/miniaudio path conceptually (the L5 work validated the
  buffer/jitter model — reuse the lessons, not the code).

### T5 — Downloads + cloud saves
- Download controls: pause/resume/cancel + bandwidth limit.
- Cloud saves v1 sync against existing server endpoints.

### T6 — Platform polish
- System tray, global hotkey to summon, Big Picture / gamepad navigation.
- Discord Rich Presence.
- XDG paths on Linux, native file pickers.

### T7 — Cutover (DONE — unified client shipped)

- Full parity audit vs. current C++ client (checklist sign-off).
- Both-OS green, signed updater verified end-to-end.
- **Flip the auto-update channel** from the C++ build to the Tauri build — done
  (T10c): native `AppUpdater` retired, unified client at v0.9.2.
- C++ client repo archived as legacy (tag/dispatch-only releases).

---

## Install / update model (the Steam ask, concretely)

- **Windows:** Tauri NSIS installer in **per-user** mode → installs under
  `%LocalAppData%`. No admin on first install, no admin on updates. (Optional
  per-machine variant later if needed for shared PCs.)
- **Linux:** install under `~/.local` (or AppImage with per-user update dir).
- **Updates:** Tauri updater pulls a signed manifest, downloads, swaps files the
  user already owns — **never elevates**.

> This also solves the original admin-prompt complaint as a side effect of the
> framework choice, rather than as a separate packaging fix.

---

## Status of the C++ Linux port (frozen, not deleted)

L0–L5 complete and shipping in the C++ client (latest **client-v1.2.63**, the
miniaudio backend). The port stays in the live product and **keeps shipping** so
the user always has a working launcher. It is **not** the long-term client — once
the Tauri client reaches T7 parity, the C++ client is retired.

The portable `arcade_core` logic + KAT discipline from the port informs the Rust
core design (deterministic, testable, OS-free logic separated from platform glue).
