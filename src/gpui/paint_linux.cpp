/* cairo + Pango backend for Paint.h.

   cairo's user space is y-down with clockwise-increasing angles, the same
   convention the element tree uses, so nothing here flips coordinates. */

#include "gpui/paint.h"
#include "gpui/scene.h"

#include <math.h>
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#define GLIB_DISABLE_DEPRECATION_WARNINGS
#include <gdk-pixbuf/gdk-pixbuf.h>

namespace gpui {

static uint64_t gNextPaintResourceGeneration = 1;
static bool gOffscreenWasRecording = false;
static bool gNoSceneBegin = false;

static bool SceneFinish(PaintCtx* ctx) {
    if (!SceneOn() || !scene::Recording()) {
        return true;
    }
    Bounds damage = {};
    bool draw = scene::FrameEnd(ctx, &damage);
    if (draw) {
        scene::Replay(ctx, &damage);
    }
    return draw;
}

static uint64_t PaintResourceGenerationNew() {
    uint64_t id = gNextPaintResourceGeneration++;
    if (id == 0) {
        id = gNextPaintResourceGeneration++;
    }
    return id;
}

struct PaintApp {
    // A cairo-backed font map context, so a layout can be shaped and measured
    // without a target bound.
    PangoContext* pango = nullptr;
};

struct PaintTarget {
    cairo_t* cr = nullptr;
};

// ─── lifecycle ────────────────────────────────────────────────────────────

PaintApp* PaintAppNew() {
    auto* pa = new PaintApp();
    PangoFontMap* map = pango_cairo_font_map_get_default();
    if (!map) {
        delete pa;
        return nullptr;
    }
    pa->pango = pango_font_map_create_context(map);
    if (!pa->pango) {
        delete pa;
        return nullptr;
    }
    // Grayscale AA, not the fontconfig default of subpixel: a frame is drawn
    // into an image surface and blitted, so LCD filtering would bake color
    // fringes into text that DirectWrite does not produce.
    cairo_font_options_t* fo = cairo_font_options_create();
    cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_SLIGHT);
    cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_OFF);
    pango_cairo_context_set_font_options(pa->pango, fo);
    cairo_font_options_destroy(fo);
    // Sizes are set in absolute device units, so keep the context at 1:1.
    pango_cairo_context_set_resolution(pa->pango, 96.0);
    return pa;
}

void PaintAppFree(PaintApp* pa) {
    if (!pa) {
        return;
    }
    if (pa->pango) {
        g_object_unref(pa->pango);
    }
    delete pa;
}

static Vec<Str> gInstalledFonts;
static bool gInstalledFontsReady = false;

const Str* PaintInstalledFontNames(PaintApp* pa, int* n) {
    (void)pa;
    if (!gInstalledFontsReady) {
        gInstalledFontsReady = true;
        PangoFontMap* map = pango_cairo_font_map_get_default();
        PangoFontFamily** families = nullptr;
        int count = 0;
        if (map) {
            pango_font_map_list_families(map, &families, &count);
        }
        for (int i = 0; i < count; i++) {
            const char* name = pango_font_family_get_name(families[i]);
            if (name && name[0]) {
                VecAppend(gInstalledFonts, StrDup(Str(name)));
            }
        }
        g_free(families);
    }
    if (n) {
        *n = gInstalledFonts.len;
    }
    return gInstalledFonts.els;
}

Str PaintSystemUIFontMappedFamily() {
    // CosmicTextSystem::new("IBM Plex Sans"): the Linux mapping of
    // `.SystemUIFont`, which most desktops do not ship.
    return StrL("IBM Plex Sans");
}

void PaintTargetFree(PaintCtx* ctx) {
    if (!ctx || !ctx->rt) {
        return;
    }
    if (ctx->rt->cr) {
        cairo_destroy(ctx->rt->cr);
    }
    delete ctx->rt;
    ctx->rt = nullptr;
}

// `native` is the window's cairo_surface_t; the context is per-frame because
// an XCB/Xlib surface is resized under us between expose events.
bool PaintTargetBegin(PaintCtx* ctx, void* native, int pxW, int pxH) {
    (void)pxW;
    (void)pxH;
    if (!ctx || !ctx->pa || !native) {
        return false;
    }
    PaintTargetFree(ctx);
    auto* t = new PaintTarget();
    t->cr = cairo_create((cairo_surface_t*)native);
    if (!t->cr || cairo_status(t->cr) != CAIRO_STATUS_SUCCESS) {
        if (t->cr) {
            cairo_destroy(t->cr);
        }
        delete t;
        return false;
    }
    ctx->rt = t;
    cairo_set_antialias(t->cr, CAIRO_ANTIALIAS_DEFAULT);
    if (SceneOn() && !gNoSceneBegin) {
        scene::FrameBegin(ctx);
    }
    return true;
}

// The offscreen target: an image surface rather than the window's, kept here
// until the pixels are read back out of it.
static cairo_surface_t* gOffscreenSurface = nullptr;

bool PaintTargetBeginOffscreen(PaintCtx* ctx, int pxW, int pxH) {
    gOffscreenWasRecording = scene::SuspendBegin();
    if (!ctx || !ctx->pa || pxW <= 0 || pxH <= 0) {
        scene::SuspendEnd(gOffscreenWasRecording);
        return false;
    }
    cairo_surface_t* surf =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pxW, pxH);
    if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        if (surf) {
            cairo_surface_destroy(surf);
        }
        scene::SuspendEnd(gOffscreenWasRecording);
        return false;
    }
    gNoSceneBegin = true;
    if (!PaintTargetBegin(ctx, surf, pxW, pxH)) {
        gNoSceneBegin = false;
        cairo_surface_destroy(surf);
        scene::SuspendEnd(gOffscreenWasRecording);
        return false;
    }
    gNoSceneBegin = false;
    gOffscreenSurface = surf;
    return true;
}

