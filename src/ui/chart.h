#ifndef GPUI_UI_CHART_H_
#define GPUI_UI_CHART_H_
/* Themed charts — crates/ui/src/chart */

#include "ui/sizing.h"
#include "ui/sankey.h"

namespace gpui {

namespace component {

// The spring a chart's pointer — the crosshair, highlight band or hover
// dot — follows the hovered datum with. A critically damped fast-tier
// response, matching ECharts' 200 ms exponential-out axis pointer.
Spring ChartPointerSpring(const App* app);
// The size of the dot marking the hovered data point.
const float kChartHoverDotSize = 8;
// The ring behind a hovered dot, growing out of the dot as the hover fades
// in. Full focus is 20 DIPs.
float ChartHoverHaloSize(float focus);

// chart/mod.rs caller_id: the ElementId a chart carries when the caller names
// none — the source location it was constructed at (Rust's
// ElementId::CodeLocation through #[track_caller]; here the call site's
// __builtin_FILE / __builtin_LINE), folded onto the id stack it was built
// under, which is what a GlobalElementId is. The hover state and the tooltip
// key on it, so a chart built at a site written out once is interactive
// without being handed an id; one site building several sibling charts has
// them share one, which is what Id is for.
uint32_t ChartCallerId(const Ctx* cx, const char* file, int line);

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
    // Plot::id: the id the hover keys on, or 0 for a chart that is off.
    uint32_t PlotId() const { return interactive ? id : 0; }
    // The outer radius the ring is laid out with: the set one, or 40% of
    // `height`.
    float ResolveOuterRadius(float height) const;
    El* IntoEl();
};

struct AreaChart {
    Arena* a = nullptr;
    Str tooltipName = {};
    // The chart's ElementId, folded onto the id stack it was built under: its
    // construction site unless Id renamed it (chart/mod.rs caller_id).
    uint32_t id = 0;
    // interactive(..): the hitbox under the cursor and what it drives -- the
    // hover emphasis and the tooltip. On by default.
    bool interactive = true;
    Ctx* cx = nullptr;
    const float* ys = nullptr;
    int n = 0;
    const char* const* labels = nullptr;
    int tickMargin = 15;
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

    static AreaChart* New(Ctx* cx, const float* ys, int n,
                          const char* file = __builtin_FILE(),
                          int line = __builtin_LINE());
    // `.y(..)`: another series over the same axes. The `Stroke`, `Fill` and
    // `Tooltip` after it belong to that series, the way Rust's chain does.
    AreaChart* Y(const float* ys);
    // name(..): what the tooltip calls the series.
    AreaChart* Tooltip(Str name);
    // id(..): rename the chart's ElementId, replacing the construction site.
    // Needed where one site builds several of these as siblings, which would
    // otherwise share one hover state. Unique among those siblings.
    AreaChart* Id(Str name);
    // interactive(false): stand the chart down. Without its hitbox it
    // neither answers the mouse nor takes the hover from an element drawn
    // over it -- a loading skeleton, an empty-state ring.
    AreaChart* Interactive(bool v);
    // Plot::id: the id the hover keys on, or 0 for a chart that is off.
    uint32_t PlotId() const { return interactive ? id : 0; }
    AreaChart* Stroke(Rgba c);
    AreaChart* Fill(Rgba c);
    // fill(linear_gradient(0., stop(bottom, 0.), stop(top, 1.))).
    AreaChart* Fill(Rgba top, Rgba bottom);
    AreaChart* Labels(const char* const* l);
    AreaChart* TickMargin(int n);
    AreaChart* Overlay(bool v = true);
    // StrokeStyle: Natural is the default Catmull-Rom curve.
    AreaChart* Linear();
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
    El* IntoEl();
};

// LineChart: the same run of points as an area chart, with nothing filled
// under it.
struct LineChart {
    Arena* a = nullptr;
    Str tooltipName = {};
    // The chart's ElementId, folded onto the id stack it was built under: its
    // construction site unless Id renamed it (chart/mod.rs caller_id).
    uint32_t id = 0;
    // interactive(..): the hitbox under the cursor and what it drives -- the
    // hover emphasis and the tooltip. On by default.
    bool interactive = true;
    Ctx* cx = nullptr;
    const float* ys = nullptr;
    int n = 0;
    const char* const* labels = nullptr;
    int tickMargin = 15;
    Rgba stroke = {};
    bool hasYDomain = false;
    float yDomainMin = 0;
    float yDomainMax = 0;
    int pointCount = 0;
    ChartStroke strokeStyle = ChartStroke::Natural;
    bool dot = false;

