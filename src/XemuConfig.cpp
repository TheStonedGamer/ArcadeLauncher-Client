#include "pch.h"
#include "XemuConfig.h"
#include "IniFile.h"

using cfgkv::IniFile;

static std::string XmReadFile(const std::wstring& path) {
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
static bool XmWriteFile(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == data.size();
}

// xemu strings are single-quoted ('scale'); strip either quote style on read.
static std::string Dequote(const std::string& v) {
    if (v.size() >= 2 && (v.front() == '\'' || v.front() == '"') && v.back() == v.front())
        return v.substr(1, v.size() - 2);
    return v;
}

std::wstring XemuConfigPath(const std::wstring& xemuExe) {
    // Portable: xemu.toml next to the exe takes priority if present.
    if (!xemuExe.empty()) {
        std::error_code ec;
        fs::path dir = fs::path(xemuExe).parent_path();
        if (!dir.empty()) {
            fs::path portable = dir / "xemu.toml";
            if (fs::exists(portable, ec)) return portable.wstring();
        }
    }
    PWSTR roam = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roam)))
        out = std::wstring(roam) + L"\\xemu\\xemu\\xemu.toml";
    if (roam) CoTaskMemFree(roam);
    return out;
}

void XemuLoadSettings(const std::wstring& xemuExe, XemuSettings& out) {
    std::wstring path = XemuConfigPath(xemuExe);
    if (path.empty()) return;
    IniFile t; t.Parse(XmReadFile(path));

    out.renderer    = ToWide(Dequote(t.Get("display", "renderer", ToUtf8(out.renderer))));
    out.renderScale = t.GetInt("display.quality", "surface_scale", out.renderScale);
    out.aspect      = ToWide(Dequote(t.Get("display.ui", "aspect_ratio", ToUtf8(out.aspect))));
    out.fit         = ToWide(Dequote(t.Get("display.ui", "fit", ToUtf8(out.fit))));
    out.fullscreen  = t.GetBool("display.window", "fullscreen_on_startup", out.fullscreen);
    out.vsync       = t.GetBool("display.window", "vsync", out.vsync);
}

bool XemuApplySettings(const std::wstring& xemuExe, const XemuSettings& s) {
    std::wstring path = XemuConfigPath(xemuExe);
    if (path.empty()) return false;
    IniFile t; t.Parse(XmReadFile(path));   // may be empty -> sections created

    auto setStr = [&](const char* sec, const char* key, const std::wstring& v) {
        t.SetRaw(sec, key, "'" + ToUtf8(v) + "'");   // single-quoted, xemu style
    };
    setStr("display", "renderer", s.renderer);
    t.SetInt      ("display.quality", "surface_scale", s.renderScale);
    setStr("display.ui", "aspect_ratio", s.aspect);
    setStr("display.ui", "fit", s.fit);
    t.SetBoolLower("display.window", "fullscreen_on_startup", s.fullscreen);
    t.SetBoolLower("display.window", "vsync", s.vsync);

    return XmWriteFile(path, t.ToString());
}
