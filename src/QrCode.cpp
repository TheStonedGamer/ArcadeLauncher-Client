#include "pch.h"
#include "QrCode.h"
#include <algorithm>
#include <cstdlib>

// ─────────────────────────────────────────────────────────────────────────────
// QR Code generator — byte mode, ECC level M, versions 1..40.
// Algorithm faithfully follows Nayuki's reference QR Code generator.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// ECC codewords per block, ECC level M, indexed by version (index 0 unused).
const int8_t ECC_PER_BLOCK[41] = {
    -1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26,
    26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28,
    28, 28, 28
};

// Number of error-correction blocks, ECC level M, indexed by version.
const int8_t NUM_BLOCKS[41] = {
    -1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17,
    17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49
};

int getNumRawDataModules(int ver) {
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2) {
        int numAlign = ver / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (ver >= 7)
            result -= 36;
    }
    return result;
}

int getNumDataCodewords(int ver) {
    return getNumRawDataModules(ver) / 8
         - ECC_PER_BLOCK[ver] * NUM_BLOCKS[ver];
}

// ── Reed-Solomon over GF(256), primitive 0x11D ──────────────────────────────
uint8_t rsMultiply(uint8_t x, uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return (uint8_t)z;
}

std::vector<uint8_t> rsComputeDivisor(int degree) {
    std::vector<uint8_t> result(degree, 0);
    result[degree - 1] = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; i++) {
        for (size_t j = 0; j < result.size(); j++) {
            result[j] = rsMultiply(result[j], root);
            if (j + 1 < result.size())
                result[j] ^= result[j + 1];
        }
        root = rsMultiply(root, 0x02);
    }
    return result;
}

std::vector<uint8_t> rsComputeRemainder(const std::vector<uint8_t>& data,
                                        const std::vector<uint8_t>& divisor) {
    std::vector<uint8_t> result(divisor.size(), 0);
    for (uint8_t b : data) {
        uint8_t factor = b ^ result[0];
        result.erase(result.begin());
        result.push_back(0);
        for (size_t i = 0; i < result.size(); i++)
            result[i] ^= rsMultiply(divisor[i], factor);
    }
    return result;
}

std::vector<int> getAlignmentPatternPositions(int ver) {
    if (ver == 1)
        return {};
    int numAlign = ver / 7 + 2;
    int step = (ver == 32) ? 26
             : (ver * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
    std::vector<int> result;
    int size = ver * 4 + 17;
    for (int i = 0, pos = size - 7; i < numAlign - 1; i++, pos -= step)
        result.insert(result.begin(), pos);
    result.insert(result.begin(), 6);
    return result;
}

// Simple bit buffer (append MSB-first).
struct BitBuffer : std::vector<bool> {
    void appendBits(uint32_t val, int len) {
        for (int i = len - 1; i >= 0; i--)
            push_back(((val >> i) & 1) != 0);
    }
};

} // namespace

bool QrCode::Module(int x, int y) const {
    if (x < 0 || y < 0 || x >= m_size || y >= m_size) return false;
    return m_modules[y][x];
}

void QrCode::setFunctionModule(int x, int y, bool dark) {
    m_modules[y][x] = dark;
    m_isFunction[y][x] = true;
}

bool QrCode::Encode(const std::string& text) {
    // Build the data segment (byte mode).
    std::vector<uint8_t> bytes(text.begin(), text.end());

    // Pick the smallest version (1..40) that fits at ECC level M.
    int version = 0;
    for (int v = 1; v <= 40; v++) {
        int capacityBits = getNumDataCodewords(v) * 8;
        int ccbits = (v <= 9) ? 8 : 16;
        int usedBits = 4 + ccbits + (int)bytes.size() * 8;
        if (usedBits <= capacityBits) {
            version = v;
            break;
        }
    }
    if (version == 0)
        return false; // too long even for version 40

    return encodeForVersion(bytes, version);
}

