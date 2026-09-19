/* The scene: recording, ordering, hashing, diffing and replay. Portable —
   nothing here names an OS or a GPU type, because the replay draws by calling
   the same Paint.h entry points the element tree would have called, and a
   backend cannot tell the difference. See scene.h for what it is for and what
   it is short of.

   Each paint backend dispatches into the recorder with the same one line at
   the top of each Paint.h entry point. */

#include "gpui/scene.h"

#include <string.h>

namespace gpui {

static int gSceneLevel = kSceneSkip;

int SceneLevelOn() {
#if GPUI_OS_WINDOWS
    static_assert((int)WinSceneMode::Off == kSceneOff);
    static_assert((int)WinSceneMode::Replay == kSceneReplay);
    static_assert((int)WinSceneMode::Cache == kSceneCache);
    static_assert((int)WinSceneMode::Skip == kSceneSkip);
    static_assert((int)WinSceneMode::Damage == kSceneDamage);
    return (int)WinPaintOptionsGet().scene;
#else
    return gSceneLevel;
#endif
}

bool SceneTakeArg(Str arg) {
    const Str prefix = StrL("__scene=");
    if (!base::StrStartsWith(arg, prefix)) {
        return false;
    }
    Str value(arg.s + len(prefix), len(arg) - len(prefix));
    if (base::StrEqI(value, "off")) {
        gSceneLevel = kSceneOff;
    } else if (base::StrEqI(value, "replay")) {
        gSceneLevel = kSceneReplay;
    } else if (base::StrEqI(value, "cache")) {
        gSceneLevel = kSceneCache;
    } else if (base::StrEqI(value, "skip")) {
        gSceneLevel = kSceneSkip;
    } else if (base::StrEqI(value, "damage")) {
        gSceneLevel = kSceneDamage;
    }
    return true;
}

namespace scene {

// ─── the recorded frame ──────────────────────────────────────────────────

enum PrimKind : uint8_t {
    kPClear = 0,
    kPRect,
    kPRound,
    kPStrokeRound,
    kPLine,
    kPEllipse,
    kPPathFill,
    kPPathGradient,
    kPPathStroke,
    kPImage,
    kPText,
    kPTextSpans
};

enum PrimFlag : uint8_t {
    kFDash = 1,      // e2 / e3 hold the dash pattern
    kFRoundCaps = 2, // PathStroke's round caps
    kFClip = 4       // TextLayoutDraw's clip-to-layout-box
};

// One thing to draw. GPUI's primitives are a struct per kind in a vector per
// kind; one POD struct for all of them costs a few floats a primitive and
// keeps the frame in one array, which is what the hash and the diff want.
//
// `mask` is the content mask, already intersected down the clip stack, so a
// primitive says where it may draw without reference to any stack. That is
// the property that makes the list reorderable and the replay's clip changes
// rare.
struct Prim {
    uint8_t kind = 0;
    uint8_t layer = 0;
    uint8_t flags = 0;
    uint8_t pad = 0;
    uint32_t seq = 0;
    // Geometry: a rect's x/y/w/h, a line's x1/y1/x2/y2, an ellipse's
    // cx/cy/rx/ry, a text run's x/y.
    float g0 = 0, g1 = 0, g2 = 0, g3 = 0;
    // Extras: radius, stroke width, and either the dash pattern or a
    // gradient's second point.
    float e0 = 0, e1 = 0, e2 = 0, e3 = 0;
    // kPImage's fitted destination. Its geometry above remains the outer
    // element bounds used for clipping and damage.
    Bounds imageBounds = {};
    Bounds mask = {};
    // What this primitive covers, for the damage rectangle. Already clipped
    // to the mask.
    Bounds bbox = {};
    Rgba color = {};
    Rgba color2 = {};
    int32_t path = -1;
    // RenderImage* and TextLayout* are retained by cur; prev holds
    // comparison data only. Custom painters may release a layout as soon
    // as TextLayoutDraw returns, before the scene replays it.
    void* ref = nullptr;
    // Monotonic identity from the resource, unlike an address that an
    // allocator may hand to a different resource after this frame.
    uint64_t resourceGeneration = 0;
    uint64_t hash = 0;
};

enum PathVerb : uint8_t {
    kVMove = 0, // 2 floats
    kVLine,     // 2
    kVCubic,    // 6
    kVArc,      // 5 + a direction in the low bit of the verb's high nibble
    kVClose     // 0
};

// A path as verbs and points rather than as a backend object, which is what
// lets it be hashed and so cached. The arc direction rides in `dirs` because
// a verb byte is a kind and nothing else.
struct PathRec {
    int verbFirst = 0, verbCount = 0;
    int ptFirst = 0, ptCount = 0;
    bool winding = false;
    bool hashed = false;
    uint64_t hash = 0;
    uint64_t cacheHash = 0;
    // First coordinate in the path. Cache geometry is built relative to it;
    // the primitive hash and damage bounds remain in absolute frame space.
    float originX = 0, originY = 0;
    // Grown as points arrive; empty until the first one.
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool any = false;
};

// The damage of the last frames that were actually presented. The swap chain
// is FLIP_SEQUENTIAL with three buffers, so the buffer a frame is handed back
// holds what was drawn three presents ago: a partial redraw has to cover
// everything that has changed since then, not only what changed this frame.
static const int kBufferDepth = 3;

struct CacheEntry {
    uint64_t hash = 0;
    Path* path = nullptr;
    int lastFrame = 0;
    bool live = false;
};

static const int kCacheSlots = 2048;

struct MaskEntry {
    uint64_t hash = 0;
    RenderImage* image = nullptr;
    Bounds bounds = {};
    int bytes = 0;
    int lastFrame = 0;
};
static const int kMaskSlots = 128;
static const int kMaskBudget = 8 * 1024 * 1024;

struct HashBag {
    Vec<uint64_t> keys;
    Vec<int> counts;
    int mask = 0;
};

// The current frame owns text and foreground ranges in textArena; previous
// primitives keep only the resulting hash and bounds.
struct TextRec {
    Str text;
    TextSpan* spans = nullptr;
    int count = 0;
    uint64_t hash = 0;
};

// Each context owns an ordered stream of its own primitives and child
// contexts. Sorting those child entries never lets a grandchild escape its
// parent, unlike sorting all primitives by one flat layer byte.
struct StackContext {
    int parent = -1;
    int first = -1;
    int last = -1;
    int rank = 0;
};
struct StackEntry {
    int next = -1;
    int context = -1;
    int prim = -1;
    int z = 0;
};

struct State {
    Vec<Prim> cur;
    Vec<Prim> prev;
    Vec<StackContext> contexts;
    Vec<StackEntry> entries;
    int currentContext = 0;
    Vec<PathRec> paths;
    Vec<uint8_t> verbs;
    Vec<float> pts;
    Arena* textArena = nullptr;
    Vec<TextRec> texts;
    // Four floats per level, mirroring the GPU backend's clip stack.
    Vec<float> clipStack;
    Bounds clip = {};
    bool recording = false;
    uint32_t seq = 0;
    float viewW = 0, viewH = 0;
    bool skipPresent = false;
    SceneStats stats;
    uint64_t prevFrameHash = 0;
    bool havePrev = false;
    int frameNo = 0;
    Bounds damageRing[kBufferDepth] = {};
    int damageRingAt = 0;
    CacheEntry cache[kCacheSlots] = {};
    CacheEntry sweepBuf[kCacheSlots] = {};
    int cacheLive = 0;
    MaskEntry masks[kMaskSlots] = {};
    int maskBytes = 0;
    HashBag bagA;
    HashBag bagB;
};

// Paint.h's path-building calls after PathNew carry only Path*, not PaintCtx*.
// Painting is single-threaded, so this non-owning pointer identifies the open
// recorder for those calls. All persistent data lives in the PaintCtx-owned
// State; switching windows switches this pointer at FrameBegin.
static State* gActive = nullptr;

#define gCur (gActive->cur)
#define gPrev (gActive->prev)
#define gContexts (gActive->contexts)
#define gEntries (gActive->entries)
#define gPaths (gActive->paths)
#define gVerbs (gActive->verbs)
#define gPts (gActive->pts)
#define gClipStack (gActive->clipStack)
#define gClip (gActive->clip)
#define gRecording (gActive->recording)
#define gSeq (gActive->seq)
#define gViewW (gActive->viewW)
#define gViewH (gActive->viewH)
#define gSkipPresent (gActive->skipPresent)
#define gStats (gActive->stats)
#define gPrevFrameHash (gActive->prevFrameHash)
#define gHavePrev (gActive->havePrev)
#define gFrameNo (gActive->frameNo)
#define gDamageRing (gActive->damageRing)
#define gDamageRingAt (gActive->damageRingAt)
#define gCache (gActive->cache)
#define gSweepBuf (gActive->sweepBuf)
#define gCacheLive (gActive->cacheLive)
#define gMasks (gActive->masks)
#define gMaskBytes (gActive->maskBytes)
#define gBagA (gActive->bagA)
#define gBagB (gActive->bagB)

static State* StateFor(PaintCtx* ctx, bool create) {
    if (!ctx) {
        return nullptr;
    }
    if (!ctx->sceneState && create) {
        ctx->sceneState = new State();
    }
    return ctx->sceneState;
}

bool Recording() {
    return gActive && gActive->recording;
}
static void AppendEntry(int parent, StackEntry entry) {
    int ix = gEntries.len;
    VecAppend(gEntries, entry);
    StackContext& c = gContexts[parent];
    if (c.last >= 0) {
        gEntries[c.last].next = ix;
    } else {
        c.first = ix;
    }
    c.last = ix;
}

int ContextPush(PaintCtx* ctx, int z) {
    if (!Recording() || !ctx || ctx->sceneState != gActive) return -1;
    int parent = gActive->currentContext;
    int child = gContexts.len;
    StackContext c;
    c.parent = parent;
    VecAppend(gContexts, c);
    StackEntry e;
    e.context = child;
    e.z = z;
    AppendEntry(parent, e);
    gActive->currentContext = child;
    return parent;
}

void ContextPop(PaintCtx* ctx, int parent) {
    if (parent >= 0 && Recording() && ctx && ctx->sceneState == gActive) {
        gActive->currentContext = parent;
    }
}

int CurrentContext(PaintCtx* ctx) {
    return Recording() && ctx && ctx->sceneState == gActive
               ? gActive->currentContext
               : 0;
}
bool SuspendBegin() {
    bool prev = Recording();
    if (gActive) {
        gActive->recording = false;
    }
    return prev;
}
void SuspendEnd(bool prev) {
    if (gActive) {
        gActive->recording = prev;
    }
}
bool SkipPresent(PaintCtx* ctx) {
    State* s = StateFor(ctx, false);
    return s && s->skipPresent;
}
const SceneStats& Stats(PaintCtx* ctx) {
    static const SceneStats empty;
    State* s = StateFor(ctx, false);
    return s ? s->stats : empty;
}

// ─── hashing ─────────────────────────────────────────────────────────────

static inline uint64_t HashBytes(uint64_t h, const void* p, int n) {
    const uint8_t* b = (const uint8_t*)p;
    for (int i = 0; i < n; i++) {
        h ^= b[i];
        h *= 0x100000001b3ull;
    }
    return h;
}
static const uint64_t kHashSeed = 0xcbf29ce484222325ull;

// Everything about a primitive that decides what appears on screen. `seq` and
// `bbox` are left out: the first is where it sat in the list, which the
// position in the list already says, and the second is derived.
// Two floats as one word, so the hash below runs eight bytes at a time.
static inline uint64_t Pair(float a, float b) {
    uint32_t x = 0, y = 0;
    memcpy(&x, &a, 4);
    memcpy(&y, &b, 4);
    return ((uint64_t)y << 32) | x;
}

// Everything about a primitive that decides what appears on screen. `seq` and
// `bbox` are left out: the first is where it sat in the list, which the
// position in the list already says, and the second is derived.
//
// Ten words rather than the hundred-odd bytes they occupy, because this runs
// over every primitive of every frame, and a byte-at-a-time FNV over a whole
// scene is a measurable share of what the scene costs — most of it on a frame
// where nothing can be cached, which is the one case where all of this is a
// loss rather than a win.
static uint64_t HashPrim(const Prim& p) {
    uint64_t w[12];
    w[0] =
        (uint64_t)p.kind | ((uint64_t)p.layer << 8) | ((uint64_t)p.flags << 16);
    w[1] = Pair(p.g0, p.g1);
    w[2] = Pair(p.g2, p.g3);
    w[3] = Pair(p.e0, p.e1);
    w[4] = Pair(p.e2, p.e3);
    w[5] = Pair(p.imageBounds.x, p.imageBounds.y);
    w[6] = Pair(p.imageBounds.w, p.imageBounds.h);
    w[7] = Pair(p.mask.x, p.mask.y);
    w[8] = Pair(p.mask.w, p.mask.h);
    uint32_t c0 = 0, c1 = 0;
    memcpy(&c0, &p.color, 4);
    memcpy(&c1, &p.color2, 4);
    w[9] = ((uint64_t)c1 << 32) | c0;
    w[10] = p.resourceGeneration;
    w[11] = p.kind == kPTextSpans                  ? gActive->texts[p.path].hash
            : (p.path >= 0 && p.path < gPaths.len) ? gPaths[p.path].hash
                                                   : 0;
    uint64_t h = kHashSeed;
    for (int i = 0; i < 12; i++) {
        h ^= w[i];
        h *= 0x100000001b3ull;
        h ^= h >> 29;
    }
    return h;
}

static uint64_t HashPath(PathRec& pr) {
    if (pr.hashed) {
        return pr.hash;
    }
    uint64_t h = kHashSeed;
    uint64_t relative = kHashSeed;
    uint8_t w = pr.winding ? 1 : 0;
    h = HashBytes(h, &w, 1);
    relative = HashBytes(relative, &w, 1);
    if (pr.verbCount > 0) {
        h = HashBytes(h, &gVerbs[pr.verbFirst], pr.verbCount);
        relative = HashBytes(relative, &gVerbs[pr.verbFirst], pr.verbCount);
    }
    if (pr.ptCount > 0) {
        h = HashBytes(h, &gPts[pr.ptFirst], pr.ptCount * (int)sizeof(float));
        int vi = pr.verbFirst;
        int pi = pr.ptFirst;
        for (int i = 0; i < pr.verbCount; i++) {
            uint8_t verb = gVerbs[vi++] & 0x7f;
            int pairs = verb == kVCubic ? 3 : verb == kVClose ? 0 : 1;
            for (int pair = 0; pair < pairs; pair++) {
                float x = gPts[pi++] - pr.originX;
                float y = gPts[pi++] - pr.originY;
                relative = HashBytes(relative, &x, (int)sizeof(x));
                relative = HashBytes(relative, &y, (int)sizeof(y));
            }
            if (verb == kVArc) {
                // Radius and angles describe shape, not placement.
                relative =
                    HashBytes(relative, &gPts[pi], 3 * (int)sizeof(float));
                pi += 3;
            }
        }
    }
    pr.hash = h;
    pr.cacheHash = relative;
    pr.hashed = true;
    return h;
}

// ─── bounds helpers ──────────────────────────────────────────────────────

static Bounds Intersect(Bounds a, Bounds b) {
    float x0 = a.x > b.x ? a.x : b.x;
    float y0 = a.y > b.y ? a.y : b.y;
    float x1 = a.Right() < b.Right() ? a.Right() : b.Right();
    float y1 = a.Bottom() < b.Bottom() ? a.Bottom() : b.Bottom();
    Bounds r = {x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0};
    return r;
}

static Bounds Union(Bounds a, Bounds b) {
    if (a.w <= 0 || a.h <= 0) {
        return b;
    }
    if (b.w <= 0 || b.h <= 0) {
        return a;
    }
    float x0 = a.x < b.x ? a.x : b.x;
    float y0 = a.y < b.y ? a.y : b.y;
    float x1 = a.Right() > b.Right() ? a.Right() : b.Right();
    float y1 = a.Bottom() > b.Bottom() ? a.Bottom() : b.Bottom();
    Bounds r = {x0, y0, x1 - x0, y1 - y0};
    return r;
}

static bool SameBounds(Bounds a, Bounds b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

// ─── recording ───────────────────────────────────────────────────────────

static Prim* Emit(PaintCtx* ctx, uint8_t kind, Bounds bbox) {
    Prim p;
    p.kind = kind;
    p.layer = ctx ? (uint8_t)ctx->paintLayer : 0;
    p.seq = gSeq++;
    p.mask = gClip;
    p.bbox = Intersect(bbox, gClip);
    VecAppend(gCur, p);
    StackEntry entry;
    entry.prim = gCur.len - 1;
    entry.z = gActive->currentContext == 0 && ctx ? ctx->paintLayer : 0;
    AppendEntry(gActive->currentContext, entry);
    return &gCur[gCur.len - 1];
}

static void ReleaseResources(State* s) {
    for (Prim& p : s->cur) {
        if (p.kind == kPImage && p.ref) {
            RenderImageRelease((RenderImage*)p.ref);
            p.ref = nullptr;
        } else if ((p.kind == kPText || p.kind == kPTextSpans) && p.ref) {
            TextLayoutRelease((TextLayout*)p.ref);
            p.ref = nullptr;
        }
    }
}

void FrameBegin(PaintCtx* ctx) {
    gActive = StateFor(ctx, true);
    if (!gActive) {
        return;
    }
    gRecording = true;
    gSkipPresent = false;
    ReleaseResources(gActive);
    if (gActive->textArena) gActive->textArena->Reset();
    VecClear(gActive->texts);
    VecClear(gCur);
    VecClear(gContexts);
    VecClear(gEntries);
    VecAppend(gContexts, StackContext{});
    gActive->currentContext = 0;
    VecClear(gPaths);
    VecClear(gVerbs);
    VecClear(gPts);
    VecClear(gClipStack);
    gSeq = 0;
    gViewW = ctx ? ctx->viewW : 0;
    gViewH = ctx ? ctx->viewH : 0;
    // The mask everything starts inside: the view. A primitive whose mask is
    // this one needs no clip at replay.
    gClip = Bounds{0, 0, gViewW, gViewH};
    gStats.clipPushes = 0;
    gStats.culled = 0;
    gStats.framePathCacheHits = 0;
    gStats.framePathCacheMisses = 0;
    gStats.framePathBuildMs = 0;
    gStats.maskCacheHits = 0;
    gStats.maskCacheMisses = 0;
}

void RecClear(PaintCtx* ctx, Rgba c) {
    Prim* p = Emit(ctx, kPClear, Bounds{0, 0, gViewW, gViewH});
    p->color = PaintFade(ctx, c);
}

void RecFillRect(PaintCtx* ctx, float x, float y, float w, float h, Rgba c) {
    Prim* p = Emit(ctx, kPRect, Bounds{x, y, w, h});
    p->g0 = x;
    p->g1 = y;
    p->g2 = w;
    p->g3 = h;
    p->color = PaintFade(ctx, c);
}

void RecFillRound(PaintCtx* ctx, float x, float y, float w, float h, float r,
                  Rgba c) {
    Prim* p = Emit(ctx, kPRound, Bounds{x, y, w, h});
    p->g0 = x;
    p->g1 = y;
    p->g2 = w;
    p->g3 = h;
    p->e0 = r;
    p->color = PaintFade(ctx, c);
}

void RecStrokeRound(PaintCtx* ctx, float x, float y, float w, float h, float r,
                    float stroke, Rgba c, const float* dash) {
    float s = stroke > 0 ? stroke : 0;
    Prim* p =
        Emit(ctx, kPStrokeRound, Bounds{x - s, y - s, w + s * 2, h + s * 2});
    p->g0 = x;
    p->g1 = y;
    p->g2 = w;
    p->g3 = h;
    p->e0 = r;
    p->e1 = stroke;
    if (dash) {
        p->flags |= kFDash;
        p->e2 = dash[0];
        p->e3 = dash[1];
    }
    p->color = PaintFade(ctx, c);
}

void RecLine(PaintCtx* ctx, float x1, float y1, float x2, float y2,
             float stroke, Rgba c, const float* dash) {
    float s = stroke > 0 ? stroke : 1;
    float lo = x1 < x2 ? x1 : x2, hi = x1 > x2 ? x1 : x2;
    float lo2 = y1 < y2 ? y1 : y2, hi2 = y1 > y2 ? y1 : y2;
    Prim* p = Emit(ctx, kPLine,
                   Bounds{lo - s, lo2 - s, hi - lo + s * 2, hi2 - lo2 + s * 2});
    p->g0 = x1;
    p->g1 = y1;
    p->g2 = x2;
    p->g3 = y2;
    p->e1 = stroke;
    if (dash) {
        p->flags |= kFDash;
        p->e2 = dash[0];
        p->e3 = dash[1];
    }
    p->color = PaintFade(ctx, c);
}

void RecEllipse(PaintCtx* ctx, float cx, float cy, float rx, float ry,
                float stroke, Rgba c) {
    float s = stroke > 0 ? stroke : 0;
    Prim* p =
        Emit(ctx, kPEllipse,
             Bounds{cx - rx - s, cy - ry - s, (rx + s) * 2, (ry + s) * 2});
    p->g0 = cx;
    p->g1 = cy;
    p->g2 = rx;
    p->g3 = ry;
    p->e1 = stroke;
    p->color = PaintFade(ctx, c);
}

void RecPushClip(PaintCtx* ctx, float x, float y, float w, float h) {
    (void)ctx;
    gStats.clipPushes++;
    VecAppend(gClipStack, gClip.x);
    VecAppend(gClipStack, gClip.y);
    VecAppend(gClipStack, gClip.w);
    VecAppend(gClipStack, gClip.h);
    gClip = Intersect(gClip, Bounds{x, y, w, h});
}

void RecPopClip(PaintCtx* ctx) {
    (void)ctx;
    if (gClipStack.len < 4) {
        return;
    }
    int n = gClipStack.len;
    gClip = Bounds{gClipStack[n - 4], gClipStack[n - 3], gClipStack[n - 2],
                   gClipStack[n - 1]};
    gClipStack.len -= 4;
}

// ─── recording: paths ────────────────────────────────────────────────────
//
// A recorded Path* is an index, not a pointer: it never reaches a backend,
// because every Paint.h path entry point asks Recording() before doing
// anything. One is added so that index 0 is not a null path.

static Path* PathHandle(int idx) {
    return (Path*)(uintptr_t)(idx + 1);
}
static PathRec* PathOf(Path* p) {
    int idx = (int)(uintptr_t)p - 1;
    if (idx < 0 || idx >= gPaths.len) {
        return nullptr;
    }
    return &gPaths[idx];
}

Path* RecPathNew(PaintCtx* ctx, bool winding) {
    State* s = StateFor(ctx, false);
    if (!s || s != gActive) {
        return nullptr;
    }
    PathRec pr;
    pr.winding = winding;
    pr.verbFirst = gVerbs.len;
    pr.ptFirst = gPts.len;
    VecAppend(gPaths, pr);
    return PathHandle(gPaths.len - 1);
}

void RecPathFree(Path* p) {
    // The geometry has to outlive the call: the primitive that referred to it
    // is still in the list and the replay has not run. It goes with the
    // frame instead.
    (void)p;
}

static void Verb(PathRec* pr, uint8_t v) {
    VecAppend(gVerbs, v);
    pr->verbCount++;
    pr->hashed = false;
}

static void Pt(PathRec* pr, float x, float y) {
    if (!pr->any) {
        pr->originX = x;
        pr->originY = y;
    }
    VecAppend(gPts, x);
    VecAppend(gPts, y);
    pr->ptCount += 2;
    if (!pr->any) {
        pr->any = true;
        pr->x0 = pr->x1 = x;
        pr->y0 = pr->y1 = y;
    }
    pr->x0 = x < pr->x0 ? x : pr->x0;
    pr->y0 = y < pr->y0 ? y : pr->y0;
    pr->x1 = x > pr->x1 ? x : pr->x1;
    pr->y1 = y > pr->y1 ? y : pr->y1;
}

void RecPathMoveTo(Path* p, float x, float y) {
    PathRec* pr = PathOf(p);
    if (!pr) {
        return;
    }
    Verb(pr, kVMove);
    Pt(pr, x, y);
}

void RecPathLineTo(Path* p, float x, float y) {
    PathRec* pr = PathOf(p);
    if (!pr) {
        return;
    }
    Verb(pr, kVLine);
    Pt(pr, x, y);
}

void RecPathCubicTo(Path* p, float x1, float y1, float x2, float y2, float x,
                    float y) {
    PathRec* pr = PathOf(p);
    if (!pr) {
        return;
    }
    Verb(pr, kVCubic);
    Pt(pr, x1, y1);
    Pt(pr, x2, y2);
    Pt(pr, x, y);
}

void RecPathArcTo(Path* p, float cx, float cy, float r, float a0, float a1,
                  bool clockwise) {
    PathRec* pr = PathOf(p);
    if (!pr) {
        return;
    }
    Verb(pr, (uint8_t)(kVArc | (clockwise ? 0x80 : 0)));
    // Five values, and the bounding box of the whole circle: an arc's extent
    // is not its endpoints, and the damage rectangle may only be too big.
    VecAppend(gPts, cx);
    VecAppend(gPts, cy);
    VecAppend(gPts, r);
    VecAppend(gPts, a0);
    VecAppend(gPts, a1);
    pr->ptCount += 5;
    pr->hashed = false;
    if (!pr->any) {
        pr->originX = cx;
        pr->originY = cy;
        pr->any = true;
        pr->x0 = cx - r;
        pr->y0 = cy - r;
        pr->x1 = cx + r;
        pr->y1 = cy + r;
    } else {
        pr->x0 = (cx - r) < pr->x0 ? cx - r : pr->x0;
        pr->y0 = (cy - r) < pr->y0 ? cy - r : pr->y0;
        pr->x1 = (cx + r) > pr->x1 ? cx + r : pr->x1;
        pr->y1 = (cy + r) > pr->y1 ? cy + r : pr->y1;
    }
}

void RecPathClose(Path* p) {
    PathRec* pr = PathOf(p);
    if (!pr) {
        return;
    }
    Verb(pr, kVClose);
}

static Bounds PathBox(const PathRec* pr, float grow) {
    if (!pr || !pr->any) {
        return Bounds{0, 0, 0, 0};
    }
    return Bounds{pr->x0 - grow, pr->y0 - grow, pr->x1 - pr->x0 + grow * 2,
                  pr->y1 - pr->y0 + grow * 2};
}

static Prim* EmitPath(PaintCtx* ctx, Path* p, uint8_t kind, float grow) {
    PathRec* pr = PathOf(p);
    if (!pr) {
        return nullptr;
    }
    HashPath(*pr);
    Prim* prim = Emit(ctx, kind, PathBox(pr, grow));
    prim->path = (int32_t)((int)(uintptr_t)p - 1);
    return prim;
}

void RecPathFill(PaintCtx* ctx, Path* p, Rgba c) {
    Prim* prim = EmitPath(ctx, p, kPPathFill, 1);
    if (prim) {
        prim->color = PaintFade(ctx, c);
    }
}

void RecPathFillGradient(PaintCtx* ctx, Path* p, float x0, float y0, float x1,
                         float y1, Rgba from, Rgba to) {
    Prim* prim = EmitPath(ctx, p, kPPathGradient, 1);
    if (!prim) {
        return;
    }
    prim->e0 = x0;
    prim->e1 = y0;
    prim->e2 = x1;
    prim->e3 = y1;
    prim->color = PaintFade(ctx, from);
    prim->color2 = PaintFade(ctx, to);
}

void RecPathStroke(PaintCtx* ctx, Path* p, float stroke, Rgba c,
                   bool roundCaps) {
    Prim* prim = EmitPath(ctx, p, kPPathStroke, stroke > 0 ? stroke : 1);
    if (!prim) {
        return;
    }
    prim->e1 = stroke;
    prim->color = PaintFade(ctx, c);
    if (roundCaps) {
        prim->flags |= kFRoundCaps;
    }
}

void RecImageDraw(PaintCtx* ctx, RenderImage* img, Bounds bounds,
                  Bounds imageBounds, int frameIndex, float radius,
                  bool grayscale) {
    Prim* p = Emit(ctx, kPImage, bounds);
    p->g0 = bounds.x;
    p->g1 = bounds.y;
    p->g2 = bounds.w;
    p->g3 = bounds.h;
    p->e0 = radius;
    p->e1 = grayscale ? 1.f : 0.f;
    p->e2 = (float)frameIndex;
    p->imageBounds = imageBounds;
    RenderImageRetain(img);
    p->ref = img;
    p->resourceGeneration = RenderImageGeneration(img);
}

void RecTextDraw(PaintCtx* ctx, TextLayout* tl, float x, float y, Rgba c,
                 bool clip, float clipW) {
    Size sz = TextLayoutSize(tl);
    Prim* p = Emit(ctx, kPText, Bounds{x, y, sz.w, sz.h});
    p->g0 = x;
    p->g1 = y;
    p->g2 = sz.w;
    p->g3 = sz.h;
    TextLayoutAddRef(tl);
    p->ref = tl;
    p->resourceGeneration = TextLayoutGeneration(tl);
    p->color = PaintFade(ctx, c);
    p->e0 = clipW;
    if (clip) {
        p->flags |= kFClip;
    }
}

bool RecTextDrawSpans(PaintCtx* ctx, TextLayout* tl, Str text, float x, float y,
                      Rgba base, const TextSpan* spans, int n) {
    if (!gActive->textArena) {
        gActive->textArena = ArenaNew();
    }
    Arena* a = gActive->textArena;
    if (!a) return false;
    Str copy = StrDup(a, text);
    auto* runs = (TextSpan*)Alloc(a, n * (int)sizeof(TextSpan));
    if (!copy.s || !runs) return false;
    uint64_t hash = kHashSeed;
    for (int i = 0; i < n; i++) {
        runs[i] = spans[i];
        runs[i].color = PaintFade(ctx, spans[i].color);
        uint32_t color = 0;
        memcpy(&color, &runs[i].color, 4);
        hash = (hash ^ (uint32_t)runs[i].lo) * 0x100000001b3ull;
        hash = (hash ^ (uint32_t)runs[i].hi) * 0x100000001b3ull;
        hash = (hash ^ color) * 0x100000001b3ull;
    }
    RecTextDraw(ctx, tl, x, y, base, false, 0);
    Prim& p = gCur[gCur.len - 1];
    p.kind = kPTextSpans;
    p.path = gActive->texts.len;
    VecAppend(gActive->texts, TextRec{copy, runs, n, hash});
    return true;
}

// ─── ordering ────────────────────────────────────────────────────────────
//
// A context's entries are sorted stably; then its children are flattened
// recursively. The parent stays atomic relative to its siblings, even if one
// of its grandchildren has a much larger z value.
static void FlattenContext(int context, Vec<Prim>& out, int* nextRank);

static void FlattenEntry(int index, Vec<Prim>& out, int* nextRank) {
    const StackEntry& e = gEntries[index];
    if (e.prim >= 0) VecAppend(out, gCur[e.prim]);
    else if (e.context >= 0) FlattenContext(e.context, out, nextRank);
}

static void FlattenContext(int context, Vec<Prim>& out, int* nextRank) {
    gContexts[context].rank = (*nextRank)++;
    bool mixed = false;
    int lastZ = 0;
    bool first = true;
    for (int e = gContexts[context].first; e >= 0; e = gEntries[e].next) {
        if (!first && gEntries[e].z < lastZ) { mixed = true; break; }
        first = false;
        lastZ = gEntries[e].z;
    }
    if (!mixed) {
        for (int e = gContexts[context].first; e >= 0; e = gEntries[e].next)
            FlattenEntry(e, out, nextRank);
        return;
    }
    Vec<int> order;
    for (int e = gContexts[context].first; e >= 0; e = gEntries[e].next) {
        int at = order.len;
        VecAppend(order, e);
        while (at > 0 && gEntries[order[at - 1]].z > gEntries[e].z) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = e;
    }
    for (int i = 0; i < order.len; i++) {
        FlattenEntry(order[i], out, nextRank);
    }
    VecReset(order);
}

static void OrderHits(PaintCtx* ctx) {
    if (!ctx || ctx->hits.len < 2) return;
    bool mixed = false;
    for (int i = 1; i < ctx->hits.len; i++) {
        if (gContexts[ctx->hits[i].sceneContext].rank <
            gContexts[ctx->hits[i - 1].sceneContext].rank) {
            mixed = true;
            break;
        }
    }
    if (!mixed) return;
    Vec<int> order, inverse;
    for (int i = 0; i < ctx->hits.len; i++) {
        int rank = gContexts[ctx->hits[i].sceneContext].rank;
        int at = order.len;
        VecAppend(order, i);
        while (at > 0 &&
               gContexts[ctx->hits[order[at - 1]].sceneContext].rank > rank) {
            order[at] = order[at - 1];
            at--;
        }
        order[at] = i;
    }
    VecAppendBlanks(inverse, ctx->hits.len);
    for (int i = 0; i < order.len; i++) inverse[order[i]] = i;
    Vec<HitRect> sorted;
    for (int i = 0; i < order.len; i++) {
        HitRect h = ctx->hits[order[i]];
        if (h.parent >= 0) h.parent = inverse[h.parent];
        VecAppend(sorted, h);
    }
    for (int i = 0; i < sorted.len; i++) ctx->hits[i] = sorted[i];
    for (ScrollRect& scroll : ctx->scrolls) {
        if (scroll.maskHit >= 0) scroll.maskHit = inverse[scroll.maskHit];
    }
    VecReset(sorted);
    VecReset(order);
    VecReset(inverse);
}

// ─── the path cache ──────────────────────────────────────────────────────
//
// A built backend path, kept across frames and found by the hash of the
// geometry that built it. This is where the D2D backend's frame time goes:
// filling a path means tessellating it, and a tessellation that can be
// realized once and drawn many times is the difference the measurements
// show.

static CacheEntry* CacheFind(uint64_t hash) {
    if (hash == 0) {
        hash = 1;
    }
    int at = (int)(hash % kCacheSlots);
    for (int i = 0; i < kCacheSlots; i++) {
        CacheEntry& e = gCache[at];
        if (!e.live) {
            return &e;
        }
        if (e.hash == hash) {
            return &e;
        }
        at = (at + 1) % kCacheSlots;
    }
    return nullptr;
}

static void CacheClear() {
    for (int i = 0; i < kCacheSlots; i++) {
        if (gCache[i].live && gCache[i].path) {
            PathFree(gCache[i].path);
        }
        gCache[i] = CacheEntry{};
    }
    gCacheLive = 0;
    for (int i = 0; i < kMaskSlots; i++) {
        if (gMasks[i].image) RenderImageRelease(gMasks[i].image);
        gMasks[i] = MaskEntry{};
    }
    gMaskBytes = 0;
}

// Everything not asked for in the last `kCacheAge` frames goes. A rehash of
// the whole table, because open addressing cannot delete in place, and it
// only runs when the table is filling up.
static const int kCacheAge = 120;
static void CacheSweep() {
    CacheEntry* old = gSweepBuf;
    for (int i = 0; i < kCacheSlots; i++) {
        old[i] = gCache[i];
        gCache[i] = CacheEntry{};
    }
    gCacheLive = 0;
    for (int i = 0; i < kCacheSlots; i++) {
        if (!old[i].live) {
            continue;
        }
        if (gFrameNo - old[i].lastFrame > kCacheAge) {
            if (old[i].path) {
                PathFree(old[i].path);
            }
            continue;
        }
        CacheEntry* e = CacheFind(old[i].hash);
        if (e) {
            *e = old[i];
            gCacheLive++;
        } else if (old[i].path) {
            PathFree(old[i].path);
        }
    }
}

static bool TranslationIndependentPathCache() {
    static int enabled = -1;
    if (enabled < 0) {
        const char* value = getenv("GPUI_PATH_CACHE_TRANSLATION");
        bool off =
            value && (value[0] == '0' ||
                      ((value[0] == 'o' || value[0] == 'O') &&
                       (value[1] == 'f' || value[1] == 'F') &&
                       (value[2] == 'f' || value[2] == 'F') && value[3] == 0));
        enabled = off ? 0 : 1;
    }
    return enabled != 0;
}

void Invalidate(PaintCtx* ctx) {
    State* s = StateFor(ctx, false);
    if (!s) {
        return;
    }
    VecClear(s->prev);
    s->havePrev = false;
    s->prevFrameHash = 0;
    for (int i = 0; i < kBufferDepth; i++) {
        s->damageRing[i] = Bounds{};
    }
}

void Reset(PaintCtx* ctx) {
    State* s = StateFor(ctx, false);
    if (!s) {
        return;
    }
    State* previous = gActive;
    gActive = s;
    bool wasRecording = s->recording;
    s->recording = false;
    CacheClear();
    Invalidate(ctx);
    s->recording = wasRecording;
    gActive = previous;
}

void Free(PaintCtx* ctx) {
    State* s = StateFor(ctx, false);
    if (!s) {
        return;
    }
    State* previous = gActive;
    gActive = s;
    s->recording = false;
    CacheClear();
    ReleaseResources(s);
    ArenaDelete(s->textArena);
    VecReset(s->cur);
    VecReset(s->prev);
    VecReset(s->contexts);
    VecReset(s->entries);
    VecReset(s->paths);
    VecReset(s->verbs);
    VecReset(s->pts);
    VecReset(s->clipStack);
    VecReset(s->bagA.keys);
    VecReset(s->bagA.counts);
    VecReset(s->bagB.keys);
    VecReset(s->bagB.counts);
    delete s;
    ctx->sceneState = nullptr;
    gActive = previous == s ? nullptr : previous;
}

// Build a backend path out of the recorded verbs. The caller is drawing, so
// Recording() is already false and these are the real entry points.
static Path* BuildPath(PaintCtx* ctx, const PathRec& pr, bool relative) {
    Path* p = PathNew(ctx, pr.winding);
    if (!p) {
        return nullptr;
    }
    int vi = pr.verbFirst;
    int pi = pr.ptFirst;
    float ox = relative ? pr.originX : 0;
    float oy = relative ? pr.originY : 0;
    for (int i = 0; i < pr.verbCount; i++) {
        uint8_t v = gVerbs[vi++];
        switch (v & 0x7f) {
            case kVMove:
                PathMoveTo(p, gPts[pi] - ox, gPts[pi + 1] - oy);
                pi += 2;
                break;
            case kVLine:
                PathLineTo(p, gPts[pi] - ox, gPts[pi + 1] - oy);
                pi += 2;
                break;
            case kVCubic:
                PathCubicTo(p, gPts[pi] - ox, gPts[pi + 1] - oy,
                            gPts[pi + 2] - ox, gPts[pi + 3] - oy,
                            gPts[pi + 4] - ox, gPts[pi + 5] - oy);
                pi += 6;
                break;
            case kVArc:
                PathArcTo(p, gPts[pi] - ox, gPts[pi + 1] - oy, gPts[pi + 2],
                          gPts[pi + 3], gPts[pi + 4], (v & 0x80) != 0);
                pi += 5;
                break;
            case kVClose:
                PathClose(p);
                break;
            default:
                break;
        }
    }
    return p;
}

// The path a primitive draws, built or found. `owned` comes back true when
// the caller has to free it, which is every path at a level below `cache`.
static Path* PathFor(PaintCtx* ctx, const Prim& prim, bool* owned, float* dx,
                     float* dy) {
    *owned = true;
    *dx = 0;
    *dy = 0;
    if (prim.path < 0 || prim.path >= gPaths.len) {
        return nullptr;
    }
    const PathRec& pr = gPaths[prim.path];
    if (SceneLevelOn() < kSceneCache) {
        gStats.pathCacheMisses++;
        gStats.framePathCacheMisses++;
        double started = TimeNow();
        Path* p = BuildPath(ctx, pr, false);
        gStats.framePathBuildMs += (float)((TimeNow() - started) * 1000.0);
        return p;
    }
    bool relative = TranslationIndependentPathCache();
    uint64_t rawHash = relative ? pr.cacheHash : pr.hash;
    uint64_t cacheHash = rawHash ? rawHash : 1;
    CacheEntry* e = CacheFind(cacheHash);
    *dx = relative ? pr.originX : 0;
    *dy = relative ? pr.originY : 0;
    if (e && e->live && e->hash == cacheHash) {
        e->lastFrame = gFrameNo;
        gStats.pathCacheHits++;
        gStats.framePathCacheHits++;
        *owned = false;
        return e->path;
    }
    double started = TimeNow();
    Path* p = BuildPath(ctx, pr, relative);
    gStats.pathCacheMisses++;
    gStats.framePathCacheMisses++;
    if (!p) {
        gStats.framePathBuildMs += (float)((TimeNow() - started) * 1000.0);
        return nullptr;
    }
    // Realizing costs something, which is why it happens here and not on
    // every path: a path worth caching is a path worth tessellating once.
    PathRealize(ctx, p);
    gStats.framePathBuildMs += (float)((TimeNow() - started) * 1000.0);
    if (e && !e->live) {
        e->hash = cacheHash;
        e->path = p;
        e->lastFrame = gFrameNo;
        e->live = true;
        gCacheLive++;
        *owned = false;
    }
    return p;
}

// A small path is rasterized once into an offscreen coverage bitmap. The
// bitmap has its final premultiplied colour so every backend can replay it
// through RenderImageDraw without another shader or a target readback. Larger
// paths and gradients keep using the geometry cache above.
struct MaskEdge {
    float x0, y0, x1, y1;
};
static const float kMaskPi = 3.14159265358979323846f;

static void MaskLine(Vec<MaskEdge>& edges, float x0, float y0, float x1,
                     float y1) {
    if (x0 != x1 || y0 != y1) VecAppend(edges, MaskEdge{x0, y0, x1, y1});
}

static void MaskEdges(const PathRec& pr, bool closeOpen,
                      Vec<MaskEdge>& edges) {
    int vi = pr.verbFirst, pi = pr.ptFirst;
    float x = 0, y = 0, sx = 0, sy = 0;
    bool open = false;
    for (int i = 0; i < pr.verbCount; i++) {
        uint8_t v = gVerbs[vi++];
        if ((v & 0x7f) == kVMove) {
            if (open && closeOpen) MaskLine(edges, x, y, sx, sy);
            x = sx = gPts[pi++];
            y = sy = gPts[pi++];
            open = true;
        } else if ((v & 0x7f) == kVLine) {
            float nx = gPts[pi++], ny = gPts[pi++];
            if (open) MaskLine(edges, x, y, nx, ny);
            else { sx = nx; sy = ny; open = true; }
            x = nx; y = ny;
        } else if ((v & 0x7f) == kVCubic) {
            float ax = gPts[pi++], ay = gPts[pi++];
            float bx = gPts[pi++], by = gPts[pi++];
            float nx = gPts[pi++], ny = gPts[pi++];
            if (!open) { x = sx = nx; y = sy = ny; open = true; continue; }
            float extent = fabsf(ax - x) + fabsf(ay - y) +
                           fabsf(bx - ax) + fabsf(by - ay) +
                           fabsf(nx - bx) + fabsf(ny - by);
            int steps = (int)(extent / 2.f) + 4;
            if (steps > 64) steps = 64;
            float ox = x, oy = y;
            for (int j = 1; j <= steps; j++) {
                float t = (float)j / steps, u = 1.f - t;
                float px = u * u * u * x + 3.f * u * u * t * ax +
                           3.f * u * t * t * bx + t * t * t * nx;
                float py = u * u * u * y + 3.f * u * u * t * ay +
                           3.f * u * t * t * by + t * t * t * ny;
                MaskLine(edges, ox, oy, px, py);
                ox = px; oy = py;
            }
            x = nx; y = ny;
        } else if ((v & 0x7f) == kVArc) {
            float cx = gPts[pi++], cy = gPts[pi++], r = gPts[pi++];
            float a0 = gPts[pi++], a1 = gPts[pi++];
            float sweep = a1 - a0;
            if ((v & 0x80) && sweep < 0) sweep += 2.f * kMaskPi;
            if (!(v & 0x80) && sweep > 0) sweep -= 2.f * kMaskPi;
            int steps = (int)(fabsf(sweep) / (kMaskPi / 30.f)) + 2;
            if (steps > 256) steps = 256;
            float ax = cx + cosf(a0) * r, ay = cy + sinf(a0) * r;
            if (open) MaskLine(edges, x, y, ax, ay);
            else { sx = ax; sy = ay; open = true; }
            x = ax; y = ay;
            for (int j = 1; j <= steps; j++) {
                float a = a0 + sweep * ((float)j / steps);
                float nx = cx + cosf(a) * r, ny = cy + sinf(a) * r;
                MaskLine(edges, x, y, nx, ny);
                x = nx; y = ny;
            }
        } else if ((v & 0x7f) == kVClose && open) {
            MaskLine(edges, x, y, sx, sy);
            x = sx; y = sy;
            open = false;
        }
    }
    if (open && closeOpen) MaskLine(edges, x, y, sx, sy);
}

static bool MaskContains(const Vec<MaskEdge>& edges, float x, float y,
                         bool winding) {
    int crossings = 0;
    for (const MaskEdge& e : edges) {
        bool up = e.y0 <= y && e.y1 > y;
        bool down = e.y1 <= y && e.y0 > y;
        if (!up && !down) continue;
        float hit = e.x0 + (y - e.y0) * (e.x1 - e.x0) / (e.y1 - e.y0);
        if (hit > x) crossings += winding ? (up ? 1 : -1) : 1;
    }
    return winding ? crossings != 0 : (crossings & 1) != 0;
}

static bool MaskStrokeContains(const Vec<MaskEdge>& edges, float x, float y,
                               float radius) {
    float limit = radius * radius;
    for (const MaskEdge& e : edges) {
        float dx = e.x1 - e.x0, dy = e.y1 - e.y0;
        float d = dx * dx + dy * dy;
        float t = d > 0 ? ((x - e.x0) * dx + (y - e.y0) * dy) / d : 0;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        float px = x - (e.x0 + t * dx), py = y - (e.y0 + t * dy);
        if (px * px + py * py <= limit) return true;
    }
    return false;
}

static void MaskDrop(int slot) {
    MaskEntry& e = gMasks[slot];
    if (!e.image) return;
    RenderImageRelease(e.image);
    gMaskBytes -= e.bytes;
    e = MaskEntry{};
}

static MaskEntry* MaskFor(PaintCtx* ctx, const Prim& prim) {
    if (SceneLevelOn() < kSceneCache || !ctx || !ctx->pa ||
        prim.path < 0 || prim.path >= gPaths.len ||
        (prim.kind != kPPathFill &&
         !(prim.kind == kPPathStroke && (prim.flags & kFRoundCaps)))) {
        return nullptr;
    }
    const PathRec& pr = gPaths[prim.path];
    float scale = ctx->dpi / 96.f;
    if (scale <= 0 || scale > 4.f || !pr.any) return nullptr;
    float grow = prim.kind == kPPathStroke ? prim.e1 : 1.f;
    Bounds box = PathBox(&pr, grow);
    int x0 = (int)floorf(box.x * scale) - 1;
    int y0 = (int)floorf(box.y * scale) - 1;
    int x1 = (int)ceilf(box.Right() * scale) + 1;
    int y1 = (int)ceilf(box.Bottom() * scale) + 1;
    int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0 || w > 160 || h > 160) return nullptr;
    uint64_t key = HashBytes(kHashSeed, &prim.hash, (int)sizeof(prim.hash));
    key = HashBytes(key, &scale, (int)sizeof(scale));
    if (!key) key = 1;
    int slot = -1, oldest = 0;
    for (int i = 0; i < kMaskSlots; i++) {
        if (gMasks[i].image && gMasks[i].hash == key) {
            gMasks[i].lastFrame = gFrameNo;
            gStats.maskCacheHits++;
            return &gMasks[i];
        }
        if (!gMasks[i].image && slot < 0) slot = i;
        if (gMasks[i].lastFrame < gMasks[oldest].lastFrame) oldest = i;
    }
    Vec<MaskEdge> edges;
    MaskEdges(pr, prim.kind == kPPathFill, edges);
    // Bound the first-frame software work as well as retained bitmap size.
    if (edges.len == 0 || (int64_t)w * h * 16 * edges.len > 4000000) {
        VecReset(edges);
        return nullptr;
    }
    gStats.maskCacheMisses++;
    int bytes = w * h * 4;
    uint8_t* pixels = (uint8_t*)Alloc(nullptr, bytes);
    if (!pixels) { VecReset(edges); return nullptr; }
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            int cover = 0;
            for (int sy = 0; sy < 4; sy++) {
                for (int sx = 0; sx < 4; sx++) {
                    float fx = (x0 + px + (sx + .5f) / 4.f) / scale;
                    float fy = (y0 + py + (sy + .5f) / 4.f) / scale;
                    cover += prim.kind == kPPathFill
                        ? MaskContains(edges, fx, fy, pr.winding)
                        : MaskStrokeContains(edges, fx, fy, prim.e1 * .5f);
                }
            }
            int a = (prim.color.a * cover + 8) / 16;
            int at = (py * w + px) * 4;
            pixels[at] = (uint8_t)((prim.color.b * a + 127) / 255);
            pixels[at + 1] = (uint8_t)((prim.color.g * a + 127) / 255);
            pixels[at + 2] = (uint8_t)((prim.color.r * a + 127) / 255);
            pixels[at + 3] = (uint8_t)a;
        }
    }
    VecReset(edges);
    RenderImage* image = RenderImageFromBgra(ctx->pa, pixels, w, h);
    base::Free(nullptr, pixels);
    if (!image) return nullptr;
    while (gMaskBytes + bytes > kMaskBudget) {
        int victim = -1;
        for (int i = 0; i < kMaskSlots; i++) {
            if (gMasks[i].image && (victim < 0 ||
                gMasks[i].lastFrame < gMasks[victim].lastFrame)) victim = i;
        }
        if (victim < 0) break;
        MaskDrop(victim);
        if (victim == slot) slot = -1;
    }
    if (slot < 0) slot = oldest;
    MaskDrop(slot);
    MaskEntry& entry = gMasks[slot];
    entry.hash = key;
    entry.image = image;
    entry.bounds = Bounds{(float)x0 / scale, (float)y0 / scale,
                          (float)w / scale, (float)h / scale};
    entry.bytes = bytes;
    entry.lastFrame = gFrameNo;
    gMaskBytes += bytes;
    return &entry;
}

