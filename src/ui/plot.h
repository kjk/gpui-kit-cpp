#ifndef GPUI_UI_PLOT_H_
#define GPUI_UI_PLOT_H_
/* Plot helpers — crates/ui/src/plot
   The d3 scales Chart maps data through; AreaChart is the render entry.

   Rust's scales are generic over the domain and range types. Here the domain
   is always float — that is what a chart axis carries — and ScaleOrdinal maps
   indexes rather than values, since what it is for is picking one of N series
   colors by position. Domains and ranges are borrowed (pointer + length), so
   nothing here allocates. */

#include "ui/chart.h"

namespace gpui {

struct Path;

namespace component {

// path_cache.rs. Native path realization is cached by src/gpui/scene.h, so a
// component cache retains the semantic key rather than a second copy of the
// backend geometry. Touch answers whether this shape was already warm.
struct PathCache {
    uint64_t key = 0;
    bool hasKey = false;

    bool Touch(uint64_t value) {
        bool warm = hasKey && key == value;
        key = value;
        hasKey = true;
        return warm;
    }
    bool IsWarm() const { return hasKey; }
};

struct PathCaches {
    Vec<PathCache> slots;

    ~PathCaches() { VecReset(slots); }
    PathCache* Slot(int index);
    void SlotPair(int index, PathCache** first, PathCache** second);
};

struct ShapeKey {
    uint64_t value = 1469598103934665603ull;

    static ShapeKey New(uint64_t extra = 0) {
        ShapeKey key;
        key.U64(extra);
        return key;
    }
    ShapeKey& U64(uint64_t v);
    ShapeKey& PointValue(Point point);
    ShapeKey& Float(float v);
    uint64_t Finish() const { return value; }
};

// ScaleLinear — https://d3js.org/d3-scale/linear
//
// The domain collapses to its min and max, and so does the range, except that
// the range keeps the order it was given in: a range that starts high and ends
// low maps the domain backwards, which is how a y axis is drawn.
struct ScaleLinear {
    int domainLen = 0;
    float domainStart = 0;
    float domainDiff = 0;
    float rangeStart = 0;
    float rangeDiff = 0;

    static ScaleLinear New(const float* domain, int domainN, const float* range,
                           int rangeN);
    // The range position of `value`, or false when the domain has no extent to
    // divide by — Rust's `Option<f32>`.
    bool Tick(float value, float* out) const;
    // The domain entry whose tick is nearest `tick`, and that tick. Returns
    // (0, 0) for an empty domain.
    void LeastIndexWithDomain(float tick, const float* domain, int domainN,
                              int* outIndex, float* outTick) const;
};

// ScalePoint — https://d3js.org/d3-scale/point
//
// Discrete domain values spread evenly across the range, the first and last
// landing on its ends. A one-value domain sits in the middle instead.
struct ScalePoint {
    const float* domain = nullptr;
    int domainLen = 0;
    float rangeStart = 0;
    float rangeTick = 0;

    static ScalePoint New(const float* domain, int domainN, const float* range,
                          int rangeN);
    // False when `value` is not in the domain.
    bool Tick(float value, float* out) const;
    // The domain index nearest `tick`.
    int LeastIndex(float tick) const;
};

// ScaleBand — https://d3js.org/d3-scale/band
//
// A bar chart's x axis: the domain's entries each take a band of the range,
// with padding between them and at the ends. Rust's domain is a vector of
// values; here it is a count, since a band is picked by index — which is what
// a caller walking its data already has.
//
// That is also why upstream's two domain fixes have no code here. Rust kept
// duplicate domain values, which halved the bands of a grouped bar chart and
// left the repeated slots unaddressable, so `new` now drops them; it then
// replaced the `Vec<T>` with a value-to-index map, making the dedupe a side
// effect of building the map and `tick()` O(1) rather than a linear
// `position()` scan. A count is already distinct and already an index, so
// this port has nothing to deduplicate and nothing to look up: `Tick` is the
// same band arithmetic on the index the caller hands it. A caller with a
// repeating domain passes the number of distinct values, which is what the
// dedupe leaves Rust with.
struct ScaleBand {
    int domainLen = 0;
    float rangeDiff = 0;
    float avgWidth = 0;
    float paddingInner = 0;
    float paddingOuter = 0;

