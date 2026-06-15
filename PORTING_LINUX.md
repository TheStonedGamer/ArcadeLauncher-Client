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
- **Done (L1b — renderer):** `src/Platform/win/RendererD2D.{h,cpp}` — a Windows
  `platform::IRenderer2D` over **Direct2D + DirectWrite** (`makeRendererD2D(rt,
  dw)`), wrapping a caller-managed `ID2D1RenderTarget` (Renderer.cpp already owns
  `BeginDraw`/`EndDraw`). Implements the full interface: solid/rounded/stroked
  rects, ellipse, line, linear gradient, RGBA→premultiplied-BGRA image upload +
  blit, UTF-8 text via `DrawTextLayout` (x-as-anchor alignment to match nanovg;
  sidesteps the `windows.h` `DrawText` macro), `measureText`, and an
  axis-aligned clip stack. Added to the vcxproj (no-pch). **Verified on the real
  Direct2D backend:** a standalone `d2d_selfcheck` (`scripts/build-d2d-selfcheck.cmd`)
  renders the shared `gridview::drawGrid` into an off-screen WIC bitmap target
  and reads tile-0 center back = `(41,107,140)`, pixel-identical to the Linux
  nanovg result. Launcher build stays green (0/0).
- **Next (L1b — rest):** the remaining Windows wrappers — ServerClient/WinHTTP
  (→IHttpClient/IWebSocket), the Win32 window/loop (→IWindow), WASAPI VoiceEngine
  (→IAudioIn/Out); then route the app through the interfaces with no visual change.

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
- **Done (L3d-a): the Linux app reads the real catalog.** `src/Catalog.{h,cpp}`
  in `arcade_core` — a portable UTF-8 reader for the same `library.json` format
  `GameLibrary::Save` emits (same field names + escaping; `findObjectEnd` survives
  a `}` inside a string value). No wstring/Win32. New headless `catalog_selfcheck`
  KAT covers the brace-in-string + escape cases. `gui_demo <library.json>` now
  parses the real catalog, skips hidden games, decodes each `coverArtPath` via
  stb_image, and renders live tiles (`gui_demo` with no arg keeps the
  deterministic demo scene for CI). Verified in WSLg against a real library.json:
  4 games → 3 tiles with the actual decoded cover; `ctest` all 7 green.
- **Done (L3d-b-1): shared grid geometry.** `src/GridLayout.{h,cpp}` in
  `arcade_core` — `grid::Metrics::forViewport()` + `tileRect`/`tileVisible`/
  `hitTest`/`scrollForIndex`, extracted **verbatim** from `Renderer.cpp`'s
  `Resize`/`DrawGrid`/`HitTestGrid`/`ScrollForSelected` so both platforms share
  one source of truth for tile placement, click-mapping, culling, and
  scroll-into-view. Pure CPU, no Win32/Direct2D/nanovg. New headless
  `grid_selfcheck` KAT asserts metrics/rects/hit-test/scroll against
  hand-computed values at a known viewport (and documents the production quirk
  that `HitTestGrid` itself doesn't reject sidebar/topbar clicks — the caller
  routes those first). `gui_demo` now lays its tiles out **through**
  `GridLayout` (was ad-hoc), so the Linux grid provably matches the Windows
  math. Windows `Renderer.cpp` is untouched — it adopts these functions in a
  later slice. Verified in WSLg: clean build, `ctest` all 8 green; CI gains a
  `grid_selfcheck` gate.
- **Done (L3d-b-2): Windows Renderer.cpp adopts the shared geometry.** Windows
  `Renderer.cpp` now drives its Direct2D grid through `grid::GridLayout` instead
  of its own copy of the math: `Resize` populates the `m_sidebarW/tileW/tileH/
  cols` members from `grid::Metrics::forViewport`; `DrawGrid` places + culls
  tiles via `grid::tileRect`/`tileVisible`; `HitTestGrid`, `HitTestCardMenuButton`
  and `ScrollForSelected` delegate to `grid::hitTest`/`tileRect`/`scrollForIndex`.
  The duplicated formulas are deleted — both platforms now share **one** layout
  source of truth, verified by `grid_selfcheck`. `GridLayout.{h,cpp}` added to
  the vcxproj (no-pch, like the other portable TUs). **Windows build green: 0
  warnings / 0 errors**, exe builds; geometry is identical (delegation is
  verbatim) so there's no visual change.
