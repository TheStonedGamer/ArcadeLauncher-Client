// ImageStb.cpp — stb_image implementation of the platform image codec
// (Linux port L3c). Decodes PNG/JPG/BMP/etc. to RGBA8 and encodes RGBA8 to PNG.
// Replaces WIC on Linux. stb headers come from the fetched nanovg checkout.
//
// Only compiled on non-Windows builds where the renderer (and thus stb) is
// available (see CMakeLists.txt).

#include "Platform/Image.h"

#include <cstdio>
#include <cstdlib>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC         // nanovg.c also embeds stb_image; keep ours local
#define STBI_NO_STDIO            // we feed bytes; file IO done explicitly below
#include "stb_image.h"

// NOTE: stb_image and stb_image_write share static helper names (e.g.
// stbi__paeth), so they cannot live in one TU — encode is in ImageStbWrite.cpp.

namespace platform {

bool decodeImageRGBA(const uint8_t* data, size_t n, DecodedImage& out) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load_from_memory(data, (int)n, &w, &h, &comp, 4);
    if (!px) return false;
    out.w = w; out.h = h;
    out.rgba.assign(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    return out.valid();
}

bool decodeImageFileRGBA(const std::string& path, DecodedImage& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); return false; }
    std::vector<uint8_t> bytes((size_t)sz);
    size_t rd = std::fread(bytes.data(), 1, (size_t)sz, f);
    std::fclose(f);
    if (rd != (size_t)sz) return false;
    return decodeImageRGBA(bytes.data(), bytes.size(), out);
}

} // namespace platform
