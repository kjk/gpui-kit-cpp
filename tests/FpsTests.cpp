/* Ported from crates/fps/src/sampler.rs, monitor.rs and memory.rs, mod tests.
 *
 * `ignores_frames_from_other_windows` has no counterpart: Rust filters a
 * process-wide frame trace by window id, while ours is already per-window, so
 * there is nothing to filter. lib.rs' FrameTraceGuard test has none either —
 * the trace is always on here (see gpui::FrameTiming) — and
 * `test_fps_monitor_builder` is a #[gpui::test]. The rest is the same
 * arithmetic on the same rolling windows. */

#include "Test.h"

#include <math.h>
#include <stdlib.h>

// timing(): a frame that answered one request to redraw. Named
// FpsTiming because gpui::Timing is the motion core's timing block.
static FrameSample FpsTiming(float drawSecs) {
    FrameSample s;
    s.drawSecs = drawSecs;
    s.invalidations = 1;
    return s;
}

// coalesced(): a frame that answered `invalidations` requests at once.
static FrameSample Coalesced(float drawSecs, uint64_t invalidations) {
    FrameSample s = FpsTiming(drawSecs);
    s.invalidations = invalidations;
    return s;
}

// warmed_sampler: a sampler past its warm-up, which is where every statistic
// below is measured from. The warm-up itself is covered by its own test.
static void Warm(FrameSampler* s) {
    s->warmup = 0;
    s->drainedBacklog = true;
}

static void IngestDraw(FrameSampler* s, FrameSample sample) {
    FrameSamplerIngestDraws(s, &sample, 1);
}

static void IngestPresent(FrameSampler* s, double presentAt, double now) {
    FrameSamplerIngestPresents(s, &presentAt, 1, now);
}

// sampler_of: a sampler holding one frame per entry in `millis`.
static void SamplerOf(FrameSampler* s, const float* millis, int n) {
    Warm(s);
    FrameSamplerSetCapacity(s, kFpsCapacity);
    for (int i = 0; i < n; i++) {
        IngestDraw(s, FpsTiming(millis[i] / 1000.f));
    }
}

static void DropsOldestSamplesBeyondCapacity() {
    FrameSampler s;
    Warm(&s);
    FrameSamplerSetCapacity(&s, 2);

    for (int i = 0; i < 3; i++) {
        IngestDraw(&s, FpsTiming(0.005f + 0.001f * (float)i));
    }

    utassert(s.n == 2);
    utassertnear(s.samples[0].drawSecs, 0.006f);
    utassertnear(s.samples[1].drawSecs, 0.007f);
}

// Feeds `count` presents spaced `interval` apart and returns the rate.
static float MeasureFps(int count, double interval) {
    FrameSampler s;
    Warm(&s);
    FrameSamplerSetCapacity(&s, 120);
    for (int i = 0; i < count; i++) {
        double presented = interval * (double)i;
        IngestPresent(&s, presented, presented);
    }
    return FrameSamplerFps(&s);
}

static void FpsIsTakenFromWhenFramesWerePresentedNotWhenTheyWereRead() {
    FrameSampler s;
    Warm(&s);
    FrameSamplerSetCapacity(&s, 120);
    const double interval = 0.010;

    // A whole batch of presents read at once, long after the first of them:
    // what a HUD that draws only when the window does sees. Stamped with the
    // time they were read, 61 frames would collapse onto one instant and
    // report no rate at all; stamped with their own times they cover
    // 600ms => 100 fps.
    double presents[61];
    for (int i = 0; i < 61; i++) {
        presents[i] = interval * (double)i;
    }
    double readAt = 0.600;
    FrameSamplerIngestPresents(&s, presents, 61, readAt);
    utassert(fabsf(FrameSamplerFps(&s) - 100.f) < 0.5f);
    utassert(fabsf(FrameSamplerPresentInterval(&s) * 1000.f - 10.f) < 0.1f);

    // Read again a second later with nothing new: everything has aged out of
    // the window, and the honest rate is zero.
    FrameSamplerIngestPresents(&s, nullptr, 0, readAt + 1.100);
    utassertnear(FrameSamplerFps(&s), 0.f);
    utassertnear(FrameSamplerPresentInterval(&s), 0.f);
}