bool PaintTargetEndOffscreen(PaintCtx* ctx, uint8_t* outBgra) {
    if (!ctx || !ctx->rt || !gOffscreenSurface) {
        scene::SuspendEnd(gOffscreenWasRecording);
        return false;
    }
    cairo_surface_flush(gOffscreenSurface);
    if (outBgra) {
        int w = cairo_image_surface_get_width(gOffscreenSurface);
        int h = cairo_image_surface_get_height(gOffscreenSurface);
        int stride = cairo_image_surface_get_stride(gOffscreenSurface);
        const uint8_t* src = cairo_image_surface_get_data(gOffscreenSurface);
        // CAIRO_FORMAT_ARGB32 is already premultiplied BGRA on a little
        // endian machine; the rows are copied without their padding.
        for (int y = 0; y < h; y++) {
            memcpy(outBgra + (size_t)y * (size_t)w * 4,
                   src + (size_t)y * (size_t)stride, (size_t)w * 4);
        }
    }
    PaintTargetFree(ctx);
    cairo_surface_destroy(gOffscreenSurface);
    gOffscreenSurface = nullptr;
    scene::SuspendEnd(gOffscreenWasRecording);
    return true;
}

bool PaintTargetEnd(PaintCtx* ctx) {
    SceneFinish(ctx);
    if (!ctx || !ctx->rt || !ctx->rt->cr) {
        return false;
    }
    cairo_surface_t* surf = cairo_get_target(ctx->rt->cr);
    if (surf) {
        cairo_surface_flush(surf);
    }
    PaintTargetFree(ctx);
    return true;
}

// ─── canvas ───────────────────────────────────────────────────────────────

static cairo_t* Cr(PaintCtx* ctx) {
    return (ctx && ctx->rt) ? ctx->rt->cr : nullptr;
}

// element_opacity: the colour is faded by the opacity in force on its way to
// cairo, which is the one place every colour passes through.
static void SetColor(PaintCtx* ctx, cairo_t* cr, Rgba c) {
    c = PaintFade(ctx, c);
    cairo_set_source_rgba(cr, c.r / 255.0, c.g / 255.0, c.b / 255.0,
                          c.a / 255.0);
}

// D2D measures a dash pattern in stroke widths; cairo in user units.
static void SetDash(cairo_t* cr, const float* dash, float stroke) {
    if (!dash) {
        cairo_set_dash(cr, nullptr, 0, 0);
        return;
    }
    double d[2] = {dash[0] * stroke, dash[1] * stroke};
    cairo_set_dash(cr, d, 2, 0);
}

static void RoundRectPath(cairo_t* cr, float x, float y, float w, float h,
                          float r) {
    float rmax = (w < h ? w : h) * 0.5f;
    if (r > rmax) {
        r = rmax;
    }
    if (r <= 0) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -kPi / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, kPi / 2);
    cairo_arc(cr, x + r, y + h - r, r, kPi / 2, kPi);
    cairo_arc(cr, x + r, y + r, r, kPi, 3 * kPi / 2);
    cairo_close_path(cr);
}

void CanvasClear(PaintCtx* ctx, Rgba c) {
    if (scene::Recording()) {
        scene::RecClear(ctx, c);
        return;
    }
    cairo_t* cr = Cr(ctx);
    if (!cr) {
        return;
    }
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    SetColor(ctx, cr, c);
    cairo_paint(cr);
    cairo_restore(cr);
}

void CanvasFillRect(PaintCtx* ctx, float x, float y, float w, float h, Rgba c) {
    if (scene::Recording()) {
        scene::RecFillRect(ctx, x, y, w, h, c);
        return;
    }
    cairo_t* cr = Cr(ctx);
    if (!cr || w <= 0 || h <= 0 || c.a == 0) {
        return;
    }
    SetColor(ctx, cr, c);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
}

void CanvasFillRound(PaintCtx* ctx, float x, float y, float w, float h, float r,
                     Rgba c) {
    if (scene::Recording()) {
        scene::RecFillRound(ctx, x, y, w, h, r, c);
        return;
    }
    cairo_t* cr = Cr(ctx);
    if (!cr || w <= 0 || h <= 0 || c.a == 0) {
        return;
    }
    SetColor(ctx, cr, c);
    RoundRectPath(cr, x, y, w, h, r);
    cairo_fill(cr);
}

void CanvasStrokeRound(PaintCtx* ctx, float x, float y, float w, float h,
                       float r, float stroke, Rgba c, const float* dash) {
    if (scene::Recording()) {
        scene::RecStrokeRound(ctx, x, y, w, h, r, stroke, c, dash);
        return;
    }
    cairo_t* cr = Cr(ctx);
    if (!cr || stroke <= 0 || w <= 0 || h <= 0) {
        return;
    }
    SetColor(ctx, cr, c);
    cairo_set_line_width(cr, stroke);
    SetDash(cr, dash, stroke);
    // Inset by half the stroke: cairo, like D2D, centers it on the path.
    RoundRectPath(cr, x + stroke * 0.5f, y + stroke * 0.5f, w - stroke,
                  h - stroke, r);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);
}

