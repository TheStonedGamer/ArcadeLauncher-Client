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

**Phase L1 — Platform interfaces + Windows wrappers** — 🚧 **boundary + portable utils landed**
- Land `src/platform/*.h`. Wrap existing Win32/Direct2D/WinHTTP/WASAPI behind
  them. Windows build now goes through the boundary (no visual change).
- **Done (L1a):** `src/platform/` boundary headers — `Platform.h` (umbrella),
  `Net.h` (IHttpClient/IWebSocket), `Window.h` (IWindow + events/keys),
  `Renderer2D.h` (IRenderer2D), `AudioIO.h` (IAudioIn/Out), plus the portable
  utilities now compiled into `arcade_core`: `Text.{h,cpp}` (UTF-8↔UTF-16 codec,
  incl. surrogate pairs), `Crypto.h`+`Sha256.cpp` (vendored FIPS-180-4 SHA-256),
  `Paths.{h,cpp}` (data/temp/exe dirs — Win32 SHGetFolderPath vs XDG, `#ifdef`).
  `CoreSmoke` gained known-answer tests for all three (SHA-256 "abc" KAT, text
  round-trip with €/😀, app-scoped path checks). MSVC compiles the new TUs clean;
  Windows MSI build untouched (platform/ files are not in the vcxproj yet — they
  enter the Windows build when code is migrated onto the boundary in L1b).
- **Next (L1b):** Windows impls under `platform/win/` that wrap the existing
  ServerClient/WinHTTP (→IHttpClient/IWebSocket), the Win32 window/loop (→IWindow),
  Direct2D Renderer (→IRenderer2D), WASAPI VoiceEngine (→IAudioIn/Out); then route
  the app through the interfaces with no visual change.

**Phase L2 — Net + Crypto on Linux** — ✅ **HTTP + WS + crypto landed**
- libcurl `IHttpClient`, IXWebSocket `IWebSocket`, vendored sha256. Headless test:
  log in, hit `/api/health`, open the social gateway from Linux. (No UI yet.)
- **Done (L2a):** `src/Platform/linux/HttpClientCurl.cpp` — libcurl-backed
  `platform::IHttpClient` / `makeHttpClient()`: synchronous, thread-safe
  (`CURLOPT_NOSIGNAL`, one easy handle per request), binary-safe request/response
  bodies, header capture (lower-cased keys), follow-redirects, gzip, ranged GET.
  CMake gains `arcade_net` (built only when `find_package(CURL)` succeeds and not
  MSVC) + `net_selfcheck` driver that GETs `{base}/api/health`. Vendored sha256
  already shipped in L1 (`Sha256.cpp`). Verified in WSL (Ubuntu 26.04, g++ 15.2,
  libcurl4-openssl-dev) on ext4: clean build, `net_selfcheck` → `OK (HTTP 200)`
  against the live server through nginx/TLS.
- **Done (L2b):** `src/Platform/linux/WebSocketIx.cpp` — IXWebSocket-backed
  `platform::IWebSocket` / `makeWebSocket()` for the social gateway: text/binary
  frames + lifecycle callbacks, TLS via OpenSSL. Mirrors the Windows pump's
  contract — it's a dumb frame pipe; the `{"type":"ping"}` heartbeat and
  reconnect/backoff stay in `SocialManager`. IXWebSocket is pulled via CMake
  `FetchContent` (tag v11.4.5; opt out with `-DARCADE_WITH_WS=OFF`). New
  `ws_selfcheck` driver logs in via `/api/login` (creds from `ARCADE_USER`/
  `ARCADE_PASS` env) and opens `wss://…/ws/social?token=…`, asserting the
  server's `hello` frame; with no creds it does an unauthenticated handshake and
  asserts the gateway answers (401), proving the WSS transport reaches the server.
  Verified in WSL (libssl-dev): clean build, `ws_selfcheck` → `OK — gateway
  responded (code 401)` against the live gateway through nginx/TLS.

**Phase L3 — Window + Renderer2D on Linux** — 🚧 **window + nanovg renderer up; Renderer.cpp migration next**
- SDL2 window + GL context; nanovg `Renderer2D`. Port the main grid/detail
  renderer (Renderer.cpp) to `Renderer2D`. First pixels on Linux.
