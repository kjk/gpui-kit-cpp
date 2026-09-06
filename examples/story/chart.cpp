#include "Story.h"
#include "ChartFixtures.h"

// cosf and sinf, for the radar chart's badge labels: MSVC hands them over
// with the rest of the runtime, gcc does not.
#include <math.h>

struct ChartStory {
    float variations[kMonthlyDeviceCount] = {};
    bool seeded = false;
    Size viewport = {800, 600};
    float scrollY = 0;
    static El* Render(ChartStory* self, Ctx* cx);
    static void OnScroll(ChartStory* self, Ctx* cx, const ScrollEvent* ev) {
        self->scrollY = ev->offsetY;
        Notify(cx);
    }
    static void Measure(PaintCtx* paint, El* el, void* data) {
        Entity<ChartStory> entity = *(Entity<ChartStory>*)data;
        ChartStory* self = entity.Get(paint->app);
        if (!self || (self->viewport.w == el->w && self->viewport.h == el->h))
            return;
        self->viewport = {el->w, el->h};
        Ctx cx = {paint->app, paint->window, nullptr, entity.id};
        Notify(&cx);
    }
};

// chart_container(): a 400px card with the title, the range, the chart and
// two lines of commentary.
static El* ChartCard(Ctx* cx, const char* title, El* chart, bool center) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* card = Div(a)
                   ->FlexCol()
                   ->Flex1()
                   ->MinW(0)
                   ->H(400)
                   ->Pad(16)
                   ->Radius(th.radiusLg)
                   ->Border(1, th.border);
    El* head = StoryTxt(cx, Str(title), 16, th.foreground)->Semibold();
    El* sub = StoryTxt(cx, StrL("January-June 2025"), 14, th.mutedFg);
    El* foot1 =
        StoryTxt(cx, StrL("Trending up by 5.2% this month"), 14, th.foreground)
            ->Semibold();
    El* foot2 = StoryTxt(cx,
                         StrL("Showing total visitors for the last 6 "
                              "months"),
                         14, th.mutedFg);
    if (center) {
        card->Child(Div(a)->W(kFill)->FlexRow()->JustifyCenter()->Child(head));
        card->Child(Div(a)->W(kFill)->FlexRow()->JustifyCenter()->Child(sub));
    } else {
        card->Child(head);
        card->Child(sub);
    }
    El* body = Div(a)->Flex1()->W(kFill)->PadY(16)->FlexRow();
    if (center) {
        body->ItemsCenter()->JustifyCenter();
    }
    body->Child(chart);
    card->Child(body);
    if (center) {
        card->Child(Div(a)->W(kFill)->FlexRow()->JustifyCenter()->Child(foot1));
        card->Child(Div(a)->W(kFill)->FlexRow()->JustifyCenter()->Child(foot2));
    } else {
        card->Child(foot1);
        card->Child(foot2);
    }
    return card;
}

