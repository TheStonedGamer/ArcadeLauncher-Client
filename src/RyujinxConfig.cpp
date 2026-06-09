#include "pch.h"
#include "RyujinxConfig.h"

namespace {

std::string RReadFile(const std::wstring& path) {
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
bool RWriteFile(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == data.size();
}

std::string TrimR(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// Minimal JSON editor for Ryujinx's flat, pretty-printed Config.json. Targets
// only top-level scalar keys (matched by the document's outermost key
// indentation), so nested input/controller maps are never touched. Only the
// value token of an exact `"key":` line is rewritten, keeping indentation and
// any trailing comma.
class FlatJson {
public:
    void Parse(const std::string& text) {
        m_lines.clear();
        m_crlf = text.find('\r') != std::string::npos;
        std::string cur;
        for (char c : text) {
            if (c == '\n') { m_lines.push_back(cur); cur.clear(); }
            else if (c != '\r') cur.push_back(c);
        }
        if (!cur.empty()) m_lines.push_back(cur);
        DetectTopIndent();
    }
    std::string ToString() const {
        std::string out;
        const char* eol = m_crlf ? "\r\n" : "\n";
        for (const auto& l : m_lines) { out += l; out += eol; }
        return out;
    }
    bool Empty() const { return m_lines.empty(); }

    std::string GetScalar(const std::string& key, const std::string& def) const {
        int idx = FindTopKey(key);
        if (idx < 0) return def;
        return ValueOf(m_lines[idx]);
    }
    void SetScalar(const std::string& key, const std::string& raw) {
        int idx = FindTopKey(key);
        if (idx < 0) return;
        const std::string& line = m_lines[idx];
        size_t indent = line.find_first_not_of(" \t");
        std::string lead = (indent == std::string::npos) ? std::string()
                                                         : line.substr(0, indent);
        bool comma = !TrimR(line).empty() && TrimR(line).back() == ',';
        m_lines[idx] = lead + "\"" + key + "\": " + raw + (comma ? "," : "");
    }

private:
    void DetectTopIndent() {
        m_topIndent = 2;  // Ryujinx uses 2-space indentation
        for (const auto& l : m_lines) {
            size_t ind = l.find_first_not_of(" \t");
            if (ind == std::string::npos) continue;
            std::string t = TrimR(l);
            if (!t.empty() && t[0] == '"') { m_topIndent = (int)ind; break; }
        }
    }
    // Find a `"key":` line whose indentation equals the top-level indent and
    // whose value is a scalar (not the start of a nested object/array).
    int FindTopKey(const std::string& key) const {
        const std::string needle = "\"" + key + "\":";
        for (int i = 0; i < (int)m_lines.size(); ++i) {
            size_t ind = m_lines[i].find_first_not_of(" \t");
            if (ind == std::string::npos || (int)ind != m_topIndent) continue;
            std::string t = TrimR(m_lines[i]);
            if (t.rfind(needle, 0) != 0) continue;
            std::string v = ValueOf(m_lines[i]);
            if (v == "{" || v == "[" || v.empty()) return -1;  // not a scalar
            return i;
        }
        return -1;
    }
    static std::string ValueOf(const std::string& line) {
        std::string t = TrimR(line);
        size_t c = t.find(':');
        if (c == std::string::npos) return {};
        std::string v = TrimR(t.substr(c + 1));
        if (!v.empty() && v.back() == ',') v.pop_back();
        return TrimR(v);
    }
    std::vector<std::string> m_lines;
    bool m_crlf = false;
    int  m_topIndent = 2;
};

std::string BoolR(bool b) { return b ? "true" : "false"; }

// Unwrap a JSON string token ("Vulkan" -> Vulkan); leave bare tokens as-is.
std::wstring Unq(const std::string& v) {
    std::string s = v;
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return ToWide(s);
}
std::string Q(const std::wstring& w) { return "\"" + ToUtf8(w) + "\""; }

}  // namespace

std::wstring RyujinxConfigPath() {
    PWSTR roam = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roam)))
        out = std::wstring(roam) + L"\\Ryujinx\\Config.json";
    if (roam) CoTaskMemFree(roam);
    return out;
}

void RyujinxLoadSettings(RyujinxSettings& out) {
    std::wstring path = RyujinxConfigPath();
    if (path.empty()) return;
    FlatJson j; j.Parse(RReadFile(path));
    if (j.Empty()) return;

    out.graphicsBackend  = Unq(j.GetScalar("graphics_backend", "\"Vulkan\""));
    out.aspectRatio      = Unq(j.GetScalar("aspect_ratio", "\"Fixed16x9\""));
    out.antiAliasing     = Unq(j.GetScalar("anti_aliasing", "\"None\""));
    out.backendThreading = Unq(j.GetScalar("backend_threading", "\"Auto\""));
    out.audioBackend     = Unq(j.GetScalar("audio_backend", "\"SDL2\""));
    try { out.resScale      = std::stoi(j.GetScalar("res_scale", "1")); } catch (...) {}
    try { out.maxAnisotropy = std::stoi(j.GetScalar("max_anisotropy", "-1")); } catch (...) {}
    try { out.audioVolume   = (int)std::lround(std::stod(j.GetScalar("audio_volume", "1")) * 100.0); }
    catch (...) {}

    out.enableVsync          = j.GetScalar("enable_vsync", "true") == "true";
    out.shaderCache          = j.GetScalar("enable_shader_cache", "true") == "true";
    out.textureRecompression = j.GetScalar("enable_texture_recompression", "false") == "true";
    out.macroHle             = j.GetScalar("enable_macro_hle", "true") == "true";
    out.startFullscreen      = j.GetScalar("start_fullscreen", "false") == "true";
    out.dockedMode           = j.GetScalar("docked_mode", "true") == "true";
}

bool RyujinxApplySettings(const RyujinxSettings& s) {
    std::wstring path = RyujinxConfigPath();
    if (path.empty()) return false;
    std::string text = RReadFile(path);
    if (text.empty()) return false;   // never create a fresh/empty Config.json
    FlatJson j; j.Parse(text);

    j.SetScalar("graphics_backend", Q(s.graphicsBackend));
    j.SetScalar("aspect_ratio",     Q(s.aspectRatio));
    j.SetScalar("anti_aliasing",    Q(s.antiAliasing));
    j.SetScalar("backend_threading", Q(s.backendThreading));
    j.SetScalar("audio_backend",    Q(s.audioBackend));
    j.SetScalar("res_scale",        std::to_string(s.resScale));
    j.SetScalar("max_anisotropy",   std::to_string(s.maxAnisotropy));
    {
        // Stored as a 0..1 float; keep one decimal so a clean value like 1 stays 1.
        double vol = (s.audioVolume < 0 ? 0 : (s.audioVolume > 100 ? 100 : s.audioVolume)) / 100.0;
        char buf[32];
        snprintf(buf, sizeof(buf), "%g", vol);
        j.SetScalar("audio_volume", buf);
    }
    j.SetScalar("enable_vsync",                 BoolR(s.enableVsync));
    j.SetScalar("enable_shader_cache",          BoolR(s.shaderCache));
    j.SetScalar("enable_texture_recompression", BoolR(s.textureRecompression));
    j.SetScalar("enable_macro_hle",             BoolR(s.macroHle));
    j.SetScalar("start_fullscreen",             BoolR(s.startFullscreen));
    j.SetScalar("docked_mode",                  BoolR(s.dockedMode));

    return RWriteFile(path, j.ToString());
}