bool QrCode::encodeForVersion(const std::vector<uint8_t>& bytes, int version) {
    m_version = version;
    m_size = version * 4 + 17;
    m_modules.assign(m_size, std::vector<bool>(m_size, false));
    m_isFunction.assign(m_size, std::vector<bool>(m_size, false));

    // ── Assemble the data bit stream ────────────────────────────────────────
    BitBuffer bb;
    bb.appendBits(0x4, 4); // byte mode indicator
    int ccbits = (version <= 9) ? 8 : 16;
    bb.appendBits((uint32_t)bytes.size(), ccbits);
    for (uint8_t b : bytes)
        bb.appendBits(b, 8);

    int dataCapacityBits = getNumDataCodewords(version) * 8;
    // Terminator (up to 4 bits) + pad to byte boundary.
    int terminator = std::min(4, dataCapacityBits - (int)bb.size());
    bb.appendBits(0, terminator);
    bb.appendBits(0, (8 - (int)bb.size() % 8) % 8);
    // Pad bytes 0xEC / 0x11 alternating.
    for (uint8_t pad = 0xEC; (int)bb.size() < dataCapacityBits; pad ^= 0xEC ^ 0x11)
        bb.appendBits(pad, 8);

    std::vector<uint8_t> dataCodewords(bb.size() / 8, 0);
    for (size_t i = 0; i < bb.size(); i++)
        if (bb[i])
            dataCodewords[i >> 3] |= (uint8_t)(1 << (7 - (i & 7)));

    // ── Draw everything ─────────────────────────────────────────────────────
    drawFunctionPatterns(ECC_PER_BLOCK[version]);
    std::vector<uint8_t> allCodewords = addEccAndInterleave(dataCodewords);
    drawCodewords(allCodewords);

    // Choose the mask with the lowest penalty.
    int bestMask = 0;
    long minPenalty = -1;
    for (int mask = 0; mask < 8; mask++) {
        applyMask(mask);
        drawFormatBits(mask);
        long p = penaltyScore();
        if (minPenalty < 0 || p < minPenalty) {
            minPenalty = p;
            bestMask = mask;
        }
        applyMask(mask); // undo (XOR is its own inverse)
    }
    applyMask(bestMask);
    drawFormatBits(bestMask);
    return true;
}

void QrCode::drawFunctionPatterns(int /*eccBits*/) {
    // Timing patterns.
    for (int i = 0; i < m_size; i++) {
        setFunctionModule(6, i, i % 2 == 0);
        setFunctionModule(i, 6, i % 2 == 0);
    }
    // Finder patterns (three corners).
    drawFinderPattern(3, 3);
    drawFinderPattern(m_size - 4, 3);
    drawFinderPattern(3, m_size - 4);

    // Alignment patterns.
    std::vector<int> align = getAlignmentPatternPositions(m_version);
    int n = (int)align.size();
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            // Skip the three finder-pattern corners.
            if ((i == 0 && j == 0) || (i == 0 && j == n - 1) || (i == n - 1 && j == 0))
                continue;
            drawAlignmentPattern(align[i], align[j]);
        }
    }

    // Reserve format + version areas (drawn with dummy mask now, real later).
    drawFormatBits(0);
    drawVersion();
}

void QrCode::drawFinderPattern(int cx, int cy) {
    for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
            int dist = std::max(std::abs(dx), std::abs(dy));
            int x = cx + dx, y = cy + dy;
            if (x >= 0 && x < m_size && y >= 0 && y < m_size)
                setFunctionModule(x, y, dist != 2 && dist != 4);
        }
    }
}

void QrCode::drawAlignmentPattern(int cx, int cy) {
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            setFunctionModule(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
}

void QrCode::drawFormatBits(int mask) {
    // ECC level M => formatBits = 0.
    int data = (0 << 3) | mask;
    int rem = data;
    for (int i = 0; i < 10; i++)
        rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = ((data << 10) | rem) ^ 0x5412;

    // First copy.
    for (int i = 0; i <= 5; i++)
        setFunctionModule(8, i, ((bits >> i) & 1) != 0);
    setFunctionModule(8, 7, ((bits >> 6) & 1) != 0);
    setFunctionModule(8, 8, ((bits >> 7) & 1) != 0);
    setFunctionModule(7, 8, ((bits >> 8) & 1) != 0);
    for (int i = 9; i < 15; i++)
        setFunctionModule(14 - i, 8, ((bits >> i) & 1) != 0);

    // Second copy.
    for (int i = 0; i < 8; i++)
        setFunctionModule(m_size - 1 - i, 8, ((bits >> i) & 1) != 0);
    for (int i = 8; i < 15; i++)
        setFunctionModule(8, m_size - 15 + i, ((bits >> i) & 1) != 0);
    setFunctionModule(8, m_size - 8, true); // always-dark module
}

void QrCode::drawVersion() {
    if (m_version < 7)
        return;
    int rem = m_version;
    for (int i = 0; i < 12; i++)
        rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    long bits = ((long)m_version << 12) | rem;

    for (int i = 0; i < 18; i++) {
        bool bit = ((bits >> i) & 1) != 0;
        int a = m_size - 11 + i % 3;
        int b = i / 3;
        setFunctionModule(a, b, bit);
        setFunctionModule(b, a, bit);
    }
}

std::vector<uint8_t> QrCode::addEccAndInterleave(const std::vector<uint8_t>& data) {
    int ver = m_version;
    int numBlocks = NUM_BLOCKS[ver];
    int blockEccLen = ECC_PER_BLOCK[ver];
    int rawCodewords = getNumRawDataModules(ver) / 8;
    int numShortBlocks = numBlocks - rawCodewords % numBlocks;
    int shortBlockLen = rawCodewords / numBlocks;

    std::vector<std::vector<uint8_t>> blocks;
    std::vector<uint8_t> rsDiv = rsComputeDivisor(blockEccLen);
    for (int i = 0, k = 0; i < numBlocks; i++) {
        int datLen = shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1);
        std::vector<uint8_t> dat(data.begin() + k, data.begin() + k + datLen);
        k += datLen;
        std::vector<uint8_t> ecc = rsComputeRemainder(dat, rsDiv);
        // Pad short blocks' data with a placeholder so interleaving aligns.
        if (i < numShortBlocks)
            dat.push_back(0);
        dat.insert(dat.end(), ecc.begin(), ecc.end());
        blocks.push_back(std::move(dat));
    }

    std::vector<uint8_t> result;
    for (size_t i = 0; i < blocks[0].size(); i++) {
        for (size_t j = 0; j < blocks.size(); j++) {
            // Skip the padding cell that only short blocks carry.
            if (!(i == (size_t)(shortBlockLen - blockEccLen) && j < (size_t)numShortBlocks))
                result.push_back(blocks[j][i]);
        }
    }
    return result;
}

