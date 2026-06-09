#include "pch.h"
#include "DuckStationConfig.h"
#include "IniFile.h"

using cfgkv::IniFile;

static std::string DsReadFile(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz{}; GetFileSizeEx(h, &sz);
    std::string buf((size_t)sz.QuadPart, '\0');
    DWORD rd = 0;
    if (!buf.empty()) ReadFile(h, buf.data(), (DWORD)buf.size(), &rd, nullptr);
    CloseHandle(h);
    buf.resize(rd);
    return buf;
}
static bool DsWriteFile(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == data.size();
}

std::wstring DuckConfigPath(const std::wstring& duckExe) {
    if (!duckExe.empty()) {
        std::error_code ec;
        fs::path dir = fs::path(duckExe).parent_path();
        if (!dir.empty()) {
            if (fs::exists(dir / "portable.txt", ec) || fs::exists(dir / "settings.ini", ec))
                return (dir / "settings.ini").wstring();
        }
    }
    PWSTR roam = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roam)))
        out = std::wstring(roam) + L"\\DuckStation\\settings.ini";
    if (roam) CoTaskMemFree(roam);
    return out;
}

void DuckLoadSettings(const std::wstring& duckExe, DuckSettings& out) {
    std::wstring path = DuckConfigPath(duckExe);
    if (path.empty()) return;
    IniFile t; t.Parse(DsReadFile(path));

    out.renderer     = ToWide(t.Get("GPU", "Renderer", ToUtf8(out.renderer)));
    out.resScale     = t.GetInt ("GPU", "ResolutionScale", out.resScale);
    out.dithering    = ToWide(t.Get("GPU", "DitheringMode", ToUtf8(out.dithering)));
    out.pgxp         = t.GetBool("GPU", "PGXPEnable", out.pgxp);
    out.aspect       = ToWide(t.Get("Display", "AspectRatio", ToUtf8(out.aspect)));
    out.vsync        = t.GetBool("Display", "VSync", out.vsync);
    out.fullscreen   = t.GetBool("Main", "StartFullscreen", out.fullscreen);
    out.audioBackend = ToWide(t.Get("Audio", "Backend", ToUtf8(out.audioBackend)));
    out.volume       = t.GetInt ("Audio", "OutputVolume", out.volume);
}

bool DuckApplySettings(const std::wstring& duckExe, const DuckSettings& s) {
    std::wstring path = DuckConfigPath(duckExe);
    if (path.empty()) return false;
    std::string text = DsReadFile(path);
    if (text.empty()) return false;   // no-op until DuckStation creates settings.ini
    IniFile t; t.Parse(text);

    t.SetRaw      ("GPU", "Renderer", ToUtf8(s.renderer));
    t.SetInt      ("GPU", "ResolutionScale", s.resScale);
    t.SetRaw      ("GPU", "DitheringMode", ToUtf8(s.dithering));
    t.SetBoolLower("GPU", "PGXPEnable", s.pgxp);
    t.SetRaw      ("Display", "AspectRatio", ToUtf8(s.aspect));
    t.SetBoolLower("Display", "VSync", s.vsync);
    t.SetBoolLower("Main", "StartFullscreen", s.fullscreen);
    t.SetRaw      ("Audio", "Backend", ToUtf8(s.audioBackend));
    t.SetInt      ("Audio", "OutputVolume", s.volume);

    return DsWriteFile(path, t.ToString());
}