void CanvasLine(PaintCtx* ctx, float x1, float y1, float x2, float y2,
                float stroke, Rgba c, const float* dash) {
    if (scene::Recording()) {
        scene::RecLine(ctx, x1, y1, x2, y2, stroke, c, dash);
        return;
    }
    cairo_t* cr = Cr(ctx);
    if (!cr) {
        return;
    }
    SetColor(ctx, cr, c);
    cairo_set_line_width(cr, stroke);
    SetDash(cr, dash, stroke);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);
}

void CanvasEllipse(PaintCtx* ctx, float cx, float cy, float rx, float ry,
                   float stroke, Rgba c) {
    if (scene::Recording()) {
        scene::RecEllipse(ctx, cx, cy, rx, ry, stroke, c);
        return;
    }
    cairo_t* cr = Cr(ctx);
    if (!cr || rx <= 0 || ry <= 0) {
        return;
    }
    SetColor(ctx, cr, c);
    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_scale(cr, rx, ry);
    cairo_new_sub_path(cr);
    cairo_arc(cr, 0, 0, 1, 0, 2 * kPi);
    cairo_restore(cr);
    if (stroke > 0) {
        cairo_set_line_width(cr, stroke);
        cairo_stroke(cr);
    } else {
        cairo_fill(cr);
    }
}

void CanvasPushClip(PaintCtx* ctx, float x, float y, float w, float h) {
    if (scene::Recording()) {
        scene::RecPushClip(ctx, x, y, w, h);
        return;
    }
    cairo_t* cr = Cr(ctx);
    if (!cr) {
        return;
    }
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
}

void CanvasPopClip(PaintCtx* ctx) {
    if (scene::Recording()) {
        scene::RecPopClip(ctx);
        return;
    }
    cairo_t* cr = Cr(ctx);
    if (cr) {
        cairo_restore(cr);
    }
}

// ─── paths ────────────────────────────────────────────────────────────────
//
// cairo builds its path on the context, but Paint.h hands a Path around
// before there is anything to draw it on, so the ops are recorded and
// replayed at fill / stroke time.

enum PathCmd : uint8_t {
    kPathMove,
    kPathLine,
    kPathCubic,
    kPathArc,
    kPathClose
};

struct CairoPathOp {
    PathCmd cmd = kPathMove;
    bool clockwise = false;
    float a = 0, b = 0, c = 0, d = 0, e = 0, f = 0;
};

struct Path {
    Vec<CairoPathOp> ops;
    bool winding = true;
    bool fig = false;
};

Path* PathNew(PaintCtx* ctx, bool winding) {
    if (scene::Recording()) {
        return scene::RecPathNew(ctx, winding);
    }
    if (!ctx) {
        return nullptr;
    }
    auto* p = new Path();
    p->winding = winding;
    return p;
}

void PathFree(Path* p) {
    if (scene::Recording()) {
        scene::RecPathFree(p);
        return;
    }
    delete p;
}

static void Push(Path* p, const CairoPathOp& op) {
    if (p) {
        VecAppend(p->ops, op);
    }
}

void PathMoveTo(Path* p, float x, float y) {
    if (scene::Recording()) {
        scene::RecPathMoveTo(p, x, y);
        return;
    }
    if (!p) {
        return;
    }
    CairoPathOp op;
    op.cmd = kPathMove;
    op.a = x;
    op.b = y;
    Push(p, op);
    p->fig = true;
}

void PathLineTo(Path* p, float x, float y) {
    if (scene::Recording()) {
        scene::RecPathLineTo(p, x, y);
        return;
    }
    if (!p) {
        return;
    }
    if (!p->fig) {
        PathMoveTo(p, x, y);
        return;
    }
    CairoPathOp op;
    op.cmd = kPathLine;
    op.a = x;
    op.b = y;
    Push(p, op);
}

void PathCubicTo(Path* p, float x1, float y1, float x2, float y2, float x,
                 float y) {
    if (scene::Recording()) {
        scene::RecPathCubicTo(p, x1, y1, x2, y2, x, y);
        return;
    }
    if (!p) {
        return;
    }
    if (!p->fig) {
        PathMoveTo(p, x, y);
        return;
    }
    CairoPathOp op;
    op.cmd = kPathCubic;
    op.a = x1;
    op.b = y1;
    op.c = x2;
    op.d = y2;
    op.e = x;
    op.f = y;
    Push(p, op);
}

void PathArcTo(Path* p, float cx, float cy, float r, float a0, float a1,
               bool clockwise) {
    if (scene::Recording()) {
        scene::RecPathArcTo(p, cx, cy, r, a0, a1, clockwise);
        return;
    }
    if (!p) {
        return;
    }
    CairoPathOp op;
    op.cmd = kPathArc;
    op.clockwise = clockwise;
    op.a = cx;
    op.b = cy;
    op.c = r;
    op.d = a0;
    op.e = a1;
    Push(p, op);
    // cairo_arc draws a line from the current point to the arc start, so an
    // arc opens a figure the same way a move does.
    p->fig = true;
}

void PathClose(Path* p) {
    if (scene::Recording()) {
        scene::RecPathClose(p);
        return;
    }
    if (!p || !p->fig) {
        return;
    }
    CairoPathOp op;
    op.cmd = kPathClose;
    Push(p, op);
    p->fig = false;
}

