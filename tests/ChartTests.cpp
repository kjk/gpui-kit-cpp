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
    component::plot::PlotLabel labels = component::plot::PlotLabel::New(arena);
    component::plot::Text label = component::plot::Text::New(
        StrL("axis label"), Point{100, 20}, Rgba8(0, 0, 0, 255));
    label.Align(component::plot::PlotTextAlign::Center);
    labels.Add(label);
    for (int frame = 0; frame < 4; frame++) {
        TextMeasBeginFrame(&paint);
        scene::FrameBegin(&paint);
        // Colour and alignment are draw inputs, independent of the cached
        // shape. Both must still invalidate the scene when they change.
        if (frame == 2) {
            labels.items[0].color = Rgba8(255, 0, 0, 255);
        } else if (frame == 3) {
            labels.items[0].align = component::plot::PlotTextAlign::Right;
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

// chart/mod.rs: a_chart_turned_off_keeps_its_id_but_not_its_hitbox — a
// chart turned off has no hitbox, so nothing above it has to fight it for
// the cursor, but it keeps its id for its appear. pie_chart.rs:
// test_tooltip_name_does_not_turn_on_leader_lines. sankey_chart.rs:
// test_tooltip_text_is_settable_without_drawing_labels.
static void AChartTurnedOffKeepsItsIdButNotItsHitbox() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    cx.app = &app;
    PieChart* off = PieAtOneSite(&cx)->Interactive(false);
    utassert(!off->PlotInteractive());
    utassert(off->PlotId() != 0);
    utassert(PieAtOneSite(&cx)->Id(StrL("pie"))->Interactive(false)->PlotId() ==
             IdFoldName(cx.path, StrL("pie")));
    utassert(PieAtOneSite(&cx)->PlotInteractive());
    // Standing down takes the hitbox, and with it the crosshair and tooltip.
    float ys[3] = {1, 2, 3};
    El* line = LineChart::New(&cx, ys, 3)->Interactive(false)->IntoEl();
    utassert(!line->Chart()->tooltip && line->Chart()->id != 0);

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

// chart/mod.rs an_appear_key_replays_the_appear: without a key a chart
// appears once; a key names the generation that replays it.
static void AnAppearKeyReplaysTheAppear() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    cx.app = &app;
    uint64_t generation = 1;
    utassert(PieAtOneSite(&cx)->AppearGeneration(&generation) &&
             generation == 0);
    utassert(!PieAtOneSite(&cx)->Appear(false)->AppearGeneration(&generation));
    uint64_t aapl = 0;
    uint64_t tsla = 0;
    uint64_t again = 0;
    utassert(
        PieAtOneSite(&cx)->AppearKey(StrL("AAPL.US"))->AppearGeneration(&aapl));
    utassert(
        PieAtOneSite(&cx)->AppearKey(StrL("TSLA.US"))->AppearGeneration(&tsla));
    utassert(PieAtOneSite(&cx)
                 ->AppearKey(StrL("AAPL.US"))
                 ->AppearGeneration(&again));
    utassert(aapl != tsla && aapl == again);
    // The runtime-painted charts carry it to the paint.
    float ys[3] = {1, 2, 3};
    El* line = LineChart::New(&cx, ys, 3)->AppearKey(StrL("AAPL.US"))->IntoEl();
    utassert(line->Chart()->appear && line->Chart()->appearGeneration == aapl);
    El* still = BarChart::New(&cx, ys, 3)->Appear(false)->IntoEl();
    utassert(!still->Chart()->appear);
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
    using component::plot::PlotTextAlign;
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

// plot/tooltip.rs: a_value_color_colors_only_the_row_added_last.
static void AValueColorColorsOnlyTheRowAddedLast() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    Rgba blue = Rgb(0, 0, 255);
    Rgba red = Rgb(255, 0, 0);
    Rgba green = Rgb(0, 255, 0);
    component::plot::Tooltip* tooltip =
        component::plot::Tooltip::New(&cx, {0, 0}, {100, 100})
            ->ValueColor(red)
            ->Row(blue, StrL("Open"), StrL("1"))
            ->Row(blue, StrL("Close"), StrL("2"))
            ->ValueColor(green);
    utassert(tooltip->rows.len == 2);
    utassert(!tooltip->rows[0].hasValueColor);
    utassert(tooltip->rows[1].hasValueColor &&
             ChartColorEq(tooltip->rows[1].valueColor, green));
    ArenaDelete(a);
}

// plot/tooltip.rs: a_plain_row_has_no_swatch_and_takes_a_value_color and
// plain_rows_keep_a_swatch_slot_only_beside_series_rows.
static void APlainRowHasNoSwatchAndTakesAValueColor() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    Rgba blue = Rgb(0, 0, 255);
    Rgba red = Rgb(255, 0, 0);
    component::plot::Tooltip* mixed =
        component::plot::Tooltip::New(&cx, {0, 0}, {100, 100})
            ->Row(blue, StrL("Call"), StrL("1"))
            ->PlainRow(StrL("Total"), StrL("3"))
            ->ValueColor(red);
    utassert(mixed->rows[0].hasColor &&
             ChartColorEq(mixed->rows[0].color, blue));
    utassert(!mixed->rows[0].hasValueColor);
    utassert(!mixed->rows[1].hasColor && mixed->rows[1].hasValueColor &&
             ChartColorEq(mixed->rows[1].valueColor, red));
    component::plot::Tooltip* plain =
        component::plot::Tooltip::New(&cx, {0, 0}, {100, 100})
            ->PlainRow(StrL("Total"), StrL("3"))
            ->PlainRow(StrL("Ratio"), StrL("0.5"));
    utassert(component::plot::TooltipHasSwatches(mixed->rows));
    utassert(!component::plot::TooltipHasSwatches(plain->rows));
    ArenaDelete(a);
}