    static LineChart* New(Ctx* cx, const float* ys, int n,
                          const char* file = __builtin_FILE(),
                          int line = __builtin_LINE());
    // name(..): what the tooltip calls the series.
    LineChart* Tooltip(Str name);
    // id(..): rename the chart's ElementId, replacing the construction site.
    // Needed where one site builds several of these as siblings, which would
    // otherwise share one hover state. Unique among those siblings.
    LineChart* Id(Str name);
    // interactive(false): stand the chart down. Without its hitbox it
    // neither answers the mouse nor takes the hover from an element drawn
    // over it -- a loading skeleton, an empty-state ring.
    LineChart* Interactive(bool v);
    // Plot::id: the id the hover keys on, or 0 for a chart that is off.
    uint32_t PlotId() const { return interactive ? id : 0; }
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
    LineChart* Linear();
    LineChart* StepAfter();
    LineChart* Dot(bool v = true);
    El* IntoEl();
};

// BarChart: a band per value, the bars rounded at the top.
struct BarChart {
    Arena* a = nullptr;
    Str tooltipName = {};
    // The chart's ElementId, folded onto the id stack it was built under: its
    // construction site unless Id renamed it (chart/mod.rs caller_id).
    uint32_t id = 0;
    // interactive(..): the hitbox under the cursor and what it drives -- the
    // hover emphasis and the tooltip. On by default.
    bool interactive = true;
    Ctx* cx = nullptr;
    const float* ys = nullptr;
    int n = 0;
    const char* const* labels = nullptr;
    int tickMargin = 1;
    Rgba fill = {};
    // padding_inner / padding_outer: ScaleBand's gaps — between neighbouring
    // bars as a share of a band, and before the first and after the last.
    float paddingInner = 0.4f;
    float paddingOuter = 0.2f;
    float minLength = 0;
    const Rgba* labelColors = nullptr;
    float radius = 4;
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
    // fill(|d, ..|): one colour per bar. The array is the caller's and has to
    // outlive the frame.
    const Rgba* fills = nullptr;
    bool gradient = false;
    bool gradientPerBar = false;
    bool gradientDiagonal = false;
    Rgba gradientFrom = {};
    Rgba gradientTo = {};

    static BarChart* New(Ctx* cx, const float* ys, int n,
                         const char* file = __builtin_FILE(),
                         int line = __builtin_LINE());
    // name(..): what the tooltip calls the series.
    BarChart* Tooltip(Str name);
    // id(..): rename the chart's ElementId, replacing the construction site.
    // Needed where one site builds several of these as siblings, which would
    // otherwise share one hover state. Unique among those siblings.
    BarChart* Id(Str name);
    // interactive(false): stand the chart down. Without its hitbox it
    // neither answers the mouse nor takes the hover from an element drawn
    // over it -- a loading skeleton, an empty-state ring.
    BarChart* Interactive(bool v);
    // Plot::id: the id the hover keys on, or 0 for a chart that is off.
    uint32_t PlotId() const { return interactive ? id : 0; }
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
    // Set the gap between neighbouring bars, as a share of each band.
    // Default 0.4.
    BarChart* PaddingInner(float v);
    // Set the gap before the first bar and after the last, as a share of a
    // band. Default 0.2.
    BarChart* PaddingOuter(float v);
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
    Str tooltipName = {};
    // The chart's ElementId, folded onto the id stack it was built under: its
    // construction site unless Id renamed it (chart/mod.rs caller_id).
    uint32_t id = 0;
    // interactive(..): the hitbox under the cursor and what it drives -- the
    // hover emphasis and the tooltip. On by default.
    bool interactive = true;

