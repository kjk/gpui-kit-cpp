/* crates/ui/src/chart/{radar,sankey}_chart.rs: public label values. */

#include "Test.h"

using namespace gpui::component;

static bool ChartColorEq(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void RadarLabelsRetainTextAndElements() {
    RadarLabel text = RadarLabel::Text(StrL("Sales"));
    El element = {};
    RadarLabel custom = RadarLabel::Element(&element);
    utassert(text.kind == RadarLabel::Kind::Text);
    utassert(base::StrEq(text.text, StrL("Sales")));
    utassert(text.element == nullptr);
    utassert(custom.kind == RadarLabel::Kind::Element);
    utassert(custom.element == &element);
    utassert(!custom.text.s);

    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    cx.app = &app;
    float values[3] = {1, 2, 3};
    El* labelElement =
        Div(a)->FlexCol()->Child(TextEl(a, StrL("custom label")));
    RadarLabel labels[3] = {RadarLabel::Text(StrL("one")),
                            RadarLabel::Element(labelElement),
                            RadarLabel::Text(StrL("three"))};
    Rgba red = RgbaHex(0xff0000);
    RadarChart* chart = RadarChart::New(&cx, values, 3)
                            ->Labels(labels)
                            ->LabelColor(red)
                            ->LabelGap(17)
                            ->GridLevels(0);
    El* root = chart->IntoEl();
    utassert(chart->labels == labels);
    utassert(chart->hasLabelColor);
    utassert(ChartColorEq(chart->labelColor, red));
    utassertnear(chart->labelGap, 17);
    utassert(chart->gridLevels == 1);
    utassert(root->customPaint != nullptr);
    utassert(root->customUser == chart);
    utassert(root->first == labelElement);
    utassert(labelElement->style.absolute);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void PlainRadarLabelsProjectToTheTaggedValue() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    cx.app = &app;
    float values[3] = {1, 2, 3};
    const char* names[3] = {"one", "two", "three"};
    RadarChart* chart = RadarChart::New(&cx, values, 3)->Labels(names);
    utassert(chart->labels != nullptr);
    for (int i = 0; i < 3; i++) {
        utassert(chart->labels[i].kind == RadarLabel::Kind::Text);
        utassert(base::StrEq(chart->labels[i].text, names[i]));
    }

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void SankeyLabelsCarryIndependentStylesAndDoNotCap() {
    Rgba red = RgbaHex(0xff0000);
    SankeyLabel plain = SankeyLabel::New(StrL("a"));
    SankeyLabel styled = SankeyLabel::New(StrL("b")).Color(red).FontSize(14);
    utassert(base::StrEq(plain.text, StrL("a")));
    utassert(!plain.hasColor);
    utassert(plain.fontSize == 0);
    // plot/label.rs: TEXT_SIZE 10 + TEXT_GAP 2.
    utassertnear(plain.LineHeight(), 12);
    utassert(styled.hasColor);
    utassert(ChartColorEq(styled.color, red));
    utassertnear(styled.LineHeight(), 16);

    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    cx.app = &app;
    SankeyChart* chart = SankeyChart::New(&cx)->Node(StrL("ignored"));
    for (int i = 0; i < 40; i++) {
        chart->CustomLabel(i & 1 ? styled : plain);
    }
    const SankeyChartNode& node = chart->nodes[0];
    utassert(node.hasCustomLabels);
    utassert(node.labels.len == 40);
    utassert(base::StrEq(node.labels[0].text, StrL("a")));
    utassert(base::StrEq(node.labels[39].text, StrL("b")));
    utassertnear(node.labels[39].fontSize, 14);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void PlotPathCachesFollowShapeKeysAndSlots() {
    ShapeKey a = ShapeKey::New(7);
    a.PointValue({1, 2}).Float(3);
    ShapeKey same = ShapeKey::New(7);
    same.PointValue({1, 2}).Float(3);
    ShapeKey moved = ShapeKey::New(7);
    moved.PointValue({1, 4}).Float(3);
    utassert(a.Finish() == same.Finish());
    utassert(a.Finish() != moved.Finish());

    PathCaches caches;
    PathCache* first = caches.Slot(2);
    utassert(first && !first->IsWarm());
    utassert(!first->Touch(a.Finish()));
    utassert(first->Touch(a.Finish()));
    utassert(!first->Touch(moved.Finish()));
    PathCache* pairA = nullptr;
    PathCache* pairB = nullptr;
    caches.SlotPair(3, &pairA, &pairB);
    utassert(pairA == caches.Slot(6));
    utassert(pairB == caches.Slot(7));
}

static void UnchangedPlotLabelsKeepTheScene() {
#if GPUI_OS_WINDOWS
    TestSuite("plot label scene stability");
    PaintApp* app = PaintAppNew();
    utassert(app);
    if (!app) {
        return;
    }
    Arena* arena = ArenaNew();
    PaintCtx paint = {};
    paint.pa = app;
    paint.viewW = 320;
    paint.viewH = 200;
    paint.opacity = 1;
    plot::PlotLabel labels = plot::PlotLabel::New(arena);
    plot::Text label = plot::Text::New(StrL("axis label"), Point{100, 20},
                                       Rgba8(0, 0, 0, 255));
    label.Align(plot::PlotTextAlign::Center);
    labels.Add(label);
    for (int frame = 0; frame < 4; frame++) {
        TextMeasBeginFrame(&paint);
        scene::FrameBegin(&paint);
        // Colour and alignment are draw inputs, independent of the cached
        // shape. Both must still invalidate the scene when they change.
        if (frame == 2) {
            labels.items[0].color = Rgba8(255, 0, 0, 255);
        } else if (frame == 3) {
            labels.items[0].align = plot::PlotTextAlign::Right;
        }
        labels.Paint(&paint, Bounds{0, 0, 320, 200});
        Bounds damage = {};
        bool changed = scene::FrameEnd(&paint, &damage);
        utassert(changed == (frame != 1));
        TextMeasEndFrame(&paint);
    }
    TextMeasClear(&paint);
    scene::Free(&paint);
    ArenaDelete(arena);
    PaintAppFree(app);
#endif
}

static void UnchangedSankeyLabelsKeepTheScene() {
#if GPUI_OS_WINDOWS
    TestSuite("sankey label scene stability");
    App* app = AppNew();
    utassert(app);
    if (!app) {
        return;
    }
    component::Init(app);
    Arena* arena = ArenaNew();
    Ctx cx = {};
    cx.app = app;
    cx.a = arena;
    PaintCtx paint = {};
    paint.pa = app->paint;
    paint.app = app;
    paint.opacity = 1;
    bool ready = PaintTargetBeginOffscreen(&paint, 200, 200);
    utassert(ready);
    if (ready) {
        SankeyChart* chart = SankeyChart::New(&cx)
                                 ->Node(StrL("Source"))
                                 ->Node(StrL("Destination"))
                                 ->Link(0, 1, 10);
        El* el = chart->IntoEl();
        el->w = el->h = 200;
        for (int frame = 0; frame < 2; frame++) {
            TextMeasBeginFrame(&paint);
            scene::FrameBegin(&paint);
            el->customPaint(&paint, el, el->customUser);
            Bounds damage = {};
            bool changed = scene::FrameEnd(&paint, &damage);
            utassert(scene::Stats(&paint).prims > 0);
            utassert(changed == (frame == 0));
            TextMeasEndFrame(&paint);
        }
        uint8_t* pixels = (uint8_t*)Alloc(arena, 200 * 200 * 4);
        utassert(PaintTargetEndOffscreen(&paint, pixels));
    }
    TextMeasClear(&paint);
    scene::Free(&paint);
    ArenaDelete(arena);
    AppFree(app);
#endif
}

static void PieSliceRadiusFallsBackToTheRing() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    cx.app = &app;
    PieChart* unset = PieChart::New(&cx)
                          ->Slice(1, RgbaHex(0xff0000))
                          ->Slice(3, RgbaHex(0x00ff00));
    utassertnear(unset->ResolveOuterRadius(200.f), 80.f);
    PieChart* set = PieChart::New(&cx)->OuterRadius(50.f);
    utassertnear(set->ResolveOuterRadius(200.f), 50.f);
    AppGlobalClear(&app);
    ArenaDelete(a);
}

// chart/mod.rs tests: a_chart_is_interactive_without_being_given_an_id,
// charts_built_at_different_sites_get_different_ids,
// charts_built_at_one_site_share_an_id, a_named_id_replaces_the_default; and
// radar_chart.rs's builder assertion that `id` names the chart.
static PieChart* PieAtOneSite(Ctx* cx) {
    return PieChart::New(cx)->Slice(1, RgbaHex(0xff0000));
}

static void AChartsIdDefaultsToItsConstructionSite() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    cx.app = &app;
    // Interactive without being given an id: every themed chart has one and
    // hands the pointer layer to its element.
    float ys[3] = {1, 2, 3};
    utassert(PieAtOneSite(&cx)->id != 0);
    El* line = LineChart::New(&cx, ys, 3)->IntoEl();
    utassert(line->Chart()->tooltip && line->Chart()->id != 0);

    uint32_t first = PieChart::New(&cx)->id;
    uint32_t second = PieChart::New(&cx)->id;
    utassert(first != second);
    utassert(PieAtOneSite(&cx)->id == PieAtOneSite(&cx)->id);

    utassert(PieAtOneSite(&cx)->Id(StrL("pie"))->id ==
             IdFoldName(cx.path, StrL("pie")));
    utassert(RadarChart::New(&cx, ys, 3)->Id(StrL("radar"))->id ==
             IdFoldName(cx.path, StrL("radar")));

    // The site is on the id stack: the same site under another parent is
    // another chart.
    uint32_t outside = PieAtOneSite(&cx)->id;
    {
        IdScope scope(&cx, StrL("row-2"));
        utassert(PieAtOneSite(&cx)->id != outside);
    }
    AppGlobalClear(&app);
    ArenaDelete(a);
}

// chart/mod.rs: a_chart_turned_off_has_no_id_to_key_anything_on. pie_chart.rs:
// test_tooltip_name_does_not_turn_on_leader_lines. sankey_chart.rs:
// test_tooltip_text_is_settable_without_drawing_labels.
static void AChartTurnedOffHasNoIdToKeyAnythingOn() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    cx.app = &app;
    utassert(PieAtOneSite(&cx)->Interactive(false)->PlotId() == 0);
    utassert(PieAtOneSite(&cx)->Id(StrL("pie"))->Interactive(false)->PlotId() ==
             0);
    utassert(PieAtOneSite(&cx)->PlotId() != 0);
    // Standing down takes the hitbox, and with it the crosshair and tooltip.
    float ys[3] = {1, 2, 3};
    El* line = LineChart::New(&cx, ys, 3)->Interactive(false)->IntoEl();
    utassert(!line->Chart()->tooltip);

    // The row's name is the slice's own, and reaching it does not put labels
    // on the ring: `Label` is what draws the leader lines.
    PieChart* titled = PieAtOneSite(&cx)->TooltipName(StrL("Tech"));
    utassert(StrEq(titled->slices[0].tooltipName, StrL("Tech")));
    utassert(!titled->hasLabels && !titled->slices[0].label.s);
    PieChart* labelled = PieAtOneSite(&cx)->Label(StrL("Tech"));
    utassert(!labelled->slices[0].tooltipName.s && labelled->hasLabels);

    // A sankey drawing its text through CustomLabel sets neither the value
    // label nor anything the tooltip would otherwise read.
    SankeyChart* sankey = SankeyChart::New(&cx)
                              ->Node(StrL("Revenue"))
                              ->TooltipName(StrL("Revenue"))
                              ->TooltipValue(StrL("12M"));
    utassert(StrEq(sankey->nodes[0].tooltipName, StrL("Revenue")));
    utassert(StrEq(sankey->nodes[0].tooltipValue, StrL("12M")));
    utassert(!sankey->nodes[0].value.s && !sankey->showValues);
    AppGlobalClear(&app);
    ArenaDelete(a);
}

