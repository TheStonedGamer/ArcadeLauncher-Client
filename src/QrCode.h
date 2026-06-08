#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Minimal, self-contained QR Code generator (byte mode only) for rendering an
// otpauth:// URI as a scannable matrix. Design follows Nayuki's QR Code
// generator algorithm (auto version selection, Reed-Solomon ECC, mask penalty
// scoring). Sufficient for short URIs; supports versions 1..40, ECC level M.
//
// Usage:
//   QrCode qr;
//   if (qr.Encode("otpauth://...")) {
//       int n = qr.Size();              // module count per side
//       bool dark = qr.Module(x, y);    // true = dark module
//   }
class QrCode {
public:
    // Encodes text as a QR Code at ECC level M. Returns false if the text is too
    // long to fit in the largest supported version.
    bool Encode(const std::string& text);

    int  Size() const { return m_size; }                 // 0 if not encoded
    bool Module(int x, int y) const;                      // dark = true

private:
    int m_size = 0;
    int m_version = 0;
    std::vector<std::vector<bool>> m_modules;   // [y][x]
    std::vector<std::vector<bool>> m_isFunction;

    void setFunctionModule(int x, int y, bool dark);
    bool encodeForVersion(const std::vector<uint8_t>& data, int version);
    void drawFunctionPatterns(int eccBits);
    void drawFinderPattern(int x, int y);
    void drawAlignmentPattern(int x, int y);
    void drawFormatBits(int mask);
    void drawVersion();
    std::vector<uint8_t> addEccAndInterleave(const std::vector<uint8_t>& data);
    void drawCodewords(const std::vector<uint8_t>& data);
    void applyMask(int mask);
    long penaltyScore() const;
};
