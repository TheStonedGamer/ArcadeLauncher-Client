// RendererD2DSelfCheckMain.cpp — Windows headless KAT for the Direct2D
// IRenderer2D backend (Linux port L1b). The Windows analog of the Linux
// gui_smoke/gui_demo pixel checks: it draws the SHARED gridview::drawGrid into
// an off-screen WIC bitmap render target through platform::makeRendererD2D,
// then reads tile-0's center pixel back and asserts it equals the card's flat
// placeholder color. Proves the D2D adapter paints what the shared UI asks for.
//
// Not part of the launcher exe — compiled standalone:
//   scripts\build-d2d-selfcheck.cmd     (runs vcvars64 + cl, then the exe)
// Exit 0 on success.

#include "Platform/win/RendererD2D.h"
#include "CatalogGridView.h"
#include "GridLayout.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

int main() {
    const int W = 1024, H = 680;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        std::printf("d2d self-check: FAILED (CoInitialize)\n"); return 1;
    }

    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dw;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 d2d.GetAddressOf())) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                   __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dw.GetAddressOf()))) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        std::printf("d2d self-check: FAILED (factory create)\n"); return 1;
    }

    ComPtr<IWICBitmap> wbmp;
    if (FAILED(wic->CreateBitmap(W, H, GUID_WICPixelFormat32bppPBGRA,
                                 WICBitmapCacheOnLoad, &wbmp))) {
        std::printf("d2d self-check: FAILED (WIC bitmap)\n"); return 1;
    }
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(d2d->CreateWicBitmapRenderTarget(wbmp.Get(), rtp, &rt))) {
        std::printf("d2d self-check: FAILED (WIC RT)\n"); return 1;
    }

    auto r = platform::makeRendererD2D(rt.Get(), dw.Get());
    if (!r) { std::printf("d2d self-check: FAILED (adapter)\n"); return 1; }

    gridview::Theme th = gridview::darkTheme();
    th.titleFont = r->loadFont("", 22, true);
    th.bodyFont  = r->loadFont("", 15, false);

    // Tile 0 has a flat placeholder (no cover) — the deterministic anchor.
    const platform::Color tile0{0.16f, 0.42f, 0.55f, 1.0f};
    std::vector<gridview::Card> cards;
    {
        gridview::Card c; c.title = "Hollow Knight"; c.platform = "PC";
        c.placeholder = tile0; cards.push_back(c);
    }
    for (int i = 1; i < 8; ++i) {
        gridview::Card c; c.title = "Game"; c.platform = "PC";
        c.placeholder = platform::Color{0.3f, 0.3f, 0.3f, 1.0f};
        c.favorite = (i % 2 == 0);           // exercise the favorite overlay
        c.install = (gridview::Install)(1 + (i % 3));  // exercise install dots
        if (i % 3 == 0) c.variantCount = i + 1;        // exercise count badge
        if (i % 2 == 1) {                              // exercise checkbox
            c.selectionMode = true;
            c.multiSelected = (i % 4 == 1);
        }
        cards.push_back(c);
    }
    const std::vector<std::string> tabs = {"All Games", "PC", "Favorites"};

    rt->BeginDraw();
    gridview::drawGrid(*r, W, H, 0.0f, cards, tabs, th);
    HRESULT hr = rt->EndDraw();
    if (FAILED(hr)) { std::printf("d2d self-check: FAILED (EndDraw 0x%08lx)\n", hr); return 1; }

    // Read tile-0 center back. PBGRA: bytes B,G,R,A (premultiplied; tile is opaque).
    grid::Metrics m = grid::Metrics::forViewport(W, H);
    grid::Rect g = grid::tileRect(m, 0, 0.0f);
    int sx = (int)(g.x + g.w / 2), sy = (int)(g.y + g.h / 2);

    WICRect lockRect{0, 0, W, H};
    ComPtr<IWICBitmapLock> lock;
    if (FAILED(wbmp->Lock(&lockRect, WICBitmapLockRead, &lock))) {
        std::printf("d2d self-check: FAILED (lock)\n"); return 1;
    }
    UINT cb = 0, stride = 0; BYTE* data = nullptr;
    lock->GetStride(&stride);
    lock->GetDataPointer(&cb, &data);
    const BYTE* px = data + (size_t)sy * stride + (size_t)sx * 4;
    int gb = px[0], gg = px[1], gr = px[2];

    int er = (int)(tile0.r * 255 + 0.5f), eg = (int)(tile0.g * 255 + 0.5f),
        eb = (int)(tile0.b * 255 + 0.5f);
    auto approxEq = [](int a, int b) { return a - b <= 10 && b - a <= 10; };
    bool ok = approxEq(gr, er) && approxEq(gg, eg) && approxEq(gb, eb);

    std::printf("d2d self-check: %s — %dx%d, tile0 center (%d,%d,%d) expected ~(%d,%d,%d)\n",
                ok ? "OK" : "FAILED", W, H, gr, gg, gb, er, eg, eb);
    return ok ? 0 : 1;
}
