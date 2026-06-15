#pragma once
// platform/win/RendererD2D.h — Windows implementation of platform::IRenderer2D
// over Direct2D + DirectWrite (Linux port L1b). Mirror of the Linux nanovg
// renderer, so the shared UI code (gridview::drawGrid, and eventually all of
// Renderer.cpp) paints identically on both platforms.
//
// This adapter WRAPS an existing ID2D1RenderTarget that the caller manages: the
// caller owns BeginDraw()/EndDraw() (Renderer.cpp already brackets the frame),
// so beginFrame()/endFrame() here only reset the transform / Flush — they do not
// clear (the shared code fills its own background) and do not present. That lets
// the grid be composed inside Renderer.cpp's existing draw session.
//
// Windows-only: compiled into the MSI build (vcxproj), never the CMake/Linux
// build. The generic makeRenderer(IWindow*) factory arrives when the Win32
// IWindow lands; until then construct via makeRendererD2D().

#include "Platform/Renderer2D.h"

struct ID2D1RenderTarget;
struct IDWriteFactory;

namespace platform {

// Build an IRenderer2D that draws into an existing, caller-managed render
// target. `rt` and `dw` must outlive the returned renderer. Returns nullptr on
// failure.
std::unique_ptr<IRenderer2D> makeRendererD2D(ID2D1RenderTarget* rt,
                                             IDWriteFactory* dw);

} // namespace platform
