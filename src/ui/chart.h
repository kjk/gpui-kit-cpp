#ifndef GPUI_UI_CHART_H_
#define GPUI_UI_CHART_H_
/* Themed charts — crates/ui/src/chart */

#include "ui/sizing.h"
#include "ui/sankey.h"

namespace gpui {

namespace component {

namespace plot {
struct Tooltip;
}

// One row a series chart's tooltip shows: its swatch, its name and the
// number it reads, before TooltipContent writes the number out.
struct ChartTooltipSeriesRow {
    Rgba swatch = {};
    Str name = {};
    double value = 0;
    // `value` is the caller's double, so it reads in f64's Display.
    bool f64 = false;
};

// chart/mod.rs TooltipContent::apply: write `tooltip` for datum `d` — the
// chart's title, when it has one (`hasTitle`) or the caller gave one, and
// one row per series with the caller's value text and colour.
plot::Tooltip* ChartTooltipApply(const ChartTooltipContent& content,
                                 plot::Tooltip* tooltip, const void* d,
                                 Str title, bool hasTitle,
                                 const ChartTooltipSeriesRow* rows, int count);

// chart/mod.rs MAX_BAND_WIDTH: the widest a bar or candle is by default, in
// pixels, however few bands share the width. Base's ScaleBand no longer caps
// a band; the charts do.
const float kChartMaxBandWidth = 30;
// The size of the dot marking the hovered data point.
const float kChartHoverDotSize = 8;
// HOVER_HALO_SIZE: the ring behind the hovered dot at full focus; the hover
// grows it out of the dot as it fades in.
const float kChartHoverHaloSize = 20;

// chart/mod.rs caller_id: the ElementId a chart carries when the caller names
// none — the source location it was constructed at (Rust's
// ElementId::CodeLocation through #[track_caller]; here the call site's
// __builtin_FILE / __builtin_LINE), folded onto the id stack it was built
// under, which is what a GlobalElementId is. The hover state and the tooltip
// key on it, so a chart built at a site written out once is interactive
// without being handed an id; one site building several sibling charts has
// them share one, which is what Id is for.
uint32_t ChartCallerId(const Ctx* cx, const char* file, int line);

// chart/mod.rs ChartAppear: whether a chart's data draws in the first time it
// is painted, and the key that replays it. Rust also holds this frame's
// PlotAppear here; the charts here are painted by the runtime after they are
// built, so the paint samples it (plot::TrackAppear) and nothing holds it.
struct ChartAppear {
    bool enabled = true;
    uint64_t generation = 0;

    void SetEnabled(bool v) { enabled = v; }
    // set_key: Rust hashes any `impl Hash`; a string key hashes its bytes
    // (FNV-1a) and a number is its own generation.
    void SetKey(Str key);
    void SetKey(uint64_t key) { generation = key; }
    // The generation a chart hands Plot::appear_generation, or false when it
    // opted out, so no appear is tracked and no frames are asked for.
    bool Generation(uint64_t* out) const {
        if (enabled && out) {
            *out = generation;
        }
        return enabled;
    }
};

// A pie or donut: each slice is a value and a color, drawn clockwise from
// twelve o'clock (crates/ui/src/chart/pie_chart.rs).
struct PieSlice {
    float value = 0;
    Rgba color = {};
    // outer_radius_fn lets a slice pull in from the rim.
    float outerInset = 0;
    Str label = {};
    // tooltip_name / tooltip_value: the hover row's name and value for this
    // slice. Unset, the row takes the chart's name and `value (share%)`.
    Str tooltipName = {};
    Str tooltipValue = {};
};

struct PieChart {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<PieSlice> slices;
    // 0 until OuterRadius is set: the ring is then 40% of the laid-out
    // height (resolve_outer_radius).
    float outerRadius = 0;
    float innerRadius = 0;
    float padAngle = 0;
    // label(): a name outside the ring, on a leader line from the slice's own
    // edge. Set on the chart rather than the slice because a slice with no
    // name is simply left unlabelled.
    bool hasLabels = false;
    // DEFAULT_LABEL_GAP: how far past the outer radius the names sit.
    float labelGap = 15;
    bool hasLabelColor = false;
    Rgba labelColor = {};
    Str tooltipName = {};
    // The chart's ElementId, folded onto the id stack it was built under: its
    // construction site unless Id renamed it (chart/mod.rs caller_id).
    uint32_t id = 0;
    // interactive(..): the hitbox under the cursor and what it drives -- the
    // hover emphasis and the tooltip. On by default.
    bool interactive = true;
    // appear(..) / appear_key(..): whether the data draws in the first time
    // the chart is painted, and the key that replays it.
    ChartAppear appear = {};

    static PieChart* New(Ctx* cx, const char* file = __builtin_FILE(),
                         int line = __builtin_LINE());
    PieChart* Slice(float value, Rgba color, float outerInset = 0);
    PieChart* Label(Str text);
    PieChart* OuterRadius(float r);
    PieChart* InnerRadius(float r);
    PieChart* PadAngle(float radians);
    PieChart* LabelGap(float gap);
    PieChart* LabelColor(Rgba c);
    // name(..): what the tooltip calls the hovered slice's series.
    PieChart* Tooltip(Str name);
    // tooltip_name(..): the slice just added names itself in the hover row,
    // without `Label` drawing leader lines around the ring.
    PieChart* TooltipName(Str name);
    // tooltip_value(..): the row's value text for the slice just added —
    // for a value that is already a ratio, or one drawn from an adjusted
    // number that should not be reported as the datum.
    PieChart* TooltipValue(Str value);
    // id(..): rename the chart's ElementId, replacing the construction site.
    // Needed where one site builds several of these as siblings, which would
    // otherwise share one hover state. Unique among those siblings.
    PieChart* Id(Str name);
    // interactive(false): stand the chart down. Without its hitbox it
    // neither answers the mouse nor takes the hover from an element drawn
    // over it -- a loading skeleton, an empty-state ring.
    PieChart* Interactive(bool v);
    // appear(..): draw the data in the first time this chart is painted. On
    // by default; the theme sets how long it takes and reduced motion skips
    // it. Turn it off for a chart painted again and again as it scrolls in
    // and out of view, such as one in each row of a long list.
    PieChart* Appear(bool v);
    // appear_key(..): draw the data in again whenever `key` changes, such as
    // the symbol or period a chart shows. Without one the data draws in
    // once, and later data paints in place.
    PieChart* AppearKey(Str key);
    PieChart* AppearKey(uint64_t key);
    // Plot::id: the id the chart keeps its state under, its appear and, when
    // interactive, its hover.
    uint32_t PlotId() const { return id; }
    // Plot::interactive: whether it tracks hover and shows its tooltip.
    bool PlotInteractive() const { return interactive; }
    // Plot::appear_generation: false when the chart opted out.
    bool AppearGeneration(uint64_t* out) const {
        return appear.Generation(out);
    }
    // The outer radius the ring is laid out with: the set one, or 40% of
    // `height`.
    float ResolveOuterRadius(float height) const;
    El* IntoEl();
};

// chart/mod.rs PointAxes: the grid, value-axis labels and reference lines a
// point chart (LineChart, AreaChart) draws, which both builders forward to.
// ChartSeries carries them to the paint.
struct PointAxes {
    bool yAxis = false;
    AxisLabelPlacement placement = AxisLabelPlacement::Outside;
    int yTickCount = 5;
    ChartTickFormatFn tickFormat = nullptr;
    void* tickFormatUser = nullptr;
    // -1 is None: every TickMargin-th point is labelled.
    int xTickCount = -1;
    int gridColumns = 0;
    bool gridDashed = true;
    float yPaddingTop = 10;
    float yPaddingBottom = 0;
    ArenaVec<double> referenceLines;

