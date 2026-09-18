// Overlays the gpui-fps HUD on a port of three.js' `webgl_lines_colors`
// demo: 3D Hilbert curves smoothed with a Catmull-Rom spline, colored by the
// same three HSL schemes as the original, rotating over a black background.
//
// The number of curves is adjustable, which makes it a load knob for watching
// the frame time trace react.
//
//   fps_monitor -bench 10 [-bench-out <path>] [-curves N] [-size WxH]
//
// -bench runs the window for real and writes the distribution of Window::draw
// after a one second warm-up, which is a number to compare against instead of
// a photograph of the HUD. -size matters as much as -curves: what a line costs
// is mostly per segment, but a window is what says how many pixels each one
// covers.

#include "gpui.h"

using namespace gpui;

#include <math.h>

// Matches the original demo: a one-iteration Hilbert curve of 64 control
// points, resampled at six points each.
static const float kHilbertSize = 200.f;
static const uint32_t kHilbertIter = 1;
static const int kSubdiv = 6;

// How far the curve actually reaches from the origin.
//
// Not kHilbertSize / 2: each recursion centers a sub-cell *on a corner* of the
// cell above it and then extends half a sub-cell further out, so one iteration
// reaches 1.5x the half size. Scaling against the nominal size instead would
// draw every curve larger than its grid cell.
static const float kHilbertExtent = kHilbertSize / 2.f * 1.5f;

// Fraction of a grid cell the curve is allowed to fill.
//
// Two effects push past the nominal size and the margin has to cover both, or
// neighbouring curves overlap: the spline overshoots its control polygon by
// about 8%, and the perspective divide magnifies the near face by
// kEye / (kEye - kHilbertExtent), roughly 1.32. Together that is ~1.43.
static const float kCellFill = 0.68f;

// Points per drawn run. A run carries one color, so the gradient is built from
// short runs rather than per-vertex colors.
static const int kSegPts = 6;

static const int kCurveStep = 1;
static const int kMaxCurves = 48;

// Distance from the eye to the origin, for the perspective divide.
static const float kEye = 620.f;
// How quickly the view catches up with the cursor.
static const float kEase = 0.08f;

static const int kMaxPts = 512;
static const int kMaxCtrl = 64;

struct V3 {
    float x = 0, y = 0, z = 0;
};

struct FpsApp {
    // The spline, shared by every curve on screen.
    V3 pts[kMaxPts];
    int nPts = 0;
    // Vertex colors for the demo's three schemes, indexed by scheme.
    Rgba pal[3][kMaxPts];
    int curves = 6;
    // Where the view is being pulled to, and where it currently is.
    float cursorTiltX = 0, cursorTiltY = 0;
    float tiltX = 0, tiltY = 0;
    double started = 0;

    // -bench <secs>: measure instead of watch. The window still runs for real,
    // but the frames it draws are collected here and written out as numbers,
    // so a change can be compared against a run from before it rather than
    // against a screenshot of the HUD.
    double benchSecs = 0;
    Str benchOut = StrL("out/fps_bench.txt");
    uint64_t benchCursor = 0;
    float benchDraws[4096];
    int nBenchDraws = 0;

    static El* Render(FpsApp* self, Ctx* cx);
};

static float Dist(V3 a, V3 b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static V3 Lerp(V3 a, V3 b, float t) {
    return V3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
              a.z + (b.z - a.z) * t};
}

// Port of three.js' hilbert3D.
static void Hilbert(V3 center, float size, uint32_t iter, const int v[8],
                    V3* out, int* n) {
    float half = size / 2.f;
    V3 corners[8] = {
        {center.x - half, center.y + half, center.z - half},
        {center.x - half, center.y + half, center.z + half},
        {center.x - half, center.y - half, center.z + half},
        {center.x - half, center.y - half, center.z - half},
        {center.x + half, center.y - half, center.z - half},
        {center.x + half, center.y - half, center.z + half},
        {center.x + half, center.y + half, center.z + half},
        {center.x + half, center.y + half, center.z - half},
    };
    V3 vec[8];
    for (int i = 0; i < 8; i++) {
        vec[i] = corners[v[i]];
    }
    if (iter == 0) {
        for (int i = 0; i < 8 && *n < kMaxCtrl; i++) {
            out[(*n)++] = vec[i];
        }
        return;
    }
    int children[8][8] = {
        {v[0], v[3], v[4], v[7], v[6], v[5], v[2], v[1]},
        {v[0], v[7], v[6], v[1], v[2], v[5], v[4], v[3]},
        {v[0], v[7], v[6], v[1], v[2], v[5], v[4], v[3]},
        {v[2], v[3], v[0], v[1], v[6], v[7], v[4], v[5]},
        {v[2], v[3], v[0], v[1], v[6], v[7], v[4], v[5]},
        {v[4], v[3], v[2], v[5], v[6], v[1], v[0], v[7]},
        {v[4], v[3], v[2], v[5], v[6], v[1], v[0], v[7]},
        {v[6], v[5], v[2], v[1], v[0], v[3], v[4], v[7]},
    };
    for (int i = 0; i < 8; i++) {
        Hilbert(vec[i], half, iter - 1, children[i], out, n);
    }
}

