#pragma once
#include "pch.h"

// Minimal section-aware key/value config editor shared by the per-emulator
// config modules. Works for both INI ([Section] / key = value) and the flat
// TOML dialects emulators use (Xenia, xemu): the caller formats each value
// (quoting strings, lowercase booleans, etc.) and the editor preserves every
// line it does not explicitly change, including comments and unmanaged keys.
namespace cfgkv {

inline std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

class IniFile {
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

    // Serialize with CRLF line endings (what Windows emulators write).
    std::string ToString() const {
        std::string out;
        for (const auto& l : m_lines) { out += l; out += "\r\n"; }
        return out;
    }

    // Raw value text (inline trailing comments stripped). Empty `section`
    // searches keys before the first section header.
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
        std::string v = Get(section, key, def ? "true" : "false");
        return v == "true" || v == "True" || v == "1";
    }
    int GetInt(const std::string& section, const std::string& key, int def) const {
        std::string v = Get(section, key, "");
        // Strip surrounding quotes if any, then parse decimal or 0x-hex.
        if (!v.empty() && v.front() == '"') {
            size_t q = v.find('"', 1);
            v = (q != std::string::npos) ? v.substr(1, q - 1) : v.substr(1);
        }
        if (v.empty()) return def;
        try { return (int)std::stoul(v, nullptr, 0); } catch (...) { return def; }
    }
    // String value with surrounding double-quotes removed (TOML strings).
    std::string GetStr(const std::string& section, const std::string& key,
                       const std::string& def) const {
        std::string v = Get(section, key, "");
        if (v.empty()) return def;
        if (v.front() == '"') {
            size_t q = v.find('"', 1);
            return (q != std::string::npos) ? v.substr(1, q - 1) : v.substr(1);
        }
        return v;
    }

    // Write `key = <raw>` verbatim (caller pre-formats the value text).
    void SetRaw(const std::string& section, const std::string& key, const std::string& raw) {
        const std::string line = key + " = " + raw;
        int s, e;
        if (!FindSection(section, s, e)) {
            if (!m_lines.empty() && !Trim(m_lines.back()).empty()) m_lines.push_back("");
            if (!section.empty()) m_lines.push_back("[" + section + "]");
            m_lines.push_back(line);
            return;
        }
        for (int i = s + 1; i < e; ++i) {
            std::string k, v;
            if (SplitKV(m_lines[i], k, v) && k == key) { m_lines[i] = line; return; }
        }
        int ins = e;
        while (ins - 1 > s && Trim(m_lines[ins - 1]).empty()) --ins;
        m_lines.insert(m_lines.begin() + ins, line);
    }
    void SetInt (const std::string& sec, const std::string& key, int v) { SetRaw(sec, key, std::to_string(v)); }
    void SetBoolLower(const std::string& sec, const std::string& key, bool v) { SetRaw(sec, key, v ? "true" : "false"); }
    void SetBoolWord (const std::string& sec, const std::string& key, bool v) { SetRaw(sec, key, v ? "True" : "False"); }
    void SetQuoted(const std::string& sec, const std::string& key, const std::string& v) { SetRaw(sec, key, "\"" + v + "\""); }

private:
    // Locate [section]: header index in `start`, one-past-section in `end`.
    // Empty section name => the implicit pre-header block at the top of file.
    bool FindSection(const std::string& section, int& start, int& end) const {
        if (section.empty()) {
            start = -1;
            end = (int)m_lines.size();
            for (int i = 0; i < (int)m_lines.size(); ++i)
                if (!Trim(m_lines[i]).empty() && Trim(m_lines[i]).front() == '[') { end = i; break; }
            return true;
        }
        const std::string header = "[" + section + "]";
        start = -1;
        for (int i = 0; i < (int)m_lines.size(); ++i) {
            std::string t = Trim(m_lines[i]);
            if (start < 0) {
                if (t == header) start = i;
            } else if (!t.empty() && t.front() == '[') { end = i; return true; }
        }
        if (start < 0) return false;
        end = (int)m_lines.size();
        return true;
    }

    // Split "key = value", stripping inline trailing comments (TOML/INI use
    // '#'; INI also ';'). Quoted string values keep their content intact.
    static bool SplitKV(const std::string& line, std::string& key, std::string& value) {
        std::string t = Trim(line);
        if (t.empty() || t.front() == '[' || t.front() == '#' || t.front() == ';') return false;
        size_t eq = t.find('=');
        if (eq == std::string::npos) return false;
        key = Trim(t.substr(0, eq));
        std::string v = Trim(t.substr(eq + 1));
        if (!v.empty() && v.front() == '"') {
            size_t q = v.find('"', 1);
            value = (q != std::string::npos) ? v.substr(0, q + 1) : v;
        } else {
            size_t h = v.find('#');
            size_t sc = v.find(';');
            size_t cut = std::min(h, sc);
            value = (cut != std::string::npos) ? Trim(v.substr(0, cut)) : v;
        }
        return !key.empty();
    }

    std::vector<std::string> m_lines;
};

}  // namespace cfgkv