    void ApplyTo(Arena* a, ChartSeries* chart) const;
};

struct AreaChart {
    Arena* a = nullptr;
    Str tooltipName = {};
    // chart/mod.rs TooltipContent: tooltip_title / tooltip_value /
    // tooltip_value_color.
    ChartTooltipContent tooltipContent = {};
    // The chart's ElementId, folded onto the id stack it was built under: its
    // construction site unless Id renamed it (chart/mod.rs caller_id).
    uint32_t id = 0;
    // interactive(..): the hitbox under the cursor and what it drives -- the
    // hover emphasis and the tooltip. On by default.
    bool interactive = true;
    // appear(..) / appear_key(..): whether the data draws in the first time
    // the chart is painted, and the key that replays it.
    ChartAppear appear = {};
    Ctx* cx = nullptr;
    const float* ys = nullptr;
    // The values as the caller gave them, when it gave doubles (the New
    // overload): what the chart writes out -- a tooltip's numbers, a bar's
    // label -- reads them, in f64's Display. Its geometry stays float.
    const double* exact = nullptr;
    int n = 0;
    const char* const* labels = nullptr;
    // tick_margin: 1, every point named, as Rust's charts default to.
    int tickMargin = 1;
    // A stacked chart draws its second series over the first one's grid.
    bool overlay = false;
    Rgba stroke = {};
    Rgba fill = {};
    // The bottom stop, when the caller gave the two-stop gradient Rust builds
    // with linear_gradient(0., ..). Alpha 0 means "fade `fill` out".
    Rgba fillBottom = {};
    ChartStroke strokeStyle = ChartStroke::Natural;
    // The series after the first, in the order `Y()` named them.
    ArenaVec<ChartSeriesExtra> more;
    bool hasYDomain = false;
    float yDomainMin = 0;
    float yDomainMax = 0;
    int pointCount = 0;
    PointAxes axes;
    // x_axis(..) / grid(..): both on by default.
    bool xAxis = true;
    bool grid = true;

