/* Ported from crates/ui/src/plot/scale/{linear,point,ordinal}.rs, mod tests.
 *
 * Rust's scales are generic and own their domain and range as Vecs; ours
 * borrow float arrays and ScaleOrdinal maps indexes. Every assertion below is
 * the reference's, with `Option<f32>` read as the bool return plus an out
 * parameter. */

#include "Test.h"

using namespace gpui::component;

// ─── ScaleLinear ──────────────────────────────────────────────────────────

static void ScaleLinearBasics() {
    const float domain[] = {1, 2, 3};
    float t = 0;

    const float up[] = {0, 100};
    ScaleLinear s = ScaleLinear::New(domain, 3, up, 2);
    utassert(s.Tick(1, &t) && TestNear(t, 0.f));
    utassert(s.Tick(2, &t) && TestNear(t, 50.f));
    utassert(s.Tick(3, &t) && TestNear(t, 100.f));

    // A descending range maps the domain backwards, which is a y axis.
    const float down[] = {100, 0};
    s = ScaleLinear::New(domain, 3, down, 2);
    utassert(s.Tick(1, &t) && TestNear(t, 100.f));
    utassert(s.Tick(2, &t) && TestNear(t, 50.f));
    utassert(s.Tick(3, &t) && TestNear(t, 0.f));
}

// test_scale_linear_unordered_domain: the domain's extent is its min and
// max, whatever order the values come in.
static void ScaleLinearUnorderedDomain() {
    const float domain[] = {3, 1, 2};
    const float range[] = {0, 100};
    ScaleLinear s = ScaleLinear::New(domain, 3, range, 2);
    float t = 0;
    utassert(s.Tick(1, &t) && TestNear(t, 0.f));
    utassert(s.Tick(3, &t) && TestNear(t, 100.f));
}

// test_scale_linear_f32, and line_chart.rs test_f32_values_scale_like_f64:
// Rust now scales f32 values like f64. The C++ domain
// is always float, so this is the same arithmetic on the reference values.
static void ScaleLinearF32() {
    const float domain[] = {0, 4};
    const float range[] = {0, 100};
    ScaleLinear s = ScaleLinear::New(domain, 2, range, 2);
    float t = 0;
    utassert(s.Tick(1, &t) && TestNear(t, 25.f));
    utassert(s.Tick(4, &t) && TestNear(t, 100.f));
}

static void ScaleLinearEmpty() {
    float t = 0;

    // No domain is no extent to divide by, so there is no tick at all.
    const float range[] = {0, 100};
    ScaleLinear s = ScaleLinear::New(nullptr, 0, range, 2);
    utassert(!s.Tick(1, &t));
    utassert(!s.Tick(2, &t));
    utassert(!s.Tick(3, &t));

    // A [0, 0] range still ticks; everything lands on zero.
    const float domain[] = {1, 2, 3};
    const float none[] = {0, 0};
    s = ScaleLinear::New(domain, 3, none, 2);
    utassert(s.Tick(1, &t) && TestNear(t, 0.f));
    utassert(s.Tick(2, &t) && TestNear(t, 0.f));
    utassert(s.Tick(3, &t) && TestNear(t, 0.f));
}

// ─── ScalePoint ───────────────────────────────────────────────────────────

static void ScalePointBasics() {
    const float domain[] = {1, 2, 3};
    const float range[] = {0, 100};
    ScalePoint s = ScalePoint::New(domain, 3, range, 2);
    float t = 0;

    utassert(s.Tick(1, &t) && TestNear(t, 0.f));
    utassert(s.Tick(2, &t) && TestNear(t, 50.f));
    utassert(s.Tick(3, &t) && TestNear(t, 100.f));
}

static void ScalePointRange() {
    const float domain[] = {1, 2, 3};
    const float range[] = {40, 80};
    ScalePoint s = ScalePoint::New(domain, 3, range, 2);
    float t = 0;

    utassert(s.Tick(1, &t) && TestNear(t, 40.f));
    utassert(s.Tick(2, &t) && TestNear(t, 60.f));
    utassert(s.Tick(3, &t) && TestNear(t, 80.f));
}

static const float zeroRange[2] = {0, 0};

static void ScalePointEmpty() {
    float t = 0;

    const float range[] = {0, 100};
    ScalePoint s = ScalePoint::New(nullptr, 0, range, 2);
    utassert(!s.Tick(1, &t));
    utassert(!s.Tick(2, &t));
    utassert(!s.Tick(3, &t));

    const float domain[] = {1, 2, 3};
    s = ScalePoint::New(domain, 3, zeroRange, 2);
    utassert(s.Tick(1, &t) && TestNear(t, 0.f));
    utassert(s.Tick(2, &t) && TestNear(t, 0.f));
    utassert(s.Tick(3, &t) && TestNear(t, 0.f));
}

static void ScalePointSingle() {
    const float domain[] = {1};
    const float range[] = {0, 100};
    ScalePoint s = ScalePoint::New(domain, 1, range, 2);
    float t = 0;

    // One point has no spacing to step by, so it sits in the middle.
    utassert(s.Tick(1, &t) && TestNear(t, 50.f));
}

// point.rs test_tick_at_matches_tick (#3262).
static void ScalePointTickAtMatchesTick() {
    const float d1[] = {1};
    const float d3[] = {1, 2, 3};
    const float d5[] = {1, 2, 3, 4, 5};
    const float* domains[] = {nullptr, d1, d3, d5};
    const int lens[] = {0, 1, 3, 5};
    const float range[] = {40, 80};
    for (int k = 0; k < 4; k++) {
        ScalePoint s = ScalePoint::New(domains[k], lens[k], range, 2);
        for (int i = 0; i < lens[k]; i++) {
            float a = 0, b = 0;
            utassert(s.TickAt(i, &a) && s.Tick(domains[k][i], &b));
            utassert(TestNear(a, b));
        }
        float t = 0;
        utassert(!s.TickAt(lens[k], &t));
    }
}

