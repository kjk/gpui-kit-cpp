#ifndef GPUI_GPUI_SCENE_H_
#define GPUI_GPUI_SCENE_H_
/* A scene between the element tree and Paint.h. Each painted element has a
   stacking context; sibling contexts are sorted by z-index, then flattened
   into a frame's primitives. Every primitive carries its content mask.

   It is on, and `__scene=off` is how to turn it off. It earns that on the
   scenes this tree draws — see the note at the end of this header for what
   was learned measuring it, and for the one kind of scene it costs rather
   than pays on.

   What a scene is for: a tree walk draws *while* it paints, so nothing can
   be known about the frame as a whole. Collect first and three things become
   possible, and all three are the reason this exists:

   - **Any backend can consume it.** The primitives name no GPU type. The
     replay walks them and calls the same Paint.h entry points the tree would
     have called, so every backend draws a scene as readily as a tree.
   - **A frame can be compared with the last one.** Every primitive hashes to
     64 bits and a frame hashes to the same; two frames that hash alike differ
     nowhere, and a frame that differs in ten primitives out of six thousand
     names the rectangle those ten cover. The swap chain is FLIP_SEQUENTIAL
     with three buffers precisely so a partial redraw comes out whole.
   - **Path rendering outlives the frame that built it.** A path is recorded as
     verbs and points, not as a backend object. Small solid paths can reuse an
     offscreen coverage bitmap; the rest reuse backend geometry by hash.

   On, the scene does all of it: collect and replay, keep path geometry and
   small offscreen masks across frames, skip a frame identical to the last
   one, and — where `SceneDamageSupported` says the back buffer holds the
   last frame's pixels, which is Windows — draw a frame that differs in part
   in part. The intermediate levels this once had, and what each was worth,
   are recorded in obsolete-windows-backends.md.

   `__scene=off` draws the element tree straight to the backend, as it used
   to; an invalid value is consumed and leaves the selection unchanged. It is
   what a bisect wants, and what to reach for if a frame ever comes out
   stale: this is the only thing in the tree that can decide not to draw.

   Each paint backend dispatches into the recorder with the same Rec* hook
   at the top of each Paint.h entry point. Nothing here names an OS. */

#include "gpui/paint.h"