// TooltipContent::<f64>: the datum is a double.
static Str DayTitle(Arena* a, const void* d, void*) {
    return StrDup(a, fmt("Day %g", *(const double*)d));
}

static Str DollarValue(Arena* a, const void*, int, double value, void*) {
    return StrDup(a, fmt("$%.2f", value));
}

static Str SignedValue(Arena* a, const void*, int row, double value, void*) {
    return StrDup(a, fmt("%d: %+g", row, value));
}

static Rgba GreenOrRed(const void*, int, double value, void*) {
    return value >= 0 ? Rgb(0, 255, 0) : Rgb(255, 0, 0);
}

// chart/mod.rs: tooltip_text_falls_back_to_the_chart_own.
static void TooltipTextFallsBackToTheChartOwn() {
    Arena* a = ArenaNew();
    ChartTooltipContent content;
    const double one = 1.;
    const double three = 3.;
    Str title = {};
    utassert(content.TitleText(a, &one, StrL("Jan"), true, &title) &&
             StrEq(title, StrL("Jan")));
    utassert(!content.TitleText(a, &one, {}, false, &title));
    utassert(StrEq(content.ValueText(a, &one, 0, 1234.5), StrL("1234.5")));

    content.title = &DayTitle;
    content.value = &DollarValue;
    utassert(content.TitleText(a, &three, {}, false, &title) &&
             StrEq(title, StrL("Day 3")));
    utassert(StrEq(content.ValueText(a, &three, 0, 1234.5), StrL("$1234.50")));
    // The raw number is the float's own, not its binary expansion.
    utassert(StrEq(ChartFormatValue(a, (double)0.1f), StrL("0.1")));
    utassert(StrEq(ChartFormatValue(a, 3), StrL("3")));
    ArenaDelete(a);
}

// The closures receive the datum: an item of what Data(..) gave the chart,
// or the chart's own number for the point.
struct SalesDatum {
    const char* month;
    float sales;
};

static Str SalesMonth(Arena*, const void* d, void*) {
    return Str(((const SalesDatum*)d)->month);
}