    static ScaleBand New(int domainN, const float* range, int rangeN);
    // band_width: what one band is drawn at, which Rust caps at thirty.
    float BandWidth() const;
    // The distance between the starts of two adjacent bands: the band width
    // plus the inner padding. The whole range for a single band.
    float Step() const;
    // band_count: lay the range out for `count` bands, the domain taking the
    // leading ones in order and the rest staying empty. A count below the
    // domain's length has no effect.
    ScaleBand BandCount(int count) const;
    // The range position of the band at `index`, or false when it is not one
    // of them. A one-band domain sits in the middle of the range.
    bool Tick(int index, float* out) const;
    // The band nearest `tick`, clamped to the domain.
    int LeastIndex(float tick) const;
};

// ScaleOrdinal — https://d3js.org/d3-scale/ordinal
//
// Rust maps a domain value to a range value; this maps the domain *index*,
// which is what the caller already has when it is handing out per-series
// colors. The range cycles when it is shorter than the domain.
struct ScaleOrdinal {
    int rangeLen = 0;
    // The range index for a value that is not in the domain: Rust's
    // `unknown()`, and -1 for its `None`.
    int unknown = -1;

    // `domainIndex` < 0 means the value was not in the domain. Returns the
    // range index, or -1 when there is nothing to map onto.
    int Map(int domainIndex) const;
};

// Where a plot's tooltip box goes: it hugs the cursor and flips toward the
// centre past the halfway line, so it never runs off the near edge. Rust
// writes it as four `left/right/top/bottom` branches; the answer here is the
// box's own origin inside the plot.
//
// `gap` is the distance the box keeps from the cursor.
Point PlotTooltipPlace(Point cursor, Size within, Size box, float gap);

// AXIS_GAP: the strip under a plot its x labels sit in.
const float kPlotAxisGap = 18;
// label.rs: TEXT_SIZE, TEXT_GAP and TEXT_HEIGHT.
const float kPlotTextSize = 10;
const float kPlotTextGap = 2;
const float kPlotTextHeight = kPlotTextSize + kPlotTextGap;

// The Rust plot module keeps its low-level plotting vocabulary below
// `plot::{axis, grid, label, shape, tooltip}`. The older C++ chart façade
// above predates that layer and deliberately remains in `component`; the
// source-shaped primitives live in this nested namespace so its Tooltip does
// not collide with crates/ui/src/tooltip.rs.
namespace plot {

using ::gpui::component::ScaleBand;
using ::gpui::component::ScaleLinear;
using ::gpui::component::ScaleOrdinal;
using ::gpui::component::ScalePoint;

using StrokeStyle = ChartStroke;
using ::gpui::AxisLabelPlacement;

inline Point OriginPoint(float x, float y, Point origin) {
    return Point{x + origin.x, y + origin.y};
}

// polygon(): an open one-pixel polygon in plot-local coordinates. The caller
// owns the returned immediate path and frees it after painting.
Path* Polygon(PaintCtx* ctx, const Point* points, int count, Bounds bounds);

enum class PlotTextAlign : uint8_t {
    Left,
    Center,
    Right
};

struct Text {
    Str text = {};
    Point origin = {};
    Rgba color = {};
    float fontSize = kPlotTextSize;
    FontWeight fontWeight = FontWeight::Normal;
    PlotTextAlign align = PlotTextAlign::Left;

    static Text New(Str text, Point origin, Rgba color);
    Text* FontSize(float value);
    Text* Weight(FontWeight value);
    Text* Align(PlotTextAlign value);
};

float MeasureTextWidth(PaintCtx* ctx, Str text, float fontSize);
// An unchanged answer borrows `text`; a truncated answer lives in `arena`.
Str TruncateTextToWidth(PaintCtx* ctx, Arena* arena, Str text, float fontSize,
                        float maxWidth);

struct PlotLabel {
    Arena* a = nullptr;
    ArenaVec<Text> items;

    static PlotLabel New(Arena* arena);
    PlotLabel* Add(const Text& text);
    PlotLabel* AddMany(const Text* text, int count);
    void Paint(PaintCtx* ctx, Bounds bounds) const;
};

enum class AxisLabelSide : uint8_t {
    End,
    Start
};

struct AxisText {
    Str text = {};
    float tick = 0;
    Rgba color = {};
    float fontSize = kPlotTextSize;
    PlotTextAlign align = PlotTextAlign::Left;

    static AxisText New(Str text, float tick, Rgba color);
    AxisText* FontSize(float value);
    AxisText* Align(PlotTextAlign value);
};

struct PlotAxis {
    Arena* a = nullptr;
    bool hasX = false;
    float x = 0;
    PlotLabel xLabel;
    bool xAxis = false;
    AxisLabelSide xLabelSide = AxisLabelSide::End;
    bool hasY = false;
    float y = 0;
    PlotLabel yLabel;
    bool yAxis = false;
    AxisLabelSide yLabelSide = AxisLabelSide::End;
    Rgba stroke = {};