- **Done (L3d-b-3a): shared portable grid *drawing*.** `src/CatalogGridView.
  {h,cpp}` — `gridview::drawCard` / `drawGrid` paint the catalog grid entirely
  through `platform::IRenderer2D` (rounded cards, cover-art clip, platform pill,
  title, hover/selection ring), with tiles placed + culled by `GridLayout`. This
  is the cross-platform replacement for Renderer.cpp's `ID2D1` `DrawCard`. It
  depends only on the `IRenderer2D` interface + `GridLayout`, so it links into
  both `arcade_gui` (Linux, today) and the Windows build (once the wrapper
  lands). `gui_demo` now renders through `gridview::drawGrid` instead of its
  bespoke scene — verified in WSLg + `ctest` (all 8 green): demo tile0 center
  matches its placeholder exactly, and a real `library.json` renders (hidden
  games skipped, covers decoded).
- **Done (L3d-b-3b): the Windows IRenderer2D backend** (see L1b above) —
  `gridview::drawGrid` now paints correctly through Direct2D, proven by
  `d2d_selfcheck`. The shared draw path is live on **both** back-ends (nanovg/GL
  + Direct2D), pixel-matched.
- **Done (L3d-b-4): portable interaction controller + interactive Linux grid.**
  `src/GridController.{h,cpp}` in `arcade_core` — `grid::Controller` owns the
  scroll offset + selected tile and turns input (wheel, click, arrow keys) into
  state via `GridLayout` (maxScroll clamp, scroll-aware hit-test with the
  sidebar/top-bar guard, scroll-into-view, clamped keyboard nav incl. short last
  row). Pure logic, no GL/Win32 — shared by both platforms. Headless
  `grid_controller_selfcheck` KAT verifies it against hand-computed values
  (`maxScroll=922`, select-into-view `=900`, gap/sidebar `→ -1`, last-row clamp,
  empty catalog). `gui_demo --hold` is now an **interactive** catalog browser:
  wheel scrolls, click/arrows select, PgUp/PgDn page, resize re-flows. `ctest`
  all 9 green; Windows launcher build green (the controller compiles into it too,
  ready to adopt).
- **Architecture note (why not swap Windows DrawCard yet):** production
  `DrawCard` does far more than `gridview::drawCard` (badges, ROM-variant counts,
  hover menu button, multi-select checkboxes, install-state overlays, favorite
  stars, lift animation). Pointing Windows at `gridview::drawGrid` now would
  *regress* those. So the unification order is: share the **pure logic**
  (GridLayout ✅, GridController ✅) that can't regress anything, grow `gridview`
  to feature-parity incrementally, and only then swap the Windows draw path.
- **Done (L3d-b-5): gridview parity — favorite + install-state.**
  `gridview::Card` gained `favorite` + an `Install` enum (`installFromString`
  maps GameLibrary's strings); `drawCard` paints a gold favorite disc (top-right)
  and an install-state dot (bottom-right: green=installed, accent=update,
  gray=not). Both are corner overlays so the deterministic center-pixel checks
  still hold. Exercised on **both** back-ends — Linux `gui_demo` + Windows
  `d2d_selfcheck` (tile0 center still `(41,107,140)`); `ctest` all 9 green,
  launcher build green.
- **Done (L3d-b-6): gridview parity — count badge + multi-select checkbox.**
  `gridview::Card` gained `variantCount` (>1 draws a "N versions" badge top-right,
  below the favorite disc, accent-stroked — mirrors Renderer.cpp's ROM-variant
  badge) and `selectionMode`/`multiSelected` (in selection mode every card shows a
  top-left checkbox, ticked with a two-segment check when selected; the platform
  pill is suppressed so the checkbox owns the corner, matching production). All
  corner overlays → center anchors unchanged. Exercised on **both** back-ends —
  Linux `gui_demo` + Windows `d2d_selfcheck` (tile0 center still `(41,107,140)`);
  `ctest` all 9 green, launcher build 0/0. With this `gridview::drawCard` now
  covers the card display model Renderer.cpp draws (placeholder/cover, border/
  selection ring + lift, platform pill, favorite, install-state, variant count,
  multi-select checkbox). Remaining DrawCard-only flourishes are animated/
  Windows-stateful (hover pulse, download progress bar, hover "⋯" overflow
  button) — deferred until the Windows grid actually adopts `gridview` so they
  can be reproduced against live animation state rather than guessed.
- **Done (L3d-c-1): portable ROM-variant logic (`variants::`) + KAT.** First
  concrete slice of GameLibrary's `std::wstring`→UTF-8 migration. The dump-variant
  rules (filename stem, grouping key, human label, default-pick score) moved into
  `src/GameVariants.{h,cpp}` (arcade_core, UTF-8, no Win32). The Windows
  `Game::Variant*` methods are now thin wrappers that `narrow`/`widen` around
  `variants::` — behaviour-identical because the dump tags (`[!]`, `[a1]`,
  `(Prototype)`, `PRG 1`, …) are pure ASCII. Locked by `variants_selfcheck`
  (real ROM filenames: Crystalis alt/verified, SMB3 PRG-1, prototype, bad+hack,
  installed-copy, empty-path fallback). `ctest` 10/10, launcher build 0/0, new CI
  gate added. **Migration pattern proven:** lift pure logic to a portable
  KAT-covered module, point the wstring side at it via the boundary codec — zero
  Windows regression, one source of truth.
- **Done (L3d-c-2): portable search-match predicate (`gamesearch::`) + KAT.**
  `GameLibrary::Search`'s match rule (case-insensitive substring over title/
  genre/platform/dev/publisher/franchise + 4-digit release-year) moved into
  `src/GameSearch.{h,cpp}` (arcade_core, UTF-8). Windows `Search` builds a
  `gamesearch::Fields` by narrowing each field (year via `gmtime`) and delegates.
  Locked by `search_selfcheck`. `ctest` 11/11, launcher 0/0, CI gate added.