static void FpsIsFramesDividedByTheSpanTheyCover() {
    // The rate is (n - 1) / span, not n / span: n frames delimit n - 1
    // intervals. Counting the frames would over-report by 1 / span, a whole
    // frame per second at these rates.
    //
    // 11 frames 10ms apart cover 100ms => 10 intervals => 100 fps.
    utassert(fabsf(MeasureFps(11, 0.010) - 100.f) < 0.5f);
    // The same span sampled more finely reports the same rate.
    utassert(fabsf(MeasureFps(101, 0.001) - 1000.f) < 5.f);
}

static void FpsMatchesTheCommonRefreshRates() {
    const double intervals[] = {16667e-6, 8333e-6, 33333e-6, 6944e-6};
    const float expected[] = {60.f, 120.f, 30.f, 144.f};
    for (int i = 0; i < 4; i++) {
        // A full second of frames at that interval.
        int count = (int)(1.0 / intervals[i]);
        utassert(fabsf(MeasureFps(count, intervals[i]) - expected[i]) < 1.f);
    }
}

static void FpsNeedsTwoFramesToHaveARateAtAll() {
    // One frame delimits no interval, so there is nothing to divide by and the
    // honest answer is zero rather than a guess.
    utassertnear(MeasureFps(0, 0.010), 0.f);
    utassertnear(MeasureFps(1, 0.010), 0.f);
    utassert(MeasureFps(2, 0.010) > 0.f);
}

static void SimultaneousFramesDoNotDivideByZero() {
    FrameSampler s;
    Warm(&s);
    FrameSamplerSetCapacity(&s, 64);

    // Three presents on one instant — what a trace with no clock behind it
    // would say — span nothing, and nothing is divided by that.
    const double now = 5.0;
    const double presents[] = {now, now, now};
    FrameSamplerIngestPresents(&s, presents, 3, now);

    utassertnear(FrameSamplerFps(&s), 0.f);
}

static void TheColdStartNeverReachesTheReadings() {
    FrameSampler s;
    FrameSamplerSetCapacity(&s, kFpsCapacity);
    float budget = 0.016667f;

    // What a window costs before any cache is warm, followed by what it costs
    // to run. Ingested straight, without the drain `tick` does, so only the
    // warm-up itself is under test.
    IngestDraw(&s, FpsTiming(0.100f));
    for (uint32_t i = 1; i < kFpsWarmupFrames; i++) {
        IngestDraw(&s, FpsTiming(0.040f));
    }
    for (int i = 0; i < 20; i++) {
        IngestDraw(&s, FpsTiming(0.005f));
    }

    utassertnear(FrameSamplerMeanDraw(&s), 0.005f);
    utassertnear(FrameSamplerPercentileDraw(&s, 0.95f), 0.005f);
    // A window that opened is not a window that is dropping frames.
    utassertnear(FrameSamplerOverBudget(&s, budget), 0.f);
}

// The first tick drains the backlog the window recorded before the HUD was
// mounted, and none of it counts either.
static void TheBacklogIsDroppedWithTheWarmUp() {
    FrameSampler s;
    FrameSamplerSetCapacity(&s, kFpsCapacity);
    FrameTiming frames[4] = {};
    for (int i = 0; i < 4; i++) {
        frames[i].drawSecs = 0.100f;
        frames[i].invalidations = 1;
        frames[i].presentAt = -1;
    }
    FrameSamplerIngest(&s, frames, 4, 1.0);
    utassert(s.drainedBacklog);
    utassert(s.n == 0);
    // The eight warm-up frames follow the backlog; the ninth is the first
    // that counts.
    for (int i = 0; i < 8; i++) {
        IngestDraw(&s, FpsTiming(0.040f));
    }
    utassert(s.n == 0);
    IngestDraw(&s, FpsTiming(0.005f));
    utassert(s.n == 1);
    utassertnear(FrameSamplerMeanDraw(&s), 0.005f);
}

