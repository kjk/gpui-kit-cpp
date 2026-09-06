#ifndef GPUI_GPUI_PAINT_H_
#define GPUI_GPUI_PAINT_H_
/* The 2D drawing surface, one signature per operation the element tree needs.
   Direct2D + DirectWrite behind Paint_win.cpp, cairo + Pango behind
   Paint_linux.cpp. Everything above this line is portable.

   Coordinates are DIPs with y growing down, matching the element tree. */

#include "gpui/gpui.h"

#include <math.h>

// Windows renderer selection is a compile-time choice. Defining
// WIN_BACKEND_ALL preserves the runtime __paint=d2d|d3d11|d3d12 selector;
// otherwise exactly one backend is compiled and __paint is ignored. With
// no definition, Direct2D is the compatibility default: it is available back
// to Windows 7 with the platform update, falls back through WARP, and keeps
// DirectWrite's ClearType text and mature driver path.
#if GPUI_OS_WINDOWS
#ifndef WIN_BACKEND_ALL
#define WIN_BACKEND_ALL 0
#endif
#if WIN_BACKEND_ALL
#undef WIN_BACKEND_DIRECT2D
#undef WIN_BACKEND_D3D11
#undef WIN_BACKEND_D3D12
#define WIN_BACKEND_DIRECT2D 1
#define WIN_BACKEND_D3D11 1
#define WIN_BACKEND_D3D12 1
#else
#if !defined(WIN_BACKEND_DIRECT2D) && !defined(WIN_BACKEND_D3D11) && \
    !defined(WIN_BACKEND_D3D12)
#define WIN_BACKEND_DIRECT2D 1
#endif
#ifndef WIN_BACKEND_DIRECT2D
#define WIN_BACKEND_DIRECT2D 0
#endif
#ifndef WIN_BACKEND_D3D11
#define WIN_BACKEND_D3D11 0
#endif
#ifndef WIN_BACKEND_D3D12
#define WIN_BACKEND_D3D12 0
#endif
#if WIN_BACKEND_DIRECT2D + WIN_BACKEND_D3D11 + WIN_BACKEND_D3D12 != 1
#error Define exactly one WIN_BACKEND_DIRECT2D, WIN_BACKEND_D3D11 or WIN_BACKEND_D3D12, or define WIN_BACKEND_ALL
#endif
#endif
#else
#undef WIN_BACKEND_ALL
#undef WIN_BACKEND_DIRECT2D
#undef WIN_BACKEND_D3D11
#undef WIN_BACKEND_D3D12
#define WIN_BACKEND_ALL 0
#define WIN_BACKEND_DIRECT2D 0
#define WIN_BACKEND_D3D11 0
#define WIN_BACKEND_D3D12 0
#endif

#define WIN_BACKEND_GPU (WIN_BACKEND_D3D11 || WIN_BACKEND_D3D12)

