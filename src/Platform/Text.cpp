// platform/Text.cpp — portable UTF-8 ↔ UTF-16 codec (Linux port L1).
#include "platform/Text.h"

namespace platform {

static const char16_t kRepl = 0xFFFD;

std::u16string to_utf16(const std::string& s) {
    std::u16string out;
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp;
        int extra;
        if (c < 0x80)        { cp = c;          extra = 0; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
        else if ((c >> 3) == 0x1E){ cp = c & 0x07; extra = 3; }
        else { out.push_back(kRepl); ++i; continue; }
        if (i + extra >= n) { out.push_back(kRepl); break; }
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            unsigned char cc = (unsigned char)s[i + k];
            if ((cc >> 6) != 0x2) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { out.push_back(kRepl); ++i; continue; }
        i += extra + 1;
        // Reject overlong encodings / out-of-range / surrogate range.
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { out.push_back(kRepl); continue; }
        if (cp <= 0xFFFF) {
            out.push_back((char16_t)cp);
        } else {
            cp -= 0x10000;
            out.push_back((char16_t)(0xD800 + (cp >> 10)));
            out.push_back((char16_t)(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

std::string from_utf16(const std::u16string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    auto emit = [&](uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back((char)cp);
        } else if (cp <= 0x7FF) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    };
    while (i < n) {
        char16_t u = s[i++];
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i < n && s[i] >= 0xDC00 && s[i] <= 0xDFFF) {
                uint32_t cp = 0x10000 + (((uint32_t)(u - 0xD800)) << 10) + (s[i] - 0xDC00);
                ++i;
                emit(cp);
            } else {
                emit(kRepl);   // unpaired high surrogate
            }
        } else if (u >= 0xDC00 && u <= 0xDFFF) {
            emit(kRepl);       // unpaired low surrogate
        } else {
            emit(u);
        }
    }
    return out;
}

} // namespace platform