// area_chart.rs: test_point_count_fills_the_leading_part. A 100-wide plot
// laid out for five points puts three of data on the first three of them;
// a count below the data's length changes nothing.
static void PointCountFillsTheLeadingPart() {
    const float xs[3] = {0, 1, 2};
    float range[2] = {};
    ChartPointRange(0.f, 100.f, 3, ChartAxisPointCount(5, 3), range);
    ScalePoint x = ScalePoint::New(xs, 3, range, 2);
    float at = -1;
    utassert(x.Tick(0, &at) && at == 0.f);
    utassert(x.Tick(2, &at) && at == 50.f);

    ChartPointRange(0.f, 100.f, 3, ChartAxisPointCount(2, 3), range);
    x = ScalePoint::New(xs, 3, range, 2);
    utassert(x.Tick(2, &at) && at == 100.f);
}

// area_chart.rs: test_y_domain_replaces_the_fit_from_zero, on a 50-high plot
// with no x axis.
static void YDomainReplacesTheFitFromZero() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    cx.app = &app;
    float ys[2] = {10, 20};
    El* pinned = AreaChart::New(&cx, ys, 2)->YDomain(10, 20)->IntoEl();
    ScaleLinear y = ChartPointValueScale(*pinned->Chart(), 50.f);
    float at = -1;
    utassert(y.Tick(10, &at) && at == 50.f);
    utassert(y.Tick(20, &at) && at == 10.f);

    El* fitted = AreaChart::New(&cx, ys, 2)->IntoEl();
    y = ChartPointValueScale(*fitted->Chart(), 50.f);
    utassert(y.Tick(0, &at) && at == 50.f);
    utassert(y.Tick(20, &at) && at == 10.f);

    // LineChart pins the same way.
    El* line = LineChart::New(&cx, ys, 2)->YDomain(10, 20)->IntoEl();
    y = ChartPointValueScale(*line->Chart(), 50.f);
    utassert(y.Tick(10, &at) && at == 50.f);
    AppGlobalClear(&app);
    ArenaDelete(a);
}

