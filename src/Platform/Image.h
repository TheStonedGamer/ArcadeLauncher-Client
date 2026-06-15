#pragma once
// platform/Image.h — image decode/encode boundary (Linux port L3c).
//
// Cover art / attachments arrive as encoded bytes (PNG/JPG/…) and must become
// RGBA8 pixels for the renderer (IRenderer2D::createImageRGBA). On Windows this
// is WIC (Renderer::DecodeImageFile); on Linux it's stb_image. All paths UTF-8.

#include <cstdint>
#include <string>
#include <vector>

namespace platform {

struct DecodedImage {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;   // w*h*4, row-major, top-down, premultiplied=no
    bool valid() const { return w > 0 && h > 0 && rgba.size() == (size_t)w * h * 4; }
};

// Decode an encoded image (PNG/JPG/BMP/…) from memory into RGBA8.
bool decodeImageRGBA(const uint8_t* data, size_t n, DecodedImage& out);

// Decode from a file path (UTF-8). Returns false if the file is missing/unreadable.
bool decodeImageFileRGBA(const std::string& path, DecodedImage& out);

// Encode RGBA8 pixels to PNG bytes (used for screenshots/round-trip tests).
// Returns empty on failure.
std::vector<uint8_t> encodePngRGBA(const uint8_t* rgba, int w, int h);

} // namespace platform