static void TooltipClosuresReceiveTheDatum() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    static const float sales[] = {10, 20, 30};
    static const SalesDatum data[] = {{"Jan", 10}, {"Feb", 20}, {"Mar", 30}};
    BarChart* own = BarChart::New(&cx, sales, 3);
    utassert(own->tooltipContent.Datum(1, sales) == &sales[1]);
    BarChart* chart =
        BarChart::New(&cx, sales, 3)->Data(data)->TooltipTitle(&SalesMonth);
    const void* d = chart->tooltipContent.Datum(2, sales);
    utassert(d == &data[2]);
    Str title = {};
    utassert(chart->tooltipContent.TitleText(a, d, {}, false, &title) &&
             StrEq(title, StrL("Mar")));
    ArenaDelete(a);
}

// The month the hovered line chart's tooltip was last titled with.
static const SalesDatum* gTitledDatum = nullptr;

static Str TitleHovered(Arena*, const void* d, void*) {
    gTitledDatum = (const SalesDatum*)d;
    return Str(gTitledDatum->month);
}

static El* FindChartEl(El* e) {
    if (!e) {
        return nullptr;
    }
    if (e->kind == ElKind::Chart) {
        return e;
    }
    for (El* c = e->first; c; c = c->next) {
        if (El* found = FindChartEl(c)) {
            return found;
        }
    }
    return nullptr;
}

struct HoveredLineView {
    static El* Render(HoveredLineView*, Ctx* cx) {
        static const float sales[] = {10, 20, 30};
        static const SalesDatum data[] = {
            {"Jan", 10}, {"Feb", 20}, {"Mar", 30}};
        return Div(cx->a)->SizeFull()->Child(LineChart::New(cx, sales, 3)
                                                 ->Data(data)
                                                 ->TooltipTitle(&TitleHovered)
                                                 ->Id(StrL("hovered-line"))
                                                 ->Appear(false)
                                                 ->IntoEl()
                                                 ->W(300)
                                                 ->H(200));
    }
};

// line_chart.rs Plot::tooltip: hovering a point builds the plot::Tooltip,
// whose title closure is handed the hovered item of the chart's data.
static void AHoveredSeriesChartBuildsItsTooltipFromTheDatum() {
    App* app = TestAppNew();
    component::Init(app);
    Window* win =
        TestWindowOpen(app, EntityNew<HoveredLineView>(app), 300, 200);
    TestDraw(win);
    gTitledDatum = nullptr;
    // The last of three points sits at the right edge.
    TestSimulateMouseMove(win, {295, 80});
    TestAdvanceClock(app, 500);
    TestDraw(win);
    utassert(gTitledDatum && StrEq(Str(gTitledDatum->month), StrL("Mar")));

    // The overlay is the Tooltip's whole: the crosshair, the series' dot and
    // the box, as children of the overlay the chart hangs over its plot,
    // and the chart paints none of them itself. One frame built and painted
    // here, where the tree can be read after.
    Entity<HoveredLineView> view = {};
    view.id = win->root;
    El* root = EntityRender(app, win, win->frameArena, view.id);
    const RuntimeStyle& th = RuntimeStyleNow(app);
    LayoutEl(&win->paint, root, 0, 0, 300, 200, th.fontSize, th.foreground);
    PaintEl(&win->paint, root);
    El* chart = FindChartEl(root);
    El* overlay = chart ? chart->last : nullptr;
    int parts = 0;
    for (El* c = overlay ? overlay->first : nullptr; c; c = c->next) {
        parts++;
    }
    utassert(parts == 3);
    TestAppFree(app);
}

