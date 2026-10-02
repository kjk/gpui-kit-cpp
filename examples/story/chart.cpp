#include "Story.h"
#include "ChartFixtures.h"

// lroundf and floorf: MSVC hands them over with the rest of the runtime, gcc
// does not.
#include <math.h>

struct ChartStory {
    float variations[kMonthlyDeviceCount] = {};
    bool seeded = false;
    Size viewport = {800, 600};
    float scrollY = 0;
    // Bumped by the replay button. The gallery is keyed on it, so every
    // chart gets fresh element state and draws in again.
    uint64_t appearGeneration = 0;
    static El* Render(ChartStory* self, Ctx* cx);
    static void OnReplay(ChartStory* self, Ctx* cx, const ClickEvent*) {
        self->appearGeneration++;
        Notify(cx);
    }
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

static float ChangePercent(float latest, float previous) {
    if (previous < 0.0001f && previous > -0.0001f) {
        return 0;
    }
    return (latest - previous) / (previous < 0 ? -previous : previous) * 100.f;
}

static float LatestChange(const float* values, int n) {
    if (n < 2) {
        return 0;
    }
    return ChangePercent(values[n - 1], values[n - 2]);
}

// chart_story.rs::windowed_change: compare the last seven combined daily
// visitor totals with the seven before them, not just the last two desktops.
static float VisitorWeekChange() {
    const int window = 7;
    double latest = 0, previous = 0;
    for (int i = kDailyDeviceCount - window; i < kDailyDeviceCount; i++)
        latest += (double)kDailyDesktop[i] + kDailyMobile[i];
    for (int i = kDailyDeviceCount - window * 2; i < kDailyDeviceCount - window;
         i++)
        previous += (double)kDailyDesktop[i] + kDailyMobile[i];
    return previous == 0 ? 0 : (float)((latest - previous) / previous * 100.);
}

static float SumF(const float* values, int n) {
    float s = 0;
    for (int i = 0; i < n; i++) {
        s += values[i];
    }
    return s;
}

static const char* Compact(Ctx* cx, float value) {
    float mag = value < 0 ? -value : value;
    if (mag >= 1000000.f) {
        return StoryFmt(cx, "%.1fM", (double)(value / 1000000.f)).s;
    }
    if (mag >= 10000.f) {
        return StoryFmt(cx, "%.0fK", (double)(value / 1000.f)).s;
    }
    if (mag >= 1000.f) {
        return StoryFmt(cx, "%.1fK", (double)(value / 1000.f)).s;
    }
    return StoryFmt(cx, "%.0f", (double)value).s;
}

static const char* Money(Ctx* cx, float value) {
    if (value < 0) {
        return StoryFmt(cx, "-$%s", Compact(cx, -value)).s;
    }
    return StoryFmt(cx, "$%s", Compact(cx, value)).s;
}

// money, as a chart's tick format: compact with a dollar sign, the sign in
// front of it.
static Str MoneyTick(Arena* a, double value, void*) {
    double mag = value < 0 ? -value : value;
    Str sign = value < 0 ? StrL("-") : StrL("");
    if (mag >= 1000000.0) {
        return StrDup(a, fmt("%s$%.1fM", sign, mag / 1000000.0));
    }
    if (mag >= 10000.0) {
        return StrDup(a, fmt("%s$%.0fK", sign, mag / 1000.0));
    }
    if (mag >= 1000.0) {
        return StrDup(a, fmt("%s$%.1fK", sign, mag / 1000.0));
    }
    return StrDup(a, fmt("%s$%.0f", sign, mag));
}

// tooltip_value(|_, value| money(value)): a tooltip row's value in money.
static Str MoneyValue(Arena* a, const void*, int, double value, void*) {
    return MoneyTick(a, value, nullptr);
}

// tooltip_value(|_, _, value| format!("${value:.2}")): a price to the cent.
static Str PriceValue(Arena* a, const void*, int, double value, void*) {
    return StrDup(a, fmt("$%.2f", value));
}

// tooltip_value(|_, _, value| format!("{value:.0} / 100")).
static Str OutOfHundred(Arena* a, const void*, int, double value, void*) {
    return StrDup(a, fmt("%.0f / 100", value));
}

// tooltip_title(|d| format!("{} 2025", d.month)): the chart's data is the
// month names, so the datum is one.
static Str MonthOf2025(Arena* a, const void* d, void*) {
    return StrDup(a, fmt("%s 2025", Str(*(const char* const*)d)));
}

// tooltip_value_color: the bullish colour for a gain, the bearish one for a
// loss; `user` is the pair.
static Rgba SignColor(const void*, int, double value, void* user) {
    const Rgba* colors = (const Rgba*)user;
    return value >= 0 ? colors[0] : colors[1];
}

static const char* TrendLine(Ctx* cx, float percent, const char* period) {
    const char* dir = percent >= 0 ? "up" : "down";
    float mag = percent < 0 ? -percent : percent;
    return StoryFmt(cx, "Trending %s by %.1f%% %s", dir, (double)mag, period).s;
}

static int ColorIndex(const char* name) {
    if (StrEq(Str(name), StrL("Direct")) || StrEq(Str(name), StrL("Chrome")) ||
        StrEq(Str(name), StrL("Free")) ||
        StrEq(Str(name), StrL("N. America"))) {
        return 0;
    }
    if (StrEq(Str(name), StrL("Organic Search")) ||
        StrEq(Str(name), StrL("Safari")) || StrEq(Str(name), StrL("Starter")) ||
        StrEq(Str(name), StrL("Europe"))) {
        return 1;
    }
    if (StrEq(Str(name), StrL("Social")) || StrEq(Str(name), StrL("Edge")) ||
        StrEq(Str(name), StrL("Pro")) || StrEq(Str(name), StrL("APAC"))) {
        return 2;
    }
    if (StrEq(Str(name), StrL("Referral")) ||
        StrEq(Str(name), StrL("Firefox")) ||
        StrEq(Str(name), StrL("Enterprise")) ||
        StrEq(Str(name), StrL("LatAm"))) {
        return 3;
    }
    return 4;
}

static Rgba Shade(Rgba base, int index) {
    float a = 1.f - 0.14f * (float)index;
    if (a < 0.3f) {
        a = 0.3f;
    }
    return RgbaOpacity(base, a);
}

struct ChartLegend {
    Rgba color;
    const char* label;
};

// chart_story.rs legend: it shares the heading row with the title, so it
// yields width rather than holding its own — shrinking lets the wrap fold a
// long series list onto another line instead of running out past the card.
// Each swatch-and-label pair keeps its width, so a wrap never parts them.
static El* LegendRow(Ctx* cx, const ChartLegend* legend, int n, bool center) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* row = Div(a)->FlexRow()->FlexWrap()->Gap(12);
    if (center) {
        row->JustifyCenter();
    } else {
        row->JustifyEnd();
    }
    for (int i = 0; i < n; i++) {
        El* item = Div(a)->FlexRow()->Shrink0()->Gap(6)->ItemsCenter();
        item->Child(Div(a)->W(8)->H(8)->Radius(2)->Bg(legend[i].color));
        item->Child(StoryTxt(cx, Str(legend[i].label), 12, th.mutedFg));
        row->Child(item);
    }
    return row;
}