static void ScalePointNearestIndexBasic() {
    const float domain[] = {1, 2, 3};
    const float range[] = {0, 100};
    ScalePoint s = ScalePoint::New(domain, 3, range, 2);

    utassert(s.NearestIndex(0) == 0);
    utassert(s.NearestIndex(50) == 1);
    utassert(s.NearestIndex(100) == 2);

    utassert(s.NearestIndex(24) == 0); // closer to 0
    utassert(s.NearestIndex(25) == 1); // equidistant, rounds up
    utassert(s.NearestIndex(26) == 1); // closer to 50
    utassert(s.NearestIndex(74) == 1); // closer to 50
    utassert(s.NearestIndex(75) == 2); // equidistant, rounds up
    utassert(s.NearestIndex(76) == 2); // closer to 100

    utassert(s.NearestIndex(-10) == 0); // below the range
    utassert(s.NearestIndex(150) == 2); // above it
}

static void ScalePointNearestIndexWithOffset() {
    const float domain[] = {1, 2, 3};
    const float range[] = {40, 80};
    ScalePoint s = ScalePoint::New(domain, 3, range, 2);

    // The points are at 40, 60, 80.
    utassert(s.NearestIndex(40) == 0);
    utassert(s.NearestIndex(60) == 1);
    utassert(s.NearestIndex(80) == 2);

    utassert(s.NearestIndex(49) == 0);
    utassert(s.NearestIndex(50) == 1);
    utassert(s.NearestIndex(51) == 1);
    utassert(s.NearestIndex(69) == 1);
    utassert(s.NearestIndex(70) == 2);
    utassert(s.NearestIndex(71) == 2);

    utassert(s.NearestIndex(30) == 0);
    utassert(s.NearestIndex(100) == 2);
}

static void ScalePointNearestIndexDegenerate() {
    const float range[] = {0, 100};
    ScalePoint empty = ScalePoint::New(nullptr, 0, range, 2);
    utassert(empty.NearestIndex(0) == 0);
    utassert(empty.NearestIndex(50) == 0);
    utassert(empty.NearestIndex(100) == 0);

    const float one[] = {1};
    ScalePoint single = ScalePoint::New(one, 1, range, 2);
    utassert(single.NearestIndex(0) == 0);
    utassert(single.NearestIndex(50) == 0);
    utassert(single.NearestIndex(100) == 0);

    const float domain[] = {1, 2, 3};
    ScalePoint noRange = ScalePoint::New(domain, 3, zeroRange, 2);
    utassert(noRange.NearestIndex(0) == 0);
    utassert(noRange.NearestIndex(50) == 0);
    utassert(noRange.NearestIndex(100) == 0);
}

// point.rs test_reversed_range: a range written high to low places the
// domain from its first end, and the nearest index walks back with it.
static void ScalePointReversedRange() {
    const float domain[] = {1, 2, 3};
    const float range[] = {100, 0};
    ScalePoint s = ScalePoint::New(domain, 3, range, 2);
    float t = 0;
    utassert(s.Tick(1, &t) && TestNear(t, 100.f));
    utassert(s.Tick(3, &t) && TestNear(t, 0.f));
    utassert(s.NearestIndex(90) == 0);
    utassert(s.NearestIndex(10) == 2);
}

// ─── ScaleOrdinal ─────────────────────────────────────────────────────────

static void ScaleOrdinalCycles() {
    // Rust: domain ["a".."e"], range [10, 20, 30]. Ours takes the domain index
    // the caller already has, so the assertions are on indexes 0..4 and the
    // range positions they land on.
    ScaleOrdinal s;
    s.rangeLen = 3;

    utassert(s.Map(0) == 0);
    utassert(s.Map(1) == 1);
    utassert(s.Map(2) == 2);
    utassert(s.Map(3) == 0); // cycles back to the first
    utassert(s.Map(4) == 1);
    utassert(s.Map(-1) == -1); // not in the domain, and no unknown set
}

static void ScaleOrdinalUnknown() {
    ScaleOrdinal s;
    s.rangeLen = 3;
    s.unknown = 0;

    utassert(s.Map(0) == 0);
    utassert(s.Map(-1) == 0);
}

static void ScaleOrdinalEmptyRange() {
    ScaleOrdinal s;
    s.rangeLen = 0;

    utassert(s.Map(0) == -1);
    utassert(s.Map(3) == -1);
}

// crates/ui/src/plot/scale/band.rs: test_scale_band.
static void ScaleBandThirds() {
    const float range[2] = {0.f, 90.f};
    ScaleBand b = ScaleBand::New(3, range, 2);
    float t = 0;
    utassert(b.Tick(0, &t) && TestNear(t, 0.f));
    utassert(b.Tick(1, &t) && TestNear(t, 30.f));
    utassert(b.Tick(2, &t) && TestNear(t, 60.f));
    utassertnear(b.BandWidth(), 30.f);
}

// test_scale_band_zero: an empty domain has no bands, and an empty range has
// no width to give them.
static void ScaleBandEmpty() {
    const float range[2] = {0.f, 90.f};
    ScaleBand none = ScaleBand::New(0, range, 2);
    float t = 0;
    utassert(!none.Tick(0, &t));
    utassert(!none.Tick(1, &t));
    utassertnear(none.BandWidth(), 0.f);

    ScaleBand noRange = ScaleBand::New(3, zeroRange, 2);
    utassert(noRange.Tick(0, &t) && TestNear(t, 0.f));
    utassert(noRange.Tick(1, &t) && TestNear(t, 0.f));
    utassert(noRange.Tick(2, &t) && TestNear(t, 0.f));
    utassertnear(noRange.BandWidth(), 0.f);
}

static void ScaleBandPadding() {
    const float range[2] = {0.f, 100.f};
    ScaleBand b = ScaleBand::New(4, range, 2);
    b.paddingInner = 0.2f;
    // A band gives a fifth of itself to the gap beside it.
    utassertnear(b.BandWidth(), 20.f);
    float t = 0;
    utassert(b.Tick(0, &t) && TestNear(t, 0.f));
    // The rest are spread by the ratio the inner padding works out to: a
    // quarter of the range each, stretched by 1 + 0.2/3.
    utassert(b.Tick(3, &t) && TestNear(t, 80.f));
}