// monitor.rs: the_headline_rate_is_what_a_frame_costs_and_the_panel_allows.
static void TheHeadlineRateIsWhatAFrameCostsAndThePanelAllows() {
    double sixty = 0.016667;
    // A cheap frame on a 60Hz panel is not 333 frames anyone could see.
    utassert(fabsf(FpsSustainableRate(0.003f, sixty) - 60.f) < 0.01f);
    // A frame that costs more than a refresh sets the rate itself.
    utassertnear(FpsSustainableRate(0.020f, sixty), 50.f);
    // Where the platform will not say, an uncapped reading beats a guess.
    utassert(fabsf(FpsSustainableRate(0.003f, 0) - 333.33f) < 0.1f);
    // No frames drawn yet is no rate, not an infinite one.
    utassertnear(FpsSustainableRate(0.f, sixty), 0.f);
}

static void AnEarlyTimerWakeDoesNotProduceAnAnimationFrame() {
    App app;
    Window win;
    win.app = &app;
    win.active = false;
    win.animFrame = true;
    // TimeNow starts its process-relative clock on first use, so put the last
    // draw safely beyond that zero while still representing an early wake.
    win.lastDrawTime = TimeNow() + 1.0;

    // Platform timers may wake early, and another timer may be due before the
    // inactive animation deadline. The tick must re-arm the remaining wait,
    // not turn that unrelated wake into an extra frame.
    WindowTimerTick(&win);
    utassert(win.invalidations == 0);
}

static void ThePercentileIsTheFrameAtTheNearestRank() {
    // Twenty frames, so the 95th percentile is rank 0.95 * 19 = 18.05, which
    // rounds to the second slowest.
    float draws[20];
    for (int i = 0; i < 20; i++) {
        draws[i] = (float)(20 - i);
    }
    FrameSampler s;
    Warm(&s);
    SamplerOf(&s, draws, 20);

    utassertnear(FrameSamplerPercentileDraw(&s, 0.95f), 0.019f);
    utassertnear(FrameSamplerPercentileDraw(&s, 1.f), 0.020f);
    utassertnear(FrameSamplerPercentileDraw(&s, 0.f), 0.001f);
}

static void ThePercentileSeparatesAStutterTheMeanAbsorbs() {
    // Eighteen quick frames and two that took twenty times as long: the shape
    // a stutter has. The mean stays inside a 60Hz budget while the tail is
    // well past it, which is the whole reason the row exists.
    float draws[20];
    for (int i = 0; i < 18; i++) {
        draws[i] = 4.f;
    }
    draws[18] = 80.f;
    draws[19] = 80.f;
    FrameSampler s;
    Warm(&s);
    SamplerOf(&s, draws, 20);

    utassert(FrameSamplerMeanDraw(&s) < 0.012f);
    utassertnear(FrameSamplerPercentileDraw(&s, 0.95f), 0.080f);
}

static void OneSlowFrameInTwentyDoesNotMoveThePercentile() {
    // The complement of the test above, and the reason a percentile is worth
    // having over the peak: a single frame is the top 5% of twenty, so it
    // stays out of the 95th. The chart still shows it, and the axis is still
    // scaled to it — the row is for the tail that *persists*, not for every
    // outlier.
    float draws[20];
    for (int i = 0; i < 19; i++) {
        draws[i] = 4.f;
    }
    draws[19] = 80.f;
    FrameSampler s;
    Warm(&s);
    SamplerOf(&s, draws, 20);

    utassertnear(FrameSamplerPercentileDraw(&s, 0.95f), 0.004f);
    utassertnear(FrameSamplerPeakDraw(&s), 0.080f);
}

static void AnEmptySamplerHasNoPercentileRatherThanAGuess() {
    FrameSampler s;
    Warm(&s);
    FrameSamplerSetCapacity(&s, 8);
    utassertnear(FrameSamplerPercentileDraw(&s, 0.95f), 0.f);
    utassertnear(FrameSamplerMeanInvalidations(&s), 0.f);
}