// A 400px card: heading, optional legend, the chart and a footer that reads
// the data — chart_story.rs Card.
static El* ChartCard(Ctx* cx, const char* title, const char* period, El* chart,
                     bool center, const char* headline, const char* note,
                     const ChartLegend* legend = nullptr, int nLegend = 0) {
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
    // The heading holds its width; the legend beside it is what gives way
    // and wraps.
    El* titles = Div(a)->FlexCol()->Shrink0();
    if (center) {
        titles->ItemsCenter();
    }
    titles->Child(StoryTxt(cx, Str(title), 16, th.foreground)->Semibold());
    titles->Child(StoryTxt(cx, Str(period), 14, th.mutedFg));
    El* head = Div(a)->FlexRow()->ItemsStart()->JustifyBetween();
    if (center) {
        head->JustifyCenter();
    }
    head->Child(titles);
    if (legend && nLegend > 0 && !center) {
        head->Child(LegendRow(cx, legend, nLegend, false));
    }
    card->Child(head);
    if (legend && nLegend > 0 && center) {
        card->Child(
            Div(a)->PadT(8)->Child(LegendRow(cx, legend, nLegend, true)));
    }
    El* body = Div(a)->Flex1()->MinH(0)->W(kFill)->PadY(16);
    if (center) {
        body->FlexRow()->ItemsCenter()->JustifyCenter();
    }
    body->Child(chart);
    card->Child(body);
    El* foot1 = StoryTxt(cx, Str(headline), 14, th.foreground)->Semibold();
    if (StrStartsWith(Str(headline), "Trending ")) {
        bool down = StrStartsWith(Str(headline), "Trending down");
        Str arrow =
            down ? StrL(
                       "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" "
                       "height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" "
                       "stroke=\"currentColor\" stroke-width=\"2\" "
                       "stroke-linecap=\"round\" "
                       "stroke-linejoin=\"round\"><path d=\"M16 "
                       "17h6v-6\"/><path d=\"m22 17-8.5-8.5-5 5L2 7\"/></svg>")
                 : StrL(
                       "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" "
                       "height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" "
                       "stroke=\"currentColor\" stroke-width=\"2\" "
                       "stroke-linecap=\"round\" "
                       "stroke-linejoin=\"round\"><path d=\"m22 7-8.5 "
                       "8.5-5-5L2 17\"/><path d=\"M16 7h6v6\"/></svg>");
        foot1 = Div(a)->FlexRow()->ItemsCenter()->Gap(6)->Child(foot1)->Child(
            component::Icon::Empty(cx)
                ->Data(arrow)
                ->Size(16)
                ->Color(down ? th.red : th.green)
                ->IntoEl());
    }
    El* foot2 = StoryTxt(cx, Str(note), 14, th.mutedFg);
    if (center) {
        card->Child(Div(a)->W(kFill)->FlexRow()->JustifyCenter()->Child(foot1));
        card->Child(Div(a)->W(kFill)->FlexRow()->JustifyCenter()->Child(foot2));
    } else {
        card->Child(foot1);
        card->Child(foot2);
    }
    return card;
}

static El* ChartCard(Ctx* cx, const char* title, El* chart, bool center) {
    return ChartCard(cx, title, "2025", chart, center,
                     "Trending up by 5.2% this month",
                     "Showing total visitors for the last 6 months");
}

// stacked_bar_chart.rs: "You can draw any chart you want by using the Plot."
// A custom plot is an element that paints itself with the plot primitives
// and, for a hover, tracks it under its own id and hands its tooltip overlay
// back to the element it paints (plot::PlotOverlayAttach).
struct StackedDevices {
    const char* date;
    float desktop;
    float mobile;
    float tablet;
    float watch;
};

