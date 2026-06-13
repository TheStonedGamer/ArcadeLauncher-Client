#pragma once
// SocialJson.h - tiny self-contained recursive JSON reader for the social
// subsystem. The rest of the client uses ad-hoc string scanning, which can't
// walk arrays of objects (friend lists, message pages, gateway event frames).
// Rather than vendor a 900 KB single-header library we keep a compact, fully
// owned value model here: object/array/string/number/bool/null, UTF-8 in,
// std::string keys. Parsing is allocation-light and never throws (errors yield
// a Null value). Sufficient for the small JSON the gateway exchanges.

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include <cstdlib>

namespace social {

class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JsonValue> arr;
    std::map<std::string, JsonValue> obj;

    bool isNull()   const { return type == Type::Null; }
    bool isObject() const { return type == Type::Object; }
    bool isArray()  const { return type == Type::Array; }

    // Object member access; returns a static Null on miss so chains are safe.
    const JsonValue& operator[](const std::string& key) const {
        static const JsonValue kNull;
        auto it = obj.find(key);
        return it == obj.end() ? kNull : it->second;
    }

    std::string asString(const std::string& def = "") const {
        return type == Type::String ? str : def;
    }
    int64_t asInt(int64_t def = 0) const {
        return type == Type::Number ? static_cast<int64_t>(num) : def;
    }
    uint64_t asUint(uint64_t def = 0) const {
        return type == Type::Number ? static_cast<uint64_t>(num) : def;
    }
    bool asBool(bool def = false) const {
        return type == Type::Bool ? b : def;
    }

    // Parse entry point. Returns a Null value on malformed input.
    static JsonValue Parse(const std::string& text) {
        size_t pos = 0;
        JsonValue v = ParseValue(text, pos);
        return v;
    }

private:
    static void SkipWs(const std::string& s, size_t& p) {
        while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) ++p;
    }

    static JsonValue ParseValue(const std::string& s, size_t& p) {
        SkipWs(s, p);
        if (p >= s.size()) return JsonValue{};
        char c = s[p];
        if (c == '{') return ParseObject(s, p);
        if (c == '[') return ParseArray(s, p);
        if (c == '"') { JsonValue v; v.type = Type::String; v.str = ParseString(s, p); return v; }
        if (c == 't' || c == 'f') return ParseBool(s, p);
        if (c == 'n') { p += 4; JsonValue v; v.type = Type::Null; return v; }
        return ParseNumber(s, p);
    }

    static std::string ParseString(const std::string& s, size_t& p) {
        std::string out;
        ++p; // opening quote
        while (p < s.size()) {
            char c = s[p++];
            if (c == '"') break;
            if (c == '\\' && p < s.size()) {
                char e = s[p++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case '/': out += '/'; break;
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case 'u': {
                        if (p + 4 <= s.size()) {
                            unsigned cp = (unsigned)strtoul(s.substr(p, 4).c_str(), nullptr, 16);
                            p += 4;
                            // Encode the BMP code point as UTF-8 (surrogate pairs
                            // are uncommon in our payloads; lone halves pass as-is).
                            if (cp < 0x80) out += (char)cp;
                            else if (cp < 0x800) {
                                out += (char)(0xC0 | (cp >> 6));
                                out += (char)(0x80 | (cp & 0x3F));
                            } else {
                                out += (char)(0xE0 | (cp >> 12));
                                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                                out += (char)(0x80 | (cp & 0x3F));
                            }
                        }
                        break;
                    }
                    default: out += e; break;
                }
            } else {
                out += c;
            }
        }
        return out;
    }

    static JsonValue ParseNumber(const std::string& s, size_t& p) {
        size_t start = p;
        while (p < s.size()) {
            char c = s[p];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') ++p;
            else break;
        }
        JsonValue v;
        v.type = Type::Number;
        v.num = atof(s.substr(start, p - start).c_str());
        return v;
    }

    static JsonValue ParseBool(const std::string& s, size_t& p) {
        JsonValue v;
        v.type = Type::Bool;
        if (s[p] == 't') { v.b = true; p += 4; }
        else { v.b = false; p += 5; }
        return v;
    }

    static JsonValue ParseArray(const std::string& s, size_t& p) {
        JsonValue v;
        v.type = Type::Array;
        ++p; // [
        SkipWs(s, p);
        if (p < s.size() && s[p] == ']') { ++p; return v; }
        while (p < s.size()) {
            v.arr.push_back(ParseValue(s, p));
            SkipWs(s, p);
            if (p < s.size() && s[p] == ',') { ++p; continue; }
            if (p < s.size() && s[p] == ']') { ++p; break; }
            break;
        }
        return v;
    }

    static JsonValue ParseObject(const std::string& s, size_t& p) {
        JsonValue v;
        v.type = Type::Object;
        ++p; // {
        SkipWs(s, p);
        if (p < s.size() && s[p] == '}') { ++p; return v; }
        while (p < s.size()) {
            SkipWs(s, p);
            if (p >= s.size() || s[p] != '"') break;
            std::string key = ParseString(s, p);
            SkipWs(s, p);
            if (p < s.size() && s[p] == ':') ++p;
            v.obj[key] = ParseValue(s, p);
            SkipWs(s, p);
            if (p < s.size() && s[p] == ',') { ++p; continue; }
            if (p < s.size() && s[p] == '}') { ++p; break; }
            break;
        }
        return v;
    }
};

// Minimal JSON string escaper for outbound frames.
inline std::string JsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// UTF-8 <-> UTF-16 helpers (the UI is wide-char; the wire is UTF-8).
std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& w);

} // namespace social
