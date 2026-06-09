#include "pch.h"
#include "DolphinConfig.h"

// ── Low-level file I/O (UTF-8, Win32) ─────────────────────────────────────────

static std::string ReadFileUtf8(const std::wstring& path) {
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

static bool WriteFileUtf8(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == data.size();
}

// ── Minimal section-aware INI editor (preserves unmanaged content) ────────────

namespace {

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

class Ini {
public:
    void Parse(const std::string& text) {
        m_lines.clear();
        std::string cur;
        for (char c : text) {
            if (c == '\n') { m_lines.push_back(cur); cur.clear(); }
            else if (c != '\r') cur.push_back(c);
        }
        if (!cur.empty()) m_lines.push_back(cur);
    }

    std::string ToString() const {
        std::string out;
        for (size_t i = 0; i < m_lines.size(); ++i) {
            out += m_lines[i];
            out += "\r\n";  // Dolphin writes CRLF on Windows
        }
        return out;
    }

    std::string Get(const std::string& section, const std::string& key,
                    const std::string& def) const {
        int s, e;
        if (!FindSection(section, s, e)) return def;
        for (int i = s + 1; i < e; ++i) {
            std::string k, v;
            if (SplitKV(m_lines[i], k, v) && k == key) return v;
        }
        return def;
    }
    bool GetBool(const std::string& section, const std::string& key, bool def) const {
        std::string v = Get(section, key, def ? "True" : "False");
        return v == "True" || v == "true" || v == "1";
    }
    // Parses both decimal and Dolphin's hex u32 form (e.g. "0x00000002").
    int GetInt(const std::string& section, const std::string& key, int def) const {
        std::string v = Get(section, key, "");
        if (v.empty()) return def;
        try { return (int)std::stoul(v, nullptr, 0); } catch (...) { return def; }
    }

    void Set(const std::string& section, const std::string& key, const std::string& value) {
        const std::string line = key + " = " + value;
        int s, e;
        if (!FindSection(section, s, e)) {
            if (!m_lines.empty() && !Trim(m_lines.back()).empty())
                m_lines.push_back("");
            m_lines.push_back("[" + section + "]");
            m_lines.push_back(line);
            return;
        }
        for (int i = s + 1; i < e; ++i) {
            std::string k, v;
            if (SplitKV(m_lines[i], k, v) && k == key) { m_lines[i] = line; return; }
        }
        // Insert at the end of the section, skipping trailing blank lines.
        int ins = e;
        while (ins - 1 > s && Trim(m_lines[ins - 1]).empty()) --ins;
        m_lines.insert(m_lines.begin() + ins, line);
    }
    void SetBool(const std::string& section, const std::string& key, bool v) {
        Set(section, key, v ? "True" : "False");
    }
    void SetInt(const std::string& section, const std::string& key, int v) {
        Set(section, key, std::to_string(v));
    }

private:
    // Locate [section]; returns header index in `start` and the index one past
    // the section (next header or EOF) in `end`.
    bool FindSection(const std::string& section, int& start, int& end) const {
        const std::string header = "[" + section + "]";
        start = -1;
        for (int i = 0; i < (int)m_lines.size(); ++i) {
            std::string t = Trim(m_lines[i]);
            if (start < 0) {
                if (t == header) start = i;
            } else if (!t.empty() && t.front() == '[') {
                end = i;
                return true;
            }
        }
        if (start < 0) return false;
        end = (int)m_lines.size();
        return true;
    }

    static bool SplitKV(const std::string& line, std::string& key, std::string& value) {
        std::string t = Trim(line);
        if (t.empty() || t.front() == '[' || t.front() == '#' || t.front() == ';')
            return false;
        size_t eq = t.find('=');
        if (eq == std::string::npos) return false;
        key   = Trim(t.substr(0, eq));
        value = Trim(t.substr(eq + 1));
        return !key.empty();
    }

    std::vector<std::string> m_lines;
};

}  // namespace

// ── User-directory resolution ─────────────────────────────────────────────────

std::wstring DolphinResolveUserDir(const std::wstring& dolphinExe) {
    if (!dolphinExe.empty()) {
        std::error_code ec;
        fs::path exeDir = fs::path(dolphinExe).parent_path();
        if (!exeDir.empty()) {
            if (fs::exists(exeDir / "portable.txt", ec) || fs::exists(exeDir / "User", ec))
                return (exeDir / "User").wstring();
        }
    }
    wchar_t profile[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::wstring(profile) + L"\\Documents\\Dolphin Emulator";
}

static std::wstring ConfigDir(const std::wstring& dolphinExe) {
    std::wstring user = DolphinResolveUserDir(dolphinExe);
    return user.empty() ? L"" : user + L"\\Config";
}

// ── Load ──────────────────────────────────────────────────────────────────────

void DolphinLoadSettings(const std::wstring& dolphinExe, DolphinSettings& out) {
    std::wstring cfg = ConfigDir(dolphinExe);
    if (cfg.empty()) return;

    Ini gfx;  gfx.Parse(ReadFileUtf8(cfg + L"\\GFX.ini"));
    Ini dol;  dol.Parse(ReadFileUtf8(cfg + L"\\Dolphin.ini"));

    out.backend        = ToWide(dol.Get("Core", "GFXBackend", ToUtf8(out.backend)));
    out.fullscreen     = dol.GetBool("Display", "Fullscreen", out.fullscreen);
    out.vsync          = gfx.GetBool("Hardware", "VSync", out.vsync);
    out.aspectRatio    = gfx.GetInt("Settings", "AspectRatio", out.aspectRatio);
    out.internalRes    = gfx.GetInt("Settings", "InternalResolution", out.internalRes);
    out.msaa           = gfx.GetInt("Settings", "MSAA", out.msaa);
    out.ssaa           = gfx.GetBool("Settings", "SSAA", out.ssaa);
    out.maxAnisotropy  = gfx.GetInt("Enhancements", "MaxAnisotropy", out.maxAnisotropy);

    out.dualCore       = dol.GetBool("Core", "CPUThread", out.dualCore);
    out.enableCheats   = dol.GetBool("Core", "EnableCheats", out.enableCheats);
    out.overclockEnable= dol.GetBool("Core", "OverclockEnable", out.overclockEnable);
    {
        std::string oc = dol.Get("Core", "Overclock", "1.0");
        double f = 1.0; try { f = std::stod(oc); } catch (...) {}
        out.overclockPercent = (int)(f * 100.0 + 0.5);
        if (out.overclockPercent < 10) out.overclockPercent = 10;
    }
    out.gcLanguage     = dol.GetInt("Core", "SelectedLanguage", out.gcLanguage);

    out.audioBackend   = ToWide(dol.Get("DSP", "Backend", ToUtf8(out.audioBackend)));
    out.volume         = dol.GetInt("DSP", "Volume", out.volume);
    out.dspHLE         = dol.GetBool("Core", "DSPHLE", out.dspHLE);
}

// ── Apply ───────────────────────────────────────────────────────────────────--

bool DolphinApplySettings(const std::wstring& dolphinExe, const DolphinSettings& s) {
    std::wstring cfg = ConfigDir(dolphinExe);
    if (cfg.empty()) return false;
    std::error_code ec;
    fs::create_directories(cfg, ec);

    std::wstring gfxPath = cfg + L"\\GFX.ini";
    std::wstring dolPath = cfg + L"\\Dolphin.ini";

    Ini gfx;  gfx.Parse(ReadFileUtf8(gfxPath));
    Ini dol;  dol.Parse(ReadFileUtf8(dolPath));

    // Graphics
    dol.Set    ("Core",     "GFXBackend",         ToUtf8(s.backend));
    dol.SetBool("Display",  "Fullscreen",         s.fullscreen);
    gfx.SetBool("Hardware", "VSync",              s.vsync);
    gfx.SetInt ("Settings", "AspectRatio",        s.aspectRatio);
    gfx.SetInt ("Settings", "InternalResolution", s.internalRes);
    {
        // MSAA is a u32 setting Dolphin serializes in hex.
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%08x", (unsigned)s.msaa);
        gfx.Set("Settings", "MSAA", buf);
    }
    gfx.SetBool("Settings", "SSAA",               s.ssaa);
    gfx.SetInt ("Enhancements", "MaxAnisotropy",  s.maxAnisotropy);

    // Core / General
    dol.SetBool("Core", "CPUThread",       s.dualCore);
    dol.SetBool("Core", "EnableCheats",    s.enableCheats);
    dol.SetBool("Core", "OverclockEnable", s.overclockEnable);
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6f", s.overclockPercent / 100.0);
        dol.Set("Core", "Overclock", buf);
    }
    dol.SetInt ("Core", "SelectedLanguage", s.gcLanguage);

    // Audio
    dol.Set    ("DSP",  "Backend", ToUtf8(s.audioBackend));
    dol.SetInt ("DSP",  "Volume",  s.volume);
    dol.SetBool("Core", "DSPHLE",  s.dspHLE);

    bool ok = WriteFileUtf8(gfxPath, gfx.ToString());
    ok = WriteFileUtf8(dolPath, dol.ToString()) && ok;
    return ok;
}

// ── Gamepad preset ─────────────────────────────────────────────────────────────

bool DolphinWriteGamepadPreset(const std::wstring& dolphinExe, bool alsoWiimote,
                               std::wstring& err) {
    std::wstring cfg = ConfigDir(dolphinExe);
    if (cfg.empty()) { err = L"Could not resolve Dolphin's user directory."; return false; }
    std::error_code ec;
    fs::create_directories(cfg, ec);

    const char* kDevice = "XInput/0/Gamepad";

    // GameCube controller (port 1) — standard XInput layout.
    std::wstring gcPath = cfg + L"\\GCPadNew.ini";
    Ini gc; gc.Parse(ReadFileUtf8(gcPath));
    const std::pair<const char*, const char*> gcMap[] = {
        { "Device",            kDevice },
        { "Buttons/A",         "`Button A`" },
        { "Buttons/B",         "`Button B`" },
        { "Buttons/X",         "`Button X`" },
        { "Buttons/Y",         "`Button Y`" },
        { "Buttons/Z",         "`Shoulder R`" },
        { "Buttons/Start",     "`Start`" },
        { "Main Stick/Up",     "`Left Y+`" },
        { "Main Stick/Down",   "`Left Y-`" },
        { "Main Stick/Left",   "`Left X-`" },
        { "Main Stick/Right",  "`Left X+`" },
        { "C-Stick/Up",        "`Right Y+`" },
        { "C-Stick/Down",      "`Right Y-`" },
        { "C-Stick/Left",      "`Right X-`" },
        { "C-Stick/Right",     "`Right X+`" },
        { "Triggers/L",        "`Trigger L`" },
        { "Triggers/R",        "`Trigger R`" },
        { "Triggers/L-Analog", "`Trigger L`" },
        { "Triggers/R-Analog", "`Trigger R`" },
        { "D-Pad/Up",          "`Pad N`" },
        { "D-Pad/Down",        "`Pad S`" },
        { "D-Pad/Left",        "`Pad W`" },
        { "D-Pad/Right",       "`Pad E`" },
    };
    for (auto& kv : gcMap) gc.Set("GCPad1", kv.first, kv.second);
    bool ok = WriteFileUtf8(gcPath, gc.ToString());

    if (alsoWiimote) {
        std::wstring wmPath = cfg + L"\\WiimoteNew.ini";
        Ini wm; wm.Parse(ReadFileUtf8(wmPath));
        const std::pair<const char*, const char*> wmMap[] = {
            { "Source",              "1" },          // emulated Wiimote
            { "Device",              kDevice },
            { "Buttons/A",           "`Button A`" },
            { "Buttons/B",           "`Trigger R`" },
            { "Buttons/1",           "`Button X`" },
            { "Buttons/2",           "`Button Y`" },
            { "Buttons/-",           "`Back`" },
            { "Buttons/+",           "`Start`" },
            { "Buttons/Home",        "`Guide`" },
            { "D-Pad/Up",            "`Pad N`" },
            { "D-Pad/Down",          "`Pad S`" },
            { "D-Pad/Left",          "`Pad W`" },
            { "D-Pad/Right",         "`Pad E`" },
            { "Nunchuk/Buttons/C",   "`Shoulder L`" },
            { "Nunchuk/Buttons/Z",   "`Trigger L`" },
            { "Nunchuk/Stick/Up",    "`Left Y+`" },
            { "Nunchuk/Stick/Down",  "`Left Y-`" },
            { "Nunchuk/Stick/Left",  "`Left X-`" },
            { "Nunchuk/Stick/Right", "`Left X+`" },
        };
        for (auto& kv : wmMap) wm.Set("Wiimote1", kv.first, kv.second);
        ok = WriteFileUtf8(wmPath, wm.ToString()) && ok;
    }

    if (!ok) err = L"Failed to write controller configuration files.";
    return ok;
}
