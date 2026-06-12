#pragma once

// ─── ArcadeLauncher version ───────────────────────────────────────────────────
// Bump these three numbers before every release, then push a tag "vMAJOR.MINOR.PATCH".
// The build script and WiX installer read the VERSIONINFO stamped into the exe,
// so this is the single source of truth for the version number.

#define ARCADE_VERSION_MAJOR  1
#define ARCADE_VERSION_MINOR  2
#define ARCADE_VERSION_PATCH  0
#define ARCADE_VERSION_BUILD  0

// Helpers — derived, do not edit.
#define _AV_STR2(x) #x
#define _AV_STR(x)  _AV_STR2(x)

#define ARCADE_VERSION_STR \
    _AV_STR(ARCADE_VERSION_MAJOR) "." \
    _AV_STR(ARCADE_VERSION_MINOR) "." \
    _AV_STR(ARCADE_VERSION_PATCH)

#define ARCADE_VERSION_WSTR \
    L"" _AV_STR(ARCADE_VERSION_MAJOR) L"." \
        _AV_STR(ARCADE_VERSION_MINOR) L"." \
        _AV_STR(ARCADE_VERSION_PATCH)

// FILEVERSION / PRODUCTVERSION quad (comma-separated for .rc files)
#define ARCADE_VERSION_QUAD \
    ARCADE_VERSION_MAJOR, ARCADE_VERSION_MINOR, ARCADE_VERSION_PATCH, ARCADE_VERSION_BUILD