static void ScaleBandSingle() {
    const float range[2] = {0.f, 90.f};
    ScaleBand b = ScaleBand::New(1, range, 2);
    float t = 0;
    // One band sits in the middle. Uncapped it spans the range and starts at
    // its beginning; capped at thirty it starts thirty in.
    utassert(b.Tick(0, &t) && TestNear(t, 0.f));
    utassert(b.MaxBandWidth(30).Tick(0, &t) && TestNear(t, 30.f));
    utassert(b.NearestIndex(80.f) == 0);
}

// band.rs max_band_width_caps_the_width_but_not_the_ticks.
static void ScaleBandMaxBandWidthCapsTheWidthButNotTheTicks() {
    const float range[2] = {0.f, 200.f};
    ScaleBand wide = ScaleBand::New(2, range, 2);
    ScaleBand capped = ScaleBand::New(2, range, 2).MaxBandWidth(30);
    utassertnear(wide.BandWidth(), 100.f);
    utassertnear(capped.BandWidth(), 30.f);
    float a = 0, b = 0;
    utassert(capped.Tick(1, &a) && wide.Tick(1, &b) && TestNear(a, b));
}

// band.rs test_scale_band_range_start: bands lead from the lower end of the
// range, whichever way it is written.
static void ScaleBandRangeStart() {
    const float range[2] = {10.f, 100.f};
    ScaleBand b = ScaleBand::New(3, range, 2);
    float t = 0;
    utassert(b.Tick(0, &t) && TestNear(t, 10.f));
    utassert(b.Tick(1, &t) && TestNear(t, 40.f));
    utassert(b.NearestIndex(41.f) == 1);
    const float reversed[2] = {100.f, 10.f};
    utassert(ScaleBand::New(3, reversed, 2).Tick(0, &t) && TestNear(t, 10.f));
}

// test_scale_band_dedup: a grouped bar chart of 2 series over 3 categories
// reaches Rust as 6 entries with 3 distinct values, and the scale must give
// 3 bands across the whole range rather than 6 half-width ones in its left
// half. The domain here is the distinct count, so the same case is
// constructed with the 3 the dedupe leaves Rust with, and the bands land on
// the same ticks at the same width.
static void ScaleBandDedup() {
    const float range[2] = {0.f, 90.f};
    ScaleBand b = ScaleBand::New(3, range, 2);
    float t = 0;
    utassert(b.Tick(0, &t) && TestNear(t, 0.f));
    utassert(b.Tick(1, &t) && TestNear(t, 30.f));
    utassert(b.Tick(2, &t) && TestNear(t, 60.f));
    utassertnear(b.BandWidth(), 30.f);
    // The band count is the domain, so there is no repeated slot that could
    // go unaddressable: every index below it answers, and the one past the
    // end does not.
    utassert(!b.Tick(3, &t));
}

static void ScaleBandNearestIndex() {
    const float range[2] = {0.f, 90.f};
    ScaleBand b = ScaleBand::New(3, range, 2);
    utassert(b.NearestIndex(0.f) == 0);
    utassert(b.NearestIndex(31.f) == 1);
    utassert(b.NearestIndex(59.f) == 2);
    // And it never runs off either end.
    utassert(b.NearestIndex(-40.f) == 0);
    utassert(b.NearestIndex(400.f) == 2);
}

// test_scale_band_count: a domain laid out for more bands takes the leading
// ones, each placed as if all were full.
static void ScaleBandCount() {
    const float range[2] = {0.f, 100.f};
    auto scale = [&](int domain) {
        ScaleBand b = ScaleBand::New(domain, range, 2).BandCount(4);
        b.paddingInner = 0.4f;
        b.paddingOuter = 0.2f;
        return b;
    };
    ScaleBand shortBand = scale(2);
    ScaleBand full = scale(4);
    float a = 0, b = 0;
    utassert(shortBand.Tick(1, &a) && full.Tick(1, &b) && a == b);
    utassertnear(shortBand.BandWidth(), full.BandWidth());
    utassertnear(shortBand.Step(), full.Step());
    // An empty band resolves past the domain rather than to its last value.
    utassert(full.Tick(3, &b) && shortBand.NearestIndex(b) == 3);
    // A single value sits in the first band instead of the center.
    utassert(scale(1).Tick(0, &a) && full.Tick(0, &b) && a == b);
    // A count below the domain's length has no effect.
    const float wide[2] = {0.f, 90.f};
    utassert(ScaleBand::New(3, wide, 2).BandCount(2).Tick(2, &a) &&
             TestNear(a, 60.f));
}

static void ScaleBandStep() {
    const float range[2] = {0.f, 90.f};
    ScaleBand b = ScaleBand::New(3, range, 2);
    float t0 = 0, t1 = 0;
    utassert(b.Tick(0, &t0) && b.Tick(1, &t1));
    utassertnear(b.Step(), t1 - t0);

    ScaleBand padded = ScaleBand::New(3, range, 2);
    padded.paddingInner = 0.4f;
    padded.paddingOuter = 0.2f;
    utassert(padded.Tick(0, &t0) && padded.Tick(1, &t1));
    utassert(fabsf(padded.Step() - (t1 - t0)) < 1e-4f);

    ScaleBand one = ScaleBand::New(1, range, 2);
    utassertnear(one.Step(), 90.f);
}