static bool Replay(cairo_t* cr, Path* p) {
    if (!cr || !p || p->ops.len == 0) {
        return false;
    }
    cairo_new_path(cr);
    cairo_set_fill_rule(
        cr, p->winding ? CAIRO_FILL_RULE_WINDING : CAIRO_FILL_RULE_EVEN_ODD);
    for (int i = 0; i < p->ops.len; i++) {
        const CairoPathOp& o = p->ops[i];
        switch (o.cmd) {
            case kPathMove:
                cairo_move_to(cr, o.a, o.b);
                break;
            case kPathLine:
                cairo_line_to(cr, o.a, o.b);
                break;
            case kPathCubic:
                cairo_curve_to(cr, o.a, o.b, o.c, o.d, o.e, o.f);
                break;
            case kPathArc:
                if (o.clockwise) {
                    cairo_arc(cr, o.a, o.b, o.c, o.d, o.e);
                } else {
                    cairo_arc_negative(cr, o.a, o.b, o.c, o.d, o.e);
                }
                break;
            case kPathClose:
                cairo_close_path(cr);
                break;
        }
    }
    return true;
}

// Nothing to cache: this backend hands the path to cairo, which owns
// whatever it wants to keep about it.
void PathRealize(PaintCtx* ctx, Path* p) {
    (void)ctx;
    (void)p;
}

void PathFill(PaintCtx* ctx, Path* p, Rgba c, float dx, float dy) {
    if (scene::Recording()) {
        scene::RecPathFill(ctx, p, c);
        return;
    }
    cairo_t* cr = Cr(ctx);
    if (!cr) {
        return;
    }
    cairo_save(cr);
    cairo_translate(cr, dx, dy);
    if (!Replay(cr, p)) {
        cairo_restore(cr);
        return;
    }
    SetColor(ctx, cr, c);
    cairo_fill(cr);
    cairo_restore(cr);
}

void PathFillGradientV(PaintCtx* ctx, Path* p, float y0, float y1, Rgba top,
                       Rgba bot) {
    PathFillGradient(ctx, p, 0, y0, 0, y1, top, bot);
}

void PathFillGradient(PaintCtx* ctx, Path* p, float x0, float y0, float x1,
                      float y1, Rgba from, Rgba to, float dx, float dy) {
    if (scene::Recording()) {
        scene::RecPathFillGradient(ctx, p, x0, y0, x1, y1, from, to);
        return;
    }
    cairo_t* cr = Cr(ctx);
    if (!cr) {
        return;
    }
    cairo_save(cr);
    cairo_translate(cr, dx, dy);
    if (!Replay(cr, p)) {
        cairo_restore(cr);
        return;
    }
    cairo_pattern_t* pat = cairo_pattern_create_linear(x0, y0, x1, y1);
    if (!pat) {
        SetColor(ctx, cr, from);
        cairo_fill(cr);
        cairo_restore(cr);
        return;
    }
    from = PaintFade(ctx, from);
    to = PaintFade(ctx, to);
    cairo_pattern_add_color_stop_rgba(pat, 0, from.r / 255.0, from.g / 255.0,
                                      from.b / 255.0, from.a / 255.0);
    cairo_pattern_add_color_stop_rgba(pat, 1, to.r / 255.0, to.g / 255.0,
                                      to.b / 255.0, to.a / 255.0);
    cairo_set_source(cr, pat);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);
    cairo_restore(cr);
}

void PathStroke(PaintCtx* ctx, Path* p, float stroke, Rgba c, bool roundCaps,
                float dx, float dy) {
    if (scene::Recording()) {
        scene::RecPathStroke(ctx, p, stroke, c, roundCaps);
        return;
    }
    cairo_t* cr = Cr(ctx);
    if (!cr) {
        return;
    }
    cairo_save(cr);
    cairo_translate(cr, dx, dy);
    if (!Replay(cr, p)) {
        cairo_restore(cr);
        return;
    }
    SetColor(ctx, cr, c);
    cairo_set_line_width(cr, stroke);
    cairo_set_line_cap(cr,
                       roundCaps ? CAIRO_LINE_CAP_ROUND : CAIRO_LINE_CAP_BUTT);
    cairo_set_line_join(
        cr, roundCaps ? CAIRO_LINE_JOIN_ROUND : CAIRO_LINE_JOIN_MITER);
    cairo_stroke(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
    cairo_restore(cr);
}

// ─── images ───────────────────────────────────────────────────────────────
//
// gdk-pixbuf is the system decoder (PNG, JPEG, GIF, WebP when the loader is
// installed), the way WIC is on Windows and ImageIO on macOS. cairo still
// paints the pixels. Animation walks GdkPixbufAnimationIter once, copying
// each frame because the iterator reuses its pixbuf.

struct LinuxImageFrame {
    cairo_surface_t* surface = nullptr;
    cairo_surface_t* graySurface = nullptr;
    int w = 0;
    int h = 0;
    int durationMs = 100;
};

struct RenderImage {
    int refs = 1;
    uint64_t generation = 0;
    RenderImageStatus status = RenderImageStatus::Ready;
    Vec<LinuxImageFrame> frames;
};

static cairo_surface_t* SurfaceFromPixbuf(GdkPixbuf* pix) {
    if (!pix) {
        return nullptr;
    }
    int w = gdk_pixbuf_get_width(pix);
    int h = gdk_pixbuf_get_height(pix);
    if (w <= 0 || h <= 0) {
        return nullptr;
    }
    cairo_surface_t* surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface) {
            cairo_surface_destroy(surface);
        }
        return nullptr;
    }
    unsigned char* dst = cairo_image_surface_get_data(surface);
    int dstStride = cairo_image_surface_get_stride(surface);
    const guint8* src = gdk_pixbuf_get_pixels(pix);
    int srcStride = gdk_pixbuf_get_rowstride(pix);
    int nch = gdk_pixbuf_get_n_channels(pix);
    bool alpha = gdk_pixbuf_get_has_alpha(pix) ? true : false;
    for (int y = 0; y < h; y++) {
        const guint8* srow = src + y * srcStride;
        unsigned char* drow = dst + y * dstStride;
        for (int x = 0; x < w; x++) {
            const guint8* s = srow + x * nch;
            uint8_t r = s[0];
            uint8_t g = nch > 1 ? s[1] : r;
            uint8_t b = nch > 2 ? s[2] : r;
            uint8_t a = alpha && nch > 3 ? s[3] : 255;
            // cairo ARGB32 is native-endian premultiplied; little-endian BGRA.
            uint32_t pr = ((uint32_t)r * a + 127) / 255;
            uint32_t pg = ((uint32_t)g * a + 127) / 255;
            uint32_t pb = ((uint32_t)b * a + 127) / 255;
            drow[x * 4 + 0] = (unsigned char)pb;
            drow[x * 4 + 1] = (unsigned char)pg;
            drow[x * 4 + 2] = (unsigned char)pr;
            drow[x * 4 + 3] = a;
        }
    }
    cairo_surface_mark_dirty(surface);
    return surface;
}

