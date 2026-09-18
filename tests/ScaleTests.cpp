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

static void ScaleLinearMultipleRange() {
    const float domain[] = {1, 2, 3};
    float t = 0;

    // Only the ends of the range count, and only their order.
    const float up[] = {0, 50, 100};
    ScaleLinear s = ScaleLinear::New(domain, 3, up, 3);
    utassert(s.Tick(1, &t) && TestNear(t, 0.f));
    utassert(s.Tick(2, &t) && TestNear(t, 50.f));
    utassert(s.Tick(3, &t) && TestNear(t, 100.f));

    const float down[] = {100, 50, 0};
    s = ScaleLinear::New(domain, 3, down, 3);
    utassert(s.Tick(1, &t) && TestNear(t, 100.f));
    utassert(s.Tick(2, &t) && TestNear(t, 50.f));
    utassert(s.Tick(3, &t) && TestNear(t, 0.f));

    const float downUp[] = {100, 0, 100};
    s = ScaleLinear::New(domain, 3, downUp, 3);
    utassert(s.Tick(1, &t) && TestNear(t, 100.f));
    utassert(s.Tick(2, &t) && TestNear(t, 50.f));
    utassert(s.Tick(3, &t) && TestNear(t, 0.f));

    const float upDown[] = {0, 100, 0};
    s = ScaleLinear::New(domain, 3, upDown, 3);
    utassert(s.Tick(1, &t) && TestNear(t, 0.f));
    utassert(s.Tick(2, &t) && TestNear(t, 50.f));
    utassert(s.Tick(3, &t) && TestNear(t, 100.f));
}

static void ScaleLinearEmpty() {
    float t = 0;

    // No domain is no extent to divide by, so there is no tick at all.
    const float range[] = {0, 100};
    ScaleLinear s = ScaleLinear::New(nullptr, 0, range, 2);
    utassert(!s.Tick(1, &t));
    utassert(!s.Tick(2, &t));
    utassert(!s.Tick(3, &t));

    // No range still ticks; everything lands on zero.
    const float domain[] = {1, 2, 3};
    s = ScaleLinear::New(domain, 3, nullptr, 0);
    utassert(s.Tick(1, &t) && TestNear(t, 0.f));
    utassert(s.Tick(2, &t) && TestNear(t, 0.f));
    utassert(s.Tick(3, &t) && TestNear(t, 0.f));
}

