// CoreSmoke.cpp — Phase L0 portable-core compile proof (see PORTING_LINUX.md).
//
// This TU exists to force the genuinely cross-platform pieces of the client to
// be *compiled and linked* on Linux with no windows.h in sight: the social
// gateway JSON model (SocialJson.h) and the self-contained QR encoder
// (QrCode.h/.cpp). If this builds clean under CMake on the Debian build box, the
// portable core is real and every later phase (Net, Renderer, Widgets) has a
// green foundation to hang off of. It is NOT part of the Windows MSI build.

#include "../Social/SocialJson.h"
#include "../QrCode.h"
#include "../platform/Text.h"
#include "../platform/Crypto.h"
#include "../platform/Paths.h"

#include <cassert>
#include <string>

namespace arcade_core_smoke {

// Round-trip UTF-8 ↔ UTF-16 through the platform text codec, including an astral
// codepoint (😀 = U+1F600, a surrogate pair) and a 3-byte BMP char (€).
int exercise_text() {
    const std::string s = u8"Arcade — €5 😀";
    std::u16string w = platform::to_utf16(s);
    if (w.empty()) return 1;
    if (platform::from_utf16(w) != s) return 2;   // lossless round-trip
    return 0;
}

// Known-answer test for the vendored SHA-256 ("abc").
int exercise_sha256() {
    const std::string hex = platform::sha256_hex(std::string("abc"));
    if (hex != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
        return 1;
    return 0;
}

// The platform path helpers should yield non-empty, app-scoped directories.
int exercise_paths() {
    if (platform::data_dir().find("ArcadeLauncher") == std::string::npos) return 1;
    if (platform::temp_dir().empty()) return 2;
    if (platform::join("a", "b").size() != 3) return 3;   // "a<sep>b"
    return 0;
}

// Round-trip a representative gateway frame through the portable JSON reader.
int exercise_json() {
    const std::string frame =
        R"({"type":"chat","from":42,"text":"hi","attachmentId":7,)"
        R"("reactions":[{"emoji":"❤","userId":1}],"deleted":false})";
    social::JsonValue v = social::JsonValue::Parse(frame);
    if (!v.isObject()) return 1;
    if (v["type"].str != "chat") return 2;
    if (static_cast<int>(v["from"].num) != 42) return 3;
    if (static_cast<int>(v["attachmentId"].num) != 7) return 4;
    if (!v["reactions"].isArray() || v["reactions"].arr.size() != 1) return 5;
    return 0;
}

// Encode a short URI and confirm the matrix came out non-empty.
int exercise_qr() {
    QrCode qr;
    if (!qr.Encode("otpauth://totp/Arcade:user?secret=ABCDEF&issuer=Arcade"))
        return 1;
    if (qr.Size() <= 0) return 2;
    (void)qr.Module(0, 0);
    return 0;
}

// A single symbol the (future) core test harness can call. Returns 0 on success.
int run_self_check() {
    int r = exercise_json();
    if (r != 0) return 100 + r;
    r = exercise_qr();
    if (r != 0) return 200 + r;
    r = exercise_text();
    if (r != 0) return 300 + r;
    r = exercise_sha256();
    if (r != 0) return 400 + r;
    r = exercise_paths();
    if (r != 0) return 500 + r;
    return 0;
}

} // namespace arcade_core_smoke