static uint64_t HashPixbuf(GdkPixbuf* pix) {
    if (!pix) {
        return 0;
    }
    int w = gdk_pixbuf_get_width(pix);
    int h = gdk_pixbuf_get_height(pix);
    int stride = gdk_pixbuf_get_rowstride(pix);
    const guint8* src = gdk_pixbuf_get_pixels(pix);
    uint64_t hash = 1469598103934665603ull;
    hash ^= (uint64_t)w;
    hash *= 1099511628211ull;
    hash ^= (uint64_t)h;
    hash *= 1099511628211ull;
    int n = stride * h;
    for (int i = 0; i < n; i++) {
        hash ^= src[i];
        hash *= 1099511628211ull;
    }
    return hash ? hash : 1;
}

static bool AppendPixbufFrame(RenderImage* img, GdkPixbuf* pix, int delayMs) {
    cairo_surface_t* surface = SurfaceFromPixbuf(pix);
    if (!surface) {
        return false;
    }
    LinuxImageFrame frame = {};
    frame.surface = surface;
    frame.w = cairo_image_surface_get_width(surface);
    frame.h = cairo_image_surface_get_height(surface);
    frame.durationMs = delayMs > 0 ? delayMs : 100;
    VecAppend(img->frames, frame);
    return true;
}

static void LinuxFrameFree(LinuxImageFrame* frame) {
    if (!frame) {
        return;
    }
    if (frame->surface) {
        cairo_surface_destroy(frame->surface);
    }
    if (frame->graySurface) {
        cairo_surface_destroy(frame->graySurface);
    }
    *frame = {};
}

RenderImage* RenderImageDecode(PaintApp* pa, const uint8_t* bytes, int len) {
    (void)pa;
    if (!bytes || len <= 0) {
        return nullptr;
    }
    GError* err = nullptr;
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    if (!loader) {
        return nullptr;
    }
    if (!gdk_pixbuf_loader_write(loader, bytes, (gsize)len, &err) ||
        !gdk_pixbuf_loader_close(loader, &err)) {
        if (err) {
            g_error_free(err);
        }
        g_object_unref(loader);
        return nullptr;
    }
    // GdkPixbuf's animation API still takes GTimeVal and is deprecated in
    // newer headers without a compatible replacement for frame iteration.
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GdkPixbufAnimation* anim = gdk_pixbuf_loader_get_animation(loader);
    if (!anim) {
        g_object_unref(loader);
        return nullptr;
    }
    auto* img = new RenderImage();
    img->generation = PaintResourceGenerationNew();
    if (gdk_pixbuf_animation_is_static_image(anim)) {
        AppendPixbufFrame(img, gdk_pixbuf_animation_get_static_image(anim), 0);
    } else {
        GTimeVal time = {};
        GdkPixbufAnimationIter* iter =
            gdk_pixbuf_animation_get_iter(anim, &time);
        uint64_t firstHash = 0;
        for (int n = 0; iter && n < 256; n++) {
            GdkPixbuf* pix = gdk_pixbuf_animation_iter_get_pixbuf(iter);
            uint64_t hash = HashPixbuf(pix);
            if (n == 0) {
                firstHash = hash;
            } else if (hash == firstHash) {
                break;
            }
            int delay = gdk_pixbuf_animation_iter_get_delay_time(iter);
            GdkPixbuf* copy = pix ? gdk_pixbuf_copy(pix) : nullptr;
            AppendPixbufFrame(img, copy ? copy : pix, delay);
            if (copy) {
                g_object_unref(copy);
            }
            if (delay < 0) {
                break;
            }
            g_time_val_add(&time, delay * 1000);
            if (!gdk_pixbuf_animation_iter_advance(iter, &time)) {
                break;
            }
        }
        if (iter) {
            g_object_unref(iter);
        }
    }
    G_GNUC_END_IGNORE_DEPRECATIONS
    g_object_unref(loader);
    if (img->frames.len == 0) {
        delete img;
        return nullptr;
    }
    return img;
}

RenderImage* RenderImageFromBgra(PaintApp* pa, const uint8_t* bgra, int w,
                                 int h) {
    (void)pa;
    if (!bgra || w <= 0 || h <= 0 || w > 0x7fffffff / 4 / h) return nullptr;
    cairo_surface_t* surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface) cairo_surface_destroy(surface);
        return nullptr;
    }
    uint8_t* dst = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < h; y++) {
        memcpy(dst + (size_t)y * stride, bgra + (size_t)y * w * 4,
               (size_t)w * 4);
    }
    cairo_surface_mark_dirty(surface);
    auto* img = new RenderImage();
    img->generation = PaintResourceGenerationNew();
    LinuxImageFrame frame = {};
    frame.surface = surface;
    frame.w = w;
    frame.h = h;
    VecAppend(img->frames, frame);
    return img;
}

