#pragma once
// Resource IDs for embedded platform logos (RT_RCDATA).
// These PNG/ICO blobs are compiled into the exe via resources/ArcadeLauncher.rc
// so they ship inside the MSI — no runtime download required. Loaded by
// PlatformIcons::LoadFromResource. Keep these in sync with the .rc file.

#define IDR_LOGO_GAMECUBE 200
#define IDR_LOGO_WII      201
#define IDR_LOGO_N64      202
#define IDR_LOGO_NES      203
#define IDR_LOGO_SNES     204
#define IDR_LOGO_PS1      205
#define IDR_LOGO_PS2      206
#define IDR_LOGO_PS3      207
#define IDR_LOGO_XBOX360  208
#define IDR_LOGO_SWITCH   209
#define IDR_LOGO_XBOX     210