- **Decision (revised): do NOT big-bang `Game` to UTF-8.** Examining the call
  graph: the Linux app already reads the catalog through the UTF-8 `catalog::`
  reader, and the *shared* logic (GridLayout, gridview, Controller, `variants::`,
  `gamesearch::`) is portable UTF-8 that both OSes call. Windows converts at the
  boundary (as `Game::Variant*` and `Search` now do). Rewriting the ~1600
  `std::wstring` field sites across ~83 files would be a large, runtime-unverified
  change that auto-ships to users — and buys the port **nothing**, since Windows
  can keep its `wstring` storage and convert at the edge. So the unification
  strategy stays: **share portable logic; Windows adapts at its boundary.** The
  wstring `Game` storage stays as-is.
- **Done (safety net): GameLibrary Save/Load round-trip KAT.** The user's real
  library (every game owned) persists through `GameLibrary::Save`/`Load`; a
  regression there would silently lose/corrupt entries, and there was no test.
  `src/core/GameLibrarySelfCheckMain.cpp` + `scripts/build-gamelib-selfcheck.cmd`
  (standalone `cl`, `/utf-8`) build a library with the awkward cases (Unicode
  title, Windows path w/ backslashes + ROM tags, summary with quotes+newlines,
  multiple collections, numbers/bools/install-state), Save → Load → assert every
  field survives, and confirm `Search` still finds the reloaded game. **Local
  pre-push gate** (Windows-only, like `d2d_selfcheck`); run it before any change
  touching persistence. Verified OK.
- **Done (L3d-c-3): live search in the Linux app via shared `gamesearch::`.**
  `catalog::Game` gained the searchable fields (`franchise`, `genres`,
  `contentPath`, `releaseDate`) + parser support (catalog KAT extended). The
  Linux `gui_demo` now builds a parallel `gamesearch::Fields` per tile and filters
  the grid through the SAME predicate Windows `Search` uses: `--search <q>` for a
  deterministic headless gate (`gui_demo_search`: "PC" → 3/8 demo tiles, a
  correct non-empty strict subset that renders), and live incremental typing in
  `--hold` (TextInput appends, Backspace drops a UTF-8 code point, title shows the
  query). `ctest` 12/12, Windows launcher 0/0. The Linux grid is now a searchable
  catalog browser driven entirely by shared portable logic.