RenderImage* RenderImageNewLoading() {
    auto* img = new RenderImage();
    img->generation = PaintResourceGenerationNew();
    img->status = RenderImageStatus::Loading;
    return img;
}

void RenderImageComplete(RenderImage* img, RenderImage* decoded) {
    if (!img) {
        if (decoded) {
            RenderImageRelease(decoded);
        }
        return;
    }
    if (decoded && decoded->frames.len > 0) {
        for (int i = 0; i < img->frames.len; i++) {
            LinuxFrameFree(&img->frames[i]);
        }
        VecReset(img->frames);
        img->frames.els = decoded->frames.els;
        img->frames.len = decoded->frames.len;
        img->frames.cap = decoded->frames.cap;
        decoded->frames.els = nullptr;
        decoded->frames.len = 0;
        decoded->frames.cap = 0;
        img->status = RenderImageStatus::Ready;
    } else {
        img->status = RenderImageStatus::Failed;
    }
    if (decoded) {
        RenderImageRelease(decoded);
    }
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
        LinuxFrameFree(&img->frames[i]);
    }
    VecReset(img->frames);
    delete img;
}

uint64_t RenderImageGeneration(const RenderImage* img) {
    return img ? img->generation : 0;
}

RenderImageStatus RenderImageStatusGet(const RenderImage* img) {
    return img ? img->status : RenderImageStatus::Failed;
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

static cairo_surface_t* ImageSurface(LinuxImageFrame* frame, bool grayscale) {
    if (!frame || !frame->surface) {
        return nullptr;
    }
    if (!grayscale || frame->graySurface) {
        return grayscale ? frame->graySurface : frame->surface;
    }
    cairo_surface_flush(frame->surface);
    unsigned char* src = cairo_image_surface_get_data(frame->surface);
    int srcStride = cairo_image_surface_get_stride(frame->surface);
    cairo_surface_t* gray =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, frame->w, frame->h);
    if (!src || cairo_surface_status(gray) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(gray);
        return nullptr;
    }
    unsigned char* dst = cairo_image_surface_get_data(gray);
    int dstStride = cairo_image_surface_get_stride(gray);
    for (int y = 0; y < frame->h; y++) {
        for (int x = 0; x < frame->w; x++) {
            unsigned char* s = src + y * srcStride + x * 4;
            unsigned char* d = dst + y * dstStride + x * 4;
            uint8_t v = (uint8_t)(((uint32_t)s[2] * 54 + (uint32_t)s[1] * 183 +
                                   (uint32_t)s[0] * 19) >>
                                  8);
            d[0] = v;
            d[1] = v;
            d[2] = v;
            d[3] = s[3];
        }
    }
    cairo_surface_mark_dirty(gray);
    frame->graySurface = gray;
    return gray;
}

void RenderImageDraw(PaintCtx* ctx, RenderImage* img, Bounds bounds,
                     Bounds imageBounds, int frameIndex, float radius,
                     bool grayscale) {
    if (scene::Recording()) {
        scene::RecImageDraw(ctx, img, bounds, imageBounds, frameIndex, radius,
                            grayscale);
        return;
    }
    cairo_t* cr = Cr(ctx);
    if (!cr || !img || img->frames.len <= 0 || bounds.w <= 0 || bounds.h <= 0 ||
        imageBounds.w <= 0 || imageBounds.h <= 0) {
        return;
    }
    if (frameIndex < 0 || frameIndex >= img->frames.len) {
        frameIndex = 0;
    }
    LinuxImageFrame* frame = &img->frames[frameIndex];
    cairo_surface_t* surface = ImageSurface(frame, grayscale);
    if (!surface || frame->w <= 0 || frame->h <= 0) {
        return;
    }
    cairo_save(cr);
    if (radius > 0) {
        // The rounded box is in the caller's coordinates, so it goes on
        // before the scale that maps the picture onto it.
        float half = (bounds.w < bounds.h ? bounds.w : bounds.h) * 0.5f;
        double r = radius > half ? half : radius;
        cairo_new_path(cr);
        cairo_arc(cr, bounds.x + bounds.w - r, bounds.y + r, r, -kPi / 2, 0);
        cairo_arc(cr, bounds.x + bounds.w - r, bounds.y + bounds.h - r, r, 0,
                  kPi / 2);
        cairo_arc(cr, bounds.x + r, bounds.y + bounds.h - r, r, kPi / 2, kPi);
        cairo_arc(cr, bounds.x + r, bounds.y + r, r, kPi, kPi * 1.5);
        cairo_close_path(cr);
        cairo_clip(cr);
    } else {
        cairo_rectangle(cr, bounds.x, bounds.y, bounds.w, bounds.h);
        cairo_clip(cr);
    }
    cairo_translate(cr, imageBounds.x, imageBounds.y);
    cairo_scale(cr, imageBounds.w / (double)frame->w,
                imageBounds.h / (double)frame->h);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_rectangle(cr, 0, 0, frame->w, frame->h);
    cairo_clip(cr);
    cairo_paint_with_alpha(cr, ctx->opacity < 0 ? 0 : ctx->opacity);
    cairo_restore(cr);
}

// ─── shaped text ──────────────────────────────────────────────────────────
//
// Pango indexes by UTF-8 byte, which is what Str carries, so unlike the
// DirectWrite backend nothing has to convert offsets.