static El* RenderChartCard(Ctx* cx, ChartStory* self, int index) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    Rgba color = th.chart3;
    switch (index) {
        case 1:
        case 2:
        case 3:
        case 4: {
            const char* titles[] = {"Pie Chart", "Pie Chart - Donut",
                                    "Pie Chart - Pad Angle",
                                    "Pie Chart - Label"};
            auto* pie = component::PieChart::New(cx)
                            ->OuterRadius(index == 4 ? 80.f : 100.f);
            if (index > 1) pie->InnerRadius(index == 4 ? 50.f : 60.f);
            if (index == 3) pie->PadAngle(4.f / 100.f);
            for (int i = 0; i < kMonthlyDeviceCount; i++) {
                pie->Slice(kMonthlyDesktop[i],
                           RgbaOpacity(color, kMonthlyAlpha[i]),
                           index == 2 ? (float)i * 4.f : 0);
                if (index == 4) pie->Label(Str(kMonthlyMonth[i]));
            }
            return ChartCard(cx, titles[index - 1], pie->IntoEl(), true);
        }
        case 0: {
            // Area Chart - Stacked: one chart with two series, desktop and
            // mobile, both from daily-devices.json —
            // `.y(..).stroke(..).fill(..).name(..)` twice over one set of axes,
            // which is what the Rust story writes.
            El* areaBox =
                component::AreaChart::New(cx, kDailyDesktop, kDailyDeviceCount)
                    ->Tooltip(StrL("Desktop"))
                    ->Stroke(th.chart1)
                    ->Fill(RgbaOpacity(th.chart1, 0.4f),
                           RgbaOpacity(th.background, 0.3f))
                    ->Y(kDailyMobile)
                    ->Stroke(th.chart2)
                    ->Fill(RgbaOpacity(th.chart2, 0.4f),
                           RgbaOpacity(th.background, 0.3f))
                    ->Tooltip(StrL("Mobile"))
                    ->Labels(kDailyDate)
                    ->TickMargin(8)
                    ->IntoEl()
                    ->W(kFill)
                    ->H(kFill);
            return ChartCard(cx, "Area Chart - Stacked", areaBox, false);
        }

        case 5: {
            // The radars, off radar-devices.json.
            return ChartCard(
                cx, "Radar Chart",
                component::RadarChart::New(cx, kRadarDesktop, kRadarDeviceCount)
                    ->Labels(kRadarMonth)
                    ->IntoEl()
                    ->W(kFill)
                    ->H(kFill),
                true);
        }

        case 6: {
            // Radar Chart - Multiple: a second ring over the first one's grid.
            El* radarMulti = Div(a)->W(kFill)->H(kFill);
            radarMulti->Child(
                component::RadarChart::New(cx, kRadarDesktop, kRadarDeviceCount)
                    ->Labels(kRadarMonth)
                    ->IntoEl()
                    ->W(kFill)
                    ->H(kFill));
            radarMulti->Child(
                component::RadarChart::New(cx, kRadarMobile, kRadarDeviceCount)
                    ->Stroke(th.chart2)
                    ->Fill(RgbaOpacity(th.chart2, 0.3f))
                    ->Overlay()
                    ->IntoEl()
                    ->Absolute()
                    ->Left(0)
                    ->Top(0)
                    ->W(kFill)
                    ->H(kFill));
            return ChartCard(cx, "Radar Chart - Multiple", radarMulti, true);
        }

        case 7: {
            // Radar Chart - Dots: an element label — the month over a grade
            // badge — so the ring pulls in to outer_radius(64.) to leave it
            // room.
            component::RadarLabel* radarLabels = (component::RadarLabel*)Alloc(
                a, sizeof(component::RadarLabel) * kRadarDeviceCount);
            for (int i = 0; i < kRadarDeviceCount; i++) {
                const char* grade = kRadarDesktop[i] >= 250.f   ? "A"
                                    : kRadarDesktop[i] >= 200.f ? "B"
                                                                : "C";
                El* badge = Div(a)->FlexCol()->ItemsCenter()->Gap(4);
                badge->Child(StoryTxt(cx, Str(kRadarMonth[i]), 12, th.mutedFg));
                badge->Child(Div(a)
                                 ->FlexRow()
                                 ->W(24)
                                 ->H(24)
                                 ->ItemsCenter()
                                 ->JustifyCenter()
                                 ->Radius(99)
                                 ->Bg(RgbaOpacity(th.chart2, 0.1f))
                                 ->Child(StoryTxt(cx, Str(grade), 14, th.chart2)
                                             ->Semibold()
                                             ->LineHeight(1.f)));
                radarLabels[i] = component::RadarLabel::Element(badge);
            }
            El* radarDots =
                component::RadarChart::New(cx, kRadarDesktop, kRadarDeviceCount)
                    ->Labels(radarLabels)
                    ->Stroke(th.chart2)
                    ->Fill(RgbaOpacity(th.chart2, 0.3f))
                    ->Dot()
                    ->OuterRadius(64)
                    ->IntoEl()
                    ->W(kFill)
                    ->H(kFill);
            return ChartCard(cx, "Radar Chart - Dots", radarDots, true);
        }

        case 8: {
            // Radar Chart - Lines Only: max_value(400) and no fill under the
            // ring.
            return ChartCard(
                cx, "Radar Chart - Lines Only",
                component::RadarChart::New(cx, kRadarDesktop, kRadarDeviceCount)
                    ->Labels(kRadarMonth)
                    ->Stroke(th.chart3)
                    ->Fill(Rgba8(0, 0, 0, 0))
                    ->Domain(0, 400)
                    ->GridLevels(5)
                    ->IntoEl()
                    ->W(kFill)
                    ->H(kFill),
                true);
        }

        case 9: {
            // The bars, off monthly-devices.json.
            return ChartCard(cx, "Bar Chart",
                             component::BarChart::New(cx, kMonthlyDesktop,
                                                      kMonthlyDeviceCount)
                                 ->Fill(th.chart1)
                                 ->Labels(kMonthlyMonth)
                                 ->Tooltip(StrL("Desktop"))
                                 ->TickMargin(1)
                                 ->IntoEl()
                                 ->W(kFill)
                                 ->H(kFill),
                             false);
        }

        case 17: {
            // Bar Chart - Negative values: the monthly figures recentred on
            // their mean, so the bars have a mix of signs to draw around the
            // zero line, and the value axis switched on beside them.
            const float* variations = self->variations;
            return ChartCard(
                cx, "Bar Chart - Negative values",
                component::BarChart::New(cx, variations, kMonthlyDeviceCount)
                    ->Fill(th.chart1)
                    ->Labels(kMonthlyMonth)
                    ->Tooltip(StrL("Variation"))
                    ->TickMargin(1)
                    ->LabelValues()
                    ->ValueAxis()
                    ->IntoEl()
                    ->W(kFill)
                    ->H(kFill),
                false);
        }

        case 10: {
            // Bar Chart - Mixed: fill(|d, ..| d.color(color)), a tint per bar.
            Rgba* mixed = (Rgba*)Alloc(a, sizeof(Rgba) * kMonthlyDeviceCount);
            for (int i = 0; i < kMonthlyDeviceCount; i++) {
                mixed[i] = RgbaOpacity(color, kMonthlyAlpha[i]);
            }
            return ChartCard(cx, "Bar Chart - Mixed",
                             component::BarChart::New(cx, kMonthlyDesktop,
                                                      kMonthlyDeviceCount)
                                 ->Fills(mixed)
                                 ->Labels(kMonthlyMonth)
                                 ->TickMargin(1)
                                 ->IntoEl()
                                 ->W(kFill)
                                 ->H(kFill),
                             false);
        }

        case 11: {
            // Bar Chart - Stacked: Stack::keys(desktop, mobile, tablet, watch)
            // over the first eight days, drawn as four series each sitting on
            // the running total of the ones below it.
            const int kStackDays = 8;
            const float* kStackSeries[4] = {kDailyDesktop, kDailyMobile,
                                            kDailyTablet, kDailyWatch};
            Rgba kStackColors[4] = {th.chart4, th.chart3, th.chart2, th.chart1};
            El* stacked = Div(a)->W(kFill)->H(kFill);
            auto* bases = (float*)Alloc(a, (int)sizeof(float) * kStackDays * 5);
            for (int d = 0; d < kStackDays; d++) {
                bases[d] = 0;
            }
            for (int k = 0; k < 4; k++) {
                float* base = bases + k * kStackDays;
                float* next = bases + (k + 1) * kStackDays;
                auto* tops = (float*)Alloc(a, (int)sizeof(float) * kStackDays);
                for (int d = 0; d < kStackDays; d++) {
                    tops[d] = base[d] + kStackSeries[k][d];
                    next[d] = tops[d];
                }
                component::BarChart* bar =
                    component::BarChart::New(cx, tops, kStackDays)
                        ->Fill(kStackColors[k])
                        ->Base(base)
                        ->Padding(0.4f)
                        ->Radius(0)
                        ->TickMargin(1)
                        ->Labels(kDailyDate);
                // Every series is scaled against the full stack, so they line
                // up.
                bar->Domain(0, bases[4 * kStackDays]);
                float top = 0;
                for (int d = 0; d < kStackDays; d++) {
                    if (bases[4 * kStackDays + d] > top) {
                        top = bases[4 * kStackDays + d];
                    }
                }
                bar->Domain(0, top);
                if (k > 0) {
                    bar->Overlay();
                }
                El* el = bar->IntoEl()->W(kFill)->H(kFill);
                if (k > 0) {
                    el->Absolute()->Left(0)->Top(0);
                }
                stacked->Child(el);
            }
            return ChartCard(cx, "Bar Chart - Stacked", stacked, false);
        }

        case 12: {
            // Bar Chart - Rounded corners: corner_radii(px(8.)).
            return ChartCard(cx, "Bar Chart - Rounded corners",
                             component::BarChart::New(cx, kMonthlyDesktop,
                                                      kMonthlyDeviceCount)
                                 ->Fill(th.chart1)
                                 ->Labels(kMonthlyMonth)
                                 ->TickMargin(1)
                                 ->Radius(8)
                                 ->LabelValues()
                                 ->IntoEl()
                                 ->W(kFill)
                                 ->H(kFill),
                             false);
        }

        case 13:
        case 14:
        case 15:
        case 16: {
            // The four alignments, all with the value written at the growing
            // end.
            struct AlignCard {
                const char* title;
                BarAlign align;
            };
            static const AlignCard kAligns[] = {
                {"Bar Chart - Bottom aligned", BarAlign::Bottom},
                {"Bar Chart - Top aligned", BarAlign::Top},
                {"Bar Chart - Left aligned", BarAlign::Left},
                {"Bar Chart - Right aligned", BarAlign::Right},
            };
            const AlignCard& ac = kAligns[index - 13];
            {
                return ChartCard(cx, ac.title,
                                 component::BarChart::New(cx, kMonthlyDesktop,
                                                          kMonthlyDeviceCount)
                                     ->Fill(th.chart1)
                                     ->Labels(kMonthlyMonth)
                                     ->TickMargin(1)
                                     ->Alignment(ac.align)
                                     ->LabelValues()
                                     ->IntoEl()
                                     ->W(kFill)
                                     ->H(kFill),
                                 false);
            }
        }

        case 18:
        case 19:
        case 20:
        case 21:
        case 22: {
            // fill_gradient: four alignments of the chart-wide ramp, then the
            // per-bar one. The sixth is fill(|_, bar, chart, _|) instead — one
            // ramp across the whole plot's diagonal, each bar showing its own
            // slice of it — so it is built below rather than in this table.
            struct GradCard {
                const char* title;
                BarAlign align;
                bool perBar;
            };
            static const GradCard kGrads[] = {
                {"Bar Chart - Gradient (Bottom)", BarAlign::Bottom, false},
                {"Bar Chart - Gradient (Top)", BarAlign::Top, false},
                {"Bar Chart - Gradient (Left)", BarAlign::Left, false},
                {"Bar Chart - Gradient (Right)", BarAlign::Right, false},
                {"Bar Chart - Gradient (Per-bar)", BarAlign::Bottom, true},
            };
            const GradCard& gc = kGrads[index - 18];
            {
                return ChartCard(
                    cx, gc.title,
                    component::BarChart::New(cx, kMonthlyDesktop,
                                             kMonthlyDeviceCount)
                        ->Labels(kMonthlyMonth)
                        ->TickMargin(1)
                        ->Alignment(gc.align)
                        ->LabelValues()
                        ->FillGradient(RgbaOpacity(th.chart1, 0.3f), th.chart1,
                                       gc.perBar)
                        ->IntoEl()
                        ->W(kFill)
                        ->H(kFill),
                    false);
            }
        }

        case 23: {
            return ChartCard(cx, "Bar Chart - Gradient (Diagonal, across bars)",
                             component::BarChart::New(cx, kMonthlyDesktop,
                                                      kMonthlyDeviceCount)
                                 ->Labels(kMonthlyMonth)
                                 ->TickMargin(1)
                                 ->LabelValues()
                                 ->FillGradientDiagonal(th.chart1, th.chart5)
                                 ->IntoEl()
                                 ->W(kFill)
                                 ->H(kFill),
                             false);
        }

        case 24: {
            return ChartCard(cx, "Line Chart - Tooltip",
                             component::LineChart::New(cx, kMonthlyDesktop,
                                                       kMonthlyDeviceCount)
                                 ->Stroke(th.chart1)
                                 ->Labels(kMonthlyMonth)
                                 ->Tooltip(StrL("Desktop"))
                                 ->TickMargin(1)
                                 ->IntoEl()
                                 ->W(kFill)
                                 ->H(kFill),
                             false);
        }

        case 25: {
            return ChartCard(cx, "Line Chart - Linear",
                             component::LineChart::New(cx, kMonthlyDesktop,
                                                       kMonthlyDeviceCount)
                                 ->Stroke(th.chart1)
                                 ->Labels(kMonthlyMonth)
                                 ->TickMargin(1)
                                 ->Linear()
                                 ->IntoEl()
                                 ->W(kFill)
                                 ->H(kFill),
                             false);
        }

        case 26: {
            return ChartCard(cx, "Line Chart - Step After",
                             component::LineChart::New(cx, kMonthlyDesktop,
                                                       kMonthlyDeviceCount)
                                 ->Stroke(th.chart1)
                                 ->Labels(kMonthlyMonth)
                                 ->TickMargin(1)
                                 ->StepAfter()
                                 ->IntoEl()
                                 ->W(kFill)
                                 ->H(kFill),
                             false);
        }

        case 27: {
            return ChartCard(cx, "Line Chart - Dots",
                             component::LineChart::New(cx, kMonthlyDesktop,
                                                       kMonthlyDeviceCount)
                                 ->Stroke(th.chart5)
                                 ->Labels(kMonthlyMonth)
                                 ->TickMargin(1)
                                 ->Dot()
                                 ->IntoEl()
                                 ->W(kFill)
                                 ->H(kFill),
                             false);
        }

        case 28:
        case 29:
        case 30:
        case 31: {
            // The four single-series area charts, which differ only in how the
            // run of points is joined and what is under it.
            struct AreaCard {
                const char* title;
                int stroke; // 0 natural, 1 linear, 2 step-after
                bool gradient;
            };
            static const AreaCard kAreas[] = {
                {"Area Chart", 0, false},
                {"Area Chart - Linear", 1, false},
                {"Area Chart - Step After", 2, false},
                {"Area Chart - Linear Gradient", 0, true},
            };
            const AreaCard& ac = kAreas[index - 28];
            {
                component::AreaChart* ch =
                    component::AreaChart::New(cx, kMonthlyDesktop,
                                              kMonthlyDeviceCount)
                        ->Stroke(th.chart1)
                        ->Labels(kMonthlyMonth)
                        ->TickMargin(1);
                if (ac.stroke == 1) {
                    ch->Linear();
                } else if (ac.stroke == 2) {
                    ch->StepAfter();
                }
                if (ac.gradient) {
                    ch->Fill(RgbaOpacity(th.chart1, 0.4f),
                             RgbaOpacity(th.background, 0.3f));
                } else {
                    ch->Fill(RgbaOpacity(th.chart1, 0.2f));
                }
                return ChartCard(cx, ac.title, ch->IntoEl()->W(kFill)->H(kFill),
                                 false);
            }
        }

        case 32: {
            // The candlesticks, off stock-prices.json.
            return ChartCard(cx, "Candlestick Chart",
                             component::CandlestickChart::New(
                                 cx, kStockOpen, kStockHigh, kStockLow,
                                 kStockClose, kStockPriceCount)
                                 ->Colors(th.chartBullish, th.chartBearish)
                                 ->Labels(kStockDate)
                                 ->TickMargin(1)
                                 ->IntoEl()
                                 ->W(kFill)
                                 ->H(kFill),
                             false);
        }

        case 33:
        case 34:
        case 35: {
            // body_width_ratio: half a band, then the whole of it.
            struct CandleCard {
                const char* title;
                float ratio;
                int tickMargin;
            };
            static const CandleCard kCandles[] = {
                {"Candlestick Chart - Narrow", 0.5f, 1},
                {"Candlestick Chart - Wide", 1.0f, 1},
                {"Candlestick Chart - Tick Margin", 0.8f, 2},
            };
            const CandleCard& cc = kCandles[index - 33];
            {
                return ChartCard(cx, cc.title,
                                 component::CandlestickChart::New(
                                     cx, kStockOpen, kStockHigh, kStockLow,
                                     kStockClose, kStockPriceCount)
                                     ->Colors(th.chartBullish, th.chartBearish)
                                     ->Labels(kStockDate)
                                     ->TickMargin(cc.tickMargin)
                                     ->BodyWidthRatio(cc.ratio)
                                     ->IntoEl()
                                     ->W(kFill)
                                     ->H(kFill),
                                 false);
            }
        }

        case 36:
        case 37: {
            // The two TSLA income statements, each a sankey of its own. A sqrt
            // value scale keeps the revenue flow from dwarfing the small profit
            // and expense ones, and the nodes carry the fixture's own colours.
            const TslaNode* kTslaNodes[kTslaStatementCount] = {kTsla0Nodes,
                                                               kTsla1Nodes};
            const TslaLink* kTslaLinks[kTslaStatementCount] = {kTsla0Links,
                                                               kTsla1Links};
            int st = index - 36;
            {
                component::SankeyChart* sk =
                    component::SankeyChart::New(cx)
                        ->NodeAlign(SankeyAlign::Center)
                        ->NodePadding(40)
                        ->ValueScale(SankeyValueScale::Sqrt);
                for (int i = 0; i < kTslaNodeCount; i++) {
                    const TslaNode& node = kTslaNodes[st][i];
                    sk->NodeColored(Str(node.name), node.color);
                    // The first statement's labels carry the year-over-year
                    // change between the value and the name; the second keeps
                    // the two default lines.
                    Str value =
                        StoryFmt(cx, "$%.2fB", node.value / 1000000000.0);
                    if (st == 0) {
                        sk->CustomLabel(component::SankeyLabel::New(value));
                        if (node.growth != kTslaNoGrowth) {
                            bool up = node.growth >= 0;
                            sk->CustomLabel(
                                component::SankeyLabel::New(
                                    StoryFmt(
                                        cx, "%s %+.2f%%",
                                        up ? "\xE2\x96\xB2" : "\xE2\x96\xBC",
                                        (double)node.growth))
                                    .Color(up ? th.success : th.danger));
                        }
                        sk->CustomLabel(
                            component::SankeyLabel::New(Str(node.name))
                                .Color(th.mutedFg));
                    } else {
                        sk->NodeValue(value);
                    }
                }
                for (int i = 0; i < kTslaLinkCount; i++) {
                    const TslaLink& link = kTslaLinks[st][i];
                    sk->Link(link.source, link.target, link.value);
                }
                return ChartCard(
                    cx,
                    StoryFmt(cx, "Sankey Chart - TSLA %s", kTslaPeriods[st]).s,
                    sk->IntoEl()->W(kFill)->H(kFill), false);
            }
        }
        default:
            return Div(a);
    }
}