// chart/mod.rs: tooltip_fill_writes_each_row_with_the_value_color and
// tooltip_fill_leaves_the_title_off_without_one.
static void TooltipFillWritesEachRowWithTheValueColor() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    Rgba blue = Rgb(0, 0, 255);
    const double one = 1.;
    ChartTooltipContent content;
    content.value = &SignedValue;
    content.valueColor = &GreenOrRed;
    ChartTooltipSeriesRow rows[2] = {{blue, StrL("Open"), 2.},
                                     {blue, StrL("Close"), -1.}};
    component::plot::Tooltip* tooltip = ChartTooltipApply(
        content, component::plot::Tooltip::New(&cx, {0, 0}, {100, 100}), &one,
        StrL("Jan"), true, rows, 2);
    utassert(tooltip->hasTitle && StrEq(tooltip->title, StrL("Jan")));
    utassert(tooltip->rows.len == 2);
    utassert(StrEq(tooltip->rows[0].value, StrL("0: +2")) &&
             ChartColorEq(tooltip->rows[0].valueColor, Rgb(0, 255, 0)));
    utassert(StrEq(tooltip->rows[1].value, StrL("1: -1")) &&
             ChartColorEq(tooltip->rows[1].valueColor, Rgb(255, 0, 0)));

    ChartTooltipContent plain;
    ChartTooltipSeriesRow alpha[1] = {{blue, StrL("Alpha"), 80.}};
    component::plot::Tooltip* untitled = ChartTooltipApply(
        plain, component::plot::Tooltip::New(&cx, {0, 0}, {100, 100}), &one, {},
        false, alpha, 1);
    utassert(!untitled->hasTitle);
    utassert(StrEq(untitled->rows[0].value, StrL("80")) && !untitled->rows[0]
                                                                .hasValueColor);
    ArenaDelete(a);
}

static int gContentCalls = 0;
static El* CallerContent(Ctx* cx, const void* d, void*) {
    gContentCalls++;
    return TextEl(cx->a, StrDup(cx->a, fmt("datum %g", *(const double*)d)));
}

// chart/mod.rs: tooltip_fill_renders_the_caller_content_without_building_rows.
static void TooltipFillRendersTheCallerContentWithoutBuildingRows() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    const double one = 1.;
    ChartTooltipContent content;
    content.title = &DayTitle;
    content.content = &CallerContent;
    ChartTooltipSeriesRow rows[1] = {{Rgb(0, 0, 255), StrL("Alpha"), 80.}};
    gContentCalls = 0;
    component::plot::Tooltip* tooltip = ChartTooltipApply(
        content, component::plot::Tooltip::New(&cx, {0, 0}, {100, 100}), &one,
        StrL("Jan"), true, rows, 1);
    utassert(gContentCalls == 1);
    utassert(!tooltip->hasTitle);
    utassert(tooltip->rows.len == 0);
    utassert(tooltip->children.len == 1 &&
             StrEq(tooltip->children[0]->text, StrL("datum 1")));
    ArenaDelete(a);
}

// radar_chart.rs: test_radar_chart_hovered_index. Bounds 200x200 => center
// (100, 100), default outer radius 80, hover region 80 + 10 (label gap).
static void RadarChartHoveredIndex() {
    Size bounds = {200, 200};
    float r = 200 * 0.4f;
    float gap = kRadarDefaultLabelGap;
    // The four spokes point at 12, 3, 6 and 9 o'clock.
    utassert(RadarHoveredIndex(4, r, gap, {100, 30}, bounds) == 0);
    utassert(RadarHoveredIndex(4, r, gap, {170, 100}, bounds) == 1);
    utassert(RadarHoveredIndex(4, r, gap, {100, 170}, bounds) == 2);
    utassert(RadarHoveredIndex(4, r, gap, {30, 100}, bounds) == 3);
    // Nearest spoke wins between two spokes.
    utassert(RadarHoveredIndex(4, r, gap, {110, 40}, bounds) == 0);
    utassert(RadarHoveredIndex(4, r, gap, {160, 90}, bounds) == 1);
    // Outside the radar.
    utassert(RadarHoveredIndex(4, r, gap, {100, 5}, bounds) == -1);
    utassert(RadarHoveredIndex(4, r, gap, {5, 5}, bounds) == -1);
    utassert(RadarHoveredIndex(0, r, gap, {100, 100}, bounds) == -1);
}

