#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Precompiled header. On Windows (MSVC) this pulls in the full Win32/Direct2D/
// WinHTTP/WIC stack exactly as before. On non-Windows (the Linux native port,
// see PORTING_LINUX.md) the Windows-only includes/pragmas/helpers are compiled
// out so that the genuinely portable files (JSON, QrCode, protocol/data models)
// can be built into a `core` static lib without dragging in windows.h. The
// Windows build is byte-for-byte unchanged — everything platform-specific lives
// inside `#ifdef _WIN32`.
// ─────────────────────────────────────────────────────────────────────────────

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <winreg.h>
#include <winhttp.h>

// Direct2D / DirectWrite / WIC
#include <d2d1.h>
#include <d2d1_1.h>   // ID2D1DeviceContext, high-quality cubic interpolation
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>

// COM
#include <wrl/client.h>

#endif // _WIN32

// STL — portable, included on every platform.
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <memory>
#include <functional>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <fstream>
#include <sstream>
#include <cassert>
#include <filesystem>
#include <condition_variable>

#ifdef _WIN32

#include <tlhelp32.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")

using Microsoft::WRL::ComPtr;

#endif // _WIN32

namespace fs = std::filesystem;

#ifdef _WIN32

inline std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

inline std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
    return s;
}

inline std::wstring GetAppDataPath() {
    wchar_t path[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path);
    return std::wstring(path) + L"\\ArcadeLauncher";
}

#endif // _WIN32
