#ifndef GPUI_FPS_FPS_H_
#define GPUI_FPS_FPS_H_
/* Realtime performance HUD — crates/fps (the `gpui-fps` crate).
 *
 * Frames per second, a rolling frame time chart, and this process' GPU, CPU
 * and memory usage. Frame data comes from the window's own trace
 * (Window::frameSeq / frameTrace). The interval counts frames *presented*,
 * stamped with their own present time, so it agrees with the platform's
 * overlay; the frame cost is what the runtime actually spent drawing rather
 * than an approximation measured from the outside. The headline rate is
 * derived from that cost — the HUD never drives the frame loop, so nothing
 * it reports is something it caused.
 *
 *     ┌──────────────────────────┐
 *     │ ﹋﹏ MAX 118 FPS ﹋︿﹏﹋  │  ← the trace runs behind the headline
 *     │ INTERVAL          8.5 ms │  ← time between presents
 *     │ FRAME             8.4 ms │  ← what a typical frame cost
 *     │ P95              14.1 ms │  ← what its slow tail cost
 *     │ DROP 0.0%       INV  1.0 │
 *     │ GPU                31.0% │
 *     │ CPU 142%       MEM 84 MB │
 *     └──────────────────────────┘
 *
 * Like the Rust crate this leans on gpui only, so any example can overlay it:
 *
 *     Div(cx->a)->SizeFull()->Child(content)->Child(FpsMonitorEl(cx));
 *
 * The parent is the whole window here; the overlay places itself absolutely.
 * The returned overlay can change its corner and its frame budget
 * (FpsOverlayOpts).
 */

#include "gpui/gpui.h"

