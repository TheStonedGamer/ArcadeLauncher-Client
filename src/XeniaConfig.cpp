#include "pch.h"
#include "XeniaConfig.h"
#include "IniFile.h"

using cfgkv::IniFile;

static std::string XReadFile(const std::wstring& path) {
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
static bool XWriteFile(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == data.size();
}

std::wstring XeniaConfigPath(const std::wstring& xeniaExe) {
    if (xeniaExe.empty()) return {};
    fs::path dir = fs::path(xeniaExe).parent_path();
    if (dir.empty()) return {};
    return (dir / "xenia-canary.config.toml").wstring();
}

void XeniaLoadSettings(const std::wstring& xeniaExe, XeniaSettings& out) {
    std::wstring path = XeniaConfigPath(xeniaExe);
    if (path.empty()) return;
    IniFile t; t.Parse(XReadFile(path));

    out.gpu        = ToWide(t.GetStr("GPU", "gpu", ToUtf8(out.gpu)));
    out.resScale   = t.GetInt("GPU", "draw_resolution_scale_x", out.resScale);
    out.frameLimit = t.GetInt("GPU", "framerate_limit", out.frameLimit);
    out.vsync      = t.GetBool("GPU", "vsync", out.vsync);
    out.fullscreen = t.GetBool("Display", "fullscreen", out.fullscreen);
    out.letterbox  = t.GetBool("Display", "present_letterbox", out.letterbox);
    out.hid        = ToWide(t.GetStr("HID", "hid", ToUtf8(out.hid)));
    out.vibration  = t.GetBool("HID", "vibration", out.vibration);
}

bool XeniaApplySettings(const std::wstring& xeniaExe, const XeniaSettings& s) {
    std::wstring path = XeniaConfigPath(xeniaExe);
    if (path.empty()) return false;
    IniFile t; t.Parse(XReadFile(path));

    t.SetQuoted   ("GPU", "gpu", ToUtf8(s.gpu));
    t.SetInt      ("GPU", "draw_resolution_scale_x", s.resScale);
    t.SetInt      ("GPU", "draw_resolution_scale_y", s.resScale);
    t.SetInt      ("GPU", "framerate_limit", s.frameLimit);
    t.SetBoolLower("GPU", "vsync", s.vsync);
    t.SetBoolLower("Display", "fullscreen", s.fullscreen);
    t.SetBoolLower("Display", "present_letterbox", s.letterbox);
    t.SetQuoted   ("HID", "hid", ToUtf8(s.hid));
    t.SetBoolLower("HID", "vibration", s.vibration);

    return XWriteFile(path, t.ToString());
}
