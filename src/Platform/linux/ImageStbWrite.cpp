// ImageStbWrite.cpp — stb_image_write half of the platform image codec
// (Linux port L3c). Kept separate from ImageStb.cpp because stb_image and
// stb_image_write define colliding static helpers (e.g. stbi__paeth) and cannot
// share a translation unit.

#include "Platform/Image.h"

#include <cstdlib>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC   // keep encode symbols local to this TU
#include "stb_image_write.h"

namespace platform {

std::vector<uint8_t> encodePngRGBA(const uint8_t* rgba, int w, int h) {
    // This stb_image_write build exposes stbi_write_png_to_mem (returns a
    // malloc'd buffer) rather than the newer _to_func variant.
    int len = 0;
    unsigned char* png = stbi_write_png_to_mem(
        const_cast<unsigned char*>(rgba), w * 4, w, h, 4, &len);
    if (!png || len <= 0) { std::free(png); return {}; }
    std::vector<uint8_t> out(png, png + len);
    std::free(png);   // stbi_write_png_to_mem allocates with the default malloc
    return out;
}

} // namespace platform
