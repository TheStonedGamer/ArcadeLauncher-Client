#pragma once
// platform/Text.h — UTF-8 ↔ UTF-16 conversion at the OS boundary (Linux port L1).
//
// The portable core standardizes on UTF-8 `std::string` (see PORTING_LINUX.md §2).
// These helpers convert to/from UTF-16 (`std::u16string`) only where an OS API
// needs wide strings. On Windows, `wchar_t` is 16-bit, so `widen()/narrow()`
// bridge UTF-8 `std::string` ↔ `std::wstring` for Win32 calls; they are compiled
// only under `_WIN32`. The `u16` functions are fully portable and unit-tested in
// the core self-check.

#include <string>
#include <cstdint>

namespace platform {

// UTF-8 std::string  ->  UTF-16 std::u16string. Invalid sequences are replaced
// with U+FFFD. Handles the full BMP plus surrogate pairs for astral planes.
std::u16string to_utf16(const std::string& utf8);

// UTF-16 std::u16string  ->  UTF-8 std::string. Lone/again-invalid surrogates
// are replaced with U+FFFD.
std::string from_utf16(const std::u16string& utf16);

#ifdef _WIN32
// Win32 boundary convenience: UTF-8 <-> UTF-16 as std::wstring. wchar_t is a
// 16-bit UTF-16 code unit on Windows, so this is a thin reinterpret over the
// portable u16 codec above. Not defined on Linux (wchar_t there is 32-bit and
// the core never hands a wstring to an OS API).
inline std::wstring widen(const std::string& utf8) {
    std::u16string w = to_utf16(utf8);
    return std::wstring(w.begin(), w.end());
}
inline std::string narrow(const std::wstring& wide) {
    std::u16string w(wide.begin(), wide.end());
    return from_utf16(w);
}
#endif

} // namespace platform
