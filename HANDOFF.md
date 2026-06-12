# ArcadeLauncher — Session Handoff

_Last updated: 2026-06-12 (work done in prior session, dated 2026-06-10 in logs)_

Native **C++17 / Win32 / Direct2D** Windows launcher (`ArcadeLauncher-Client`) talking to a
**Rust/axum/MariaDB** backend (`ArcadeLauncher-Server`) via nginx reverse proxy.

## Repos / hosts
- Client: `C:\Users\BrianTheMint\source\repos\ArcadeLauncher-Client` — `github.com/TheStonedGamer/ArcadeLauncher-Client`
- Server: `C:\Users\BrianTheMint\source\repos\ArcadeLauncher-Server` — `github.com/TheStonedGamer/ArcadeLauncher-Server`
- App host: `10.0.0.210` (root), systemd `arcadelauncher-server`, port `8721`. **Prod access is authorization-gated per turn.**
- Reverse proxy: nginx on `10.0.0.203` (login as **brian**, not root) → `arcade.orlandoaio.net` → upstream `10.0.0.210:8721`.
- Public URL: **`https://arcade.orlandoaio.net`** (works on-LAN and remotely). The LAN IP `10.0.0.210:8721` only works inside the home network.

## Build & release
- Build exe only: `.\scripts\build.ps1 -SkipPackage`. Full MSI: `.\scripts\build.ps1`.
- **Pushing to `main` triggers GitHub Actions** ("Server Client Release"): auto-bumps `src/Version.h`,
  builds MSI, publishes `client-v*` tag with assets `ArcadeLauncher-Server-Client-x64.msi` + `.exe`.
  The bump commit carries `[skip actions]`; local pushes are often rejected non-fast-forward →
  `git pull --rebase origin main` then push.
- WiX `UpgradeCode` `DA9B3C2E-5F7A-4B8D-9C1E-0F2A3B4C5D6E` is **PERMANENT**.
- Commit messages end with `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- PowerShell here-strings mangle when chained after `;` — use repeated `-m` flags for multi-paragraph commits.

## Work completed this session (all shipped to `main`)
1. **Removed server-sync settings from Settings → General.** Server URL/username/password/install-root/
   "enable sync" block deleted from `SettingsWindow.cpp::BuildGeneralPage` + its Load/Save refs. Server
   connection is configured only at sign-in now. Underlying `cfg.server.*` fields preserved.
2. **RPCS3 settings: status text clipping** — `BuildRpcs3Page` Executable group grown 120→128px, progress
   bar/label nudged up, page advance 130→138.
3. **Password change froze + "Request failed"** (`AccountDialog.cpp`, `ServerClient.cpp`):
   - Moved `ChangePassword` onto a worker thread (`PasswordChangeThread`) posting `WM_PW_CHANGED`; button
     disables + shows "Changing password…"; thread joined in `WM_DESTROY`.
   - `ServerClient::HttpGet/HttpPostForm/HttpSendRaw` now surface the real WinHTTP error via
     `WinHttpErrorText()` (e.g. "connection timed out 12002") instead of bare "Request failed".
4. **"Invalid URL" on password change** — stored `serverBaseUrl` was `arcade.orlandoaio.net` with **no
   scheme**; `WinHttpCrackUrl` rejected it. `ServerClient` ctor now prepends a scheme when missing
   (`https://` for public hosts, `http://` for `10.`/`192.168.`/`127.`/`localhost`).
5. **Desktop/Start-Menu shortcut icons** — were the launcher icon. New `MakeGameIconFile()` in `App.cpp`
   builds a per-game `.ico` from the cached cover art (aspect-fit on 256×256 transparent square, PNG-in-ICO
   via WIC) under `%AppData%\ArcadeLauncher\game_icons`; `WriteShellLink`/`CreateGameShortcuts` take an
   optional icon path, threaded from `stored->coverArtPath`. Falls back to launcher icon. Only affects
   shortcuts created from the next install onward.