// radar_chart.rs: test_radar_chart_builder, plus series_stroke's defaults —
// each series takes the next theme chart colour, and its fill the stroke at
// 0.3 until it is given one.
static void RadarChartBuilder() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    cx.app = &app;
    const Theme& th = ThemeNow(&app);
    float as[2] = {80, 50};
    float bs[2] = {60, 90};
    Rgba red = RgbaHex(0xff0000);
    RadarChart* chart = RadarChart::New(&cx, as, 2)
                            ->Stroke(red)
                            ->Fill(red)
                            ->Tooltip(StrL("A"))
                            ->Value(bs)
                            ->MaxValue(100)
                            ->OuterRadius(120)
                            ->LabelGap(8)
                            ->Grid(false)
                            ->GridLevels(5)
                            ->Dot()
                            ->Id(StrL("radar"));
    utassert(len(chart->more) == 1);
    utassert(ChartColorEq(chart->stroke, red) &&
             ChartColorEq(chart->fill, red));
    utassert(StrEq(chart->tooltipName, StrL("A")));
    utassert(!chart->more[0].name.s);
    utassert(ChartColorEq(chart->more[0].stroke, th.chart2));
    utassert(
        ChartColorEq(chart->more[0].fillTop, RgbaOpacity(th.chart2, 0.3f)));
    utassert(chart->domainMin == 0 && chart->domainMax == 100);
    utassertnear(chart->outerRadius, 120);
    utassertnear(chart->labelGap, 8);
    utassert(!chart->grid && chart->gridLevels == 5 && chart->dot);
    utassert(chart->id == IdFoldName(cx.path, StrL("radar")));
    utassertnear(chart->ResolveOuterRadius(300), 120);

    RadarChart* plain = RadarChart::New(&cx, as, 2);
    utassert(ChartColorEq(plain->stroke, th.chart1));
    utassert(ChartColorEq(plain->fill, RgbaOpacity(th.chart1, 0.3f)));
    utassertnear(plain->ResolveOuterRadius(200), 80);
    // A later stroke carries an unset fill with it, and a set one stays.
    plain->Value(bs)->Stroke(red);
    utassert(ChartColorEq(plain->more[0].fillTop, RgbaOpacity(red, 0.3f)));
    plain->Fill(th.chart3)->Stroke(th.chart4);
    utassert(ChartColorEq(plain->more[0].fillTop, th.chart3));
    El* e = plain->IntoEl();
    utassert(e->Chart()->nMore == 1 && e->Chart()->more[0].ys == bs);
    utassert(e->customPaint != nullptr && plain->el == e);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

// bar_chart.rs: the_tooltip_swatch_follows_the_bar_color. A solid fill as is,
// a fill_gradient by its first stop, and the default fill for a ramp across
// the plot. the_tooltip_reads_the_painted_bar_frame checks Rust's frame
// arithmetic, which the colour here does not need.
static void TheTooltipSwatchFollowsTheBarColor() {
    Rgba chart2 = Rgb(1, 2, 3);
    Rgba gain = Rgb(0, 255, 0);
    Rgba loss = Rgb(255, 0, 0);
    ChartSeries bars;
    bars.kind = ChartKind::Bar;
    bars.stroke = chart2;
    utassert(ChartColorEq(ChartBarTooltipColor(bars, 0), chart2));
    Rgba fills[2] = {gain, loss};
    ChartSeries solid = bars;
    solid.barFills = fills;
    utassert(ChartColorEq(ChartBarTooltipColor(solid, 1), loss));
    ChartSeries diagonal = bars;
    diagonal.barGradient = true;
    diagonal.barGradientDiagonal = true;
    diagonal.barFillFrom = gain;
    diagonal.barFillTo = loss;
    utassert(ChartColorEq(ChartBarTooltipColor(diagonal, 0), chart2));
    ChartSeries stops = diagonal;
    stops.barGradientDiagonal = false;
    utassert(ChartColorEq(ChartBarTooltipColor(stops, 0), gain));
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
    AChartTurnedOffKeepsItsIdButNotItsHitbox();
    AnAppearKeyReplaysTheAppear();
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
    AValueColorColorsOnlyTheRowAddedLast();
    APlainRowHasNoSwatchAndTakesAValueColor();
    TooltipTextFallsBackToTheChartOwn();
    TooltipClosuresReceiveTheDatum();
    AHoveredSeriesChartBuildsItsTooltipFromTheDatum();
    TooltipFillWritesEachRowWithTheValueColor();
    TheTooltipSwatchFollowsTheBarColor();
    TooltipFillRendersTheCallerContentWithoutBuildingRows();
    RadarChartHoveredIndex();
    RadarChartBuilder();
}