// chart/mod.rs: only_the_last_point_right_aligns_its_label. The last item of
// data laid out for more points sits mid-axis and stays centered.
static void OnlyTheLastPointRightAlignsItsLabel() {
    using plot::PlotTextAlign;
    utassert(ChartPointLabelAlign(0, 3) == PlotTextAlign::Left);
    utassert(ChartPointLabelAlign(1, 3) == PlotTextAlign::Center);
    utassert(ChartPointLabelAlign(2, 3) == PlotTextAlign::Right);
    utassert(ChartPointLabelAlign(0, 5) == PlotTextAlign::Left);
    utassert(ChartPointLabelAlign(1, 5) == PlotTextAlign::Center);
    utassert(ChartPointLabelAlign(2, 5) == PlotTextAlign::Center);
    utassert(ChartPointLabelAlign(0, 1) == PlotTextAlign::Center);
}

// bar_chart.rs: test_value_tick_positions. Both ends are included, so five
// ticks mean four intervals.
static void ValueTickPositionsCountTicks() {
    float out[8] = {};
    utassert(ChartValueTickPositions(10.f, 110.f, 5, out, 8) == 5);
    utassert(out[0] == 10.f && out[1] == 35.f && out[2] == 60.f &&
             out[3] == 85.f && out[4] == 110.f);
    // Top-aligned charts have the baseline before the far edge.
    utassert(ChartValueTickPositions(110.f, 10.f, 3, out, 8) == 3);
    utassert(out[0] == 110.f && out[1] == 60.f && out[2] == 10.f);
    utassert(ChartValueTickPositions(0.f, 50.f, 2, out, 8) == 2);
    utassert(out[0] == 0.f && out[1] == 50.f);
}

