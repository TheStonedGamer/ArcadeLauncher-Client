#include "pch.h"
#include "MesenConfig.h"

namespace {

std::string MReadFile(const std::wstring& path) {
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
bool MWriteFile(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    BOOL ok = WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h);
    return ok && wr == data.size();
}

std::string TrimM(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// Nested-object JSON editor (same approach as GopherConfig's JsonLite): rewrites
// only the value token of a scalar `"key":` line inside a named object, keeping
// indentation + trailing comma. Preserves the UTF-8 BOM since line 0 is never
// modified. Never restructures the document.
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
        return ValueOf(m_lines[idx]);
    }
    void SetScalar(const std::string& obj, const std::string& key,
                   const std::string& raw) {
        int s, e;
        if (!ObjectRange(obj, s, e)) return;
        int idx = FindKey(s, e, key);
        if (idx < 0) return;
        const std::string& line = m_lines[idx];
        size_t indent = line.find_first_not_of(" \t");
        std::string lead = (indent == std::string::npos) ? std::string()
                                                         : line.substr(0, indent);
        bool comma = !TrimM(line).empty() && TrimM(line).back() == ',';
        m_lines[idx] = lead + "\"" + key + "\": " + raw + (comma ? "," : "");
    }

private:
    bool ObjectRange(const std::string& obj, int& start, int& end) const {
        const std::string needle = "\"" + obj + "\":";
        int open = -1; size_t openIndent = 0;
        for (int i = 0; i < (int)m_lines.size(); ++i) {
            std::string t = TrimM(m_lines[i]);
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
            std::string t = TrimM(m_lines[i]);
            if (ind == openIndent && !t.empty() && t[0] == '}') { end = i; break; }
        }
        return true;
    }
    // Match a key only at the object's immediate-child indentation, so a nested
    // sub-object key of the same name is never picked up.
    int FindKey(int start, int end, const std::string& key) const {
        const std::string needle = "\"" + key + "\":";
        size_t childIndent = std::string::npos;
        for (int i = start; i < end; ++i) {
            size_t ind = m_lines[i].find_first_not_of(" \t");
            if (ind == std::string::npos) continue;
            if (childIndent == std::string::npos) childIndent = ind;
            if (ind != childIndent) continue;   // skip deeper nested keys
            if (TrimM(m_lines[i]).rfind(needle, 0) == 0) return i;
        }
        return -1;
    }
    static std::string ValueOf(const std::string& line) {
        std::string t = TrimM(line);
        size_t c = t.find(':');
        if (c == std::string::npos) return {};
        std::string v = TrimM(t.substr(c + 1));
        if (!v.empty() && v.back() == ',') v.pop_back();
        return TrimM(v);
    }
    std::vector<std::string> m_lines;
    bool m_crlf = false;
};

std::string BoolM(bool b) { return b ? "true" : "false"; }

std::wstring Unq(const std::string& v) {
    std::string s = v;
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.substr(1, s.size() - 2);
    return ToWide(s);
}
std::string Q(const std::wstring& w) { return "\"" + ToUtf8(w) + "\""; }

int IntOr(const std::string& v, int def) {
    try { return std::stoi(v); } catch (...) { return def; }
}

bool FileExists(const std::wstring& p) {
    return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

std::wstring MesenConfigPath(const std::wstring& exePath) {
    // Portable: settings.json next to Mesen.exe.
    if (!exePath.empty()) {
        size_t slash = exePath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            std::wstring portable = exePath.substr(0, slash) + L"\\settings.json";
            if (FileExists(portable)) return portable;
        }
    }
    // Default: %USERPROFILE%\Documents\Mesen2\settings.json.
    PWSTR docs = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)))
        out = std::wstring(docs) + L"\\Mesen2\\settings.json";
    if (docs) CoTaskMemFree(docs);
    return out;
}

void MesenLoadSettings(const std::wstring& exePath, const std::wstring& console,
                       MesenSettings& out) {
    std::wstring path = MesenConfigPath(exePath);
    if (path.empty()) return;
    JsonLite j; j.Parse(MReadFile(path));
    if (j.Empty()) return;
    std::string con = ToUtf8(console);

    out.aspectRatio = Unq(j.GetScalar("Video", "AspectRatio", "\"NoStretching\""));
    out.videoFilter = Unq(j.GetScalar("Video", "VideoFilter", "\"None\""));
    out.verticalSync           = j.GetScalar("Video", "VerticalSync", "false") == "true";
    out.bilinear               = j.GetScalar("Video", "UseBilinearInterpolation", "false") == "true";
    out.integerFpsMode         = j.GetScalar("Video", "IntegerFpsMode", "false") == "true";
    out.fullscreenIntegerScale = j.GetScalar("Video", "FullscreenForceIntegerScale", "false") == "true";
    out.exclusiveFullscreen    = j.GetScalar("Video", "UseExclusiveFullscreen", "false") == "true";

    out.masterVolume     = IntOr(j.GetScalar("Audio", "MasterVolume", "100"), 100);
    out.audioLatency     = IntOr(j.GetScalar("Audio", "AudioLatency", "60"), 60);
    out.enableAudio      = j.GetScalar("Audio", "EnableAudio", "true") == "true";
    out.muteInBackground = j.GetScalar("Audio", "MuteSoundInBackground", "false") == "true";

    out.runAheadFrames = IntOr(j.GetScalar("Emulation", "RunAheadFrames", "0"), 0);
    out.enableRewind   = j.GetScalar("Preferences", "EnableRewind", "true") == "true";

    out.region = Unq(j.GetScalar(con, "Region", "\"Auto\""));
}

bool MesenApplySettings(const std::wstring& exePath, const std::wstring& console,
                        const MesenSettings& s) {
    std::wstring path = MesenConfigPath(exePath);
    if (path.empty()) return false;
    std::string text = MReadFile(path);
    if (text.empty()) return false;   // never create a fresh/empty settings.json
    JsonLite j; j.Parse(text);
    std::string con = ToUtf8(console);

    j.SetScalar("Video", "AspectRatio", Q(s.aspectRatio));
    j.SetScalar("Video", "VideoFilter", Q(s.videoFilter));
    j.SetScalar("Video", "VerticalSync", BoolM(s.verticalSync));
    j.SetScalar("Video", "UseBilinearInterpolation", BoolM(s.bilinear));
    j.SetScalar("Video", "IntegerFpsMode", BoolM(s.integerFpsMode));
    j.SetScalar("Video", "FullscreenForceIntegerScale", BoolM(s.fullscreenIntegerScale));
    j.SetScalar("Video", "UseExclusiveFullscreen", BoolM(s.exclusiveFullscreen));

    auto clamp = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    j.SetScalar("Audio", "MasterVolume", std::to_string(clamp(s.masterVolume, 0, 100)));
    j.SetScalar("Audio", "AudioLatency", std::to_string(clamp(s.audioLatency, 15, 300)));
    j.SetScalar("Audio", "EnableAudio", BoolM(s.enableAudio));
    j.SetScalar("Audio", "MuteSoundInBackground", BoolM(s.muteInBackground));

    j.SetScalar("Emulation", "RunAheadFrames", std::to_string(clamp(s.runAheadFrames, 0, 10)));
    j.SetScalar("Preferences", "EnableRewind", BoolM(s.enableRewind));

    j.SetScalar(con, "Region", Q(s.region));

    return MWriteFile(path, j.ToString());
}
