/* Everything a window does that is not the OS window: frame drawing, input
   dispatch, the app lifecycle. Window_win.cpp and Window_linux.cpp call in
   here; nothing here calls back out except through Platform.h. */

#include "gpui/platform.h"

#include <stdio.h>
#include "gpui/keymap.h"
#include "gpui/image.h"
#include "gpui/paint.h"
// For the GPU backend's per-frame counters, which FrameBenchTick reports.
#include "gpui/paintgpu.h"
#include "gpui/scene.h"
#include "sys/http.h"
#include "sys/executor.h"
#include "sys/sysinfo.h"
#include "base/focus_trap.h"
#include "base/global_state.h"
#include "base/text_selection.h"
#include "base/tooltip.h"

namespace gpui {

void OngoingScroll::Filter(Point* delta, TouchPhase phase) {
    if (!delta) {
        return;
    }
    float ax = delta->x < 0 ? -delta->x : delta->x;
    float ay = delta->y < 0 ? -delta->y : delta->y;
    if (!active || phase == TouchPhase::Started) {
        axis = ax > ay ? Axis::Horizontal : Axis::Vertical;
        active = true;
    } else if (axis == Axis::Horizontal && ay > ax * 2.f) {
        axis = Axis::Vertical;
    } else if (axis == Axis::Vertical && ax > ay * 2.f) {
        axis = Axis::Horizontal;
    }
    if (axis == Axis::Horizontal) {
        delta->y = 0;
    } else {
        delta->x = 0;
    }
    if (phase == TouchPhase::Ended || phase == TouchPhase::Cancelled) {
        active = false;
    }
}

int WindowCollectFrames(Window* win, uint64_t* cursor, FrameTiming* out,
                        int max) {
    if (!win || !cursor || !out || max <= 0) {
        return 0;
    }
    uint64_t from = *cursor;
    // Frames that fell out of the ring while nobody was collecting are gone.
    if (win->frameSeq > (uint64_t)kFrameTraceCap &&
        from < win->frameSeq - (uint64_t)kFrameTraceCap) {
        from = win->frameSeq - (uint64_t)kFrameTraceCap;
    }
    if (from + (uint64_t)max < win->frameSeq) {
        from = win->frameSeq - (uint64_t)max;
    }
    int n = 0;
    for (uint64_t i = from; i < win->frameSeq; i++) {
        out[n++] = win->frameTrace[i % (uint64_t)kFrameTraceCap];
    }
    *cursor = win->frameSeq;
    return n;
}

// ─── frame ────────────────────────────────────────────────────────────────

// GPUI_FRAME_BENCH=<n> draws n frames back to back, prints what each one
// cost, and quits. It is here rather than in a backend because what it times
// is Window::draw — building the element tree, laying it out, painting it —
// which is the number a frame budget is actually spent against, and because
// the split below is what says which of the three a change moved. Inert
// unless the variable is set.
static double gFrameBuildSecs = 0;
static double gFrameLayoutSecs = 0;
static double gFramePaintSecs = 0;

// GPUI_INTERACTION_BENCH records frames requested by actual input and timers.
// Unlike GPUI_FRAME_BENCH it neither manufactures frames nor quits the app;
// cmd/bench-scene.ts drives the window and consumes these machine-readable
// lines. Timing stops before this log, so file I/O is outside the sample.
static bool InteractionBenchOn() {
    static int on = -1;
    if (on < 0) {
        const char* value = getenv("GPUI_INTERACTION_BENCH");
        on = value && value[0] && value[0] != '0';
    }
    return on != 0;
}

static void InteractionBenchRecord(Window* win, const FrameTiming& timing) {
    if (!InteractionBenchOn()) {
        return;
    }
    const scene::SceneStats& sc = scene::Stats(&win->paint);
    uint64_t privateBytes = 0;
    SysSelfPrivateMemory(&privateBytes);
    logf(
        "interaction-bench frame=%llu draw=%.6f build=%.6f layout=%.6f "
        "paint=%.6f presented=%d invalidations=%llu prims=%d changed=%d "
        "damage=%.6f pathHits=%d pathMisses=%d pathBuild=%.6f arena=%llu "
        "arenaAllocs=%llu private=%llu",
        (unsigned long long)win->frameSeq, timing.drawSecs * 1000.f,
        gFrameBuildSecs * 1000.0, gFrameLayoutSecs * 1000.0,
        gFramePaintSecs * 1000.0, timing.presentAt >= 0 ? 1 : 0,
        (unsigned long long)timing.invalidations, SceneOn() ? sc.prims : -1,
        SceneOn() ? sc.primsChanged : -1, SceneOn() ? sc.damageFraction : -1.f,
        SceneOn() ? sc.framePathCacheHits : -1,
        SceneOn() ? sc.framePathCacheMisses : -1,
        SceneOn() ? sc.framePathBuildMs : -1.f,
        (unsigned long long)ArenaUsed(win->frameArena),
        (unsigned long long)win->frameArena->nAllocsSinceReset,
        (unsigned long long)privateBytes);
}

static void FrameBenchTick(Window* win, float secs) {
    static int want = -1;
    static int seen = 0;
    static int warm = 0;
    static Vec<float> samples;
    static Vec<float> build;
    static Vec<float> layout;
    static Vec<float> paint;
    if (want < 0) {
        char buf[16] = {};
#if GPUI_OS_WINDOWS
        DWORD n = GetEnvironmentVariableA("GPUI_FRAME_BENCH", buf, sizeof(buf));
        want = (n > 0 && n < sizeof(buf)) ? StrToIntUnchecked(Str(buf)) : 0;
#else
        const char* e = getenv("GPUI_FRAME_BENCH");
        if (e) {
            StrCopyZ(buf, (int)sizeof(buf), e);
        }
        want = buf[0] ? StrToIntUnchecked(Str(buf)) : 0;
#endif
        if (want > 0) {
            // Back to back, so the measurement is of drawing and not of how
            // often something asked for a frame.
            win->anim = true;
        }
    }
    if (want <= 0) {
        return;
    }
    // Ask for the next frame by hand. `win->anim` alone is not enough: it is
    // only read when something arms the platform timer, and a page that is
    // not animating has nothing that would.
    AppInvalidate(win);
    PlatSetTimer(win, 1);
    // The first thirty are thrown away: the swap chain, the shaders, the
    // glyph atlas and the shaped-text cache all fill up in them, and none of
    // that is what a steady frame costs.
    if (warm < 30) {
        warm++;
        return;
    }
    VecAppend(samples, secs * 1000.f);
    VecAppend(build, (float)(gFrameBuildSecs * 1000.0));
    VecAppend(layout, (float)(gFrameLayoutSecs * 1000.0));
    VecAppend(paint, (float)(gFramePaintSecs * 1000.0));
    if (++seen < want) {
        return;
    }
    int n = samples.len;
    for (int i = 1; i < n; i++) {
        float v = samples[i];
        int j = i - 1;
        while (j >= 0 && samples[j] > v) {
            samples[j + 1] = samples[j];
            j--;
        }
        samples[j + 1] = v;
    }
    double sum = 0;
    for (int i = 0; i < n; i++) {
        sum += samples[i];
    }
    logf(
        "frame-bench n=%d mean=%.3fms median=%.3fms p95=%.3fms min=%.3fms "
        "max=%.3fms",
        n, sum / n, samples[n / 2], samples[(int)((float)n * 0.95f)],
        samples[0], samples[n - 1]);
    double sb = 0, sl = 0, sp = 0;
    for (int i = 0; i < n; i++) {
        sb += build[i];
        sl += layout[i];
        sp += paint[i];
    }
    logf("frame-bench phases build=%.3fms layout=%.3fms paint=%.3fms", sb / n,
         sl / n, sp / n);
    uint64_t privateBytes = 0;
    SysSelfPrivateMemory(&privateBytes);
    logf("frame-bench memory arena=%llu arenaAllocs=%llu El=%llu private=%llu",
         (unsigned long long)ArenaUsed(win->frameArena),
         (unsigned long long)win->frameArena->nAllocsSinceReset,
         (unsigned long long)sizeof(El), (unsigned long long)privateBytes);
    // What the last frame's layout had to tell taffy about. On a page that
    // did not change, made and dropped are zero and restyled is the handful
    // of boxes whose style is a function of something that moved.
    LayoutCacheStats ls = LayoutCacheLastStats(win->layout);
    logf(
        "frame-bench layout nodes=%d live=%d slots=%d made=%d dropped=%d "
        "restyled=%d remeasured=%d allocs=%d",
        ls.nodes, LayoutCacheNodeCount(win->layout),
        LayoutCacheSlotCount(win->layout), ls.made, ls.dropped, ls.restyled,
        ls.remeasured, ls.allocs);
#if GPUI_OS_WINDOWS
    if (PaintGpuOn()) {
        const gpuw::FrameStats& st = gpuw::LastFrameStats();
        logf(
            "frame-bench %s instances=%d draws=%d pathTris=%d "
            "glyphsRasterized=%d",
            PaintD3d12On() ? StrL("d3d12") : StrL("d3d11"), st.instances,
            st.draws, st.pathTriangles, st.glyphsRasterized);
    }
#endif
    if (SceneOn()) {
        const scene::SceneStats& sc = scene::Stats(&win->paint);
        logf(
            "frame-bench scene prims=%d culled=%d layers=%d clipPushes=%d "
            "maskChanges=%d paths=%d verbs=%d changed=%d",
            sc.prims, sc.culled, sc.layers, sc.clipPushes, sc.maskChanges,
            sc.pathPrims, sc.pathVerbs, sc.primsChanged);
        int lookups = sc.pathCacheHits + sc.pathCacheMisses;
        logf(
            "frame-bench scene cache hits=%d misses=%d live=%d hitRate=%.1f%% "
            "unchanged=%d/%d partial=%d meanDamage=%.1f%%",
            sc.pathCacheHits, sc.pathCacheMisses, sc.pathCacheLive,
            lookups ? 100.0 * sc.pathCacheHits / lookups : 0.0,
            sc.framesUnchanged, sc.frames, sc.framesPartial,
            sc.framesPartial ? 100.0 * sc.damageFracSum / sc.framesPartial
                             : 0.0);
    }
    want = 0;
    PlatSetTimer(win, 0);
    AppQuit(win);
}

// ─── the laid-out tree, as text ───────────────────────────────────────────
//
// `GPUI_LAYOUT_DUMP=<path>` writes every frame's tree to a file: a header line
// per frame, then a line per element with where layout put it. It is the only
// way to see what a frame was laid out as without asking the window to draw
// again — a screenshot on Windows goes through PrintWindow, which makes the
// window render, so a capture can never show a frame that came out wrong and
// was left on screen. Diffing two consecutive frames says what moved.
//
// Inert unless the variable is set.
static FILE* LayoutDumpFile() {
    static FILE* f = nullptr;
    static bool tried = false;
    if (tried) {
        return f;
    }
    tried = true;
    const char* path = getenv("GPUI_LAYOUT_DUMP");
    if (!path || !path[0]) {
        return nullptr;
    }
    f = fopen(path, "wb");
    if (f) {
        logf("layout: dumping every frame to %s (GPUI_LAYOUT_DUMP)", Str(path));
    }
    return f;
}

static void LayoutDumpEl(FILE* f, El* e, int depth) {
    if (!e) {
        return;
    }
    // The text, cut short: what is on the line matters for telling one
    // element from another, not what it says.
    char text[41] = {};
    int n = e->text.len < 40 ? e->text.len : 40;
    for (int i = 0; i < n; i++) {
        char c = e->text.s[i];
        text[i] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    fprintf(f, "%*s%d id=%d x=%.1f y=%.1f w=%.1f h=%.1f%s%s\n", depth * 2, "",
            (int)e->kind, e->clickId, e->x, e->y, e->w, e->h,
            text[0] ? " " : "", text);
    for (El* c = e->first; c; c = c->next) {
        LayoutDumpEl(f, c, depth + 1);
    }
}

static void LayoutDumpFrame(Window* win, El* root) {
    FILE* f = LayoutDumpFile();
    if (!f || !win || !root) {
        return;
    }
    // What became of the frame, beside what it was: a frame the scene found
    // identical to the last one is not presented, so a dump that shows the
    // right layout and `presented=0` is a screen that is still showing the
    // frame before it.
    LayoutCacheStats ls = LayoutCacheLastStats(win->layout);
    fprintf(f,
            "--- frame %llu t=%.3f view=%.0fx%.0f prims=%d presented=%d "
            "nodes=%d slots=%d made=%d dropped=%d restyled=%d remeasured=%d "
            "allocs=%d\n",
            (unsigned long long)win->frameSeq, TimeNow(), win->paint.viewW,
            win->paint.viewH, SceneOn() ? scene::Stats(&win->paint).prims : -1,
            (SceneOn() && scene::SkipPresent(&win->paint)) ? 0 : 1, ls.nodes,
            LayoutCacheSlotCount(win->layout), ls.made, ls.dropped, ls.restyled,
            ls.remeasured, ls.allocs);
    LayoutDumpEl(f, root, 0);
    fflush(f);
}

static uint64_t AccessibilityHashBytes(uint64_t hash, const void* data,
                                       int len) {
    const uint8_t* bytes = (const uint8_t*)data;
    for (int i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ull;
    }
    return hash;
}

static uint64_t AccessibilityHashStr(uint64_t hash, Str value) {
    hash = AccessibilityHashBytes(hash, &value.len, (int)sizeof(value.len));
    return value.s && value.len > 0
               ? AccessibilityHashBytes(hash, value.s, value.len)
               : hash;
}

static uint64_t AccessibilityTreeHash(const Vec<AccessibilityNode>& nodes) {
    uint64_t hash = 0xcbf29ce484222325ull;
    hash = AccessibilityHashBytes(hash, &nodes.len, (int)sizeof(nodes.len));
#define GPUI_A11Y_HASH(value)                                                 \
    do {                                                                      \
        const auto hashValue = (value);                                       \
        hash =                                                                \
            AccessibilityHashBytes(hash, &hashValue, (int)sizeof(hashValue)); \
    } while (false)
    for (int i = 0; i < nodes.len; i++) {
        const AccessibilityNode& node = nodes[i];
        const AccessibilityInfo& info = node.info;
        GPUI_A11Y_HASH(node.id);
        GPUI_A11Y_HASH(node.parent);
        GPUI_A11Y_HASH(node.actions);
        GPUI_A11Y_HASH(node.focusId);
        GPUI_A11Y_HASH(info.role);
        hash = AccessibilityHashStr(hash, info.authorId);
        hash = AccessibilityHashStr(hash, info.label);
        hash = AccessibilityHashStr(hash, info.value);
        hash = AccessibilityHashStr(hash, info.placeholder);
        GPUI_A11Y_HASH(info.toggled);
        GPUI_A11Y_HASH(info.orientation);
        GPUI_A11Y_HASH(info.numericValue);
        GPUI_A11Y_HASH(info.minNumericValue);
        GPUI_A11Y_HASH(info.maxNumericValue);
        GPUI_A11Y_HASH(info.numericValueStep);
        GPUI_A11Y_HASH(info.positionInSet);
        GPUI_A11Y_HASH(info.sizeOfSet);
        GPUI_A11Y_HASH(info.rowCount);
        GPUI_A11Y_HASH(info.columnCount);
        GPUI_A11Y_HASH(info.rowIndex);
        GPUI_A11Y_HASH(info.columnIndex);
        GPUI_A11Y_HASH(info.level);
        GPUI_A11Y_HASH(info.hasNumericValue);
        GPUI_A11Y_HASH(info.hasMinNumericValue);
        GPUI_A11Y_HASH(info.hasMaxNumericValue);
        GPUI_A11Y_HASH(info.hasNumericValueStep);
        GPUI_A11Y_HASH(info.hasPositionInSet);
        GPUI_A11Y_HASH(info.hasSizeOfSet);
        GPUI_A11Y_HASH(info.hasRowCount);
        GPUI_A11Y_HASH(info.hasColumnCount);
        GPUI_A11Y_HASH(info.hasRowIndex);
        GPUI_A11Y_HASH(info.hasColumnIndex);
        GPUI_A11Y_HASH(info.hasLevel);
        GPUI_A11Y_HASH(info.selected);
        GPUI_A11Y_HASH(info.hasSelected);
        GPUI_A11Y_HASH(info.expanded);
        GPUI_A11Y_HASH(info.hasExpanded);
        GPUI_A11Y_HASH(info.disabled);
    }
#undef GPUI_A11Y_HASH
    return hash;
}

void WindowDrawFrame(Window* win, void* native, int pxW, int pxH, float dipW,
                     float dipH) {
    if (!win) {
        return;
    }
    double drawStart = TimeNow();
    // One instant for the whole frame, which is what every transition in it
    // measures against, and the frame that has just been asked for: whatever
    // still has somewhere to go asks again below.
    win->frameNow = drawStart;
    win->animFrame = false;
    // Before the target is opened, because opening it starts the scene's
    // frame and the scene culls and clips against this size. A window that
    // has just grown would otherwise draw one frame culled to the size it
    // had before — everything outside the old view thrown away — and since
    // nothing asks for another frame after a resize, that frame is what
    // stays on screen until the next click or keystroke.
    win->paint.viewW = dipW;
    win->paint.viewH = dipH;
    // Root/WindowBorder writes the client-decorated inset while rendering.
    // Clear the last frame first so removing that wrapper removes the inset.
    win->paint.clientInset = 0;
    if (!PaintTargetBegin(&win->paint, native, pxW, pxH)) {
        return;
    }

    // Accessibility strings and callbacks point into the frame arena. Drop
    // the old projection before that arena is reset, so no stale semantic
    // record survives while the next view tree is being built.
    VecClear(win->accessibility);
    if (win->frameArena) {
        win->frameArena->Reset();
    } else {
        win->frameArena = ArenaNew();
    }
    ResetTempArena();
    // element_opacity starts at 1 each frame, the way GPUI's window does.
    win->paint.opacity = 1.f;
    VecClear(win->paint.hits);
    // Last frame's scroll boxes are kept one frame more, for a lazy list to
    // read the viewport it was given before it decides how many rows to
    // build; see Window::prevScrolls. The two storages trade places rather
    // than copy: Vec's copy constructor is a deep one.
    {
        Vec<ScrollRect>& now = win->paint.scrolls;
        Vec<ScrollRect>& was = win->prevScrolls;
        int len = now.len, cap = now.cap;
        ScrollRect* els = now.els;
        now.len = was.len;
        now.cap = was.cap;
        now.els = was.els;
        was.len = len;
        was.cap = cap;
        was.els = els;
    }
    VecClear(win->paint.scrolls);
    VecClear(win->paint.texts);
    VecClear(win->paint.inputs);
    win->paint.textDocLen = 0;
    win->paint.selA = -1;
    win->paint.selB = -1;
    win->paint.hoverId = win->hoverId;
    win->paint.dragOverId = win->dragOverId;
    win->paint.dragKind = win->activeDrag.kind;
    // clicked_state.element: the element the press landed on, held until the
    // button comes back up. Unlike the hover it does not move with the
    // pointer, so a press that slides off still paints its active style.
    win->paint.activeId = win->mouseDown ? win->pressedId : 0;
    win->paint.focusId = win->focusId;
    win->paint.mouseX = win->mouseX;
    win->paint.mouseY = win->mouseY;
    win->paint.scrollDragId = win->mouseDown ? win->scrollDragId : 0;
    win->paint.scrollDragHorizontal = win->scrollDragHorizontal;
    win->paint.picking = win->inspector.picking;
    win->paint.wantsAnimFrame = false;
    win->paint.pickHit = false;
    win->paint.paintDepth = 0;
    win->paint.hitParent = -1;
    win->paint.hasHitMask = false;
    win->paint.hitMask = {};
    win->paint.pickTier = 0;
    win->paint.pick = {};
    if (win->inspector.pending) {
        // The press is what this frame picks against, not the pointer.
        win->paint.mouseX = win->inspector.pendingX;
        win->paint.mouseY = win->inspector.pendingY;
    }
    TextMeasBeginFrame(&win->paint);
    // Remember whether this frame's trap was already open, then let the
    // current element tree declare its trap. A newly appearing trap takes
    // focus; an existing one only constrains Tab while focus remains in it.
    win->previousTrap = win->pendingTrap;
    win->pendingTrap = 0;

    // Whatever the view pointed win->input at is the focused field. Start its
    // caret and stop the one that lost focus, so no app has to. Rust hangs
    // this off InputState::on_focus / on_blur, which is where InputFocus does
    // it too; this is the same handoff for a view that points win->input at a
    // field itself rather than calling InputFocus.
    if (win->input != win->prevInput) {
        if (win->prevInput) {
            BlinkStop(win->app, win, &win->prevInput->blink);
        }
        if (win->input) {
            BlinkStart(win->app, win, &win->input->blink);
        }
        win->prevInput = win->input;
    }

    // AutoScroll's tick. Rust spawns a 16 ms background task per state; the
    // frame is that clock here, so one tick is one frame and the request for
    // the next keeps it running while the pointer stays out at the edge. The
    // selection is re-run at the pointer's last place, since the content has
    // moved under it.
    if (win->input && win->input->autoScroll.IsActive() && win->mouseDown) {
        InputState* s = win->input;
        float was = s->scrollY;
        s->scrollY += s->autoScroll.delta;
        if (s->scrollY < 0) {
            s->scrollY = 0;
        }
        float most = s->contentH - s->viewH;
        if (most < 0) {
            most = 0;
        }
        if (s->scrollY > most) {
            s->scrollY = most;
        }
        if (s->scrollY != was && s->autoScroll.hasLastDrag) {
            bool affinity = false;
            int offset =
                InputIndexForPosition(s, &win->paint, s->autoScroll.lastDrag.x,
                                      s->autoScroll.lastDrag.y, &affinity);
            InputSelectToWithAffinity(s, win->app, win, offset, affinity);
        }
        WindowRequestAnimationFrame(win);
    }

    // The window's own selection, before the view builds: an application
    // only says Selectable() on its text, the way Rust has the window drive
    // every registered run.
    WindowSelectionApply(win);

    // The three phases, timed apart, for GPUI_FRAME_BENCH.
    double tBuild0 = TimeNow();
    // The views this frame is made of, collected as they render: what a
    // notify aims at. GPUI's Window::dirty_views is filled the same way.
    win->rendered.len = 0;
    El* root = EntityRender(win->app, win, win->frameArena, win->root);
    if (win->tooltip.IsValid()) {
        El* tooltip =
            EntityRender(win->app, win, win->frameArena, win->tooltip);
        if (tooltip) {
            if (root) {
                root->Child(tooltip);
            } else {
                root = tooltip;
            }
        }
    }
    gFrameBuildSecs = TimeNow() - tBuild0;

    // Every named element's id, joined with its ancestors'. Before layout,
    // because the hit rects paint reads and the focus rects FocusCollect
    // gathers are both keyed by what this fills in.
    IdsCollect(root);

    win->paint.app = win->app;
    win->paint.window = win;
    const RuntimeStyle& th = RuntimeStyleNow(win->app);
    CanvasClear(&win->paint, th.background);
    gFrameLayoutSecs = 0;
    gFramePaintSecs = 0;
    if (root) {
        double tLay0 = TimeNow();
        if (!win->layout) {
            win->layout = LayoutCacheNew();
        }
        LayoutEl(&win->paint, root, 0, 0, dipW, dipH, th.fontSize,
                 th.foreground, win->layout);
        FocusCollect(win, root);
        AccessibilityCollect(root, &win->accessibility);
        // A dialog that has just opened takes focus into itself, which is what
        // Rust gets from tracking focus on the trap container.
        FocusTrapApplyPending(win);
        gFrameLayoutSecs = TimeNow() - tLay0;
        double tPaint0 = TimeNow();
        PaintEl(&win->paint, root);
        gFramePaintSecs = TimeNow() - tPaint0;
    }
    uint64_t accessibilityHash = AccessibilityTreeHash(win->accessibility);
    if (accessibilityHash != win->accessibilityHash) {
        win->accessibilityHash = accessibilityHash;
        PlatAccessibilityTreeChanged(win);
    }
    // A Scrolling scrollbar part-way through its fade wants the next frame.
    // One ask for the whole tree, after it has painted, the way Rust's
    // scrollbar schedules its own idle timer.
    if (win->paint.wantsAnimFrame) {
        WindowRequestAnimationFrame(win);
    }
    // The element the pointer is over while picking, and the one already
    // picked: GPUI paints the same two highlights over everything.
    win->paint.paintLayer = kPaintLayerInspector;
    if (win->inspector.on) {
        const RuntimeStyle& ith = RuntimeStyleNow(win->app);
        if ((win->inspector.picking || win->inspector.pending) &&
            win->paint.pickHit) {
            Bounds b = win->paint.pick.bounds;
            FillRound(&win->paint, b.x, b.y, b.w, b.h, 0,
                      RgbaOpacity(ith.inspectorAccent, 0.2f));
            DrawRoundStroke(&win->paint, b.x, b.y, b.w, b.h, 0, 1,
                            ith.inspectorAccent);
        } else if (win->inspector.hasPick) {
            Bounds b = win->inspector.pick.bounds;
            DrawRoundStroke(&win->paint, b.x, b.y, b.w, b.h, 0, 1,
                            ith.inspectorAccent);
        }
    }

    win->paint.paintLayer = kPaintLayerTree;

    double tEnd0 = TimeNow();
    PaintTargetEnd(&win->paint);
    // The present goes with the painting: on the GPU path it is where the
    // multisampled surface is resolved, which is part of what drawing cost.
    gFramePaintSecs += TimeNow() - tEnd0;
    TextMeasEndFrame(&win->paint);
    LayoutDumpFrame(win, root);

    // Text-selection participants report their geometry while their view
    // renders. Rust schedules this sweep for the next-frame callback after
    // paint, so registration is independent of whether the lifecycle element
    // or a participant painted first.
    WindowSelectionFinishFrame(win);

    // The pick a press asked for is settled against the frame it aimed at.
    if (win->inspector.pending) {
        if (win->paint.pickHit) {
            win->inspector.pick = win->paint.pick;
            win->inspector.hasPick = true;
        }
        win->inspector.pending = false;
        win->inspector.picking = false;
        AppInvalidate(win);
    }

    // The transitions of anything the frame did not build are dropped, which
    // is GPUI's element state going with the element. Something that comes
    // back on screen starts its entrance again rather than resuming one.
    WindowMotionSweep(win);

    // A picture is still on its way: this frame asked image.h for it and got
    // nothing. Nothing else need be keeping the window awake, so arm the
    // clock here — WindowTimerTick repaints while HttpFetchPending is
    // non-zero, and the picture appears the frame after the worker lands.
    if (HttpFetchPending() > 0) {
        PlatSetTimer(win, WindowTimerMs(win));
    }

    // Record the frame for the trace. GPUI times Window::draw, which is this
    // whole function: build the element tree, lay it out, paint it.
    double drawEnd = TimeNow();
    FrameTiming timing;
    timing.drawSecs = (float)(drawEnd - drawStart);
    timing.invalidations = win->invalidations;
    win->invalidations = 0;
    // The present happened inside PaintTargetEnd above, so this is the closest
    // thing to GPUI's `present_end` the runtime has; a frame the scene did not
    // present carries no present time at all.
    bool presented = !(SceneOn() && scene::SkipPresent(&win->paint));
    timing.presentAt = presented ? drawEnd : -1;
    win->frameTrace[win->frameSeq % (uint64_t)kFrameTraceCap] = timing;
    win->lastDrawTime = drawEnd;
    InteractionBenchRecord(win, timing);
    win->frameSeq++;
    FrameBenchTick(win, timing.drawSecs);
}

// The hit rect an element id painted, from the last frame. The tree is
// Whether the frame gave this id a focus handle. CollectFocus walks the tree
// for `FocusId`, so an element that only has `Click(id)` is missing here.
static bool FocusIdIsFocusable(Window* win, int id) {
    for (int i = 0; i < win->focusEls.len; i++) {
        if (win->focusEls[i].id == id) {
            return true;
        }
    }
    return false;
}

// And whether it asked to take focus from a press. GPUI's `track_focus` gives
// an element a handle and a place in the Tab order and nothing else; the
// widgets that focus on a click say so themselves.
static bool FocusIdTakesPress(Window* win, int id) {
    for (int i = 0; i < win->focusEls.len; i++) {
        if (win->focusEls[i].id == id) {
            return win->focusEls[i].focusOnPress;
        }
    }
    return false;
}

// rebuilt every frame, so an id is the only handle that survives one.

static const HitRect* HitRectById(Window* win, int id) {
    if (!win || !id) {
        return nullptr;
    }
    for (int i = win->paint.hits.len - 1; i >= 0; i--) {
        if (win->paint.hits[i].id == id) {
            return &win->paint.hits[i];
        }
    }
    return nullptr;
}

// `hovered` in GPUI is asked of an element — bounds containment on the top
// layer — not of the one box the hit test names, so a wrapper with an
// on_hover hears the pointer arrive even when a button inside it is what was
// hit. The enclosing hit rects are that set: each of them contains the
// pointer, and two absolutely placed siblings are not inside one another, so
// the chain is what bounds containment would have found anyway.
static int HitIndexById(Window* win, int id) {
    if (!win || !id) {
        return -1;
    }
    for (int i = win->paint.hits.len - 1; i >= 0; i--) {
        if (win->paint.hits[i].id == id) {
            return i;
        }
    }
    return -1;
}

// The element and everything it is inside of, innermost first.
static const int kHoverChainMax = 32;
static int HoverChain(Window* win, int ix, int* out) {
    int n = 0;
    while (ix >= 0 && ix < win->paint.hits.len && n < kHoverChainMax) {
        out[n++] = ix;
        ix = win->paint.hits[ix].parent;
    }
    return n;
}

static bool InChain(const int* chain, int n, int ix) {
    for (int i = 0; i < n; i++) {
        if (chain[i] == ix) {
            return true;
        }
    }
    return false;
}

// Only what changed hears anything: a box the pointer was already inside and
// is still inside stays hovered, which is what makes on_hover a pair of edges
// rather than a report every move.
static void WindowHoverChanged(Window* win, int wasId, int nowId) {
    int wasC[kHoverChainMax];
    int nowC[kHoverChainMax];
    int nWas = HoverChain(win, HitIndexById(win, wasId), wasC);
    int nNow = HoverChain(win, HitIndexById(win, nowId), nowC);
    // The handlers are copied out first: one of them can rebuild the tree,
    // and the frame these indices point into goes with it.
    Listener leaving[kHoverChainMax];
    Listener entering[kHoverChainMax];
    int nLeaving = 0;
    int nEntering = 0;
    for (int i = 0; i < nWas; i++) {
        const HitRect& hr = win->paint.hits[wasC[i]];
        if (hr.onHover.IsValid() && !InChain(nowC, nNow, wasC[i])) {
            leaving[nLeaving++] = hr.onHover;
        }
    }
    // Outermost first, so a card hears its trigger before the box around it.
    for (int i = nNow - 1; i >= 0; i--) {
        const HitRect& hr = win->paint.hits[nowC[i]];
        if (hr.onHover.IsValid() && !InChain(wasC, nWas, nowC[i])) {
            entering[nEntering++] = hr.onHover;
        }
    }
    HoverEvent left = {false};
    HoverEvent entered = {true};
    for (int i = 0; i < nLeaving; i++) {
        ListenerCall(win->app, win, leaving[i], &left);
    }
    for (int i = 0; i < nEntering; i++) {
        ListenerCall(win->app, win, entering[i], &entered);
    }
}

// ─── input ────────────────────────────────────────────────────────────────

// The press is this window's for as long as the button is down: GPUI grabs
// the pointer so a drag can leave the window and still be heard, and the
// release that ends it is the one that must never go missing.
static void SetMouseDown(Window* win, bool down) {
    if (win->mouseDown == down) {
        return;
    }
    win->mouseDown = down;
    PlatSetMouseCapture(win, down);
}

static bool SliderKeyStep(Window* win, int key, bool ctrl, bool alt);
static bool SemanticKeyStep(Window* win, int key, bool ctrl, bool alt);

bool WindowKeyDown(Window* win, int key, bool shift, bool ctrl, bool alt,
                   bool platform, bool function) {
    if (!win) {
        return false;
    }
    // The focused field gets the chord first, as GPUI dispatches an action to
    // whatever has focus before anything else sees the key. The view's own
    // subscription still hears it — that is Rust's cx.propagate(), which every
    // action the input does not consume ends with — but a key the field ate is
    // not also an Enter on the focused element.
    //
    // Unless a sequence is half-finished: the rest of a binding written as
    // "ctrl-k ctrl-o" belongs to the keymap and to nothing else, which is
    // what GPUI's matcher running ahead of the text input buys. Both the
    // field and the page's own Copy stand aside for it.
    bool held = KeymapPending();
    win->eatChar = false;
    bool eaten = false;

    // The chord, resolved once. The matcher holds a half-finished sequence on
    // itself, so asking it twice for one keystroke would append the chord
    // twice; the answer is taken here and handed to whoever wants it.
    intptr_t actionArg = 0;
    bool actionPending = false;
    uint32_t action = 0;
    if (!held) {
        action = WindowResolveKeyAction(win, key, shift, ctrl, alt, platform,
                                        function, &actionArg, &actionPending);
    }
    if (actionPending) {
        // Begun a sequence: nothing under the keymap sees this keystroke.
        win->eatChar = true;
        win->eatReturn = false;
        AppInvalidate(win);
        return true;
    }

    // The focused field first, which is where its own key context puts it:
    // the bindings that resolved above are the ones state.rs installs, and
    // `Input` is the innermost context on the way out from the field.
    if (!held && action && win->input && win->input->focused) {
        InputAction act = InputActionOf(action, actionArg);
        // `Enter { shift }` carries the modifier the caret needs.
        bool sh =
            act == InputAction::Enter ? InputEnterShift(actionArg) : shift;
        eaten = InputPerform(win->input, win->app, win, act, sh);
        if (eaten) {
            action = 0; // taken; nothing further looks at it
        }
    }
    // Copy, once the focused field has had its go: a field with a selection
    // of its own copied that, and this is the page's selection — Rust's
    // TextSelection::selected_text, on the same chord. Nothing selected
    // leaves the key to whatever else wants it.
    if (!held && !eaten && key == KeyC && KeySecondary(ctrl, platform) &&
        !shift && !alt) {
        eaten = WindowSelectionCopy(win);
    }
    // The focused slider's arrows, before anything else looks at them: an
    // element bound to a SliderState is a slider whatever else it is.
    if (!held && !eaten && SliderKeyStep(win, key, ctrl, alt)) {
        win->eatChar = true;
        return true;
    }
    // A spinbutton's focused editor inherits the semantic operations from
    // its frame. Up/down invoke the same operation as assistive technology
    // and the two step buttons; horizontal arrows remain editor movement.
    if (!held && !eaten && SemanticKeyStep(win, key, ctrl, alt)) {
        win->eatChar = true;
        return true;
    }
    // div().on_key_down: the focused element's own listener, and then the
    // ones above it, before the keymap resolves the chord. A field that is
    // not a text editor reads keys here — the keymap has no action to give it
    // and `win->input` takes an InputState, which an OTP field is not.
    if (!held && !eaten) {
        KeyEvent kd = {};
        kd.vk = key;
        kd.down = true;
        kd.shift = shift;
        kd.ctrl = ctrl;
        kd.alt = alt;
        kd.platform = platform;
        kd.function = function;
        if (WindowDispatchKeyEvent(win, &kd)) {
            // The character it also arrives as belongs to the handler that
            // took the key, not to whatever is under it.
            win->eatChar = true;
            AppInvalidate(win);
            return true;
        }
    }
    // The keymap, once the focused field has had its go: a field's own
    // editing is Rust's innermost key context, so a binding further out
    // cannot take a keystroke away from it. An action that is handled ends
    // the keystroke here.
    if (!eaten && action && WindowDispatchAction(win, action, actionArg)) {
        // The character the keystroke also arrives as is the keymap's now:
        // the second chord of a sequence is an ordinary letter, and typing it
        // into the field underneath is what the binding was there to stop.
        win->eatChar = true;
        win->eatReturn = false;
        AppInvalidate(win);
        return true;
    }
    // The focus ring, last of the three: `tab` is bound on the window in
    // GPUI, the outermost key context there is, so a field indenting with it
    // and a binding over it both come first. Only a tab nobody wanted walks
    // the focus.
    if (!eaten && key == KeyTab) {
        FocusTrapTab(win, shift);
        AppInvalidate(win);
        return true;
    }
    bool windowHandled = false;
    if (win->onKey.IsValid()) {
        KeyEvent ev = {};
        ev.vk = key;
        ev.down = true;
        ev.shift = shift;
        ev.ctrl = ctrl;
        ev.alt = alt;
        ev.platform = platform;
        ev.function = function;
        ListenerCall(win->app, win, win->onKey, &ev);
        windowHandled = !ev.propagate;
    }
    // Enter and Space both activate the focused element, and the press only
    // arms that: the click is made from the release, the same as the mouse's.
    // GPUI keeps the focus generation the keystroke went down at as
    // `pending_keyboard_down`, and its key-down listener clears it for every
    // other key — a chord that ran an action is not half of an activation.
    // A focused field takes the space as text instead, so it never arms.
    bool activates = (key == KeyReturn && !win->eatReturn) ||
                     (key == KeySpace && !(win->input && win->input->focused));
    bool modified = shift || ctrl || alt || platform || function;
    win->keyPressPending = activates && !modified && !eaten && win->focusId;
    win->keyPressGen = win->focusGen;
    win->eatReturn = false;
    AppInvalidate(win);
    return eaten || windowHandled || win->keyPressPending;
}

void WindowKeyUp(Window* win, int key, bool shift, bool ctrl, bool alt,
                 bool platform, bool function) {
    if (!win) {
        return;
    }
    // div().on_key_up: the focused element's own listener and the ones above
    // it, before the release is read as a keyboard activation. It never eats
    // the activation — GPUI's `on_key_up` observes the release rather than
    // claiming it — so what a handler stops is only the rest of this chain.
    {
        KeyEvent ku = {};
        ku.vk = key;
        ku.down = false;
        ku.shift = shift;
        ku.ctrl = ctrl;
        ku.alt = alt;
        ku.platform = platform;
        ku.function = function;
        WindowDispatchKeyUpEvent(win, &ku);
    }
    // The release consumes the pending press whatever it is: a clean
    // activation makes the click, and anything else — another key coming up
    // mid-press, a modifier that has since gone down — cancels it.
    bool pending = win->keyPressPending;
    int gen = win->keyPressGen;
    win->keyPressPending = false;
    if (!ClickFromKeyRelease(pending, gen, win->focusGen, key,
                             shift || ctrl || alt || platform || function)) {
        return;
    }
    // GPUI registers the keyboard activation on the painted element, so a
    // focus with nothing on screen behind it activates nothing.
    const HitRect* focused = HitRectById(win, win->focusId);
    if (!focused) {
        return;
    }
    // ClickEvent::Keyboard: no pointer was involved, so the position is the
    // element's own box, and there is no count or modifier to carry.
    ClickEvent ev = {0, 0, MouseButton::Left, win->focusId};
    ev.keyboard = true;
    ev.keyboardKey = key;
    ev.x = focused->bounds.CenterX();
    ev.y = focused->bounds.CenterY();
    ev.el = focused->bounds;
    // Both halves of what a click on it would have run, and only those: a
    // keyboard click reaches the element's own listeners, never the window's
    // unhandled-click path — nothing was clicked outside anything.
    if (focused->listener.IsValid()) {
        ListenerCall(win->app, win, focused->listener, &ev);
    }
    if (focused->onClick.IsValid()) {
        focused->onClick.Call();
    }
    AppInvalidate(win);
}

void WindowChar(Window* win, uint32_t ch, bool ctrl, bool alt) {
    if (!win) {
        return;
    }
    if (win->onKey.IsValid() && ch >= 32) {
        KeyEvent ev = {};
        ev.ch = ch;
        ev.down = true;
        ev.ctrl = ctrl;
        ev.alt = alt;
        ListenerCall(win->app, win, win->onKey, &ev);
    }
    // A typed character reaches the focused field the way GPUI hands one to
    // the focused EntityInputHandler. The control codes are keys, not text:
    // backspace, tab, return and escape all came through WindowKeyDown
    // already, and Ctrl+letter arrives here as 1..26.
    bool ate = win->eatChar;
    win->eatChar = false;
    if (!ate && win->input && win->input->focused && ch >= 32 && ch != 127 &&
        !ctrl && !alt) {
        InputTypeChar(win->input, win->app, win, ch);
        ate = true;
    }
    // The focused element's own key listener hears the character half too:
    // a digit typed into an OTP field arrives as a WM_CHAR and never as a
    // chord the keymap could resolve.
    if (!ate && ch >= 32 && ch != 127 && !ctrl && !alt) {
        KeyEvent kd = {};
        kd.ch = ch;
        kd.down = true;
        if (WindowDispatchKeyEvent(win, &kd)) {
            AppInvalidate(win);
            return;
        }
    }
    AppInvalidate(win);
}

// cx.emit(SliderEvent::..) — the subscription lives on the state, the way
// InputState::onChange does.
static void SliderEmit(Window* win, SliderState* s, SliderEventKind kind) {
    if (!s->onChange.IsValid()) {
        return;
    }
    SliderEvent ev = {kind, s->value};
    ListenerCall(win->app, win, s->onChange, &ev);
}

const AccessibilityNode* WindowAccessibilityNode(const Window* win,
                                                 uint32_t nodeId) {
    if (!win || !nodeId) {
        return nullptr;
    }
    for (int i = 0; i < win->accessibility.len; i++) {
        if (win->accessibility[i].id == nodeId) {
            return &win->accessibility[i];
        }
    }
    return nullptr;
}

bool WindowAccessibilityPerform(Window* win, uint32_t nodeId,
                                AccessibilityAction action, Str value) {
    const AccessibilityNode* found = WindowAccessibilityNode(win, nodeId);
    if (!found) {
        return false;
    }
    // A listener may invalidate the window, so keep the frame record by value
    // before invoking application code.
    AccessibilityNode node = *found;
    uint8_t required = AccessibilityActionNone;
    switch (action) {
        case AccessibilityAction::Default:
            required = AccessibilityActionDefault;
            break;
        case AccessibilityAction::Focus:
            required = AccessibilityActionFocus;
            break;
        case AccessibilityAction::Increment:
            required = AccessibilityActionIncrement;
            break;
        case AccessibilityAction::Decrement:
            required = AccessibilityActionDecrement;
            break;
        case AccessibilityAction::SetValue:
            required = AccessibilityActionSetValue;
            break;
    }
    if (!(node.actions & required)) {
        return false;
    }
    ClickEvent ev = {};
    ev.x = node.bounds.CenterX();
    ev.y = node.bounds.CenterY();
    ev.id = node.clickId;
    ev.el = node.bounds;
    ev.keyboard = true;
    if (action == AccessibilityAction::Focus) {
        WindowSetFocusId(win, node.focusId);
        AppInvalidate(win);
        return true;
    }
    if (action == AccessibilityAction::Increment ||
        action == AccessibilityAction::Decrement) {
        Listener fn = action == AccessibilityAction::Increment
                          ? node.accessibilityIncrement
                          : node.accessibilityDecrement;
        Func0 direct = action == AccessibilityAction::Increment
                           ? node.accessibilityIncrementDirect
                           : node.accessibilityDecrementDirect;
        if (fn.IsValid()) {
            ListenerCall(win->app, win, fn, &ev);
        } else if (direct.IsValid()) {
            direct.Call();
        } else {
            int dir = action == AccessibilityAction::Increment ? 1 : -1;
            if (!SliderStepBy(node.slider, dir, false)) {
                return false;
            }
            SliderEmit(win, node.slider, SliderEventKind::Change);
        }
        AppInvalidate(win);
        return true;
    }
    if (action == AccessibilityAction::SetValue) {
        InputReplaceAll(node.input, win->app, win, value);
        AppInvalidate(win);
        return true;
    }

    if (node.accessibilityDefault.IsValid()) {
        ListenerCall(win->app, win, node.accessibilityDefault, &ev);
        AppInvalidate(win);
        return true;
    }
    if (node.listener.IsValid()) {
        ListenerCall(win->app, win, node.listener, &ev);
    }
    if (node.onClick.IsValid()) {
        node.onClick.Call();
    }
    if (node.clickAction) {
        WindowDispatchAction(win, node.clickAction, node.clickActionArg);
    }
    AppInvalidate(win);
    return true;
}

bool WindowAccessibilitySetNumericValue(Window* win, uint32_t nodeId,
                                        float value) {
    const AccessibilityNode* found = WindowAccessibilityNode(win, nodeId);
    if (!found || found->info.disabled || !found->slider) {
        return false;
    }
    AccessibilityNode node = *found;
    SliderState* slider = node.slider;
    float lo = slider->value.range ? slider->value.lo : slider->min;
    if (value < lo) {
        value = lo;
    }
    if (value > slider->max) {
        value = slider->max;
    }
    if (slider->step > 0) {
        value = roundf(value / slider->step) * slider->step;
        if (value < lo) {
            value = lo;
        }
        if (value > slider->max) {
            value = slider->max;
        }
    }
    if (value == slider->value.End()) {
        return true;
    }
    SliderValue next = slider->value;
    SliderValueSetEnd(&next, value);
    SliderSetValue(slider, next);
    SliderEmit(win, slider, SliderEventKind::Change);
    AppInvalidate(win);
    return true;
}

// SliderTrack::on_mouse_down and its on_drag_move: a press jumps the value to
// where it landed and takes the nearer end of a range; every move until the
// release keeps that end following the pointer.
static void SliderPress(Window* win, const HitRect* hit, Point at) {
    SliderState* s = hit->slider;
    // The rail reported its own box when it painted; a slider built without
    // one maps against the box that took the press instead.
    if (s->bounds.w <= 0 || s->bounds.h <= 0) {
        SliderSetBounds(s, hit->bounds);
    }
    s->dragStart = SliderIsStartAt(s, hit->sliderAxis, at);
    if (SliderUpdateByPosition(s, hit->sliderAxis, at, s->dragStart)) {
        SliderEmit(win, s, SliderEventKind::Change);
    }
    AppInvalidate(win);
}

// InputState::on_mouse_down. A press focuses the field, puts the caret where
// it landed and opens a drag; shift extends the selection instead of dropping
// it, a second press takes the word and a third the line. A press anywhere
// else blurs whatever had focus, which is what GPUI's focus handle does.
static void InputPress(Window* win, const MouseDownEvent& in) {
    InputState* s = InputAtPosition(&win->paint, in.x, in.y);
    if (!s) {
        if (win->input) {
            InputBlur(win->input, win->app, win);
        }
        return;
    }
    if (s->disabled) {
        return;
    }
    // A press on a fold chevron toggles it and goes no further — Rust's icon
    // handler calls cx.stop_propagation() so the press never reaches the text
    // underneath and moves the caret.
    int foldLine = InputFoldIconAt(s, in.x, in.y);
    if (foldLine >= 0) {
        InputToggleFold(s, win->app, win, foldLine);
        return;
    }
    // "Clear inline completion on any mouse interaction."
    InputClearInlineCompletion(s);
    if (!s->focused) {
        InputFocus(s, win->app, win);
    }
    bool lineEndAffinity = false;
    int offset =
        InputIndexForPosition(s, &win->paint, in.x, in.y, &lineEndAffinity);
    // `M::on_click(..)`, which is go-to-definition and returns true when it
    // took the press — so the same click does not also move the caret.
    if (InputClickDefinition(s, win->app, win, offset,
                             in.modifiers.Secondary())) {
        return;
    }
    if (in.clickCount >= 3) {
        InputSelectLine(s, win->app, win, offset);
    } else if (in.clickCount == 2) {
        InputSelectWord(s, win->app, win, offset);
    } else if (in.modifiers.shift) {
        InputSelectToWithAffinity(s, win->app, win, offset, lineEndAffinity);
    } else {
        InputMoveToWithAffinity(s, win->app, win, offset, lineEndAffinity);
    }
    s->selecting = true;
}

// ─── scrollbar ────────────────────────────────────────────────────────────
//
// crates/base/src/scrollbar.rs installs a MouseDownEvent handler over the
// bar's bounds and a MouseMoveEvent one for the drag. The arithmetic is in
// src/base/scrollbar.cpp; this is the routing.

// Whether a box scrolls at all along one axis, which is what decides if it
// has a bar to aim at.
static bool ScrollsY(const ScrollRect& s) {
    return s.contentH > s.bounds.h + 1.f;
}
static bool ScrollsX(const ScrollRect& s) {
    return s.contentW > s.bounds.w + 1.f;
}

// An offset that stays inside the content, which is Rust's clamp on the
// scroll handle rather than anything the wheel does.
static float ClampScroll(float off, float content, float viewport) {
    float most = content - viewport;
    if (most < 0) {
        most = 0;
    }
    if (off > most) {
        off = most;
    }
    return off < 0 ? 0 : off;
}

// The scrolled box whose scrollbar band the pointer is in, or null, and which
// of its two bars. Innermost first, the way the hit test reads its rects.
static ScrollRect* ScrollbarAt(PaintCtx* ctx, float x, float y,
                               bool* horizontal) {
    for (int i = ctx->scrolls.len - 1; i >= 0; i--) {
        const ScrollRect& s = ctx->scrolls[i];
        if (!s.onScroll.IsValid() && !s.input) {
            continue;
        }
        // A bar that has faded out keeps its band and its layout and takes no
        // press: scrollbar.rs disables the hitbox while it is hidden.
        if (!s.barVisible) {
            continue;
        }
        if (s.barY && ScrollsY(s) && y >= s.bounds.y &&
            y <= s.bounds.Bottom() && x >= s.bounds.Right() - s.trackWidth &&
            x <= s.bounds.Right()) {
            *horizontal = false;
            return &ctx->scrolls[i];
        }
        if (s.barX && ScrollsX(s) && x >= s.bounds.x && x <= s.bounds.Right() &&
            y >= s.bounds.Bottom() - s.trackWidth && y <= s.bounds.Bottom()) {
            *horizontal = true;
            return &ctx->scrolls[i];
        }
    }
    return nullptr;
}

static void ScrollbarEmit(Window* win, ScrollRect* s, float offsetX,
                          float offsetY) {
    // A text field owns its own offset — Rust's editor scrollbar reaches the
    // state's scroll handle the same way, rather than telling a view about it.
    if (s->input) {
        s->input->scrollX = ClampScroll(offsetX, s->contentW, s->bounds.w);
        s->input->scrollY = ClampScroll(offsetY, s->contentH, s->bounds.h);
        s->scrollX = s->input->scrollX;
        s->scrollY = s->input->scrollY;
        AppInvalidate(win);
        return;
    }
    offsetX = ClampScroll(offsetX, s->contentW, s->bounds.w);
    offsetY = ClampScroll(offsetY, s->contentH, s->bounds.h);
    s->scrollX = offsetX;
    s->scrollY = offsetY;
    ScrollEvent ev = {s->id, offsetY, offsetX};
    ListenerCall(win->app, win, s->onScroll, &ev);
    AppInvalidate(win);
}

// The press. Inside the thumb it opens a drag and keeps where it landed;
// anywhere else on the track the thumb jumps its centre to the press, which
// is Rust's two branches on `thumb_bounds.contains`. Both bars go through
// this once, along whichever axis they are.
static void ScrollbarPress(Window* win, ScrollRect* s, float x, float y,
                           bool horizontal) {
    float track = horizontal ? s->bounds.w : s->bounds.h;
    float content = horizontal ? s->contentW : s->contentH;
    float origin = horizontal ? s->bounds.x : s->bounds.y;
    float at = horizontal ? x : y;
    float marginEnd = horizontal && s->barY && ScrollsY(*s) ? s->trackWidth : 0;
    float rawThumb =
        ScrollbarThumbSize(track, track, content, s->thumbMinLength);
    float rawStart =
        origin + ScrollbarThumbPos(track, rawThumb,
                                   horizontal ? s->scrollX : s->scrollY, track,
                                   content, marginEnd);
    float thumbStart = rawStart + s->thumbInset;
    float thumbLength = rawThumb - s->thumbInset * 2.f;
    if (thumbLength < 0) thumbLength = 0;
    bool crossInside = horizontal ? y <= s->bounds.Bottom() - s->thumbInset
                                  : x <= s->bounds.Right() - s->thumbInset;
    bool onThumb =
        crossInside && at >= thumbStart && at <= thumbStart + thumbLength;
    if (!onThumb) {
        // The painted hover thumb is wider than the resting one. A press on
        // the extra pixels is still a grab, not a jump down the track.
        rawThumb =
            ScrollbarThumbSize(track, track, content, s->thumbHoverMinLength);
        rawStart =
            origin + ScrollbarThumbPos(track, rawThumb,
                                       horizontal ? s->scrollX : s->scrollY,
                                       track, content, marginEnd);
        float hoverStart = rawStart + s->thumbHoverInset;
        float hoverLength = rawThumb - s->thumbHoverInset * 2.f;
        if (hoverLength < 0) hoverLength = 0;
        bool hoverCross = horizontal
                              ? y <= s->bounds.Bottom() - s->thumbHoverInset
                              : x <= s->bounds.Right() - s->thumbHoverInset;
        if (hoverCross && at >= hoverStart && at <= hoverStart + hoverLength) {
            onThumb = true;
            thumbStart = hoverStart;
            thumbLength = hoverLength;
        }
    }
    if (onThumb) {
        // The pointer has already selected thumb_hover in prepaint. Resolve
        // that state's potentially different inset and minimum before
        // retaining the grab point.
        rawThumb =
            ScrollbarThumbSize(track, track, content, s->thumbHoverMinLength);
        rawStart =
            origin + ScrollbarThumbPos(track, rawThumb,
                                       horizontal ? s->scrollX : s->scrollY,
                                       track, content, marginEnd);
        thumbStart = rawStart + s->thumbHoverInset;
        thumbLength = rawThumb - s->thumbHoverInset * 2.f;
        if (thumbLength < 0) thumbLength = 0;
        crossInside = horizontal ? y <= s->bounds.Bottom() - s->thumbHoverInset
                                 : x <= s->bounds.Right() - s->thumbHoverInset;
        onThumb =
            crossInside && at >= thumbStart && at <= thumbStart + thumbLength;
    }
    if (onThumb) {
        win->scrollDragId = s->id;
        win->scrollDragHorizontal = horizontal;
        win->scrollDragGrab = at - thumbStart;
        win->scrollDragInput = s->input;
        return;
    }
    // A track press moves once; only pressing the thumb starts a drag.
    float off = ScrollbarOffsetForTrackPress(at, origin, track, thumbLength,
                                             track, content);
    ScrollbarEmit(win, s, horizontal ? off : s->scrollX,
                  horizontal ? s->scrollY : off);
}

// The scroll rect of an id, from the frame on screen. Zero is "not a
// handle": several boxes can have it, and a lookup would grab the wrong one.
static ScrollRect* ScrollRectById(Window* win, int id) {
    if (id == 0) {
        return nullptr;
    }
    for (int i = win->paint.scrolls.len - 1; i >= 0; i--) {
        if (win->paint.scrolls[i].id == id) {
            return &win->paint.scrolls[i];
        }
    }
    return nullptr;
}

const ScrollRect* WindowLastScrollRect(const Window* win, int id) {
    if (!win || id == 0) {
        return nullptr;
    }
    for (int i = win->prevScrolls.len - 1; i >= 0; i--) {
        if (win->prevScrolls[i].id == id) {
            return &win->prevScrolls[i];
        }
    }
    return nullptr;
}

static ScrollRect* ScrollRectForDrag(Window* win) {
    ScrollRect* s = ScrollRectById(win, win->scrollDragId);
    if (s) {
        return s;
    }
    if (!win->scrollDragInput) {
        return nullptr;
    }
    for (int i = win->paint.scrolls.len - 1; i >= 0; i--) {
        if (win->paint.scrolls[i].input == win->scrollDragInput) {
            return &win->paint.scrolls[i];
        }
    }
    return nullptr;
}

static void ScrollbarDrag(Window* win, float x, float y) {
    ScrollRect* s = ScrollRectForDrag(win);
    if (!s || (!s->onScroll.IsValid() && !s->input)) {
        return;
    }
    bool horizontal = win->scrollDragHorizontal;
    float track = horizontal ? s->bounds.w : s->bounds.h;
    float content = horizontal ? s->contentW : s->contentH;
    float origin = horizontal ? s->bounds.x : s->bounds.y;
    float at = horizontal ? x : y;
    float rawThumb =
        ScrollbarThumbSize(track, track, content, s->thumbActiveMinLength);
    float thumb = rawThumb - s->thumbActiveInset * 2.f;
    if (thumb < 0) thumb = 0;
    float marginEnd = horizontal && s->barY && ScrollsY(*s) ? s->trackWidth : 0;
    float off = ScrollbarOffsetForDrag(at, win->scrollDragGrab, origin, track,
                                       thumb, track, content, marginEnd);
    ScrollbarEmit(win, s, horizontal ? off : s->scrollX,
                  horizontal ? s->scrollY : off);
}

static void SliderDrag(Window* win, const HitRect* hit, Point at) {
    SliderState* s = hit->slider;
    if (SliderUpdateByPosition(s, hit->sliderAxis, at, s->dragStart)) {
        SliderEmit(win, s, SliderEventKind::Change);
        AppInvalidate(win);
    }
}

// Slider's on_mouse_up + on_mouse_up_out, which Rust puts on the root so a
// release counts wherever it lands. Every slider the frame painted is asked;
// handle_release clears the flag, so one that was not being dragged says
// nothing and a state bound twice only answers once.
static void SliderRelease(Window* win) {
    for (int i = 0; i < win->paint.hits.len; i++) {
        SliderState* s = win->paint.hits[i].slider;
        if (s && SliderHandleRelease(s)) {
            SliderEmit(win, s, SliderEventKind::Release);
            AppInvalidate(win);
        }
    }
}

// slider.rs's `on_a11y_action(Increment | Decrement)`, on the keyboard. There
// is no accessibility layer in this tree for the role and the aria values to
// live in, and the half that is reachable is the half a keyboard user needs:
// the arrows over the focused track, stepping by the slider's own step. Which
// end of a range moves is the one the last press took, which is what a reader
// who just dragged one of them expects to keep moving.
static bool SliderKeyStep(Window* win, int key, bool ctrl, bool alt) {
    if (ctrl || alt || !win->focusId) {
        return false;
    }
    int dir = 0;
    if (key == KeyRight || key == KeyUp) {
        dir = 1;
    } else if (key == KeyLeft || key == KeyDown) {
        dir = -1;
    }
    if (dir == 0) {
        return false;
    }
    for (int i = 0; i < win->paint.hits.len; i++) {
        const HitRect& hr = win->paint.hits[i];
        if (hr.id != win->focusId || !hr.slider) {
            continue;
        }
        if (SliderStepBy(hr.slider, dir,
                         hr.slider->value.range && hr.slider->dragStart)) {
            SliderEmit(win, hr.slider, SliderEventKind::Change);
        }
        // The keystroke was the slider's whether or not it could move: an
        // arrow on a slider at its limit is not also a walk of the focus.
        AppInvalidate(win);
        return true;
    }
    return false;
}

static bool SemanticKeyStep(Window* win, int key, bool ctrl, bool alt) {
    if (ctrl || alt || !win->focusId || (key != KeyUp && key != KeyDown)) {
        return false;
    }
    for (int i = 0; i < win->focusEls.len; i++) {
        const FocusRect& fr = win->focusEls[i];
        if (fr.id != win->focusId) {
            continue;
        }
        Listener fn = key == KeyUp ? fr.accessibilityIncrement
                                   : fr.accessibilityDecrement;
        Func0 direct = key == KeyUp ? fr.accessibilityIncrementDirect
                                    : fr.accessibilityDecrementDirect;
        if (!fn.IsValid() && !direct.IsValid()) {
            return false;
        }
        ClickEvent ev = {};
        ev.x = fr.bounds.CenterX();
        ev.y = fr.bounds.CenterY();
        ev.el = fr.bounds;
        ev.keyboard = true;
        ev.keyboardKey = key;
        if (fn.IsValid()) {
            ListenerCall(win->app, win, fn, &ev);
        } else {
            direct.Call();
        }
        AppInvalidate(win);
        return true;
    }
    return false;
}

// How far the pointer travels before a press counts as a drag rather than a
// click. GPUI starts the drag from the first move that leaves the press
// behind; a few DIPs of slack is what a mouse gives a firm click.
static const float kDragThreshold = 4.f;

static void DispatchMouseMove(Window* win, const MouseMoveEvent& in) {
    float x = in.x;
    float y = in.y;
    win->mouseX = x;
    win->mouseY = y;
    win->mouseModifiers = in.modifiers;
    // The hand over a symbol a secondary-hover found a definition for, which
    // is the hitbox `hover_definition_hitbox` inserts. The bounds are last
    // frame's, measured where the symbol was painted.
    for (int i = 0; i < win->paint.inputs.len; i++) {
        InputState* f = win->paint.inputs[i];
        if (f->hoverDef.locations.len > 0 && f->hoverDef.bounds
                                                 .Contains({x, y})) {
            if (win->cursor != CursorKind::Pointer) {
                win->cursor = CursorKind::Pointer;
                PlatSetCursor(win, CursorKind::Pointer);
            }
            AppInvalidate(win);
            return;
        }
    }
    // An I-beam over anything selectable, the way every text view does it.
    // TextHitOffsetAt only answers for text that asked to be Selectable().
    // Anything else, the element under the pointer says what shape it wants —
    // and a drag keeps the shape it started with, so a column edge stays a
    // resize cursor while the pointer runs off it.
    CursorKind want = CursorKind::Arrow;
    const HitRect* under = HitRectById(win, win->pressedId);
    if (!under || !win->mouseDown) {
        under = HitTestRect(&win->paint, x, y);
    }
    // An element that named a shape of its own wins over the I-beam: a link
    // inside a selectable TextView asks for the hand, and in GPUI the
    // cursor_pointer the link pushes is the innermost one and so the one that
    // takes. A control that suppresses selection (a button, a menu row) keeps
    // the arrow rather than inheriting the I-beam of text behind it. Anything
    // that named nothing leaves the selectable text on this stacking layer
    // to say — not the page under a popup.
    if (under && under->cursor != CursorKind::Arrow) {
        want = under->cursor;
    } else if (under && under->suppressTextSelection) {
        want = CursorKind::Arrow;
    } else if (TextHitOffsetIn(&win->paint, x, y, false, -1, nullptr,
                               under ? under->paintLayer : 0) >= 0) {
        want = CursorKind::IBeam;
    }
    if (want != win->cursor) {
        win->cursor = want;
        PlatSetCursor(win, want);
    }
    int id = HitTest(&win->paint, x, y);
    if (id != win->hoverId) {
        // div().on_hover(..): the element the pointer left hears false and the
        // one it entered hears true. Both are read off the frame that is still
        // on screen, before hoverId moves, so the leaving element is still
        // findable.
        const HitRect* now = HitRectById(win, id);
        Str tip = now ? now->tooltip : Str{};
        Bounds tipAt = now ? now->bounds : Bounds{};
        int tipPlacement = now ? now->tooltipPlacement : -1;
        WindowHoverChanged(win, win->hoverId, id);
        win->hoverId = id;
        // El::Tip is a tooltip trigger. Rust's triggers call request_show and
        // request_hide on the window's one overlay; the hover change is where
        // that happens here, since the trigger is a style flag rather than an
        // element that could carry handlers of its own.
        if (tip.s) {
            TooltipRequestShow(win, tip, tipAt, tipPlacement);
        } else {
            TooltipRequestHide(win);
        }
        AppInvalidate(win);
    }
    // The drag half of the window's selection, before the view's own handler
    // so a page that watches moves sees the selection already extended.
    WindowSelectionDrag(win, x, y);
    if (win->onMouseMove.IsValid()) {
        ListenerCall(win->app, win, win->onMouseMove, &in);
    }
    const HitRect* movingOver = HitTestRect(&win->paint, x, y);
    if (movingOver && movingOver->onMouseMove.IsValid()) {
        MouseMoveEvent local = in;
        local.el = movingOver->bounds;
        ListenerCall(win->app, win, movingOver->onMouseMove, &local);
    }
    // Whether this press has become a drag: GPUI starts one from the move
    // that leaves the press behind, not from the press itself.
    if (win->mouseDown && !win->pressedMoved) {
        float dx = x - win->pressedX;
        float dy = y - win->pressedY;
        if (dx * dx + dy * dy > kDragThreshold * kDragThreshold) {
            win->pressedMoved = true;
        }
    }

    // on_drag_move: the element that took the press hears every move until
    // the release, wherever the pointer has got to by then. The press picked
    // up whatever payload the element named with on_drag, and every move
    // carries it back the way DragMoveEvent<T> does.
    // drag_over: which drop target the pointer is over, so an element that
    // takes this kind of drag can show itself while one is in flight.
    if (win->activeDrag.IsValid()) {
        const HitRect* over =
            HitTestDrop(&win->paint, x, y, win->activeDrag.kind);
        win->dragOverId = over ? over->id : 0;
    }
    const HitRect* pressed = HitRectById(win, win->pressedId);
    if (pressed && pressed->onDragMove.IsValid()) {
        DragMoveEvent ev = {pressed->drag, in, pressed->bounds};
        ListenerCall(win->app, win, pressed->onDragMove, &ev);
    }
    if (pressed && pressed->slider) {
        SliderDrag(win, pressed, {x, y});
    }
    // The bar keeps every move until the release, wherever the pointer has
    // got to — the same rule the slider and on_drag_move go by. A zero
    // scrollId still drags when the press named the InputState.
    if (win->mouseDown && (win->scrollDragId || win->scrollDragInput)) {
        ScrollbarDrag(win, x, y);
    }
    // InputState::on_drag_move: the field that took the press keeps every move
    // until the release, wherever the pointer has got to. The button being
    // held is `win->mouseDown` rather than the move event's own flag, the same
    // signal the slider drag and on_drag_move above go by.
    if (win->input && win->input->selecting && win->mouseDown) {
        InputState* s = win->input;
        s->autoScroll.lastDrag = Point{x, y};
        s->autoScroll.hasLastDrag = true;
        bool affinity = false;
        int offset = InputIndexForPosition(s, &win->paint, x, y, &affinity);
        InputSelectToWithAffinity(s, win->app, win, offset, affinity);
        // A drag that has reached the edge of a field with somewhere to go
        // keeps scrolling it until the pointer comes back in. A single-line
        // field has nowhere to go, which is why Rust asks the same question.
        float delta = 0;
        if (!InputIsSingleLine(s) &&
            AutoScrollComputeDelta(y, s->inputBounds, &delta)) {
            s->autoScroll.Set(delta);
            AppInvalidate(win);
        } else {
            s->autoScroll.SetNone();
        }
    }
    if (win->mouseDown) {
        AppInvalidate(win);
    }
}

// How far the pointer may wander between two presses and still be one run.
// Windows asks the OS with SM_CXDOUBLECLK, which is 4 px on every default
// install; the other two have no setting to ask for.
static const float kClickSlop = 4;
// The title bar is 34 tall; a double click on empty chrome maximizes.
static const float kCaptionH = 34;

int WindowClickCount(Window* win, float x, float y, MouseButton button) {
    if (!win) {
        return 1;
    }
    double now = TimeNow();
    float dx = x - win->lastDownX;
    float dy = y - win->lastDownY;
    bool sameRun = win->clickRun > 0 && button == win->lastDownButton &&
                   now - win->lastDownAt <= PlatDoubleClickMs() / 1000.0 &&
                   dx * dx + dy * dy <= kClickSlop * kClickSlop;
    win->clickRun = sameRun ? win->clickRun + 1 : 1;
    win->lastDownAt = now;
    win->lastDownX = x;
    win->lastDownY = y;
    win->lastDownButton = button;
    return win->clickRun;
}

int WindowCurrentClickCount(Window* win) {
    return win && win->clickRun > 0 ? win->clickRun : 1;
}

// A press that something else took: whatever was pending stops being so, and
// the release that follows makes no click.
static void ClearPendingClick(Window* win) {
    win->pressPending = false;
    win->pressedId = 0;
    win->pressedMoved = false;
}

// ─── the dispatch chain ──────────────────────────────────────────────────

// cx.stop_propagation().
void WindowStopPropagation(Ctx* cx) {
    if (cx && cx->win) {
        cx->win->stopPropagation = true;
    }
}

// The chain of hit rects the pointer is inside, leaf first. Not every box
// that contains the point: two absolutely placed siblings can overlap without
// either being inside the other, so the chain is the one the paint recorded.
static void HitChain(Window* win, float x, float y, Vec<int>* out) {
    VecClear(*out);
    int leaf = -1;
    for (int i = win->paint.hits.len - 1; i >= 0; i--) {
        if (win->paint.hits[i].bounds.Contains({x, y})) {
            leaf = i;
            break;
        }
    }
    for (int i = leaf; i >= 0; i = win->paint.hits[i].parent) {
        VecAppend(*out, i);
    }
}

// Window::dispatch_event: the chain outside-in for the Capture phase, then
// inside-out for the Bubble phase, stopping wherever a handler said to.
// `pick` answers the handler an element registered, or an invalid Listener.
template <typename Ev, typename Pick>
static void DispatchChain(Window* win, const Vec<int>& chain, Ev* ev, Pick pick,
                          bool stopMouseDown = false) {
    win->stopPropagation = false;
    for (int k = chain.len - 1; k >= 0 && !win->stopPropagation; k--) {
        const HitRect& hr = win->paint.hits[chain[k]];
        Listener l = pick(hr, DispatchPhase::Capture);
        if (l.IsValid()) {
            ev->phase = DispatchPhase::Capture;
            ev->el = hr.bounds;
            ListenerCall(win->app, win, l, ev);
        }
    }
    for (int k = 0; k < chain.len && !win->stopPropagation; k++) {
        const HitRect& hr = win->paint.hits[chain[k]];
        Listener l = pick(hr, DispatchPhase::Bubble);
        if (l.IsValid()) {
            ev->phase = DispatchPhase::Bubble;
            ev->el = hr.bounds;
            ListenerCall(win->app, win, l, ev);
        }
        if (stopMouseDown && hr.stopMouseDown && ev->IsFocusing()) {
            win->stopPropagation = true;
        }
    }
    win->stopPropagation = false;
}

// Every element whose box does not contain this press hears it. This walks
// the frame rather than the hit chain: GPUI registers on_mouse_down_out
// against an element's hitbox and it observes empty window space and sibling
// overlays too. It runs after the ordinary mouse-down chain, matching
// on_mouse_up_out and ensuring an open popover's trigger closes it before the
// content (which is necessarily elsewhere) observes the same press.
static void DispatchMouseDownOut(Window* win, const MouseDownEvent& in) {
    for (int i = 0; i < win->paint.hits.len; i++) {
        const HitRect& hr = win->paint.hits[i];
        if (hr.onMouseDownOut.IsValid() && !hr.bounds.Contains({in.x, in.y})) {
            ListenerCall(win->app, win, hr.onMouseDownOut, &in);
        }
    }
}

static void DispatchMouseDown(Window* win, const MouseDownEvent& in) {
    float x = in.x;
    float y = in.y;
    // text_selection.rs resets this in capture phase. Controls that own the
    // press set it while the event bubbles; selection begins afterwards.
    BaseResetTextSelectionSuppression(win->app);
    // ManagedTooltipExt hides the active overlay immediately on a primary
    // press, even when the pointer remains over the same trigger.
    if (in.IsFocusing()) {
        TooltipHide(win);
    }
    if (win->onMouseDown.IsValid()) {
        ListenerCall(win->app, win, win->onMouseDown, &in);
    }
    // Only the left button clicks an element. GPUI routes a press of any
    // button to whatever asked for that button, and never turns a right one
    // into a click; so a non-left press reaches the element's own
    // on_mouse_down — which is how a popover opens on the right button — and
    // stops before the click path, the focus move and the caret below.
    if (!in.IsFocusing()) {
        // Over the whole chain, not just the element the press landed on: a
        // table row marks itself as right-clicked and the context menu that
        // wraps the table opens, both from the one press. That is why
        // `cx.stop_propagation()` exists on this path at all — a cell that
        // takes a secondary press keeps the row under it from taking it too.
        Vec<int> chain;
        HitChain(win, x, y, &chain);
        MouseDownEvent ev = in;
        DispatchChain(
            win, chain, &ev,
            [](const HitRect& hr, DispatchPhase phase) {
                return hr.mouseDownPhase == phase ? hr.onMouseDown : Listener{};
            },
            true);
        VecReset(chain);
        DispatchMouseDownOut(win, in);
        ClearPendingClick(win);
        AppInvalidate(win);
        return;
    }
    // The scrollbar sits over whatever it scrolls, so it is asked first: a
    // press on the bar is the bar's, not the row underneath it. Rust says the
    // same with cx.stop_propagation().
    // Inspector::is_picking: the press picks the element under the pointer
    // and goes no further, so the page it is over is not clicked.
    if (win->inspector.picking) {
        win->inspector.pending = true;
        win->inspector.pendingX = x;
        win->inspector.pendingY = y;
        DispatchMouseDownOut(win, in);
        ClearPendingClick(win);
        AppInvalidate(win);
        return;
    }
    bool barHorizontal = false;
    ScrollRect* bar = ScrollbarAt(&win->paint, x, y, &barHorizontal);
    if (bar) {
        SetMouseDown(win, true);
        ScrollbarPress(win, bar, x, y, barHorizontal);
        DispatchMouseDownOut(win, in);
        // The bar took the press, so nothing is waiting to become a click.
        ClearPendingClick(win);
        // d8376ad5 moved Rust's notify below its thumb/track branch. This
        // shared dispatch point already covers both: a track jump gets a
        // frame immediately even when the scroll handle only stages state.
        AppInvalidate(win);
        return;
    }
    const HitRect* hit = HitTestRect(&win->paint, x, y);
    int id = hit ? hit->id : 0;
    SetMouseDown(win, true);
    win->pressedId = id;
    // window.active_drag: a press on an element with a payload starts the
    // drag, and it lasts until the button comes back up.
    win->activeDrag = hit ? hit->drag : DragPayload{};
    win->dragOverId = 0;
    // cursor_offset, which GPUI takes as the drag starts: how far into the
    // element the press was.
    win->dragOffX = hit ? x - hit->bounds.x : 0;
    win->dragOffY = hit ? y - hit->bounds.y : 0;
    // A press takes focus only where the element asked for it. Rust gives a
    // disabled widget its element id all the same — `div().id(id)` is what
    // makes it hit-testable and hoverable — and hangs `track_focus` off
    // `when(!disabled)`, so pressing one leaves focus where it was; and
    // `track_focus` on an enabled one still does not focus it, which is why
    // only the widgets that call `focus()` themselves are `FocusOnPress`.
    // What the press focuses is what the element said it is focusable *as*,
    // which until focus handles existed was always the same number it is hit
    // as. A box tracking a handle is the first case where the two differ.
    //
    // Asked of the chain, not of the one rect the hit test names: a press is
    // *on* every box that contains it, the way `hovered` is. That is what
    // `list.rs` and `data_table.rs` rely on — the focus call hangs off a press
    // on a row, and the row is inside the thing that takes the focus. The
    // innermost box that wants the press wins, so a field inside a table still
    // takes it for itself, and a row that is only hit-testable is stepped over
    // rather than swallowing the press on the way past.
    int focusTarget = 0;
    {
        Vec<int> chain;
        HitChain(win, x, y, &chain);
        for (int k = 0; k < chain.len; k++) {
            int fid = win->paint.hits[chain[k]].focusId;
            if (fid && FocusIdIsFocusable(win, fid) &&
                FocusIdTakesPress(win, fid)) {
                focusTarget = fid;
                break;
            }
        }
        VecReset(chain);
    }
    if (focusTarget) {
        WindowSetFocusId(win, focusTarget);
    }
    // on_mouse_down, ahead of the click: an element that wants the press
    // itself — a slider jumping to it — gets the whole event, not the
    // ClickEvent the click path builds.
    {
        // on_mouse_down over the whole chain, not just the element the press
        // landed on: a tile's frame hears the press its drag bar took, which
        // is what brings it to the front.
        Vec<int> chain;
        HitChain(win, x, y, &chain);
        MouseDownEvent ev = in;
        DispatchChain(
            win, chain, &ev,
            [](const HitRect& hr, DispatchPhase phase) {
                return hr.mouseDownPhase == phase ? hr.onMouseDown : Listener{};
            },
            true);
        VecReset(chain);
    }
    DispatchMouseDownOut(win, in);
    // A semantic control can suppress selection without consuming the mouse
    // event. Ask the whole hit chain because a text/icon child may be the
    // innermost rectangle while the Button around it owns the press.
    {
        Vec<int> chain;
        HitChain(win, x, y, &chain);
        for (int i = 0; i < chain.len; i++) {
            if (win->paint.hits[chain[i]].suppressTextSelection) {
                BaseSuppressTextSelection(win->app);
                break;
            }
        }
        VecReset(chain);
    }
    if (hit && hit->slider) {
        BaseSuppressTextSelection(win->app);
        SliderPress(win, hit, {x, y});
    }
    InputState* inputAtPress = InputAtPosition(&win->paint, x, y);
    if (inputAtPress && !inputAtPress->disabled) {
        BaseSuppressTextSelection(win->app);
    }
    InputPress(win, in);
    // Bubble handlers and built-in controls have now had the same chance to
    // suppress that Rust gives them. A press anywhere else starts or clears
    // the window-owned selection.
    WindowSelectionPress(win, x, y, in.clickCount, in.modifiers.shift);
    // The click itself is not here: GPUI holds the press and fires on_click
    // from the release, on the element that took both. DispatchMouseUp does
    // that; what the press leaves behind is pressedId and the count.
    win->pressPending = true;
    win->pressedCount = in.clickCount;
    win->pressedX = x;
    win->pressedY = y;
    win->pressedMoved = false;
    win->pressedButton = in.button;
    win->pressedModifiers = in.modifiers;
    // TitleBar::on_double_click -> window.zoom_window(), in title_bar.rs. The
    // press was dispatched first, so an element that put itself in the title
    // bar still saw it — Rust bubbles the same way. The empty half of the band
    // counts too, since the gap between a TitleBar's controls is no hit rect
    // of its own — but only in a window that draws its own title bar: in one
    // wearing the system caption the top of the client area is ordinary
    // content, and a double click there is not a zoom. Windows answers
    // WM_NCHITTEST with HTCAPTION over the caption and never sends that press
    // here at all, so on that platform this is only the empty half.
    bool caption = id == ClickWinCaption ||
                   (id == 0 && win->opts.clientTitleBar && y < kCaptionH);
    if (in.clickCount == 2 && caption) {
        AppToggleMaximize(win);
    }
    AppInvalidate(win);
}

static void DispatchMouseUp(Window* win, const MouseUpEvent& in) {
    SetMouseDown(win, false);
    // with_unset_drag_pos: the release ends the scrollbar drag wherever it
    // landed.
    win->scrollDragId = 0;
    win->scrollDragGrab = 0;
    win->scrollDragInput = nullptr;
    WindowSelectionRelease(win);
    if (win->onMouseUp.IsValid()) {
        ListenerCall(win->app, win, win->onMouseUp, &in);
    }
    // The element under the pointer hears the release, then the one that took
    // the press stops being held. A drag that ended somewhere else leaves the
    // first of those empty, which is what on_mouse_up_out is for.
    const HitRect* hit = HitTestRect(&win->paint, in.x, in.y);
    {
        Vec<int> chain;
        HitChain(win, in.x, in.y, &chain);
        MouseUpEvent ev = in;
        DispatchChain(
            win, chain, &ev, [](const HitRect& hr, DispatchPhase phase) {
                return hr.mouseUpPhase == phase ? hr.onMouseUp : Listener{};
            });
        VecReset(chain);
    }
    // on_mouse_up_out: every element that asked for the release it did not
    // get. Rust hears one wherever the pointer is, so this walks the frame
    // rather than only the element that took the press — a drag that ended
    // off the edge is the case it exists for.
    for (int i = 0; i < win->paint.hits.len; i++) {
        const HitRect& hr = win->paint.hits[i];
        if (hr.onMouseUpOut.IsValid() && !hr.bounds.Contains({in.x, in.y})) {
            ListenerCall(win->app, win, hr.onMouseUpOut, &in);
        }
    }
    // on_drop: the element under the pointer that takes this drag hears where
    // it landed. It runs after on_mouse_up_out, so a source that is winding
    // its own drag down has already done so by the time the target acts.
    // A drag that actually happened takes the release: GPUI hands the up to
    // the drop and the click never runs. A press that picked a payload up and
    // never went anywhere is still a click.
    bool dragged = win->activeDrag.IsValid() && win->pressedMoved;
    if (dragged) {
        const HitRect* target =
            HitTestDrop(&win->paint, in.x, in.y, win->activeDrag.kind);
        if (target) {
            DropEvent ev = {win->activeDrag, in.x, in.y, target->bounds};
            ListenerCall(win->app, win, target->onDrop, &ev);
        }
    }
    // active_drag.take(): the drag is over whether or not it went anywhere.
    // A press that picked a payload up and let go without moving used to
    // leave it behind, and whatever draws from it — the dragged tab's
    // preview, a drop target's highlight — kept drawing.
    win->activeDrag = {};
    win->dragOverId = 0;
    SliderRelease(win);
    // InputState::on_mouse_up: the drag is over, and the word a double click
    // pinned stops holding the selection open.
    if (win->input && win->input->selecting) {
        win->input->selecting = false;
        win->input->hasSelectedWordRange = false;
        win->input->autoScroll.Stop();
    }
    // The click, last: GPUI's on_click fires from the release, and only when
    // the same button that went down comes up over the element that took it.
    // A press that slid off somewhere else is no click at all — which is what
    // lets a reader change their mind by moving off the button before
    // letting go.
    int upId = hit ? hit->id : 0;
    if (ClickFromRelease(win->pressPending, win->pressedId, win->pressedButton,
                         dragged, upId, in.button)) {
        ClickEvent ev = {in.x, in.y, in.button, win->pressedId};
        ev.clickCount = win->pressedCount;
        ev.modifiers = win->pressedModifiers;
        if (hit) {
            ev.el = hit->bounds;
        }
        // on_click bubbles. GPUI registers it in the Bubble phase against the
        // element's hitbox, so every enclosing element that asked for one
        // hears the click, innermost first, until one stops it — and none of
        // gpui-kit's own do: a `Button`'s handler stops nothing, and a
        // table's sort icon sits inside the column head whose click selects
        // the column, so pressing it sorts *and* selects. The port delivered
        // the click to the one rect the hit test named, which is why a box
        // made hit-testable anywhere between the pointer and a listener took
        // the click away from it.
        bool handled = false;
        {
            Vec<int> chain;
            HitChain(win, in.x, in.y, &chain);
            win->stopPropagation = false;
            for (int k = 0; k < chain.len && !win->stopPropagation; k++) {
                const HitRect& hr = win->paint.hits[chain[k]];
                if (!hr.listener.IsValid()) {
                    continue;
                }
                handled = true;
                bool stops = hr.stopClick;
                ListenerCall(win->app, win, hr.listener, &ev);
                if (stops) {
                    break;
                }
            }
            VecReset(chain);
        }
        if (!handled && win->onClick.IsValid() && !(hit && hit->slider)) {
            // A press on a slider is handled by the slider, so it is not the
            // outside click that dismisses an overlay.
            ListenerCall(win->app, win, win->onClick, &ev);
        }
        if (hit && hit->onClick.IsValid()) {
            hit->onClick.Call();
        }
        // El::OnClickAction, last. The element asked for an action rather
        // than a handler, and the action walks out from the focus the way a
        // chord's would — so a dialog's Cancel button reaches the same
        // handler its escape key does.
        //
        // Out through the enclosing elements, not just the one the pointer
        // landed on: the wrapper that names the action is an ancestor of the
        // button that was actually hit, which is how Rust's Cancel and Action
        // wrappers are written. The first one that names an action wins.
        Vec<int> clickChain;
        HitChain(win, in.x, in.y, &clickChain);
        for (int k = 0; k < clickChain.len; k++) {
            const HitRect& hr = win->paint.hits[clickChain[k]];
            if (!hr.clickAction) {
                continue;
            }
            WindowDispatchAction(win, hr.clickAction, hr.clickActionArg);
            break;
        }
    }
    ClearPendingClick(win);
    AppInvalidate(win);
}

static void DispatchMouseExited(Window* win, const MouseExitEvent& in) {
    win->hoverId = 0;
    if (win->onMouseExit.IsValid()) {
        ListenerCall(win->app, win, win->onMouseExit, &in);
    }
    AppInvalidate(win);
}

static bool HitDescendsFrom(const PaintCtx& paint, int leaf, int ancestor) {
    for (int i = leaf; i >= 0; i = paint.hits[i].parent) {
        if (i == ancestor) {
            return true;
        }
    }
    return false;
}

// The mask is a hitbox upstream. Here its scroll element records a hit-chain
// node, which gives the same occlusion answer without painting a transparent
// sibling: the topmost hit under the pointer must be the mask or one of its
// descendants.
static bool ScrollMaskIsTopmost(Window* win, const ScrollRect& s, float x,
                                float y) {
    if (s.maskHit < 0) {
        return true;
    }
    int leaf = -1;
    for (int i = win->paint.hits.len - 1; i >= 0; i--) {
        if (win->paint.hits[i].bounds.Contains({x, y})) {
            leaf = i;
            break;
        }
    }
    return leaf >= 0 && HitDescendsFrom(win->paint, leaf, s.maskHit);
}

#if !GPUI_OS_WASM
static OngoingScroll* ScrollLockFor(Window* win, int id, Axis maskAxis) {
    int* slotId = maskAxis == Axis::Horizontal ? &win->scrollLockHorizontalId
                                               : &win->scrollLockVerticalId;
    OngoingScroll* slot = maskAxis == Axis::Horizontal
                              ? &win->scrollLockHorizontal
                              : &win->scrollLockVertical;
    if (*slotId != id) {
        *slotId = id;
        *slot = {};
    }
    return slot;
}
#endif

// ScrollableMask's capture-phase axis choice. Line-wheel deltas are compared
// independently; precise deltas keep the OngoingScroll lock for the gesture.
static Point ScrollMaskDelta(Window* win, const ScrollRect& s,
                             const ScrollWheelEvent& in, Axis maskAxis) {
    Point delta = {in.deltaX, in.deltaY};
#if GPUI_OS_WASM
    (void)win;
    (void)s;
    (void)maskAxis;
#else
    if (in.precise) {
        ScrollLockFor(win, s.id, maskAxis)->Filter(&delta, in.phase);
    }
#endif
    if (delta.x != 0 && delta.y != 0) {
        float ax = delta.x < 0 ? -delta.x : delta.x;
        float ay = delta.y < 0 ? -delta.y : delta.y;
        if (ax > ay) {
            delta.y = 0;
        } else {
            delta.x = 0;
        }
    }
    return delta;
}

static void DispatchScrollWheel(Window* win, const ScrollWheelEvent& in) {
    // div().on_scroll_wheel: the element chain under the pointer, innermost
    // first, before anything scrolls. GPUI offers the gesture to the
    // interactive elements it passes through and only then to whatever would
    // scroll; a handler that clears `propagate` keeps it.
    {
        Vec<int> chain;
        HitChain(win, in.x, in.y, &chain);
        for (int k = 0; k < chain.len; k++) {
            const HitRect& hr = win->paint.hits[chain[k]];
            if (!hr.onScrollWheel.IsValid()) {
                continue;
            }
            ScrollWheelEvent ev = in;
            ev.propagate = true;
            ListenerCall(win->app, win, hr.onScrollWheel, &ev);
            if (!ev.propagate) {
                VecReset(chain);
                AppInvalidate(win);
                return;
            }
        }
        VecReset(chain);
    }
    // A multi-line field takes the wheel before anything around it, the way
    // the editor's own scroll handle does in Rust.
    InputState* field = InputAtPosition(&win->paint, in.x, in.y);
    if (field && InputIsMultiLine(field)) {
        bool canY = field->contentH > field->viewH;
        // A field that does not wrap scrolls sideways too, and a wheel with
        // no sideways delta over one that can only go that way is read as
        // one, the way a scrolled box reads it.
        bool canX = field->contentW > field->viewW;
        float dx = in.deltaX;
        float dy = in.deltaY;
        if (canX && !canY && dx == 0) {
            dx = dy;
            dy = 0;
        }
        if (canY) {
            field->scrollY =
                ClampScroll(field->scrollY - dy, field->contentH, field->viewH);
        }
        if (canX) {
            field->scrollX =
                ClampScroll(field->scrollX - dx, field->contentW, field->viewW);
        }
        if (canX || canY) {
            AppInvalidate(win);
            return;
        }
    }
    // The scrolled box under the pointer takes the wheel, which is what a
    // `div().overflow_scroll()` does in GPUI — the offset is the view's here,
    // so the box reports where it should now be rather than moving itself.
    // Only a box that asked for the event gets it; anything else falls
    // through to the window subscription, as it did before there were any.
    for (int i = win->paint.scrolls.len - 1; i >= 0; i--) {
        ScrollRect& s = win->paint.scrolls[i];
        if (!s.onScroll.IsValid() || !s.bounds.Contains({in.x, in.y})) {
            continue;
        }
        if (s.maskAxes) {
            // A dialog/menu above the mask blocks the whole gesture; letting
            // the search continue would scroll the ancestor underneath the
            // same overlay instead.
            if (!ScrollMaskIsTopmost(win, s, in.x, in.y)) {
                AppInvalidate(win);
                return;
            }
            Point horizontal = {};
            Point vertical = {};
            if (s.maskAxes & 1) {
                horizontal = ScrollMaskDelta(win, s, in, Axis::Horizontal);
            }
            if (s.maskAxes & 2) {
                vertical = ScrollMaskDelta(win, s, in, Axis::Vertical);
            }

            // Both masks see the same gesture upstream. They agree on the
            // dominant axis; the raw comparison only resolves an exact-zero
            // or equal-delta case without making one axis remap the other.
            bool takeX = (s.maskAxes & 1) && horizontal.x != 0;
            bool takeY = (s.maskAxes & 2) && vertical.y != 0;
            if (takeX && takeY) {
                float ax = in.deltaX < 0 ? -in.deltaX : in.deltaX;
                float ay = in.deltaY < 0 ? -in.deltaY : in.deltaY;
                takeX = ax > ay;
                takeY = !takeX;
            }
            if (takeX) {
                float offX = ClampScroll(s.scrollX - horizontal.x, s.contentW,
                                         s.bounds.w);
                if (offX != s.scrollX) {
                    ScrollbarEmit(win, &s, offX, s.scrollY);
                } else {
                    AppInvalidate(win);
                }
                // Horizontal masks trap the gesture even at the edge: GPUI
                // would otherwise remap it onto a vertical-only ancestor.
                return;
            }
            if (takeY) {
                float offY =
                    ClampScroll(s.scrollY - vertical.y, s.contentH, s.bounds.h);
                if (offY == s.scrollY) {
                    // Vertical scroll chaining: the parent gets the wheel at
                    // the edge or when this viewport has no overflow.
                    continue;
                }
                ScrollbarEmit(win, &s, s.scrollX, offY);
                return;
            }
            // Dominated by an axis this mask does not own.
            continue;
        }
        bool canY = ScrollsY(s);
        bool canX = ScrollsX(s);
        if (!canY && !canX) {
            continue;
        }
        // A wheel with no sideways delta over a box that only scrolls
        // sideways scrolls it anyway, which is what a mouse without a tilt
        // wheel needs.
        float dx = in.deltaX;
        float dy = in.deltaY;
        if (canX && !canY && dx == 0) {
            dx = dy;
            dy = 0;
        }
        float offY = canY ? ClampScroll(s.scrollY - dy, s.contentH, s.bounds.h)
                          : s.scrollY;
        float offX = canX ? ClampScroll(s.scrollX - dx, s.contentW, s.bounds.w)
                          : s.scrollX;
        if (offX == s.scrollX && offY == s.scrollY) {
            AppInvalidate(win);
            return;
        }
        ScrollbarEmit(win, &s, offX, offY);
        return;
    }
    if (win->onScrollWheel.IsValid()) {
        ListenerCall(win->app, win, win->onScrollWheel, &in);
    }
    AppInvalidate(win);
}

void WindowDispatchInput(Window* win, const PlatformInput* input) {
    if (!win || !input) {
        return;
    }
    switch (input->kind) {
        case PlatformInputKind::MouseDown:
            DispatchMouseDown(win, input->mouseDown);
            break;
        case PlatformInputKind::MouseUp:
            DispatchMouseUp(win, input->mouseUp);
            break;
        case PlatformInputKind::MouseMove:
            DispatchMouseMove(win, input->mouseMove);
            break;
        case PlatformInputKind::MouseExited:
            DispatchMouseExited(win, input->mouseExited);
            break;
        case PlatformInputKind::ScrollWheel:
            DispatchScrollWheel(win, input->scrollWheel);
            break;
    }
}

PlatformInput InputMouseDown(MouseButton button, float x, float y,
                             Modifiers modifiers, int clickCount,
                             bool firstMouse) {
    PlatformInput in = {};
    in.kind = PlatformInputKind::MouseDown;
    in.mouseDown.button = button;
    in.mouseDown.x = x;
    in.mouseDown.y = y;
    in.mouseDown.modifiers = modifiers;
    in.mouseDown.clickCount = clickCount;
    in.mouseDown.firstMouse = firstMouse;
    return in;
}

PlatformInput InputMouseUp(MouseButton button, float x, float y,
                           Modifiers modifiers, int clickCount) {
    PlatformInput in = {};
    in.kind = PlatformInputKind::MouseUp;
    in.mouseUp.button = button;
    in.mouseUp.x = x;
    in.mouseUp.y = y;
    in.mouseUp.modifiers = modifiers;
    in.mouseUp.clickCount = clickCount;
    return in;
}

PlatformInput InputMouseMove(float x, float y, bool pressed,
                             MouseButton pressedButton, Modifiers modifiers) {
    PlatformInput in = {};
    in.kind = PlatformInputKind::MouseMove;
    in.mouseMove.x = x;
    in.mouseMove.y = y;
    in.mouseMove.pressed = pressed;
    in.mouseMove.pressedButton = pressedButton;
    in.mouseMove.modifiers = modifiers;
    return in;
}

PlatformInput InputMouseExited(float x, float y, bool pressed,
                               MouseButton pressedButton, Modifiers modifiers) {
    PlatformInput in = {};
    in.kind = PlatformInputKind::MouseExited;
    in.mouseExited.x = x;
    in.mouseExited.y = y;
    in.mouseExited.pressed = pressed;
    in.mouseExited.pressedButton = pressedButton;
    in.mouseExited.modifiers = modifiers;
    return in;
}

PlatformInput InputScrollWheel(float x, float y, float deltaX, float deltaY,
                               bool precise, Modifiers modifiers,
                               TouchPhase phase) {
    PlatformInput in = {};
    in.kind = PlatformInputKind::ScrollWheel;
    in.scrollWheel.x = x;
    in.scrollWheel.y = y;
    in.scrollWheel.deltaX = deltaX;
    in.scrollWheel.deltaY = deltaY;
    in.scrollWheel.precise = precise;
    in.scrollWheel.modifiers = modifiers;
    in.scrollWheel.phase = phase;
    return in;
}

// blink_cursor.rs: INTERVAL and PAUSE_DELAY.
static const int kBlinkIntervalMs = 500;
static const int kBlinkPauseMs = 300;

static BlinkCursor* BlinkGet(App* app, EntityId handle) {
    return (BlinkCursor*)EntityGet(app, handle);
}

void BlinkCursor::OnFlip(BlinkCursor* self, Ctx* cx, const TickEvent*) {
    if (self->paused) {
        return;
    }
    self->visible = !self->visible;
    Notify(cx);
}

void BlinkCursor::OnResume(BlinkCursor* self, Ctx* cx, const TickEvent*) {
    // The pause is over; pick blinking back up lit, as Rust does.
    self->paused = false;
    self->visible = true;
    Listener flip;
    flip.SetFn(&BlinkCursor::OnFlip);
    flip.view = cx->self;
    self->timer = WindowSetInterval(cx->win, kBlinkIntervalMs, flip);
    Notify(cx);
}

// The Listener a timer calls back through, bound to the cursor's own entity —
// which is what makes the timer die with it.
template <typename T, typename E>
static Listener BlinkListener(EntityId handle, void (*fn)(T*, Ctx*, const E*)) {
    Listener l;
    l.SetFn(fn);
    l.view = handle;
    return l;
}

void BlinkStart(App* app, Window* win, EntityId* handle) {
    if (!app || !win || !handle) {
        return;
    }
    if (!handle->IsValid()) {
        // cx.new(|_| BlinkCursor::new())
        *handle = EntityNewRaw(app, new BlinkCursor(), nullptr,
                               &EntityDropT<BlinkCursor>);
    }
    BlinkCursor* b = BlinkGet(app, *handle);
    if (!b || b->timer) {
        return; // already blinking
    }
    b->paused = false;
    // Rust starts hidden and flips on the first tick; lit immediately is what
    // makes a click feel like it landed.
    b->visible = true;
    b->timer = WindowSetInterval(win, kBlinkIntervalMs,
                                 BlinkListener(*handle, &BlinkCursor::OnFlip));
    AppInvalidate(win);
}

void BlinkStop(App* app, Window* win, EntityId* handle) {
    if (!app || !win || !handle || !handle->IsValid()) {
        return;
    }
    BlinkCursor* b = BlinkGet(app, *handle);
    if (!b) {
        return;
    }
    WindowCancelTimer(win, b->timer);
    b->timer = 0;
    b->paused = false;
    b->visible = false;
    AppInvalidate(win);
}

void BlinkPause(App* app, Window* win, EntityId* handle) {
    if (!app || !win || !handle || !handle->IsValid()) {
        return;
    }
    BlinkCursor* b = BlinkGet(app, *handle);
    if (!b || !b->timer) {
        return; // not blinking, nothing to keep solid
    }
    WindowCancelTimer(win, b->timer);
    b->paused = true;
    b->visible = true;
    b->timer = WindowSetTimeout(win, kBlinkPauseMs,
                                BlinkListener(*handle, &BlinkCursor::OnResume));
    AppInvalidate(win);
}

bool BlinkVisible(App* app, EntityId handle) {
    BlinkCursor* b = BlinkGet(app, handle);
    if (!b || !b->timer) {
        return false;
    }
    // Paused means solid, not hidden.
    return b->paused || b->visible;
}

// How long an animating window waits between frames: one 60Hz frame, whether
// or not it is the active one, since `inactive_frame_interval` is unset.
static const double kAnimationFrameInterval = 0.016;

static bool WindowAnimationDue(Window* win, double now) {
    if (!win || !(win->anim || win->opts.anim || win->animFrame)) {
        return false;
    }
    if (win->lastDrawTime <= 0) {
        return true;
    }
    return now >= win->lastDrawTime + kAnimationFrameInterval;
}

void WindowTimerTick(Window* win) {
    if (!win) {
        return;
    }
    double now = TimeNow();

    // A snapshot of the count, so a timer armed by a handler runs next pass
    // rather than inside this one.
    int n = win->timers.len;
    for (int i = 0; i < n && i < win->timers.len; i++) {
        TimerSub& t = win->timers[i];
        if (t.dueAt > now) {
            continue;
        }
        Listener l = t.l;
        int ms = t.ms;
        if (t.repeat) {
            t.dueAt = now + (double)ms / 1000.0;
        } else {
            t.dueAt = 0; // swept below
        }
        TickEvent ev = {ms};
        ListenerCall(win->app, win, l, &ev);
    }

    // Drop the one-shots that fired, and any timer whose view is gone — the
    // lifetime Rust gets from Task being dropped with its entity.
    int keep = 0;
    for (int i = 0; i < win->timers.len; i++) {
        const TimerSub& t = win->timers[i];
        bool dead = t.dueAt <= 0 ||
                    (t.l.view.IsValid() && !EntityGet(win->app, t.l.view));
        if (dead) {
            continue;
        }
        win->timers[keep++] = win->timers[i];
    }
    win->timers.len = keep;

    // PlatSetTimer may wake before the requested deadline (on Windows this is
    // deliberate, to avoid missing a display interval). A resource timer can
    // also be sooner than the next animation frame. Neither is permission to
    // draw early; the remaining deadline is re-armed below.
    if (WindowAnimationDue(win, now)) {
        AppInvalidate(win);
    }
    PlatSetTimer(win, WindowTimerMs(win));
}

int WindowChromeHit(Window* win, float x, float y) {
    if (!win) {
        return 0;
    }
    int id = HitTest(&win->paint, x, y);
    if (id == ClickWinMin || id == ClickWinMax || id == ClickWinClose ||
        id == ClickWinCaption) {
        return id;
    }
    return 0;
}

int WindowTimerMs(Window* win) {
    if (!win) {
        return 0;
    }
    // Milliseconds until the soonest thing that wants the window back, or 0
    // if nothing does.
    double now = TimeNow();
    double soonest = -1;
    if (win->anim || win->opts.anim || win->animFrame) {
        // WindowOptions::inactive_frame_interval is unset: GPUI then paces
        // an inactive window's animation at the same 16 ms as an active
        // one's. The story app used to ask for 500 ms between inactive
        // frames, only to hold the FPS HUD's self-driven redraws to 2 FPS in
        // the background; the HUD no longer drives frames, and the setting
        // went with it (upstream b586fad3). This only paces continuing
        // animation frames. A demand-driven frame from input or Notify is
        // invalidated immediately, as upstream does; Windows can send wheel
        // input to an inactive hovered window.
        double target = (win->lastDrawTime > 0)
                            ? (win->lastDrawTime + kAnimationFrameInterval)
                            : now;
        if (target < now) {
            target = now;
        }
        if (soonest < 0 || target < soonest) {
            soonest = target;
        }
    }
    // A fetch in flight used to be a reason to come back at 20 Hz and ask the
    // table whether it had landed yet. It reports itself now: the executor
    // posts AppFetchLanded when the worker finishes and that invalidates the
    // window, so an idle window with a picture on the way sleeps like any
    // other.
    for (int i = 0; i < win->timers.len; i++) {
        double due = win->timers[i].dueAt;
        if (due > 0 && (soonest < 0 || due < soonest)) {
            soonest = due;
        }
    }
    if (soonest < 0) {
        return 0;
    }
    int ms = (int)lround((soonest - now) * 1000.0);
    return ms > 0 ? ms : 1;
}

// ─── lifecycle ────────────────────────────────────────────────────────────

Window* WindowAlloc(App* app, WinOpts opts) {
    if (!app) {
        return nullptr;
    }
    Window* win = new Window();
    win->app = app;
    win->opts = opts;
    win->anim = opts.anim;
    // The factories and the font cache live on App; each window borrows them.
    win->paint.pa = app->paint;
    VecAppend(app->windows, win);
    return win;
}

bool AppAnyWindowOpen(App* app) {
    if (!app) {
        return false;
    }
    for (int i = 0; i < app->windows.len; i++) {
        if (app->windows[i]->plat) {
            return true;
        }
    }
    return false;
}

// A field and the window it is focused in point at each other, and either one
// can be destroyed first. ~InputState clears the window's half; this clears
// the field's, so neither order leaves a dangling pointer.
//
// WindowClosed does it properly, through InputBlur, for a window the platform
// closes. This is the backstop for the paths that do not go through there:
// AppShutdown deletes its windows outright, and gets away with it today only
// because EntityDropAll happens to run first — which covers a field an entity
// owns and no other. A field that is a plain member of something the app
// holds, focused when the process comes down, would reach into the freed
// window from its own destructor.
//
// The `focusWin == this` test matters: a field that has since been focused in
// another window is still listed here, and its live registration is that
// other window's to clear, not ours.
Window::~Window() {
    if (input && input->focusWin == this) {
        input->focusWin = nullptr;
        input->focused = false;
    }
    if (prevInput && prevInput->focusWin == this) {
        prevInput->focusWin = nullptr;
        prevInput->focused = false;
    }
    input = nullptr;
    prevInput = nullptr;
    scene::Free(&paint);
}

void WindowClosed(Window* win) {
    if (!win) {
        return;
    }
    // The focused field outlives the window it was focused in — it belongs to
    // a view, and the app may still hold that view — so the two let go of
    // each other here rather than leaving a pointer either way.
    if (win->input) {
        InputBlur(win->input, win->app, win);
    }
    win->input = nullptr;
    win->prevInput = nullptr;
    PaintTargetFree(&win->paint);
    win->plat = nullptr;
    win->running = false;
}

// A picture arrived. image.h answered nothing for it while it was on its way,
// so every window draws once more and asks the table again. Runs on the main
// thread: sys/http.cpp hands this to the executor as a fetch's completion.
static void AppFetchLanded(App* app) {
    if (!app) {
        return;
    }
    for (int i = 0; i < app->windows.len; i++) {
        AppInvalidate(app->windows[i]);
    }
}

App* AppNew() {
    App* app = new App();
    // Somewhere to read icons and images from, unless the caller has
    // already said. Without a root the icon set falls back to the
    // built-in strokes, which cover only part of it, so an app that
    // never mentioned assets drew nothing where the rest should be.
    // A caller that wants an example's own subfolder still asks for it,
    // and AssetsClear + AssetsAddRoot still replaces what this found.
    if (AssetsRootCount() == 0) {
        AssetsAddDefaultRoots(Str{});
    }
    app->paint = PaintAppNew();
    if (!app->paint) {
        delete app;
        return nullptr;
    }
    if (!PlatInit(app)) {
        PaintAppFree(app->paint);
        delete app;
        return nullptr;
    }
    // This thread is the one everything the UI owns is touched on, and the
    // platform loop is what a worker nudges to get a task looked at. GPUI
    // says the same thing by handing its foreground executor the platform
    // dispatcher at startup.
    ExecInit();
    ExecSetWake(MkFunc0(PlatWake, app));
    HttpSetOnFetchDone(MkFunc0(AppFetchLanded, app));
    return app;
}

// Defined with AppOnShutdown below, and called from here.
static void AppRunShutdownFns();

void AppFree(App* app) {
    if (!app) {
        return;
    }
    // Cancel queued image fetches and tell running ones to discard their
    // results while their App callback is still valid. Then stop the executor;
    // a request still inside the OS client owns only its job and static slot.
    ImageCacheClear();
    ExecShutdown();
    EntityDropAll(app);
    for (int i = 0; i < app->windows.len; i++) {
        Window* w = app->windows[i];
        if (w->frameArena) {
            ArenaDelete(w->frameArena);
        }
        TextMeasClear(&w->paint);
        LayoutCacheFree(w->layout);
        w->layout = nullptr;
        WindowSelectionFree(w);
        PaintTargetFree(&w->paint);
        VecReset(w->timers);
        WindowKeyedFree(w);
        WindowMotionFree(w);
        delete w;
    }
    VecReset(app->windows);
    LayoutScratchFree();
    ScrollFadeClear();
    StyleOverrideClearAll();
    AppMenuClear(app);
    AppRunShutdownFns();
    AppGlobalClear(app);
    PaintAppFree(app->paint);
    app->paint = nullptr;
    PlatShutdown(app);
    delete app;
    DestroyTempArena();
}

// window.request_animation_frame(). The flag is cleared as the next frame
// starts, so a caller that still has somewhere to go asks again while it
// renders, and one that has arrived stops.
void WindowRequestAnimationFrame(Window* win) {
    if (!win || win->animFrame) {
        return;
    }
    win->animFrame = true;
    // Nothing else may be keeping the window awake: arm the clock now, the
    // way AppRequestAnim does.
    PlatSetTimer(win, WindowTimerMs(win));
}

// The teardowns src/base and src/ui have registered, in the order they came.
static const int kMaxShutdownFns = 16;
static void (*gShutdownFns[kMaxShutdownFns])() = {};
static int gShutdownFnN = 0;

void AppOnShutdown(void (*fn)()) {
    if (!fn || gShutdownFnN >= kMaxShutdownFns) {
        return;
    }
    for (int i = 0; i < gShutdownFnN; i++) {
        if (gShutdownFns[i] == fn) {
            return;
        }
    }
    gShutdownFns[gShutdownFnN++] = fn;
}

static void AppRunShutdownFns() {
    for (int i = 0; i < gShutdownFnN; i++) {
        gShutdownFns[i]();
    }
    gShutdownFnN = 0;
}

void AppRefreshWindows(App* app) {
    if (!app) {
        return;
    }
    for (int i = 0; i < app->windows.len; i++) {
        AppInvalidate(app->windows[i]);
    }
}

void AppRequestAnim(Window* win, bool on) {
    if (!win) {
        return;
    }
    win->anim = on;
    win->opts.anim = on;
    // WindowTimerMs answers 0 when nothing is left wanting the window back.
    PlatSetTimer(win, WindowTimerMs(win));
}

// ─── runtime command line ────────────────────────────────────────────────

static bool gGeomAsked = false;
static int gGeom[4] = {0, 0, 0, 0};

// "12,-3,960,921" -> four ints. Anything else leaves the request unset rather
// than opening a window somewhere surprising.
static bool ParseGeom(Str value, int out[4]) {
    int at = 0;
    for (int i = 0; i < 4; i++) {
        if (i > 0) {
            if (at >= value.len || value.s[at] != ',') {
                return false;
            }
            at++;
        }
        bool neg = false;
        if (at < value.len && value.s[at] == '-') {
            neg = true;
            at++;
        }
        int digits = 0;
        int v = 0;
        while (at < value.len && value.s[at] >= '0' && value.s[at] <= '9') {
            v = v * 10 + (value.s[at] - '0');
            at++;
            digits++;
            if (digits > 6) {
                return false;
            }
        }
        if (digits == 0) {
            return false;
        }
        out[i] = neg ? -v : v;
    }
    return at == value.len && out[2] > 0 && out[3] > 0;
}

bool WindowGeomRequested(int* x, int* y, int* w, int* h) {
    if (!gGeomAsked) {
        return false;
    }
    *x = gGeom[0];
    *y = gGeom[1];
    *w = gGeom[2];
    *h = gGeom[3];
    return true;
}

// -gpui-inspector: open the inspector on the first frame. The panel is
// otherwise only reachable through ctrl-shift-i, which a screenshot harness
// cannot send — it reads the real keyboard state.
static bool gInspectorAsked = false;

int GpuiTakeRuntimeArgs(int argc, char** argv) {
    Str geomPrefix = StrL("-gpui-window=");
    int keep = 0;
    for (int i = 0; i < argc; i++) {
        Str argument = Str(argv[i]);
#if GPUI_OS_WINDOWS
        if (i > 0 && argument && WinPaintOptionsTakeArg(argument)) {
            continue;
        }
#endif
        if (i > 0 && argument && LayoutReuseTakeArg(argument)) {
            continue;
        }
        if (i > 0 && StrEq(argument, StrL("-gpui-inspector"))) {
            gInspectorAsked = true;
            continue;
        }
        if (i > 0 && StrStartsWith(argument, geomPrefix)) {
            int g[4];
            if (ParseGeom(Str(argument.s + geomPrefix.len,
                              argument.len - geomPrefix.len),
                          g)) {
                gGeomAsked = true;
                for (int k = 0; k < 4; k++) {
                    gGeom[k] = g[k];
                }
            }
            continue;
        }
        argv[keep++] = argv[i];
    }
    for (int i = keep; i <= argc; i++) {
        argv[i] = nullptr;
    }
    return keep;
}

// crates/story/src/lib.rs create_new_window_with_size: a window never asks
// for more than 85% of the display, however big the caller's default is.
void WindowClampToDisplay(int* dipW, int* dipH, int screenW, int screenH) {
    if (screenW > 0 && *dipW > (int)((float)screenW * 0.85f)) {
        *dipW = (int)((float)screenW * 0.85f);
    }
    if (screenH > 0 && *dipH > (int)((float)screenH * 0.85f)) {
        *dipH = (int)((float)screenH * 0.85f);
    }
}

Window* WindowOpenView(App* app, Str title, int dipW, int dipH, EntityId root,
                       WinOpts opts) {
    Window* win = WindowOpen(app, title, dipW, dipH, opts);
    if (win) {
        win->root = root;
        if (gInspectorAsked) {
            WindowToggleInspector(win);
        }
        AppInvalidate(win);
    }
    return win;
}

int AppRunView(Str title, int dipW, int dipH, EntityId root, App* app,
               WinOpts opts) {
    if (!WindowOpenView(app, title, dipW, dipH, root, opts)) {
        return 1;
    }
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}

void AppClose(Window* win) {
    AppQuit(win);
}

void AppQuitAll(App* app) {
    if (!app) {
        return;
    }
    // Closing a window takes it out of the list, so the list is copied first
    // and each entry checked against what is left — a window that closed
    // another one on its way out is not visited twice.
    Vec<Window*> windows;
    for (int i = 0; i < app->windows.len; i++) {
        VecAppend(windows, app->windows[i]);
    }
    for (int i = 0; i < windows.len; i++) {
        Window* win = windows[i];
        bool live = false;
        for (int j = 0; j < app->windows.len; j++) {
            live = live || app->windows[j] == win;
        }
        if (live) {
            AppQuit(win);
        }
    }
    VecReset(windows);
}

bool AppIsMaximized(Window* win) {
    return win && win->maximized;
}

// ─── the application menu bar ──────────────────────────────────────────────
//
// cx.set_menus(): the menus turned into what the platform takes, and the
// table that turns the id a chosen row reports back into its action. GPUI
// keeps a `Vec<OwnedMenuItem>` for the same reason — the OS answers with a
// number, and something has to remember what the number meant.

struct AppMenuBinding {
    uint32_t action = 0;
    intptr_t arg = 0;
};

struct AppMenuState {
    Arena* arena = nullptr;
    Vec<AppMenuBinding> rows;

    ~AppMenuState() {
        VecReset(rows);
        if (arena) {
            ArenaDelete(arena);
        }
    }
};

// Whose menus these are, so a row chosen later can be dispatched into it.
// An OS application menu is process-wide even when the model behind it is an
// App global; this is only the currently installed platform target.
static App* gAppMenuApp = nullptr;

// The rows as the platform takes them, numbering every row that can be
// chosen in preorder — the same rule NativeMenu::Show numbers by, so the two
// halves agree without either spelling the order out to the other.
static PlatMenuItem* AppMenuToPlat(AppMenuState* state, Arena* a,
                                   const MenuRow* rows, int n) {
    if (!rows || n <= 0) {
        return nullptr;
    }
    auto* out = (PlatMenuItem*)a->Push((uint64_t)n * sizeof(PlatMenuItem),
                                       alignof(PlatMenuItem), true);
    for (int i = 0; i < n; i++) {
        const MenuRow& r = rows[i];
        PlatMenuItem& p = out[i];
        if (r.separator || r.label.len <= 0) {
            p.separator = true;
            continue;
        }
        p.label = StrDup(a, r.label).s;
        p.disabled = r.disabled;
        p.checked = r.checked;
        if (r.submenu && r.submenuN > 0) {
            p.submenu = AppMenuToPlat(state, a, r.submenu, r.submenuN);
            p.submenuN = r.submenuN;
            continue;
        }
        // A row that cannot be chosen is not numbered, so it cannot be
        // reported either — which is what leaving its id at zero says.
        if (r.disabled) {
            continue;
        }
        VecAppend(state->rows, AppMenuBinding{r.action, r.arg});
        p.id = state->rows.len;
        // The shortcut beside the label, out of the keymap rather than typed
        // into the row: the menu bar matches it itself, and what it fires is
        // the row, which dispatches the action the chord would have reached.
        KeyChord chord = {};
        if (r.action && KeymapAnyBindingForAction(r.action, &chord)) {
            Str key = KeyName(chord.vk);
            if (key.len > 0) {
                p.key = StrDup(a, key).s;
                p.keyMods.control = chord.ctrl;
                p.keyMods.alt = chord.alt;
                p.keyMods.shift = chord.shift;
                p.keyMods.platform = chord.platform;
                p.keyMods.function = chord.function;
            }
        }
    }
    return out;
}

bool AppHasMenuBar() {
    return PlatHasAppMenu();
}

void AppSetMenus(App* app, const MenuDef* menus, int n) {
    if (!app) {
        return;
    }
    AppMenuState* state = AppGlobalEnsure<AppMenuState>(app);
    if (!state) {
        return;
    }
    if (!state->arena) {
        state->arena = ArenaNew();
    }
    if (!state->arena) {
        return;
    }
    gAppMenuApp = app;
    VecReset(state->rows);
    state->arena->Reset();
    if (!menus || n <= 0) {
        PlatSetAppMenu(app, nullptr, 0);
        return;
    }
    Arena* a = state->arena;
    auto* bar = (PlatMenuItem*)a->Push((uint64_t)n * sizeof(PlatMenuItem),
                                       alignof(PlatMenuItem), true);
    for (int i = 0; i < n; i++) {
        bar[i].label = StrDup(a, menus[i].name).s;
        bar[i].submenu = AppMenuToPlat(state, a, menus[i].items, menus[i].n);
        bar[i].submenuN = menus[i].n;
    }
    // The table is built whether or not anything shows it: a platform with no
    // menu bar still has the rows, and the numbering is what the tests read.
    PlatSetAppMenu(app, bar, n);
}

bool AppMenuRowForId(int id, uint32_t* action, intptr_t* arg) {
    return AppMenuRowForId(gAppMenuApp, id, action, arg);
}

bool AppMenuRowForId(const App* app, int id, uint32_t* action, intptr_t* arg) {
    AppMenuState* state = AppGlobalGet<AppMenuState>(app);
    if (!state || id <= 0 || id > state->rows.len) {
        return false;
    }
    if (action) {
        *action = state->rows[id - 1].action;
    }
    if (arg) {
        *arg = state->rows[id - 1].arg;
    }
    return true;
}

void AppMenuClear(App* app) {
    if (!app) {
        return;
    }
    if (gAppMenuApp == app) {
        PlatSetAppMenu(app, nullptr, 0);
        gAppMenuApp = nullptr;
    }
    AppGlobalRemove<AppMenuState>(app);
}

void AppMenuChosen(int id) {
    uint32_t action = 0;
    intptr_t arg = 0;
    if (!gAppMenuApp || !AppMenuRowForId(gAppMenuApp, id, &action, &arg) ||
        !action) {
        return;
    }
    // The window the menu bar was over. GPUI dispatches a menu action to the
    // key window, and falls back to the application's own handlers when there
    // is none — which is what WindowDispatchAction ends with anyway.
    Window* win = nullptr;
    for (int i = 0; i < gAppMenuApp->windows.len; i++) {
        Window* w = gAppMenuApp->windows[i];
        if (w && w->active) {
            win = w;
            break;
        }
    }
    if (!win && gAppMenuApp->windows.len > 0) {
        win = gAppMenuApp->windows[0];
    }
    if (!win) {
        return;
    }
    WindowDispatchAction(win, action, arg);
    // The choice arrives between frames, so nothing else would repaint what
    // the handler changed.
    AppInvalidate(win);
}

// SPI_GETWHEELSCROLLLINES is three on a default Windows install and the other
// two platforms scroll by the same three lines; nothing here reads the
// setting, so the constant is named rather than spelled out four times.
static const float kWheelScrollLines = 3.f;

float WheelNotchPixels(const App* app) {
    return kWheelScrollLines * RuntimeStyleNow(app).fontSize * kLineHeight;
}

} // namespace gpui