// bar_chart.rs: test_min_length_extends_away_from_zero.
static void MinLengthExtendsAwayFromZero() {
    // A zero or tiny bar grows the way a positive one would.
    utassert(BarExtendToMinLength(100, 100, false, BarAlign::Bottom, 2) == 98);
    utassert(BarExtendToMinLength(10, 10, false, BarAlign::Top, 2) == 12);
    utassert(BarExtendToMinLength(10, 10, false, BarAlign::Left, 2) == 12);
    utassert(BarExtendToMinLength(90, 90, false, BarAlign::Right, 2) == 88);
    // A small negative bar grows to the other side of the zero line.
    utassert(BarExtendToMinLength(50.5f, 50, true, BarAlign::Bottom, 2) == 52);
    // A bar already long enough is left alone.
    utassert(BarExtendToMinLength(40, 100, false, BarAlign::Bottom, 2) == 40);
}

// chart/mod.rs: the_default_ticks_keep_the_grid_in_place. Five ticks put the
// grid where it always was: four lines splitting the plot, the baseline left
// to the x axis.
static void TheDefaultTicksKeepTheGridInPlace() {
    float rows[8] = {};
    int n = ChartTickPositions(ChartSeries{}.yTickCount, 100.f, rows, 8);
    utassert(n == 5);
    utassert(rows[0] == 0.f && rows[1] == 25.f && rows[2] == 50.f &&
             rows[3] == 75.f);
}