static void InvalidationsAverageOverTheRetainedFrames() {
    FrameSampler s;
    Warm(&s);
    FrameSamplerSetCapacity(&s, 8);

    // A window asked to redraw five times for every three frames it drew.
    const uint64_t invalidations[] = {1, 3, 1};
    for (int i = 0; i < 3; i++) {
        IngestDraw(&s, Coalesced(0.004f, invalidations[i]));
    }

    utassert(fabsf(FrameSamplerMeanInvalidations(&s) - 5.f / 3.f) < 1e-6f);
}

static void FramesOutsideTheRollingWindowStopCounting() {
    FrameSampler s;
    Warm(&s);
    FrameSamplerSetCapacity(&s, 64);

    for (int i = 0; i < 10; i++) {
        double presented = 0.010 * (double)i;
        IngestDraw(&s, FpsTiming(0.004f));
        IngestPresent(&s, presented, presented);
    }
    utassert(FrameSamplerFps(&s) > 0.f);

    // Two seconds later the window has gone idle: every retained frame is
    // older than the rolling window, so the rate collapses to zero.
    FrameSamplerIngestPresents(&s, nullptr, 0, 2.0);
    utassertnear(FrameSamplerFps(&s), 0.f);
    // The chart history survives, so the last known shape stays on screen.
    utassert(s.n == 10);
}

// Ours: the three readings the tick publishes from the retained draws, plus
// the ingest seam over the window's own frame records, where a frame that was
// drawn but not presented counts as a draw and not as a present.
static void MeanAndPeakAndOverBudget() {
    FrameSampler s;
    Warm(&s);
    FrameSamplerSetCapacity(&s, 64);
    utassertnear(FrameSamplerMeanDraw(&s), 0.f);
    utassertnear(FrameSamplerPeakDraw(&s), 0.f);
    utassertnear(FrameSamplerOverBudget(&s, 0.016f), 0.f);

    FrameTiming frames[4] = {};
    const float draws[] = {0.004f, 0.008f, 0.030f, 0.002f};
    for (int i = 0; i < 4; i++) {
        frames[i].drawSecs = draws[i];
        frames[i].invalidations = 1;
        frames[i].presentAt = 0.010 * (double)i;
    }
    // The scene found the third frame identical to the one before it: drawn,
    // never presented.
    frames[2].presentAt = -1;
    FrameSamplerIngest(&s, frames, 4, 0.030);
    utassertnear(FrameSamplerMeanDraw(&s), 0.011f);
    utassertnear(FrameSamplerPeakDraw(&s), 0.030f);
    utassertnear(FrameSamplerOverBudget(&s, 0.016f), 0.25f);
    utassert(s.n == 4);
    utassert(s.nPresents == 3);
}

// ─── resource history ─────────────────────────────────────────────────────

static ResourceSample Resources(float cpuPercent, uint64_t memoryBytes,
                                float gpuPercent) {
    ResourceSample s;
    s.cpuPercent = cpuPercent;
    s.memoryBytes = memoryBytes;
    s.gpuPercent = gpuPercent;
    return s;
}

static const uint64_t kHundredMib = 100ull * 1024 * 1024;

static void ResourceReadingsAverageOverTheWindow() {
    ResourceHistory history;
    history.windowSecs = 3.f;

    // Four readings a second apart, all of them still inside a three second
    // window: the oldest is exactly the window's age, and the window is
    // inclusive of it.
    const float cpus[] = {400.f, 100.f, 200.f, 300.f};
    for (int second = 0; second < 4; second++) {
        ResourceHistoryPush(
            &history, Resources(cpus[second], kHundredMib, cpus[second] / 10.f),
            (double)second);
    }

    ResourceSample mean;
    utassert(ResourceHistoryMean(&history, &mean));
    utassert(fabsf(mean.cpuPercent - 250.f) < 0.01f);
    utassert(mean.gpuPercent >= 0 && fabsf(mean.gpuPercent - 25.f) < 0.01f);
    utassert(mean.memoryBytes == kHundredMib);

    // A fifth a second later pushes the first out, so the reading that was
    // four times the others stops weighing on the mean.
    ResourceHistoryPush(&history, Resources(100.f, kHundredMib, 10.f), 4.0);
    utassert(ResourceHistoryMean(&history, &mean));
    utassert(fabsf(mean.cpuPercent - 175.f) < 0.01f);
}