    static AreaChart* New(Ctx* cx, const float* ys, int n,
                          const char* file = __builtin_FILE(),
                          int line = __builtin_LINE());
    // The same over doubles, which the chart keeps for the numbers it
    // writes out (`exact`); it draws them as floats.
    static AreaChart* New(Ctx* cx, const double* ys, int n,
                          const char* file = __builtin_FILE(),
                          int line = __builtin_LINE());
    // `.y(..)`: another series over the same axes. The `Stroke`, `Fill` and
    // `Tooltip` after it belong to that series, the way Rust's chain does.
    AreaChart* Y(const float* ys);
    // name(..): what the tooltip calls the series.
    AreaChart* Tooltip(Str name);
    // tooltip_title: the tooltip's title for the datum, instead of its
    // label.
    AreaChart* TooltipTitle(ChartTooltipTitleFn fn, void* user = nullptr);
    // tooltip_value: each row's value text; the raw number by default. `row`
    // is the series' index in the order `Y` added them.
    AreaChart* TooltipValue(ChartTooltipValueFn fn, void* user = nullptr);
    // tooltip_value_color: each row's value colour, such as green or red by
    // its sign; the tooltip's text colour by default.
    AreaChart* TooltipValueColor(ChartTooltipValueColorFn fn,
                                 void* user = nullptr);
    // tooltip_content: draw the box's content for a datum yourself, in place
    // of the title and rows. The crosshair, the dots and where the box sits
    // stay the chart's.
    AreaChart* TooltipContent(ChartTooltipContentFn fn, void* user = nullptr);
    // data(..): the items the chart's points are, one per point, which the
    // tooltip closures above receive as their datum in place of the chart's
    // own number. Rust's charts hold `data` and map it; these hold the
    // numbers, and this is what Rust's closures would have read them from.
    AreaChart* Data(const void* items, int stride);
    template <typename T>
    AreaChart* Data(const T* items) {
        return Data((const void*)items, (int)sizeof(T));
    }
    // id(..): rename the chart's ElementId, replacing the construction site.
    // Needed where one site builds several of these as siblings, which would
    // otherwise share one hover state. Unique among those siblings.
    AreaChart* Id(Str name);
    // interactive(false): stand the chart down. Without its hitbox it
    // neither answers the mouse nor takes the hover from an element drawn
    // over it -- a loading skeleton, an empty-state ring.
    AreaChart* Interactive(bool v);
    // appear(..): draw the data in the first time this chart is painted. On
    // by default; the theme sets how long it takes and reduced motion skips
    // it. Turn it off for a chart painted again and again as it scrolls in
    // and out of view, such as one in each row of a long list.
    AreaChart* Appear(bool v);
    // appear_key(..): draw the data in again whenever `key` changes, such as
    // the symbol or period a chart shows. Without one the data draws in
    // once, and later data paints in place.
    AreaChart* AppearKey(Str key);
    AreaChart* AppearKey(uint64_t key);
    // Plot::id: the id the chart keeps its state under, its appear and, when
    // interactive, its hover.
    uint32_t PlotId() const { return id; }
    // Plot::interactive: whether it tracks hover and shows its tooltip.
    bool PlotInteractive() const { return interactive; }
    // Plot::appear_generation: false when the chart opted out.
    bool AppearGeneration(uint64_t* out) const {
        return appear.Generation(out);
    }
    AreaChart* Stroke(Rgba c);
    AreaChart* Fill(Rgba c);
    // fill(linear_gradient(0., stop(bottom, 0.), stop(top, 1.))).
    AreaChart* Fill(Rgba top, Rgba bottom);
    AreaChart* Labels(const char* const* l);
    AreaChart* TickMargin(int n);
    AreaChart* Overlay(bool v = true);
    // StrokeStyle: Natural is the default Catmull-Rom curve.
    AreaChart* Natural();
    AreaChart* Linear();
    // x_axis(false): no x axis line or labels, and no room kept for them.
    AreaChart* XAxis(bool v);
    // grid(false): no grid lines.
    AreaChart* Grid(bool v);
    AreaChart* StepAfter();
    // y_domain(min, max): pin the y axis to min..max instead of fitting every
    // series from zero, where zero is not a meaningful baseline (a price line).
    // The range keeps the 10 DIPs above max the default leaves, and the series
    // are clipped to the plot, so a value outside the range stops at its
    // edge. Nothing is drawn when min equals max.
    AreaChart* YDomain(float min, float max);
    // point_count(count): lay the x axis out for `count` evenly spaced
    // points instead of the data's own length. The data takes the leading
    // points in order and the rest stay empty, as an intraday chart does
    // before the close. A count below the data's length has no effect.
    AreaChart* PointCount(int count);
    // y_axis: show the y axis's tick labels, one at each of the y ticks.
    // Default false.
    AreaChart* YAxis(bool v = true);
    // y_axis_label_placement: in a gutter left of the plot (Outside, the
    // default), or inside it beside their grid lines.
    AreaChart* YAxisLabelPlacement(AxisLabelPlacement placement);
    // y_tick_count: how many ticks the y axis carries, evenly spaced from the
    // baseline to the top edge with both ends included. They place the
    // horizontal grid lines and the labels, each reading the value the scale
    // puts at its height. At least 2; default 5.
    AreaChart* YTickCount(int count);
    // y_tick_format: the text of each y-axis tick label from its value.
    AreaChart* YTickFormat(ChartTickFormatFn format, void* user = nullptr);
    // x_tick_count: label `count` of the x values, spread evenly from the
    // first to the last, instead of every TickMargin-th. With PointCount they
    // spread over every point the axis is laid out for, so they keep their
    // places as the data grows.
    AreaChart* XTickCount(int count);
    // grid_columns: divide the plot into `count` columns with vertical grid
    // lines, the first on its left edge. Default 0.
    AreaChart* GridColumns(int count);
    // grid_dashed: default true.
    AreaChart* GridDashed(bool dashed);
    // reference_line: a dashed line across the plot at `value`, such as a
    // previous close. Call again for more; one outside the y axis is not
    // drawn.
    AreaChart* ReferenceLine(double value);
    // y_padding: the space kept clear above the highest value and below the
    // lowest. Default 10 above, none below.
    AreaChart* YPadding(float top, float bottom);
    El* IntoEl();
};

// LineChart: the same run of points as an area chart, with nothing filled
// under it.
struct LineChart {
    Arena* a = nullptr;
    Str tooltipName = {};
    // chart/mod.rs TooltipContent: tooltip_title / tooltip_value /
    // tooltip_value_color.
    ChartTooltipContent tooltipContent = {};
    // The chart's ElementId, folded onto the id stack it was built under: its
    // construction site unless Id renamed it (chart/mod.rs caller_id).
    uint32_t id = 0;
    // interactive(..): the hitbox under the cursor and what it drives -- the
    // hover emphasis and the tooltip. On by default.
    bool interactive = true;
    // appear(..) / appear_key(..): whether the data draws in the first time
    // the chart is painted, and the key that replays it.
    ChartAppear appear = {};
    Ctx* cx = nullptr;
    const float* ys = nullptr;
    // The values as the caller gave them, when it gave doubles (the New
    // overload): what the chart writes out -- a tooltip's numbers, a bar's
    // label -- reads them, in f64's Display. Its geometry stays float.
    const double* exact = nullptr;
    int n = 0;
    const char* const* labels = nullptr;
    // tick_margin: 1, every point named, as Rust's charts default to.
    int tickMargin = 1;
    Rgba stroke = {};
    bool hasYDomain = false;
    float yDomainMin = 0;
    float yDomainMax = 0;
    int pointCount = 0;
    PointAxes axes;
    ChartStroke strokeStyle = ChartStroke::Natural;
    bool dot = false;
    // x_axis(..) / grid(..): both on by default.
    bool xAxis = true;
    bool grid = true;