6. **Friend's remote `12002` timeout — root cause + fix.** All server-URL defaults pointed at the **LAN IP**
   `http://10.0.0.210:8721` (login dialog `App.cpp`, `AssetEnsure.cpp`, `FirstLaunchSetup.cpp`,
   `SettingsWindow.cpp`). Remote clients timed out (nginx access log showed **zero** remote ArcadeLauncher
   hits; LAN `POST /api/account/password` returned 200). Changed all four defaults to
   `https://arcade.orlandoaio.net`.
7. **Prod nginx fix (deployed on `10.0.0.203`).** The arcade site config had regressed to set
   `proxy_set_header Upgrade $http_upgrade;` / `proxy_set_header Connection "upgrade";` unconditionally
   (the documented keep-alive/401-501 gotcha). Removed both lines (timestamped `.bak` saved), `nginx -t`
   passed, `systemctl reload nginx`, verified GET+POST through the proxy respond in ~0.1s.

## Work completed 2026-06-12 (pushed to `main`, commit `0f73112`; release Action running)
1. **Standardized install-folder naming (`ServerClient.cpp`).** Install dirs now use
   `<sanitized game name>-<id>` via new `InstallFolderName` / `ResolveInstallFolder`.
   `SanitizeNameComponent` strips `\ / : * ? " < > |` + control chars, collapses spaces to
   `_`, trims leading/trailing dot/space/underscore, bounds to 80 chars. `ResolveInstallFolder`
   **transparently keeps any pre-existing legacy `<id>` folder** so prior installs aren't
   orphaned/re-downloaded. Applied to catalog hydration, `InstallGame`, `ValidateGame`,
   `UninstallGame`. (Server IDs/paths untouched — chosen over a breaking server re-key.)
2. **Windows Defender exclusion toggle (`SettingsWindow.cpp`, `Config.*`).** Settings →
   General gained "Enable Windows Defender Exclusions for PC Game Folders (requires admin)"
   (`ID_P_CHK4`, persisted as `cfg.defenderExclusions`). On change only, `ApplyDefenderExclusion`
   runs `Add-`/`Remove-MpPreference -ExclusionPath '<root>'` via **elevated (`runas`)
   powershell.exe on a detached thread** (no UI freeze / UAC block). `SanitizeExclusionPath`
   requires an absolute drive/UNC path and rejects `" ' \` $ ; | & < > * ? %` + control chars
   before single-quoted interpolation. **Never** uses `-ExecutionPolicy Bypass`. Target =
   `server.installRoot` (fallback `%AppData%\ServerLibrary`→`server_games`).
- `build.ps1 -SkipPackage` = **0 warnings / 0 errors**.

## Open / follow-up items
- **Friend must re-enter the server URL.** New default only applies to fresh sign-ins; their saved config
  still has the LAN IP. They need to **log out → log back in** with `https://arcade.orlandoaio.net`.
  (No code fix outstanding; this is a one-time user action.)
- **Existing shortcuts keep the old launcher icon** — only newly created ones get game art. Optional future
  work: a one-time "rebuild all shortcut icons" pass.
- The user's own stored `serverBaseUrl` is schemeless (`arcade.orlandoaio.net`); harmless now (ctor
  normalizes), but could be rewritten to the full URL on next save if desired.

## Standing rules / conventions
- Prod deploy/SSH to `10.0.0.210` (root) requires **explicit per-turn authorization**. nginx `10.0.0.203`
  login is user `brian`.
- Do **not** read prod `*.env` files or query MariaDB for credentials/tokens (classifier-blocked).
- Do **not** weaken PowerShell execution policy with `-ExecutionPolicy Bypass`.
- User runs catalog scans/rescans **manually** — do not trigger them.
- Server source is split via `include!` into crate-root scope (`auth.rs`, `handlers.rs`, `db.rs`, etc.);
  only `main.rs` holds `use`/consts/`main`/tests. Argon2 is `Argon2::default()` (fast — not a perf concern).

## Quick verification commands
- Client build: `.\scripts\build.ps1 -SkipPackage`
- Release status: `gh run list --workflow "server-client-release.yml" --limit 3` ; `gh release list`
- Proxy health: `ssh brian@10.0.0.203 "curl -s -o /dev/null -w '%{http_code} %{time_total}s\n' https://arcade.orlandoaio.net/api/catalog"`