struct TextLayout {
    uint64_t generation = 0;
    PangoLayout* layout = nullptr;
    // What TextLayoutNew reported, kept so TextLayoutSize can answer without
    // measuring again.
    Size size = {};
    int refs = 1;
    // GPUI's line box (fontSize * phi) and what Pango would use on its own.
    float box = 0;
    float natural = 0;
    int lines = 1;
};

// DirectWrite gets "Segoe UI" and "Consolas"; fontconfig resolves these two
// generic families to whatever the distro ships.
static const char* kSans = "Sans";
static const char* kMono = "Monospace";

static PangoWeight PangoWeightFor(uint8_t weight, float fontSize) {
    switch (weight & kFontWeightMask) {
        case kFontWeightThin:
            return PANGO_WEIGHT_THIN;
        case kFontWeightExtraLight:
            return PANGO_WEIGHT_ULTRALIGHT;
        case kFontWeightLight:
            return PANGO_WEIGHT_LIGHT;
        case kFontWeightExplicitNormal:
            return PANGO_WEIGHT_NORMAL;
        case kFontWeightMedium:
            return PANGO_WEIGHT_MEDIUM;
        case kFontWeightSemibold:
            return PANGO_WEIGHT_SEMIBOLD;
        case kFontWeightBold:
            return PANGO_WEIGHT_BOLD;
        case kFontWeightExtraBold:
            return PANGO_WEIGHT_ULTRABOLD;
        case kFontWeightBlack:
            return PANGO_WEIGHT_HEAVY;
        default:
            break;
    }
    // The 20 px and 24 px DirectWrite formats are created semibold, and a run
    // that asks for no weight of its own inherits that. Match it.
    return fontSize >= 18.f ? PANGO_WEIGHT_SEMIBOLD : PANGO_WEIGHT_NORMAL;
}

TextLayout* TextLayoutNew(PaintCtx* ctx, Str s, float fontSize, float maxW,
                          bool wrap, uint8_t weight, float lineH,
                          Size* outSize) {
    if (!ctx || !ctx->pa || !ctx->pa->pango || !s.s || len(s) <= 0) {
        return nullptr;
    }
    if (fontSize <= 0) {
        fontSize = 16.f;
    }
    PangoLayout* l = pango_layout_new(ctx->pa->pango);
    if (!l) {
        return nullptr;
    }
    PangoFontDescription* fd = pango_font_description_new();
    pango_font_description_set_family(fd, (weight & kFontMono) ? kMono : kSans);
    pango_font_description_set_weight(fd, PangoWeightFor(weight, fontSize));
    if (weight & kFontItalic) {
        pango_font_description_set_style(fd, PANGO_STYLE_ITALIC);
    }
    pango_font_description_set_absolute_size(fd,
                                             (double)fontSize * PANGO_SCALE);
    pango_layout_set_font_description(l, fd);
    pango_font_description_free(fd);

    if (weight & (kFontUnderline | kFontStrike)) {
        PangoAttrList* attrs = pango_attr_list_new();
        if (weight & kFontUnderline) {
            pango_attr_list_insert(
                attrs, pango_attr_underline_new(PANGO_UNDERLINE_SINGLE));
        }
        if (weight & kFontStrike) {
            pango_attr_list_insert(attrs, pango_attr_strikethrough_new(TRUE));
        }
        pango_layout_set_attributes(l, attrs);
        pango_attr_list_unref(attrs);
    }

    pango_layout_set_text(l, s.s, len(s));
    if (wrap && maxW > 0) {
        pango_layout_set_width(l, (int)(maxW * PANGO_SCALE));
        pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
    } else {
        pango_layout_set_width(l, -1);
    }

    auto* tl = new TextLayout();
    tl->generation = PaintResourceGenerationNew();
    tl->layout = l;
    tl->lines = pango_layout_get_line_count(l);
    if (tl->lines < 1) {
        tl->lines = 1;
    }
    int pw = 0, ph = 0;
    pango_layout_get_pixel_size(l, &pw, &ph);
    tl->natural = (float)ph / (float)tl->lines;
    tl->box = fontSize * (lineH > 0 ? lineH : kLineHeight);
    // Pango's spacing only goes between lines, so the extra half-box above the
    // first line and below the last is added by the caller-visible height and
    // by the draw offset in TextLayoutDraw.
    if (tl->lines > 1) {
        pango_layout_set_spacing(l,
                                 (int)((tl->box - tl->natural) * PANGO_SCALE));
        pango_layout_get_pixel_size(l, &pw, &ph);
    }
    tl->size = Size{(float)pw, tl->box * (float)tl->lines};
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
    if (tl->layout) {
        g_object_unref(tl->layout);
    }
    delete tl;
}

uint64_t TextLayoutGeneration(const TextLayout* tl) {
    return tl ? tl->generation : 0;
}

// Where the glyphs sit inside the phi-tall line box.
static float BoxPad(TextLayout* tl) {
    return (tl->box - tl->natural) * 0.5f;
}

bool PaintTextLayoutSpans(PaintCtx* ctx, TextLayout* tl, Str text, float x,
                          float y, Rgba base, const TextSpan* spans, int n) {
    if (scene::Recording()) {
        return scene::RecTextDrawSpans(ctx, tl, text, x, y, base, spans, n);
    }
    (void)tl;
    (void)text;
    (void)x;
    (void)y;
    (void)base;
    (void)spans;
    (void)n;
    return false;
}