    static LineChart* New(Ctx* cx, const float* ys, int n,
                          const char* file = __builtin_FILE(),
                          int line = __builtin_LINE());
    // The same over doubles, which the chart keeps for the numbers it
    // writes out (`exact`); it draws them as floats.
    static LineChart* New(Ctx* cx, const double* ys, int n,
                          const char* file = __builtin_FILE(),
                          int line = __builtin_LINE());
    // name(..): what the tooltip calls the series.
    LineChart* Tooltip(Str name);
    // tooltip_title: the tooltip's title for the datum, instead of its
    // label.
    LineChart* TooltipTitle(ChartTooltipTitleFn fn, void* user = nullptr);
    // tooltip_value: each row's value text; the raw number by default. `row`
    // is always 0; Rust's closure takes no row index for the one series.
    LineChart* TooltipValue(ChartTooltipValueFn fn, void* user = nullptr);
    // tooltip_value_color: each row's value colour, such as green or red by
    // its sign; the tooltip's text colour by default.
    LineChart* TooltipValueColor(ChartTooltipValueColorFn fn,
                                 void* user = nullptr);
    // tooltip_content: draw the box's content for a datum yourself, in place
    // of the title and rows. The crosshair, the dots and where the box sits
    // stay the chart's.
    LineChart* TooltipContent(ChartTooltipContentFn fn, void* user = nullptr);
    // data(..): the items the chart's points are, one per point, which the
    // tooltip closures above receive as their datum in place of the chart's
    // own number. Rust's charts hold `data` and map it; these hold the
    // numbers, and this is what Rust's closures would have read them from.
    LineChart* Data(const void* items, int stride);
    template <typename T>
    LineChart* Data(const T* items) {
        return Data((const void*)items, (int)sizeof(T));
    }
    // id(..): rename the chart's ElementId, replacing the construction site.
    // Needed where one site builds several of these as siblings, which would
    // otherwise share one hover state. Unique among those siblings.
    LineChart* Id(Str name);
    // interactive(false): stand the chart down. Without its hitbox it
    // neither answers the mouse nor takes the hover from an element drawn
    // over it -- a loading skeleton, an empty-state ring.
    LineChart* Interactive(bool v);
    // appear(..): draw the data in the first time this chart is painted. On
    // by default; the theme sets how long it takes and reduced motion skips
    // it. Turn it off for a chart painted again and again as it scrolls in
    // and out of view, such as one in each row of a long list.
    LineChart* Appear(bool v);
    // appear_key(..): draw the data in again whenever `key` changes, such as
    // the symbol or period a chart shows. Without one the data draws in
    // once, and later data paints in place.
    LineChart* AppearKey(Str key);
    LineChart* AppearKey(uint64_t key);
    // Plot::id: the id the chart keeps its state under, its appear and, when
    // interactive, its hover.
    uint32_t PlotId() const { return id; }
    // Plot::interactive: whether it tracks hover and shows its tooltip.
    bool PlotInteractive() const { return interactive; }
    // Plot::appear_generation: false when the chart opted out.
    bool AppearGeneration(uint64_t* out) const {
        return appear.Generation(out);
    }
    LineChart* Stroke(Rgba c);
    LineChart* Labels(const char* const* l);
    LineChart* TickMargin(int n);
    // y_domain(min, max): pin the y axis to min..max instead of fitting the
    // line from zero, where zero is not a meaningful baseline (a price line).
    // The range keeps the 10 DIPs above max the default leaves, and the series
    // are clipped to the plot, so a value outside the range stops at its
    // edge. Nothing is drawn when min equals max.
    LineChart* YDomain(float min, float max);
    // point_count(count): lay the x axis out for `count` evenly spaced
    // points instead of the data's own length. The data takes the leading
    // points in order and the rest stay empty, as an intraday chart does
    // before the close. A count below the data's length has no effect.
    LineChart* PointCount(int count);
    // y_axis: show the y axis's tick labels, one at each of the y ticks.
    // Default false.
    LineChart* YAxis(bool v = true);
    // y_axis_label_placement: in a gutter left of the plot (Outside, the
    // default), or inside it beside their grid lines.
    LineChart* YAxisLabelPlacement(AxisLabelPlacement placement);
    // y_tick_count: how many ticks the y axis carries, evenly spaced from the
    // baseline to the top edge with both ends included. They place the
    // horizontal grid lines and the labels, each reading the value the scale
    // puts at its height. At least 2; default 5.
    LineChart* YTickCount(int count);
    // y_tick_format: the text of each y-axis tick label from its value.
    LineChart* YTickFormat(ChartTickFormatFn format, void* user = nullptr);
    // x_tick_count: label `count` of the x values, spread evenly from the
    // first to the last, instead of every TickMargin-th. With PointCount they
    // spread over every point the axis is laid out for, so they keep their
    // places as the data grows.
    LineChart* XTickCount(int count);
    // grid_columns: divide the plot into `count` columns with vertical grid
    // lines, the first on its left edge. Default 0.
    LineChart* GridColumns(int count);
    // grid_dashed: default true.
    LineChart* GridDashed(bool dashed);
    // reference_line: a dashed line across the plot at `value`, such as a
    // previous close. Call again for more; one outside the y axis is not
    // drawn.
    LineChart* ReferenceLine(double value);
    // y_padding: the space kept clear above the highest value and below the
    // lowest. Default 10 above, none below.
    LineChart* YPadding(float top, float bottom);
    LineChart* Natural();
    LineChart* Linear();
    LineChart* StepAfter();
    LineChart* Dot(bool v = true);
    // x_axis(false): no x axis line or labels, and no room kept for them.
    LineChart* XAxis(bool v);
    // grid(false): no grid lines.
    LineChart* Grid(bool v);
    El* IntoEl();
};

// BarChart: a band per value, the bars rounded at the top.
struct BarChart {
    Arena* a = nullptr;
    Str tooltipName = {};
    // chart/mod.rs TooltipContent: tooltip_title / tooltip_value /
    // tooltip_value_color.
    ChartTooltipContent tooltipContent = {};
    // The chart's ElementId, folded onto the id stack it was built under: its
    // construction site unless Id renamed it (chart/mod.rs caller_id).
    uint32_t id = 0;
    // interactive(..): the hitbox under the cursor and what it drives -- the
    // hover emphasis and the tooltip. On by default.
    bool interactive = true;
    // appear(..) / appear_key(..): whether the data draws in the first time
    // the chart is painted, and the key that replays it.
    ChartAppear appear = {};
    Ctx* cx = nullptr;
    const float* ys = nullptr;
    // The values as the caller gave them, when it gave doubles (the New
    // overload): what the chart writes out -- a tooltip's numbers, a bar's
    // label -- reads them, in f64's Display. Its geometry stays float.
    const double* exact = nullptr;
    int n = 0;
    const char* const* labels = nullptr;
    int tickMargin = 1;
    Rgba fill = {};
    // padding_inner / padding_outer: ScaleBand's gaps — between neighbouring
    // bars as a share of a band, and before the first and after the last.
    float paddingInner = 0.4f;
    float paddingOuter = 0.2f;
    // max_band_width: the widest a bar is, however few bands share the
    // width; chart/mod.rs MAX_BAND_WIDTH (30) by default.
    float maxBandWidth = kChartMaxBandWidth;
    float minLength = 0;
    const Rgba* labelColors = nullptr;
    // corner_radii: square unless asked, Corners::all(px(0.)).
    float radius = 0;
    float domainMin = 0;
    float domainMax = 0;
    BarAlign align = BarAlign::Bottom;
    // Stack: the value each bar starts at, one per band.
    const float* bases = nullptr;
    // A series drawn over the grid another one already put down.
    bool overlay = false;
    bool labelValues = false;
    // value_axis / value_tick_count: the labels down the value axis, and how
    // many ticks they are placed on.
    bool valueAxis = false;
    int valueTickCount = 5;
    AxisLabelPlacement valueAxisLabelPlacement = AxisLabelPlacement::Outside;
    ChartTickFormatFn valueTickFormat = nullptr;
    void* valueTickFormatUser = nullptr;
    int bandCount = 0;
    int bandTickCount = -1;
    bool gridDashed = true;
    // fill(|d, ..|): one colour per bar. The array is the caller's and has to
    // outlive the frame.
    const Rgba* fills = nullptr;
    bool gradient = false;
    bool gradientPerBar = false;
    bool gradientDiagonal = false;
    Rgba gradientFrom = {};
    Rgba gradientTo = {};
    // label_axis(..) / grid(..): both on by default.
    bool labelAxis = true;
    bool grid = true;