// ─── diffing ─────────────────────────────────────────────────────────────
//
// Not position for position. Almost nothing that changes on screen leaves the
// primitive count alone — a hover adds a background fill, a row appears, a
// popup opens — and a diff that gives up when the counts differ gives up on
// nearly every frame that matters. So the two frames are compared as
// multisets of primitive hashes: what is in the new frame and not the old is
// damage, what was in the old and is not in the new is damage, and everything
// else did not move. A primitive that moved is both, which is right — the
// rectangle has to cover where it was and where it is.

// A hash to count map, open addressed, rebuilt each frame. Two of them, one
// per direction of the comparison.
static void BagBuild(HashBag& b, const Vec<Prim>& v) {
    int cap = 16;
    while (cap < len(v) * 2) {
        cap *= 2;
    }
    b.mask = cap - 1;
    VecClear(b.keys);
    VecClear(b.counts);
    VecAppendBlanks(b.keys, cap);
    VecAppendBlanks(b.counts, cap);
    for (int i = 0; i < cap; i++) {
        b.keys[i] = 0;
        b.counts[i] = 0;
    }
    for (int i = 0; i < len(v); i++) {
        uint64_t k = v[i].hash | 1; // 0 is the empty slot
        int at = (int)(k)&b.mask;
        while (b.counts[at] != 0 && b.keys[at] != k) {
            at = (at + 1) & b.mask;
        }
        b.keys[at] = k;
        b.counts[at]++;
    }
}