static int Shown(int len, int count, int margin, int* out) {
    bool labeled[16] = {};
    ChartLabeledItems(len, count, margin, labeled);
    int n = 0;
    for (int i = 0; i < len; i++) {
        if (labeled[i]) {
            out[n++] = i;
        }
    }
    return n;
}

// chart/mod.rs: a_label_count_spreads_labels_from_the_first_item_to_the_last.
static void ALabelCountSpreadsLabelsFromTheFirstItemToTheLast() {
    int ix[16] = {};
    utassert(Shown(11, 3, 1, ix) == 3 && ix[0] == 0 && ix[1] == 5 &&
             ix[2] == 10);
    utassert(Shown(10, 2, 1, ix) == 2 && ix[0] == 0 && ix[1] == 9);
    utassert(Shown(3, 5, 1, ix) == 3 && ix[0] == 0 && ix[2] == 2);
    utassert(Shown(4, 1, 1, ix) == 1 && ix[0] == 0);
    utassert(Shown(4, 0, 1, ix) == 0);
    // Without a count the stride still decides.
    utassert(Shown(4, -1, 2, ix) == 2 && ix[0] == 1 && ix[1] == 3);
}

// chart/mod.rs: a_tick_reads_the_value_at_its_height. The top tick reads past
// the highest value by the padding above it.
static void ATickReadsTheValueAtItsHeight() {
    float ys[2] = {10, 20};
    ChartSeries fitted = {};
    fitted.ys = ys;
    fitted.n = 2;
    ChartValueExtent extent = {};
    ChartPointValueScale(fitted, 110.f, &extent);
    utassert(extent.ValueAt(110.f) == 0.0);
    utassert(extent.ValueAt(10.f) == 20.0);
    utassert(fabs(extent.ValueAt(0.f) - 22.0) < 1e-4);
    float at = 0;
    utassert(extent.PositionOf(20.0, &at) && at == 10.f);

    float zero[1] = {0};
    ChartSeries pinned = {};
    pinned.ys = zero;
    pinned.n = 1;
    pinned.pinnedDomain = true;
    pinned.domainMin = 100;
    pinned.domainMax = 200;
    pinned.yPaddingTop = 0;
    ChartPointValueScale(pinned, 100.f, &extent);
    utassert(extent.ValueAt(0.f) == 200.0);
    utassert(extent.PositionOf(150.0, &at) && at == 50.f);
}

