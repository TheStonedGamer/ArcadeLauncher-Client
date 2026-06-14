# ArcadeLauncher Client — Linux Native Port Plan

Target: a **native** Linux build (AppImage/.deb/Flatpak), one shared C++17
codebase with Windows, via a thin platform-abstraction layer. Not Wine.

This is a multi-week, phased effort. Each phase must keep the **Windows build
green** (new code lives behind a platform boundary or `#ifdef _WIN32`).

---

## 1. Reality of the current codebase (measured 2026-06-14)

38 `.cpp` files, ~23.5k LOC. Windows coupling by subsystem:

| Subsystem | Files | Notes |
|---|---|---|
| Win32 windowing / message loop (`HWND`, `WndProc`) | 31 | pervasive — App.cpp (4991 LOC) owns the loop |
| Native common controls (LISTBOX/BUTTON/EDIT/COMBO, owner-draw) | SettingsWindow (2575), AccountDialog (860), LibraryDialog (527), FirstLaunchSetup (469) | **no 1:1 Linux equivalent — reimplement** |
| Direct2D / DirectWrite | Renderer (2297) + 4 | custom 2D vector UI → portable 2D renderer |
| WIC image loading | 6 | → stb_image |
| WinHTTP (REST **and** WebSocket) | 10 | ServerClient (1782) + Social → libcurl + WS lib |
| WASAPI voice | VoiceEngine (1) | → miniaudio (PipeWire/ALSA/Pulse) |
| wincrypt SHA-256 | 1 | → vendored sha256 / OpenSSL |
| Registry, `SHGetFolderPath`, MSI, auto-update | 6 + packaging | → XDG dirs + AppImage updater |

**Already portable** (~20–25% of LOC, comes along ~free): the social protocol,
`SocialJson.h`, `Config` models, server/catalog logic, business rules.

---

## 2. The big decision: strings

Everything is `std::wstring`. `wchar_t` is **16-bit on Windows, 32-bit on Linux**,
and all the Linux libs (SDL, libcurl, fontconfig, file APIs) want **UTF-8 `char`**.

**Chosen strategy: migrate the core to UTF-8 `std::string`, keep Windows happy at
the OS boundary.** Concretely:
- Introduce `platform::widen()/narrow()` (UTF-8 ↔ UTF-16) used **only** at Win32
  API call sites. On Linux these are no-ops over UTF-8.
- New/ported code uses `std::string` (UTF-8). Existing `std::wstring` is migrated
  file-by-file as each file is ported; until then a file stays Windows-only.
- This is done incrementally — not a big-bang sed.

(Alternative considered: keep `std::wstring` everywhere and convert at every Linux
lib boundary. Rejected — more conversions, 32-bit `wchar_t` wastes memory, and the
text shaper wants UTF-8 anyway.)

---

## 3. Dependency choices (cross-platform, permissively licensed)

| Need | Library | Why |
|---|---|---|
| Window + input + GL context | **SDL2** | battle-tested, handles X11/Wayland, gamepad (we already do Big Picture) |
| 2D vector rendering | **nanovg** (OpenGL backend) | close conceptual match to Direct2D (paths, fills, gradients, rounded rects, text); lightweight |
| Text | nanovg font (stb_truetype) + **fontconfig** for system font lookup | replaces DirectWrite |
| Image decode | **stb_image** | replaces WIC |
| HTTP | **libcurl** | replaces WinHTTP REST |
| WebSocket | **IXWebSocket** | C++ WS+TLS, simple API; replaces the WinHTTP WS pump |
| Crypto (SHA-256) | vendored single-file sha256 (or OpenSSL if already linked) | replaces wincrypt |
| Audio out/in | **miniaudio** (single header) | wraps PipeWire/ALSA/Pulse; replaces WASAPI |
| Build | **CMake** | drives both Linux and (eventually) Windows |
| Packaging | **AppImage** first, then `.deb`/Flatpak | self-contained, easy updater |

The native controls (settings, dialogs) get reimplemented as a **small retained
widget set drawn with nanovg** (button/checkbox/combo/listbox/text-edit), shared
by both platforms eventually — this also lets us drop the Win32 control code long-term.

---

## 4. Platform-abstraction boundary (`src/platform/`)

Define narrow interfaces; Windows and Linux each provide an implementation. The
app talks to interfaces, never to `windows.h`/SDL directly.