// One off the count if it is there, and true if it was.
static bool BagTake(HashBag& b, uint64_t hash) {
    uint64_t k = hash | 1;
    int at = (int)(k)&b.mask;
    for (int i = 0; i <= b.mask; i++) {
        if (b.counts[at] == 0 && b.keys[at] == 0) {
            return false;
        }
        if (b.keys[at] == k && b.counts[at] > 0) {
            b.counts[at]--;
            return true;
        }
        at = (at + 1) & b.mask;
    }
    return false;
}

bool FrameEnd(PaintCtx* ctx, Bounds* damage) {
    State* s = StateFor(ctx, false);
    if (!s) {
        return false;
    }
    gActive = s;
    gRecording = false;
    gFrameNo++;
    Vec<Prim> ordered;
    int rank = 0;
    FlattenContext(0, ordered, &rank);
    for (int i = 0; i < gCur.len; i++) gCur[i] = ordered[i];
    VecReset(ordered);
    OrderHits(ctx);

    uint64_t frameHash = kHashSeed;
    for (int i = 0; i < gCur.len; i++) {
        gCur[i].hash = HashPrim(gCur[i]);
        frameHash = HashBytes(frameHash, &gCur[i].hash, 8);
    }
    int nLayers = 0;
    {
        int last = -1;
        for (int i = 0; i < gCur.len; i++) {
            if (gCur[i].layer != last) {
                nLayers++;
                last = gCur[i].layer;
            }
        }
    }
    gStats.prims = gCur.len;
    gStats.contexts = gContexts.len;
    gStats.layers = nLayers;
    gStats.pathPrims = 0;
    gStats.pathVerbs = gVerbs.len;
    for (int i = 0; i < gCur.len; i++) {
        if (gCur[i].kind >= kPPathFill && gCur[i].kind <= kPPathStroke) {
            gStats.pathPrims++;
        }
    }
    gStats.frames++;

    Bounds whole = Bounds{0, 0, gViewW, gViewH};
    Bounds dmg = whole;
    int changed = gCur.len;
    bool identical = false;
    if (gHavePrev) {
        if (frameHash == gPrevFrameHash && gPrev.len == gCur.len) {
            // Identical, which is worth knowing whether or not this level
            // acts on it. `dmg` stays the whole view: a level below `skip`
            // draws the frame again, and handing it an empty rectangle would
            // draw nothing at all.
            identical = true;
            changed = 0;
        }
    }
    if (!identical && gHavePrev && SceneLevelOn() >= kSceneDamage) {
        changed = 0;
        Bounds d = {};
        BagBuild(gBagA, gPrev);
        for (int i = 0; i < gCur.len; i++) {
            if (BagTake(gBagA, gCur[i].hash)) {
                continue;
            }
            changed++;
            d = Union(d, gCur[i].bbox);
        }
        BagBuild(gBagB, gCur);
        for (int i = 0; i < gPrev.len; i++) {
            if (BagTake(gBagB, gPrev[i].hash)) {
                continue;
            }
            changed++;
            d = Union(d, gPrev[i].bbox);
        }
        dmg = Intersect(d, whole);
    }
    gStats.primsChanged = changed;

    // The buffer this frame draws into holds what was presented three
    // presents ago, so the redraw has to cover that frame's damage too.
    if (SceneLevelOn() >= kSceneDamage && !identical) {
        Bounds acc = dmg;
        for (int i = 0; i < kBufferDepth; i++) {
            acc = Union(acc, gDamageRing[i]);
        }
        gDamageRing[gDamageRingAt] = dmg;
        gDamageRingAt = (gDamageRingAt + 1) % kBufferDepth;
        dmg = Intersect(acc, whole);
    }

    bool skip = identical && SceneLevelOn() >= kSceneSkip;
    gSkipPresent = skip;
    float wholeArea = whole.w * whole.h;
    gStats.damageFraction =
        skip ? 0.f : (wholeArea > 0 ? (dmg.w * dmg.h) / wholeArea : 1.f);
    if (identical) {
        gStats.framesUnchanged++;
    } else if (dmg.w < whole.w || dmg.h < whole.h) {
        gStats.framesPartial++;
        float area = whole.w * whole.h;
        gStats.damageFracSum += area > 0 ? (dmg.w * dmg.h) / area : 1.f;
    }

    // This frame becomes the one the next is compared against, whether or not
    // it was drawn: what is on screen did not change either way.
    VecClear(gPrev);
    for (int i = 0; i < gCur.len; i++) {
        Prim previous = gCur[i];
        previous.ref = nullptr; // Only the hash and bounds survive this frame.
        VecAppend(gPrev, previous);
    }
    gPrevFrameHash = frameHash;
    gHavePrev = true;

    if (damage) {
        *damage = skip ? Bounds{0, 0, 0, 0} : dmg;
    }
    if (gCacheLive > kCacheSlots / 2) {
        CacheSweep();
    }
    gStats.pathCacheLive = gCacheLive;
    return !skip;
}

