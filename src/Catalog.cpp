// Catalog.cpp — portable library.json parser (Linux port L3d). Mirrors the
// field-by-field reader in GameLibrary.cpp (ReadJsonField/ReadJsonNum/
// FindObjectEnd), operating on UTF-8 std::string with no wstring/Win32.

#include "Catalog.h"

#include <cctype>
#include <fstream>
#include <iterator>

namespace catalog {
namespace {

// Read a string field "key":"value", reversing the same escapes Save writes
// (\n, \\, \"). Returns empty if absent.
std::string readField(const std::string& json, const std::string& key) {
    const std::string search = "\"" + key + "\":\"";
    size_t p = json.find(search);
    if (p == std::string::npos) return {};
    p += search.size();
    std::string val;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) {
            ++p;
            if (json[p] == 'n') val += '\n';
            else if (json[p] == '\\') val += '\\';
            else if (json[p] == '"') val += '"';
            else val += json[p];
        } else {
            val += json[p];
        }
        ++p;
    }
    return val;
}

uint64_t readNum(const std::string& json, const std::string& key) {
    const std::string search = "\"" + key + "\":";
    size_t p = json.find(search);
    if (p == std::string::npos) return 0;
    p += search.size();
    while (p < json.size() && json[p] == ' ') ++p;
    uint64_t v = 0;
    while (p < json.size() && std::isdigit((unsigned char)json[p]))
        v = v * 10 + (json[p++] - '0');
    return v;
}

bool readBool(const std::string& json, const std::string& key) {
    const std::string search = "\"" + key + "\":";
    size_t p = json.find(search);
    if (p == std::string::npos) return false;
    p += search.size();
    while (p < json.size() && json[p] == ' ') ++p;
    return json.compare(p, 4, "true") == 0;
}

// Find the matching '}' for the '{' at start, skipping braces inside strings.
size_t findObjectEnd(const std::string& raw, size_t start) {
    int depth = 0;
    bool inStr = false;
    for (size_t i = start; i < raw.size(); ++i) {
        char c = raw[i];
        if (inStr) {
            if (c == '\\') { ++i; continue; }   // skip escaped char
            if (c == '"') inStr = false;
        } else {
            if (c == '"') inStr = true;
            else if (c == '{') ++depth;
            else if (c == '}') { if (--depth == 0) return i; }
        }
    }
    return std::string::npos;
}

} // namespace

std::vector<Game> parse(const std::string& json) {
    std::vector<Game> out;
    size_t pos = 0;
    while (true) {
        size_t start = json.find('{', pos);
        if (start == std::string::npos) break;
        size_t end = findObjectEnd(json, start);
        if (end == std::string::npos) break;
        std::string obj = json.substr(start, end - start + 1);
        pos = end + 1;

        Game g;
        g.id = readField(obj, "id");
        if (g.id.empty()) continue;   // skip malformed entries
        g.title           = readField(obj, "title");
        g.platform        = readField(obj, "platform");
        g.installState    = readField(obj, "installState");
        g.coverArtPath    = readField(obj, "coverArtPath");
        g.coverArtUrl     = readField(obj, "coverArtUrl");
        g.developer       = readField(obj, "developer");
        g.publisher       = readField(obj, "publisher");
        g.franchise       = readField(obj, "franchise");
        g.genres          = readField(obj, "genres");
        g.contentPath     = readField(obj, "contentPath");
        g.releaseDate     = (int64_t)readNum(obj, "releaseDate");
        g.playtimeSeconds = readNum(obj, "playtimeSeconds");
        g.favorite        = readBool(obj, "favorite");
        g.hidden          = readBool(obj, "hidden");
        out.push_back(std::move(g));
    }
    return out;
}

std::vector<Game> loadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::string raw((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());
    return parse(raw);
}

} // namespace catalog
