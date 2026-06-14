#pragma once
// platform/Crypto.h — portable cryptographic helpers (Linux port L1).
//
// Replaces the Windows wincrypt SHA-256 used for download verification with a
// vendored, dependency-free implementation that compiles identically on both
// platforms. The existing Windows code can adopt this incrementally.

#include <array>
#include <cstdint>
#include <cstddef>
#include <string>

namespace platform {

// Raw 32-byte SHA-256 digest of `len` bytes at `data`.
std::array<uint8_t, 32> sha256(const void* data, size_t len);

// Lowercase hex string of the SHA-256 digest of a byte range.
std::string sha256_hex(const void* data, size_t len);

inline std::string sha256_hex(const std::string& s) {
    return sha256_hex(s.data(), s.size());
}

} // namespace platform