// ─── replay ──────────────────────────────────────────────────────────────

void Replay(PaintCtx* ctx, const Bounds* damage) {
    State* s = StateFor(ctx, false);
    if (!s) {
        return;
    }
    gActive = s;
    Bounds whole = Bounds{0, 0, gViewW, gViewH};
    bool partial = damage && !SameBounds(*damage, whole);
    if (partial && (damage->w <= 0 || damage->h <= 0)) {
        return;
    }
    // The colours were faded as they were recorded, the way a backend fades
    // them as it is handed them; fading again would square the opacity.
    float saved = ctx->opacity;
    ctx->opacity = 1.f;
    if (partial) {
        CanvasPushClip(ctx, damage->x, damage->y, damage->w, damage->h);
    }
    Bounds cur = whole;
    bool pushed = false;
    gStats.maskChanges = 0;
    for (int i = 0; i < gCur.len; i++) {
        const Prim& p = gCur[i];
        if (p.bbox.w <= 0 || p.bbox.h <= 0) {
            // Wholly clipped away. A scene knows this; a tree walk that
            // issues the call does not.
            if (p.kind != kPClear) {
                gStats.culled++;
                continue;
            }
        }
        if (partial && p.kind != kPClear) {
            Bounds hit = Intersect(p.bbox, *damage);
            if (hit.w <= 0 || hit.h <= 0) {
                gStats.culled++;
                continue;
            }
        }
        if (!SameBounds(p.mask, cur)) {
            if (pushed) {
                CanvasPopClip(ctx);
                pushed = false;
            }
            if (!SameBounds(p.mask, whole)) {
                CanvasPushClip(ctx, p.mask.x, p.mask.y, p.mask.w, p.mask.h);
                pushed = true;
            }
            cur = p.mask;
            gStats.maskChanges++;
        }
        switch (p.kind) {
            case kPClear:
                if (partial) {
                    // Clearing is a fill of what is being redrawn: the rest
                    // of the buffer is last frame's and has to stay.
                    CanvasFillRect(ctx, damage->x, damage->y, damage->w,
                                   damage->h, p.color);
                } else {
                    CanvasClear(ctx, p.color);
                }
                break;
            case kPRect:
                CanvasFillRect(ctx, p.g0, p.g1, p.g2, p.g3, p.color);
                break;
            case kPRound:
                CanvasFillRound(ctx, p.g0, p.g1, p.g2, p.g3, p.e0, p.color);
                break;
            case kPStrokeRound: {
                float dash[2] = {p.e2, p.e3};
                CanvasStrokeRound(ctx, p.g0, p.g1, p.g2, p.g3, p.e0, p.e1,
                                  p.color, (p.flags & kFDash) ? dash : nullptr);
                break;
            }
            case kPLine: {
                float dash[2] = {p.e2, p.e3};
                CanvasLine(ctx, p.g0, p.g1, p.g2, p.g3, p.e1, p.color,
                           (p.flags & kFDash) ? dash : nullptr);
                break;
            }
            case kPEllipse:
                CanvasEllipse(ctx, p.g0, p.g1, p.g2, p.g3, p.e1, p.color);
                break;
            case kPImage:
                RenderImageDraw(ctx, (RenderImage*)p.ref,
                                Bounds{p.g0, p.g1, p.g2, p.g3}, p.imageBounds,
                                (int)p.e2, p.e0, p.e1 != 0);
                break;
            case kPText:
                TextLayoutDraw(ctx, (TextLayout*)p.ref, p.g0, p.g1, p.color,
                               (p.flags & kFClip) != 0, p.e0);
                break;
            case kPTextSpans:
                TextLayoutDrawSpans(ctx, (TextLayout*)p.ref,
                                    gActive->texts[p.path].text, p.g0, p.g1,
                                    p.color, gActive->texts[p.path].spans,
                                    gActive->texts[p.path].count);
                break;
            case kPPathFill:
            case kPPathGradient:
            case kPPathStroke: {
                MaskEntry* mask = MaskFor(ctx, p);
                if (mask) {
                    RenderImageDraw(ctx, mask->image, mask->bounds);
                    break;
                }
                bool owned = false;
                float dx = 0, dy = 0;
                Path* path = PathFor(ctx, p, &owned, &dx, &dy);
                if (!path) {
                    break;
                }
                if (p.kind == kPPathFill) {
                    PathFill(ctx, path, p.color, dx, dy);
                } else if (p.kind == kPPathGradient) {
                    PathFillGradient(ctx, path, p.e0 - dx, p.e1 - dy, p.e2 - dx,
                                     p.e3 - dy, p.color, p.color2, dx, dy);
                } else {
                    PathStroke(ctx, path, p.e1, p.color,
                               (p.flags & kFRoundCaps) != 0, dx, dy);
                }
                if (owned) {
                    PathFree(path);
                }
                break;
            }
            default:
                break;
        }
    }
    if (pushed) {
        CanvasPopClip(ctx);
    }
    if (partial) {
        CanvasPopClip(ctx);
    }
    gStats.maskCacheLive = 0;
    for (int i = 0; i < kMaskSlots; i++) {
        if (gMasks[i].image) gStats.maskCacheLive++;
    }
    ctx->opacity = saved;
}

} // namespace scene
} // namespace gpui

// Source files are amalgamated into one translation unit for normal builds.
// Keep the short aliases above private to this implementation.
#undef gCur
#undef gPrev
#undef gContexts
#undef gEntries
#undef gPaths
#undef gVerbs
#undef gPts
#undef gClipStack
#undef gClip
#undef gRecording
#undef gSeq
#undef gViewW
#undef gViewH
#undef gSkipPresent
#undef gStats
#undef gPrevFrameHash
#undef gHavePrev
#undef gFrameNo
#undef gDamageRing
#undef gDamageRingAt
#undef gCache
#undef gSweepBuf
#undef gCacheLive
#undef gMasks
#undef gMaskBytes
#undef gBagA
#undef gBagB
