// Settings.cpp — portable General-page settings model (Linux port L4-g). See
// Settings.h. The JSON helpers mirror the launcher's hand-rolled config format
// (flat "key":value pairs) but stay pure std::string so they link on Linux too.
#include "Settings.h"
#include "Platform/Paths.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace settings {
namespace {

// Find the start of the scalar value for `"key":` (after the colon and any
// spaces). Returns npos when the key is absent.
size_t valueStart(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\":";
    size_t p = json.find(needle);
    if (p == std::string::npos) return std::string::npos;
    p += needle.size();
    while (p < json.size() && json[p] == ' ') ++p;
    return p;
}

bool readBool(const std::string& json, const std::string& key, bool def) {
    size_t p = valueStart(json, key);
    if (p == std::string::npos) return def;
    return json.compare(p, 4, "true") == 0;
}

int readInt(const std::string& json, const std::string& key, int def) {
    size_t p = valueStart(json, key);
    if (p == std::string::npos) return def;
    bool neg = false;
    if (p < json.size() && json[p] == '-') { neg = true; ++p; }
    if (p >= json.size() || !std::isdigit((unsigned char)json[p])) return def;
    int v = 0;
    while (p < json.size() && std::isdigit((unsigned char)json[p]))
        v = v * 10 + (json[p++] - '0');
    return neg ? -v : v;
}

// Replace the scalar value token (true/false/number) of `key` in-place with
// `text`. No-op when the key is absent. The token runs to the first ',', '}',
// whitespace, or newline — the shape of every scalar in this config.
std::string replaceScalar(std::string json, const std::string& key, const std::string& text) {
    size_t p = valueStart(json, key);
    if (p == std::string::npos) return json;
    size_t end = p;
    while (end < json.size() && json[end] != ',' && json[end] != '}' &&
           json[end] != '\n' && json[end] != '\r' && json[end] != ' ')
        ++end;
    json.replace(p, end - p, text);
    return json;
}

} // namespace

General parseGeneral(const std::string& json) {
    General g;
    g.startFullscreen     = readBool(json, "startFullscreen",     g.startFullscreen);
    g.minimizeOnLaunch    = readBool(json, "minimizeOnLaunch",    g.minimizeOnLaunch);
    g.defenderExclusions  = readBool(json, "defenderExclusions",  g.defenderExclusions);
    g.discordRichPresence = readBool(json, "discordRichPresence", g.discordRichPresence);
    g.downloadLimitKBps   = readInt (json, "downloadLimitKBps",   g.downloadLimitKBps);
    return g;
}

std::string applyGeneral(const std::string& json, const General& g) {
    auto B = [](bool v) { return std::string(v ? "true" : "false"); };
    std::string out = json;
    out = replaceScalar(out, "startFullscreen",     B(g.startFullscreen));
    out = replaceScalar(out, "minimizeOnLaunch",    B(g.minimizeOnLaunch));
    out = replaceScalar(out, "defenderExclusions",  B(g.defenderExclusions));
    out = replaceScalar(out, "discordRichPresence", B(g.discordRichPresence));
    out = replaceScalar(out, "downloadLimitKBps",   std::to_string(g.downloadLimitKBps));
    return out;
}

forms::Panel buildGeneralPanel(const General& g, const platform::Rect& bounds) {
    forms::Panel p;
    p.bounds = bounds;
    forms::addCheckbox(p, "startFullscreen",
                       "Start fullscreen  (F11 toggles at any time)", g.startFullscreen);
    forms::addCheckbox(p, "minimizeOnLaunch",
                       "Minimize launcher to tray when a game launches", g.minimizeOnLaunch);
    forms::addCheckbox(p, "defenderExclusions",
                       "Enable Windows Defender exclusions for PC game folders",
                       g.defenderExclusions);
    forms::addCheckbox(p, "discordRichPresence",
                       "Show Discord Rich Presence while a game runs", g.discordRichPresence);
    forms::addTextEdit(p, "downloadLimitKBps",
                       "Download limit (KB/s, 0 = unlimited)",
                       std::to_string(g.downloadLimitKBps));
    forms::layout(p);
    return p;
}

General readGeneralPanel(const forms::Panel& p) {
    General g;
    g.startFullscreen     = forms::boolValue(p, "startFullscreen",     g.startFullscreen);
    g.minimizeOnLaunch    = forms::boolValue(p, "minimizeOnLaunch",    g.minimizeOnLaunch);
    g.defenderExclusions  = forms::boolValue(p, "defenderExclusions",  g.defenderExclusions);
    g.discordRichPresence = forms::boolValue(p, "discordRichPresence", g.discordRichPresence);

    const std::string lim = forms::textValue(p, "downloadLimitKBps", "0");
    int v = 0;
    for (char c : lim) {
        if (!std::isdigit((unsigned char)c)) { v = 0; break; }  // non-numeric -> 0
        v = v * 10 + (c - '0');
    }
    g.downloadLimitKBps = v < 0 ? 0 : v;
    return g;
}

// ── File-backed General ──────────────────────────────────────────────────────
namespace {
// Read an entire file into a string. Returns false when it can't be opened.
bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}
} // namespace

std::string configPath() {
    return platform::join(platform::data_dir(), "config.json");
}

bool loadGeneralFromFile(const std::string& path, General& out) {
    std::string json;
    if (!readFile(path, json) || json.empty()) return false;
    out = parseGeneral(json);
    return true;
}

bool saveGeneralToFile(const std::string& path, const General& g) {
    // Only rewrite an existing config — never create the file (the Windows
    // Config::Save owns the full schema), so the portable model can't write a
    // half-populated config that the launcher would then read back.
    std::string json;
    if (!readFile(path, json) || json.empty()) return false;

    const std::string updated = applyGeneral(json, g);

    // Write to a temp sibling, then replace, so an interrupted write leaves the
    // original config intact.
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << updated;
        if (!f.good()) return false;
    }
    std::remove(path.c_str());                       // POSIX rename replaces, Win32 needs this
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());                    // leave no stray temp on failure
        return false;
    }
    return true;
}

} // namespace settings