struct StackedBarPlot {
    StackedDevices days[8] = {};
    int n = 0;
    // Plot::id: a single demo instance, so a fixed id is fine.
    uint32_t id = 0;
    ArenaVec<component::plot::StackSeries> series;
};

static const char* const kStackKeys[4] = {"desktop", "mobile", "tablet",
                                          "watch"};

static bool StackedValue(const void* item, int, Str key, void*, float* out) {
    const StackedDevices* d = (const StackedDevices*)item;
    if (StrEq(key, StrL("desktop"))) {
        *out = d->desktop;
    } else if (StrEq(key, StrL("mobile"))) {
        *out = d->mobile;
    } else if (StrEq(key, StrL("tablet"))) {
        *out = d->tablet;
    } else if (StrEq(key, StrL("watch"))) {
        *out = d->watch;
    } else {
        return false;
    }
    return true;
}

// The height kept under the plot for the x-axis labels, drawn at the default
// label size.
static float StackedAxisGap() {
    return component::plot::AxisGutter(component::plot::kPlotTextSize);
}

static component::plot::ScaleBand StackedBand(int n, float width) {
    const float range[2] = {0.f, width};
    component::plot::ScaleBand x = component::plot::ScaleBand::New(n, range, 2)
                                       .MaxBandWidth(30.f);
    x.paddingInner = 0.4f;
    x.paddingOuter = 0.2f;
    return x;
}

struct StackedScales {
    component::plot::ScaleBand x;
    component::plot::ScaleLinear y;
    float height = 0;
    const StackedDevices* first = nullptr;
};

static bool StackedCross(const void* item, int, void* user, float* out) {
    const StackedScales* s = (const StackedScales*)user;
    const auto* p = (const component::plot::StackPoint*)item;
    int day = (int)((const StackedDevices*)p->data - s->first);
    return s->x.Tick(day, out);
}

static bool StackedBase(const void* item, int, void* user, float* out) {
    const StackedScales* s = (const StackedScales*)user;
    const auto* p = (const component::plot::StackPoint*)item;
    if (!s->y.Tick(p->y0, out)) {
        *out = s->height;
    }
    return true;
}

static bool StackedTop(const void* item, int, void* user, float* out) {
    const StackedScales* s = (const StackedScales*)user;
    const auto* p = (const component::plot::StackPoint*)item;
    return s->y.Tick(p->y1, out);
}

static Background StackedFill(const void*, int, Bounds,
                              component::plot::BarAlignment, void* user) {
    return Background(*(const Rgba*)user);
}

static void StackedColors(const Theme& th, Rgba out[4]) {
    out[0] = th.chart4;
    out[1] = th.chart3;
    out[2] = th.chart2;
    out[3] = th.chart1;
}

