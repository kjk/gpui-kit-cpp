#ifndef GPUI_GPUI_PAINTGPU_H_
#define GPUI_GPUI_PAINTGPU_H_
/* The two custom Windows backends for Paint.h, which draw the way GPUI's own
   renderer does. A default build contains Direct2D only; define
   WIN_BACKEND_D3D11 or WIN_BACKEND_D3D12 to compile one of these instead, or
   WIN_BACKEND_ALL to compile all three and retain the runtime selector. See
   the note at the end for what they are worth.

   The default Windows backend is already on the GPU: Direct2D on a D3D11
   device, presenting through a DXGI flip-model swap chain. What it is not is
   GPUI's *shape* of GPU renderer. D2D takes one call per primitive and
   decides for itself how to batch them; Blade — and `directx_renderer.rs`
   behind it — puts every rounded rect, border and glyph of a frame into one
   instance buffer and issues a handful of draws, with the rounding, the
   border and the content mask evaluated analytically in the pixel shader.
   This is that, far enough along to draw the story gallery and the showcase
   and be timed against the D2D path on the same machine in the same process.
   One CPU front end builds the batches, path fans, glyph atlas and image
   uploads. Native D3D11 and D3D12 submission halves consume the same data;
   D3D12 does not pass through D3D11On12.

   In a WIN_BACKEND_ALL build, `__paint=d2d|d3d11|d3d12` on the command line
   picks its native submission API. A fixed build ignores unavailable choices.
   `__msaa=1|2|4|8` sets the sample count (default 4): quads and
   glyphs carry their own analytic coverage, but tessellated paths and
   expanded strokes get their antialiasing from the sample count, and its
   cost is worth being able to see.

   The HLSL lives in paintgpu_win.hlsl, but an ordinary build never compiles
   it. `bun cmd/update-win-shaders.ts` runs fxc /O3 /WX for VSQuad, PSQuad,
   VSTri and PSTri and writes checked-in paintgpu_shaders_win.cpp. Its DXBC is
   encoded with all 95 printable ASCII characters in C++ raw strings and
   decoded once into BSS when the first custom device opens. build.ts compares
   the HLSL SHA-256 with the generated marker and stops with the regeneration
   command if they differ. Thus shipping binaries neither link D3DCompiler nor
   depend on D3DCompiler_47.dll.

   What lives where: everything device-independent is shared with
   paint_win.cpp rather than written three times — the DirectWrite factory
   and its text formats, an IDWriteTextLayout (which is what a `TextLayout*`
   is on Windows, so shaping, measurement, hit-testing and range rects are the
   same code on all paths), and WIC image decode. D3D11 additionally borrows the
   default backend's device; D3D12 owns its device, queue, triple-buffered
   command allocators and upload heaps. Only targets and submission differ. */

#include "gpui/paint.h"

#if GPUI_OS_WINDOWS