    static PlotAxis New(Arena* arena);
    PlotAxis* X(float value);
    PlotAxis* ShowXAxis(bool value);
    PlotAxis* XLabel(const AxisText* labels, int count);
    PlotAxis* XLabelSide(AxisLabelSide value);
    PlotAxis* Y(float value);
    PlotAxis* ShowYAxis(bool value);
    PlotAxis* YLabel(const AxisText* labels, int count);
    PlotAxis* YLabelSide(AxisLabelSide value);
    PlotAxis* Stroke(Rgba value);
    void Paint(PaintCtx* ctx, Bounds bounds) const;
};

struct Grid {
    const float* x = nullptr;
    int xCount = 0;
    const float* y = nullptr;
    int yCount = 0;
    Rgba stroke = {};
    const float* dashArray = nullptr;
    int dashCount = 0;

    static Grid New();
    Grid* X(const float* values, int count);
    Grid* Y(const float* values, int count);
    Grid* Stroke(Rgba value);
    Grid* DashArray(const float* values, int count);
    void Paint(PaintCtx* ctx, Bounds bounds) const;
};

// Rust stores Vec<T> and boxed accessors on each shape. The C++ port borrows
// the same records and stores plain callbacks: no data is copied, and a
// caller can still plot any POD record without an STL closure or type erasure.
struct PlotItems {
    const void* data = nullptr;
    int count = 0;
    int stride = 0;

    const void* At(int index) const;
};

typedef bool (*PlotValueFn)(const void* item, int index, void* user,
                            float* out);

struct Line {
    PlotItems items = {};
    PlotValueFn x = nullptr;
    void* xUser = nullptr;
    PlotValueFn y = nullptr;
    void* yUser = nullptr;
    Background stroke = {};
    float strokeWidth = 1;
    StrokeStyle strokeStyle = StrokeStyle::Natural;
    bool dot = false;
    float dotSize = 4;
    Rgba dotFillColor = RgbaTransparent();
    bool hasDotStrokeColor = false;
    Rgba dotStrokeColor = {};

    static Line New();
    Line* Data(const void* values, int count, int stride);
    Line* X(PlotValueFn fn, void* user = nullptr);
    Line* Y(PlotValueFn fn, void* user = nullptr);
    Line* Stroke(Background value);
    Line* StrokeWidth(float value);
    Line* Style(StrokeStyle value);
    Line* Dots(bool value = true);
    Line* DotSize(float value);
    Line* DotFill(Rgba value);
    Line* DotStroke(Rgba value);
    int Points(Bounds bounds, Point* out, int capacity) const;
    void Paint(PaintCtx* ctx, Bounds bounds) const;
};

struct Area {
    PlotItems items = {};
    PlotValueFn x = nullptr;
    void* xUser = nullptr;
    bool hasY0 = false;
    float y0 = 0;
    PlotValueFn y1 = nullptr;
    void* y1User = nullptr;
    Background fill = {};
    Background stroke = {};
    StrokeStyle strokeStyle = StrokeStyle::Natural;

    static Area New();
    Area* Data(const void* values, int count, int stride);
    Area* X(PlotValueFn fn, void* user = nullptr);
    Area* Y0(float value);
    Area* Y1(PlotValueFn fn, void* user = nullptr);
    Area* Fill(Background value);
    Area* Stroke(Background value);
    Area* Style(StrokeStyle value);
    void Paint(PaintCtx* ctx, Bounds bounds) const;
};

enum class BarAlignment : uint8_t {
    Bottom,
    Top,
    Left,
    Right
};

bool BarAlignmentIsHorizontal(BarAlignment value);
float BarAlignmentGradientAngle(BarAlignment value);
Point BarLabelOrigin(BarAlignment alignment, float cross, float base,
                     float value, float bandWidth);

typedef Background (*PlotBarFillFn)(const void* item, int index, Bounds frame,
                                    BarAlignment alignment, void* user);
typedef void (*PlotBarLabelFn)(const void* item, int index, Point origin,
                               void* user, Vec<Text>* out);

struct Bar {
    PlotItems items = {};
    BarAlignment alignment = BarAlignment::Bottom;
    PlotValueFn cross = nullptr;
    void* crossUser = nullptr;
    float bandWidth = 0;
    PlotValueFn base = nullptr;
    void* baseUser = nullptr;
    PlotValueFn value = nullptr;
    void* valueUser = nullptr;
    PlotBarFillFn fill = nullptr;
    void* fillUser = nullptr;
    PlotBarLabelFn label = nullptr;
    void* labelUser = nullptr;
    Corners cornerRadii = {};

