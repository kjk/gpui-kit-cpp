/* Core Graphics + Core Text backend for Paint.h. Objective-C++: compiled with
   -x objective-c++, which is how the mac half of the amalgam is built.

   The view is flipped (GpuiView.isFlipped), so the context handed to a frame
   is already y-down with the origin top-left, matching the element tree. Only
   glyphs need the text matrix flipped back. */

#include "gpui/paint.h"

#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>

#include <math.h>

namespace gpui {

static uint64_t gNextPaintResourceGeneration = 1;

static uint64_t PaintResourceGenerationNew() {
    uint64_t id = gNextPaintResourceGeneration++;
    if (id == 0) {
        id = gNextPaintResourceGeneration++;
    }
    return id;
}

// A resolved font, keyed by the size and weight byte the element tree asks
// for. Small and linear: a frame uses a handful of distinct fonts.
struct FontSlot {
    float size = 0;
    uint8_t weight = 0;
    CTFontRef font = nullptr;
};

enum {
    kFontCacheCap = 32
};

struct PaintApp {
    FontSlot fonts[kFontCacheCap] = {};
    int nFonts = 0;
};

struct PaintTarget {
    CGContextRef cg = nullptr;
};

// ─── lifecycle ────────────────────────────────────────────────────────────

PaintApp* PaintAppNew() {
    return new PaintApp();
}

void PaintAppFree(PaintApp* pa) {
    if (!pa) {
        return;
    }
    for (int i = 0; i < pa->nFonts; i++) {
        if (pa->fonts[i].font) {
            CFRelease(pa->fonts[i].font);
        }
    }
    delete pa;
}

void PaintTargetFree(PaintCtx* ctx) {
    if (!ctx || !ctx->rt) {
        return;
    }
    delete ctx->rt;
    ctx->rt = nullptr;
}

// `native` is the CGContextRef the view's drawRect handed us. It is only
// valid for this frame, so the target is rebuilt every time.
bool PaintTargetBegin(PaintCtx* ctx, void* native, int pxW, int pxH) {
    (void)pxW;
    (void)pxH;
    if (!ctx || !ctx->pa || !native) {
        return false;
    }
    PaintTargetFree(ctx);
    auto* t = new PaintTarget();
    t->cg = (CGContextRef)native;
    ctx->rt = t;
    CGContextSetShouldAntialias(t->cg, true);
    // Upright glyphs in a y-down space.
    CGContextSetTextMatrix(t->cg, CGAffineTransformMakeScale(1, -1));
    return true;
}

// The offscreen target: a bitmap context rather than the window's, kept here
// until the pixels are read back out of it.
static CGContextRef gOffscreenCg = nullptr;
static int gOffscreenW = 0;
static int gOffscreenH = 0;

bool PaintTargetBeginOffscreen(PaintCtx* ctx, int pxW, int pxH) {
    if (!ctx || !ctx->pa || pxW <= 0 || pxH <= 0) {
        return false;
    }
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGBitmapInfo bitmapInfo =
        (CGBitmapInfo)((uint32_t)kCGImageAlphaPremultipliedFirst |
                       (uint32_t)kCGBitmapByteOrder32Little);
    CGContextRef cg =
        CGBitmapContextCreate(nullptr, (size_t)pxW, (size_t)pxH, 8,
                              (size_t)pxW * 4, space, bitmapInfo);
    CGColorSpaceRelease(space);
    if (!cg) {
        return false;
    }
    // The window's view is flipped, so an offscreen target is flipped too and
    // everything drawn into it lands the same way up.
    CGContextTranslateCTM(cg, 0, (CGFloat)pxH);
    CGContextScaleCTM(cg, 1, -1);
    if (!PaintTargetBegin(ctx, cg, pxW, pxH)) {
        CGContextRelease(cg);
        return false;
    }
    gOffscreenCg = cg;
    gOffscreenW = pxW;
    gOffscreenH = pxH;
    return true;
}

bool PaintTargetEndOffscreen(PaintCtx* ctx, uint8_t* outBgra) {
    if (!ctx || !gOffscreenCg) {
        return false;
    }
    if (outBgra) {
        const uint8_t* src =
            (const uint8_t*)CGBitmapContextGetData(gOffscreenCg);
        size_t stride = CGBitmapContextGetBytesPerRow(gOffscreenCg);
        if (src) {
            for (int y = 0; y < gOffscreenH; y++) {
                memcpy(outBgra + (size_t)y * (size_t)gOffscreenW * 4,
                       src + (size_t)y * stride, (size_t)gOffscreenW * 4);
            }
        }
    }
    PaintTargetFree(ctx);
    CGContextRelease(gOffscreenCg);
    gOffscreenCg = nullptr;
    gOffscreenW = 0;
    gOffscreenH = 0;
    return true;
}

bool PaintTargetEnd(PaintCtx* ctx) {
    if (!ctx || !ctx->rt) {
        return false;
    }
    PaintTargetFree(ctx);
    return true;
}

// ─── canvas ───────────────────────────────────────────────────────────────

static CGContextRef Cg(PaintCtx* ctx) {
    return (ctx && ctx->rt) ? ctx->rt->cg : nullptr;
}

// element_opacity: both colour seams fade what they are given by the opacity
// in force, which is where GPUI applies it too.
static void SetFill(PaintCtx* ctx, CGContextRef cg, Rgba c) {
    c = PaintFade(ctx, c);
    CGContextSetRGBFillColor(cg, c.r / 255.0, c.g / 255.0, c.b / 255.0,
                             c.a / 255.0);
}

static void SetStroke(PaintCtx* ctx, CGContextRef cg, Rgba c) {
    c = PaintFade(ctx, c);
    CGContextSetRGBStrokeColor(cg, c.r / 255.0, c.g / 255.0, c.b / 255.0,
                               c.a / 255.0);
}

// D2D measures a dash pattern in stroke widths; Core Graphics in user units.
static void SetDash(CGContextRef cg, const float* dash, float stroke) {
    if (!dash) {
        CGContextSetLineDash(cg, 0, nullptr, 0);
        return;
    }
    CGFloat d[2] = {dash[0] * stroke, dash[1] * stroke};
    CGContextSetLineDash(cg, 0, d, 2);
}

static CGPathRef RoundRectPath(float x, float y, float w, float h, float r) {
    CGRect rect = CGRectMake(x, y, w, h);
    float rmax = (w < h ? w : h) * 0.5f;
    if (r > rmax) {
        r = rmax;
    }
    if (r <= 0) {
        return CGPathCreateWithRect(rect, nullptr);
    }
    return CGPathCreateWithRoundedRect(rect, r, r, nullptr);
}

void CanvasClear(PaintCtx* ctx, Rgba c) {
    CGContextRef cg = Cg(ctx);
    if (!cg) {
        return;
    }
    SetFill(ctx, cg, c);
    CGContextFillRect(cg, CGContextGetClipBoundingBox(cg));
}

void CanvasFillRect(PaintCtx* ctx, float x, float y, float w, float h, Rgba c) {
    CGContextRef cg = Cg(ctx);
    if (!cg || w <= 0 || h <= 0 || c.a == 0) {
        return;
    }
    SetFill(ctx, cg, c);
    CGContextFillRect(cg, CGRectMake(x, y, w, h));
}

void CanvasFillRound(PaintCtx* ctx, float x, float y, float w, float h, float r,
                     Rgba c) {
    CGContextRef cg = Cg(ctx);
    if (!cg || w <= 0 || h <= 0 || c.a == 0) {
        return;
    }
    CGPathRef path = RoundRectPath(x, y, w, h, r);
    SetFill(ctx, cg, c);
    CGContextAddPath(cg, path);
    CGContextFillPath(cg);
    CGPathRelease(path);
}

void CanvasStrokeRound(PaintCtx* ctx, float x, float y, float w, float h,
                       float r, float stroke, Rgba c, const float* dash) {
    CGContextRef cg = Cg(ctx);
    if (!cg || stroke <= 0 || w <= 0 || h <= 0) {
        return;
    }
    // Inset by half the stroke: Core Graphics, like D2D, centers it.
    CGPathRef path = RoundRectPath(x + stroke * 0.5f, y + stroke * 0.5f,
                                   w - stroke, h - stroke, r);
    SetStroke(ctx, cg, c);
    CGContextSetLineWidth(cg, stroke);
    SetDash(cg, dash, stroke);
    CGContextAddPath(cg, path);
    CGContextStrokePath(cg);
    CGContextSetLineDash(cg, 0, nullptr, 0);
    CGPathRelease(path);
}

void CanvasLine(PaintCtx* ctx, float x1, float y1, float x2, float y2,
                float stroke, Rgba c, const float* dash) {
    CGContextRef cg = Cg(ctx);
    if (!cg) {
        return;
    }
    SetStroke(ctx, cg, c);
    CGContextSetLineWidth(cg, stroke);
    SetDash(cg, dash, stroke);
    CGContextBeginPath(cg);
    CGContextMoveToPoint(cg, x1, y1);
    CGContextAddLineToPoint(cg, x2, y2);
    CGContextStrokePath(cg);
    CGContextSetLineDash(cg, 0, nullptr, 0);
}

void CanvasEllipse(PaintCtx* ctx, float cx, float cy, float rx, float ry,
                   float stroke, Rgba c) {
    CGContextRef cg = Cg(ctx);
    if (!cg || rx <= 0 || ry <= 0) {
        return;
    }
    CGRect box = CGRectMake(cx - rx, cy - ry, rx * 2, ry * 2);
    CGContextBeginPath(cg);
    CGContextAddEllipseInRect(cg, box);
    if (stroke > 0) {
        SetStroke(ctx, cg, c);
        CGContextSetLineWidth(cg, stroke);
        CGContextStrokePath(cg);
    } else {
        SetFill(ctx, cg, c);
        CGContextFillPath(cg);
    }
}

void CanvasPushClip(PaintCtx* ctx, float x, float y, float w, float h) {
    CGContextRef cg = Cg(ctx);
    if (!cg) {
        return;
    }
    CGContextSaveGState(cg);
    CGContextClipToRect(cg, CGRectMake(x, y, w, h));
}

void CanvasPopClip(PaintCtx* ctx) {
    CGContextRef cg = Cg(ctx);
    if (cg) {
        CGContextRestoreGState(cg);
    }
}

// ─── paths ────────────────────────────────────────────────────────────────

struct Path {
    CGMutablePathRef path = nullptr;
    bool winding = true;
    bool fig = false;
    float cx = 0, cy = 0; // current point, for the arc-without-a-figure case
};

Path* PathNew(PaintCtx* ctx, bool winding) {
    if (!ctx) {
        return nullptr;
    }
    auto* p = new Path();
    p->path = CGPathCreateMutable();
    p->winding = winding;
    return p;
}

void PathFree(Path* p) {
    if (!p) {
        return;
    }
    if (p->path) {
        CGPathRelease(p->path);
    }
    delete p;
}

void PathMoveTo(Path* p, float x, float y) {
    if (!p) {
        return;
    }
    CGPathMoveToPoint(p->path, nullptr, x, y);
    p->fig = true;
    p->cx = x;
    p->cy = y;
}

void PathLineTo(Path* p, float x, float y) {
    if (!p) {
        return;
    }
    if (!p->fig) {
        PathMoveTo(p, x, y);
        return;
    }
    CGPathAddLineToPoint(p->path, nullptr, x, y);
    p->cx = x;
    p->cy = y;
}

void PathCubicTo(Path* p, float x1, float y1, float x2, float y2, float x,
                 float y) {
    if (!p) {
        return;
    }
    if (!p->fig) {
        PathMoveTo(p, x, y);
        return;
    }
    CGPathAddCurveToPoint(p->path, nullptr, x1, y1, x2, y2, x, y);
    p->cx = x;
    p->cy = y;
}

void PathArcTo(Path* p, float cx, float cy, float r, float a0, float a1,
               bool clockwise) {
    if (!p) {
        return;
    }
    // CGPathAddArc sweeps toward increasing angle when its `clockwise` flag is
    // false. Our angles grow clockwise on screen because this space is y-down,
    // so the flag is inverted.
    CGPathAddArc(p->path, nullptr, cx, cy, r, a0, a1, !clockwise);
    p->fig = true;
    p->cx = cx + r * cosf(a1);
    p->cy = cy + r * sinf(a1);
}

void PathClose(Path* p) {
    if (!p || !p->fig) {
        return;
    }
    CGPathCloseSubpath(p->path);
    p->fig = false;
}

// Nothing to cache: this backend hands the path to Core Graphics, which owns
// whatever it wants to keep about it.
void PathRealize(PaintCtx* ctx, Path* p) {
    (void)ctx;
    (void)p;
}

void PathFill(PaintCtx* ctx, Path* p, Rgba c, float dx, float dy) {
    CGContextRef cg = Cg(ctx);
    if (!cg || !p || CGPathIsEmpty(p->path)) {
        return;
    }
    CGContextSaveGState(cg);
    CGContextTranslateCTM(cg, dx, dy);
    SetFill(ctx, cg, c);
    CGContextAddPath(cg, p->path);
    if (p->winding) {
        CGContextFillPath(cg);
    } else {
        CGContextEOFillPath(cg);
    }
    CGContextRestoreGState(cg);
}

void PathFillGradientV(PaintCtx* ctx, Path* p, float y0, float y1, Rgba top,
                       Rgba bot) {
    PathFillGradient(ctx, p, 0, y0, 0, y1, top, bot);
}

void PathFillGradient(PaintCtx* ctx, Path* p, float x0, float y0, float x1,
                      float y1, Rgba from, Rgba to, float dx, float dy) {
    CGContextRef cg = Cg(ctx);
    if (!cg || !p || CGPathIsEmpty(p->path)) {
        return;
    }
    from = PaintFade(ctx, from);
    to = PaintFade(ctx, to);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGFloat comps[8] = {from.r / 255.0, from.g / 255.0, from.b / 255.0,
                        from.a / 255.0, to.r / 255.0,   to.g / 255.0,
                        to.b / 255.0,   to.a / 255.0};
    CGFloat stops[2] = {0.0, 1.0};
    CGGradientRef grad =
        CGGradientCreateWithColorComponents(space, comps, stops, 2);
    CGColorSpaceRelease(space);
    if (!grad) {
        PathFill(ctx, p, from, dx, dy);
        return;
    }
    CGContextSaveGState(cg);
    CGContextTranslateCTM(cg, dx, dy);
    CGContextAddPath(cg, p->path);
    if (p->winding) {
        CGContextClip(cg);
    } else {
        CGContextEOClip(cg);
    }
    CGContextDrawLinearGradient(
        cg, grad, CGPointMake(x0, y0), CGPointMake(x1, y1),
        kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
    CGContextRestoreGState(cg);
    CGGradientRelease(grad);
}

void PathStroke(PaintCtx* ctx, Path* p, float stroke, Rgba c, bool roundCaps,
                float dx, float dy) {
    CGContextRef cg = Cg(ctx);
    if (!cg || !p || CGPathIsEmpty(p->path)) {
        return;
    }
    CGContextSaveGState(cg);
    CGContextTranslateCTM(cg, dx, dy);
    SetStroke(ctx, cg, c);
    CGContextSetLineWidth(cg, stroke);
    CGContextSetLineCap(cg, roundCaps ? kCGLineCapRound : kCGLineCapButt);
    CGContextSetLineJoin(cg, roundCaps ? kCGLineJoinRound : kCGLineJoinMiter);
    CGContextAddPath(cg, p->path);
    CGContextStrokePath(cg);
    CGContextSetLineCap(cg, kCGLineCapButt);
    CGContextSetLineJoin(cg, kCGLineJoinMiter);
    CGContextRestoreGState(cg);
}

// ─── utf-8 / utf-16 offsets ───────────────────────────────────────────────
//
// Str is UTF-8 and Core Text indexes UTF-16, so the two have to be mapped the
// way the DirectWrite backend does.

// Decode one codepoint; returns the bytes consumed.
static int Utf8Decode(const char* s, int len, uint32_t* out) {
    if (len <= 0) {
        return 0;
    }
    auto b = (const unsigned char*)s;
    if (b[0] < 0x80) {
        *out = b[0];
        return 1;
    }
    if ((b[0] & 0xe0) == 0xc0 && len >= 2) {
        *out = ((uint32_t)(b[0] & 0x1f) << 6) | (b[1] & 0x3f);
        return 2;
    }
    if ((b[0] & 0xf0) == 0xe0 && len >= 3) {
        *out = ((uint32_t)(b[0] & 0x0f) << 12) |
               ((uint32_t)(b[1] & 0x3f) << 6) | (b[2] & 0x3f);
        return 3;
    }
    if ((b[0] & 0xf8) == 0xf0 && len >= 4) {
        *out = ((uint32_t)(b[0] & 0x07) << 18) |
               ((uint32_t)(b[1] & 0x3f) << 12) |
               ((uint32_t)(b[2] & 0x3f) << 6) | (b[3] & 0x3f);
        return 4;
    }
    *out = 0xfffd;
    return 1;
}

static int Utf8OffToU16(Str s, int u8off) {
    if (!s.s || u8off <= 0) {
        return 0;
    }
    if (u8off > s.len) {
        u8off = s.len;
    }
    int i = 0;
    int u16 = 0;
    while (i < u8off) {
        uint32_t cp = 0;
        int adv = Utf8Decode(s.s + i, s.len - i, &cp);
        if (adv <= 0) {
            break;
        }
        i += adv;
        u16 += cp >= 0x10000 ? 2 : 1;
    }
    return u16;
}

static int U16OffToUtf8(Str s, int u16off) {
    if (!s.s || u16off <= 0) {
        return 0;
    }
    int i = 0;
    int u16 = 0;
    while (i < s.len && u16 < u16off) {
        uint32_t cp = 0;
        int adv = Utf8Decode(s.s + i, s.len - i, &cp);
        if (adv <= 0) {
            break;
        }
        i += adv;
        u16 += cp >= 0x10000 ? 2 : 1;
    }
    return i;
}

// ─── images ───────────────────────────────────────────────────────────────
//
// NSBitmapImageRep decodes everything the system has a codec for — PNG,
// JPEG, GIF, TIFF, HEIC — and hands out a CGImage, so this needs no
// framework the build does not already link. ImageIO would do the same job
// one layer down.

struct MacImageFrame {
    CGImageRef image = nullptr;
    CGImageRef grayImage = nullptr;
    int w = 0;
    int h = 0;
    int durationMs = 100;
};

struct RenderImage {
    int refs = 1;
    uint64_t generation = 0;
    Vec<MacImageFrame> frames;
};

RenderImage* RenderImageDecode(PaintApp* pa, const uint8_t* bytes, int len) {
    (void)pa;
    if (!bytes || len <= 0) {
        return nullptr;
    }
    NSData* data = [NSData dataWithBytes:bytes length:(NSUInteger)len];
    NSBitmapImageRep* rep = [NSBitmapImageRep imageRepWithData:data];
    if (!rep) {
        return nullptr;
    }
    auto* img = new RenderImage();
    img->generation = PaintResourceGenerationNew();
    NSInteger count = [[rep valueForProperty:NSImageFrameCount] integerValue];
    if (count < 1) {
        count = 1;
    }
    for (NSInteger i = 0; i < count; i++) {
        if (count > 1) {
            [rep setProperty:NSImageCurrentFrame
                   withValue:[NSNumber numberWithInteger:i]];
        }
        CGImageRef cg = [rep CGImage];
        if (!cg) {
            continue;
        }
        MacImageFrame frame = {};
        frame.image = CGImageRetain(cg);
        frame.w = (int)CGImageGetWidth(cg);
        frame.h = (int)CGImageGetHeight(cg);
        NSNumber* duration = [rep valueForProperty:NSImageCurrentFrameDuration];
        if (duration && [duration doubleValue] > 0) {
            frame.durationMs = (int)([duration doubleValue] * 1000.0);
        }
        VecAppend(img->frames, frame);
    }
    if (img->frames.len == 0) {
        delete img;
        return nullptr;
    }
    return img;
}

void RenderImageRetain(RenderImage* img) {
    if (img) {
        img->refs++;
    }
}

void RenderImageRelease(RenderImage* img) {
    if (!img || --img->refs != 0) {
        return;
    }
    for (int i = 0; i < img->frames.len; i++) {
        MacImageFrame& frame = img->frames[i];
        if (frame.image) {
            CGImageRelease(frame.image);
        }
        if (frame.grayImage) {
            CGImageRelease(frame.grayImage);
        }
    }
    VecReset(img->frames);
    delete img;
}

uint64_t RenderImageGeneration(const RenderImage* img) {
    return img ? img->generation : 0;
}

RenderImageStatus RenderImageStatusGet(const RenderImage* img) {
    return img ? RenderImageStatus::Ready : RenderImageStatus::Failed;
}

Size RenderImageSizePx(const RenderImage* img, int frameIndex) {
    if (!img || img->frames.len <= 0) {
        return {};
    }
    if (frameIndex < 0 || frameIndex >= img->frames.len) {
        frameIndex = 0;
    }
    return {(float)img->frames[frameIndex].w, (float)img->frames[frameIndex].h};
}

int RenderImageFrameCount(const RenderImage* img) {
    return img ? img->frames.len : 0;
}

int RenderImageFrameDurationMs(const RenderImage* img, int frameIndex) {
    if (!img || img->frames.len <= 0) {
        return 0;
    }
    if (frameIndex < 0 || frameIndex >= img->frames.len) {
        frameIndex = 0;
    }
    return img->frames[frameIndex].durationMs;
}

static CGImageRef ImageForDraw(MacImageFrame* frame, bool grayscale) {
    if (!grayscale || frame->grayImage) {
        return grayscale ? frame->grayImage : frame->image;
    }
    size_t bytesPerRow = (size_t)frame->w * 2;
    size_t bytes = bytesPerRow * (size_t)frame->h;
    uint8_t* pixels = AllocArray<uint8_t>(bytes);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceGray();
    CGContextRef cg =
        pixels && space
            ? CGBitmapContextCreate(pixels, (size_t)frame->w, (size_t)frame->h,
                                    8, bytesPerRow, space,
                                    kCGImageAlphaPremultipliedLast)
            : nullptr;
    if (cg) {
        CGContextDrawImage(cg, CGRectMake(0, 0, frame->w, frame->h),
                           frame->image);
        frame->grayImage = CGBitmapContextCreateImage(cg);
        CGContextRelease(cg);
    }
    if (space) {
        CGColorSpaceRelease(space);
    }
    Free(nullptr, pixels);
    return frame->grayImage;
}

void RenderImageDraw(PaintCtx* ctx, RenderImage* img, Bounds bounds,
                     Bounds imageBounds, int frameIndex, float radius,
                     bool grayscale) {
    CGContextRef cg = Cg(ctx);
    if (!cg || !img || img->frames.len <= 0 || bounds.w <= 0 || bounds.h <= 0 ||
        imageBounds.w <= 0 || imageBounds.h <= 0) {
        return;
    }
    if (frameIndex < 0 || frameIndex >= img->frames.len) {
        frameIndex = 0;
    }
    CGImageRef image = ImageForDraw(&img->frames[frameIndex], grayscale);
    if (!image) {
        return;
    }
    CGContextSaveGState(cg);
    if (ctx->opacity < 1.f) {
        CGContextSetAlpha(cg, ctx->opacity < 0 ? 0 : ctx->opacity);
    }
    if (radius > 0) {
        // Clipped in the caller's coordinates, before the flip below.
        float half = (bounds.w < bounds.h ? bounds.w : bounds.h) * 0.5f;
        CGFloat r = radius > half ? half : radius;
        CGPathRef path = CGPathCreateWithRoundedRect(
            CGRectMake(bounds.x, bounds.y, bounds.w, bounds.h), r, r, nullptr);
        CGContextAddPath(cg, path);
        CGContextClip(cg);
        CGPathRelease(path);
    } else {
        CGContextClipToRect(cg,
                            CGRectMake(bounds.x, bounds.y, bounds.w, bounds.h));
    }
    // The context is y-down for everything else here, and CGContextDrawImage
    // is the one call that reads y-up, so the box is flipped about itself.
    CGContextTranslateCTM(cg, imageBounds.x, imageBounds.y + imageBounds.h);
    CGContextScaleCTM(cg, 1, -1);
    CGContextDrawImage(cg, CGRectMake(0, 0, imageBounds.w, imageBounds.h),
                       image);
    CGContextRestoreGState(cg);
}

// ─── shaped text ──────────────────────────────────────────────────────────

struct MacLine {
    CTLineRef line = nullptr;
    int start = 0; // UTF-16 index into the whole string
    int len = 0;
    float width = 0;
};

struct TextLayout {
    uint64_t generation = 0;
    int refs = 1;
    // What TextLayoutNew reported, kept so TextLayoutSize can answer without
    // measuring again.
    Size size = {};
    CFAttributedStringRef attr = nullptr;
    MacLine* lines = nullptr;
    int nLines = 0;
    // GPUI's line box and where the baseline sits inside it.
    float box = 0;
    float baseline = 0;
    float width = 0;
    // Core Text knows kCTUnderlineStyleAttributeName and nothing else: the
    // strikethrough attribute is AppKit's, and CTLineDraw ignores it. So the
    // rule is drawn here, from the font's own metrics — its offset above the
    // baseline and its thickness, which is what DirectWrite and Pango use for
    // theirs.
    bool strike = false;
    float strikeOff = 0;
    float strikeThick = 1;
};

static NSFontWeight WeightFor(uint8_t weight, float fontSize) {
    switch (weight & kFontWeightMask) {
        case kFontWeightThin:
            return NSFontWeightThin;
        case kFontWeightExtraLight:
            return NSFontWeightUltraLight;
        case kFontWeightLight:
            return NSFontWeightLight;
        case kFontWeightExplicitNormal:
            return NSFontWeightRegular;
        case kFontWeightMedium:
            return NSFontWeightMedium;
        case kFontWeightSemibold:
            return NSFontWeightSemibold;
        case kFontWeightBold:
            return NSFontWeightBold;
        case kFontWeightExtraBold:
            return NSFontWeightHeavy;
        case kFontWeightBlack:
            return NSFontWeightBlack;
        default:
            break;
    }
    // The 20 px and 24 px DirectWrite formats are created semibold, and a run
    // that asks for no weight of its own inherits that. Match it.
    return fontSize >= 18.f ? NSFontWeightSemibold : NSFontWeightRegular;
}

static CTFontRef FontFor(PaintApp* pa, float fontSize, uint8_t weight) {
    for (int i = 0; i < pa->nFonts; i++) {
        if (pa->fonts[i].size == fontSize && pa->fonts[i].weight == weight) {
            return pa->fonts[i].font;
        }
    }
    NSFontWeight w = WeightFor(weight, fontSize);
    NSFont* font = nil;
    if (weight & kFontMono) {
        if (@available(macOS 10.15, *)) {
            font = [NSFont monospacedSystemFontOfSize:fontSize weight:w];
        }
        if (!font) {
            font = [NSFont fontWithName:@"Menlo" size:fontSize];
        }
    } else {
        font = [NSFont systemFontOfSize:fontSize weight:w];
    }
    if (!font) {
        font = [NSFont systemFontOfSize:fontSize];
    }
    if ((weight & kFontItalic) && font) {
        NSFont* italic =
            [[NSFontManager sharedFontManager] convertFont:font
                                               toHaveTrait:NSItalicFontMask];
        if (italic) {
            font = italic;
        }
    }
    if (!font) {
        return nullptr;
    }
    auto ct = (CTFontRef)CFBridgingRetain(font);
    if (pa->nFonts >= kFontCacheCap) {
        // The cache is a fixed set of sizes in practice; if it ever fills up,
        // drop the oldest rather than growing without bound.
        CFRelease(pa->fonts[0].font);
        for (int i = 1; i < kFontCacheCap; i++) {
            pa->fonts[i - 1] = pa->fonts[i];
        }
        pa->nFonts = kFontCacheCap - 1;
    }
    pa->fonts[pa->nFonts].size = fontSize;
    pa->fonts[pa->nFonts].weight = weight;
    pa->fonts[pa->nFonts].font = ct;
    pa->nFonts++;
    return ct;
}

TextLayout* TextLayoutNew(PaintCtx* ctx, Str s, float fontSize, float maxW,
                          bool wrap, uint8_t weight, float lineH,
                          Size* outSize) {
    if (!ctx || !ctx->pa || !s.s || s.len <= 0) {
        return nullptr;
    }
    if (fontSize <= 0) {
        fontSize = 16.f;
    }
    CTFontRef font = FontFor(ctx->pa, fontSize, weight);
    if (!font) {
        return nullptr;
    }
    CFStringRef text = CFStringCreateWithBytes(
        nullptr, (const UInt8*)s.s, s.len, kCFStringEncodingUTF8, false);
    if (!text) {
        return nullptr;
    }
    CFIndex u16Len = CFStringGetLength(text);
    if (u16Len <= 0) {
        CFRelease(text);
        return nullptr;
    }

    CFMutableDictionaryRef attrs =
        CFDictionaryCreateMutable(nullptr, 3, &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(attrs, kCTFontAttributeName, font);
    // CTLine otherwise supplies its default black foreground and ignores the
    // CGContext fill color selected by TextLayoutDraw.
    CFDictionarySetValue(attrs, kCTForegroundColorFromContextAttributeName,
                         kCFBooleanTrue);
    if (weight & kFontUnderline) {
        int32_t style = kCTUnderlineStyleSingle;
        CFNumberRef n = CFNumberCreate(nullptr, kCFNumberSInt32Type, &style);
        CFDictionarySetValue(attrs, kCTUnderlineStyleAttributeName, n);
        CFRelease(n);
    }
    CFAttributedStringRef attr = CFAttributedStringCreate(nullptr, text, attrs);
    CFRelease(attrs);
    CFRelease(text);
    if (!attr) {
        return nullptr;
    }

    CTTypesetterRef ts = CTTypesetterCreateWithAttributedString(attr);
    if (!ts) {
        CFRelease(attr);
        return nullptr;
    }

    // Hard newlines split paragraphs; inside a paragraph the typesetter picks
    // the break. Both the DirectWrite and Pango backends behave this way.
    auto* lines = (MacLine*)AllocArray<MacLine>((int)u16Len + 2);
    int nLines = 0;
    float width = 0;
    CFIndex pos = 0;
    UniChar buf[1];
    while (pos < u16Len) {
        CFIndex paraEnd = pos;
        while (paraEnd < u16Len) {
            CFStringGetCharacters(CFAttributedStringGetString(attr),
                                  CFRangeMake(paraEnd, 1), buf);
            if (buf[0] == '\n') {
                break;
            }
            paraEnd++;
        }
        CFIndex lineStart = pos;
        do {
            CFIndex count = paraEnd - lineStart;
            if (wrap && maxW > 0 && count > 0) {
                CFIndex fits =
                    CTTypesetterSuggestLineBreak(ts, lineStart, maxW);
                if (fits > 0 && fits < count) {
                    count = fits;
                }
            }
            CTLineRef line =
                CTTypesetterCreateLine(ts, CFRangeMake(lineStart, count));
            if (line) {
                CGFloat asc = 0, desc = 0, lead = 0;
                double w = CTLineGetTypographicBounds(line, &asc, &desc, &lead);
                lines[nLines].line = line;
                lines[nLines].start = (int)lineStart;
                lines[nLines].len = (int)count;
                lines[nLines].width = (float)w;
                if ((float)w > width) {
                    width = (float)w;
                }
                nLines++;
            }
            lineStart += count;
            // An empty paragraph still contributes one (empty) line.
        } while (lineStart < paraEnd);
        pos = paraEnd + 1;
    }
    CFRelease(ts);

    if (nLines == 0) {
        Free(nullptr, lines);
        CFRelease(attr);
        return nullptr;
    }

    auto* tl = new TextLayout();
    tl->generation = PaintResourceGenerationNew();
    tl->attr = attr;
    tl->lines = lines;
    tl->nLines = nLines;
    tl->width = width;
    tl->box = fontSize * (lineH > 0 ? lineH : kLineHeight);
    // gpui text_system centers the glyphs in the box: half the leftover goes
    // above the ascent.
    CGFloat ascent = CTFontGetAscent(font);
    CGFloat descent = CTFontGetDescent(font);
    tl->baseline = (float)ascent + (tl->box - (float)(ascent + descent)) * 0.5f;
    if (weight & kFontStrike) {
        tl->strike = true;
        // The rule sits just under a third of the ascent above the baseline —
        // about the x-height's middle for a text face — and is as thick as
        // the font's underline. A face that reports no thickness (some bitmap
        // and fallback faces do not) gets one device-independent pixel.
        tl->strikeThick = (float)CTFontGetUnderlineThickness(font);
        if (tl->strikeThick <= 0) {
            tl->strikeThick = 1;
        }
        tl->strikeOff = (float)ascent * 0.31f;
    }

    tl->size = Size{width, tl->box * (float)nLines};
    if (outSize) {
        outSize->w = tl->size.w;
        outSize->h = tl->size.h;
    }
    return tl;
}

Size TextLayoutSize(TextLayout* tl) {
    return tl ? tl->size : Size{0, 0};
}

void TextLayoutAddRef(TextLayout* tl) {
    if (tl) {
        tl->refs++;
    }
}

void TextLayoutRelease(TextLayout* tl) {
    if (!tl) {
        return;
    }
    if (--tl->refs > 0) {
        return;
    }
    for (int i = 0; i < tl->nLines; i++) {
        if (tl->lines[i].line) {
            CFRelease(tl->lines[i].line);
        }
    }
    Free(nullptr, tl->lines);
    if (tl->attr) {
        CFRelease(tl->attr);
    }
    delete tl;
}

uint64_t TextLayoutGeneration(const TextLayout* tl) {
    return tl ? tl->generation : 0;
}

bool PaintTextLayoutSpans(PaintCtx*, TextLayout*, Str, float, float, Rgba,
                          const TextSpan*, int) {
    return false;
}

void TextLayoutDraw(PaintCtx* ctx, TextLayout* tl, float x, float y, Rgba c,
                    bool clip, float clipW) {
    // No ellipsis here yet: Core Text truncates through
    // CTLineCreateTruncatedLine, which means re-typesetting the line rather
    // than setting a mode on the layout, so a truncated run is cut at the box
    // edge on macOS where Windows and Linux end it in one.
    (void)clipW;
    CGContextRef cg = Cg(ctx);
    if (!cg || !tl) {
        return;
    }
    if (clip) {
        // Horizontally only: a descender's ink runs below a line box of
        // relative 1.0, so clipping to that box cut the tails of "g" and "y"
        // — the `overflow_hidden` upstream dropped while keeping the
        // ellipsis.
        float boxH = tl->box * (float)tl->nLines;
        CGContextSaveGState(cg);
        CGContextClipToRect(cg, CGRectMake(x, y - boxH, tl->width, boxH * 3.f));
    }
    SetFill(ctx, cg, c);
    CGContextSetTextMatrix(cg, CGAffineTransformMakeScale(1, -1));
    for (int i = 0; i < tl->nLines; i++) {
        float base = y + (float)i * tl->box + tl->baseline;
        CGContextSetTextPosition(cg, x, base);
        CTLineDraw(tl->lines[i].line, cg);
        if (tl->strike && tl->lines[i].width > 0) {
            CGContextFillRect(
                cg, CGRectMake(x, base - tl->strikeOff, tl->lines[i].width,
                               tl->strikeThick));
        }
    }
    if (clip) {
        CGContextRestoreGState(cg);
    }
}

int TextLayoutHitPoint(TextLayout* tl, Str s, float relX, float relY) {
    if (!tl || tl->nLines == 0) {
        return 0;
    }
    int row = tl->box > 0 ? (int)(relY / tl->box) : 0;
    if (row < 0) {
        row = 0;
    }
    if (row >= tl->nLines) {
        row = tl->nLines - 1;
    }
    const MacLine& ml = tl->lines[row];
    CFIndex idx =
        CTLineGetStringIndexForPosition(ml.line, CGPointMake(relX, 0));
    if (idx == kCFNotFound) {
        idx = ml.start + ml.len;
    }
    return U16OffToUtf8(s, (int)idx);
}

float TextLayoutBaseline(TextLayout* tl) {
    return tl ? tl->baseline : 0.f;
}

int TextLayoutRangeRects(TextLayout* tl, Str s, int u8a, int u8b, Bounds* out,
                         int max) {
    if (!tl || !out || max <= 0 || u8a >= u8b) {
        return 0;
    }
    int a = Utf8OffToU16(s, u8a);
    int b = Utf8OffToU16(s, u8b);
    int n = 0;
    for (int i = 0; i < tl->nLines && n < max; i++) {
        const MacLine& ml = tl->lines[i];
        int lo = a > ml.start ? a : ml.start;
        int hi = b < ml.start + ml.len ? b : ml.start + ml.len;
        if (lo >= hi) {
            continue;
        }
        CGFloat x0 = CTLineGetOffsetForStringIndex(ml.line, lo, nullptr);
        CGFloat x1 = CTLineGetOffsetForStringIndex(ml.line, hi, nullptr);
        if (x1 < x0) {
            CGFloat t = x0;
            x0 = x1;
            x1 = t;
        }
        out[n].x = (float)x0;
        out[n].y = (float)i * tl->box;
        out[n].w = (float)(x1 - x0);
        out[n].h = tl->box;
        n++;
    }
    return n;
}

} // namespace gpui