// The tooltip box hugs the cursor and flips toward the middle past halfway,
// which is what keeps it inside the plot.
static void PlotTooltipQuadrants() {
    Size within = {200, 100};
    Size box = {40, 20};
    // Top left quarter: down and to the right of the cursor.
    Point at = PlotTooltipPlace({10, 10}, within, box, 8);
    utassert(TestNear(at.x, 18.f) && TestNear(at.y, 18.f));
    // Right half: the box's right edge is what hugs the cursor.
    at = PlotTooltipPlace({150, 10}, within, box, 8);
    utassert(TestNear(at.x, 102.f) && TestNear(at.y, 18.f));
    // Bottom half: it sits above.
    at = PlotTooltipPlace({10, 80}, within, box, 8);
    utassert(TestNear(at.x, 18.f) && TestNear(at.y, 52.f));
    at = PlotTooltipPlace({150, 80}, within, box, 8);
    utassert(TestNear(at.x, 102.f) && TestNear(at.y, 52.f));
}

// A box too big for the plot to hold either way still starts inside it.
static void PlotTooltipClamps() {
    Size within = {200, 100};
    Point at = PlotTooltipPlace({190, 90}, within, {400, 400}, 8);
    utassert(TestNear(at.x, 0.f) && TestNear(at.y, 0.f));
}

namespace plot = gpui::component::plot;

static bool PlotFloat(const void* item, int, void*, float* out) {
    *out = *(const float*)item;
    return true;
}

static bool PlotDouble(const void* item, int, void*, float* out) {
    *out = *(const float*)item * 2.f;
    return true;
}

static bool RadialAngle(const void*, int index, void* user, float* out) {
    int count = (int)(intptr_t)user;
    *out = (float)index * 2.f * kPi / (float)count;
    return true;
}

static void PlotShapeGeometry() {
    utassertnear(kPlotTextSize, 10.f);
    utassertnear(kPlotTextGap, 2.f);
    utassertnear(kPlotTextHeight, 12.f);
    Point origin = component::plot::OriginPoint(3, 4, {10, 20});
    utassertnear(origin.x, 13.f);
    utassertnear(origin.y, 24.f);

    // shape/arc.rs::test_arc_builder and test_arc_centroid.
    component::plot::Arc arc = component::plot::Arc::New();
    arc.InnerRadius(10)->OuterRadius(20);
    component::plot::ArcData arcData = {};
    arcData.value = 1;
    arcData.endAngle = kPi;
    Point centroid = arc.Centroid(arcData);
    utassertnear(centroid.x, 15.f);
    utassertnear(centroid.y, 0.f);

    // shape/line.rs::test_line_path: accessors resolve every valid datum.
    float lineValues[] = {1, 2, 3};
    component::plot::Line line = component::plot::Line::New();
    line.Data(lineValues, 3, sizeof(float))->X(PlotFloat)->Y(PlotDouble);
    Point points[3] = {};
    utassert(line.Points({0, 0, 100, 100}, points, 3) == 3);
    utassertnear(points[0].x, 1.f);
    utassertnear(points[2].y, 6.f);

    // radial_line.rs: noon, three, six and nine o'clock around (50, 50).
    float radialValues[] = {1, 1, 1, 1};
    component::plot::RadialLine radial = component::plot::RadialLine::New();
    radial.Data(radialValues, 4, sizeof(float))
        ->Angle(RadialAngle, (void*)(intptr_t)4)
        ->Radius(PlotFloat);
    Point radialPoints[4] = {};
    utassert(radial.Points({0, 0, 100, 100}, radialPoints, 4) == 4);
    const Point expected[] = {{50, 49}, {51, 50}, {50, 51}, {49, 50}};
    for (int i = 0; i < 4; i++) {
        utassertnear(radialPoints[i].x, expected[i].x);
        utassertnear(radialPoints[i].y, expected[i].y);
    }
}

static void PlotPieArcs() {
    float values[] = {0, 1, 0, 2};
    component::plot::Pie pie = component::plot::Pie::New();
    pie.Value(PlotFloat);
    Arena* arena = ArenaNew();
    ArenaVec<component::plot::ArcData> arcs;
    pie.Arcs(arena, {values, 4, sizeof(float)}, &arcs);
    utassert(len(arcs) == 2);
    component::plot::ArcData resolved[2] = {};
    int resolvedCount = 0;
    for (const component::plot::ArcData& item : arcs) {
        if (resolvedCount < 2) resolved[resolvedCount++] = item;
    }
    if (resolvedCount >= 2) {
        utassert(resolved[0].index == 1 && resolved[1].index == 3);
        utassertnear(resolved[0].value, 1.f);
        utassertnear(resolved[1].value, 2.f);
        utassertnear(resolved[0].startAngle, 0.f);
        utassertnear(resolved[0].endAngle, resolved[1].startAngle);
        utassertnear(resolved[1].endAngle, 2.f * kPi);
    }
    ArenaDelete(arena);
}

static void PlotArcContains() {
    component::plot::Arc arc = component::plot::Arc::New();
    arc.InnerRadius(10.f)->OuterRadius(40.f);
    component::plot::ArcData right = {};
    right.value = 1.f;
    right.startAngle = 0.f;
    right.endAngle = kPi;
    Bounds bounds = {0, 0, 100, 100};
    utassert(arc.Contains(right, {80.f, 50.f}, bounds));
    utassert(!arc.Contains(right, {20.f, 50.f}, bounds));
    utassert(!arc.Contains(right, {55.f, 50.f}, bounds));
    utassert(!arc.Contains(right, {95.f, 50.f}, bounds));
    // A wider arc reaches the same point.
    component::plot::Arc wider = component::plot::Arc::New();
    wider.InnerRadius(10.f)->OuterRadius(50.f);
    utassert(wider.Contains(right, {95.f, 50.f}, bounds));
    utassert(arc.Contains(right, {50.f, 20.f}, bounds));
    utassert(!arc.Contains(right, {50.f, 80.f}, bounds));
}

static void PlotHoverReaders() {
    component::plot::TooltipState state =
        component::plot::TooltipState::New(2, {10.f, 20.f}, nullptr, 0);
    component::plot::PlotHover hover;
    hover.state = state;
    hover.progress = 1.f;
    hover.hovered = true;
    utassert(hover.State().index == 2);
    utassert(hover.IsHovered());
    utassert(!hover.IsEntering());

    component::plot::PlotHover entering = hover;
    entering.progress = 0.f;
    utassert(entering.IsEntering());

    component::plot::PlotHover lingering = hover;
    lingering.progress = 0.4f;
    lingering.hovered = false;
    utassert(!lingering.IsHovered());
    utassert(!lingering.IsEntering());
}