    static Bar New();
    Bar* Data(const void* values, int count, int stride);
    Bar* Alignment(BarAlignment value);
    Bar* Cross(PlotValueFn fn, void* user = nullptr);
    Bar* BandWidth(float value);
    Bar* Base(PlotValueFn fn, void* user = nullptr);
    Bar* Value(PlotValueFn fn, void* user = nullptr);
    Bar* Fill(PlotBarFillFn fn, void* user = nullptr);
    Bar* Label(PlotBarLabelFn fn, void* user = nullptr);
    Bar* CornerRadii(Corners value);
    void Paint(PaintCtx* ctx, Bounds bounds) const;
};

struct ArcData {
    const void* data = nullptr;
    int index = 0;
    float value = 0;
    float startAngle = 0;
    float endAngle = 0;
    float padAngle = 0;
};

struct Arc {
    float innerRadius = 0;
    float outerRadius = 0;

    static Arc New();
    Arc* InnerRadius(float value);
    Arc* OuterRadius(float value);
    Point Centroid(const ArcData& arc) const;
    Path* PathFor(PaintCtx* ctx, const ArcData& arc, Bounds bounds,
                  float innerOverride = -1, float outerOverride = -1) const;
    // Whether the cursor at `position` (relative to the bounds origin) is on
    // this arc's slice: within its angles and between inner and outer radius
    // (this arc's own radii when the overrides are negative).
    bool Contains(const ArcData& arc, Point position, Bounds bounds,
                  float innerOverride = -1, float outerOverride = -1) const;
    void Paint(PaintCtx* ctx, const ArcData& arc, Rgba color, Bounds bounds,
               float innerOverride = -1, float outerOverride = -1) const;
    // Paint, reusing the path tessellated by an earlier paint while its
    // angles, radii and the bounds size are unchanged.
    void PaintCached(PaintCtx* ctx, const ArcData& arc, Rgba color,
                     Bounds bounds, PathCache* cache, float innerOverride = -1,
                     float outerOverride = -1) const;
};

struct Pie {
    PlotValueFn value = nullptr;
    void* valueUser = nullptr;
    float startAngle = 0;
    float endAngle = 2 * kPi;
    float padAngle = 0;

    static Pie New();
    Pie* Value(PlotValueFn fn, void* user = nullptr);
    Pie* StartAngle(float value);
    Pie* EndAngle(float value);
    Pie* PadAngle(float value);
    void Arcs(Arena* arena, PlotItems items, ArenaVec<ArcData>* out) const;
};

struct RadialLine {
    PlotItems items = {};
    PlotValueFn angle = nullptr;
    void* angleUser = nullptr;
    PlotValueFn radius = nullptr;
    void* radiusUser = nullptr;
    bool closed = false;
    bool hasFill = false;
    Background fill = {};
    Background stroke = {};
    float strokeWidth = 1;
    bool dot = false;
    float dotSize = 4;
    Rgba dotFillColor = RgbaTransparent();
    bool hasDotStrokeColor = false;
    Rgba dotStrokeColor = {};

    static RadialLine New();
    RadialLine* Data(const void* values, int count, int stride);
    RadialLine* Angle(PlotValueFn fn, void* user = nullptr);
    RadialLine* Radius(PlotValueFn fn, void* user = nullptr);
    RadialLine* Closed(bool value = true);
    RadialLine* Fill(Background value);
    RadialLine* Stroke(Background value);
    RadialLine* StrokeWidth(float value);
    RadialLine* Dots(bool value = true);
    RadialLine* DotSize(float value);
    RadialLine* DotFill(Rgba value);
    RadialLine* DotStroke(Rgba value);
    int Points(Bounds bounds, Point* out, int capacity) const;
    void Paint(PaintCtx* ctx, Bounds bounds) const;
};

struct StackPoint {
    float y0 = 0;
    float y1 = 0;
    const void* data = nullptr;
};

struct StackSeries {
    Str key = {};
    int index = 0;
    ArenaVec<StackPoint> points;
};

typedef bool (*PlotStackValueFn)(const void* item, int index, Str key,
                                 void* user, float* out);

struct Stack {
    PlotItems items = {};
    const Str* keys = nullptr;
    int keyCount = 0;
    PlotStackValueFn value = nullptr;
    void* valueUser = nullptr;

