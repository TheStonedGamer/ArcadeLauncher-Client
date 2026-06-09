#include "pch.h"
#include "Rpcs3Config.h"

namespace {

std::string R3ReadFile(const std::wstring& path) {
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
bool R3WriteFile(const std::wstring& path, const std::string& data) {
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
int IndentOf(const std::string& s) {
    int n = 0;
    while (n < (int)s.size() && s[n] == ' ') ++n;
    return n;
}

// Minimal YAML editor: targets scalar keys that are direct (indent-2) children
// of a named top-level (indent-0) section. Nested sub-maps are skipped because
// their keys live at indent >= 4. Every line not explicitly rewritten is kept.
class YamlLite {
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
    }
    std::string ToString() const {
        std::string out;
        const char* eol = m_crlf ? "\r\n" : "\n";
        for (const auto& l : m_lines) { out += l; out += eol; }
        return out;
    }
    bool Empty() const { return m_lines.empty(); }

    std::string GetScalar(const std::string& section, const std::string& key,
                          const std::string& def) const {
        int s, e;
        if (!SectionRange(section, s, e)) return def;
        for (int i = s; i < e; ++i) {
            std::string k, v;
            if (DirectChildKV(m_lines[i], k, v) && k == key) return Unquote(v);
        }
        return def;
    }
    void SetScalar(const std::string& section, const std::string& key,
                   const std::string& raw) {
        int s, e;
        if (!SectionRange(section, s, e)) return;
        for (int i = s; i < e; ++i) {
            std::string k, v;
            if (DirectChildKV(m_lines[i], k, v) && k == key) {
                m_lines[i] = "  " + key + ": " + raw;
                return;
            }
        }
    }

private:
    // Section header: indent 0, trimmed == "name:". Range is the lines after it
    // up to (excluding) the next indent-0 non-empty line.
    bool SectionRange(const std::string& name, int& start, int& end) const {
        const std::string header = name + ":";
        int hdr = -1;
        for (int i = 0; i < (int)m_lines.size(); ++i) {
            if (IndentOf(m_lines[i]) == 0 && TrimR(m_lines[i]) == header) { hdr = i; break; }
        }
        if (hdr < 0) return false;
        start = hdr + 1;
        end = (int)m_lines.size();
        for (int i = start; i < (int)m_lines.size(); ++i) {
            if (!TrimR(m_lines[i]).empty() && IndentOf(m_lines[i]) == 0) { end = i; break; }
        }
        return true;
    }
    // True for an indent-exactly-2 "key: value" line. key keeps internal spaces.
    static bool DirectChildKV(const std::string& line, std::string& key, std::string& value) {
        if (IndentOf(line) != 2) return false;
        std::string t = TrimR(line);
        if (t.empty() || t[0] == '#') return false;
        size_t c = t.find(':');
        if (c == std::string::npos) return false;
        key = TrimR(t.substr(0, c));
        value = TrimR(t.substr(c + 1));
        return !key.empty();
    }
    static std::string Unquote(const std::string& v) {
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
            return v.substr(1, v.size() - 2);
        return v;
    }
    std::vector<std::string> m_lines;
    bool m_crlf = false;
};

std::string BoolStr(bool b) { return b ? "true" : "false"; }

}  // namespace

std::wstring Rpcs3ConfigPath(const std::wstring& rpcs3Exe) {
    if (rpcs3Exe.empty()) return {};
    fs::path dir = fs::path(rpcs3Exe).parent_path();
    if (dir.empty()) return {};
    return (dir / "config" / "config.yml").wstring();
}

void Rpcs3LoadSettings(const std::wstring& rpcs3Exe, Rpcs3Settings& out) {
    std::wstring path = Rpcs3ConfigPath(rpcs3Exe);
    if (path.empty()) return;
    YamlLite y; y.Parse(R3ReadFile(path));
    if (y.Empty()) return;

    out.renderer      = ToWide(y.GetScalar("Video", "Renderer", ToUtf8(out.renderer)));
    out.resolution    = ToWide(y.GetScalar("Video", "Resolution", ToUtf8(out.resolution)));
    out.aspect        = ToWide(y.GetScalar("Video", "Aspect ratio", ToUtf8(out.aspect)));
    out.frameLimit    = ToWide(y.GetScalar("Video", "Frame limit", ToUtf8(out.frameLimit)));
    out.msaa          = ToWide(y.GetScalar("Video", "MSAA", ToUtf8(out.msaa)));
    out.vsync         = y.GetScalar("Video", "VSync Mode", "Disabled") != "Disabled";
    try { out.resScale = std::stoi(y.GetScalar("Video", "Resolution Scale", "100")); } catch (...) {}
    try { out.aniso    = std::stoi(y.GetScalar("Video", "Anisotropic Filter Override", "0")); } catch (...) {}
    out.writeColorBuf = y.GetScalar("Video", "Write Color Buffers", "false") == "true";
    out.stretchScreen = y.GetScalar("Video", "Stretch To Display Area", "false") == "true";
    out.audioRenderer = ToWide(y.GetScalar("Audio", "Renderer", ToUtf8(out.audioRenderer)));
    try { out.masterVolume = std::stoi(y.GetScalar("Audio", "Master Volume", "100")); } catch (...) {}
}

bool Rpcs3ApplySettings(const std::wstring& rpcs3Exe, const Rpcs3Settings& s) {
    std::wstring path = Rpcs3ConfigPath(rpcs3Exe);
    if (path.empty()) return false;
    std::string text = R3ReadFile(path);
    if (text.empty()) return false;   // never create a fresh/empty config.yml
    YamlLite y; y.Parse(text);

    y.SetScalar("Video", "Renderer", ToUtf8(s.renderer));
    y.SetScalar("Video", "Resolution", ToUtf8(s.resolution));
    y.SetScalar("Video", "Aspect ratio", ToUtf8(s.aspect));
    y.SetScalar("Video", "Frame limit", ToUtf8(s.frameLimit));
    y.SetScalar("Video", "MSAA", ToUtf8(s.msaa));
    y.SetScalar("Video", "VSync Mode", s.vsync ? "Full" : "Disabled");
    y.SetScalar("Video", "Resolution Scale", std::to_string(s.resScale));
    y.SetScalar("Video", "Anisotropic Filter Override", std::to_string(s.aniso));
    y.SetScalar("Video", "Write Color Buffers", BoolStr(s.writeColorBuf));
    y.SetScalar("Video", "Stretch To Display Area", BoolStr(s.stretchScreen));
    y.SetScalar("Audio", "Renderer", ToUtf8(s.audioRenderer));
    y.SetScalar("Audio", "Master Volume", std::to_string(s.masterVolume));

    return R3WriteFile(path, y.ToString());
}
