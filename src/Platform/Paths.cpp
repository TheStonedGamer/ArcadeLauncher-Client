// platform/Paths.cpp — application directories per platform (Linux port L1).
#include "Platform/Paths.h"
#include "Platform/Text.h"

#include <cstdlib>

#ifdef _WIN32
#  include <windows.h>
#  include <shlobj.h>
#else
#  include <unistd.h>
#  include <limits.h>
#endif

namespace platform {

char sep() {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char s = sep();
    if (a.back() == s) return a + b;
    return a + s + b;
}

#ifdef _WIN32

std::string data_dir() {
    wchar_t buf[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf)))
        return join(narrow(buf), "ArcadeLauncher");
    return "ArcadeLauncher";
}

std::string temp_dir() {
    wchar_t buf[MAX_PATH + 1]{};
    DWORD n = GetTempPathW(MAX_PATH + 1, buf);
    if (n == 0) return ".";
    std::wstring w(buf, n);
    if (!w.empty() && (w.back() == L'\\' || w.back() == L'/')) w.pop_back();
    return narrow(w);
}

std::string exe_dir() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring w(buf, n);
    size_t slash = w.find_last_of(L"\\/");
    if (slash != std::wstring::npos) w.resize(slash);
    return narrow(w);
}

#else  // ───────────────────────────── Linux / POSIX ─────────────────────────

static std::string env_or(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

std::string data_dir() {
    std::string home = env_or("HOME", ".");
    std::string base = env_or("XDG_DATA_HOME", join(home, ".local/share"));
    return join(base, "ArcadeLauncher");
}

std::string temp_dir() {
    return env_or("TMPDIR", "/tmp");
}

std::string exe_dir() {
    char buf[PATH_MAX]{};
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    std::string p(buf, (size_t)n);
    size_t slash = p.find_last_of('/');
    if (slash != std::string::npos) p.resize(slash);
    return p;
}

#endif

} // namespace platform