static Str DollarTick(Arena* a, double value, void*) {
    return StrDup(a, fmt("$%.0f", value));
}

// bar_chart.rs: value_tick_labels_walk_the_domain_from_the_far_end.
static void ValueTickLabelsWalkTheDomainFromTheFarEnd() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    cx.app = &app;
    float ys[2] = {10, 20};
    El* bar = BarChart::New(&cx, ys, 2)->ValueTickCount(3)->IntoEl();
    Str labels[4] = {};
    utassert(ChartBarValueTickLabels(a, *bar->Chart(), labels, 4) == 3);
    utassert(base::StrEq(labels[0], StrL("20")) &&
             base::StrEq(labels[1], StrL("10")) &&
             base::StrEq(labels[2], StrL("0")));
    El* money = BarChart::New(&cx, ys, 2)
                    ->ValueTickCount(3)
                    ->ValueTickFormat(&DollarTick)
                    ->IntoEl();
    ChartBarValueTickLabels(a, *money->Chart(), labels, 4);
    utassert(base::StrEq(labels[0], StrL("$20")) &&
             base::StrEq(labels[2], StrL("$0")));
    AppGlobalClear(&app);
    ArenaDelete(a);
}

// bar_chart.rs: a_band_count_keeps_each_bar_in_its_band, through the band
// scale the bars are laid out on, and the value-axis gutter: labels inside
// the plot leave the bars their full width.
static void ABandCountKeepsEachBarInItsBand() {
    const float range[2] = {0.f, 40.f};
    ScaleBand wide = ScaleBand::New(2, range, 2).BandCount(2);
    ScaleBand narrow = ScaleBand::New(2, range, 2).BandCount(4);
    wide.paddingInner = narrow.paddingInner = 0.4f;
    wide.paddingOuter = narrow.paddingOuter = 0.2f;
    utassertnear(narrow.BandWidth() * 2.f, wide.BandWidth());
    float t = 0;
    utassert(narrow.Tick(1, &t) && t < 20.f);
    ScaleBand grown = ScaleBand::New(3, range, 2).BandCount(4);
    grown.paddingInner = 0.4f;
    grown.paddingOuter = 0.2f;
    float g = 0;
    utassert(grown.Tick(1, &g) && g == t);
    utassertnear(grown.BandWidth(), narrow.BandWidth());

    ChartSeries outside = {};
    outside.kind = ChartKind::Bar;
    outside.valueAxis = true;
    ChartSeries inside = outside;
    inside.axisLabelPlacement = AxisLabelPlacement::Inside;
    utassert(ChartBarValueAxisGap(outside, kChartValueAxisGap) ==
             kChartValueAxisGap);
    utassert(ChartBarValueAxisGap(inside, kChartValueAxisGap) == 0.f);
}

void TestChart() {
    TestSuite("chart labels");
    RadarLabelsRetainTextAndElements();
    PlainRadarLabelsProjectToTheTaggedValue();
    SankeyLabelsCarryIndependentStylesAndDoNotCap();
    PlotPathCachesFollowShapeKeysAndSlots();
    UnchangedPlotLabelsKeepTheScene();
    UnchangedSankeyLabelsKeepTheScene();
    PieSliceRadiusFallsBackToTheRing();
    AChartsIdDefaultsToItsConstructionSite();
    AChartTurnedOffHasNoIdToKeyAnythingOn();
    PointCountFillsTheLeadingPart();
    YDomainReplacesTheFitFromZero();
    OnlyTheLastPointRightAlignsItsLabel();
    ValueTickPositionsCountTicks();
    MinLengthExtendsAwayFromZero();
    TheDefaultTicksKeepTheGridInPlace();
    ALabelCountSpreadsLabelsFromTheFirstItemToTheLast();
    ATickReadsTheValueAtItsHeight();
    ValueTickLabelsWalkTheDomainFromTheFarEnd();
    ABandCountKeepsEachBarInItsBand();
}
