// ImageSelfCheckMain.cpp — Phase L3c headless image-codec self-check.
//
// Round-trips a known RGBA8 image through PNG encode → decode and asserts the
// pixels survive. Pure CPU; no display or GL needed, so it gates in CI like the
// other core checks. Exit 0 on success.

#include "Platform/Image.h"

#include <cstdint>
#include <cstdio>
#include <vector>

int main() {
    // Build a small known image: a 4x4 with a distinct corner color.
    const int W = 4, H = 4;
    std::vector<uint8_t> src((size_t)W * H * 4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            size_t i = ((size_t)y * W + x) * 4;
            src[i + 0] = (uint8_t)(x * 60 + 10);
            src[i + 1] = (uint8_t)(y * 60 + 20);
            src[i + 2] = (uint8_t)(x * y * 12 + 5);
            src[i + 3] = 255;
        }

    std::vector<uint8_t> png = platform::encodePngRGBA(src.data(), W, H);
    if (png.empty()) { std::printf("image self-check: ENCODE FAILED\n"); return 1; }
    // PNG magic check (\x89PNG).
    if (png.size() < 8 || png[0] != 0x89 || png[1] != 'P' || png[2] != 'N' ||
        png[3] != 'G') {
        std::printf("image self-check: bad PNG signature\n");
        return 2;
    }

    platform::DecodedImage dec;
    if (!platform::decodeImageRGBA(png.data(), png.size(), dec)) {
        std::printf("image self-check: DECODE FAILED\n");
        return 3;
    }
    if (dec.w != W || dec.h != H) {
        std::printf("image self-check: size mismatch %dx%d != %dx%d\n",
                    dec.w, dec.h, W, H);
        return 4;
    }
    if (dec.rgba != src) {
        std::printf("image self-check: pixels did not round-trip\n");
        return 5;
    }

    std::printf("image self-check: OK (%dx%d RGBA round-tripped through %zu-byte PNG)\n",
                W, H, png.size());
    return 0;
}