static void PaintStackedBars(PaintCtx* ctx, El* e, void* user) {
    auto* p = (StackedBarPlot*)user;
    if (!p || !ctx->app || p->n <= 0) {
        return;
    }
    const Theme& th = ThemeNow(ctx->app);
    Arena* scratch = GetTempArena();
    Bounds bounds = e->Bounds();
    float width = bounds.w;
    float height = bounds.h - StackedAxisGap();

    // 2. Calculate X/Y scales
    StackedScales scales;
    scales.x = StackedBand(p->n, width);
    scales.height = height;
    scales.first = p->days;
    float bandWidth = scales.x.BandWidth();
    float max = 0;
    for (const component::plot::StackSeries& s : p->series) {
        for (const component::plot::StackPoint& pt : s.points) {
            max = pt.y1 > max ? pt.y1 : max;
        }
    }
    const float domain[2] = {0.f, max};
    const float range[2] = {height, 10.f};
    scales.y = component::plot::ScaleLinear::New(domain, 2, range, 2);

    // 3. Draw X axis labels
    ArenaVec<component::plot::AxisText> labels;
    for (int i = 0; i < p->n; i++) {
        float tick = 0;
        if (scales.x.Tick(i, &tick)) {
            component::plot::AxisText text = component::plot::AxisText::New(
                Str(p->days[i].date), tick + bandWidth / 2.f, th.mutedFg);
            text.Align(component::plot::PlotTextAlign::Center);
            labels.Append(scratch, text);
        }
    }
    component::plot::PlotAxis::New(scratch)
        .X(height)
        ->XLabel(labels.Flatten(scratch), len(labels))
        ->Stroke(th.border)
        ->Paint(ctx, bounds);

    // 4. Setup color scale
    Rgba colors[4];
    StackedColors(th, colors);

    // 5. Draw grid lines
    float gridY[4];
    for (int i = 0; i < 4; i++) {
        gridY[i] = height * (float)i / 4.f;
    }
    const float dash[2] = {4.f, 2.f};
    component::plot::Grid::New()
        .Y(gridY, 4)
        ->Stroke(th.border)
        ->DashArray(dash, 2)
        ->Paint(ctx, bounds);

    // 6. Draw stacked bars
    for (const component::plot::StackSeries& s : p->series) {
        component::plot::Bar::New()
            .Data(s.points.Flatten(scratch), len(s.points),
                  (int)sizeof(component::plot::StackPoint))
            ->BandWidth(bandWidth)
            ->Cross(StackedCross, &scales)
            ->Base(StackedBase, &scales)
            ->Value(StackedTop, &scales)
            ->Fill(StackedFill, &colors[s.index % 4])
            ->Paint(ctx, bounds);
    }

    // The hover: tooltip_state then tooltip, as PlotElement drives them.
    if (!ctx->window || !ctx->window->frameArena) {
        return;
    }
    Ctx idCx = {};
    idCx.app = ctx->app;
    idCx.win = ctx->window;
    idCx.path = p->id;
    Point cursor = {ctx->mouseX - bounds.x, ctx->mouseY - bounds.y};
    bool inside = cursor.x >= 0 && cursor.y >= 0 && cursor.x <= bounds.w &&
                  cursor.y <= bounds.h;
    component::plot::TooltipState live = {};
    const component::plot::TooltipState* livePtr = nullptr;
    // Ignore the x-axis label gutter so hovering the labels doesn't show a
    // tooltip.
    if (inside && cursor.y <= height) {
        int index = scales.x.NearestIndex(cursor.x);
        float tick = 0;
        if (index >= 0 && index < p->n && scales.x.Tick(index, &tick)) {
            live = component::plot::TooltipState::New(
                index, {tick + bandWidth / 2.f, cursor.y}, nullptr, 0);
            livePtr = &live;
        }
    }
    component::plot::PlotHover hover = {};
    Point linger = cursor;
    if (!component::plot::TrackHover(
            &idCx, livePtr, livePtr ? &cursor : nullptr, &hover, &linger)) {
        return;
    }
    const component::plot::TooltipState& held = hover.State();
    if (held.index < 0 || held.index >= p->n) {
        return;
    }
    Ctx buildCx = idCx;
    buildCx.a = ctx->window->frameArena;
    // Highlight the hovered column with a translucent band the width of the
    // bars, confined to the plot height so it doesn't cover the x-axis
    // labels. The overlay fades in and out with the hover, and the band
    // glides between columns, on its own.
    component::plot::CrossLine band =
        component::plot::CrossLine::New(held.crossLine);
    band.Height(height)->Band(bandWidth);
    component::plot::Tooltip* tooltip =
        component::plot::Tooltip::New(&buildCx, linger, {bounds.w, bounds.h})
            ->Gap(8)
            ->Cross(band)
            ->Title(Str(p->days[held.index].date));
    // One row per stacked series (its segment value at this band).
    double total = 0;
    for (const component::plot::StackSeries& s : p->series) {
        float value = held.index < len(s.points)
                          ? s.points[held.index].y1 - s.points[held.index].y0
                          : 0.f;
        total += value;
        tooltip->Row(colors[s.index % 4], s.key,
                     ChartFormatValue(buildCx.a, value));
    }
    tooltip->PlainRow(StrL("total"), ChartFormatValue(buildCx.a, total));
    component::plot::PlotOverlayAttach(ctx, e, bounds, tooltip->IntoEl());
}

static El* StackedBarChartEl(Ctx* cx, int days) {
    StackedBarPlot* p = ArenaNew<StackedBarPlot>(cx->a);
    p->n = days < 8 ? days : 8;
    for (int i = 0; i < p->n; i++) {
        p->days[i] = {kDailyDate[i], kDailyDesktop[i], kDailyMobile[i],
                      kDailyTablet[i], kDailyWatch[i]};
    }
    p->id = IdFoldName(cx->path, StrL("stacked-bar-chart"));
    // 1. Calculate the stacked data
    Str* keys = (Str*)Alloc(cx->a, (int)sizeof(Str) * 4);
    for (int k = 0; k < 4; k++) {
        keys[k] = Str(kStackKeys[k]);
    }
    component::plot::Stack::New()
        .Data(p->days, p->n, (int)sizeof(StackedDevices))
        ->Keys(keys, 4)
        ->Value(StackedValue)
        ->Series(cx->a, &p->series);
    El* e = Div(cx->a)->W(kFill)->H(kFill);
    e->customPaint = PaintStackedBars;
    e->customUser = p;
    return e;
}

// AreaGradient's tooltip_content: the month in semibold over this year, last
// year and the change between them, the change in the bullish or bearish
// colour by its sign.
// One month of the metrics, the datum the gradient chart's tooltip reads.
struct MetricDatum {
    const char* month = nullptr;
    float revenue = 0;
    float lastYear = 0;
};

static El* RevenueVsLastYear(Ctx* cx, const void* d, void*) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    const MetricDatum* m = (const MetricDatum*)d;
    float revenue = m->revenue;
    float lastYear = m->lastYear;
    float change = ChangePercent(revenue, lastYear);
    Rgba changeColor = change >= 0 ? th.chartBullish : th.chartBearish;
    auto row = [&](const char* label, Str value) {
        return Div(a)
            ->FlexRow()
            ->JustifyBetween()
            ->Gap(16)
            ->Child(TextEl(a, Str(label))->Fg(th.mutedFg))
            ->Child(TextEl(a, value));
    };
    return Div(a)
        ->FlexCol()
        ->Gap(4)
        ->Child(TextEl(a, Str(m->month))->Semibold())
        ->Child(row("2025", Str(Money(cx, revenue))))
        ->Child(row("2024", Str(Money(cx, lastYear))))
        ->Child(row("Change", StrDup(a, fmt("%+.1f%%", (double)change)))
                    ->Fg(changeColor));
}