El* ChartStory::Render(ChartStory* self, Ctx* cx) {
    if (!self->seeded) {
        self->seeded = true;
        float sum = 0;
        for (float v : kMonthlyDesktop) sum += v;
        for (int i = 0; i < kMonthlyDeviceCount; i++)
            self->variations[i] =
                (float)lroundf(kMonthlyDesktop[i] - sum / kMonthlyDeviceCount);
    }
    // chart_story.rs: 400px cards, 16px gaps/inset, 280px minimum width.
    int columns =
        std::max(1, (int)floorf((self->viewport.w - 32 + 16) / (280 + 16)));
    struct Row {
        int first;
        int count;
    };
    // Eight fixed fixture sections; separators are rows in the same list.
    const int sectionCounts[] = {1, 4, 4, 15, 4, 4, 4, kTslaStatementCount};
    constexpr int kMaxRows = 36 + kTslaStatementCount + 6;
    Row rows[kMaxRows];
    float sizes[kMaxRows];
    int count = 0;
    int card = 0;
    for (int section = 0; section < 8; section++) {
        if (section >= 2) {
            rows[count] = {-1, 0};
            sizes[count++] = 1 + 16;
        }
        for (int i = 0; i < sectionCounts[section]; i += columns) {
            rows[count] = {card + i,
                           std::min(columns, sectionCounts[section] - i)};
            sizes[count++] = 400 + 16;
        }
        card += sectionCounts[section];
    }
    sizes[count - 1] -= 16;
    float total = 32 + VirtualListContentSize(sizes, count);
    self->scrollY =
        std::max(0.f, std::min(self->scrollY, total - self->viewport.h));
    // One card's height of overscan on both sides, as upstream ListState.
    VirtualRange visible = VirtualListVisibleRange(
        sizes, count, self->scrollY - 16 - 400, self->viewport.h + 800);
    El* content = Div(cx->a)->W(kFill)->H(total)->Shrink0();
    float y = 16 + VirtualListItemOrigin(sizes, count, visible.first);
    for (int i = visible.first; i < visible.end; i++) {
        El* row = Div(cx->a)
                      ->Absolute()
                      ->Left(16)
                      ->Right(16)
                      ->Top(y)
                      ->H(rows[i].first < 0 ? 1.f : 400.f)
                      ->FlexRow()
                      ->Gap(16);
        if (rows[i].first < 0) {
            row->Child(component::Separator::Horizontal(cx)->IntoEl());
        } else {
            for (int col = 0; col < rows[i].count; col++) {
                // Retained fixtures are shared; chart builders run only for
                // cards in the visible range and its overscan.
                IdScope scope(cx,
                              StoryFmt(cx, "chart-%d", rows[i].first + col));
                row->Child(RenderChartCard(cx, self, rows[i].first + col));
            }
        }
        content->Child(row);
        y += sizes[i];
    }
    El* root = Div(cx->a)
                   ->SizeFull()
                   ->MinH(0)
                   ->ClipY()
                   ->ScrollY(self->scrollY)
                   ->ScrollId(HashClickId(StrL("chart-gallery")))
                   ->OnScroll(Listen(cx, &ChartStory::OnScroll))
                   ->Child(content);
    auto* owner = ArenaNew<Entity<ChartStory>>(cx->a);
    owner->id = cx->self;
    root->customPaint = &ChartStory::Measure;
    root->customUser = owner;
    return root;
}

STORY_PAGE(StoryChart, ChartStory);
