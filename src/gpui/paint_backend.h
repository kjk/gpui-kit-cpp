#ifndef GPUI_GPUI_PAINT_BACKEND_H_
#define GPUI_GPUI_PAINT_BACKEND_H_
/* What the paint_*.cpp backends share behind Paint.h: the generation counter
   their resources are stamped with, the end-of-frame scene replay, and the
   bookkeeping of a RenderImage and a TextLayout, and the Paint.h entry points
   whose bodies do not depend on the drawing library.

   A mobile host adapter supplies Paint.h itself and may use the helpers or
   not; the Paint.h functions paint_backend.cpp defines are behind
   GPUI_PAINT_IN_TREE, so they never collide with its own. */

#include "gpui/paint.h"

// The native image types ImageFrame holds, named without their SDK headers
// (ID2D1Bitmap, cairo_surface_t, CGImageRef).
#if GPUI_OS_WINDOWS
struct ID2D1Bitmap;
#elif GPUI_OS_LINUX
struct _cairo_surface;
#elif GPUI_OS_MAC
struct CGImage;
#endif

namespace gpui {

// A process-unique, never-zero stamp for a paint resource (image, text
// layout, target), so a cache keyed on it cannot confuse a freed resource with
// a new one allocated at the same address.
uint64_t PaintResourceGenerationNew();

// Close the scene recording and draw what it collected: a pure scroll by
// shifting pixels when the backend can, otherwise the damaged area. Returns
// false when the frame was unchanged. `replayUnchanged` draws the whole scene
// anyway on an unchanged frame, for a target whose previous contents are not
// kept (an AppKit view may already have cleared it, and an identical frame
// left unpainted would flash).
bool PaintSceneFinish(PaintCtx* ctx, bool replayUnchanged);

// Copy h rows of w BGRA pixels between buffers whose rows are dstStride and
// srcStride bytes apart, leaving any row padding of the destination alone.
void PaintCopyBgraRows(uint8_t* dst, size_t dstStride, const uint8_t* src,
                       size_t srcStride, int w, int h);

// The grayscale copy of h rows of w premultiplied BGRA pixels an image draws
// with when asked for grayscale: Rec. 709 luma in 8.8 fixed point, alpha
// kept. dst may be src.
void PaintGrayBgraRows(uint8_t* dst, size_t dstStride, const uint8_t* src,
                       size_t srcStride, int w, int h);

// ─── what the in-tree backends define once ────────────────────────────────
//
// Paint.h entry points whose bodies do not depend on the drawing library are
// defined once, in paint_backend.cpp, over the shapes below. The macros say
// which backends share which: iOS and Android supply Paint.h through their
// host adapter and take none of it.

#if GPUI_OS_WINDOWS || GPUI_OS_LINUX || GPUI_OS_MAC || GPUI_OS_WASM
#define GPUI_PAINT_IN_TREE 1
#else
#define GPUI_PAINT_IN_TREE 0
#endif
// The backends that keep decoded frames (WIC, gdk-pixbuf, ImageIO). The
// browser keeps its picture itself.
#if GPUI_OS_WINDOWS || GPUI_OS_LINUX || GPUI_OS_MAC
#define GPUI_PAINT_IMAGE_FRAMES 1
#else
#define GPUI_PAINT_IMAGE_FRAMES 0
#endif
// The backends that draw through a 2D library one call at a time (cairo, Core
// Graphics, Canvas2D) rather than dispatching between Direct2D and the GPU
// renderer.
#if GPUI_OS_LINUX || GPUI_OS_MAC || GPUI_OS_WASM
#define GPUI_PAINT_DIRECT 1
#else
#define GPUI_PAINT_DIRECT 0
#endif

#if GPUI_PAINT_IN_TREE

// The first member of every in-tree backend's TextLayout, which must stay a
// standard-layout struct: the shared TextLayoutAddRef / Release / Generation /
// TextLen (and, on a direct backend, TextLayoutSize) reach it through a
// pointer cast, since the rest of the struct is the backend's own.
struct TextLayoutHead {
    // The byte length of the text it was shaped from (TextLayoutTextLen).
    int textLen = 0;
    uint64_t generation = 0;
    int refs = 1;
    // TextLayoutSize on a direct backend: how far the aligned lines reach,
    // kept so it can answer without measuring again.
    Size cover = {};
};

// The backend's half of the last TextLayoutRelease: free what it shaped
// with, then the layout.
void TextLayoutDestroy(TextLayout* tl);

// The backend's half of PaintInstalledFontNames: append every installed
// family's name (StrDup'd) to `out`. Asked once per process; the shared half
// keeps the answer.
void PaintListInstalledFonts(PaintApp* pa, Vec<Str>* out);

// One decoded frame. `w`, `h` and `durationMs` are what the shared accessors
// read; the rest is the backend's, declared without its SDK header.
#if GPUI_PAINT_IMAGE_FRAMES
struct ImageFrame {
    int w = 0;
    int h = 0;
    int durationMs = 100;
#if GPUI_OS_WINDOWS
    uint8_t* bgra = nullptr;
    ID2D1Bitmap* bmp = nullptr;
    ID2D1Bitmap* grayBmp = nullptr;
    // The render target `bmp` belongs to. D2D bitmaps are device resources
    // and do not survive it. Use its generation rather than its address: a
    // newly allocated target may occupy the same address as the old one.
    uint64_t bmpTargetGeneration = 0;
#elif GPUI_OS_LINUX
    _cairo_surface* surface = nullptr;
    _cairo_surface* graySurface = nullptr;
#elif GPUI_OS_MAC
    CGImage* image = nullptr;
    CGImage* grayImage = nullptr;
#endif
};
// The backend's: release what one frame holds.
void ImageFrameFree(ImageFrame* frame);
#endif

// Every in-tree backend's RenderImage: a refcount (Rust's Arc<RenderImage>),
// a generation, a load status, and the decoded frames or the browser's own
// picture.
struct RenderImage {
    int refs = 1;
    uint64_t generation = 0;
    RenderImageStatus status = RenderImageStatus::Ready;
#if GPUI_PAINT_IMAGE_FRAMES
    Vec<ImageFrame> frames;
#endif
#if GPUI_OS_WASM
    int js = 0;
#endif
};

// What goes with an image besides its frames when the last reference does:
// the browser's picture on wasm.
#if GPUI_OS_WASM
void RenderImageDropNative(RenderImage* img);
#else
inline void RenderImageDropNative(RenderImage*) {}
#endif

RenderImage* RenderImageAlloc(RenderImageStatus status);

#if GPUI_PAINT_IMAGE_FRAMES
// A frame index a caller may hand in, made one that exists: anything out of
// range is the first frame. -1 when the image has no frames.
int RenderImageFrameIndex(const RenderImage* img, int frameIndex);
// A Ready image holding one frame.
RenderImage* RenderImageWithFrame(const ImageFrame& frame);
void RenderImageFramesFree(RenderImage* img);
#endif

#if GPUI_PAINT_DIRECT
// The backend's half of TextLayoutNew, handed a context with a PaintApp,
// text that is not empty and a font size above zero. The shared half stamps
// the head's textLen and generation.
TextLayout* TextLayoutShape(PaintCtx* ctx, Str s, float fontSize, float maxW,
                            bool wrap, uint16_t weight, float lineH,
                            Size* outSize, TextAlign align);
// The first member of a direct backend's Path, which stays standard-layout
// for the same reason TextLayout does: the shared PathNew / MoveTo / LineTo /
// CubicTo / ArcTo / Close keep the fill rule and the open-figure rule here
// and hand each segment to the backend's PathEmit* below.
struct PathHead {
    // The nonzero fill rule; false is even-odd.
    bool winding = true;
    // A figure is open: a line or curve with none starts one at its end
    // point instead, and a close with none does nothing.
    bool fig = false;
};
// The backend's halves of PathNew and PathFree, past the scene's recording.
Path* PathAlloc();
void PathDestroy(Path* p);
void PathEmitMove(Path* p, float x, float y);
void PathEmitLine(Path* p, float x, float y);
void PathEmitCubic(Path* p, float x1, float y1, float x2, float y2, float x,
                   float y);
void PathEmitArc(Path* p, float cx, float cy, float r, float a0, float a1,
                 bool clockwise);
void PathEmitClose(Path* p);
// The backend's half of PaintTargetFree: release and delete the target.
void PaintTargetDestroy(PaintTarget* rt);
// The start of a PaintTargetBegin handed a native surface: false without a
// PaintApp or a surface, otherwise the previous frame's target is freed.
bool PaintTargetReplace(PaintCtx* ctx, void* native);
#endif

// Each backend's half of the Paint.h drawing calls. paint_backend.cpp defines
// the public ones, which hand a call to the scene while it records and to
// these otherwise.
void PaintBackendCanvasClear(PaintCtx* ctx, Rgba c);
void PaintBackendCanvasFillRect(PaintCtx* ctx, float x, float y, float w,
                                float h, Rgba c);
void PaintBackendCanvasFillRound(PaintCtx* ctx, float x, float y, float w,
                                 float h, float r, Rgba c);
void PaintBackendCanvasStrokeRound(PaintCtx* ctx, float x, float y, float w,
                                   float h, float r, float stroke, Rgba c,
                                   const float* dash);
void PaintBackendCanvasLine(PaintCtx* ctx, float x1, float y1, float x2,
                            float y2, float stroke, Rgba c, const float* dash);
void PaintBackendCanvasEllipse(PaintCtx* ctx, float cx, float cy, float rx,
                               float ry, float stroke, Rgba c);
void PaintBackendCanvasPushClip(PaintCtx* ctx, float x, float y, float w,
                                float h);
void PaintBackendCanvasPopClip(PaintCtx* ctx);
void PaintBackendPathFill(PaintCtx* ctx, Path* p, Rgba c, float dx, float dy);
void PaintBackendPathFillGradient(PaintCtx* ctx, Path* p, float x0, float y0,
                                  float x1, float y1, Rgba from, Rgba to,
                                  float dx, float dy);
void PaintBackendPathStroke(PaintCtx* ctx, Path* p, float stroke, Rgba c,
                            bool roundCaps, float dx, float dy);
void PaintBackendPathStrokeGradient(PaintCtx* ctx, Path* p, float stroke,
                                    float x0, float y0, float x1, float y1,
                                    Rgba from, Rgba to, bool roundCaps,
                                    float dx, float dy);
void PaintBackendRenderImageDraw(PaintCtx* ctx, RenderImage* img, Bounds bounds,
                                 Bounds imageBounds, int frameIndex,
                                 float radius, bool grayscale);
void PaintBackendTextLayoutDraw(PaintCtx* ctx, TextLayout* tl, float x, float y,
                                Rgba c, bool clip, float clipW);
Path* PaintBackendPathNew(PaintCtx* ctx, bool winding);
void PaintBackendPathFree(Path* p);
void PaintBackendPathMoveTo(Path* p, float x, float y);
void PaintBackendPathLineTo(Path* p, float x, float y);
void PaintBackendPathCubicTo(Path* p, float x1, float y1, float x2, float y2,
                             float x, float y);
void PaintBackendPathArcTo(Path* p, float cx, float cy, float r, float a0,
                           float a1, bool clockwise);
void PaintBackendPathClose(Path* p);
// Whether this backend can colour `n` spans of `tl` at all, and its drawing
// of them; false from either sends the caller to its fallback.
bool PaintBackendTextLayoutSpansAccepted(const TextLayout* tl, int n);
bool PaintBackendTextLayoutSpans(PaintCtx* ctx, TextLayout* tl, Str text,
                                 float x, float y, Rgba base,
                                 const TextSpan* spans, int n);

#endif // GPUI_PAINT_IN_TREE

} // namespace gpui
#endif // GPUI_GPUI_PAINT_BACKEND_H_
