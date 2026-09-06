#include "fps/fps.h"

#include "gpui/paint.h"
#include "sys/executor.h"
#include "sys/gpu.h"
#include "sys/sysinfo.h"

#include <math.h>

namespace gpui {

// ─── style (crates/fps/src/style.rs) ──────────────────────────────────────

const FpsStyle& FpsStyleDark() {
    // The trace colors lean bright and saturated so the chart reads like a
    // vitals monitor against the dark backdrop.
    static FpsStyle style = {
        RgbaHsla(0.f, 0.f, 0.04f, 0.92f),   // background
        RgbaHsla(0.f, 0.f, 0.98f, 1.f),     // foreground
        RgbaHsla(0.f, 0.f, 0.62f, 1.f),     // muted
        RgbaHsla(0.41f, 0.95f, 0.56f, 1.f), // good
        RgbaHsla(0.11f, 0.95f, 0.6f, 1.f),  // warn
        RgbaHsla(0.99f, 0.9f, 0.62f, 1.f),  // bad
    };
    return style;
}

Rgba FpsLevelColor(const FpsStyle& style, float frameSecs, float budgetSecs) {
    if (frameSecs <= budgetSecs) {
        return style.good;
    }
    if (frameSecs <= budgetSecs * 2.f) {
        return style.warn;
    }
    return style.bad;
}

// ─── sampler (crates/fps/src/sampler.rs) ──────────────────────────────────

// FPS_WINDOW: frames presented longer ago than this stop contributing to the
// FPS readout.
static const double kFpsWindow = 1.0;

void FrameSamplerSetCapacity(FrameSampler* s, int capacity) {
    if (capacity < 1) {
        capacity = 1;
    }
    if (capacity > kFpsCapacity) {
        capacity = kFpsCapacity;
    }
    s->capacity = capacity;
    if (s->n > capacity) {
        int drop = s->n - capacity;
        memmove(s->samples, s->samples + drop,
                sizeof(FrameSample) * (size_t)capacity);
        s->n = capacity;
    }
}

void FrameSamplerIngestDraws(FrameSampler* s, const FrameSample* samples,
                             int n) {
    if (!s) {
        return;
    }
    if (s->capacity < 1 || s->capacity > kFpsCapacity) {
        s->capacity = kFpsCapacity;
    }
    for (int i = 0; i < n; i++) {
        if (s->n == s->capacity) {
            memmove(s->samples, s->samples + 1,
                    sizeof(FrameSample) * (size_t)(s->n - 1));
            s->n--;
        }
        s->samples[s->n++] = samples[i];
    }
}

void FrameSamplerIngestPresents(FrameSampler* s, const double* presentAt, int n,
                                double now) {
    if (!s) {
        return;
    }
    for (int i = 0; i < n; i++) {
        if (s->nPresents == kFpsPresents) {
            memmove(s->presents, s->presents + 1,
                    sizeof(double) * (size_t)(s->nPresents - 1));
            s->nPresents--;
        }
        s->presents[s->nPresents++] = presentAt[i];
    }

    int drop = 0;
    while (drop < s->nPresents && now - s->presents[drop] > kFpsWindow) {
        drop++;
    }
    if (drop > 0) {
        memmove(s->presents, s->presents + drop,
                sizeof(double) * (size_t)(s->nPresents - drop));
        s->nPresents -= drop;
    }
}

void FrameSamplerIngest(FrameSampler* s, const FrameTiming* frames, int n,
                        double now) {
    if (!s) {
        return;
    }
    if (n > kFrameTraceCap) {
        n = kFrameTraceCap;
    }
    FrameSample draws[kFrameTraceCap];
    double presents[kFrameTraceCap];
    int nPresents = 0;
    for (int i = 0; i < n; i++) {
        draws[i].drawSecs = frames[i].drawSecs;
        draws[i].invalidations = frames[i].invalidations;
        // A frame the scene found identical to the last one was drawn but
        // never presented, so it costs a draw time and delimits no interval.
        if (frames[i].presentAt >= 0) {
            presents[nPresents++] = frames[i].presentAt;
        }
    }
    FrameSamplerIngestDraws(s, draws, n);
    FrameSamplerIngestPresents(s, presents, nPresents, now);
}

void FrameSamplerTick(FrameSampler* s, Window* win) {
    if (!s || !win) {
        return;
    }
    FrameTiming timings[kFrameTraceCap];
    int n = WindowCollectFrames(win, &s->cursor, timings, kFrameTraceCap);
    FrameSamplerIngest(s, timings, n, TimeNow());
}

float FrameSamplerFps(const FrameSampler* s) {
    if (s->nPresents < 2) {
        return 0;
    }
    double span = s->presents[s->nPresents - 1] - s->presents[0];
    if (span <= 0) {
        return 0;
    }
    return (float)((double)(s->nPresents - 1) / span);
}

float FrameSamplerPresentInterval(const FrameSampler* s) {
    float fps = FrameSamplerFps(s);
    if (fps <= 0) {
        return 0;
    }
    return 1.f / fps;
}

float FrameSamplerMeanDraw(const FrameSampler* s) {
    if (s->n <= 0) {
        return 0;
    }
    double total = 0;
    for (int i = 0; i < s->n; i++) {
        total += s->samples[i].drawSecs;
    }
    return (float)(total / (double)s->n);
}

float FrameSamplerPercentileDraw(const FrameSampler* s, float percentile) {
    if (s->n <= 0) {
        return 0;
    }
    // A sorted copy of the draw times. The retained set is at most
    // kFpsCapacity long, so an insertion sort is the whole of it.
    float draws[kFpsCapacity];
    for (int i = 0; i < s->n; i++) {
        float v = s->samples[i].drawSecs;
        int j = i - 1;
        for (; j >= 0 && draws[j] > v; j--) {
            draws[j + 1] = draws[j];
        }
        draws[j + 1] = v;
    }
    if (percentile < 0) {
        percentile = 0;
    }
    if (percentile > 1) {
        percentile = 1;
    }
    int last = s->n - 1;
    int rank = (int)lroundf(percentile * (float)last);
    if (rank > last) {
        rank = last;
    }
    return draws[rank];
}

float FrameSamplerMeanInvalidations(const FrameSampler* s) {
    if (s->n <= 0) {
        return 0;
    }
    uint64_t total = 0;
    for (int i = 0; i < s->n; i++) {
        total += s->samples[i].invalidations;
    }
    return (float)total / (float)s->n;
}

float FrameSamplerPeakDraw(const FrameSampler* s) {
    float peak = 0;
    for (int i = 0; i < s->n; i++) {
        if (s->samples[i].drawSecs > peak) {
            peak = s->samples[i].drawSecs;
        }
    }
    return peak;
}

float FrameSamplerOverBudget(const FrameSampler* s, float budgetSecs) {
    if (s->n <= 0) {
        return 0;
    }
    int over = 0;
    for (int i = 0; i < s->n; i++) {
        if (s->samples[i].drawSecs > budgetSecs) {
            over++;
        }
    }
    return (float)over / (float)s->n;
}

void ResourceHistoryPush(ResourceHistory* h, ResourceSample sample,
                         double now) {
    if (!h) {
        return;
    }
    if (h->n == kResourceHistoryCap) {
        memmove(h->at, h->at + 1, sizeof(double) * (size_t)(h->n - 1));
        memmove(h->samples, h->samples + 1,
                sizeof(ResourceSample) * (size_t)(h->n - 1));
        h->n--;
    }
    h->at[h->n] = now;
    h->samples[h->n] = sample;
    h->n++;

    // The window is inclusive of a reading exactly its age.
    int drop = 0;
    while (drop < h->n && now - h->at[drop] > (double)h->windowSecs) {
        drop++;
    }
    if (drop > 0) {
        memmove(h->at, h->at + drop, sizeof(double) * (size_t)(h->n - drop));
        memmove(h->samples, h->samples + drop,
                sizeof(ResourceSample) * (size_t)(h->n - drop));
        h->n -= drop;
    }
}

bool ResourceHistoryMean(const ResourceHistory* h, ResourceSample* out) {
    if (!h || !out || h->n <= 0) {
        return false;
    }
    double cpu = 0;
    // Summed wide: the byte counts are large and the window is only bounded
    // by time, so a fast sampling cadence must not be able to overflow it.
    // Rust sums into a u128; a double carries the same range here.
    double memory = 0;
    double gpuTotal = 0;
    int gpuReadings = 0;
    for (int i = 0; i < h->n; i++) {
        const ResourceSample& s = h->samples[i];
        cpu += s.cpuPercent;
        memory += (double)s.memoryBytes;
        if (s.gpuPercent >= 0) {
            gpuTotal += s.gpuPercent;
            gpuReadings++;
        }
    }
    out->cpuPercent = (float)(cpu / (double)h->n);
    out->memoryBytes = (uint64_t)(memory / (double)h->n);
    out->gpuPercent =
        gpuReadings > 0 ? (float)(gpuTotal / (double)gpuReadings) : -1.f;
    return true;
}

// ResourceProbe::read: one raw reading, before the window averages it.
static bool ResourceProbeRead(ResourceProbe* probe, ResourceSample* out,
                              double now) {
    uint64_t cpu100ns = 0;
    uint64_t residentBytes = 0;
    if (!PlatSelfUsage(&cpu100ns, &residentBytes)) {
        return false;
    }

    // The first sample only establishes the baseline; CPU is a delta against
    // the previous one and reads zero until there is one.
    bool primed = probe->primed;
    double elapsed = now - probe->prevAt;
    uint64_t delta = cpu100ns - probe->prevCpu100ns;
    probe->prevCpu100ns = cpu100ns;
    probe->prevAt = now;
    probe->primed = true;
    if (!primed || elapsed <= 0) {
        // ResourceProbe::new constructs the GPU probe on Rust's background
        // executor. Do the same while establishing our CPU baseline, so the
        // expensive first PDH wildcard lookup cannot land in a later render.
        (void)GpuAvailable();
        return false;
    }

    // 100ns ticks of CPU over 100ns ticks of wall clock. Left on the single
    // core scale sysinfo reports, which passes 100 as soon as the process
    // spreads over more than one core; it is not divided by the core count.
    out->cpuPercent = (float)((double)delta / (elapsed * 1e7) * 100.);
    // Private memory where the platform publishes the counter, and the
    // resident set where it does not — a worse number, but a present one.
    uint64_t privateBytes = 0;
    out->memoryBytes =
        SysSelfPrivateMemory(&privateBytes) ? privateBytes : residentBytes;
    // The third reading, and for a renderer the half that usually explains a
    // slow frame. -1 where the platform has no counter, which keeps the row
    // out of the HUD rather than showing a zero.
    out->gpuPercent = GpuUsagePercent();
    return true;
}

bool ResourceProbeSample(ResourceProbe* probe, ResourceSample* out) {
    if (!probe || !out) {
        return false;
    }
    double now = TimeNow();
    ResourceSample reading;
    if (!ResourceProbeRead(probe, &reading, now)) {
        return false;
    }
    ResourceHistoryPush(&probe->history, reading, now);
    return ResourceHistoryMean(&probe->history, out);
}

// ─── monitor (crates/fps/src/monitor.rs) ──────────────────────────────────

// FRAME_PERCENTILE: which frame the P95 row reports. The 95th rather than the
// 99th: the chart keeps 120 frames by default, so the 99th is the second
// slowest of them — one frame, which moves the row on its own and reads as
// noise.
static const float kFramePercentile = 0.95f;

// How fast the chart's y axis relaxes back down after a spike. Growth is
// immediate so a slow frame is never clipped, while the decay is gradual so
// the bars don't visibly rescale every frame.
static const float kAxisDecay = 0.04f;

// A fixed width keeps every row flush with the chart and stops the HUD from
// resizing as the readings gain or lose digits. Collapsed, the HUD hugs its
// text instead and only the figure gets a fixed box.
static const float kHudWidth = 172.f;
static const float kCompactFigureWidth = 25.f;

// Size of every label and reading. Collapsed, the figure uses it too.
static const float kTextSize = 10.f;

// The trace sits behind the headline, so it is dimmed enough to stay out of
// the figure's way while still showing its shape and color.
static const float kTraceOpacity = 0.35f;

// Tall enough to give the trace room to show its shape around the figure.
static const float kHeadlineHeight = 35.f;

// The headline figure, in a box wide enough for four digits: an uncapped
// frame rate on a small window reaches four figures.
static const float kFigureSize = 28.f;
static const float kFigureWidth = 70.f;

// Width of the `FPS` unit, and of the empty box mirroring it on the other side
// of the figure so the figure lands on the HUD's true center.
static const float kUnitWidth = 22.f;

// How often the numbers are recomputed. The trace keeps up with every frame,
// but the readings do not: recomputed per frame they flicker through digits
// too fast to read.
static const double kReadoutInterval = 0.5;

// Distance from the edges the overlay is pinned to.
static const float kOverlayMargin = 12.f;

// A tenth is worth showing while the reading is small, where it is the
// difference between idle and a busy timer; past ten the extra digit only
// churns, and dropping it also keeps the reading inside the row's share of the
// HUD on a machine with enough cores to reach four figures.
TempStr FpsFormatCpuTemp(float percent) {
    if (percent < 10.f) {
        return fmt("%.1f%%", percent);
    }
    return fmt("%.0f%%", percent);
}

TempStr FpsFormatBytesTemp(uint64_t bytes) {
    const double kMib = 1024. * 1024.;
    const double kGib = kMib * 1024.;
    double v = (double)bytes;
    if (v >= kGib) {
        return fmt("%.2f GB", v / kGib);
    }
    return fmt("%.0f MB", v / kMib);
}

void FpsMonitorSetFrameBudget(FpsMonitor* self, float budgetSecs) {
    if (!self || budgetSecs <= 0) {
        return;
    }
    self->frameBudget = budgetSecs;
    self->axisMax = budgetSecs * 2.f;
}

void FpsMonitorSetContinuous(FpsMonitor* self, bool continuous) {
    if (self) {
        self->continuous = continuous;
    }
}

// Republishes the readings if kReadoutInterval has passed.
static void UpdateReadout(FpsMonitor* self) {
    double now = TimeNow();
    if (self->readoutAt >= 0 && now - self->readoutAt < kReadoutInterval) {
        return;
    }
    const FrameSampler* s = &self->sampler;
    self->readout.fps = FrameSamplerFps(s);
    self->readout.intervalMillis = FrameSamplerPresentInterval(s) * 1000.f;
    // The mean over the interval rather than the latest frame, which at this
    // cadence would be an arbitrary sample.
    self->readout.frameMillis = FrameSamplerMeanDraw(s) * 1000.f;
    self->readout.percentileMillis =
        FrameSamplerPercentileDraw(s, kFramePercentile) * 1000.f;
    self->readout
        .droppedPercent = FrameSamplerOverBudget(s, self->frameBudget) * 100.f;
    self->readout.invalidations = FrameSamplerMeanInvalidations(s);
    self->readoutAt = now;
}

// Grows immediately to fit the slowest retained frame and decays back slowly,
// so a single spike doesn't make the whole chart jump.
static void UpdateAxis(FpsMonitor* self) {
    float floorSecs = self->frameBudget * 2.f;
    float target = FrameSamplerPeakDraw(&self->sampler);
    if (target < floorSecs) {
        target = floorSecs;
    }
    self->axisMax = target > self->axisMax
                        ? target
                        : self->axisMax + (target - self->axisMax) * kAxisDecay;
}

struct FpsResourceJob {
    App* app = nullptr;
    EntityId monitor = {};
    ResourceProbe probe;
    ResourceSample sample;
    bool ok = false;
};

static void FpsResourceWork(FpsResourceJob* job) {
    job->ok = ResourceProbeSample(&job->probe, &job->sample);
}

static void FpsResourceDone(FpsResourceJob* job) {
    FpsMonitor* self = job && job->app
                           ? (FpsMonitor*)EntityGet(job->app, job->monitor)
                           : nullptr;
    // A completion can arrive after the keyed state was dropped, or after a
    // newer job replaced it. In either case the heap job is all that remains
    // ours to touch.
    if (!self || self->resourceJob != job) {
        delete job;
        return;
    }
    self->resourceJob = nullptr;
    self->resourceTask = 0;
    self->probe = job->probe;
    if (job->ok) {
        self->resources = job->sample;
        self->hasResources = true;
        NotifyEntity(job->app, job->monitor, nullptr);
    }
    delete job;
}

void FpsMonitor::OnResourceTick(FpsMonitor* self, Ctx* cx, const TickEvent*) {
    if (!self || !cx || self->resourceTask || self->resourceJob) {
        return;
    }
    FpsResourceJob* job = new FpsResourceJob();
    job->app = cx->app;
    job->monitor = cx->self;
    job->probe = self->probe;
    int task =
        ExecSpawn(MkFunc0(FpsResourceWork, job), MkFunc0(FpsResourceDone, job));
    if (!task) {
        delete job;
        return;
    }
    self->resourceJob = job;
    self->resourceTask = task;
}

static void StartResourceSampling(FpsMonitor* self, Ctx* cx) {
    if (!self->showResources || self->resourceTimer || !cx->win) {
        return;
    }
    int ms = (int)lroundf(self->resourceInterval * 1000.f);
    // sysinfo refuses a faster refresh too; even the native one-process
    // counters become noise below this point.
    if (ms < 200) {
        ms = 200;
    }
    self->resourceWindow = cx->win;
    self->resourceTimer =
        WindowSetInterval(cx->win, ms, Listen(cx, &FpsMonitor::OnResourceTick));
    if (!self->resourceTimer) {
        self->resourceWindow = nullptr;
        return;
    }
    // Rust constructs and primes ResourceProbe immediately on its background
    // executor, then waits one interval before publishing the first delta.
    FpsMonitor::OnResourceTick(self, cx, nullptr);
}

FpsMonitor::~FpsMonitor() {
    if (resourceWindow && resourceTimer) {
        WindowCancelTimer(resourceWindow, resourceTimer);
    }
    if (resourceTask && ExecCancel(resourceTask)) {
        delete resourceJob;
    } else if (resourceJob) {
        // The job is already running or its completion is queued, so it
        // outlives this monitor and will be drained later — possibly after
        // AppFree. Detach it: FpsResourceDone answers a null app by deleting
        // the job and touching nothing else, so the App pointer it carries
        // can never be followed once the App is gone.
        resourceJob->app = nullptr;
    }
    resourceWindow = nullptr;
    resourceTimer = 0;
    resourceTask = 0;
    resourceJob = nullptr;
}

static bool SameRgba(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// The frame time trace, drawn behind the readings so it fills the HUD instead
// of taking a band of its own. Drawn as runs of equal color rather than one
// segment per frame: in the common case where nothing is dropped the whole
// chart collapses into a single run.
static void PaintFpsTrace(PaintCtx* ctx, El* e, void* user) {
    auto* self = (FpsMonitor*)user;
    const FrameSampler* s = &self->sampler;
    if (!ctx->rt || s->n < 2 || e->w <= 0 || e->h <= 0) {
        return;
    }
    const FpsStyle& style = FpsStyleDark();
    float axisMax = self->axisMax > 1e-6f ? self->axisMax : 1e-6f;
    float slot = e->w / (float)s->capacity;
    // Fewer samples than the capacity means the chart is still filling up;
    // keep the newest frame pinned to the right edge so the history scrolls
    // instead of stretching.
    int leading = s->capacity - s->n;
    if (leading < 0) {
        leading = 0;
    }

    float px[kFpsCapacity];
    float py[kFpsCapacity];
    Rgba colors[kFpsCapacity];
    for (int i = 0; i < s->n; i++) {
        float secs = s->samples[i].drawSecs;
        float ratio = secs / axisMax;
        if (ratio < 0) {
            ratio = 0;
        }
        if (ratio > 1) {
            ratio = 1;
        }
        px[i] = e->x + slot * (float)(leading + i) + slot * 0.5f;
        py[i] = e->y + e->h * (1.f - ratio);
        colors[i] = RgbaOpacity(FpsLevelColor(style, secs, self->frameBudget),
                                kTraceOpacity);
    }

    int start = 0;
    while (start + 1 < s->n) {
        // A segment is as slow as the frame it ends on, so the color of the
        // later point decides the run.
        Rgba color = colors[start + 1];
        int end = start + 1;
        while (end < s->n && SameRgba(colors[end], color)) {
            CanvasLine(ctx, px[end - 1], py[end - 1], px[end], py[end], 1.f,
                       color);
            end++;
        }
        // Share the boundary point with the next run so the line stays
        // connected across a color change.
        start = end - 1;
    }
}

// row(): a row carrying two pairs, pushed to either inner edge.
static El* FpsRow(Ctx* cx) {
    return Div(cx->a)->FlexRow()->W(kFill)->JustifyBetween()->Gap(8)->PadY(1);
}

// A `LABEL value` pair kept together, for rows that carry more than one
// reading. The label stays muted so it reads as a caption, not as data.
static El* FpsPair(Ctx* cx, Str label, Str value, Rgba valueColor,
                   const FpsStyle& style) {
    return Div(cx->a)
        ->FlexRow()
        ->Gap(4)
        ->Child(TextEl(cx->a, label)->Fg(style.muted))
        ->Child(TextEl(cx->a, value)->Fg(valueColor));
}

// One `LABEL … value` row. The value is right aligned against the HUD's inner
// edge, so in a monospace font every row's digits line up in a column and
// nothing shifts as the readings change width.
static El* FpsReading(Ctx* cx, Str label, Str value, Rgba valueColor,
                      const FpsStyle& style) {
    return FpsRow(cx)
        ->Child(TextEl(cx->a, label)->Fg(style.muted))
        ->Child(TextEl(cx->a, value)->Fg(valueColor));
}

// The headline reading, with the frame time trace painted behind it.
//
// The trace lives in this row rather than spanning the whole HUD because this
// is its emptiest part — the figure is centered and short, leaving both flanks
// open — so the trace stays readable instead of being cut up by the denser
// rows below.
static El* FpsHeadline(Ctx* cx, FpsMonitor* self, float fps, Rgba color,
                       const FpsStyle& style) {
    El* trace = Div(cx->a)->Absolute()->Top(0)->Left(0)->SizeFull();
    trace->customPaint = PaintFpsTrace;
    trace->customUser = self;

    // The figure is centered in a fixed box so neither the unit nor the group
    // shifts as the count gains or loses a digit; the two share a bottom edge.
    // The box is the figure's own size because the headline sets
    // line_height(relative(1.)) on it, tighter than the inherited phi.
    El* figure = Div(cx->a)
                     ->W(kFigureWidth)
                     ->H(kFigureSize)
                     ->FlexRow()
                     ->ItemsCenter()
                     ->JustifyCenter()
                     ->Child(TextEl(cx->a, fmt("%.0f", fps))
                                 ->Font(kFigureSize)
                                 ->LineHeight(1.f)
                                 ->Fg(color));

    return Div(cx->a)
        ->ClipY()
        ->W(kFill)
        ->H(kHeadlineHeight)
        ->Child(trace)
        ->Child(Div(cx->a)
                    ->FlexRow()
                    ->SizeFull()
                    ->ItemsEnd()
                    ->JustifyCenter()
                    ->Gap(4)
                    // An empty box matching the unit on the right. Without it
                    // the unit's own width pushes the figure off center by
                    // half of it, which reads as misalignment.
                    ->Child(Div(cx->a)->W(kUnitWidth)->H(kTextSize))
                    ->Child(figure)
                    ->Child(Div(cx->a)
                                ->W(kUnitWidth)
                                ->Child(TextEl(cx->a, StrL("FPS"))
                                            ->Fg(style.muted))));
}

void FpsMonitor::OnToggleCompact(FpsMonitor* self, Ctx* cx, const ClickEvent*) {
    self->compact = !self->compact;
    Notify(cx);
}

El* FpsMonitor::Render(FpsMonitor* self, Ctx* cx) {
    FrameSamplerTick(&self->sampler, cx->win);
    UpdateReadout(self);
    UpdateAxis(self);
    StartResourceSampling(self, cx);
    // The HUD keeps the window drawing back to back. GPUI spells this
    // window.request_animation_frame() once per render.
    if (self->continuous && cx->win) {
        WindowRequestAnimationFrame(cx->win);
    }

    const FpsStyle& style = FpsStyleDark();
    FpsReadout r = self->readout;
    float budget = self->frameBudget;
    // A low demand-driven rate does not mean expensive frames. Only the
    // frame-cost rows are graded against the budget.
    Rgba fpsColor = style.foreground;

    El* hud = Div(cx->a)
                  ->Click(HashClickId(StrL("gpui-fps-hud")))
                  ->FlexRow()
                  ->Bg(style.background)
                  ->Mono()
                  ->Font(kTextSize)
                  ->SuppressTextSelection()
                  ->OnClick(Listen(cx, &FpsMonitor::OnToggleCompact));

    if (self->compact) {
        // Collapsed, the HUD is one small tag: the figure drops to the same
        // size as its unit, the box shrinks to the text, and everything else
        // is dropped, so it sits over the interface without competing with it.
        return hud->ItemsCenter()
            ->Gap(4)
            ->PadX(6)
            ->PadY(2)
            ->Radius(3)
            ->Child(
                Div(cx->a)
                    ->W(kCompactFigureWidth)
                    ->FlexRow()
                    ->JustifyEnd()
                    ->Child(TextEl(cx->a, fmt("%.0f", r.fps))->Fg(fpsColor)))
            ->Child(TextEl(cx->a, StrL("FPS"))->Fg(style.muted));
    }

    hud->FlexCol()
        ->W(kHudWidth)
        ->PadX(8)
        ->PadY(6)
        ->Radius(4)
        ->Child(FpsHeadline(cx, self, r.fps, fpsColor, style))
        // The same figure the platform overlay calls its frame interval: time
        // between presents, which is the headline's reciprocal. Ungraded,
        // like there.
        ->Child(FpsReading(cx, StrL("INTERVAL"),
                           fmt("%.1f ms", r.intervalMillis), style.foreground,
                           style))
        // Graded against the budget, not against the frame rate: an idle
        // window draws a handful of frames a second, every one of them well
        // inside the budget, and this row is what says so.
        ->Child(FpsReading(cx, StrL("FRAME"), fmt("%.1f ms", r.frameMillis),
                           FpsLevelColor(style, r.frameMillis / 1000.f, budget),
                           style))
        // Graded the same way, so the two millisecond rows read as one
        // measurement seen twice: what a frame usually costs, and what its
        // slow tail costs.
        ->Child(FpsReading(
            cx, StrL("P95"), fmt("%.1f ms", r.percentileMillis),
            FpsLevelColor(style, r.percentileMillis / 1000.f, budget), style))
        // Dropped frames and wasted invalidations share a row: both count
        // redundant work rather than measuring a duration, so neither belongs
        // in the millisecond column above.
        ->Child(
            FpsRow(cx)
                ->Child(
                    FpsPair(cx, StrL("DROP"), fmt("%.1f%%", r.droppedPercent),
                            FpsLevelColor(
                                style, r.droppedPercent > 0 ? 1.f : 0.f, 0.5f),
                            style))
                // Ungraded, unlike every other reading in the HUD. One per
                // frame is the ideal, but it is not the floor here: in
                // continuous mode the monitor requests an animation frame of
                // its own on every render, so an application invalidating
                // once a frame measures two and a healthy HUD would sit
                // permanently in the red. The baseline depends on that switch
                // and on how the application drives its own redraws, which is
                // not something the HUD can grade — so the number is reported
                // and the reading is left to whoever knows what to expect.
                ->Child(FpsPair(cx, StrL("INV"), fmt("%.1f", r.invalidations),
                                style.foreground, style)));
    if (self->showResources && self->hasResources) {
        // The GPU reading is a row of its own, and is left out where the
        // platform publishes no counter for it — `when_some(gpu_percent)`.
        if (self->resources.gpuPercent >= 0) {
            hud->Child(FpsReading(cx, StrL("GPU"),
                                  fmt("%.1f%%", self->resources.gpuPercent),
                                  style.foreground, style));
        }
        // CPU and memory share a row: both are coarse background samples,
        // unlike the per-frame numbers.
        hud->Child(
            FpsRow(cx)
                ->Child(FpsPair(cx, StrL("CPU"),
                                FpsFormatCpuTemp(self->resources.cpuPercent),
                                style.foreground, style))
                ->Child(FpsPair(cx, StrL("MEM"),
                                FpsFormatBytesTemp(self->resources.memoryBytes),
                                style.foreground, style)));
    }
    return hud;
}

// ─── overlay (crates/fps/src/overlay.rs) ──────────────────────────────────

El* FpsOverlayEl(Ctx* cx, Entity<FpsMonitor> monitor, FpsOverlayOpts opts) {
    // The overlay's settings land on the monitor before it renders, the way
    // FpsOverlay::render updates the entity before handing it to the tree.
    if (opts.frameBudget > 0 || opts.continuous >= 0) {
        FpsMonitor* self = monitor.Get(cx->app);
        if (self) {
            if (opts.frameBudget > 0) {
                FpsMonitorSetFrameBudget(self, opts.frameBudget);
            }
            if (opts.continuous >= 0) {
                FpsMonitorSetContinuous(self, opts.continuous != 0);
            }
        }
    }
    El* hud = EntityRender(cx->app, cx->win, cx->a, monitor.id);
    if (!hud) {
        return Div(cx->a);
    }
    // Corners are placed by their own two offsets so the overlay stays the
    // size of the HUD. The centered anchors need a strip to center within, but
    // it is only stretched along the one axis that needs it, keeping the area
    // laid over the content as small as possible.
    El* box = Div(cx->a)->Absolute()->FlexRow();
    float m = kOverlayMargin;
    switch (opts.anchor) {
        case FpsAnchor::TopLeft:
            box->Top(m)->Left(m);
            break;
        case FpsAnchor::TopRight:
            box->Top(m)->Right(m);
            break;
        case FpsAnchor::BottomLeft:
            box->Bottom(m)->Left(m);
            break;
        case FpsAnchor::BottomRight:
            box->Bottom(m)->Right(m);
            break;
        case FpsAnchor::TopCenter:
            box->Top(m)->Left(0)->W(kFill)->JustifyCenter();
            break;
        case FpsAnchor::BottomCenter:
            box->Bottom(m)->Left(0)->W(kFill)->JustifyCenter();
            break;
        case FpsAnchor::LeftCenter:
            box->Left(m)->Top(0)->H(kFill)->ItemsCenter();
            break;
        case FpsAnchor::RightCenter:
            box->Right(m)->Top(0)->H(kFill)->ItemsCenter();
            break;
    }
    return box->Child(hud);
}

El* FpsMonitorEl(Ctx* cx, FpsOverlayOpts opts) {
    // The monitor is created on first use and reused afterwards, one per
    // window, so this can be called straight from Render every frame. Rust
    // keeps the same map as a global keyed by WindowId.
    auto* slot = KeyedState<Entity<FpsMonitor>>(
        cx, (uint32_t)HashClickId(StrL("gpui-fps-monitor")));
    if (!slot) {
        return Div(cx->a);
    }
    if (!slot->IsValid()) {
        *slot = EntityNew<FpsMonitor>(cx);
    }
    return FpsOverlayEl(cx, *slot, opts);
}

} // namespace gpui