    static BarChart* New(Ctx* cx, const float* ys, int n,
                         const char* file = __builtin_FILE(),
                         int line = __builtin_LINE());
    // The same over doubles, which the chart keeps for the numbers it
    // writes out (`exact`); it draws them as floats.
    static BarChart* New(Ctx* cx, const double* ys, int n,
                         const char* file = __builtin_FILE(),
                         int line = __builtin_LINE());
    // name(..): what the tooltip calls the series.
    BarChart* Tooltip(Str name);
    // tooltip_title: the tooltip's title for the datum, instead of its
    // label.
    BarChart* TooltipTitle(ChartTooltipTitleFn fn, void* user = nullptr);
    // tooltip_value: each row's value text; the raw number by default. `row`
    // is always 0; Rust's closure takes no row index for the one bar.
    BarChart* TooltipValue(ChartTooltipValueFn fn, void* user = nullptr);
    // tooltip_value_color: each row's value colour, such as green or red by
    // its sign; the tooltip's text colour by default.
    BarChart* TooltipValueColor(ChartTooltipValueColorFn fn,
                                void* user = nullptr);
    // tooltip_content: draw the box's content for a datum yourself, in place
    // of the title and rows. The crosshair, the dots and where the box sits
    // stay the chart's.
    BarChart* TooltipContent(ChartTooltipContentFn fn, void* user = nullptr);
    // data(..): the items the chart's points are, one per point, which the
    // tooltip closures above receive as their datum in place of the chart's
    // own number. Rust's charts hold `data` and map it; these hold the
    // numbers, and this is what Rust's closures would have read them from.
    BarChart* Data(const void* items, int stride);
    template <typename T>
    BarChart* Data(const T* items) {
        return Data((const void*)items, (int)sizeof(T));
    }
    // id(..): rename the chart's ElementId, replacing the construction site.
    // Needed where one site builds several of these as siblings, which would
    // otherwise share one hover state. Unique among those siblings.
    BarChart* Id(Str name);
    // interactive(false): stand the chart down. Without its hitbox it
    // neither answers the mouse nor takes the hover from an element drawn
    // over it -- a loading skeleton, an empty-state ring.
    BarChart* Interactive(bool v);
    // appear(..): draw the data in the first time this chart is painted. On
    // by default; the theme sets how long it takes and reduced motion skips
    // it. Turn it off for a chart painted again and again as it scrolls in
    // and out of view, such as one in each row of a long list.
    BarChart* Appear(bool v);
    // appear_key(..): draw the data in again whenever `key` changes, such as
    // the symbol or period a chart shows. Without one the data draws in
    // once, and later data paints in place.
    BarChart* AppearKey(Str key);
    BarChart* AppearKey(uint64_t key);
    // Plot::id: the id the chart keeps its state under, its appear and, when
    // interactive, its hover.
    uint32_t PlotId() const { return id; }
    // Plot::interactive: whether it tracks hover and shows its tooltip.
    bool PlotInteractive() const { return interactive; }
    // Plot::appear_generation: false when the chart opted out.
    bool AppearGeneration(uint64_t* out) const {
        return appear.Generation(out);
    }
    BarChart* Fill(Rgba c);
    BarChart* Labels(const char* const* l);
    BarChart* TickMargin(int n);
    // Show or hide the value-axis tick labels. Enabling this reserves 32 DIPs
    // along the band axis (left of vertical bars, below horizontal ones) for
    // them. Default false.
    BarChart* ValueAxis(bool on = true);
    // How many ticks the value axis carries, evenly spaced from the baseline
    // to the far edge with both ends included, which drives both the grid
    // lines and the value-axis tick labels. Unlike TickMargin, a stride over
    // the band categories, this counts the ticks themselves. Values below 2
    // are raised to 2. Default 5.
    BarChart* ValueTickCount(int count);
    // value_axis_label_placement: in a gutter beside the bars (Outside, the
    // default), or inside the plot beside their grid lines, which keeps the
    // bars' room.
    BarChart* ValueAxisLabelPlacement(AxisLabelPlacement placement);
    // value_tick_format: the text of each value-axis tick label from its
    // value. Default is whole numbers bare and the rest to one decimal.
    BarChart* ValueTickFormat(ChartTickFormatFn format, void* user = nullptr);
    // band_count: lay the band axis out for `count` bands instead of the
    // data's own length; the data takes the leading ones, so each bar keeps
    // its width and place as the data grows.
    BarChart* BandCount(int count);
    // band_tick_count: label `count` of the bands, spread evenly from the
    // first to the last, instead of every TickMargin-th.
    BarChart* BandTickCount(int count);
    // grid_dashed: default true.
    BarChart* GridDashed(bool dashed);
    // label_axis(false): no band axis line or band labels, and no room kept
    // for them under the bars.
    BarChart* LabelAxis(bool v);
    // grid(false): no grid lines.
    BarChart* Grid(bool v);
    // Set the gap between neighbouring bars, as a share of each band.
    // Default 0.4.
    BarChart* PaddingInner(float v);
    // Set the gap before the first bar and after the last, as a share of a
    // band. Default 0.2.
    BarChart* PaddingOuter(float v);
    // Keep every bar at most `width` wide, so a few bars across a wide chart
    // stay narrow instead of filling their bands. Default 30.
    BarChart* MaxBandWidth(float width);
    // Draw every bar at least `length` DIPs long, so a zero or tiny value
    // still shows a stub instead of disappearing into the baseline. The stub
    // grows away from the zero line: to the negative side for a negative
    // value, to the positive side for zero. Default 0.
    BarChart* MinLength(float length);
    BarChart* Radius(float v);
    BarChart* Domain(float lo, float hi);
    BarChart* Alignment(BarAlign v);
    BarChart* Base(const float* y0);
    BarChart* Overlay(bool v = true);
    // BarChart::label(|d| d.desktop.to_string()).
    BarChart* LabelValues(bool v = true);
    // label_color(|d| ..): one colour per bar's label, instead of the
    // theme's foreground for all of them. The array is the caller's and has
    // to outlive the frame.
    BarChart* LabelColors(const Rgba* colors);
    BarChart* Fills(const Rgba* colors);
    // fill_gradient: `perBar` runs the whole ramp inside every bar rather
    // than across the chart's range.
    BarChart* FillGradient(Rgba from, Rgba to, bool perBar = false);
    // fill(|_, bar, chart, _|): one ramp across the whole plot's diagonal,
    // each bar showing the slice of it under its own footprint.
    BarChart* FillGradientDiagonal(Rgba from, Rgba to);
    El* IntoEl();
};

// CandlestickChart: open, high, low and close per band, the body colored by
// which way it closed.
struct CandlestickChart {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    const float* opens = nullptr;
    const float* highs = nullptr;
    const float* lows = nullptr;
    const float* closes = nullptr;
    int n = 0;
    const char* const* labels = nullptr;
    int tickMargin = 1;
    Rgba up = {};
    Rgba down = {};
    float padding = 0.3f;
    float bodyWidthRatio = 0.8f;
    // max_band_width: 30 by default, as for BarChart.
    float maxBandWidth = kChartMaxBandWidth;
    Str tooltipName = {};
    // chart/mod.rs TooltipContent: tooltip_title / tooltip_value /
    // tooltip_value_color.
    ChartTooltipContent tooltipContent = {};
    // The chart's ElementId, folded onto the id stack it was built under: its
    // construction site unless Id renamed it (chart/mod.rs caller_id).
    uint32_t id = 0;
    // interactive(..): the hitbox under the cursor and what it drives -- the
    // hover emphasis and the tooltip. On by default.
    bool interactive = true;
    // appear(..) / appear_key(..): whether the data draws in the first time
    // the chart is painted, and the key that replays it.
    ChartAppear appear = {};

