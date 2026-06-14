#pragma once
// platform/Renderer2D.h — immediate-mode 2D drawing boundary (Linux port L1).
//
// A close conceptual match to the Direct2D surface the UI already uses (filled
// /stroked rounded rects, text, images, clip stack, gradients). Windows wraps
// Direct2D/DirectWrite (L1); Linux implements with nanovg over OpenGL (L3). The
// existing Renderer.cpp migrates from ID2D1RenderTarget to this interface.

#include <cstdint>
#include <memory>
#include <string>

namespace platform {

struct Color { float r = 0, g = 0, b = 0, a = 1; };
struct Rect  { float x = 0, y = 0, w = 0, h = 0; };

enum class TextAlign { Left, Center, Right };

// Opaque handles owned by the renderer.
using FontId  = uint32_t;
using ImageId = uint32_t;

class IRenderer2D {
public:
    virtual ~IRenderer2D() = default;

    virtual void beginFrame(int pxWidth, int pxHeight, float dpiScale) = 0;
    virtual void endFrame() = 0;             // present/swap

    // Fonts (family lookup via fontconfig on Linux / DirectWrite on Windows).
    virtual FontId loadFont(const std::string& family, float pxSize, bool bold) = 0;

    // Images: decode raw RGBA8 pixels into a GPU/texture handle.
    virtual ImageId createImageRGBA(const uint8_t* pixels, int w, int h) = 0;
    virtual void    destroyImage(ImageId) = 0;

    // Shapes.
    virtual void fillRect(const Rect&, const Color&) = 0;
    virtual void fillRoundedRect(const Rect&, float radius, const Color&) = 0;
    virtual void strokeRoundedRect(const Rect&, float radius, const Color&, float width) = 0;
    virtual void fillEllipse(float cx, float cy, float rx, float ry, const Color&) = 0;
    virtual void drawLine(float x0, float y0, float x1, float y1, const Color&, float width) = 0;
    virtual void linearGradientRect(const Rect&, const Color& a, const Color& b, bool vertical) = 0;

    // Image blit (fit/cover handled by the caller via src/dst rects).
    virtual void drawImage(ImageId, const Rect& dst, float opacity) = 0;

    // Text. Returns the advance width; measure-only when measureOnly is true.
    virtual float drawText(FontId, const std::string& utf8, float x, float y,
                           const Color&, TextAlign align = TextAlign::Left) = 0;
    virtual float measureText(FontId, const std::string& utf8) = 0;

    // Clip stack.
    virtual void pushClip(const Rect&) = 0;
    virtual void popClip() = 0;
};

} // namespace platform