    static Stack New();
    Stack* Data(const void* values, int count, int stride);
    Stack* Keys(const Str* values, int count);
    Stack* Value(PlotStackValueFn fn, void* user = nullptr);
    void Series(Arena* arena, ArenaVec<StackSeries>* out) const;
};

// shape/sankey.rs's immediate filled ribbon. It uses the already complete
// source-shaped layout records from src/base/sankey.h.
Path* SankeyLinkPath(PaintCtx* ctx, const SankeyNodeLayout& source,
                     const SankeyNodeLayout& target,
                     const SankeyLinkLayout& link, float minWidth,
                     Point origin);

enum class CrossLineAxis : uint8_t {
    Vertical,
    Horizontal,
    Both
};

struct CrossLine {
    Point point = {};
    float verticalStart = 0;
    bool hasVerticalLength = false;
    float verticalLength = 0;
    float horizontalStart = 0;
    bool hasHorizontalLength = false;
    float horizontalLength = 0;
    float thickness = 1;
    bool dashed = true;
    CrossLineAxis direction = CrossLineAxis::Vertical;

    static CrossLine New(Point point);
    CrossLine* Band(float value);
    CrossLine* Horizontal();
    CrossLine* Both();
    CrossLine* Height(float value);
    CrossLine* Width(float value);
    CrossLine* Span(float start, float length);
    CrossLine* HSpan(float start, float length);
    bool ShowVertical() const;
    bool ShowHorizontal() const;
    El* IntoEl(Ctx* cx) const;
};

struct Dot {
    Point point = {};
    float size = 6;
    Rgba stroke = RgbaTransparent();
    Rgba fill = RgbaTransparent();
    // Diameter of the translucent ring behind the dot; 0 draws no ring.
    float halo = 0;

    static Dot New(Point point);
    Dot* Size(float value);
    Dot* Stroke(Rgba value);
    Dot* Fill(Rgba value);
    Dot* Halo(float value);
    El* IntoEl(Ctx* cx) const;
};

struct TooltipState {
    int index = 0;
    Point crossLine = {};
    const Point* dots = nullptr;
    int dotCount = 0;

    static TooltipState New(int index, Point crossLine, const Point* dots,
                            int dotCount);
};

// The datum a plot has in focus this frame, handed to Plot::hover.
// Carries the TooltipState the cursor resolved to and how far the hover has
// faded in. After the cursor leaves, the state lingers here while focus
// eases back to zero.
struct PlotHover {
    TooltipState state = {};
    float focus = 0;
    bool hovered = false;

    const TooltipState& State() const { return state; }
    float Focus() const { return focus; }
    bool IsHovered() const { return hovered; }
    bool IsEntering() const { return hovered && focus == 0.f; }
    // PlotHover::glide: follow `target` the way a Tooltip glides its
    // crosshair and dots — on the pointer spring, adopting the target on the
    // entering frame. For a position a plot also paints with, such as the
    // centre of a highlighted band; the tooltip's own glide is then off.
    float Glide(Ctx* cx, motion::TransitionId id, float target) const;
};

// Resolve the datum a plot shows this frame from the live state the cursor
// resolved to. While live is set it is shown as is; after the cursor leaves,
// the last state lingers with its focus easing to zero. False means nothing
// is hovered and nothing is fading. The returned cursor is the live one, or
// the last one while the state lingers.
bool TrackHover(Ctx* cx, const TooltipState* live, const Point* cursor,
                PlotHover* outHover, Point* outCursor);

struct TooltipRow {
    Rgba color = {};
    Str label = {};
    Str value = {};
};

struct Tooltip {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    float gap = 0;
    bool hasCrossLine = false;
    CrossLine crossLine = {};
    ArenaVec<Dot> dots;
    bool appearance = true;
    bool hasTitle = false;
    Str title = {};
    ArenaVec<TooltipRow> rows;
    ArenaVec<El*> children;
    Point cursor = {};
    Size within = {};
    // Opacity of the whole overlay when set; see Focus. Negative means
    // follow the plot's tracked hover fade.
    float focus = -1.f;
    // Tooltip::glide: whether the crosshair and dots glide between data.
    bool glide = true;