```
src/platform/
  Platform.h      // app lifecycle, window create, event pump, clipboard, paths
  Window.h        // surface + input events (mouse/key/text/gamepad/resize)
  Renderer2D.h    // begin/end frame, fill/stroke path, rounded-rect, image, text, clip
  Net.h           // IHttpClient (GET/POST/PUT, headers, ranged), IWebSocket
  AudioIO.h       // IAudioOut / IAudioIn (PCM frames)
  Paths.h         // data dir (%LOCALAPPDATA% ↔ $XDG_DATA_HOME), temp, exe dir
  Crypto.h        // sha256
  win/  *.cpp      // Win32/Direct2D/WinHTTP/WASAPI impls (wrap existing code)
  linux/ *.cpp     // SDL2/nanovg/libcurl/IXWebSocket/miniaudio impls
```

Migration tactic: **wrap, then replace.** First move existing Windows code behind
these interfaces (no behavior change, Windows still green). Then write the Linux
impls against the same interfaces. The UI code becomes platform-agnostic once it
only calls `Renderer2D` instead of `ID2D1RenderTarget`.

---

## 5. Phased execution (each phase: Windows stays green; Linux progress is additive)

**Phase L0 — Toolchain & portable core** *(foundation)* — ✅ **core build green**
- CMake build that compiles the already-portable files (JSON, Config, protocol)
  into a `core` static lib on Linux. Stand up a Debian build CT (proposed
  `10.0.0.221`) as the Linux build/CI box. Prove the core compiles clean.
- **Done:** `CMakeLists.txt` builds `arcade_core` (QrCode + `src/core/CoreSmoke.cpp`
  exercising `SocialJson.h`) + a `core_selfcheck` driver. `pch.h` is now
  cross-platform (Win stack behind `#ifdef _WIN32`; Windows MSI build unchanged).
  Verified on Debian 12 / g++ 12 / cmake 3.25: compiles, links, `core_selfcheck`
  exits 0. Dedicated **build CT provisioned: VMID 128 `arcade-linux-build` @
  `10.0.0.221`** (Debian 12, 4c/4G/20G, unprivileged+nesting, onboot). Toolchain
  installed: build-essential, cmake, git, pkg-config, and the L2–L5 dev libs
  (libsdl2-dev, libgl1-mesa-dev, libcurl4-openssl-dev, libssl-dev,
  libfontconfig1-dev, libasound2-dev). End-to-end verified: clone from GitHub →
  cmake → build → `core_selfcheck` exits 0.

**Phase L1 — Platform interfaces + Windows wrappers**
- Land `src/platform/*.h`. Wrap existing Win32/Direct2D/WinHTTP/WASAPI behind
  them. Windows build now goes through the boundary (no visual change).

**Phase L2 — Net + Crypto on Linux**
- libcurl `IHttpClient`, IXWebSocket `IWebSocket`, vendored sha256. Headless test:
  log in, hit `/api/health`, open the social gateway from Linux. (No UI yet.)

**Phase L3 — Window + Renderer2D on Linux**
- SDL2 window + GL context; nanovg `Renderer2D`. Port the main grid/detail
  renderer (Renderer.cpp) to `Renderer2D`. First pixels on Linux.

**Phase L4 — Retained widget set**
- nanovg button/checkbox/combo/listbox/text-edit. Port SettingsWindow + dialogs
  off Win32 controls onto it (shared by both platforms).

**Phase L5 — Audio (voice) on Linux**
- miniaudio `IAudioOut`/`IAudioIn`; wire VoiceEngine. (Pairs with Phase 2 voice v2.)

**Phase L6 — Paths, config, integrations**
- XDG data dirs, autostart (.desktop), global hotkey (X11/Wayland), tray, file
  pickers (portal). Replace registry/SHGetFolderPath usage.

**Phase L7 — Packaging & auto-update**
- AppImage with embedded updater (mirror the Windows auto-update flow); then
  `.deb`/Flatpak. CI builds both OS artifacts.

---

## 6. Risks / open questions

- **Wayland vs X11**: SDL2 abstracts both; global hotkey + tray are the rough
  edges (Wayland restricts global hotkeys — may need a portal or X11 fallback).
- **Font rendering parity**: nanovg + fontconfig won't pixel-match DirectWrite;
  acceptable, but the UI metrics (the hand-tuned layout) need a once-over.
- **Emulator integration**: launch paths/args are Windows-centric (`.exe`,
  emulator config writers like DolphinConfig). Linux emulators differ — a whole
  sub-track (maps to Phase 2 launch profiles).
- **Effort**: realistically several weeks. Parallelizable once the boundary
  (L1) exists — Net, Renderer, and Widgets can proceed independently.

---

## 7. Immediate next step

Stand up the Debian build CT and land Phase **L0** (CMake + portable-core compile).
Everything after hangs off a green Linux core build.