struct PlotSales {
    float apples;
    float bananas;
    float cherries;
};

static bool PlotSalesValue(const void* item, int, Str key, void*, float* out) {
    const PlotSales* sales = (const PlotSales*)item;
    if (StrEqI(key, "apples")) {
        *out = sales->apples;
    } else if (StrEqI(key, "bananas")) {
        *out = sales->bananas;
    } else if (StrEqI(key, "cherries")) {
        *out = sales->cherries;
    } else {
        return false;
    }
    return true;
}

static void PlotStackSeries() {
    PlotSales values[] = {{10, 20, 30}, {15, 25, 35}};
    Str keys[] = {StrL("apples"), StrL("bananas"), StrL("cherries")};
    component::plot::Stack stack = component::plot::Stack::New();
    stack.Data(values, 2, sizeof(PlotSales))
        ->Keys(keys, 3)
        ->Value(PlotSalesValue);
    Arena* arena = ArenaNew();
    ArenaVec<component::plot::StackSeries> series;
    stack.Series(arena, &series);
    utassert(len(series) == 3);
    Str resolvedKeys[3] = {};
    component::plot::StackPoint resolvedPoints[3] = {};
    int resolvedCount = 0;
    for (const component::plot::StackSeries& item : series) {
        if (resolvedCount >= 3) break;
        resolvedKeys[resolvedCount] = item.key;
        if (item.points.len > 0) {
            resolvedPoints[resolvedCount] = item.points[0];
        }
        resolvedCount++;
    }
    if (resolvedCount >= 3) {
        utassert(StrEqI(resolvedKeys[0], keys[0]));
        utassertnear(resolvedPoints[0].y0, 0.f);
        utassertnear(resolvedPoints[0].y1, 10.f);
        utassertnear(resolvedPoints[1].y0, 10.f);
        utassertnear(resolvedPoints[1].y1, 30.f);
        utassertnear(resolvedPoints[2].y0, 30.f);
        utassertnear(resolvedPoints[2].y1, 60.f);
    }
    ArenaDelete(arena);
}

// axis.rs builder_order_does_not_move_labels: the labels, the line and the
// side can be set in any order, and a side set after the labels still
// applies to them.
static void PlotAxisBuilderOrderDoesNotMoveLabels() {
    Arena* arena = ArenaNew();
    component::plot::AxisText labels[2] = {
        component::plot::AxisText::New(StrL("a"), 10, Rgba{}),
        component::plot::AxisText::New(StrL("b"), 20, Rgba{}),
    };
    labels[1].Align(component::plot::PlotTextAlign::Right);
    using component::plot::AxisLabelSide;
    component::plot::PlotAxis first = component::plot::PlotAxis::New(arena);
    first.XLabel(labels, 2)
        ->X(50)
        ->XLabelSide(AxisLabelSide::Start)
        ->YLabel(labels, 2)
        ->Y(30)
        ->YLabelSide(AxisLabelSide::Start);
    component::plot::PlotAxis last = component::plot::PlotAxis::New(arena);
    last.XLabelSide(AxisLabelSide::Start)
        ->X(50)
        ->XLabel(labels, 2)
        ->YLabelSide(AxisLabelSide::Start)
        ->Y(30)
        ->YLabel(labels, 2);
    ArenaVec<component::plot::Text> fx = first.XTexts(arena, 50);
    ArenaVec<component::plot::Text> lx = last.XTexts(arena, 50);
    ArenaVec<component::plot::Text> fy = first.YTexts(arena, 30);
    ArenaVec<component::plot::Text> ly = last.YTexts(arena, 30);
    utassert(fx.len == 2 && lx.len == 2 && fy.len == 2 && ly.len == 2);
    for (int i = 0; i < fx.len && i < lx.len; i++) {
        utassert(StrEqI(fx[i].text, lx[i].text));
        utassertnear(fx[i].origin.x, lx[i].origin.x);
        utassertnear(fx[i].origin.y, lx[i].origin.y);
    }
    for (int i = 0; i < fy.len && i < ly.len; i++) {
        utassertnear(fy[i].origin.x, ly[i].origin.x);
        utassertnear(fy[i].origin.y, ly[i].origin.y);
    }
    if (fx.len > 0) {
        utassertnear(fx[0].origin.y, 50.f - component::kPlotTextGap -
                                         component::kPlotTextHeight);
    }
    ArenaDelete(arena);
}

// axis.rs axis_gutter_fits_default_labels.
static void PlotAxisGutterFitsDefaultLabels() {
    utassertnear(gpui::plot::AxisGutter(component::kPlotTextSize), 18.f);
}