void TextLayoutDraw(PaintCtx* ctx, TextLayout* tl, float x, float y, Rgba c,
                    bool clip, float clipW) {
    if (scene::Recording()) {
        scene::RecTextDraw(ctx, tl, x, y, c, clip, clipW);
        return;
    }
    cairo_t* cr = Cr(ctx);
    if (!cr || !tl || !tl->layout) {
        return;
    }
    // GPUI's `truncate()` is `text_overflow: Ellipsis`. Pango does the whole
    // of it — a width to cut at and an ellipsize mode — and both are set here
    // rather than when the run was shaped, because a non-wrapping run is
    // shaped unconstrained and does not know the box it lands in.
    bool ellipsized = false;
    if (clip && clipW > 0) {
        pango_layout_set_width(tl->layout, (int)(clipW * PANGO_SCALE));
        pango_layout_set_ellipsize(tl->layout, PANGO_ELLIPSIZE_END);
        ellipsized = true;
    }
    if (clip) {
        int pw = 0, ph = 0;
        pango_layout_get_pixel_size(tl->layout, &pw, &ph);
        int w = pango_layout_get_width(tl->layout);
        float boxW = w > 0 ? (float)w / PANGO_SCALE : (float)pw;
        // The width is the cut; the height is not. A descender's ink runs
        // below a line box of relative 1.0, and clipping to it is what hid
        // the tails of "g" and "y" — the `overflow_hidden` upstream dropped
        // while keeping the ellipsis.
        float boxH = tl->box * (float)tl->lines;
        cairo_save(cr);
        cairo_rectangle(cr, x, y - boxH, boxW, boxH * 3.f);
        cairo_clip(cr);
    }
    SetColor(ctx, cr, c);
    cairo_move_to(cr, x, y + BoxPad(tl));
    pango_cairo_show_layout(cr, tl->layout);
    if (clip) {
        cairo_restore(cr);
    }
    // The layout is cached and may be drawn again somewhere that does not
    // truncate, so it goes back the way it was found.
    if (ellipsized) {
        pango_layout_set_ellipsize(tl->layout, PANGO_ELLIPSIZE_NONE);
        pango_layout_set_width(tl->layout, -1);
    }
}

int TextLayoutHitPoint(TextLayout* tl, Str s, float relX, float relY) {
    if (!tl || !tl->layout) {
        return 0;
    }
    int index = 0;
    int trailing = 0;
    int px = (int)(relX * PANGO_SCALE);
    bool inside = pango_layout_xy_to_index(
        tl->layout, px, (int)((relY - BoxPad(tl)) * PANGO_SCALE), &index,
        &trailing);
    // Outside a wrapped row Pango returns the nearest glyph's leading edge,
    // not the nearest caret edge: a click past the right side can therefore
    // land one character before the wrap. Clamp x against the visual line
    // Pango selected from y, matching DirectWrite/CoreText hit testing.
    if (!inside) {
        int lineNo = 0;
        int lineX = 0;
        pango_layout_index_to_line_x(tl->layout, index, FALSE, &lineNo, &lineX);
        PangoLayoutLine* line =
            pango_layout_get_line_readonly(tl->layout, lineNo);
        if (line) {
            PangoRectangle logical = {};
            pango_layout_line_get_extents(line, nullptr, &logical);
            if (px <= logical.x) {
                index = line->start_index;
                trailing = 0;
            } else if (px >= logical.x + logical.width) {
                index = line->start_index + line->length;
                trailing = 0;
            }
        }
    }
    // `trailing` counts characters past `index` the point fell after.
    const char* text = pango_layout_get_text(tl->layout);
    while (trailing > 0 && text && text[index]) {
        index = (int)(g_utf8_next_char(text + index) - text);
        trailing--;
    }
    if (index < 0) {
        index = 0;
    }
    if (index > len(s)) {
        index = len(s);
    }
    return index;
}

float TextLayoutBaseline(TextLayout* tl) {
    if (!tl || !tl->layout) {
        return 0;
    }
    // pango_layout_get_baseline is measured from the layout's top, and the
    // rect a decoration hangs off starts a half-leading lower.
    float fromTop = (float)pango_layout_get_baseline(tl->layout) / PANGO_SCALE;
    return fromTop - BoxPad(tl);
}

int TextLayoutRangeRects(TextLayout* tl, Str s, int u8a, int u8b, Bounds* out,
                         int max) {
    if (!tl || !tl->layout || !out || max <= 0 || u8a >= u8b) {
        return 0;
    }
    (void)s;
    float pad = BoxPad(tl);
    int n = 0;
    PangoLayoutIter* iter = pango_layout_get_iter(tl->layout);
    if (!iter) {
        return 0;
    }
    do {
        PangoLayoutLine* line = pango_layout_iter_get_line_readonly(iter);
        if (!line) {
            continue;
        }
        int lineStart = line->start_index;
        int lineEnd = lineStart + line->length;
        int lo = u8a > lineStart ? u8a : lineStart;
        int hi = u8b < lineEnd ? u8b : lineEnd;
        if (lo >= hi) {
            continue;
        }
        int x0 = 0, x1 = 0;
        pango_layout_line_index_to_x(line, lo, FALSE, &x0);
        pango_layout_line_index_to_x(line, hi, FALSE, &x1);
        int y0 = 0, y1 = 0;
        pango_layout_iter_get_line_yrange(iter, &y0, &y1);
        float left = (float)x0 / PANGO_SCALE;
        float right = (float)x1 / PANGO_SCALE;
        if (right < left) {
            float t = left;
            left = right;
            right = t;
        }
        out[n].x = left;
        out[n].y = (float)y0 / PANGO_SCALE + pad;
        out[n].w = right - left;
        out[n].h = (float)(y1 - y0) / PANGO_SCALE;
        n++;
    } while (n < max && pango_layout_iter_next_line(iter));
    pango_layout_iter_free(iter);
    return n;
}

} // namespace gpui