namespace gpui {

#if GPUI_OS_WINDOWS
enum class WinPaintBackend : uint8_t {
    Direct2D,
    D3D11,
    D3D12,
};

// __msaa=1|2|4|8 controls only the custom D3D11/D3D12 renderer; Direct2D
// uses its own antialiasing and ignores it. Quads, rounded rectangles and
// glyphs have analytic shader coverage, so changing this mostly affects
// tessellated paths and expanded strokes. X1 is cheapest but leaves those
// edges jagged. Each higher value smooths them with proportionally more
// multisample storage, raster work and resolve bandwidth; X4 is the quality /
// cost default, while X8 is primarily useful for comparison or path-heavy UI.
enum class WinPaintMsaa : uint8_t {
    X1 = 1,
    X2 = 2,
    X4 = 4,
    X8 = 8,
};

// __scene=off|replay|cache|skip|damage controls the scene collected in front
// of either Windows renderer. Levels are cumulative:
//
//   Off      draws immediately. It uses no scene recording and is the best
//            diagnostic when scene replay might be producing a stale frame.
//   Replay   records, sorts and replays every primitive, paying that overhead
//            without caching or suppressing any drawing.
//   Cache    also retains realized path geometry by hash, trading cache memory
//            for avoiding repeated path construction/tessellation.
//   Skip     also hashes whole frames and omits drawing and presenting an
//            unchanged frame. This is the measured, safe default.
//   Damage   also redraws only the union of changed primitive bounds. It can
//            save work on localized changes, but is the most experimental
//            mode and carries prior damage across the triple-buffered chain.
enum class WinSceneMode : uint8_t {
    Off,
    Replay,
    Cache,
    Skip,
    Damage,
};

struct WinPaintOptions {
    WinPaintBackend backend = WinPaintBackend::Direct2D;
    WinPaintMsaa msaa = WinPaintMsaa::X4;
    WinSceneMode scene = WinSceneMode::Skip;
    // Test seam: after this many presented custom-GPU frames, tear down the
    // same device-local state a DXGI device-removal result invalidates. Zero
    // is inert. Kept in the typed options so the stress runner reaches the
    // production recovery path without a second renderer control channel.
    int gpuResetEvery = 0;
};

// Process-start renderer options. WinPaintOptionsTakeArg consumes the
// reserved __paint=, __msaa=, __scene= and __gpu_reset_every= arguments
// before GpuiMain sees argv; invalid values are still consumed and leave the
// current/default choice unchanged.
const WinPaintOptions& WinPaintOptionsGet();
bool WinPaintOptionsTakeArg(Str arg);
#endif

// Text weight byte: the weight in the low bits plus family / decoration
// flags, so the shaped-text cache keys mono and proportional runs apart on
// its own. Both backends decode the same byte.
enum : uint8_t {
    kFontWeightMask = 15,
    kFontWeightNormal = 0,
    kFontWeightThin = 1,
    kFontWeightExtraLight = 2,
    kFontWeightLight = 3,
    kFontWeightExplicitNormal = 4,
    kFontWeightMedium = 5,
    kFontWeightSemibold = 6,
    kFontWeightBold = 7,
    kFontWeightExtraBold = 8,
    kFontWeightBlack = 9,
    kFontMono = 16,
    kFontUnderline = 32,
    kFontItalic = 64,
    // text_decoration_line_through(): what a markdown `~~del~~` run and an
    // HTML <s> / <del> paint with. DirectWrite and Pango draw it themselves;
    // Core Text has no strikethrough attribute, so paint_mac draws the rule.
    kFontStrike = 128
};

// GPUI lays every line of text into a box phi times the font size — the
// default TextStyle::line_height (gpui::phi(), geometry.rs) — and centers the
// glyphs in it. Both text engines are tighter than that on their own, so
// without it every text block, and every row that shrink-wraps one, comes out
// shorter than the original.
const float kLineHeight = 1.618034f;

// ─── backend lifecycle ────────────────────────────────────────────────────

// Create the factories and the shared font set. Null on failure.
PaintApp* PaintAppNew();
void PaintAppFree(PaintApp* pa);

// Bind `native` — the HWND on Windows, the cairo surface on Linux — as this
// frame's target and open a drawing batch. False means skip the frame.
//
// Windows takes the window, not a device context, because the frame goes to
// the window's own swap chain: D2D has no GPU path to an HDC, and the DC
// render target this used to be spent most of the frame copying its surface
// back through the GDI interop.
bool PaintTargetBegin(PaintCtx* ctx, void* native, int pxW, int pxH);
// An offscreen square of pixels rather than a window: transparent to start
// with, and read back as premultiplied BGRA, top-down, by
// PaintTargetEndOffscreen. What a menu icon is rasterized through — the OS
// wants a bitmap of one, not an element that draws it.
bool PaintTargetBeginOffscreen(PaintCtx* ctx, int pxW, int pxH);
// `outBgra` takes pxW * pxH * 4 bytes.
bool PaintTargetEndOffscreen(PaintCtx* ctx, uint8_t* outBgra);
// Close the batch. Returns false if the device was lost and the target was
// dropped; the next frame recreates it.
bool PaintTargetEnd(PaintCtx* ctx);
// Drop the cached target: a DPI change, a resize, or a lost device.
void PaintTargetFree(PaintCtx* ctx);

// The colour to actually paint: what the caller asked for, faded by the
// opacity in force. GPUI multiplies every primitive's colour by
// `element_opacity()` the same way, at the same moment — as the primitive is
// handed to the backend, not when the style was built.
inline Rgba PaintFade(const PaintCtx* ctx, Rgba c) {
    if (!ctx || ctx->opacity >= 1.f) {
        return c;
    }
    float a = (float)c.a * (ctx->opacity < 0 ? 0 : ctx->opacity);
    c.a = (uint8_t)(a <= 0 ? 0 : (a >= 255 ? 255 : lroundf(a)));
    return c;
}

// ─── canvas ───────────────────────────────────────────────────────────────

void CanvasClear(PaintCtx* ctx, Rgba c);
void CanvasFillRect(PaintCtx* ctx, float x, float y, float w, float h, Rgba c);
void CanvasFillRound(PaintCtx* ctx, float x, float y, float w, float h, float r,
                     Rgba c);
inline float ClampRadius(float r, float w, float h) {
    float lim = (w < h ? w : h) * 0.5f;
    if (lim < 0) lim = 0;
    return r > lim ? lim : r;
}
inline void FillRound(PaintCtx* ctx, float x, float y, float w, float h,
                      float r, Rgba c) {
    CanvasFillRound(ctx, x, y, w, h, ClampRadius(r, w, h), c);
}
// `dash` is a two-element {on, off} pattern in stroke widths, or null for a
// solid line.
void CanvasStrokeRound(PaintCtx* ctx, float x, float y, float w, float h,
                       float r, float stroke, Rgba c,
                       const float* dash = nullptr);
inline void DrawRoundStroke(PaintCtx* ctx, float x, float y, float w, float h,
                            float r, float stroke, Rgba c) {
    CanvasStrokeRound(ctx, x, y, w, h, ClampRadius(r, w, h), stroke, c);
}
void CanvasLine(PaintCtx* ctx, float x1, float y1, float x2, float y2,
                float stroke, Rgba c, const float* dash = nullptr);
// stroke <= 0 fills the ellipse instead of stroking it.
void CanvasEllipse(PaintCtx* ctx, float cx, float cy, float rx, float ry,
                   float stroke, Rgba c);
void CanvasPushClip(PaintCtx* ctx, float x, float y, float w, float h);
void CanvasPopClip(PaintCtx* ctx);

// ─── paths ────────────────────────────────────────────────────────────────
//
// Build, draw, free. Ordinary callers rebuild each frame; the scene may keep
// the backend geometry and draw translated copies across frames.

struct Path;

// `winding` picks the nonzero fill rule; false is even-odd.
Path* PathNew(PaintCtx* ctx, bool winding);
void PathFree(Path* p);
void PathMoveTo(Path* p, float x, float y);
void PathLineTo(Path* p, float x, float y);
void PathCubicTo(Path* p, float x1, float y1, float x2, float y2, float x,
                 float y);
// An arc of the circle at (cx, cy), from a0 to a1 radians measured clockwise
// from +x in this y-down space. Starts a figure if none is open.
void PathArcTo(Path* p, float cx, float cy, float r, float a0, float a1,
               bool clockwise);
void PathClose(Path* p);

// `dx`/`dy` place a retained path that was built at the origin. Ordinary
// callers leave them zero; the scene cache uses them to share one backend
// geometry between translated copies of the same shape.
void PathFill(PaintCtx* ctx, Path* p, Rgba c, float dx = 0, float dy = 0);
// A vertical linear gradient from `top` at y0 to `bot` at y1.
// A linear gradient between two points, which is what a sankey ribbon wants:
// its two ends are side by side, not one above the other.
void PathFillGradient(PaintCtx* ctx, Path* p, float x0, float y0, float x1,
                      float y1, Rgba from, Rgba to, float dx = 0, float dy = 0);
void PathFillGradientV(PaintCtx* ctx, Path* p, float y0, float y1, Rgba top,
                       Rgba bot);
void PathStroke(PaintCtx* ctx, Path* p, float stroke, Rgba c,
                bool roundCaps = false, float dx = 0, float dy = 0);
// Say that `p` is about to be drawn more than once, so a backend that can
// pay a tessellation forward does it now: D2D builds a geometry realization,
// which is the one thing that makes a path cheap to fill twice. A backend
// with nothing to cache leaves this empty, and nothing has to call it — it is
// what src/gpui/scene.cpp calls when it puts a path in its cache.
void PathRealize(PaintCtx* ctx, Path* p);

// ─── images ───────────────────────────────────────────────────────────────
//
// A decoded bitmap. GPUI hands an `img(..)` element's source to its asset
// system, which decodes with the `image` crate; there is no such crate here
// and no room for one, so the decode is the platform's own: WIC on Windows,
// NSBitmapImageRep on macOS, and cairo's PNG loader on Linux — which is why
// Linux reads PNG and nothing else. gpui/image.h caches what comes back and
// is what the element tree talks to.

struct RenderImage;

// Hosted decoders return only after decoding, while the browser must return a
// handle before its Image element finishes. Keep that platform difference
// explicit so the image cache can show fallback content until the pixels are
// ready and remember a terminal decode failure.
enum class RenderImageStatus : uint8_t {
    Loading,
    Ready,
    Failed,
};

// Decode `bytes`. Null when the format is not one this platform reads, which
// the caller shows as the image's alt text.
RenderImage* RenderImageDecode(PaintApp* pa, const uint8_t* bytes, int len);
// Decode returns one owning reference. Retain/Release are main-thread only,
// the explicit counterpart of Rust's Arc<RenderImage>. GPU storage is separate.
void RenderImageRetain(RenderImage* img);
void RenderImageRelease(RenderImage* img);
// Monotonic identity assigned when the resource is made. Unlike its address,
// this is never reused while the process runs, so retained-frame hashes do
// not confuse a new allocation with the object that previously occupied it.
uint64_t RenderImageGeneration(const RenderImage* img);
RenderImageStatus RenderImageStatusGet(const RenderImage* img);
// The image's own size in pixels.
Size RenderImageSizePx(const RenderImage* img, int frameIndex = 0);
int RenderImageFrameCount(const RenderImage* img);
int RenderImageFrameDurationMs(const RenderImage* img, int frameIndex);
// Draw it scaled into `b`. The caller has already picked the box, so this is
// a straight stretch — object_fit is decided above.
// `radius` rounds the corners the picture is drawn into, which is what an
// avatar is: `AvatarImage::new(src).size_full().rounded_full()`. Zero draws
// the plain rectangle. It is a parameter rather than a clip because a clip
// here is axis-aligned only, and the four backends all have a cheap way to
// fill a rounded rect with a picture.
// `bounds` is the element's content mask; `imageBounds` is ObjectFit's
// placement of the intrinsic image inside it.
void RenderImageDraw(PaintCtx* ctx, RenderImage* img, Bounds bounds,
                     Bounds imageBounds, int frameIndex, float radius = 0,
                     bool grayscale = false);
inline void RenderImageDraw(PaintCtx* ctx, RenderImage* img, Bounds bounds,
                            float radius = 0) {
    RenderImageDraw(ctx, img, bounds, bounds, 0, radius, false);
}

// ─── shaped text ──────────────────────────────────────────────────────────
//
// A TextLayout is one shaped run, refcounted so the measurement cache in
// Gpui.cpp can hold on to it across frames.

struct TextLayout;

// Shape `s` and report its size. maxW <= 0 is unconstrained. Null if the text
// is empty or shaping failed.
TextLayout* TextLayoutNew(PaintCtx* ctx, Str s, float fontSize, float maxW,
                          bool wrap, uint8_t weight, float lineH,
                          Size* outSize);
// What TextLayoutNew reported as `outSize`, asked for again: the size the
// shaped run occupies, which is what a caller holding only the layout needs
// to know what area drawing it covers.
Size TextLayoutSize(TextLayout* tl);
void TextLayoutAddRef(TextLayout* tl);
void TextLayoutRelease(TextLayout* tl);
uint64_t TextLayoutGeneration(const TextLayout* tl);
// `clip` cuts the run at `clipW` — GPUI's `truncate()`, which is
// `text_overflow: Ellipsis`, so what the backend draws is the run trimmed with
// an ellipsis rather than cut through a glyph. A non-wrapping run is shaped
// unconstrained (see TextMeasLayout), so the width has to come with the draw:
// the layout does not know the box it is going into. `clipW` of 0 leaves it
// to the caller's own clip.
void TextLayoutDraw(PaintCtx* ctx, TextLayout* tl, float x, float y, Rgba c,
                    bool clip, float clipW = 0);
// Draw foreground colors in one pass where the backend supports it. Ranges
// are sorted, non-overlapping UTF-8 byte offsets into the shaped text.
// Backgrounds and decorations are painted by the caller. False means the
// caller must use its range-clip fallback; nothing has been drawn.
bool PaintTextLayoutSpans(PaintCtx* ctx, TextLayout* tl, Str text, float x,
                          float y, Rgba base, const TextSpan* spans, int n);
// Uses the backend's single pass or the portable range-clip fallback.
void TextLayoutDrawSpans(PaintCtx* ctx, TextLayout* tl, Str text, float x,
                         float y, Rgba base, const TextSpan* spans, int n);
void DrawTextAt(PaintCtx* ctx, Str s, float x, float y, float w, float h,
                float fontSize, Rgba c, bool truncate, bool wrap = false,
                float measMaxW = -1.f, int weight = 0, float lineH = 0);
// The UTF-8 offset into `s` nearest the layout-relative point.
int TextLayoutHitPoint(TextLayout* tl, Str s, float relX, float relY);
// The rectangles covering UTF-8 range [u8a, u8b), one per line. Returns how
// many were written.
int TextLayoutRangeRects(TextLayout* tl, Str s, int u8a, int u8b, Bounds* out,
                         int max);
// Where the baseline sits inside a line box, measured from the line's top.
// The first line's, which is the one a decoration under a run needs: an
// input method composes one line at a time.
float TextLayoutBaseline(TextLayout* tl);

} // namespace gpui
#endif // GPUI_GPUI_PAINT_H_