namespace gpui {

// ─── style ────────────────────────────────────────────────────────────────

// crates/fps/src/style.rs. Internal and fixed: the palette is not
// configurable because its contrast is load bearing.
struct FpsStyle {
    Rgba background; // backdrop behind the HUD
    Rgba foreground; // primary readouts (the FPS number)
    Rgba muted;      // secondary readouts (units, labels, resource row)
    Rgba good;       // frames inside the budget
    Rgba warn;       // frames over budget but within twice of it
    Rgba bad;        // frames over twice the budget
};

// Dark HUD, legible over any window background. The backdrop is nearly opaque
// on purpose: nothing can read the pixels underneath an element, so the only
// way to stay readable everywhere is to stop the background from taking part
// in the composite.
const FpsStyle& FpsStyleDark();

// The color for a frame that took `frameSecs` against `budgetSecs`.
Rgba FpsLevelColor(const FpsStyle& style, float frameSecs, float budgetSecs);

// ─── sampler ──────────────────────────────────────────────────────────────

enum : uint16_t {
    // DEFAULT_CAPACITY: frames the chart keeps.
    kFpsCapacity = 120,
    // Presents inside the one second FPS window. An uncapped renderer can
    // beat the sample capacity, so this is sized well past it.
    kFpsPresents = 512,
    // Resource readings inside the trailing window. The interval is clamped
    // to 200 ms and the window is three seconds, so sixteen is the most that
    // can ever be inside it; the rest is headroom.
    kResourceHistoryCap = 32,
    // WARMUP_FRAMES: frames dropped on the floor before any of them count.
    //
    // A window's first frames are its most expensive — shaders, the glyph
    // atlas, the icons, every cache still cold — and they are not what the
    // application costs to run. Measured, one of them is a hundred
    // milliseconds against a budget of sixteen, and a HUD that has seen eight
    // frames reports it as a twelfth of the window's work in amber. The reader
    // has done nothing wrong and there is nothing to fix, so the default
    // reading has to be a healthy one.
    kFpsWarmupFrames = 8,
};

// FrameSample: one drawn frame.
struct FrameSample {
    // How long the window's draw took for this frame, in seconds.
    float drawSecs = 0;
    // How many invalidations were coalesced into this frame. A number well
    // above one means the window was asked to redraw more often than it
    // could.
    uint64_t invalidations = 0;
};

// crates/fps/src/sampler.rs. Drains the window's frame trace and keeps the
// last `capacity` frames plus the present times inside the FPS window.
struct FrameSampler {
    FrameSample samples[kFpsCapacity] = {}; // oldest first
    int n = 0;
    int capacity = kFpsCapacity;
    // present_times: when the frames still inside the FPS window were
    // presented. The frame's own present time, not the moment this sampler
    // read the trace: a HUD that draws only when the window does reads the
    // trace in batches, and a batch stamped with the time it was read
    // collapses every frame in it onto one instant — the rate then depends on
    // how often the HUD looked, not on how often the window presented.
    double presents[kFpsPresents] = {};
    int nPresents = 0;
    uint64_t cursor = 0; // FrameTimingCollector position
    // How many more frames are dropped before the statistics begin.
    uint32_t warmup = kFpsWarmupFrames;
    // Whether the backlog has been discarded yet.
    //
    // The first read drains everything the window has recorded since it
    // opened. For a HUD switched on later that is history it was not there
    // for; for one up from the start it is the cold start. Neither is the
    // steady state the rows below the headline are describing.
    bool drainedBacklog = false;
};

// Drains the frames drawn since the previous call. Call once per rendered
// frame.
void FrameSamplerTick(FrameSampler* s, Window* win);
// The half of the tick that is not the window: the frames that arrived and
// the moment they were read, which is what makes the rolling window testable
// without a window to drive it. Rust filters the process-wide frame trace by
// window id here; ours is already per-window. A frame that was drawn but not
// presented (presentAt < 0) is a draw and not a present.
void FrameSamplerIngest(FrameSampler* s, const FrameTiming* frames, int n,
                        double now);
// ingest_draws: retains the cost of each drawn frame, newest last.
void FrameSamplerIngestDraws(FrameSampler* s, const FrameSample* samples,
                             int n);
// ingest_presents: records when frames were presented and forgets the ones
// that have aged out of the FPS window as of `now`. `presentAt` must be in
// order.
void FrameSamplerIngestPresents(FrameSampler* s, const double* presentAt, int n,
                                double now);
void FrameSamplerSetCapacity(FrameSampler* s, int capacity);
// Frames presented per second, over the frames still inside the one second
// window. Presented, not drawn: a drawn frame that was never presented did
// not reach the screen and is not a frame the reader saw. `n` frames span
// `n - 1` intervals, so the rate comes from the elapsed span rather than the
// raw count; that keeps it correct before the window fills.
float FrameSamplerFps(const FrameSampler* s);
// present_interval: mean seconds between consecutive presents inside the
// window, as the platform's overlay reports its frame interval. The
// reciprocal of the rate; zero when there is no rate.
float FrameSamplerPresentInterval(const FrameSampler* s);
// sustainable_rate: the rate a full redraw could sustain — what a frame's
// cost implies, held to what the panel can scan out. `displayPeriod` is the
// seconds between refreshes, or 0 — Rust's `None` — where the platform would
// not say what the panel runs at, and an uncapped reading is better than one
// held to a guess: see PlatDisplayRefreshPeriod for why guessing was tried
// and abandoned.
float FpsSustainableRate(float meanDrawSecs, double displayPeriod);
float FrameSamplerMeanDraw(const FrameSampler* s);
// percentile_draw: the draw time `percentile` of the retained frames came in
// at or under, as in 0.95 for the 95th. The mean beside it says what a
// typical frame costs; this says what the slow tail costs, and the tail is
// what a stutter *is*. The rank is the nearest one rather than an
// interpolation between two frames, so every value the HUD shows is a frame
// that was really drawn.
float FrameSamplerPercentileDraw(const FrameSampler* s, float percentile);
// mean_invalidations: mean number of invalidations coalesced into one
// retained frame. One means every redraw the window was asked for became a
// frame; well above one means it was asked far more often than it could
// answer, which is work being thrown away — and unlike a slow frame it does
// not show up in the draw times at all.
float FrameSamplerMeanInvalidations(const FrameSampler* s);
// The slowest retained frame, used to scale the chart's y axis.
float FrameSamplerPeakDraw(const FrameSampler* s);
// over_budget_ratio: share of the retained frames that overran `budgetSecs`,
// in 0..1.
float FrameSamplerOverBudget(const FrameSampler* s, float budgetSecs);

// A sample of this process' resource usage.
struct ResourceSample {
    // On the scale `top`, Activity Monitor and Task Manager's per-process
    // column all use: 100 is one saturated logical core, so a process spread
    // across a core and a half reads 140. Deliberately not divided by the
    // core count — that makes the reading depend on hardware the application
    // has nothing to do with (the same work reads 12% on a four core laptop
    // and 2% on a twenty-four core desktop) and compresses every interesting
    // value into the bottom of the range, where a UI thread pinning a core
    // reads 4% and looks idle.
    float cpuPercent = 0;
    // Memory this process is responsible for, in bytes: private memory rather
    // than the resident set, which is mostly shared library pages. See
    // SysSelfPrivateMemory.
    uint64_t memoryBytes = 0;
    // This process' share of the GPU, or -1 where the platform publishes no
    // counter for it — Rust's `Option<f32>`, and the reason the row is left
    // out on its own rather than showing a zero.
    float gpuPercent = -1.f;
};

// ResourceHistory: averages the resource readings taken inside a trailing
// window. Each of the three is a coarse sample of something that moves
// between one sample and the next: CPU is the share of a single interval, GPU
// the share of another, memory a snapshot of an allocator that grows and
// releases in steps. Published raw at the sampling cadence they jump by tens
// of percent between readings that describe the same steady workload, and the
// eye tracks the churn instead of the value.
struct ResourceHistory {
    double at[kResourceHistoryCap] = {};
    ResourceSample samples[kResourceHistoryCap] = {};
    int n = 0;
    // RESOURCE_WINDOW: how far back CPU, memory and GPU are averaged over. At
    // the default interval that is six readings: long enough to settle the
    // churn between one sample and the next, short enough that a real change
    // reaches the HUD while the reader is still looking at what caused it.
    float windowSecs = 3.f;
};

void ResourceHistoryPush(ResourceHistory* h, ResourceSample sample, double now);
// The mean of the retained readings; false before the first one has landed.
// The GPU share is averaged over the readings that carried one rather than
// over all of them: a momentary gap in a counter that is otherwise being
// published would otherwise read as a dip towards zero.
bool ResourceHistoryMean(const ResourceHistory* h, ResourceSample* out);

// Rust probes this on its background executor. Windows' process counters are
// cheap, but opening the GPU PDH wildcard counter is not, so this port keeps
// the whole probe off the render thread too. Returns false until it has a
// delta to divide by, and then the mean over the trailing window: averaging
// belongs on this side of the thread boundary, since the probe is what knows
// the cadence the readings arrive at, and the render thread should not be
// doing arithmetic over a history it would otherwise have to keep.
struct ResourceProbe {
    uint64_t prevCpu100ns = 0;
    double prevAt = 0;
    bool primed = false;
    ResourceHistory history;
};

bool ResourceProbeSample(ResourceProbe* probe, ResourceSample* out);

// ─── monitor ──────────────────────────────────────────────────────────────

// The numbers as last published to the screen.
struct FpsReadout {
    // The rate a full redraw of this window could sustain: the reciprocal of
    // frameMillis.
    //
    // Derived rather than counted, because counting it would mean causing it.
    // A frame rate measured from presents is only the rate the application
    // happens to be drawing at, and the only way to make that number mean "as
    // fast as this UI can go" is to keep the window drawing back to back —
    // which costs a full layout and paint per frame and lands in the resource
    // row right underneath. The frame cost answers the same question without
    // being paid for.
    //
    // A ceiling the frame cost can prove, not one the display can show: a
    // window whose frames cost 3ms could redraw 333 times a second, on a
    // panel that would scan out sixty of them.
    float maxFps = 0;
    // Frames presented per second: the rate the window is actually drawing
    // at, which an idle application drives to zero. The reciprocal of
    // intervalMillis.
    float fps = 0;
    // Mean time between presents, in milliseconds: the platform overlay's
    // "frame interval".
    float intervalMillis = 0;
    // Mean draw cost of the retained frames, in milliseconds.
    float frameMillis = 0;
    // The slow tail of the same frames frameMillis is the mean of.
    float percentileMillis = 0;
    float droppedPercent = 0;
    // Mean invalidations coalesced into one frame; one means none were
    // wasted.
    float invalidations = 0;
};

// Heap state shared by one resource worker and its main-thread completion.
// The definition stays private to fps.cpp.
struct FpsResourceJob;

// Which question the headline answers.
//
// Both readings come out of the same samples, so switching is free — which is
// the whole point. The rate a UI can hold and the rate it is holding are
// different questions, and the only expensive way to answer the first is to
// stop the second from being answerable.
enum class FpsHeadline : uint8_t {
    // The rate a full redraw could sustain, from what one costs.
    Max,
    // The rate the window is drawing at.
    Observed,
};

// crates/fps/src/monitor.rs. A view rather than a stateless component, so the
// click that collapses it has an entity to run against.
//
// The HUD never asks for a frame of its own. A dirty view schedules a
// *window* draw and the runtime rebuilds the whole tree, so a HUD that drove
// the frame loop to keep its counter moving would be paying a full layout
// and paint per frame — and reporting that cost in the resource row as if it
// were the application's. The headline is derived from what a frame costs
// instead, which answers the same question for free and leaves the readings
// measuring the application alone.
struct FpsMonitor {
    FrameSampler sampler;
    FpsReadout readout;
    double readoutAt = -1;
    // One 60Hz frame, the budget a frame is judged against. Set it to 1/144
    // on a high refresh rate display.
    float frameBudget = 1.f / 60.f;
    FpsHeadline headline = FpsHeadline::Max;
    // The panel's refresh period, and which display it was asked about, so
    // that moving the window to another monitor re-asks and staying on one
    // does not ask again every frame. `displayAsked` is Rust's `Some`.
    bool displayAsked = false;
    uint64_t display = 0;
    double displayPeriod = 0;
    bool showResources = true;
    float resourceInterval = 0.5f;
    ResourceProbe probe;
    ResourceSample resources;
    bool hasResources = false;
    // The clock that republishes the readings — Rust's `clock` task. Nothing
    // else wakes the HUD: it does not drive the frame loop, and a window that
    // has stopped drawing produces no renders to refresh it from, so without
    // this the figures would freeze at whatever the application last drew —
    // exactly when a frozen `137` is most likely to be read as the truth.
    // With resources on it is also when they are probed.
    Window* clockWindow = nullptr;
    int clockTimer = 0;
    int resourceTask = 0;
    FpsResourceJob* resourceJob = nullptr;
    bool compact = false;
    // Upper bound of the chart's y axis, in seconds.
    float axisMax = (1.f / 60.f) * 2.f;

