// platform/win/RendererD2D.cpp — Direct2D/DirectWrite implementation of
// platform::IRenderer2D (Linux port L1b). See RendererD2D.h for the contract.

#include "Platform/win/RendererD2D.h"
#include "Platform/Text.h"   // platform::widen (UTF-8 -> UTF-16)

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

// NB: we deliberately draw text via ID2D1RenderTarget::DrawTextLayout (not
// DrawText) — <windows.h>'s `#define DrawText DrawTextW` macro otherwise
// mangles the method name, and DrawTextLayout has no such collision.

using Microsoft::WRL::ComPtr;

namespace platform {
namespace {

inline D2D1_COLOR_F toD2D(const Color& c) {
    return D2D1::ColorF(c.r, c.g, c.b, c.a);
}
inline D2D1_RECT_F toD2D(const Rect& r) {
    return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h);
}

class RendererD2D final : public IRenderer2D {
public:
    RendererD2D(ID2D1RenderTarget* rt, IDWriteFactory* dw) : m_rt(rt), m_dw(dw) {
        m_rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_brush);
    }

    void beginFrame(int pxWidth, int pxHeight, float dpiScale) override {
        m_w = pxWidth; m_h = pxHeight; (void)dpiScale;
        // Caller owns BeginDraw/Clear; we just normalize the transform.
        m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    void endFrame() override { m_rt->Flush(); }

    FontId loadFont(const std::string& family, float pxSize, bool bold) override {
        std::wstring fam = family.empty() ? L"Segoe UI" : widen(family);
        ComPtr<IDWriteTextFormat> fmt;
        HRESULT hr = m_dw->CreateTextFormat(
            fam.c_str(), nullptr,
            bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, pxSize, L"",
            &fmt);
        if (FAILED(hr) || !fmt) return 0;
        // Top-left anchored by default; per-call alignment set in drawText.
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        m_fonts.push_back(fmt);
        return (FontId)m_fonts.size();  // 1-based; 0 = invalid
    }

    ImageId createImageRGBA(const uint8_t* pixels, int w, int h) override {
        if (!pixels || w <= 0 || h <= 0) return 0;
        // RGBA8 straight -> BGRA8 premultiplied (D2D's native bitmap layout).
        std::vector<uint8_t> bgra((size_t)w * h * 4);
        for (size_t i = 0; i < (size_t)w * h; ++i) {
            uint8_t r = pixels[i * 4 + 0], g = pixels[i * 4 + 1],
                    b = pixels[i * 4 + 2], a = pixels[i * 4 + 3];
            bgra[i * 4 + 0] = (uint8_t)(b * a / 255);
            bgra[i * 4 + 1] = (uint8_t)(g * a / 255);
            bgra[i * 4 + 2] = (uint8_t)(r * a / 255);
            bgra[i * 4 + 3] = a;
        }
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                              D2D1_ALPHA_MODE_PREMULTIPLIED));
        ComPtr<ID2D1Bitmap> bmp;
        HRESULT hr = m_rt->CreateBitmap(D2D1::SizeU(w, h), bgra.data(),
                                        (UINT32)(w * 4), props, &bmp);
        if (FAILED(hr) || !bmp) return 0;
        m_images.push_back(bmp);
        return (ImageId)m_images.size();  // 1-based; 0 = invalid
    }
    void destroyImage(ImageId id) override {
        if (id >= 1 && id <= m_images.size()) m_images[id - 1].Reset();
    }

    void fillRect(const Rect& r, const Color& c) override {
        m_brush->SetColor(toD2D(c));
        m_rt->FillRectangle(toD2D(r), m_brush.Get());
    }
    void fillRoundedRect(const Rect& r, float radius, const Color& c) override {
        m_brush->SetColor(toD2D(c));
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(toD2D(r), radius, radius),
                                   m_brush.Get());
    }
    void strokeRoundedRect(const Rect& r, float radius, const Color& c,
                           float width) override {
        m_brush->SetColor(toD2D(c));
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(toD2D(r), radius, radius),
                                   m_brush.Get(), width);
    }
    void fillEllipse(float cx, float cy, float rx, float ry,
                     const Color& c) override {
        m_brush->SetColor(toD2D(c));
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry),
                          m_brush.Get());
    }
    void drawLine(float x0, float y0, float x1, float y1, const Color& c,
                  float width) override {
        m_brush->SetColor(toD2D(c));
        m_rt->DrawLine(D2D1::Point2F(x0, y0), D2D1::Point2F(x1, y1),
                       m_brush.Get(), width);
    }
    void linearGradientRect(const Rect& r, const Color& a, const Color& b,
                            bool vertical) override {
        D2D1_GRADIENT_STOP stops[2] = {{0.0f, toD2D(a)}, {1.0f, toD2D(b)}};
        ComPtr<ID2D1GradientStopCollection> coll;
        if (FAILED(m_rt->CreateGradientStopCollection(stops, 2, &coll))) return;
        D2D1_POINT_2F p0 = D2D1::Point2F(r.x, r.y);
        D2D1_POINT_2F p1 = vertical ? D2D1::Point2F(r.x, r.y + r.h)
                                    : D2D1::Point2F(r.x + r.w, r.y);
        ComPtr<ID2D1LinearGradientBrush> grad;
        if (FAILED(m_rt->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(p0, p1), coll.Get(), &grad)))
            return;
        m_rt->FillRectangle(toD2D(r), grad.Get());
    }

    void drawImage(ImageId id, const Rect& dst, float opacity) override {
        if (id < 1 || id > m_images.size() || !m_images[id - 1]) return;
        m_rt->DrawBitmap(m_images[id - 1].Get(), toD2D(dst), opacity,
                         D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }

    float drawText(FontId f, const std::string& utf8, float x, float y,
                   const Color& c, TextAlign align) override {
        if (f < 1 || f > m_fonts.size()) return 0;
        std::wstring ws = widen(utf8);
        // Reproduce nanovg's x-as-anchor semantics via a wide layout box +
        // horizontal alignment (Left: x=left, Center: x=center, Right: x=right).
        const float BIG = 8000.0f;
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(m_dw->CreateTextLayout(ws.c_str(), (UINT32)ws.size(),
                                          m_fonts[f - 1].Get(), BIG, BIG,
                                          &layout)))
            return 0;
        float ox = x;
        switch (align) {
            case TextAlign::Center:
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                ox = x - BIG / 2;
                break;
            case TextAlign::Right:
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                ox = x - BIG;
                break;
            default:
                layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                ox = x;
                break;
        }
        m_brush->SetColor(toD2D(c));
        m_rt->DrawTextLayout(D2D1::Point2F(ox, y), layout.Get(), m_brush.Get(),
                             D2D1_DRAW_TEXT_OPTIONS_CLIP);
        return measureText(f, utf8);
    }
    float measureText(FontId f, const std::string& utf8) override {
        if (f < 1 || f > m_fonts.size()) return 0;
        std::wstring ws = widen(utf8);
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(m_dw->CreateTextLayout(ws.c_str(), (UINT32)ws.size(),
                                          m_fonts[f - 1].Get(), 1e6f, 1e6f,
                                          &layout)))
            return 0;
        DWRITE_TEXT_METRICS tm{};
        layout->GetMetrics(&tm);
        return tm.widthIncludingTrailingWhitespace;
    }

    void pushClip(const Rect& r) override {
        m_rt->PushAxisAlignedClip(toD2D(r), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        ++m_clipDepth;
    }
    void popClip() override {
        if (m_clipDepth > 0) { m_rt->PopAxisAlignedClip(); --m_clipDepth; }
    }

private:
    ID2D1RenderTarget* m_rt;   // not owned
    IDWriteFactory*    m_dw;    // not owned
    ComPtr<ID2D1SolidColorBrush> m_brush;
    std::vector<ComPtr<IDWriteTextFormat>> m_fonts;
    std::vector<ComPtr<ID2D1Bitmap>> m_images;
    int m_w = 0, m_h = 0, m_clipDepth = 0;
};

} // namespace

std::unique_ptr<IRenderer2D> makeRendererD2D(ID2D1RenderTarget* rt,
                                             IDWriteFactory* dw) {
    if (!rt || !dw) return nullptr;
    return std::make_unique<RendererD2D>(rt, dw);
}

} // namespace platform
