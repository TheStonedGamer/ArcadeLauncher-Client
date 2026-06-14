#pragma once
// platform/Paths.h — per-platform application directories (Linux port L1).
//
// Returns UTF-8 paths. Windows maps to %LOCALAPPDATA%\ArcadeLauncher and the
// temp/exe dirs via the Win32 API; Linux follows the XDG Base Directory spec
// ($XDG_DATA_HOME or ~/.local/share, $XDG_CACHE_HOME, /proc/self/exe). Replaces
// scattered SHGetFolderPath / GetModuleFileName usage behind one boundary.

#include <string>

namespace platform {

// Per-user writable data dir for the app (created lazily by callers as needed).
// Windows: %LOCALAPPDATA%\ArcadeLauncher.  Linux: $XDG_DATA_HOME/ArcadeLauncher
// (default ~/.local/share/ArcadeLauncher).
std::string data_dir();

// System temp directory (no trailing separator).
std::string temp_dir();

// Directory containing the running executable.
std::string exe_dir();

// Path separator for the current platform ('\\' on Windows, '/' on Linux).
char sep();

// Join two path components with the platform separator.
std::string join(const std::string& a, const std::string& b);

} // namespace platform