    ~FpsMonitor();
    static El* Render(FpsMonitor* self, Ctx* cx);
    static void OnToggleCompact(FpsMonitor* self, Ctx* cx, const ClickEvent*);
    // The right button switches the headline between the two rates; the
    // `MAX` marker is what says which of the two the figure is.
    static void OnToggleHeadline(FpsMonitor* self, Ctx* cx,
                                 const MouseDownEvent* event);
    static void OnClockTick(FpsMonitor* self, Ctx* cx, const TickEvent*);
};

// set_frame_budget: what the overlay applies on the monitor's behalf. The
// budget also resets the chart's axis floor, so a 144Hz budget doesn't leave
// the chart scaled for 60Hz frames.
void FpsMonitorSetFrameBudget(FpsMonitor* self, float budgetSecs);

// format_cpu: a tenth below ten, whole percent above.
TempStr FpsFormatCpuTemp(float percent);
// format_bytes: whole MB, or GB to two places from a gigabyte up.
TempStr FpsFormatBytesTemp(uint64_t bytes);

// Where in the parent the HUD sits.
enum class FpsAnchor : uint8_t {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    TopCenter,
    BottomCenter,
    LeftCenter,
    RightCenter,
};

// crates/fps/src/overlay.rs: FpsOverlay's builder fields. The budget is
// Rust's `Option` — left unset it leaves the monitor as it is.
struct FpsOverlayOpts {
    // Where in the parent the HUD sits. Defaults to the top right.
    FpsAnchor anchor = FpsAnchor::TopRight;
    // The per-frame budget used for chart grading and its vertical scale, in
    // seconds; 0 leaves the monitor's budget alone.
    float frameBudget = 0;
};

// Pins a monitor to an edge or corner of its parent, the way a game overlays
// its frame counter.
El* FpsOverlayEl(Ctx* cx, Entity<FpsMonitor> monitor,
                 FpsOverlayOpts opts = FpsOverlayOpts{});

// gpui_fps::fps_monitor: the HUD pinned to the top right, with the monitor
// created on first use and reused afterwards, one per window. Render it only
// when it should be visible.
El* FpsMonitorEl(Ctx* cx, FpsOverlayOpts opts = FpsOverlayOpts{});

} // namespace gpui
#endif // GPUI_FPS_FPS_H_
