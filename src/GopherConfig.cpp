#include "pch.h"
#include "GopherConfig.h"

namespace {

std::string GReadFile(const std::wstring& path) {
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
bool GWriteFile(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == data.size();
}

std::string TrimG(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// Minimal JSON editor for pretty-printed config.json (one key per line). Targets
// scalar keys inside a named object. Only rewrites the value token of an exact
// matching `"key":` line, keeping indentation and any trailing comma — so it
// can never restructure the document.
class JsonLite {
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

    std::string GetScalar(const std::string& obj, const std::string& key,
                          const std::string& def) const {
        int s, e;
        if (!ObjectRange(obj, s, e)) return def;
        int idx = FindKey(s, e, key);
        if (idx < 0) return def;
        std::string v;
        ValueOf(m_lines[idx], v);
        return v;
    }
    void SetScalar(const std::string& obj, const std::string& key, const std::string& raw) {
        int s, e;
        if (!ObjectRange(obj, s, e)) return;
        int idx = FindKey(s, e, key);
        if (idx < 0) return;
        const std::string& line = m_lines[idx];
        size_t indent = line.find_first_not_of(" \t");
        std::string lead = (indent == std::string::npos) ? std::string() : line.substr(0, indent);
        bool comma = !TrimG(line).empty() && TrimG(line).back() == ',';
        m_lines[idx] = lead + "\"" + key + "\": " + raw + (comma ? "," : "");
    }

private:
    // Find `"obj": {` line; range covers the lines between it and its closing
    // brace (matched by indentation of the opening line).
    bool ObjectRange(const std::string& obj, int& start, int& end) const {
        const std::string needle = "\"" + obj + "\":";
        int open = -1; size_t openIndent = 0;
        for (int i = 0; i < (int)m_lines.size(); ++i) {
            std::string t = TrimG(m_lines[i]);
            if (t.rfind(needle, 0) == 0 && t.find('{') != std::string::npos) {
                open = i;
                openIndent = m_lines[i].find_first_not_of(" \t");
                break;
            }
        }
        if (open < 0) return false;
        start = open + 1;
        end = (int)m_lines.size();
        for (int i = start; i < (int)m_lines.size(); ++i) {
            size_t ind = m_lines[i].find_first_not_of(" \t");
            std::string t = TrimG(m_lines[i]);
            if (ind == openIndent && !t.empty() && t[0] == '}') { end = i; break; }
        }
        return true;
    }
    int FindKey(int start, int end, const std::string& key) const {
        const std::string needle = "\"" + key + "\":";
        for (int i = start; i < end; ++i) {
            std::string t = TrimG(m_lines[i]);
            if (t.rfind(needle, 0) == 0) return i;
        }
        return -1;
    }
    static void ValueOf(const std::string& line, std::string& value) {
        std::string t = TrimG(line);
        size_t c = t.find(':');
        if (c == std::string::npos) { value.clear(); return; }
        std::string v = TrimG(t.substr(c + 1));
        if (!v.empty() && v.back() == ',') v.pop_back();
        value = TrimG(v);
    }
    std::vector<std::string> m_lines;
    bool m_crlf = false;
};

std::string BoolJ(bool b) { return b ? "true" : "false"; }

}  // namespace

std::wstring GopherConfigPath() {
    PWSTR roam = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roam)))
        out = std::wstring(roam) + L"\\gopher64\\config.json";
    if (roam) CoTaskMemFree(roam);
    return out;
}

void GopherLoadSettings(GopherSettings& out) {
    std::wstring path = GopherConfigPath();
    if (path.empty()) return;
    JsonLite j; j.Parse(GReadFile(path));
    if (j.Empty()) return;

    try { out.upscale = std::stoi(j.GetScalar("video", "upscale", "1")); } catch (...) {}
    out.ssaa            = j.GetScalar("video", "ssaa", "false") == "true";
    out.integerScaling  = j.GetScalar("video", "integer_scaling", "false") == "true";
    out.fullscreen      = j.GetScalar("video", "fullscreen", "false") == "true";
    out.widescreen      = j.GetScalar("video", "widescreen", "false") == "true";
    out.vsync           = j.GetScalar("video", "vsync", "true") == "true";
    out.crt             = j.GetScalar("video", "crt", "false") == "true";
    out.overclock       = j.GetScalar("emulation", "overclock", "false") == "true";
    out.disableExpansionPak = j.GetScalar("emulation", "disable_expansion_pak", "false") == "true";
    out.usb             = j.GetScalar("emulation", "usb", "false") == "true";
}

bool GopherApplySettings(const GopherSettings& s) {
    std::wstring path = GopherConfigPath();
    if (path.empty()) return false;
    std::string text = GReadFile(path);
    if (text.empty()) return false;   // never create a fresh/empty config.json
    JsonLite j; j.Parse(text);

    j.SetScalar("video", "upscale", std::to_string(s.upscale));
    j.SetScalar("video", "ssaa", BoolJ(s.ssaa));
    j.SetScalar("video", "integer_scaling", BoolJ(s.integerScaling));
    j.SetScalar("video", "fullscreen", BoolJ(s.fullscreen));
    j.SetScalar("video", "widescreen", BoolJ(s.widescreen));
    j.SetScalar("video", "vsync", BoolJ(s.vsync));
    j.SetScalar("video", "crt", BoolJ(s.crt));
    j.SetScalar("emulation", "overclock", BoolJ(s.overclock));
    j.SetScalar("emulation", "disable_expansion_pak", BoolJ(s.disableExpansionPak));
    j.SetScalar("emulation", "usb", BoolJ(s.usb));

    return GWriteFile(path, j.ToString());
}
