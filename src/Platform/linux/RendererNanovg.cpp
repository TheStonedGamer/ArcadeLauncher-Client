// RendererNanovg.cpp — nanovg/OpenGL implementation of platform::IRenderer2D
// (Linux port L3b). A close match to the Direct2D surface the Windows UI uses:
// filled/stroked rounded rects, ellipses, lines, gradients, images, text, and a
// clip (scissor) stack. Presents to the SDL window's GL context.
//
// nanovg's core (nanovg.c) is compiled into arcade_gui; this TU pulls in the
// GL3 backend implementation. GL entry points are loaded via GLEW.
//
// Only compiled on non-Windows builds where SDL2/OpenGL/GLEW were found.

#include "Platform/Renderer2D.h"
#include "Platform/Window.h"

#include <GL/glew.h>
#include <SDL.h>

#include "nanovg.h"
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace platform {
namespace {

NVGcolor nvg(const Color& c) { return nvgRGBAf(c.r, c.g, c.b, c.a); }

// Resolve a font family to a TTF path. A path (contains '/') is used as-is;
// otherwise we fall back to DejaVu, which ships on essentially every desktop
// Linux. Real fontconfig family lookup arrives with the L4 widget set.
std::string resolveFontPath(const std::string& family, bool bold) {
    if (family.find('/') != std::string::npos) return family;
    const char* base = "/usr/share/fonts/truetype/dejavu/";
    return std::string(base) + (bold ? "DejaVuSans-Bold.ttf" : "DejaVuSans.ttf");
}

struct FontEntry {
    int   handle = -1;   // nanovg font id
    float size  = 16.0f;
};

class NanovgRenderer final : public IRenderer2D {
public:
    bool init(IWindow* window) {
        window_ = window;
        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK) return false;
        glGetError();  // swallow the benign INVALID_ENUM GLEW leaves behind

        vg_ = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
        return vg_ != nullptr;
    }

    ~NanovgRenderer() override {
        if (vg_) nvgDeleteGL3(vg_);
    }

    void beginFrame(int pxWidth, int pxHeight, float dpiScale) override {
        pxW_ = pxWidth; pxH_ = pxHeight;
        glViewport(0, 0, pxWidth, pxHeight);
        // Caller chose the clear color via a fillRect; clear to transparent here
        // so the first fillRect over the whole surface defines the background.
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        const float ratio = dpiScale > 0 ? dpiScale : 1.0f;
        nvgBeginFrame(vg_, pxWidth / ratio, pxHeight / ratio, ratio);
        clip_.clear();
    }

    void endFrame() override {
        nvgEndFrame(vg_);
        if (window_)
            SDL_GL_SwapWindow(static_cast<SDL_Window*>(window_->nativeHandle()));
    }

    FontId loadFont(const std::string& family, float pxSize, bool bold) override {
        const std::string path = resolveFontPath(family, bold);
        const std::string name = "f" + std::to_string(nextFont_);
        int h = nvgCreateFont(vg_, name.c_str(), path.c_str());
        FontEntry e;
        e.handle = h;          // -1 if the file was missing; drawText no-ops then
        e.size   = pxSize;
        const FontId id = nextFont_++;
        fonts_[id] = e;
        return id;
    }

    ImageId createImageRGBA(const uint8_t* pixels, int w, int h) override {
        int img = nvgCreateImageRGBA(vg_, w, h, 0, pixels);
        const ImageId id = nextImage_++;
        images_[id] = img;
        return id;
    }
    void destroyImage(ImageId id) override {
        auto it = images_.find(id);
        if (it != images_.end()) { nvgDeleteImage(vg_, it->second); images_.erase(it); }
    }

    void fillRect(const Rect& r, const Color& c) override {
        nvgBeginPath(vg_);
        nvgRect(vg_, r.x, r.y, r.w, r.h);
        nvgFillColor(vg_, nvg(c));
        nvgFill(vg_);
    }
    void fillRoundedRect(const Rect& r, float radius, const Color& c) override {
        nvgBeginPath(vg_);
        nvgRoundedRect(vg_, r.x, r.y, r.w, r.h, radius);
        nvgFillColor(vg_, nvg(c));
        nvgFill(vg_);
    }
    void strokeRoundedRect(const Rect& r, float radius, const Color& c,
                           float width) override {
        nvgBeginPath(vg_);
        nvgRoundedRect(vg_, r.x, r.y, r.w, r.h, radius);
        nvgStrokeColor(vg_, nvg(c));
        nvgStrokeWidth(vg_, width);
        nvgStroke(vg_);
    }
    void fillEllipse(float cx, float cy, float rx, float ry,
                     const Color& c) override {
        nvgBeginPath(vg_);
        nvgEllipse(vg_, cx, cy, rx, ry);
        nvgFillColor(vg_, nvg(c));
        nvgFill(vg_);
    }
    void drawLine(float x0, float y0, float x1, float y1, const Color& c,
                  float width) override {
        nvgBeginPath(vg_);
        nvgMoveTo(vg_, x0, y0);
        nvgLineTo(vg_, x1, y1);
        nvgStrokeColor(vg_, nvg(c));
        nvgStrokeWidth(vg_, width);
        nvgStroke(vg_);
    }
    void linearGradientRect(const Rect& r, const Color& a, const Color& b,
                            bool vertical) override {
        NVGpaint p = vertical
            ? nvgLinearGradient(vg_, r.x, r.y, r.x, r.y + r.h, nvg(a), nvg(b))
            : nvgLinearGradient(vg_, r.x, r.y, r.x + r.w, r.y, nvg(a), nvg(b));
        nvgBeginPath(vg_);
        nvgRect(vg_, r.x, r.y, r.w, r.h);
        nvgFillPaint(vg_, p);
        nvgFill(vg_);
    }
    void drawImage(ImageId id, const Rect& dst, float opacity) override {
        auto it = images_.find(id);
        if (it == images_.end()) return;
        NVGpaint p = nvgImagePattern(vg_, dst.x, dst.y, dst.w, dst.h, 0,
                                     it->second, opacity);
        nvgBeginPath(vg_);
        nvgRect(vg_, dst.x, dst.y, dst.w, dst.h);
        nvgFillPaint(vg_, p);
        nvgFill(vg_);
    }

    float drawText(FontId id, const std::string& utf8, float x, float y,
                   const Color& c, TextAlign align) override {
        const FontEntry* f = font(id);
        if (!f || f->handle < 0) return 0;
        applyFont(*f, align);
        nvgFillColor(vg_, nvg(c));
        return nvgText(vg_, x, y, utf8.c_str(), nullptr);
    }
    float measureText(FontId id, const std::string& utf8) override {
        const FontEntry* f = font(id);
        if (!f || f->handle < 0) return 0;
        applyFont(*f, TextAlign::Left);
        float bounds[4];
        return nvgTextBounds(vg_, 0, 0, utf8.c_str(), nullptr, bounds);
    }

    void pushClip(const Rect& r) override {
        clip_.push_back(r);
        nvgIntersectScissor(vg_, r.x, r.y, r.w, r.h);
    }
    void popClip() override {
        if (clip_.empty()) return;
        clip_.pop_back();
        nvgResetScissor(vg_);
        // Re-apply the remaining stack as a running intersection.
        for (const Rect& r : clip_)
            nvgIntersectScissor(vg_, r.x, r.y, r.w, r.h);
    }

private:
    const FontEntry* font(FontId id) const {
        auto it = fonts_.find(id);
        return it == fonts_.end() ? nullptr : &it->second;
    }
    void applyFont(const FontEntry& f, TextAlign align) {
        nvgFontFaceId(vg_, f.handle);
        nvgFontSize(vg_, f.size);
        int a = NVG_ALIGN_TOP;
        a |= (align == TextAlign::Center) ? NVG_ALIGN_CENTER
           : (align == TextAlign::Right)  ? NVG_ALIGN_RIGHT
                                          : NVG_ALIGN_LEFT;
        nvgTextAlign(vg_, a);
    }

    IWindow*   window_ = nullptr;
    NVGcontext* vg_    = nullptr;
    int pxW_ = 0, pxH_ = 0;

    std::unordered_map<FontId, FontEntry> fonts_;
    std::unordered_map<ImageId, int>      images_;
    FontId  nextFont_  = 1;
    ImageId nextImage_ = 1;
    std::vector<Rect> clip_;
};

} // namespace

std::unique_ptr<IRenderer2D> makeRenderer(IWindow* window) {
    auto r = std::make_unique<NanovgRenderer>();
    if (!r->init(window)) return nullptr;
    return r;
}

} // namespace platform