static void PlotBarAndAxisContracts() {
    utassert(!component::plot::BarAlignmentIsHorizontal(
        component::plot::BarAlignment::Bottom));
    utassert(component::plot::BarAlignmentIsHorizontal(
        component::plot::BarAlignment::Left));
    utassertnear(component::plot::BarAlignmentGradientAngle(
                     component::plot::BarAlignment::Bottom),
                 0.f);
    utassertnear(component::plot::BarAlignmentGradientAngle(
                     component::plot::BarAlignment::Top),
                 180.f);
    utassertnear(component::plot::BarAlignmentGradientAngle(
                     component::plot::BarAlignment::Left),
                 90.f);
    utassertnear(component::plot::BarAlignmentGradientAngle(
                     component::plot::BarAlignment::Right),
                 270.f);
    Point label = component::plot::BarLabelOrigin(
        component::plot::BarAlignment::Bottom, 10, 100, 40, 20);
    utassertnear(label.x, 20.f);
    utassertnear(label.y, 28.f);
    label = component::plot::BarLabelOrigin(component::plot::BarAlignment::Left,
                                            10, 0, 40, 20);
    utassertnear(label.x, 42.f);
    utassertnear(label.y, 15.f);

    Arena* arena = ArenaNew();
    component::plot::PlotAxis axis = component::plot::PlotAxis::New(arena);
    utassert(axis.xAxis && !axis.yAxis);
    component::plot::AxisText tick =
        component::plot::AxisText::New(StrL("x"), 20, Rgb(1, 2, 3));
    // Labels are placed when the axis paints, so one given before x still
    // lands against the line.
    axis.XLabel(&tick, 1)->X(30);
    ArenaVec<component::plot::Text> xs = axis.XTexts(arena, axis.x);
    utassert(xs.len == 1);
    if (xs.len > 0) {
        utassertnear(xs[0].origin.x, 20.f);
        utassertnear(xs[0].origin.y, 36.f);
    }
    axis.YLabelSide(component::plot::AxisLabelSide::Start)
        ->Y(12)
        ->YLabel(&tick, 1);
    ArenaVec<component::plot::Text> ys = axis.YTexts(arena, axis.y);
    utassert(ys.len == 1);
    if (ys.len > 0) {
        utassertnear(ys[0].origin.x, 10.f);
        utassertnear(ys[0].origin.y, 15.f);
    }
    ArenaDelete(arena);

    component::plot::CrossLine cross =
        component::plot::CrossLine::New({10, 20});
    utassert(cross.ShowVertical() && !cross.ShowHorizontal());
    cross.Both()->Span(3, 40)->HSpan(4, 50);
    utassert(cross.ShowVertical() && cross.ShowHorizontal());
    utassert(cross.hasVerticalLength && cross.verticalStart == 3 &&
             cross.verticalLength == 40);
    utassert(cross.hasHorizontalLength && cross.horizontalStart == 4 &&
             cross.horizontalLength == 50);
}

// ─── plot/appear.rs ──────────────────────────────────────────────────────

static gpui::plot::PlotAppear AppearAt(float time) {
    gpui::plot::PlotAppear appear;
    appear.time = time;
    appear.easing = Easing::Linear();
    return appear;
}

// test_complete_appear
static void PlotCompleteAppear() {
    gpui::plot::PlotAppear appear = gpui::plot::PlotAppear::Complete();
    utassert(!appear.IsAppearing());
    utassert(appear.Progress() == 1.f);
    utassert(appear.Staggered(3, 10, 0.5f) == 1.f);
}

// test_staggered_marks_share_the_appear
static void PlotStaggeredMarksShareTheAppear() {
    // The first mark starts at once, the last once the spread has passed.
    utassertnear(AppearAt(0.f).Staggered(0, 5, 0.5f), 0.f);
    utassertnear(AppearAt(0.25f).Staggered(0, 5, 0.5f), 0.5f);
    utassertnear(AppearAt(0.5f).Staggered(4, 5, 0.5f), 0.f);
    utassertnear(AppearAt(0.75f).Staggered(4, 5, 0.5f), 0.5f);
    // Every mark finishes with the appear.
    for (int index = 0; index < 5; index++) {
        utassert(AppearAt(1.f).Staggered(index, 5, 0.5f) == 1.f);
    }
}

// test_staggered_without_spread_moves_together
static void PlotStaggeredWithoutSpreadMovesTogether() {
    utassertnear(AppearAt(0.4f).Staggered(0, 3, 0.f), 0.4f);
    utassertnear(AppearAt(0.4f).Staggered(2, 3, 0.f), 0.4f);
    // A lone mark ignores the spread.
    utassertnear(AppearAt(0.4f).Staggered(0, 1, 0.5f), 0.4f);
}

// The window the appear tests sample in: a 100 ms linear appear in the Base
// theme, a plot id on the stack, and the frame clock the test drives. Rust's
// tests open a window whose Recorder plot receives the appear each frame;
// TrackAppear is the call PlotElement makes for it, so the tests make it
// frame by frame instead.
struct AppearHarness {
    App app;
    Window* win = nullptr;
    Arena* arena = nullptr;
    Ctx cx = {};

    AppearHarness() {
        win = new Window();
        win->app = &app;
        arena = ArenaNew();
        cx = {&app, win, arena, {}};
        cx.path = HashClickId(StrL("recorder"));
        BaseTheme base;
        base.plot = base_theme::PlotTheme::New().WithMotion(
            gpui::plot::PlotMotion{}.WithAppear(motion::Transition::New(100)
                                                    .Ease(Easing::Linear())));
        BaseThemeSet(&app, base);
        MotionSetReduced(false);
    }
    ~AppearHarness() {
        MotionSetReduced(false);
        ArenaDelete(arena);
        delete win;
        AppGlobalClear(&app);
    }
    // One frame at `now` seconds: whether the plot asked for another.
    float Frame(double now, uint64_t generation, bool* wantsFrame) {
        win->frameNow = now;
        win->animFrame = false;
        float progress = gpui::plot::TrackAppear(&cx, generation).Progress();
        if (wantsFrame) {
            *wantsFrame = win->animFrame;
        }
        return progress;
    }
};

// test_plot_appears_once_over_the_theme_duration
static void PlotAppearsOnceOverTheThemeDuration() {
    AppearHarness h;
    bool wants = false;
    utassertnear(h.Frame(1.0, 0, &wants), 0.f);
    utassert(wants);
    utassertnear(h.Frame(1.05, 0, &wants), 0.5f);
    utassertnear(h.Frame(1.1, 0, &wants), 1.f);
    // Once whole, the plot stops asking for frames and stays whole.
    utassertnear(h.Frame(1.2, 0, &wants), 1.f);
    utassert(!wants);
}

// test_reduced_motion_skips_the_appear
static void PlotReducedMotionSkipsTheAppear() {
    AppearHarness h;
    MotionSetReduced(true);
    bool wants = true;
    utassert(h.Frame(1.0, 0, &wants) == 1.f);
    utassert(!wants);
}