// Centripetal Catmull-Rom evaluation over the whole control polygon, with the
// endpoints clamped.
//
// Centripetal (the alpha = 0.5 knot parameterization) rather than uniform,
// matching CatmullRomCurve3's default, because a Hilbert curve turns a corner
// at nearly every control point. Uniform parameterization overshoots hard at
// those turns — the curve shoots away from its control polygon and loops back
// — which reads as long straight spikes across the scene.
static V3 Catmull(const V3* ctrl, int nCtrl, float t) {
    if (nCtrl <= 0) {
        return {};
    }
    if (nCtrl == 1) {
        return ctrl[0];
    }
    int spans = nCtrl - 1;
    float scaled = (t < 0 ? 0 : (t > 1 ? 1 : t)) * (float)spans;
    int span = (int)scaled;
    if (span > spans - 1) {
        span = spans - 1;
    }
    float local = scaled - (float)span;
    auto at = [&](int i) -> V3 {
        if (i < 0) {
            i = 0;
        }
        if (i >= nCtrl) {
            i = nCtrl - 1;
        }
        return ctrl[i];
    };
    V3 p0 = at(span - 1), p1 = at(span), p2 = at(span + 1), p3 = at(span + 2);
    // Knots spaced by the square root of the chord length. Coincident control
    // points — which the clamped endpoints always produce — would collapse a
    // span to zero width, so each step is floored. The floor is well above
    // FLT_EPSILON: a span that small makes the divisions below overflow into
    // the millions and lose all precision.
    auto knot = [](V3 a, V3 b) {
        float d = sqrtf(Dist(a, b));
        return d < 1e-3f ? 1e-3f : d;
    };
    float t0 = 0, t1 = t0 + knot(p0, p1), t2 = t1 + knot(p1, p2),
          t3 = t2 + knot(p2, p3);
    float tt = t1 + (t2 - t1) * local;
    // Barry-Goldman pyramid: three lerps, then two, then one.
    V3 a1 = Lerp(p0, p1, (tt - t0) / (t1 - t0));
    V3 a2 = Lerp(p1, p2, (tt - t1) / (t2 - t1));
    V3 a3 = Lerp(p2, p3, (tt - t2) / (t3 - t2));
    V3 b1 = Lerp(a1, a2, (tt - t0) / (t2 - t0));
    V3 b2 = Lerp(a2, a3, (tt - t1) / (t3 - t1));
    return Lerp(b1, b2, (tt - t1) / (t2 - t1));
}