// The averaged CPU stays on the single core scale rather than being folded
// back into 0..=100: a process holding two cores busy reads 200 whether it is
// averaged or not.
static void AveragingDoesNotCapTheCpuReading() {
    ResourceHistory history;
    const double now = 7.0;
    ResourceHistoryPush(&history, Resources(150.f, 0, -1.f), now);
    ResourceHistoryPush(&history, Resources(250.f, 0, -1.f), now);

    ResourceSample mean;
    utassert(ResourceHistoryMean(&history, &mean));
    utassert(fabsf(mean.cpuPercent - 200.f) < 0.01f);
}

static void AGapInTheGpuCounterDoesNotReadAsADip() {
    ResourceHistory history;
    const double now = 7.0;
    // The middle reading missed the counter. Averaging it in as a zero would
    // report 40%; the honest mean is over the two that carried one.
    ResourceHistoryPush(&history, Resources(0.f, 0, 60.f), now);
    ResourceHistoryPush(&history, Resources(0.f, 0, -1.f), now);
    ResourceHistoryPush(&history, Resources(0.f, 0, 60.f), now);

    ResourceSample mean;
    utassert(ResourceHistoryMean(&history, &mean));
    utassertnear(mean.gpuPercent, 60.f);
}

static void APlatformWithNoGpuCounterStaysWithoutOne() {
    ResourceHistory history;
    ResourceSample mean;
    // Nothing has been sampled yet.
    utassert(!ResourceHistoryMean(&history, &mean));

    ResourceHistoryPush(&history, Resources(12.f, 0, -1.f), 1.0);
    utassert(ResourceHistoryMean(&history, &mean));
    utassert(mean.gpuPercent < 0);
}

// ─── monitor ──────────────────────────────────────────────────────────────

static void FormatsMemoryByMagnitude() {
    utassert(StrEq(FpsFormatBytesTemp(184ull * 1024 * 1024), StrL("184 MB")));
    utassert(
        StrEq(FpsFormatBytesTemp(3ull * 1024 * 1024 * 1024), StrL("3.00 GB")));
}

// The reading is on the single core scale, so it passes 100 and keeps going —
// the row must show that rather than round it away or clip it.
static void FormatsCpuOnTheSingleCoreScale() {
    // A process spread over a core and a half, which under a scale where 100
    // is the whole machine would have read 5.8% on a 24 core desktop.
    utassert(StrEq(FpsFormatCpuTemp(140.f), StrL("140%")));
    // Saturating every core of a big machine still has somewhere to go.
    utassert(StrEq(FpsFormatCpuTemp(2400.f), StrL("2400%")));
    // Small readings keep the tenth that distinguishes them.
    utassert(StrEq(FpsFormatCpuTemp(0.4f), StrL("0.4%")));
    utassert(StrEq(FpsFormatCpuTemp(9.9f), StrL("9.9%")));
    utassert(StrEq(FpsFormatCpuTemp(12.4f), StrL("12%")));
}

// ─── memory (crates/fps/src/memory.rs) ────────────────────────────────────

// How much anonymous memory the reading has to move by to prove its unit.
static const size_t kBallast = 64u * 1024 * 1024;