    static CandlestickChart* New(Ctx* cx, const float* opens,
                                 const float* highs, const float* lows,
                                 const float* closes, int n,
                                 const char* file = __builtin_FILE(),
                                 int line = __builtin_LINE());
    CandlestickChart* Tooltip(Str name);
    // tooltip_title: the tooltip's title for the datum, instead of its
    // label.
    CandlestickChart* TooltipTitle(ChartTooltipTitleFn fn,
                                   void* user = nullptr);
    // tooltip_value: each row's value text; the raw number by default. `row`
    // is 0 to 3 for open, high, low and close.
    CandlestickChart* TooltipValue(ChartTooltipValueFn fn,
                                   void* user = nullptr);
    // tooltip_value_color: each row's value colour, such as green or red by
    // its sign; the tooltip's text colour by default.
    CandlestickChart* TooltipValueColor(ChartTooltipValueColorFn fn,
                                        void* user = nullptr);
    // tooltip_content: draw the box's content for a datum yourself, in place
    // of the title and rows. The crosshair, the dots and where the box sits
    // stay the chart's.
    CandlestickChart* TooltipContent(ChartTooltipContentFn fn,
                                     void* user = nullptr);
    // data(..): the items the chart's points are, one per point, which the
    // tooltip closures above receive as their datum in place of the chart's
    // own number. Rust's charts hold `data` and map it; these hold the
    // numbers, and this is what Rust's closures would have read them from.
    CandlestickChart* Data(const void* items, int stride);
    template <typename T>
    CandlestickChart* Data(const T* items) {
        return Data((const void*)items, (int)sizeof(T));
    }
    // id(..): rename the chart's ElementId, replacing the construction site.
    // Needed where one site builds several of these as siblings, which would
    // otherwise share one hover state. Unique among those siblings.
    CandlestickChart* Id(Str name);
    // interactive(false): stand the chart down. Without its hitbox it
    // neither answers the mouse nor takes the hover from an element drawn
    // over it -- a loading skeleton, an empty-state ring.
    CandlestickChart* Interactive(bool v);
    // appear(..): draw the data in the first time this chart is painted. On
    // by default; the theme sets how long it takes and reduced motion skips
    // it. Turn it off for a chart painted again and again as it scrolls in
    // and out of view, such as one in each row of a long list.
    CandlestickChart* Appear(bool v);
    // appear_key(..): draw the data in again whenever `key` changes, such as
    // the symbol or period a chart shows. Without one the data draws in
    // once, and later data paints in place.
    CandlestickChart* AppearKey(Str key);
    CandlestickChart* AppearKey(uint64_t key);
    // Plot::id: the id the chart keeps its state under, its appear and, when
    // interactive, its hover.
    uint32_t PlotId() const { return id; }
    // Plot::interactive: whether it tracks hover and shows its tooltip.
    bool PlotInteractive() const { return interactive; }
    // Plot::appear_generation: false when the chart opted out.
    bool AppearGeneration(uint64_t* out) const {
        return appear.Generation(out);
    }
    CandlestickChart* Colors(Rgba up, Rgba down);
    CandlestickChart* Labels(const char* const* l);
    CandlestickChart* TickMargin(int n);
    CandlestickChart* Padding(float v);
    CandlestickChart* BodyWidthRatio(float v);
    // Keep every candle's band at most `width` wide. Default 30.
    CandlestickChart* MaxBandWidth(float width);
    El* IntoEl();
};

// The label of one radar dimension. Text is painted by the plot and inherits
// LabelColor; an element is measured at its natural size and styles itself.
// This is RadarLabel in radar_chart.rs, represented as a POD tag because a
// frame element cannot be retained behind a variant or trait object here.
struct RadarLabel {
    enum class Kind : uint8_t {
        Text,
        Element
    };

    Kind kind = Kind::Text;
    Str text = {};
    El* element = nullptr;