- **Done (L3a):** `src/Platform/linux/WindowSdl.cpp` — SDL2-backed
  `platform::IWindow` / `makeWindow()`: window + GL 3.2 core context (stencil for
  nanovg), full SDL→`platform::Event` translation (mouse/wheel/keys/text/resize/
  focus/quit), clipboard, title/show, `nativeHandle()` = `SDL_Window*`. CMake adds
  `arcade_gui` + a `gui_smoke` driver that opens the window, clears to the dark-
  theme color, reads the BACK buffer back to verify real output (center pixel ==
  expected within tolerance), and dumps `gui_smoke.ppm`. **First pixels on Linux:**
  verified in WSLg (SDL2 2.32, Mesa llvmpipe GL 4.5) — `gui_smoke` → `OK
  (32,34,48)` and the PPM shows the rendered frame. CI runs it headless under
  `xvfb` + software GL. `--hold` keeps the window open for manual inspection.
- **Done (L3b):** `src/Platform/linux/RendererNanovg.cpp` — nanovg/GL3
  `platform::IRenderer2D` / `makeRenderer(IWindow*)`: rounded/filled/stroked rects,
  ellipses, lines, linear gradients, images (`nvgImagePattern`), UTF-8 text with
  alignment + `measureText`, and a nested scissor clip stack. GL entry points via
  GLEW; nanovg fetched with CMake `FetchContent` and compiled from source (pinned
  commit; `LANGUAGES C CXX` so `nanovg.c` builds; opt out with
  `-DARCADE_WITH_RENDERER=OFF`). New `gui_demo` draws a full catalog grid through
  the interface — gradient top bar, sidebar tabs, 8 rounded-rect game tiles with
  platform pills + titles — and verifies a tile's center pixel deterministically.
  Verified in WSLg (Mesa llvmpipe) and via `ctest` (all 5 self-checks green); CI
  runs both GUI checks headless under `xvfb`. `--hold` opens it interactively.
- **Done (L3c):** cover-art image pipeline. `Platform/Image.h` + stb_image impls
  (`ImageStb.cpp` decode, `ImageStbWrite.cpp` encode — split TUs since stb_image
  and stb_image_write share `static` helpers; both `*_STATIC` so they don't clash
  with nanovg.c's embedded stb_image). `decodeImageRGBA`/`decodeImageFileRGBA`/
  `encodePngRGBA` replace WIC on Linux. New headless `image_selfcheck` round-trips
  RGBA→PNG→RGBA (KAT). `gui_demo` now uses the **production GitHub-dark palette**
  (C_BG `0x0D1117` … C_ACCENT `0x58A6FF`) and draws **real decoded cover art** per
  tile via `createImageRGBA`+`drawImage` (clipped to the rounded card). Verified
  in WSLg + `ctest` (all 6 checks green).
- **Next (L3d):** migrate `GameLibrary`/catalog parse off `std::wstring` to UTF-8
  so the real catalog is readable on Linux, port `Renderer.cpp`'s grid/detail
  drawing onto `IRenderer2D` (replacing the demo scene with live data), and wire
  `App.cpp`'s message loop to `IWindow` to stand up the actual Linux app shell.

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

---

## 8. Release & version lockstep (Windows ↔ Linux)

**Policy: the Linux client tracks the Windows client in lockstep.** Once the port
is complete there is exactly one client, two artifacts — every release ships both
the Windows MSI **and** the Linux build at the **same version**, off the **same
commit/tag**. We do not let the two platforms drift.

How the CI enforces it (`.github/workflows/server-client-release.yml`):
- The **Windows job** owns versioning: it bumps `src/Version.h` (patch / `[minor]`
  / `[major]`), commits, pushes, and creates the `client-vX.Y.Z` tag + GitHub
  release. It exposes `VERSION`/`TAG` as job outputs.
- The **Linux job** (`build-linux`, `needs: build`) checks out that exact `TAG`,
  builds via CMake, runs the self-check gate, and **attaches its Linux artifact to
  the same release**. Because it builds the tagged commit, the Linux binary
  carries the identical `Version.h` — same `/api/health` version handshake, same
  server compatibility rules (major.minor lockstep with the server).
- Consequence: a `[minor]`/`[major]` bump moves **both** clients together, and the
  production server must be redeployed for that minor before either updates (same
  rule as today, now covering Linux too).

**Artifact maturity:** until the GUI (L3) and AppImage packaging (L7) land, the
Linux job builds the portable core + headless self-checks and tarballs them. As
those phases land, the job's build/package steps grow into the real runnable
client (and eventually `.deb`/Flatpak) — the lockstep wiring stays the same.
The Linux auto-updater (L7) mirrors the Windows "update on launch" flow so both
platforms self-update from the same releases.