// Whether a probe exists is the platform's answer; what must hold is that one
// which does exist reports *bytes*, since the counters behind it are published
// in different units — kibibytes on Linux, bytes through the other two — and
// the conversion is the one thing every backend has to get right.
//
// Asserted as growth rather than as an absolute floor: what a process happens
// to own says nothing. Allocating a known amount and watching the reading
// follow is what separates the units — a backend reporting unconverted
// kibibytes would move by a thousandth of the ballast, and one reporting pages
// by a four-thousandth.
static void AReadingFollowsWhatTheProcessAllocatesInBytes() {
    uint64_t before = 0;
    if (!SysSelfPrivateMemory(&before)) {
        return;
    }

    // Touched a page at a time, because two of the three counters move only
    // once the pages are faulted in rather than when they are reserved.
    auto* ballast = (volatile uint8_t*)malloc(kBallast);
    utassert(ballast != nullptr);
    if (!ballast) {
        return;
    }
    for (size_t off = 0; off < kBallast; off += 4096) {
        ballast[off] = 1;
    }

    uint64_t after = 0;
    utassert(SysSelfPrivateMemory(&after));
    // Half the ballast, not all of it: this only has to be the right order of
    // magnitude to settle the unit, and the process is free to release other
    // memory between the two readings.
    utassert(after > before && after - before > (uint64_t)kBallast / 2);
    // Read back across the reading, so nothing can free or elide it first.
    utassert(ballast[0] == 1);
    free((void*)ballast);
}

#if GPUI_OS_LINUX
// The whole point of the counter: it leaves out the file pages that make the
// resident set say more about the machine's graphics stack than about the
// application. On Linux this is an identity — RssAnon is one of the two halves
// VmRSS is the sum of — so it can be asserted rather than merely expected.
// PlatSelfUsage's memory is that resident set, out of /proc/self/statm.
static void AnonymousMemoryIsAPartOfTheResidentSet() {
    uint64_t anonymous = 0;
    if (!SysSelfPrivateMemory(&anonymous)) {
        return;
    }
    uint64_t cpu = 0;
    uint64_t resident = 0;
    utassert(PlatSelfUsage(&cpu, &resident));
    utassert(anonymous > 0);
    utassert(anonymous <= resident);
}
#endif

void TestFrameSampler() {
    TestSuite("fps/sampler");
    DropsOldestSamplesBeyondCapacity();
    FpsIsTakenFromWhenFramesWerePresentedNotWhenTheyWereRead();
    FpsIsFramesDividedByTheSpanTheyCover();
    FpsMatchesTheCommonRefreshRates();
    FpsNeedsTwoFramesToHaveARateAtAll();
    SimultaneousFramesDoNotDivideByZero();
    TheColdStartNeverReachesTheReadings();
    TheBacklogIsDroppedWithTheWarmUp();
    TheHeadlineRateIsWhatAFrameCostsAndThePanelAllows();
    AnEarlyTimerWakeDoesNotProduceAnAnimationFrame();
    ThePercentileIsTheFrameAtTheNearestRank();
    ThePercentileSeparatesAStutterTheMeanAbsorbs();
    OneSlowFrameInTwentyDoesNotMoveThePercentile();
    AnEmptySamplerHasNoPercentileRatherThanAGuess();
    InvalidationsAverageOverTheRetainedFrames();
    FramesOutsideTheRollingWindowStopCounting();
    MeanAndPeakAndOverBudget();

    TestSuite("fps/sampler/resource_history");
    ResourceReadingsAverageOverTheWindow();
    AveragingDoesNotCapTheCpuReading();
    AGapInTheGpuCounterDoesNotReadAsADip();
    APlatformWithNoGpuCounterStaysWithoutOne();

    TestSuite("fps/monitor");
    FormatsMemoryByMagnitude();
    FormatsCpuOnTheSingleCoreScale();

    TestSuite("fps/gpu");
    // crates/fps/src/gpu.rs: a_reading_is_a_percentage. Whether a probe
    // exists at all is the platform's answer, and a headless CI machine may
    // well have no accelerator to report; what must hold is that a probe
    // which does exist reports a percentage rather than a raw counter or a
    // fraction. The first sample is the baseline and answers nothing.
    if (GpuAvailable()) {
        (void)GpuUsagePercent();
        float percent = GpuUsagePercent();
        if (percent >= 0) {
            utassert(percent <= 100.f);
        }
    }

    TestSuite("fps/memory");
    AReadingFollowsWhatTheProcessAllocatesInBytes();
#if GPUI_OS_LINUX
    AnonymousMemoryIsAPartOfTheResidentSet();
#endif
}