namespace gpui {

// These project WinPaintOptions into the shared custom-renderer front end.
bool PaintGpuOn();
// Which native submission half the shared GPU renderer uses.
bool PaintD3d12On();
// The sample count the GPU backend renders at; 1 is no multisampling.
int PaintGpuSamples();

// ─── borrowed from the D2D backend ───────────────────────────────────────
//
// Implemented in paint_win.cpp, which owns them. They come back as void*
// rather than as their COM types so that this header — which lands in the
// amalgamated gpui.h on every platform — pulls in no D3D or DirectWrite
// headers of its own.

// ID3D11Device*, created on first use. Null if the machine has neither a
// hardware device nor WARP.
void* PaintSharedD3dDevice(PaintApp* pa);
// IDXGIFactory2*, the one the shared device's adapter belongs to.
void* PaintSharedDxgiFactory(PaintApp* pa);
// Drop the D3D11/DXGI/D2D device group after device removal. DirectWrite and
// its device-independent text formats stay alive.
void PaintSharedD3dDeviceReset(PaintApp* pa);
// IDWriteFactory*.
void* PaintSharedDwrite(PaintApp* pa);
// The premultiplied BGRA WIC decoded, which is what RenderImageDecode keeps.
// False when the image never decoded.
bool PaintImagePixels(const RenderImage* img, const uint8_t** bgra, int* w,
                      int* h, int frameIndex);
// IDWriteTextLayout*. TextLayout is wrapped so its stable scene generation
// can live beside the native object.
void* PaintTextLayoutNative(TextLayout* tl);

// ─── the GPU backend ─────────────────────────────────────────────────────
//
// One name for one name with Paint.h, so paint_win.cpp's dispatch is a line
// per entry point and the two implementations never have to agree on
// anything else. Everything Paint.h declares that is *not* here is shared:
// PaintAppNew / Free, RenderImageDecode / Free / SizePx, and every TextLayout
// call but Draw.

namespace gpuw {

// Release the renderer-wide device resources after every target belonging to
// the PaintApp has been freed.
void PaintAppFree();
bool PaintTargetBegin(PaintCtx* ctx, void* native, int pxW, int pxH);
bool PaintTargetBeginOffscreen(PaintCtx* ctx, int pxW, int pxH);
bool PaintTargetEndOffscreen(PaintCtx* ctx, uint8_t* outBgra);
bool PaintTargetEnd(PaintCtx* ctx);
void PaintTargetFree(PaintCtx* ctx);

void CanvasClear(PaintCtx* ctx, Rgba c);
void CanvasFillRect(PaintCtx* ctx, float x, float y, float w, float h, Rgba c);
void CanvasFillRound(PaintCtx* ctx, float x, float y, float w, float h, float r,
                     Rgba c);
void CanvasStrokeRound(PaintCtx* ctx, float x, float y, float w, float h,
                       float r, float stroke, Rgba c, const float* dash);
void CanvasLine(PaintCtx* ctx, float x1, float y1, float x2, float y2,
                float stroke, Rgba c, const float* dash);
void CanvasEllipse(PaintCtx* ctx, float cx, float cy, float rx, float ry,
                   float stroke, Rgba c);
void CanvasPushClip(PaintCtx* ctx, float x, float y, float w, float h);
void CanvasPopClip(PaintCtx* ctx);

Path* PathNew(PaintCtx* ctx, bool winding);
void PathFree(Path* p);
void PathMoveTo(Path* p, float x, float y);
void PathLineTo(Path* p, float x, float y);
void PathCubicTo(Path* p, float x1, float y1, float x2, float y2, float x,
                 float y);
void PathArcTo(Path* p, float cx, float cy, float r, float a0, float a1,
               bool clockwise);
void PathClose(Path* p);
void PathFill(PaintCtx* ctx, Path* p, Rgba c, float dx, float dy);
void PathFillGradient(PaintCtx* ctx, Path* p, float x0, float y0, float x1,
                      float y1, Rgba from, Rgba to, float dx, float dy);
void PathStroke(PaintCtx* ctx, Path* p, float stroke, Rgba c, bool roundCaps,
                float dx, float dy);
void PathRealize(PaintCtx* ctx, Path* p);

void RenderImageDraw(PaintCtx* ctx, RenderImage* img, Bounds bounds,
                     Bounds imageBounds, int frameIndex, float radius,
                     bool grayscale);
// The final RenderImageRelease invalidates textures keyed by this identity.
void RenderImageFree(uint64_t imageGeneration);
int RenderImageCacheCountForTest(uint64_t imageGeneration);
void TextLayoutDraw(PaintCtx* ctx, TextLayout* tl, float x, float y, Rgba c,
                    bool clip, float clipW);

// What the last frame cost, for the benchmark harness: how many instances the
// frame put in the buffer, how many draw calls it took, and how many glyphs
// had to be rasterized into the atlas rather than found in it.
struct FrameStats {
    int instances = 0;
    int draws = 0;
    int glyphsRasterized = 0;
    int pathTriangles = 0;
};
const FrameStats& LastFrameStats();

} // namespace gpuw

// ─── what it is worth ────────────────────────────────────────────────────
//
// Measured with GPUI_FRAME_BENCH (see window_common.cpp), which times the
// three phases of Window::draw apart. Only the paint phase is below; the
// other two are the same code on all backends. Median paint-phase mean from
// three release runs of 600 frames, scene disabled, one machine — so the
// ratios are the answer, not the absolute numbers.
//
//     scene             Direct2D   D3D11      D3D12
//     showcase          0.200 ms   0.170 ms   0.159 ms
//     system_monitor    0.448 ms   0.203 ms   0.256 ms
//     story             1.625 ms   0.582 ms   0.744 ms
//
// The charts are where it wins biggest, because D2D re-tessellates path
// geometry on the CPU every frame and stencil-and-cover does not. Read the
// whole-frame number before getting excited. D3D12 is slightly ahead on the
// light scene, but its explicit state and command recording cost more over the
// story's many alternating quad/stencil passes; D3D11 remains the faster
// custom submission half there. Multisampling is close to free on the two
// heavier scenes and costs about 0.05 ms on the light one.
//
// Two things that came out of measuring it, in case either is ever true
// again: the showcase was *slower* than D2D until the per-frame stencil clear
// went (a full-surface D24S8 clear is pure bandwidth, and the cover pass
// already leaves the buffer at zero), and it batched the whole scene into a
// single draw call while doing it, so the cost was never the drawing.
//
// Text now keeps DirectWrite's RGB ClearType mask, applies its display gamma
// and enhanced contrast in the shader, and caches all three third-pixel
// phases. Against D2D, 1600x1026 story and 856x676 showcase captures have
// 0.88% and 0.30% of pixels over the comparator's 90 channel-sum tolerance;
// the remaining differences are raster/compositing noise rather than moved
// geometry. D3D11 and D3D12 consume the same atlas and shader inputs; their
// captures are byte-identical.
//
// `bun cmd/gpu-parity.ts` owns those comparisons. It also churns window sizes
// and uses __gpu_reset_every=N to repeatedly discard the swap chain, shaders,
// pipelines, image textures and glyph atlas through the same recovery routine
// used after DXGI_ERROR_DEVICE_REMOVED / RESET, then requires the recovered
// frame to be pixel-identical to a fresh one on both submission APIs.

} // namespace gpui

#endif // GPUI_OS_WINDOWS
#endif // GPUI_GPUI_PAINTGPU_H_
