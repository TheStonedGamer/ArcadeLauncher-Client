// GameVariants.cpp — portable ROM-dump variant logic (Linux port L3d-c).
// Mirrors the Windows Game::Variant* methods byte-for-byte on the ASCII dump
// tags; operates on UTF-8 std::string so both platforms share it.

#include "GameVariants.h"

#include <cctype>
#include <vector>

namespace variants {

namespace {

// ASCII lowercase. Dump tags ([!], [a1], (Proto), PRG 1, …) are all ASCII, so
// a byte-wise lower is exactly the Windows towlower result for them; for the
// grouping key, dumps of one game share an identical title, so any consistent
// lowering groups them identically regardless of non-ASCII bytes.
std::string toLower(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        c = (char)std::tolower((unsigned char)c);
    return out;
}

bool contains(const std::string& hay, const char* needle) {
    return hay.find(needle) != std::string::npos;
}

} // namespace

std::string fileStem(const std::string& contentPath,
                     const std::string& titleFallback) {
    std::string s = contentPath;
    size_t slash = s.find_last_of("\\/");
    if (slash != std::string::npos) s = s.substr(slash + 1);
    size_t dot = s.find_last_of('.');
    if (dot != std::string::npos && dot > 0) s = s.substr(0, dot);
    return s.empty() ? titleFallback : s;
}

std::string key(int platformId, const std::string& title) {
    return std::to_string(platformId) + "|" + toLower(title);
}

std::string label(const std::string& contentPath,
                  const std::string& titleFallback) {
    const std::string low = toLower(fileStem(contentPath, titleFallback));
    std::vector<std::string> parts;
    auto has = [&](const char* s) { return contains(low, s); };
    if (has("[!]"))                                   parts.push_back("Verified");
    if (has("prototype") || has("proto"))             parts.push_back("Prototype");
    if (has("prg 1") || has("prg1"))                  parts.push_back("PRG 1");
    if (has("trad-en") || has("t-en") || has("[t+en") || has("[tr en"))
                                                      parts.push_back("Eng patch");
    // Alt-dump index [a1]/[a2]/…
    size_t ap = low.find("[a");
    if (ap != std::string::npos && ap + 2 < low.size() &&
        std::isdigit((unsigned char)low[ap + 2]))
        parts.push_back(std::string("Alt ") + low[ap + 2]);
    if (has("[b"))                                    parts.push_back("Bad dump");
    if (has("[h"))                                    parts.push_back("Hack");
    if (has("[p"))                                    parts.push_back("Pirate");
    std::string out;
    for (const std::string& p : parts) { if (!out.empty()) out += ", "; out += p; }
    return out;
}

int score(const std::string& contentPath, const std::string& titleFallback,
          bool installedServerCopy) {
    int s = 100;
    if (installedServerCopy) s -= 1000;
    const std::string low = toLower(fileStem(contentPath, titleFallback));
    auto has = [&](const char* x) { return contains(low, x); };
    if (has("[!]")) s -= 50;
    if (has("[a")) s += 15;
    if (has("prototype") || has("proto")) s += 40;
    if (has("[b")) s += 60;
    if (has("[h") || has("[p") || has("[t")) s += 30;
    if (has("(u)") || has("(usa)")) s -= 5;   // mild US-region preference
    return s;
}

} // namespace variants