    static Tooltip* New(Ctx* cx, Point cursor, Size within);
    Tooltip* Title(Str value);
    Tooltip* Row(Rgba color, Str label, Str value);
    Tooltip* Gap(float value);
    Tooltip* Cross(const CrossLine& value);
    Tooltip* Dots(const Dot* values, int count);
    Tooltip* Appearance(bool value);
    Tooltip* Child(El* value);
    Tooltip* Focus(float value);
    // Glide the crosshair and dots between data on the pointer spring, or
    // snap them to each datum. A crosshair glides along the axis it marks
    // only. Turn this off for positions the plot springs itself
    // (PlotHover::Glide). Default true.
    Tooltip* Glide(bool value);
    El* IntoEl();
};

} // namespace plot

// chart/mod.rs axis_point_count: how many points the x axis of a point chart
// (LineChart, AreaChart) is laid out for — `pointCount`, or the data's own
// length when that is unset (0) or smaller.
int ChartAxisPointCount(int pointCount, int dataLen);

// chart/mod.rs point_range: the x range a point scale spreads `dataLen`
// points over, when the axis is laid out for `pointCount` of them across
// `width` from `start`. The data takes the leading points, so each keeps its
// place as the data grows.
void ChartPointRange(float start, float width, int dataLen, int pointCount,
                     float out[2]);

// chart/mod.rs ValueExtent: the value range a y scale spans and the pixel
// range it maps onto, in double so a tick label can read the value at any
// height of the plot.
struct ChartValueExtent {
    double lo = 0;
    double hi = 0;
    float bottom = 0;
    float top = 0;

    // The value the scale puts at pixel `y`.
    double ValueAt(float y) const;
    // The pixel the scale puts `value` at; false for a scale with no extent.
    bool PositionOf(double value, float* out) const;
};

// chart/mod.rs point_value_scale: the y scale of a point chart, from
// `height` less the bottom y_padding up to the top one. A pinned domain
// (y_domain) maps its ends onto that range; otherwise the scale fits every
// series from zero. Rust takes the values as an iterator; here they are the
// ChartSeries the chart built. The extent is written when asked for.
ScaleLinear ChartPointValueScale(const ChartSeries& chart, float height,
                                 ChartValueExtent* extent = nullptr);

// VALUE_AXIS_GAP: the least space kept beside the plot for value-axis tick
// labels drawn outside it; wider labels widen it (value_axis_gap).
const float kChartValueAxisGap = 32;

// format_tick: whole numbers bare, the rest to one decimal. In `a`.
Str ChartFormatTick(Arena* a, double value);
// The chart's own format for a tick value: its tick format, or format_tick.
Str ChartTickLabel(Arena* a, const ChartSeries& chart, double value);

// labeled_items: which of `len` items carry a category label — `labelCount`
// of them spread evenly from the first to the last, or every `tickMargin`-th
// when it is -1 (None). Writes `len` flags.
void ChartLabeledItems(int len, int labelCount, int tickMargin, bool* out);

// PointAxes::tick_positions: `count` (at least 2) y ticks evenly spaced in
// pixels from the top edge (0) to the baseline (`height`), both included.
// Writes at most `cap`; returns how many there are.
int ChartTickPositions(int count, float height, float* out, int cap);

// BarChart::value_tick_labels: the value-axis tick label text, from the
// domain maximum at the far end down to the minimum at the baseline.
int ChartBarValueTickLabels(Arena* a, const ChartSeries& chart, Str* out,
                            int cap);

// BarChart::value_axis_gap: the gutter the value-axis labels take along the
// band axis — none unless they are shown outside the plot, a line of text
// below horizontal bars, and `measured` (value_axis_gap) beside vertical ones.
float ChartBarValueAxisGap(const ChartSeries& chart, float measured);

// build_point_x_labels' alignment: a label on the first point is
// left-aligned, one on the axis's last point right-aligned, and the rest —
// including the last datum of data laid out for more points — centered.
plot::PlotTextAlign ChartPointLabelAlign(int index, int pointCount);

// bar_chart.rs extend_to_min_length: push a bar's value end (`tick`, in
// pixels) away from `zero` until the bar is `min` long, in the direction its
// value grows for `alignment` — toward the origin for Bottom and Right.
float BarExtendToMinLength(float tick, float zero, bool negative,
                           BarAlign alignment, float min);

// bar_chart.rs value_tick_positions: `count` (at least 2) evenly spaced tick
// positions from `far` through `baseline`, both included. Writes at most
// `cap` and returns how many there are.
int ChartValueTickPositions(float farEdge, float baseline, int count,
                            float* out, int cap);

} // namespace component
} // namespace gpui
#endif // GPUI_UI_PLOT_H_