// A Hilbert curve resampled through a Catmull-Rom spline, plus the three
// vertex color schemes from the original demo.
static void BuildGeom(FpsApp* app) {
    V3 ctrl[kMaxCtrl];
    int nCtrl = 0;
    int order[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    Hilbert({}, kHilbertSize, kHilbertIter, order, ctrl, &nCtrl);
    int samples = nCtrl * kSubdiv;
    if (samples > kMaxPts - 1) {
        samples = kMaxPts - 1;
    }
    app->nPts = 0;
    for (int i = 0; i <= samples && app->nPts < kMaxPts; i++) {
        app->pts[app->nPts++] = Catmull(ctrl, nCtrl, (float)i / (float)samples);
    }
    for (int i = 0; i < app->nPts; i++) {
        V3 v = app->pts[i];
        float lx = -v.x / 200.f;
        float ly = -v.y / 200.f;
        app->pal[0][i] = RgbaHsla(0.6f, 1.f, (lx < 0 ? 0 : lx) + 0.5f, 1.f);
        app->pal[1][i] = RgbaHsla(0.9f, 1.f, (ly < 0 ? 0 : ly) + 0.5f, 1.f);
        app->pal[2][i] = RgbaHsla((float)i / (float)app->nPts, 1.f, 0.5f, 1.f);
    }
}

// Rotates around Y then X and applies a perspective divide.
static void Project(V3 v, float yaw, float pitch, float cx, float cy,
                    float scale, float* ox, float* oy) {
    float sy = sinf(yaw), cyw = cosf(yaw);
    float x = v.x * cyw + v.z * sy;
    float z = v.z * cyw - v.x * sy;
    float sp = sinf(pitch), cp = cosf(pitch);
    float y = v.y * cp - z * sp;
    z = z * cp + v.y * sp;
    // Guard the divide: a vertex level with the eye would blow up.
    float depth = kEye + z;
    if (depth < 1) {
        depth = 1;
    }
    float persp = kEye / depth;
    *ox = cx + x * persp * scale;
    *oy = cy + y * persp * scale;
}

static bool Finite(float v) {
    return v == v && v < 1e30f && v > -1e30f;
}

static void PaintCurves(PaintCtx* ctx, El* e, void* user) {
    auto* app = (FpsApp*)user;
    if (!ctx->rt) {
        return;
    }
    int curves = app->curves;
    // Lay the curves out on the squarest grid that fits them.
    int columns = (int)ceilf(sqrtf((float)curves));
    if (columns < 1) {
        columns = 1;
    }
    int rows = (curves + columns - 1) / columns;
    float cellW = e->w / (float)columns;
    float cellH = e->h / (float)rows;
    float scale =
        (cellW < cellH ? cellW : cellH) / (kHilbertExtent * 2.f) * kCellFill;
    float spin = (float)(TimeNow() - app->started) * 0.35f;

    for (int index = 0; index < curves; index++) {
        int column = index % columns;
        int row = index / columns;
        float cx = e->x + cellW * ((float)column + 0.5f);
        float cy = e->y + cellH * ((float)row + 0.5f);
        // Alternating spin direction, as in the original.
        float dir = (index % 2 == 0) ? 1.f : -1.f;
        float yaw = spin * dir + (float)index * 0.4f + app->tiltX;
        float pitch = app->tiltY;
        const Rgba* pal = app->pal[index % 3];
        float px[kMaxPts], py[kMaxPts];
        for (int i = 0; i < app->nPts; i++) {
            Project(app->pts[i], yaw, pitch, cx, cy, scale, &px[i], &py[i]);
        }

        // Paint the curve as short runs of constant color, approximating the
        // per-vertex gradient of the original.
        int start = 0;
        while (start + 1 < app->nPts) {
            int end = start + kSegPts;
            if (end > app->nPts - 1) {
                end = app->nPts - 1;
            }
            bool finite = true;
            for (int i = start; i <= end; i++) {
                if (!Finite(px[i]) || !Finite(py[i])) {
                    finite = false;
                    break;
                }
            }
            if (finite) {
                Rgba c = pal[(start + end) / 2];
                for (int i = start + 1; i <= end; i++) {
                    CanvasLine(ctx, px[i - 1], py[i - 1], px[i], py[i], 1.f, c);
                }
            }
            // Share the boundary vertex so runs join without a gap.
            start = end;
        }
    }
}

static void OnMouseMove(FpsApp* app, Ctx* cx, const MouseMoveEvent* ev) {
    WinSize view = WindowSize(cx->win);
    // A zero-sized viewport would divide to infinity here, and the easing in
    // Render would then keep the tilt non-finite forever.
    if (view.dipW <= 0 || view.dipH <= 0) {
        return;
    }
    // No notify: the scene already redraws every frame.
    app->cursorTiltX = (ev->x / view.dipW - 0.5f) * 2.4f;
    app->cursorTiltY = (ev->y / view.dipH - 0.5f) * 1.2f;
}

static void StepCurves(FpsApp* app, Ctx* cx, const ClickEvent*,
                       intptr_t delta) {
    int n = app->curves + (int)delta * kCurveStep;
    if (n < 1) {
        n = 1;
    }
    if (n > kMaxCurves) {
        n = kMaxCurves;
    }
    app->curves = n;
    Notify(cx);
}

static El* LoadButton(Ctx* cx, Str id, Str label, intptr_t delta) {
    return Div(cx->a)
        ->Click(HashClickId(id))
        ->FlexRow()
        ->ItemsCenter()
        ->PadX(12)
        ->PadY(4)
        ->Radius(6)
        ->Bg(RgbaHsla(0.f, 0.f, 1.f, 0.08f))
        ->Border(1, RgbaHsla(0.f, 0.f, 1.f, 0.16f))
        ->Child(
            TextEl(cx->a, label)->Font(12)->Fg(RgbaHsla(0.f, 0.f, 0.75f, 1.f)))
        ->OnClick(Listen(cx, &StepCurves, delta));
}

static El* RenderLoadControls(FpsApp* app, Ctx* cx) {
    Rgba fg = RgbaHsla(0.f, 0.f, 0.75f, 1.f);
    return Div(cx->a)
        ->Absolute()
        ->Bottom(16)
        ->Left(16)
        ->FlexRow()
        ->ItemsCenter()
        ->Gap(8)
        ->Font(12)
        ->Child(LoadButton(cx, StrL("fewer"), StrL("\xE2\x88\x92 load"), -1))
        ->Child(TextEl(cx->a, fmt("%d curves", app->curves))->Fg(fg))
        ->Child(LoadButton(cx, StrL("more"), StrL("+ load"), 1));
}

// Collect what the window drew since the last look, and when the run is up,
// write the distribution out and quit. The first second is warm-up: the swap
// chain and the font cache are still filling then, and those frames are not
// what the steady state costs.
static void BenchTick(FpsApp* app, Ctx* cx) {
    double elapsed = TimeNow() - app->started;
    FrameTiming timings[kFrameTraceCap];
    int n = WindowCollectFrames(cx->win, &app->benchCursor, timings,
                                kFrameTraceCap);
    if (elapsed > 1.0) {
        for (int i = 0; i < n && app->nBenchDraws < 4096; i++) {
            app->benchDraws[app->nBenchDraws++] = timings[i].drawSecs;
        }
    }
    if (elapsed < app->benchSecs) {
        return;
    }

    int count = app->nBenchDraws;
    for (int i = 1; i < count; i++) {
        float v = app->benchDraws[i];
        int j = i - 1;
        for (; j >= 0 && app->benchDraws[j] > v; j--) {
            app->benchDraws[j + 1] = app->benchDraws[j];
        }
        app->benchDraws[j + 1] = v;
    }
    double total = 0;
    for (int i = 0; i < count; i++) {
        total += app->benchDraws[i];
    }
    TempStr benchOut = StrDupTemp(app->benchOut);
    FILE* f = fopen(benchOut.s, "w");
    if (f && count > 0) {
        double secs = elapsed - 1.0;
        fprintf(f, "frames    %d over %.1fs (%.1f fps)\n", count, secs,
                secs > 0 ? count / secs : 0);
        fprintf(f, "mean      %.3f ms\n", total / count * 1000);
        fprintf(f, "median    %.3f ms\n", app->benchDraws[count / 2] * 1000);
        fprintf(f, "p95       %.3f ms\n",
                app->benchDraws[count * 95 / 100] * 1000);
        fprintf(f, "max       %.3f ms\n", app->benchDraws[count - 1] * 1000);
    }
    if (f) {
        fclose(f);
    }
    AppQuit(cx->win);
}

El* FpsApp::Render(FpsApp* app, Ctx* cx) {
    if (app->benchSecs > 0) {
        BenchTick(app, cx);
    }
    // Ease toward the cursor rather than snapping, the way the original demo
    // drifts its camera.
    app->tiltX += (app->cursorTiltX - app->tiltX) * kEase;
    app->tiltY += (app->cursorTiltY - app->tiltY) * kEase;

    El* canvas = Div(cx->a)->Absolute()->Top(0)->Left(0)->SizeFull();
    canvas->customPaint = PaintCurves;
    canvas->customUser = app;

    return Div(cx->a)
        ->SizeFull()
        ->Bg(Rgb(0, 0, 0))
        ->Child(canvas)
        ->Child(RenderLoadControls(app, cx))
        ->Child(FpsMonitorEl(cx));
}

int GpuiMain(int argc, char** argv) {
    App* app = AppNew();
    component::Init(app);
    Entity<FpsApp> view = EntityNew<FpsApp>(app);
    FpsApp* self = view.Get(app);
    int winW = 800;
    int winH = 600;
    for (int i = 1; i < argc; i++) {
        Str argument = Str(argv[i]);
        if (StrEq(argument, StrL("-bench")) && i + 1 < argc) {
            self->benchSecs = StrToFloatUnchecked(Str(argv[++i]));
        } else if (StrEq(argument, StrL("-bench-out")) && i + 1 < argc) {
            self->benchOut = Str(argv[++i]);
        } else if (StrEq(argument, StrL("-curves")) && i + 1 < argc) {
            self->curves = StrToIntUnchecked(Str(argv[++i]));
        } else if (StrEq(argument, StrL("-size")) && i + 1 < argc) {
            // -size WxH: the load knob the curve count is not. Line drawing
            // costs what it covers, so a number measured at one window size
            // says nothing about another.
            Str size = Str(argv[++i]);
            int w = StrToIntUnchecked(size);
            int x = StrFind(size, "x");
            int h =
                x >= 0
                    ? StrToIntUnchecked(Str(size.s + x + 1, len(size) - x - 1))
                    : 0;
            if (w > 0 && h > 0) {
                winW = w;
                winH = h;
            }
        }
    }
    ThemeSet(app, ThemeMode::Dark);
    self->started = TimeNow();
    BuildGeom(self);
    // The scene has to keep asking for frames to stay animated, which is what
    // the Rust example spells window.request_animation_frame().
    WinOpts opts = {};
    opts.anim = true;
    opts.timerMs = 16;
    Window* win =
        WindowOpenView(app, StrL("FPS Monitor C++"), winW, winH, view.id, opts);
    WindowOnMouseMove(win, ListenTo(view, &OnMouseMove));
    int rc = AppRun(app);
    AppFree(app);
    return rc;
}