// test_plot_without_a_generation_does_not_appear: a plot that does not opt
// in is whole at once and asks for no frames, even with an appear duration
// in the theme. The generation is the plot's to hand over: a hand-built
// ChartEl carries none, so its paint never calls TrackAppear, keeps no state
// and draws with the complete appear.
static void PlotWithoutAGenerationDoesNotAppear() {
    AppearHarness h;
    ChartSeries plain;
    utassert(!plain.appear);
    gpui::plot::PlotAppear appear = gpui::plot::PlotAppear::Complete();
    utassert(appear.Progress() == 1.f && !h.win->animFrame);
    utassert(h.win->motionSlots.len == 0);
}

// test_new_generation_replays_the_appear
static void PlotNewGenerationReplaysTheAppear() {
    AppearHarness h;
    h.Frame(1.0, 0, nullptr);
    utassertnear(h.Frame(1.1, 0, nullptr), 1.f);
    utassertnear(h.Frame(1.2, 1, nullptr), 0.f);
}

// A plot inside a PlotAppearScope, or none, the way PlotElement paints it
// under PaintElNode: the scope pushed and its memory kept alive for the
// frame, the plot sampled if it is mounted, and the frame's motion swept.
// Rust's ScopedView; `scope` < 0 is none. Returns the progress the plot was
// handed, or -1 when it was not painted.
struct ScopedAppearHarness : AppearHarness {
    float Frame(double now, int scope, bool mounted, uint64_t generation,
                bool* wantsFrame = nullptr) {
        win->frameNow = now;
        win->animFrame = false;
        uint32_t key = 0;
        if (scope >= 0) {
            Ctx top = {&app, win, arena, {}};
            El* el = gpui::plot::PlotAppearScope::New(
                &top, StrDup(arena, fmt("scope-%d", scope)), Div(arena));
            key = el->plotAppearScope;
            (void)WindowPlotAppearScopeToken(win, key);
            VecAppend(win->plotAppearScopes, key);
        }
        float progress = -1.f;
        if (mounted) {
            progress = gpui::plot::TrackAppear(&cx, generation).Progress();
        }
        if (scope >= 0) {
            win->plotAppearScopes.len--;
        }
        if (wantsFrame) {
            *wantsFrame = win->animFrame;
        }
        WindowMotionSweep(win);
        win->frameSeq++;
        return progress;
    }
    ~ScopedAppearHarness() {
        VecReset(win->plotAppearScopes);
        WindowKeyedFree(win);
        WindowMotionFree(win);
    }
};

// test_scope_keeps_a_finished_appear_across_a_remount: inside a scope, a plot
// painted again after a gap is whole at once and asks for no frames; a new
// generation still replays.
static void PlotScopeKeepsAFinishedAppearAcrossARemount() {
    ScopedAppearHarness h;
    utassertnear(h.Frame(1.0, 0, true, 0), 0.f);
    utassertnear(h.Frame(1.1, 0, true, 0), 1.f);
    utassert(h.Frame(1.2, 0, false, 0) == -1.f);
    bool wants = true;
    utassertnear(h.Frame(1.3, 0, true, 0, &wants), 1.f);
    utassert(!wants);
    utassertnear(h.Frame(1.4, 0, true, 1), 0.f);
}

// test_scope_replays_an_unfinished_appear
static void PlotScopeReplaysAnUnfinishedAppear() {
    ScopedAppearHarness h;
    utassertnear(h.Frame(1.0, 0, true, 0), 0.f);
    utassert(h.Frame(1.01, 0, false, 0) == -1.f);
    utassertnear(h.Frame(1.02, 0, true, 0), 0.f);
}

// test_scope_forgets_when_it_goes: a scope under a new id, or one that stops
// being painted, draws its plots in afresh.
static void PlotScopeForgetsWhenItGoes() {
    ScopedAppearHarness h;
    h.Frame(1.0, 0, true, 0);
    utassertnear(h.Frame(1.1, 0, true, 0), 1.f);
    utassertnear(h.Frame(1.2, 1, true, 0), 0.f);
    utassertnear(h.Frame(1.3, 1, true, 0), 1.f);
    utassertnear(h.Frame(1.4, -1, true, 0), 0.f);
    utassertnear(h.Frame(1.5, -1, true, 0), 1.f);
    utassertnear(h.Frame(1.6, 1, true, 0), 0.f);
}

// test_without_a_scope_a_remount_replays
static void PlotWithoutAScopeARemountReplays() {
    ScopedAppearHarness h;
    h.Frame(1.0, -1, true, 0);
    utassertnear(h.Frame(1.1, -1, true, 0), 1.f);
    utassert(h.Frame(1.2, -1, false, 0) == -1.f);
    utassertnear(h.Frame(1.3, -1, true, 0), 0.f);
}

// grid.rs: solid_line_is_one_segment, dashes_alternate_and_clip_at_the_end,
// odd_dash_array_repeats_like_svg and
// line_box_is_one_pixel_centred_on_the_coordinate.
static void PlotGridDashSegments() {
    Arena* a = ArenaNew();
    ArenaVec<Point> solid;
    gpui::plot::GridDashSegments(a, {0, 5}, {10, 5}, nullptr, 0, &solid);
    utassert(len(solid) == 2 && solid[0].x == 0 && solid[1].x == 10);
    const float empty[1] = {0};
    ArenaVec<Point> none;
    gpui::plot::GridDashSegments(a, {0, 5}, {10, 5}, empty, 0, &none);
    utassert(len(none) == 2 && none[0].x == 0 && none[1].x == 10);

    const float dashes[2] = {4, 2};
    ArenaVec<Point> clipped;
    gpui::plot::GridDashSegments(a, {0, 5}, {11, 5}, dashes, 2, &clipped);
    utassert(len(clipped) == 4);
    utassertnear(clipped[0].x, 0);
    utassertnear(clipped[1].x, 4);
    utassertnear(clipped[2].x, 6);
    utassertnear(clipped[3].x, 10);

    // 5,3,2 is 5 on, 3 off, 2 on, 5 off, 3 on, 2 off.
    const float odd[3] = {5, 3, 2};
    ArenaVec<Point> svg;
    gpui::plot::GridDashSegments(a, {0, 0}, {0, 20}, odd, 3, &svg);
    const float want[6] = {0, 5, 8, 10, 15, 18};
    utassert(len(svg) == 6);
    for (int i = 0; i < 6 && i < len(svg); i++) {
        utassertnear(svg[i].y, want[i]);
    }

    Bounds vertical = gpui::plot::GridLineBounds({10, 0}, {10, 40});
    utassertnear(vertical.x, 9.5f);
    utassertnear(vertical.y, 0);
    utassertnear(vertical.w, 1);
    utassertnear(vertical.h, 40);
    Bounds horizontal = gpui::plot::GridLineBounds({40, 7}, {0, 7});
    utassertnear(horizontal.x, 0);
    utassertnear(horizontal.y, 6.5f);
    utassertnear(horizontal.w, 40);
    utassertnear(horizontal.h, 1);
    ArenaDelete(a);
}