- **Done (L3d-c-4): shared sort ordering (`gamesort::`) + KAT + Linux sort.**
  The catalog sort modes (Title/Platform/Rating/Playtime/Recent) moved into
  `src/GameSort.{h,cpp}` (arcade_core, UTF-8), mirroring App.cpp's comparators
  (descending for rating/playtime/recent, every mode tie-broken by title then
  id). Locked by `sort_selfcheck`. The Linux `gui_demo` sorts the parallel
  card/field/key arrays through it: `--sort <mode>` deterministic gate
  (`gui_demo_sort`: asserts non-decreasing order) and live F1 cycling in
  `--hold`. `ctest` 14/14, Windows launcher 0/0. (Windows keeps its native
  in-place wstring sort for now — adopting `gamesort::` there means a
  precompute-keys-once refactor of the hot `ApplyFilter` path, deferred; the KAT
  pins both to identical semantics.)
- **Done (L3d-c-5): shared sidebar tab filtering (`gamefilter::`) + KAT + Linux
  tab filter.** App.cpp's `ApplyFilter` per-page selection moved into
  `src/GameFilter.{h,cpp}` (arcade_core, UTF-8): a `passes(Item, TabSel)`
  predicate for All / Favorites / Recently Played / Installed / Ready to Download
  / Updates / Hidden / a Platform / a Collection, mirroring App.cpp exactly —
  including the universal rule that hidden games appear only on the Hidden page.
  (Background Downloads is excluded: it depends on the live download queue, not a
  per-game predicate.) `tabSelect(label)` maps a sidebar label to its page (any
  unknown label → a platform tab, matching `BuildSidebarEntries`). Locked by
  `filter_selfcheck` (Linux ctest) and `scripts/build-filter-selfcheck.cmd`
  (MSVC). `catalog::Game` gained `serverBacked` / `lastPlayed` / `collections`
  (newline-joined) so the predicate has full parity inputs;
  `catalog_selfcheck` extended to assert them. The Linux `gui_demo` filters the
  parallel card/field/key/**item** arrays through it: `--tab <label>`
  deterministic gate (`gui_demo_tab`: exact match-count + non-empty + tile0
  rendered) and live sidebar clicks in `--hold` (hit-test the tab list → switch
  `gamefilter::TabSel` → re-filter, composed with live search). `ctest` 16/16,
  Windows launcher 0/0. (Windows keeps its native wstring `ApplyFilter`; the KAT
  pins both to identical semantics, same approach as `gamesort::`.)
- **Done (L3d-c-6): dynamic sidebar entry model (`sidebar::`) + KAT + Linux
  dynamic sidebar.** `Renderer::BuildSidebarEntries`' tab-list logic moved into
  `src/SidebarModel.{h,cpp}` (arcade_core, UTF-8): `sidebar::build(items)`
  produces the ordered `Entry{label, gamefilter::TabSel}` list — the six fixed
  tabs (All Games / Favorites / Recently Played / Installed / Ready to Download /
  Updates), then one tab per platform actually present (canonical
  `platformOrder()`, then any unknown platform appended sorted), then one per
  collection (unique, sorted), then Hidden (only when something is hidden). It is
  genuinely *dynamic* — derived from the catalog rather than hard-coded — and
  each entry carries its own `TabSel`, so model and filter (`gamefilter::passes`)
  can never disagree (collection tabs filter correctly without re-guessing from a
  label, which `tabSelect` can't do). Hidden items still count toward the
  platform/collection sets so a platform that exists only as hidden games keeps
  its tab. Locked by `sidebar_selfcheck` (Linux ctest) and
  `scripts/build-sidebar-selfcheck.cmd` (MSVC). The Linux `gui_demo` now builds
  its sidebar via `sidebar::build` (replacing the old hard-coded tab list):
  `--sidebar` deterministic gate (`gui_demo_sidebar`: fixed-prefix + per-entry
  arg checks + rendered) and live `--hold` clicks switch to the clicked entry's
  own `TabSel`. `ctest` 18/18, Windows launcher 0/0. (Windows keeps its native
  wstring `BuildSidebarEntries`, which still surfaces every *configured*
  platform/collection regardless of contents; the KAT pins the ordering. A later
  step can have Windows adopt `sidebar::` once its sidebar is fed catalog-derived
  sets.)
- **Next (Linux app shell / L1b-rest):** the browse experience (grid, scroll,
  selection, covers, search, sort, tab filter, dynamic sidebar) is now all on
  shared tested logic. Next is the heavier surface: L4 widgets (settings/dialogs
  on a nanovg widget set) → L5 audio (miniaudio) → L6 XDG paths/integrations →
  L7 AppImage + auto-update. Windows stays green and unchanged; each step
  verified on both OSes.

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