void QrCode::drawCodewords(const std::vector<uint8_t>& data) {
    size_t i = 0; // bit index into data
    for (int right = m_size - 1; right >= 1; right -= 2) {
        if (right == 6)
            right = 5;
        for (int vert = 0; vert < m_size; vert++) {
            for (int j = 0; j < 2; j++) {
                int x = right - j;
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? m_size - 1 - vert : vert;
                if (!m_isFunction[y][x] && i < data.size() * 8) {
                    m_modules[y][x] = ((data[i >> 3] >> (7 - (i & 7))) & 1) != 0;
                    i++;
                }
            }
        }
    }
}

void QrCode::applyMask(int mask) {
    for (int y = 0; y < m_size; y++) {
        for (int x = 0; x < m_size; x++) {
            if (m_isFunction[y][x])
                continue;
            bool invert = false;
            switch (mask) {
                case 0: invert = (x + y) % 2 == 0; break;
                case 1: invert = y % 2 == 0; break;
                case 2: invert = x % 3 == 0; break;
                case 3: invert = (x + y) % 3 == 0; break;
                case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
                case 5: invert = x * y % 2 + x * y % 3 == 0; break;
                case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
                case 7: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
            }
            if (invert)
                m_modules[y][x] = !m_modules[y][x];
        }
    }
}

long QrCode::penaltyScore() const {
    long result = 0;
    const int N1 = 3, N2 = 3, N3 = 40, N4 = 10;

    // Adjacent same-color runs in rows and columns.
    for (int y = 0; y < m_size; y++) {
        bool runColor = false;
        int runX = 0;
        for (int x = 0; x < m_size; x++) {
            if (m_modules[y][x] == runColor) {
                runX++;
                if (runX == 5) result += N1;
                else if (runX > 5) result++;
            } else {
                runColor = m_modules[y][x];
                runX = 1;
            }
        }
    }
    for (int x = 0; x < m_size; x++) {
        bool runColor = false;
        int runY = 0;
        for (int y = 0; y < m_size; y++) {
            if (m_modules[y][x] == runColor) {
                runY++;
                if (runY == 5) result += N1;
                else if (runY > 5) result++;
            } else {
                runColor = m_modules[y][x];
                runY = 1;
            }
        }
    }

    // 2x2 blocks of same color.
    for (int y = 0; y < m_size - 1; y++) {
        for (int x = 0; x < m_size - 1; x++) {
            bool c = m_modules[y][x];
            if (c == m_modules[y][x + 1] && c == m_modules[y + 1][x] && c == m_modules[y + 1][x + 1])
                result += N2;
        }
    }

    // Finder-like patterns (1:1:3:1:1 with adjacent light) — rows and columns.
    for (int y = 0; y < m_size; y++) {
        int bits = 0;
        for (int x = 0; x < m_size; x++) {
            bits = ((bits << 1) & 0x7FF) | (m_modules[y][x] ? 1 : 0);
            if (x >= 10 && (bits == 0x05D || bits == 0x5D0))
                result += N3;
        }
    }
    for (int x = 0; x < m_size; x++) {
        int bits = 0;
        for (int y = 0; y < m_size; y++) {
            bits = ((bits << 1) & 0x7FF) | (m_modules[y][x] ? 1 : 0);
            if (y >= 10 && (bits == 0x05D || bits == 0x5D0))
                result += N3;
        }
    }

    // Balance of dark/light modules.
    int dark = 0;
    for (int y = 0; y < m_size; y++)
        for (int x = 0; x < m_size; x++)
            if (m_modules[y][x]) dark++;
    int total = m_size * m_size;
    int k = (std::abs(dark * 20 - total * 10) + total - 1) / total - 1;
    result += (long)k * N4;
    return result;
}
