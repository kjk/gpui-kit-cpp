/* The halves of the paint backends that do not depend on the OS; see
   paint_backend.h. */

#include "gpui/paint_backend.h"
#include "gpui/scene.h"

namespace gpui {

static uint64_t gNextPaintResourceGeneration = 1;

uint64_t PaintResourceGenerationNew() {
    uint64_t id = gNextPaintResourceGeneration++;
    if (id == 0) {
        id = gNextPaintResourceGeneration++;
    }
    return id;
}

bool PaintSceneFinish(PaintCtx* ctx, bool replayUnchanged) {
    if (!SceneOn() || !scene::Recording()) {
        return true;
    }
    Bounds damage = {};
    bool draw = scene::FrameEnd(ctx, &damage);
    if (draw) {
        if (!scene::ReplayScroll(ctx)) {
            scene::Replay(ctx, &damage);
        }
    } else if (replayUnchanged) {
        scene::Replay(ctx, nullptr);
    }
    return draw;
}

void PaintCopyBgraRows(uint8_t* dst, size_t dstStride, const uint8_t* src,
                       size_t srcStride, int w, int h) {
    for (int y = 0; y < h; y++) {
        memcpy(dst + (size_t)y * dstStride, src + (size_t)y * srcStride,
               (size_t)w * 4);
    }
}

void PaintGrayBgraRows(uint8_t* dst, size_t dstStride, const uint8_t* src,
                       size_t srcStride, int w, int h) {
    for (int y = 0; y < h; y++) {
        const uint8_t* s = src + (size_t)y * srcStride;
        uint8_t* d = dst + (size_t)y * dstStride;
        for (int x = 0; x < w; x++, s += 4, d += 4) {
            uint8_t v = RgbLuma709(s[2], s[1], s[0]);
            uint8_t a = s[3];
            d[0] = v;
            d[1] = v;
            d[2] = v;
            d[3] = a;
        }
    }
}

#if GPUI_PAINT_IN_TREE

// ─── fonts ────────────────────────────────────────────────────────────────

static Vec<Str> gInstalledFonts;
static bool gInstalledFontsReady = false;

const Str* PaintInstalledFontNames(PaintApp* pa, int* n) {
    if (!gInstalledFontsReady) {
        gInstalledFontsReady = true;
        PaintListInstalledFonts(pa, &gInstalledFonts);
    }
    if (n) {
        *n = len(gInstalledFonts);
    }
    return gInstalledFonts.els;
}

// ─── text layouts ─────────────────────────────────────────────────────────

static TextLayoutHead* TextLayoutHeadOf(TextLayout* tl) {
    return reinterpret_cast<TextLayoutHead*>(tl);
}

static const TextLayoutHead* TextLayoutHeadOf(const TextLayout* tl) {
    return reinterpret_cast<const TextLayoutHead*>(tl);
}

void TextLayoutAddRef(TextLayout* tl) {
    if (tl) {
        TextLayoutHeadOf(tl)->refs++;
    }
}

void TextLayoutRelease(TextLayout* tl) {
    if (!tl || --TextLayoutHeadOf(tl)->refs > 0) {
        return;
    }
    TextLayoutDestroy(tl);
}

uint64_t TextLayoutGeneration(const TextLayout* tl) {
    return tl ? TextLayoutHeadOf(tl)->generation : 0;
}

int TextLayoutTextLen(const TextLayout* tl) {
    return tl ? TextLayoutHeadOf(tl)->textLen : 0;
}

// ─── images ───────────────────────────────────────────────────────────────

RenderImage* RenderImageAlloc(RenderImageStatus status) {
    auto* img = new RenderImage();
    img->generation = PaintResourceGenerationNew();
    img->status = status;
    return img;
}

RenderImage* RenderImageNewLoading() {
    return RenderImageAlloc(RenderImageStatus::Loading);
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
    RenderImageDropNative(img);
#if GPUI_PAINT_IMAGE_FRAMES
    RenderImageFramesFree(img);
#endif
    delete img;
}

uint64_t RenderImageGeneration(const RenderImage* img) {
    return img ? img->generation : 0;
}

#endif // GPUI_PAINT_IN_TREE

#if GPUI_PAINT_IMAGE_FRAMES

int RenderImageFrameIndex(const RenderImage* img, int frameIndex) {
    if (!img || len(img->frames) <= 0) {
        return -1;
    }
    if (frameIndex < 0 || frameIndex >= len(img->frames)) {
        return 0;
    }
    return frameIndex;
}

RenderImage* RenderImageWithFrame(const ImageFrame& frame) {
    RenderImage* img = RenderImageAlloc(RenderImageStatus::Ready);
    VecAppend(img->frames, frame);
    return img;
}

void RenderImageFramesFree(RenderImage* img) {
    for (int i = 0; i < len(img->frames); i++) {
        ImageFrameFree(&img->frames[i]);
    }
    VecReset(img->frames);
}

// A Loading handle takes over the frames of what was decoded for it (or
// turns Failed when nothing was), and the decoded image is released.
void RenderImageComplete(RenderImage* img, RenderImage* decoded) {
    if (!img) {
        if (decoded) {
            RenderImageRelease(decoded);
        }
        return;
    }
    if (decoded && len(decoded->frames) > 0) {
        RenderImageFramesFree(img);
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

RenderImageStatus RenderImageStatusGet(const RenderImage* img) {
    return img ? img->status : RenderImageStatus::Failed;
}

Size RenderImageSizePx(const RenderImage* img, int frameIndex) {
    int i = RenderImageFrameIndex(img, frameIndex);
    if (i < 0) {
        return {};
    }
    return {(float)img->frames[i].w, (float)img->frames[i].h};
}

int RenderImageFrameCount(const RenderImage* img) {
    return img ? len(img->frames) : 0;
}

int RenderImageFrameDurationMs(const RenderImage* img, int frameIndex) {
    int i = RenderImageFrameIndex(img, frameIndex);
    return i < 0 ? 0 : img->frames[i].durationMs;
}

#endif // GPUI_PAINT_IMAGE_FRAMES

#if GPUI_PAINT_DIRECT

// ─── what the direct backends share ───────────────────────────────────────

static PathHead* PathHeadOf(Path* p) {
    return reinterpret_cast<PathHead*>(p);
}

void PaintTargetFree(PaintCtx* ctx) {
    if (!ctx || !ctx->rt) {
        return;
    }
    PaintTargetDestroy(ctx->rt);
    ctx->rt = nullptr;
}

bool PaintTargetReplace(PaintCtx* ctx, void* native) {
    if (!ctx || !ctx->pa || !native) {
        return false;
    }
    PaintTargetFree(ctx);
    return true;
}

TextLayout* TextLayoutNew(PaintCtx* ctx, Str s, float fontSize, float maxW,
                          bool wrap, uint16_t weight, float lineH,
                          Size* outSize, TextAlign align) {
    if (!ctx || !ctx->pa || !s.s || len(s) <= 0) {
        return nullptr;
    }
    if (fontSize <= 0) {
        fontSize = 16.f;
    }
    TextLayout* tl = TextLayoutShape(ctx, s, fontSize, maxW, wrap, weight,
                                     lineH, outSize, align);
    if (tl) {
        TextLayoutHeadOf(tl)->textLen = len(s);
        TextLayoutHeadOf(tl)->generation = PaintResourceGenerationNew();
    }
    return tl;
}

Size TextLayoutSize(TextLayout* tl) {
    return tl ? TextLayoutHeadOf(tl)->cover : Size{0, 0};
}

// cairo, Core Graphics and Canvas2D cannot colour spans of one shaped run:
// the scene records the spans when it is recording (and replays them as
// plain runs), and otherwise the caller is told to fall back.
bool PaintTextLayoutSpans(PaintCtx* ctx, TextLayout* tl, Str text, float x,
                          float y, Rgba base, const TextSpan* spans, int n) {
    if (scene::Recording()) {
        return scene::RecTextDrawSpans(ctx, tl, text, x, y, base, spans, n);
    }
    return false;
}

Path* PathNew(PaintCtx* ctx, bool winding) {
    if (scene::Recording()) {
        return scene::RecPathNew(ctx, winding);
    }
    if (!ctx) {
        return nullptr;
    }
    Path* p = PathAlloc();
    PathHeadOf(p)->winding = winding;
    return p;
}

void PathFree(Path* p) {
    if (scene::Recording()) {
        scene::RecPathFree(p);
        return;
    }
    PathDestroy(p);
}

void PathMoveTo(Path* p, float x, float y) {
    if (scene::Recording()) {
        scene::RecPathMoveTo(p, x, y);
        return;
    }
    if (!p) {
        return;
    }
    PathEmitMove(p, x, y);
    PathHeadOf(p)->fig = true;
}

void PathLineTo(Path* p, float x, float y) {
    if (scene::Recording()) {
        scene::RecPathLineTo(p, x, y);
        return;
    }
    if (!p) {
        return;
    }
    if (!PathHeadOf(p)->fig) {
        PathMoveTo(p, x, y);
        return;
    }
    PathEmitLine(p, x, y);
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
    if (!PathHeadOf(p)->fig) {
        PathMoveTo(p, x, y);
        return;
    }
    PathEmitCubic(p, x1, y1, x2, y2, x, y);
}

// An arc opens a figure the way a move does: every backend draws a line from
// the current point to the arc's start.
void PathArcTo(Path* p, float cx, float cy, float r, float a0, float a1,
               bool clockwise) {
    if (scene::Recording()) {
        scene::RecPathArcTo(p, cx, cy, r, a0, a1, clockwise);
        return;
    }
    if (!p) {
        return;
    }
    PathEmitArc(p, cx, cy, r, a0, a1, clockwise);
    PathHeadOf(p)->fig = true;
}

void PathClose(Path* p) {
    if (scene::Recording()) {
        scene::RecPathClose(p);
        return;
    }
    if (!p || !PathHeadOf(p)->fig) {
        return;
    }
    PathEmitClose(p);
    PathHeadOf(p)->fig = false;
}

#endif // GPUI_PAINT_DIRECT

} // namespace gpui