static El* RenderChartCard(Ctx* cx, ChartStory* self, int index) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    Rgba color = th.chart3;
    switch (index) {
        case 1: {
            float total = SumF(kTrafficVisitors, kTrafficCount);
            int top = 0;
            for (int i = 1; i < kTrafficCount; i++) {
                if (kTrafficVisitors[i] > kTrafficVisitors[top]) {
                    top = i;
                }
            }
            auto* pie = component::PieChart::New(cx)
                            ->Tooltip(StrL("Visitors"))
                            ->OuterRadius(90.f);
            ChartLegend legend[kTrafficCount];
            for (int i = 0; i < kTrafficCount; i++) {
                Rgba c = Shade(color, ColorIndex(kTrafficSource[i]));
                pie->Slice(kTrafficVisitors[i], c);
                legend[i] = {c, kTrafficSource[i]};
            }
            return ChartCard(
                cx, "Traffic Sources", "June 2025", pie->IntoEl(), true,
                StoryFmt(cx, "%s brings %.0f%% of traffic", kTrafficSource[top],
                         (double)(kTrafficVisitors[top] / total * 100.f))
                    .s,
                StoryFmt(cx, "%s visitors across five channels",
                         Compact(cx, total))
                    .s,
                legend, kTrafficCount);
        }
        case 2: {
            auto* pie = component::PieChart::New(cx)
                            ->Tooltip(StrL("Share"))
                            ->InnerRadius(58.f)
                            ->OuterRadius(90.f);
            ChartLegend legend[kBrowserCount];
            for (int i = 0; i < kBrowserCount; i++) {
                Rgba c = Shade(color, ColorIndex(kBrowserName[i]));
                pie->Slice(kBrowserShare[i], c);
                legend[i] = {c, kBrowserName[i]};
            }
            El* donut = Div(a)->W(180)->H(180)->Child(pie->IntoEl());
            donut
                ->Child(Div(a)
                            ->Absolute()
                            ->Left(0)
                            ->Top(0)
                            ->W(180)
                            ->H(180)
                            ->FlexCol()
                            ->ItemsCenter()
                            ->JustifyCenter()
                            ->Child(StoryTxt(cx,
                                             StoryFmt(cx, "%.0f%%",
                                                      (double)kBrowserShare[0]),
                                             24, th.foreground)
                                        ->Semibold())
                            ->Child(StoryTxt(cx, Str(kBrowserName[0]), 12,
                                             th.mutedFg)));
            return ChartCard(
                cx, "Browser Share", "June 2025", donut, true,
                StoryFmt(cx, "%s leads by %.0f points", kBrowserName[0],
                         (double)(kBrowserShare[0] - kBrowserShare[1]))
                    .s,
                "Share of sessions by browser family", legend, kBrowserCount);
        }
        case 3: {
            float total = SumF(kPlanAccounts, kPlanCount);
            float paid = total - kPlanAccounts[0];
            auto* pie = component::PieChart::New(cx)
                            ->Tooltip(StrL("Accounts"))
                            ->InnerRadius(56.f)
                            ->OuterRadius(90.f)
                            ->PadAngle(4.f / 100.f);
            ChartLegend legend[kPlanCount];
            for (int i = 0; i < kPlanCount; i++) {
                Rgba c = Shade(color, ColorIndex(kPlanName[i]));
                pie->Slice(kPlanAccounts[i], c);
                legend[i] = {c, kPlanName[i]};
            }
            return ChartCard(
                cx, "Plan Mix", "June 2025", pie->IntoEl(), true,
                StoryFmt(cx, "%.0f%% of accounts are on a paid plan",
                         (double)(paid / total * 100.f))
                    .s,
                StoryFmt(cx, "%s accounts in total", Compact(cx, total)).s,
                legend, kPlanCount);
        }
        case 4: {
            float total = SumF(kRegionRevenue, kRegionCount);
            auto* pie = component::PieChart::New(cx)
                            ->Tooltip(StrL("Revenue"))
                            ->InnerRadius(48.f)
                            ->OuterRadius(76.f);
            for (int i = 0; i < kRegionCount; i++) {
                pie->Slice(kRegionRevenue[i],
                           Shade(color, ColorIndex(kRegionName[i])));
                pie->Label(Str(kRegionName[i]));
                pie->TooltipName(Str(kRegionName[i]));
            }
            return ChartCard(
                cx, "Revenue by Region", "Q2 2025", pie->IntoEl(), true,
                StoryFmt(cx, "%s of %s comes from the two largest regions",
                         Money(cx, kRegionRevenue[0] + kRegionRevenue[1]),
                         Money(cx, total))
                    .s,
                "Recognized revenue, in US dollars");
        }
        case 0: {
            ChartLegend legend[] = {{th.chart2, "Desktop"},
                                    {th.chart4, "Mobile"}};
            El* areaBox =
                component::AreaChart::New(cx, kDailyDesktop, kDailyDeviceCount)
                    ->Tooltip(StrL("Desktop"))
                    ->Stroke(th.chart2)
                    ->Fill(RgbaOpacity(th.chart2, 0.45f),
                           RgbaOpacity(th.chart2, 0.f))
                    ->Y(kDailyMobile)
                    ->Stroke(th.chart4)
                    ->Fill(RgbaOpacity(th.chart4, 0.45f),
                           RgbaOpacity(th.chart4, 0.f))
                    ->Tooltip(StrL("Mobile"))
                    ->Labels(kDailyDate)
                    ->TickMargin(8)
                    ->IntoEl()
                    ->W(kFill)
                    ->H(kFill);
            float visitors = 0;
            for (int i = 0; i < kDailyDeviceCount; i++) {
                visitors += kDailyDesktop[i] + kDailyMobile[i];
            }
            return ChartCard(
                cx, "Visitors", "April – June 2025", areaBox, false,
                TrendLine(cx, VisitorWeekChange(), "this week"),
                StoryFmt(cx, "%s visitors over the last three months",
                         Compact(cx, visitors))
                    .s,
                legend, 2);
        }

        case 5: {
            float average = SumF(kScoreAlpha, kScoreCount) / (float)kScoreCount;
            return ChartCard(
                cx, "Product Score", "Alpha, Q2 review",
                component::RadarChart::New(cx, kScoreAlpha, kScoreCount)
                    ->Labels(kScoreDim)
                    ->Stroke(th.chart2)
                    ->Fill(RgbaOpacity(th.chart2, 0.3f))
                    ->Tooltip(StrL("Alpha"))
                    ->MaxValue(100)
                    ->TooltipValue(OutOfHundred)
                    ->Id(StrL("radar-chart"))
                    ->IntoEl()
                    ->W(kFill)
                    ->H(kFill),
                true, StoryFmt(cx, "Scores %.0f on average", (double)average).s,
                "Six review dimensions, scored out of 100");
        }

        case 6: {
            float alpha = SumF(kScoreAlpha, kScoreCount);
            float beta = SumF(kScoreBeta, kScoreCount);
            const ChartLegend legend[] = {{th.chart2, "Alpha"},
                                          {th.chart4, "Beta"}};
            return ChartCard(
                cx, "Alpha vs Beta", "Q2 review",
                component::RadarChart::New(cx, kScoreAlpha, kScoreCount)
                    ->Labels(kScoreDim)
                    ->Stroke(th.chart2)
                    ->Fill(RgbaOpacity(th.chart2, 0.25f))
                    ->Tooltip(StrL("Alpha"))
                    ->Value(kScoreBeta)
                    ->Stroke(th.chart4)
                    ->Fill(RgbaOpacity(th.chart4, 0.25f))
                    ->Tooltip(StrL("Beta"))
                    ->MaxValue(100)
                    ->Id(StrL("radar-chart-multiple"))
                    ->IntoEl()
                    ->W(kFill)
                    ->H(kFill),
                true,
                alpha >= beta
                    ? StoryFmt(cx, "Alpha leads by %.0f points overall",
                               (double)(alpha - beta))
                          .s
                    : StoryFmt(cx, "Beta leads by %.0f points overall",
                               (double)(beta - alpha))
                          .s,
                "Alpha wins on usability, Beta on reliability", legend, 2);
        }

        case 7: {
            // An element label: the dimension name over a grade badge, so the
            // ring pulls in to outer_radius(64.) to leave it room.
            component::RadarLabel* radarLabels = (component::RadarLabel*)Alloc(
                a, sizeof(component::RadarLabel) * kScoreCount);
            for (int i = 0; i < kScoreCount; i++) {
                const char* grade = kScoreAlpha[i] >= 85.f   ? "A"
                                    : kScoreAlpha[i] >= 70.f ? "B"
                                                             : "C";
                El* badge = Div(a)->FlexCol()->ItemsCenter()->Gap(4);
                badge->Child(StoryTxt(cx, Str(kScoreDim[i]), 12, th.mutedFg));
                badge->Child(Div(a)
                                 ->FlexRow()
                                 ->W(24)
                                 ->H(24)
                                 ->ItemsCenter()
                                 ->JustifyCenter()
                                 ->Radius(99)
                                 ->Bg(RgbaOpacity(th.chart2, 0.1f))
                                 ->Child(StoryTxt(cx, Str(grade), 14, th.chart2)
                                             ->Semibold()));
                radarLabels[i] = component::RadarLabel::Element(badge);
            }
            El* radarDots =
                component::RadarChart::New(cx, kScoreAlpha, kScoreCount)
                    ->Labels(radarLabels)
                    ->Tooltip(StrL("Alpha"))
                    ->Stroke(th.chart2)
                    ->Fill(RgbaOpacity(th.chart2, 0.25f))
                    ->MaxValue(100)
                    ->Dot()
                    ->OuterRadius(64)
                    ->Id(StrL("radar-chart-dots"))
                    ->IntoEl()
                    ->W(kFill)
                    ->H(kFill);
            return ChartCard(cx, "Review Grades", "Alpha, Q2 review", radarDots,
                             true, "Two dimensions graded A",
                             "A from 85, B from 70, C below");
        }

        case 8: {
            const ChartLegend legend[] = {{th.chart4, "Beta"}};
            return ChartCard(
                cx, "Beta Profile", "Q2 review",
                component::RadarChart::New(cx, kScoreBeta, kScoreCount)
                    ->Labels(kScoreDim)
                    ->Tooltip(StrL("Beta"))
                    ->Stroke(th.chart4)
                    ->Fill(Rgba8(0, 0, 0, 0))
                    ->MaxValue(100)
                    ->GridLevels(5)
                    ->Id(StrL("radar-chart-lines-only"))
                    ->IntoEl()
                    ->W(kFill)
                    ->H(kFill),
                true, "Strongest on reliability and support",
                "Outline only, five grid rings", legend, 1);
        }

        case 9: {
            return ChartCard(
                cx, "Monthly Revenue", "2025",
                component::BarChart::New(cx, kMetricRevenue, kMetricCount)
                    ->Fill(th.chart2)
                    ->Labels(kMetricMonth)
                    ->Tooltip(StrL("Revenue"))
                    ->Radius(6)
                    ->TickMargin(1)
                    ->ValueAxis()
                    ->ValueTickCount(3)
                    ->ValueTickFormat(&MoneyTick)
                    ->GridDashed(false)
                    ->BandTickCount(6)
                    ->IntoEl()
                    ->W(kFill)
                    ->H(kFill),
                false,
                TrendLine(cx, LatestChange(kMetricRevenue, kMetricCount),
                          "this month"),
                StoryFmt(cx, "%s recognized this year",
                         Money(cx, SumF(kMetricRevenue, kMetricCount)))
                    .s);
        }

        case 17: {
            // Bar Chart - Negative values: the monthly figures recentred on
            // their mean, so the bars have a mix of signs to draw around the
            // zero line, and the value axis switched on beside them.
            // Each bar, and its label, in the bullish or bearish colour of
            // its sign.
            const float* variations = self->variations;
            Rgba* signs = (Rgba*)Alloc(a, sizeof(Rgba) * kMonthlyDeviceCount);
            Rgba* signColors = (Rgba*)Alloc(a, sizeof(Rgba) * 2);
            signColors[0] = th.chartBullish;
            signColors[1] = th.chartBearish;
            for (int i = 0; i < kMonthlyDeviceCount; i++) {
                signs[i] =
                    variations[i] >= 0 ? th.chartBullish : th.chartBearish;
            }
            return ChartCard(
                cx, "Bar Chart - Negative values",
                component::BarChart::New(cx, variations, kMonthlyDeviceCount)
                    ->Fills(signs)
                    ->LabelColors(signs)
                    ->Labels(kMonthlyMonth)
                    ->Tooltip(StrL("Variation"))
                    ->Data(kMonthlyMonth)
                    ->TooltipTitle(&MonthOf2025)
                    ->TooltipValue(&MoneyValue)
                    ->TooltipValueColor(&SignColor, signColors)
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
                                 ->PaddingInner(0.6f)
                                 ->PaddingOuter(0.1f)
                                 ->IntoEl()
                                 ->W(kFill)
                                 ->H(kFill),
                             false);
        }

        case 11: {
            // Visitors by Device: a custom Plot (stacked_bar_chart.rs) over
            // the first eight days.
            float total = 0;
            for (int i = 0; i < 8; i++) {
                total += kDailyDesktop[i] + kDailyMobile[i] + kDailyTablet[i] +
                         kDailyWatch[i];
            }
            const ChartLegend legend[] = {{th.chart4, "Desktop"},
                                          {th.chart3, "Mobile"},
                                          {th.chart2, "Tablet"},
                                          {th.chart1, "Watch"}};
            return ChartCard(
                cx, "Visitors by Device", "First week of April",
                StackedBarChartEl(cx, 8), false,
                StoryFmt(cx, "%s visitors in eight days", Compact(cx, total)).s,
                "Stacked by device, a custom Plot", legend, 4);
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
                component::BarChart* bar = component::BarChart::New(
                    cx, kMonthlyDesktop, kMonthlyDeviceCount);
                // Rust's Downloads card labels four of its bands.
                if (index == 18) {
                    bar->BandTickCount(4);
                }
                return ChartCard(
                    cx, gc.title,
                    bar->Labels(kMonthlyMonth)
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
                                 ->YAxis()
                                 ->YTickFormat(&MoneyTick)
                                 ->XTickCount(4)
                                 ->TooltipValue(&MoneyValue)
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

        case 30: {
            float revenue = SumF(kMetricRevenue, kMetricCount);
            float lastYear = SumF(kMetricLastYear, kMetricCount);
            const ChartLegend legend[] = {{th.chart2, "2025"},
                                          {th.chart1, "2024"}};
            MetricDatum* metrics =
                (MetricDatum*)Alloc(a, (int)sizeof(MetricDatum) * kMetricCount);
            for (int i = 0; i < kMetricCount; i++) {
                metrics[i] = {kMetricMonth[i], kMetricRevenue[i],
                              kMetricLastYear[i]};
            }
            El* area =
                component::AreaChart::New(cx, kMetricLastYear, kMetricCount)
                    ->Labels(kMetricMonth)
                    ->Stroke(th.chart1)
                    ->Fill(RgbaOpacity(th.chart1, 0.45f),
                           RgbaOpacity(th.chart1, 0.f))
                    ->Tooltip(StrL("2024"))
                    ->Y(kMetricRevenue)
                    ->Stroke(th.chart2)
                    ->Fill(RgbaOpacity(th.chart2, 0.45f),
                           RgbaOpacity(th.chart2, 0.f))
                    ->Tooltip(StrL("2025"))
                    ->Data(metrics)
                    ->TooltipContent(RevenueVsLastYear)
                    ->Id(StrL("area-chart-gradient"))
                    ->IntoEl()
                    ->W(kFill)
                    ->H(kFill);
            return ChartCard(cx, "Revenue vs Last Year", "2025", area, false,
                             TrendLine(cx, ChangePercent(revenue, lastYear),
                                       "year over year"),
                             "Gradient fills fade to the baseline", legend, 2);
        }

        case 28:
        case 29: {
            // The four single-series area charts, which differ only in how the
            // run of points is joined and what is under it.
            struct AreaCard {
                const char* title;
                int stroke; // 0 natural, 1 linear, 2 step-after
                bool gradient;
            };
            static const AreaCard kAreas[] = {
                // Rust's Storage Used step-after card is gone (upstream
                // 8ed5dd50).
                {"Area Chart", 0, false},
                {"Area Chart - Linear", 1, false},
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

        case 31: {
            // Intraday Price: the first four fifths of a trading day's minute
            // prices, on an axis laid out for the whole session and a y axis
            // pinned a quarter of the range below the low.
            const int kMinutes = kIntradayCount * 4 / 5;
            float low = kIntradayPrice[0];
            float high = kIntradayPrice[0];
            for (int i = 1; i < kMinutes; i++) {
                low = std::min(low, kIntradayPrice[i]);
                high = std::max(high, kIntradayPrice[i]);
            }
            float open = kIntradayPrice[0];
            float last = kIntradayPrice[kMinutes - 1];
            return ChartCard(
                cx, "Intraday Price", "Today, in progress",
                component::AreaChart::New(cx, kIntradayPrice, kMinutes)
                    ->Labels(kIntradayTime)
                    ->Stroke(th.chart2)
                    ->Fill(RgbaOpacity(th.chart2, 0.45f),
                           RgbaOpacity(th.chart2, 0.f))
                    ->Linear()
                    ->YDomain(low - (high - low) / 4.f, high)
                    ->PointCount(kIntradayCount)
                    ->XTickCount(4)
                    ->Tooltip(StrL("Price"))
                    ->Id(StrL("area-chart-in-progress"))
                    ->IntoEl()
                    ->W(kFill)
                    ->H(kFill),
                false,
                TrendLine(cx, ChangePercent(last, open), "since the open"),
                "A pinned y axis, and room for the minutes still to come");
        }

        case 32: {
            // The candlesticks, off stock-prices.json. Forty sessions do not
            // fit forty labels, so every card thins them.
            return ChartCard(cx, "Candlestick Chart",
                             component::CandlestickChart::New(
                                 cx, kStockOpen, kStockHigh, kStockLow,
                                 kStockClose, kStockPriceCount)
                                 ->Tooltip(StrL("Price"))
                                 ->TooltipValue(&PriceValue)
                                 ->Colors(th.chartBullish, th.chartBearish)
                                 ->Labels(kStockDate)
                                 ->TickMargin(5)
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
                {"Candlestick Chart - Narrow", 0.5f, 5},
                {"Candlestick Chart - Wide", 1.0f, 5},
                {"Candlestick Chart - Tick Margin", 0.8f, 10},
            };
            const CandleCard& cc = kCandles[index - 33];
            {
                return ChartCard(cx, cc.title,
                                 component::CandlestickChart::New(
                                     cx, kStockOpen, kStockHigh, kStockLow,
                                     kStockClose, kStockPriceCount)
                                     ->Tooltip(StrL("Price"))
                                     ->TooltipValue(&PriceValue)
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
                        // `labels` draws the node text but never reaches
                        // the tooltip, so the tooltip needs its own name and
                        // value.
                        sk->TooltipName(Str(node.name))->TooltipValue(value);
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
    // The list's bottom inset only: the toolbar above holds the top gap.
    float total = 16 + VirtualListContentSize(sizes, count);
    self->scrollY =
        std::max(0.f, std::min(self->scrollY, total - self->viewport.h));
    // One card's height of overscan on both sides, as upstream ListState.
    VirtualRange visible = VirtualListVisibleRange(
        sizes, count, self->scrollY - 400, self->viewport.h + 800);
    El* content = Div(cx->a)->W(kFill)->H(total)->Shrink0();
    float y = VirtualListItemOrigin(sizes, count, visible.first);
    // ElementId::NamedInteger("chart-gallery", appear_generation).
    IdScope gallery(cx, StoryFmt(cx, "chart-gallery-%llu",
                                 (unsigned long long)self->appearGeneration));
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
    El* list = Div(cx->a)
                   ->Flex1()
                   ->W(kFill)
                   ->MinH(0)
                   ->ClipY()
                   ->ScrollY(self->scrollY)
                   ->ScrollId(HashClickId(StrL("chart-gallery")))
                   ->OnScroll(Listen(cx, &ChartStory::OnScroll))
                   ->Child(content);
    auto* owner = ArenaNew<Entity<ChartStory>>(cx->a);
    owner->id = cx->self;
    list->customPaint = &ChartStory::Measure;
    list->customUser = owner;
    // The toolbar stays put while the gallery scrolls under it, so the gap
    // below it belongs to the toolbar, not to the list's padding.
    El* group = StoryToolbarGroup(cx);
    group->Child(StoryToolbarButton(cx, StrL("chart-replay"),
                                    IconName::RotateCw, StrL("Replay"),
                                    Listen(cx, &ChartStory::OnReplay)));
    El* toolbar = Div(cx->a)->W(kFill)->PadX(16)->PadT(16)->PadB(16)->Child(
        Div(cx->a)->FlexRow()->W(kFill)->JustifyEnd()->Child(group));
    return Div(cx->a)->FlexCol()->SizeFull()->Child(toolbar)->Child(list);
}

STORY_PAGE(StoryChart, ChartStory);