static void ScaleLinearLeastIndexWithDomain() {
    const float domain[] = {1, 2, 3};
    const float range[] = {0, 100};
    ScaleLinear s = ScaleLinear::New(domain, 3, range, 2);

    int index = 0;
    float tick = 0;
    s.LeastIndexWithDomain(0, domain, 3, &index, &tick);
    utassert(index == 0);
    utassertnear(tick, 0.f);

    s.LeastIndexWithDomain(50, domain, 3, &index, &tick);
    utassert(index == 1);
    utassertnear(tick, 50.f);

    s.LeastIndexWithDomain(100, domain, 3, &index, &tick);
    utassert(index == 2);
    utassertnear(tick, 100.f);
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

static void ScalePointEmpty() {
    float t = 0;

    const float range[] = {0, 100};
    ScalePoint s = ScalePoint::New(nullptr, 0, range, 2);
    utassert(!s.Tick(1, &t));
    utassert(!s.Tick(2, &t));
    utassert(!s.Tick(3, &t));

    const float domain[] = {1, 2, 3};
    s = ScalePoint::New(domain, 3, nullptr, 0);
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

static void ScalePointLeastIndexBasic() {
    const float domain[] = {1, 2, 3};
    const float range[] = {0, 100};
    ScalePoint s = ScalePoint::New(domain, 3, range, 2);

    utassert(s.LeastIndex(0) == 0);
    utassert(s.LeastIndex(50) == 1);
    utassert(s.LeastIndex(100) == 2);

    utassert(s.LeastIndex(24) == 0); // closer to 0
    utassert(s.LeastIndex(25) == 1); // equidistant, rounds up
    utassert(s.LeastIndex(26) == 1); // closer to 50
    utassert(s.LeastIndex(74) == 1); // closer to 50
    utassert(s.LeastIndex(75) == 2); // equidistant, rounds up
    utassert(s.LeastIndex(76) == 2); // closer to 100

    utassert(s.LeastIndex(-10) == 0); // below the range
    utassert(s.LeastIndex(150) == 2); // above it
}

static void ScalePointLeastIndexWithOffset() {
    const float domain[] = {1, 2, 3};
    const float range[] = {40, 80};
    ScalePoint s = ScalePoint::New(domain, 3, range, 2);

    // The points are at 40, 60, 80.
    utassert(s.LeastIndex(40) == 0);
    utassert(s.LeastIndex(60) == 1);
    utassert(s.LeastIndex(80) == 2);

    utassert(s.LeastIndex(49) == 0);
    utassert(s.LeastIndex(50) == 1);
    utassert(s.LeastIndex(51) == 1);
    utassert(s.LeastIndex(69) == 1);
    utassert(s.LeastIndex(70) == 2);
    utassert(s.LeastIndex(71) == 2);

    utassert(s.LeastIndex(30) == 0);
    utassert(s.LeastIndex(100) == 2);
}

static void ScalePointLeastIndexDegenerate() {
    const float range[] = {0, 100};
    ScalePoint empty = ScalePoint::New(nullptr, 0, range, 2);
    utassert(empty.LeastIndex(0) == 0);
    utassert(empty.LeastIndex(50) == 0);
    utassert(empty.LeastIndex(100) == 0);

    const float one[] = {1};
    ScalePoint single = ScalePoint::New(one, 1, range, 2);
    utassert(single.LeastIndex(0) == 0);
    utassert(single.LeastIndex(50) == 0);
    utassert(single.LeastIndex(100) == 0);

    const float domain[] = {1, 2, 3};
    ScalePoint noRange = ScalePoint::New(domain, 3, nullptr, 0);
    utassert(noRange.LeastIndex(0) == 0);
    utassert(noRange.LeastIndex(50) == 0);
    utassert(noRange.LeastIndex(100) == 0);
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

    ScaleBand noRange = ScaleBand::New(3, nullptr, 0);
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
    // One band sits in the middle: the width is capped at thirty, so it
    // starts thirty in.
    utassert(b.Tick(0, &t) && TestNear(t, 30.f));
    utassert(b.LeastIndex(80.f) == 0);
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

static void ScaleBandLeastIndex() {
    const float range[2] = {0.f, 90.f};
    ScaleBand b = ScaleBand::New(3, range, 2);
    utassert(b.LeastIndex(0.f) == 0);
    utassert(b.LeastIndex(31.f) == 1);
    utassert(b.LeastIndex(59.f) == 2);
    // And it never runs off either end.
    utassert(b.LeastIndex(-40.f) == 0);
    utassert(b.LeastIndex(400.f) == 2);
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
    Point origin = plot::OriginPoint(3, 4, {10, 20});
    utassertnear(origin.x, 13.f);
    utassertnear(origin.y, 24.f);

    // shape/arc.rs::test_arc_builder and test_arc_centroid.
    plot::Arc arc = plot::Arc::New();
    arc.InnerRadius(10)->OuterRadius(20);
    plot::ArcData arcData = {};
    arcData.value = 1;
    arcData.endAngle = kPi;
    Point centroid = arc.Centroid(arcData);
    utassertnear(centroid.x, 15.f);
    utassertnear(centroid.y, 0.f);

    // shape/line.rs::test_line_path: accessors resolve every valid datum.
    float lineValues[] = {1, 2, 3};
    plot::Line line = plot::Line::New();
    line.Data(lineValues, 3, sizeof(float))->X(PlotFloat)->Y(PlotDouble);
    Point points[3] = {};
    utassert(line.Points({0, 0, 100, 100}, points, 3) == 3);
    utassertnear(points[0].x, 1.f);
    utassertnear(points[2].y, 6.f);

    // radial_line.rs: noon, three, six and nine o'clock around (50, 50).
    float radialValues[] = {1, 1, 1, 1};
    plot::RadialLine radial = plot::RadialLine::New();
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
    plot::Pie pie = plot::Pie::New();
    pie.Value(PlotFloat);
    Arena* arena = ArenaNew();
    ArenaVec<plot::ArcData> arcs;
    pie.Arcs(arena, {values, 4, sizeof(float)}, &arcs);
    utassert(len(arcs) == 2);
    plot::ArcData resolved[2] = {};
    int resolvedCount = 0;
    for (const plot::ArcData& item : arcs) {
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
    plot::Arc arc = plot::Arc::New();
    arc.InnerRadius(10.f)->OuterRadius(40.f);
    plot::ArcData right = {};
    right.value = 1.f;
    right.startAngle = 0.f;
    right.endAngle = kPi;
    Bounds bounds = {0, 0, 100, 100};
    utassert(arc.Contains(right, {80.f, 50.f}, bounds));
    utassert(!arc.Contains(right, {20.f, 50.f}, bounds));
    utassert(!arc.Contains(right, {55.f, 50.f}, bounds));
    utassert(!arc.Contains(right, {95.f, 50.f}, bounds));
    utassert(arc.Contains(right, {95.f, 50.f}, bounds, -1, 50.f));
    utassert(arc.Contains(right, {50.f, 20.f}, bounds));
    utassert(!arc.Contains(right, {50.f, 80.f}, bounds));
}

static void PlotHoverReaders() {
    plot::TooltipState state =
        plot::TooltipState::New(2, {10.f, 20.f}, nullptr, 0);
    plot::PlotHover hover;
    hover.state = state;
    hover.focus = 1.f;
    hover.hovered = true;
    utassert(hover.State().index == 2);
    utassert(hover.IsHovered());
    utassert(!hover.IsEntering());

    plot::PlotHover entering = hover;
    entering.focus = 0.f;
    utassert(entering.IsEntering());

    plot::PlotHover lingering = hover;
    lingering.focus = 0.4f;
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
    plot::Stack stack = plot::Stack::New();
    stack.Data(values, 2, sizeof(PlotSales))
        ->Keys(keys, 3)
        ->Value(PlotSalesValue);
    Arena* arena = ArenaNew();
    ArenaVec<plot::StackSeries> series;
    stack.Series(arena, &series);
    utassert(len(series) == 3);
    Str resolvedKeys[3] = {};
    plot::StackPoint resolvedPoints[3] = {};
    int resolvedCount = 0;
    for (const plot::StackSeries& item : series) {
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

static void PlotBarAndAxisContracts() {
    utassert(!plot::BarAlignmentIsHorizontal(plot::BarAlignment::Bottom));
    utassert(plot::BarAlignmentIsHorizontal(plot::BarAlignment::Left));
    utassertnear(plot::BarAlignmentGradientAngle(plot::BarAlignment::Bottom),
                 0.f);
    utassertnear(plot::BarAlignmentGradientAngle(plot::BarAlignment::Top),
                 180.f);
    utassertnear(plot::BarAlignmentGradientAngle(plot::BarAlignment::Left),
                 90.f);
    utassertnear(plot::BarAlignmentGradientAngle(plot::BarAlignment::Right),
                 270.f);
    Point label =
        plot::BarLabelOrigin(plot::BarAlignment::Bottom, 10, 100, 40, 20);
    utassertnear(label.x, 20.f);
    utassertnear(label.y, 28.f);
    label = plot::BarLabelOrigin(plot::BarAlignment::Left, 10, 0, 40, 20);
    utassertnear(label.x, 42.f);
    utassertnear(label.y, 15.f);

    Arena* arena = ArenaNew();
    plot::PlotAxis axis = plot::PlotAxis::New(arena);
    utassert(axis.xAxis && !axis.yAxis);
    plot::AxisText tick = plot::AxisText::New(StrL("x"), 20, Rgb(1, 2, 3));
    // Rust resolves labels at builder-call time: a label before x is absent.
    axis.XLabel(&tick, 1);
    utassert(axis.xLabel.items.len == 0);
    axis.X(30)->XLabel(&tick, 1);
    utassert(axis.xLabel.items.len == 1);
    if (axis.xLabel.items.len > 0) {
        const plot::Text& text = axis.xLabel.items[0];
        utassertnear(text.origin.x, 20.f);
        utassertnear(text.origin.y, 36.f);
    }
    axis.YLabelSide(plot::AxisLabelSide::Start)->Y(12)->YLabel(&tick, 1);
    utassert(axis.yLabel.items.len == 1);
    if (axis.yLabel.items.len > 0) {
        const plot::Text& text = axis.yLabel.items[0];
        utassertnear(text.origin.x, 10.f);
        utassertnear(text.origin.y, 15.f);
    }
    ArenaDelete(arena);

    plot::CrossLine cross = plot::CrossLine::New({10, 20});
    utassert(cross.ShowVertical() && !cross.ShowHorizontal());
    cross.Both()->Span(3, 40)->HSpan(4, 50);
    utassert(cross.ShowVertical() && cross.ShowHorizontal());
    utassert(cross.hasVerticalLength && cross.verticalStart == 3 &&
             cross.verticalLength == 40);
    utassert(cross.hasHorizontalLength && cross.horizontalStart == 4 &&
             cross.horizontalLength == 50);
}

void TestScale() {
    TestSuite("scale/linear");
    ScaleLinearBasics();
    ScaleLinearMultipleRange();
    ScaleLinearEmpty();
    ScaleLinearLeastIndexWithDomain();

    TestSuite("scale/point");
    ScalePointBasics();
    ScalePointRange();
    ScalePointEmpty();
    ScalePointSingle();
    ScalePointLeastIndexBasic();
    ScalePointLeastIndexWithOffset();
    ScalePointLeastIndexDegenerate();

    TestSuite("scale/ordinal");
    ScaleOrdinalCycles();
    ScaleOrdinalUnknown();
    ScaleOrdinalEmptyRange();

    TestSuite("scale/band");
    ScaleBandThirds();
    ScaleBandEmpty();
    ScaleBandPadding();
    ScaleBandSingle();
    ScaleBandDedup();
    ScaleBandLeastIndex();
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
    PlotBarAndAxisContracts();
}
