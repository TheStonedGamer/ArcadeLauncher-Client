#include "pch.h"
#include "Pcsx2Config.h"
#include "IniFile.h"

using cfgkv::IniFile;

static std::string P2ReadFile(const std::wstring& path) {
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
static bool P2WriteFile(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == data.size();
}

// PCSX2.ini location: portable (inis\ next to exe, or portable.ini marker) else
// Documents\PCSX2\inis\PCSX2.ini.
std::wstring Pcsx2ConfigPath(const std::wstring& pcsx2Exe) {
    if (!pcsx2Exe.empty()) {
        fs::path dir = fs::path(pcsx2Exe).parent_path();
        if (!dir.empty()) {
            std::error_code ec;
            fs::path portMark = dir / "portable.ini";
            fs::path portIni  = dir / "inis" / "PCSX2.ini";
            if (fs::exists(portMark, ec) || fs::exists(portIni, ec))
                return portIni.wstring();
        }
    }
    PWSTR docs = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) {
        out = std::wstring(docs) + L"\\PCSX2\\inis\\PCSX2.ini";
    }
    if (docs) CoTaskMemFree(docs);
    return out;
}

void Pcsx2LoadSettings(const std::wstring& pcsx2Exe, Pcsx2Settings& out) {
    std::wstring path = Pcsx2ConfigPath(pcsx2Exe);
    if (path.empty()) return;
    IniFile t; t.Parse(P2ReadFile(path));

    out.renderer     = t.GetInt ("EmuCore/GS", "Renderer", out.renderer);
    out.upscale      = t.GetInt ("EmuCore/GS", "upscale_multiplier", out.upscale);
    out.aspect       = ToWide(t.GetStr("EmuCore/GS", "AspectRatio", ToUtf8(out.aspect)));
    out.maxAniso     = t.GetInt ("EmuCore/GS", "MaxAnisotropy", out.maxAniso);
    out.vsync        = t.GetBool("EmuCore/GS", "VsyncEnable", out.vsync);
    out.mipmap       = t.GetBool("EmuCore/GS", "mipmap", out.mipmap);
    out.fxaa         = t.GetBool("EmuCore/GS", "fxaa", out.fxaa);
    out.fullscreen   = t.GetBool("UI", "StartFullscreen", out.fullscreen);
    out.cheats       = t.GetBool("EmuCore", "EnableCheats", out.cheats);
    out.widescreen   = t.GetBool("EmuCore", "EnableWideScreenPatches", out.widescreen);
    out.fastBoot     = t.GetBool("EmuCore", "EnableFastBoot", out.fastBoot);
    out.audioBackend = ToWide(t.GetStr("SPU2/Output", "Backend", ToUtf8(out.audioBackend)));
    out.volume       = t.GetInt ("SPU2/Output", "StandardVolume", out.volume);
}

bool Pcsx2ApplySettings(const std::wstring& pcsx2Exe, const Pcsx2Settings& s) {
    std::wstring path = Pcsx2ConfigPath(pcsx2Exe);
    if (path.empty()) return false;
    std::string text = P2ReadFile(path);
    if (text.empty()) return false;   // never write a fresh/empty PCSX2.ini
    IniFile t; t.Parse(text);

    t.SetInt      ("EmuCore/GS", "Renderer", s.renderer);
    t.SetInt      ("EmuCore/GS", "upscale_multiplier", s.upscale);
    t.SetRaw      ("EmuCore/GS", "AspectRatio", ToUtf8(s.aspect));   // unquoted string
    t.SetInt      ("EmuCore/GS", "MaxAnisotropy", s.maxAniso);
    t.SetBoolLower("EmuCore/GS", "VsyncEnable", s.vsync);
    t.SetBoolLower("EmuCore/GS", "mipmap", s.mipmap);
    t.SetBoolLower("EmuCore/GS", "fxaa", s.fxaa);
    t.SetBoolLower("UI", "StartFullscreen", s.fullscreen);
    t.SetBoolLower("EmuCore", "EnableCheats", s.cheats);
    t.SetBoolLower("EmuCore", "EnableWideScreenPatches", s.widescreen);
    t.SetBoolLower("EmuCore", "EnableFastBoot", s.fastBoot);
    t.SetRaw      ("SPU2/Output", "Backend", ToUtf8(s.audioBackend));
    t.SetInt      ("SPU2/Output", "StandardVolume", s.volume);

    return P2WriteFile(path, t.ToString());
}