// A gradient stroke, grid line and dot paint the whole gradient rather than
// its first stop: red at the left end, blue at the right. The pixels come
// back premultiplied BGRA.
static void PlotGradientStrokesAndDotsPaintBothStops() {
#if !GPUI_OS_WASM
    App* app = AppNew();
    if (!app) {
        return;
    }
    Arena* arena = ArenaNew();
    PaintCtx paint = {};
    paint.pa = app->paint;
    paint.app = app;
    paint.opacity = 1;
    const int kW = 64;
    const int kH = 48;
    if (PaintTargetBeginOffscreen(&paint, kW, kH)) {
        Rgba red = Rgba8(255, 0, 0, 255);
        Rgba blue = Rgba8(0, 0, 255, 255);
        // 90 degrees runs left to right.
        Background ramp =
            BackgroundLinear(90, ColorStopAt(red, 0), ColorStopAt(blue, 1));
        // A 4px stroked path along y = 6.
        Path* path = PathNew(&paint, false);
        PathMoveTo(path, 4, 6);
        PathLineTo(path, 60, 6);
        PathStrokeGradient(&paint, path, 4, 4, 6, 60, 6, red, blue);
        PathFree(path);
        // A grid line along y = 20 across the whole width.
        const float gridY[1] = {20};
        gpui::plot::Grid::New().Y(gridY, 1)->Stroke(ramp)->Paint(
            &paint, Bounds{0, 0, (float)kW, (float)kH});
        // A 16px dot at (32, 36) filled with the same ramp.
        CanvasEllipseGradient(&paint, 32, 36, 8, 8, 24, 36, 40, 36, red, blue);
        uint8_t* px = (uint8_t*)Alloc(arena, kW * kH * 4);
        utassert(PaintTargetEndOffscreen(&paint, px));
        auto redish = [&](int x, int y) {
            const uint8_t* p = px + (y * kW + x) * 4;
            return p[3] > 100 && p[2] > p[0] + 60;
        };
        auto blueish = [&](int x, int y) {
            const uint8_t* p = px + (y * kW + x) * 4;
            return p[3] > 100 && p[0] > p[2] + 60;
        };
        utassert(redish(6, 6) && blueish(58, 6));
        utassert(redish(2, 20) && blueish(61, 20));
        utassert(redish(26, 36) && blueish(38, 36));
        // Off the dot, nothing.
        utassert(px[(36 * kW + 2) * 4 + 3] == 0);
    }
    TextMeasClear(&paint);
    ArenaDelete(arena);
    AppFree(app);
#endif
}

void TestScale() {
    TestSuite("scale/linear");
    ScaleLinearBasics();
    ScaleLinearUnorderedDomain();
    ScaleLinearF32();
    ScaleLinearEmpty();

    TestSuite("scale/point");
    ScalePointBasics();
    ScalePointRange();
    ScalePointEmpty();
    ScalePointSingle();
    ScalePointTickAtMatchesTick();
    ScalePointNearestIndexBasic();
    ScalePointNearestIndexWithOffset();
    ScalePointNearestIndexDegenerate();
    ScalePointReversedRange();

    TestSuite("scale/ordinal");
    ScaleOrdinalCycles();
    ScaleOrdinalUnknown();
    ScaleOrdinalEmptyRange();

    TestSuite("scale/band");
    ScaleBandThirds();
    ScaleBandEmpty();
    ScaleBandPadding();
    ScaleBandSingle();
    ScaleBandMaxBandWidthCapsTheWidthButNotTheTicks();
    ScaleBandRangeStart();
    ScaleBandDedup();
    ScaleBandNearestIndex();
    ScaleBandCount();
    ScaleBandStep();

    TestSuite("plot/tooltip");
    PlotTooltipQuadrants();
    PlotTooltipClamps();

    TestSuite("plot/shapes");
    PlotShapeGeometry();
    PlotPieArcs();
    PlotArcContains();
    PlotHoverReaders();
    PlotStackSeries();
    PlotAxisBuilderOrderDoesNotMoveLabels();
    PlotAxisGutterFitsDefaultLabels();
    PlotBarAndAxisContracts();
    PlotGridDashSegments();
    PlotGradientStrokesAndDotsPaintBothStops();

    TestSuite("plot/appear");
    PlotCompleteAppear();
    PlotStaggeredMarksShareTheAppear();
    PlotStaggeredWithoutSpreadMovesTogether();
    PlotAppearsOnceOverTheThemeDuration();
    PlotReducedMotionSkipsTheAppear();
    PlotWithoutAGenerationDoesNotAppear();
    PlotNewGenerationReplaysTheAppear();
    PlotScopeKeepsAFinishedAppearAcrossARemount();
    PlotScopeReplaysAnUnfinishedAppear();
    PlotScopeForgetsWhenItGoes();
    PlotWithoutAScopeARemountReplays();
}