    static CandlestickChart* New(Ctx* cx, const float* opens,
                                 const float* highs, const float* lows,
                                 const float* closes, int n,
                                 const char* file = __builtin_FILE(),
                                 int line = __builtin_LINE());
    CandlestickChart* Tooltip(Str name);
    // id(..): rename the chart's ElementId, replacing the construction site.
    // Needed where one site builds several of these as siblings, which would
    // otherwise share one hover state. Unique among those siblings.
    CandlestickChart* Id(Str name);
    // interactive(false): stand the chart down. Without its hitbox it
    // neither answers the mouse nor takes the hover from an element drawn
    // over it -- a loading skeleton, an empty-state ring.
    CandlestickChart* Interactive(bool v);
    // Plot::id: the id the hover keys on, or 0 for a chart that is off.
    uint32_t PlotId() const { return interactive ? id : 0; }
    CandlestickChart* Colors(Rgba up, Rgba down);
    CandlestickChart* Labels(const char* const* l);
    CandlestickChart* TickMargin(int n);
    CandlestickChart* Padding(float v);
    CandlestickChart* BodyWidthRatio(float v);
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

// RadarChart: one value per axis, plotted on rings around a centre.
struct RadarChart {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    const float* values = nullptr;
    int n = 0;
    const RadarLabel* labels = nullptr;
    Rgba stroke = {};
    Rgba fill = {};
    float domainMin = 0;
    float domainMax = 0;
    // A second ring over the first one's grid, the way a stacked area chart
    // overlays its series.
    bool overlay = false;
    bool dot = false;
    float outerRadius = 0;
    int gridLevels = 4;
    float labelGap = 10;
    Rgba labelColor = {};
    bool hasLabelColor = false;
    Str tooltipName = {};
    // The chart's ElementId, folded onto the id stack it was built under: its
    // construction site unless Id renamed it (chart/mod.rs caller_id).
    uint32_t id = 0;
    // interactive(..): the hitbox under the cursor and what it drives -- the
    // hover emphasis and the tooltip. On by default.
    bool interactive = true;

    static RadarChart* New(Ctx* cx, const float* values, int n,
                           const char* file = __builtin_FILE(),
                           int line = __builtin_LINE());
    RadarChart* Tooltip(Str name);
    // id(..): rename the chart's ElementId, replacing the construction site.
    // Needed where one site builds several of these as siblings, which would
    // otherwise share one hover state. Unique among those siblings.
    RadarChart* Id(Str name);
    // interactive(false): stand the chart down. Without its hitbox it
    // neither answers the mouse nor takes the hover from an element drawn
    // over it -- a loading skeleton, an empty-state ring.
    RadarChart* Interactive(bool v);
    // Plot::id: the id the hover keys on, or 0 for a chart that is off.
    uint32_t PlotId() const { return interactive ? id : 0; }
    RadarChart* Stroke(Rgba c);
    RadarChart* Fill(Rgba c);
    RadarChart* Labels(const char* const* l);
    RadarChart* Labels(const RadarLabel* l);
    RadarChart* LabelColor(Rgba c);
    RadarChart* LabelGap(float v);
    RadarChart* Domain(float lo, float hi);
    RadarChart* Overlay(bool v = true);
    RadarChart* Dot(bool v = true);
    RadarChart* OuterRadius(float v);
    RadarChart* GridLevels(int v);
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
    // Plot::id: the id the hover keys on, or 0 for a chart that is off.
    uint32_t PlotId() const { return interactive ? id : 0; }
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