namespace gpui {

// Set at process start, before any frame is open.
bool SceneOn();
// Consumes `__scene=off|on` (also 0|1). Invalid values are still consumed
// and leave the selection unchanged.
bool SceneTakeArg(Str arg);
// Whether a frame that differs in part may be drawn in part, which needs the
// back buffer to hold the last frame's pixels. True on Windows (scene_win.cpp,
// checked on screen by cmd/bench-scene.ts), false elsewhere (scene_posix.cpp)
// until those targets are checked the same way.
bool SceneDamageSupported();

namespace scene {

struct State;

// A PaintCtx owns one of these lazily. Free is separate because PaintCtx is a
// POD-friendly public type and does not own resources through a destructor.
void Free(PaintCtx* ctx);

// True while the recorder is swallowing Paint.h calls rather than letting
// them reach a backend. False during the replay, which is what lets the
// replay use the ordinary entry points.
bool Recording();
// A context keeps its descendants together when siblings are ordered by z.
// The return value restores the parent after the element has painted.
int ContextPush(PaintCtx* ctx, int z);
void ContextPop(PaintCtx* ctx, int parent);
int CurrentContext(PaintCtx* ctx);

// Open the frame's recording. The real target is already begun, because the
// replay at the end of the frame draws into it.
void FrameBegin(PaintCtx* ctx);
// Close the recording, order the primitives, hash them and compare the frame
// with the last one. Returns false when the frame is identical and the caller
// may skip both the replay and the present; `damage` comes back as the
// rectangle worth redrawing, which is the whole view unless the level is
// `damage` and the difference was small.
bool FrameEnd(PaintCtx* ctx, Bounds* damage);
// Draw the collected frame through the ordinary Paint.h entry points.
// `damage` null redraws all of it.
void Replay(PaintCtx* ctx, const Bounds* damage);
// An offscreen target opened inside a frame — a menu icon being rasterized —
// draws for its own sake and not for the window's, so the recorder steps
// aside for it. SuspendBegin hands back what to give SuspendEnd.
bool SuspendBegin();
void SuspendEnd(bool prev);

// True when FrameEnd said the frame was unchanged, which is what tells
// PaintTargetEnd not to present. Cleared by the next FrameBegin.
bool SkipPresent(PaintCtx* ctx);

// Forget the previous frame, so the next one is drawn whole and presented
// whatever it looks like. What the surface holds and what this thinks it
// holds have parted company: a swap chain resized, its buffers discarded.
// The path cache is geometry and survives.
void Invalidate(PaintCtx* ctx);
// The above, and drop the path cache with it. A lost device, a target freed
// -- anything that takes the objects the cache holds down with it.
void Reset(PaintCtx* ctx);

// ─── what the recorder is handed ─────────────────────────────────────────
//
// One for one with Paint.h, minus the entry points a scene has no business
// intercepting: the target lifecycle, the offscreen target, shaping,
// measurement and image decoding all go straight through.

bool RecTextDrawSpans(PaintCtx* ctx, TextLayout* tl, Str text, float x, float y,
                      Rgba base, const TextSpan* spans, int n);
void RecClear(PaintCtx* ctx, Rgba c);
void RecFillRect(PaintCtx* ctx, float x, float y, float w, float h, Rgba c);
void RecFillRound(PaintCtx* ctx, float x, float y, float w, float h, float r,
                  Rgba c);
void RecStrokeRound(PaintCtx* ctx, float x, float y, float w, float h, float r,
                    float stroke, Rgba c, const float* dash);
void RecLine(PaintCtx* ctx, float x1, float y1, float x2, float y2,
             float stroke, Rgba c, const float* dash);
void RecEllipse(PaintCtx* ctx, float cx, float cy, float rx, float ry,
                float stroke, Rgba c);
void RecPushClip(PaintCtx* ctx, float x, float y, float w, float h);
void RecPopClip(PaintCtx* ctx);

Path* RecPathNew(PaintCtx* ctx, bool winding);
void RecPathFree(Path* p);
void RecPathMoveTo(Path* p, float x, float y);
void RecPathLineTo(Path* p, float x, float y);
void RecPathCubicTo(Path* p, float x1, float y1, float x2, float y2, float x,
                    float y);
void RecPathArcTo(Path* p, float cx, float cy, float r, float a0, float a1,
                  bool clockwise);
void RecPathClose(Path* p);
void RecPathFill(PaintCtx* ctx, Path* p, Rgba c);
void RecPathFillGradient(PaintCtx* ctx, Path* p, float x0, float y0, float x1,
                         float y1, Rgba from, Rgba to);
void RecPathStroke(PaintCtx* ctx, Path* p, float stroke, Rgba c,
                   bool roundCaps);
void RecPathStrokeGradient(PaintCtx* ctx, Path* p, float stroke, float x0,
                           float y0, float x1, float y1, Rgba from, Rgba to,
                           bool roundCaps);

void RecImageDraw(PaintCtx* ctx, RenderImage* img, Bounds bounds,
                  Bounds imageBounds, int frameIndex, float radius,
                  bool grayscale);
inline void RecImageDraw(PaintCtx* ctx, RenderImage* img, Bounds bounds,
                         float radius) {
    RecImageDraw(ctx, img, bounds, bounds, 0, radius, false);
}
void RecTextDraw(PaintCtx* ctx, TextLayout* tl, float x, float y, Rgba c,
                 bool clip, float clipW);
// A scroll container's viewport, recorded while its clip is in force.
// `scrollX` / `scrollY` are the content offsets (positive-down /
// positive-right). Replay does not draw it.
void RecScroll(PaintCtx* ctx, float x, float y, float w, float h, float scrollX,
               float scrollY);
// When FrameEnd decided this frame is a pure scroll, shift the pixels already
// on the target and replay only the exposed strip and the chrome that moved.
// False means the caller draws the frame the ordinary way.
bool ReplayScroll(PaintCtx* ctx);

// ─── what a frame cost ───────────────────────────────────────────────────

struct SceneStats {
    // This frame.
    int prims = 0;
    int contexts = 0;
    int layers = 0;
    // How many times the replay had to change the clip, which is the number
    // of PushAxisAlignedClip calls a D2D replay makes. The tree issued far
    // more push/pop pairs than this: a mask that comes back to a rectangle
    // already in force costs nothing.
    int maskChanges = 0;
    // Primitives the replay never issued because their content mask had
    // already reduced them to nothing. A tree walk cannot know this: it calls
    // the backend and the backend clips. This is the single largest thing the
    // scene does for the D2D path.
    int culled = 0;
    // How many times the tree pushed a clip, against `maskChanges` above,
    // which is how many the replay had to.
    int clipPushes = 0;
    int pathPrims = 0;
    int pathVerbs = 0;
    // Paths the replay found already built, and paths it had to build.
    int pathCacheHits = 0;
    int pathCacheMisses = 0;
    // Cache activity and backend path construction for the most recent
    // frame. Construction includes PathRealize on a cache miss.
    int framePathCacheHits = 0;
    int framePathCacheMisses = 0;
    float framePathBuildMs = 0;
    int pathCacheLive = 0;
    int maskCacheHits = 0;
    int maskCacheMisses = 0;
    int maskCacheLive = 0;
    // Across the run, so a bench line can report a rate.
    int frames = 0;
    int framesUnchanged = 0;
    int framesPartial = 0;
    // Mean fraction of the view the damage rectangle covered, over the
    // frames that were neither unchanged nor whole.
    float damageFracSum = 0;
    // The primitives that differed from the previous frame, this frame.
    int primsChanged = 0;
    // Fraction of the view redrawn by the most recent frame: zero when it was
    // skipped, one for a full replay, and between them for damage replay.
    float damageFraction = 1;
    // Scroll-layer blits that reused the previous pixels, and frames that
    // looked like a scroll but had to be drawn whole.
    int scrollBlits = 0;
    int scrollRejects = 0;
};
const SceneStats& Stats(PaintCtx* ctx);

} // namespace scene

// ─── what measuring it taught ────────────────────────────────────────────
//
// The scene once had five levels (off, replay, cache, skip, damage) and was
// measured against two more Windows renderers; the numbers and the decision
// to keep only on/off and Direct2D are in obsolete-windows-backends.md. Four
// things those measurements say, in the order they matter:
//
// 1. **Collecting is not what pays; culling is.** `replay` alone takes 41% off
//    the D2D paint of the story gallery, and it is not the ordering and not
//    the batching. 799 of the story's 1071 primitives have a content mask that
//    has already reduced them to nothing — rows scrolled out of a list, text
//    under a collapsed section — and a tree walk hands every one of them to
//    the backend to be clipped. The scene knows the mask before it draws and
//    issues none of them. On the GPU backend the same cull takes the frame
//    from 6219 instances in 243 draws to 1613 in 39.
// 2. **Reordering buys nothing**, which was the thing worth finding out. The
//    quad path was already one batch; sorting by layer moves almost nothing,
//    because the two paint walks were already in layer order; and the replay
//    changes the clip 26 times where the tree pushed 17, so even the mask
//    coalescing is a wash. A scene is not how you make this renderer batch.
// 3. **The path cache is where Direct2D gains what a GPU backend gets for
//    free.** 99% of path lookups hit across the story and the system monitor,
//    and filling a geometry realization instead of a geometry takes another
//    12% off the D2D paint. It costs a little fidelity: 0.03% of the story's
//    pixels differ from the un-realized fill, all of them on curve edges.
// 4. **Skipping is worth more than everything else together and means less.**
//    A benchmark redraws one frame 600 times, so 97% of them are identical and
//    the paint phase falls to a tenth. A real window is idle most of the time
//    too — but do not read 0.25 as the story's frame cost, read it as what an
//    idle window costs, which was already zero in a build that does not redraw
//    when nothing asked it to.
//
// And the counterweight, which is `fps_monitor`: when every primitive changes
// every frame, collecting and hashing them is 11% on top of the D2D paint, the
// caches never hit, and the damage rectangle comes out at 96% of the view.
// That 11% was 20% until the primitive hash went from a byte-at-a-time FNV
// over the struct to ten words, which is worth knowing because it says where
// the cost of a scene that cannot be reused actually sits: not in collecting
// it, in reading it back. On a scene that is genuinely animating this is a
// loss, and it is on by default because a UI is mostly still.
//
// The one fidelity cost of having it on: 0.03% of the story's pixels, 45 of
// them far enough off to count as a real difference, all on the curve edges of
// icons — a cached path is filled from a geometry realization, which is
// tessellated once at one tolerance, and FillGeometry is not.
//
// What it is short of, and what each would take:
//
// - **Damage is checked on screen, on Windows only.** cmd/bench-scene.ts
//   drives scrolls, hovers, a popup, a caret and a chart tick through real
//   input and timers, reads the visible surface back and finds a full redraw
//   and a damage redraw pixel-identical (-settle=2500, the window topmost). Two
//   things it caught: a damage clip on a fractional DIP left a blended seam
//   (the rectangle is now snapped out to device pixels), and a text run's ink
//   overhangs its layout box (its bbox is padded). The other platforms redraw a
//   changed frame whole until their back buffers are checked the same way.
// - **Mask cache coverage is bounded.** A small solid fill or round-cap
//   stroke is rasterized once into a colourized coverage image. Large paths,
//   gradients and other strokes retain the geometry path; see port-status.md.

} // namespace gpui
#endif // GPUI_GPUI_SCENE_H_