    static RadarLabel Text(Str text);
    static RadarLabel Element(El* element);
};

// radar_chart.rs DEFAULT_LABEL_GAP: the extra gap between the outer ring and
// the labels.
const float kRadarDefaultLabelGap = 10;

// radar_chart.rs hovered_index: the spoke nearest the cursor at `position`
// (relative to the chart's box of `size`), or -1 when the cursor is past the
// labels' ring (`outerRadius` + `labelGap` from the centre) or there are no
// spokes. Spoke 0 points at twelve o'clock and they run clockwise.
int RadarHoveredIndex(int n, float outerRadius, float labelGap, Point position,
                      Size size);

// RadarChart: one value per axis, plotted on rings around a centre. Each
// series is a closed polygon over every spoke; `New` takes the first, and
// each `Value` adds one more, with the `Stroke`, `Fill` and `Tooltip` after
// it belonging to that series, the way Rust's
// `.value(..).stroke(..).fill(..).name(..)` chain does.
struct RadarChart {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    const float* values = nullptr;
    // The values as the caller gave them, when it gave doubles (the New
    // overload): what the chart writes out -- a tooltip's numbers, a bar's
    // label -- reads them, in f64's Display. Its geometry stays float.
    const double* exact = nullptr;
    int n = 0;
    const RadarLabel* labels = nullptr;
    // The first series' colours; the stroke defaults to chart_1 and the fill
    // to the stroke at 0.3, as series_stroke does.
    Rgba stroke = {};
    Rgba fill = {};
    // The series after the first, in the order `Value` added them. Each one's
    // stroke defaults to the next theme chart colour, cycled.
    ArenaVec<ChartSeriesExtra> more;
    // Whether the last series' fill was set, so a later Stroke leaves it.
    bool lastFillSet = false;
    float domainMin = 0;
    float domainMax = 0;
    bool dot = false;
    float outerRadius = 0;
    int gridLevels = 4;
    // grid(..): the rings and spokes, on by default.
    bool grid = true;
    float labelGap = kRadarDefaultLabelGap;
    Rgba labelColor = {};
    bool hasLabelColor = false;
    // name(..) of the first series.
    Str tooltipName = {};
    // chart/mod.rs TooltipContent: tooltip_title / tooltip_value /
    // tooltip_value_color / tooltip_content.
    ChartTooltipContent tooltipContent = {};
    // The chart's ElementId, folded onto the id stack it was built under: its
    // construction site unless Id renamed it (chart/mod.rs caller_id).
    uint32_t id = 0;
    // interactive(..): the hitbox under the cursor and what it drives -- a
    // dot per series on the hovered dimension and the tooltip. On by default.
    bool interactive = true;
    // appear(..) / appear_key(..): whether the data draws in the first time
    // the chart is painted, and the key that replays it.
    ChartAppear appear = {};
    // The element IntoEl made, which the hover paints over.
    El* el = nullptr;

    static RadarChart* New(Ctx* cx, const float* values, int n,
                           const char* file = __builtin_FILE(),
                           int line = __builtin_LINE());
    // The same over doubles, which the chart keeps for the numbers it
    // writes out (`exact`); it draws them as floats.
    static RadarChart* New(Ctx* cx, const double* values, int n,
                           const char* file = __builtin_FILE(),
                           int line = __builtin_LINE());
    // value(..): another series over the same spokes.
    RadarChart* Value(const float* ys);
    // name(..): what the tooltip calls the series added last.
    RadarChart* Tooltip(Str name);
    // tooltip_title: the tooltip's title for the datum, instead of its
    // dimension's text label.
    RadarChart* TooltipTitle(ChartTooltipTitleFn fn, void* user = nullptr);
    // tooltip_value: each row's value text; the raw number by default. `row`
    // is the series' index in the order `Value` added them.
    RadarChart* TooltipValue(ChartTooltipValueFn fn, void* user = nullptr);
    // tooltip_value_color: each row's value colour; the tooltip's text
    // colour by default.
    RadarChart* TooltipValueColor(ChartTooltipValueColorFn fn,
                                  void* user = nullptr);
    // tooltip_content: draw the box's content for a datum yourself, in place
    // of the title and rows. The dots and where the box sits stay the
    // chart's.
    RadarChart* TooltipContent(ChartTooltipContentFn fn, void* user = nullptr);
    // data(..): the items the chart's points are, one per point, which the
    // tooltip closures above receive as their datum in place of the chart's
    // own number. Rust's charts hold `data` and map it; these hold the
    // numbers, and this is what Rust's closures would have read them from.
    RadarChart* Data(const void* items, int stride);
    template <typename T>
    RadarChart* Data(const T* items) {
        return Data((const void*)items, (int)sizeof(T));
    }
    // id(..): rename the chart's ElementId, replacing the construction site.
    // Needed where one site builds several of these as siblings, which would
    // otherwise share one hover state. Unique among those siblings.
    RadarChart* Id(Str name);
    // interactive(false): stand the chart down. Without its hitbox it
    // neither answers the mouse nor takes the hover from an element drawn
    // over it -- a loading skeleton, an empty-state ring.
    RadarChart* Interactive(bool v);
    // appear(..): draw the data in the first time this chart is painted. On
    // by default; the theme sets how long it takes and reduced motion skips
    // it. Turn it off for a chart painted again and again as it scrolls in
    // and out of view, such as one in each row of a long list.
    RadarChart* Appear(bool v);
    // appear_key(..): draw the data in again whenever `key` changes, such as
    // the symbol or period a chart shows. Without one the data draws in
    // once, and later data paints in place.
    RadarChart* AppearKey(Str key);
    RadarChart* AppearKey(uint64_t key);
    // Plot::id: the id the chart keeps its state under, its appear and, when
    // interactive, its hover.
    uint32_t PlotId() const { return id; }
    // Plot::interactive: whether it tracks hover and shows its tooltip.
    bool PlotInteractive() const { return interactive; }
    // Plot::appear_generation: false when the chart opted out.
    bool AppearGeneration(uint64_t* out) const {
        return appear.Generation(out);
    }
    // stroke(..) / fill(..) of the series added last.
    RadarChart* Stroke(Rgba c);
    RadarChart* Fill(Rgba c);
    RadarChart* Labels(const char* const* l);
    RadarChart* Labels(const RadarLabel* l);
    RadarChart* LabelColor(Rgba c);
    RadarChart* LabelGap(float v);
    // max_value(..): the value at the outer ring, which is 0..v.
    RadarChart* MaxValue(float v);
    RadarChart* Domain(float lo, float hi);
    RadarChart* Dot(bool v = true);
    RadarChart* OuterRadius(float v);
    RadarChart* GridLevels(int v);
    // grid(false): no rings or spokes.
    RadarChart* Grid(bool v);
    // resolve_outer_radius: the caller's radius, or two fifths of `height`.
    float ResolveOuterRadius(float height) const;
    El* IntoEl();
};

// SankeyChart: nodes in columns with ribbons between them, each as thick as
// the flow it carries (crates/ui/src/chart/sankey_chart.rs). The layout is
// `Sankey` in base; what is here is the paint and the labels.
// DEFAULT_NODE_WIDTH, DEFAULT_NODE_PADDING and the rest of the chart's own
// defaults, which are not the layout generator's.
const float kSankeyChartNodeWidth = 10;
const float kSankeyChartNodePadding = 16;
const float kSankeyChartLinkOpacity = 0.3f;
const float kSankeyChartMinLinkWidth = 1;
const float kSankeyChartLabelGap = 6;
// MAX_LABEL_WIDTH_RATIO and MAX_LABEL_MARGIN_RATIO: a long label is truncated
// to a modest column beside the flow rather than taking the chart over.
const float kSankeyMaxLabelWidthRatio = 0.2f;
const float kSankeyMaxLabelMarginRatio = 0.6f;

// A styled line of a Sankey node label. An unset colour uses foreground and
// an unset font size uses the plot's 10-DIP text size.
struct SankeyLabel {
    Str text = {};
    Rgba color = {};
    float fontSize = 0;
    bool hasColor = false;

    static SankeyLabel New(Str text);
    SankeyLabel Color(Rgba color) const;
    SankeyLabel FontSize(float fontSize) const;
    float LineHeight() const;
};

struct SankeyChartNode {
    Str label = {};
    // The value shown above the name, when the caller asked for one.
    Str value = {};
    // labels(): a line between the value and the name, in its own colour —
    // the year-over-year change the TSLA statement carries.
    Str note = {};
    Rgba noteColor = {};
    Rgba color = {};
    bool hasColor = false;
    // labels(..) wins over value_label/node_label in Rust. Once CustomLabel
    // is called this arbitrary line list likewise replaces the convenience
    // value/note/name triple above.
    ArenaVec<SankeyLabel> labels;
    bool hasCustomLabels = false;
    // tooltip_name / tooltip_value: the hover row's name (none when unset)
    // and value (the drawn value label, else the raw throughput).
    Str tooltipName = {};
    Str tooltipValue = {};
};

struct SankeyChart {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    // As many nodes and links as the caller adds; both grow into the frame
    // arena the builder is on.
    ArenaVec<SankeyChartNode> nodes;
    ArenaVec<SankeyLink> links;
    float nodeWidth = kSankeyChartNodeWidth;
    float nodePadding = kSankeyChartNodePadding;
    SankeyAlign align = SankeyAlign::Justify;
    int iterations = 6;
    SankeyValueScale valueScale = SankeyValueScale::Linear;
    float nodeRadius = 0;
    float linkOpacity = kSankeyChartLinkOpacity;
    float minLinkWidth = kSankeyChartMinLinkWidth;
    float labelGap = kSankeyChartLabelGap;
    // Whether the node's throughput is written above its name, which is
    // Rust's value_label.
    bool showValues = false;
    Str tooltipName = {};
    // The chart's ElementId, folded onto the id stack it was built under: its
    // construction site unless Id renamed it (chart/mod.rs caller_id).
    uint32_t id = 0;
    // interactive(..): the hitbox under the cursor and what it drives -- the
    // hover emphasis and the tooltip. On by default.
    bool interactive = true;
    // appear(..) / appear_key(..): whether the data draws in the first time
    // the chart is painted, and the key that replays it.
    ChartAppear appear = {};

    static SankeyChart* New(Ctx* cx, const char* file = __builtin_FILE(),
                            int line = __builtin_LINE());
    SankeyChart* Tooltip(Str name);
    // tooltip_name(..) / tooltip_value(..) for the node just added: what
    // the hover row says, for a chart drawing its text through CustomLabel,
    // which never reaches the tooltip.
    SankeyChart* TooltipName(Str name);
    SankeyChart* TooltipValue(Str value);
    // id(..): rename the chart's ElementId, replacing the construction site.
    // Needed where one site builds several of these as siblings, which would
    // otherwise share one hover state. Unique among those siblings.
    SankeyChart* Id(Str name);
    // interactive(false): stand the chart down. Without its hitbox it
    // neither answers the mouse nor takes the hover from an element drawn
    // over it -- a loading skeleton, an empty-state ring.
    SankeyChart* Interactive(bool v);
    // appear(..): draw the data in the first time this chart is painted. On
    // by default; the theme sets how long it takes and reduced motion skips
    // it. Turn it off for a chart painted again and again as it scrolls in
    // and out of view, such as one in each row of a long list.
    SankeyChart* Appear(bool v);
    // appear_key(..): draw the data in again whenever `key` changes, such as
    // the symbol or period a chart shows. Without one the data draws in
    // once, and later data paints in place.
    SankeyChart* AppearKey(Str key);
    SankeyChart* AppearKey(uint64_t key);
    // Plot::id: the id the chart keeps its state under, its appear and, when
    // interactive, its hover.
    uint32_t PlotId() const { return id; }
    // Plot::interactive: whether it tracks hover and shows its tooltip.
    bool PlotInteractive() const { return interactive; }
    // Plot::appear_generation: false when the chart opted out.
    bool AppearGeneration(uint64_t* out) const {
        return appear.Generation(out);
    }
    // A node, by the order they are added — a link names them by index.
    SankeyChart* Node(Str label);
    SankeyChart* NodeColored(Str label, Rgba color);
    // The value and the note of the node just added.
    SankeyChart* NodeValue(Str text);
    SankeyChart* NodeNote(Str text, Rgba color);
    SankeyChart* CustomLabel(SankeyLabel label);
    SankeyChart* CustomLabels(const SankeyLabel* labels, int n);
    SankeyChart* Link(int source, int target, double value);
    SankeyChart* NodeWidth(float v);
    SankeyChart* NodePadding(float v);
    SankeyChart* NodeAlign(SankeyAlign v);
    SankeyChart* Iterations(int v);
    SankeyChart* ValueScale(SankeyValueScale v);
    SankeyChart* NodeCornerRadius(float v);
    SankeyChart* LinkOpacity(float v);
    SankeyChart* MinLinkWidth(float v);
    SankeyChart* LabelGap(float v);
    SankeyChart* ShowValues(bool v = true);
    El* IntoEl();
};

// raw_throughput: what a node carries in the values the caller gave, which is
// what a label reads — the layout's own value is in scaled units under a
// non-linear scale. Writes one per node.
void SankeyChartThroughput(const SankeyLink* links, int nLinks, double* out,
                           int n);

} // namespace component
} // namespace gpui
#endif // GPUI_UI_CHART_H_
