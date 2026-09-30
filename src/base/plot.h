#ifndef GPUI_BASE_PLOT_H_
#define GPUI_BASE_PLOT_H_
/* Unstyled plotting — crates/base/src/plot

   Scales, shapes, axes, grids, labels and the hover tracking behind every
   plot. Colors are always handed in by the caller; a styled layer supplies
   chart defaults, the tooltip overlay and hover timing (PlotMotion, which
   the ui theme projects onto the Base theme).

   Rust's scales are generic over the domain and range types. Here the domain
   is always float — that is what a chart axis carries — and ScaleOrdinal maps
   indexes rather than values, since what it is for is picking one of N series
   colors by position. Domains are borrowed (pointer + length), so nothing
   here allocates. */

#include "gpui/gpui.h"
#include "base/motion.h"
#include "base/sankey.h"

namespace gpui {

struct Path;

namespace plot {

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
// The extent of the domain maps onto the range: the smallest value to
// range[0] and the largest to range[1], so a reversed range such as
// {height, 0} puts larger values higher. Rust takes the range as `[f32; 2]`;
// here it is a pointer and a count, of which the first two are the ends (a
// shorter one is `[0, 0]`).
struct ScaleLinear {
    float domainStart = 0;
    float domainDiff = 0;
    float rangeStart = 0;
    float rangeDiff = 0;

    static ScaleLinear New(const float* domain, int domainN, const float* range,
                           int rangeN);
    // The range position of `value`, or false when the domain has no extent to
    // divide by — Rust's `Option<f32>`.
    bool Tick(float value, float* out) const;
};

// ScalePoint — https://d3js.org/d3-scale/point
//
// Discrete domain values spread evenly from range[0] to range[1], the first
// and last landing on its ends, in that order even when the range is
// reversed. A one-value domain sits in the middle instead.
struct ScalePoint {
    const float* domain = nullptr;
    int domainLen = 0;
    float rangeStart = 0;
    float rangeTick = 0;

    static ScalePoint New(const float* domain, int domainN, const float* range,
                          int rangeN);
    // False when `value` is not in the domain.
    bool Tick(float value, float* out) const;
    // tick_at: the position of the domain value at `index`, without searching
    // the domain — Tick on domain[index] for a domain of unique values. False
    // past the end.
    bool TickAt(int index, float* out) const;
    // nearest_index: the domain index nearest `tick`.
    int NearestIndex(float tick) const;
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
    // The lower end of the range, whichever way it was written: bands lead
    // from there in domain order.
    float rangeStart = 0;
    float rangeDiff = 0;
    float avgWidth = 0;
    float paddingInner = 0;
    float paddingOuter = 0;
    // max_band_width: the widest a band may be; negative is unset.
    float maxBandWidth = -1;

    static ScaleBand New(int domainN, const float* range, int rangeN);
    // band_width: the range divided among the bands, less the inner padding,
    // and no wider than MaxBandWidth when it is set.
    float BandWidth() const;
    // max_band_width: cap the band width at `width`, so a few bands in a wide
    // range stay narrow; a band still starts where it would uncapped. Unset by
    // default.
    ScaleBand MaxBandWidth(float width) const;
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
    // nearest_index: the band nearest `tick`, clamped to the domain.
    int NearestIndex(float tick) const;
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

// label.rs: TEXT_SIZE, TEXT_GAP and TEXT_HEIGHT — the default label font
// size, the gap between a label and what it labels, and the height a label
// line takes. Defaults, not rules: a styled layer passes its own size and
// reserves axis space with AxisGutter for it.
const float kPlotTextSize = 10;
const float kPlotTextGap = 2;
const float kPlotTextHeight = kPlotTextSize + kPlotTextGap;
// AXIS_GAP, deprecated upstream for axis_gutter: the x-axis gutter for labels
// at the default kPlotTextSize.
const float kPlotAxisGap = 18;

// axis_gutter: the space below (or above) an x-axis line that tick labels of
// `fontSize` need — the gap PlotAxis leaves between the line and the labels,
// the labels themselves, and a trailing gap. 18 at the default size.
inline float AxisGutter(float fontSize) {
    return fontSize + kPlotTextGap * 4.f;
}

// mod.rs Curve: how a Line or Area connects its points, like d3's curve
// factories (curveNatural, curveLinear, curveStepAfter). The runtime's chart
// series already carries the same three as ChartStroke, so this is that
// type rather than a second one.
using Curve = ChartStroke;
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

// A tick label on a PlotAxis: its text, where along the axis it sits and how
// it is drawn. `fontSize` defaults to kPlotTextSize.
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

// Axis lines and their tick labels.
//
// The builders only record values: where the lines sit, which side their
// labels take and the labels themselves are combined when the axis paints,
// so they can be set in any order.
struct PlotAxis {
    Arena* a = nullptr;
    bool hasX = false;
    float x = 0;
    ArenaVec<AxisText> xLabels;
    bool xAxis = true;
    AxisLabelSide xLabelSide = AxisLabelSide::End;
    bool hasY = false;
    float y = 0;
    ArenaVec<AxisText> yLabels;
    bool yAxis = false;
    AxisLabelSide yLabelSide = AxisLabelSide::End;
    Background stroke = {};

    static PlotAxis New(Arena* arena);
    // Place the x-axis line at `value` from the top of the plot. Without it
    // the x-axis draws neither its line nor its labels.
    PlotAxis* X(float value);
    // Show or hide the x-axis line; its labels are drawn either way.
    PlotAxis* ShowXAxis(bool value);
    // The tick labels of the x-axis.
    PlotAxis* XLabel(const AxisText* labels, int count);
    PlotAxis* XLabelSide(AxisLabelSide value);
    PlotAxis* Y(float value);
    // Show or hide the y-axis line; its labels are drawn either way. Default
    // false.
    PlotAxis* ShowYAxis(bool value);
    PlotAxis* YLabel(const AxisText* labels, int count);
    PlotAxis* YLabelSide(AxisLabelSide value);
    // The stroke of the axis lines. The runtime draws a line with a solid
    // brush, so a gradient strokes with its first stop.
    PlotAxis* Stroke(Background value);
    // x_texts / y_texts: the labels placed against the line at `at`, in
    // `arena`. Exposed so the tests reach them the way Rust's do.
    ArenaVec<Text> XTexts(Arena* arena, float at) const;
    ArenaVec<Text> YTexts(Arena* arena, float at) const;
    void Paint(PaintCtx* ctx, Bounds bounds) const;
};

// Axis-aligned grid lines across a plot, at the given x and y positions.
struct Grid {
    const float* x = nullptr;
    int xCount = 0;
    const float* y = nullptr;
    int yCount = 0;
    Background stroke = {};
    const float* dashArray = nullptr;
    int dashCount = 0;

    static Grid New();
    Grid* X(const float* values, int count);
    Grid* Y(const float* values, int count);
    // The stroke of the grid lines; a gradient strokes with its first stop.
    Grid* Stroke(Background value);
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
    ::gpui::plot::Curve curve = ::gpui::plot::Curve::Natural;
    bool dot = false;
    float dotSize = 4;
    Background dotFill = Background(RgbaTransparent());
    bool hasDotStroke = false;
    Rgba dotStroke = {};

    static Line New();
    Line* Data(const void* values, int count, int stride);
    Line* X(PlotValueFn fn, void* user = nullptr);
    Line* Y(PlotValueFn fn, void* user = nullptr);
    Line* Stroke(Background value);
    Line* StrokeWidth(float value);
    // How the Line connects its points. Defaults to Curve::Natural.
    Line* Curve(::gpui::plot::Curve value);
    // Draw a dot on every point.
    Line* Dots(bool value = true);
    Line* DotSize(float value);
    // The fill of the dots.
    Line* DotFill(Background value);
    // The 1px border color of the dots. Defaults to the dot fill when it is a
    // solid color.
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
    ::gpui::plot::Curve curve = ::gpui::plot::Curve::Natural;

    static Area New();
    Area* Data(const void* values, int count, int stride);
    Area* X(PlotValueFn fn, void* user = nullptr);
    Area* Y0(float value);
    Area* Y1(PlotValueFn fn, void* user = nullptr);
    Area* Fill(Background value);
    Area* Stroke(Background value);
    // How the Area's top line connects its points. Defaults to
    // Curve::Natural.
    Area* Curve(::gpui::plot::Curve value);
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

// One slice of a Pie: its datum and the angles it spans, in radians with 0
// at 12 o'clock and positive angles proceeding clockwise.
struct ArcData {
    const void* data = nullptr;
    int index = 0;
    float value = 0;
    float startAngle = 0;
    float endAngle = 0;
    float padAngle = 0;

    // ArcData::new: a slice of `data` from `startAngle` to `endAngle`, with
    // no padding.
    static ArcData New(const void* data, int index, float value,
                       float startAngle, float endAngle) {
        ArcData out;
        out.data = data;
        out.index = index;
        out.value = value;
        out.startAngle = startAngle;
        out.endAngle = endAngle;
        return out;
    }
};

struct Arc {
    float innerRadius = 0;
    float outerRadius = 0;

    static Arc New();
    Arc* InnerRadius(float value);
    Arc* OuterRadius(float value);
    Point Centroid(const ArcData& arc) const;
    Path* PathFor(PaintCtx* ctx, const ArcData& arc, Bounds bounds) const;
    // Whether the cursor at `position` (relative to the bounds origin) is on
    // this arc's slice: within its angles and between this arc's radii.
    bool Contains(const ArcData& arc, Point position, Bounds bounds) const;
    void Paint(PaintCtx* ctx, const ArcData& arc, Background fill,
               Bounds bounds) const;
    // Paint, reusing the path tessellated by an earlier paint while its
    // angles, radii and the bounds size are unchanged. To paint one slice at
    // other radii (lifted on hover, say), build another Arc with those radii.
    void PaintCached(PaintCtx* ctx, const ArcData& arc, Background fill,
                     Bounds bounds, PathCache* cache) const;
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
    Background dotFill = Background(RgbaTransparent());
    bool hasDotStroke = false;
    Rgba dotStroke = {};

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
    // The fill of the dots, and their 1px border color, which defaults to
    // the fill when it is a solid color.
    RadialLine* DotFill(Background value);
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

// hover.rs TooltipState: the datum the cursor resolved to. Positions are
// relative to the plot's origin.
struct TooltipState {
    // The hovered datum's index in the plot's data.
    int index = 0;
    // Where a crosshair marking the datum sits.
    Point crossLine = {};
    // The data points to mark, one per series at the hovered datum.
    const Point* dots = nullptr;
    int dotCount = 0;

    static TooltipState New(int index, Point crossLine, const Point* dots,
                            int dotCount);
};

// The datum a plot has under the pointer this frame, handed to Plot::hover.
// Carries the TooltipState the cursor resolved to and how far the hover has
// faded in. After the cursor leaves, the state lingers here while the
// progress eases back to zero, so a hover-driven presentation can fade out
// over the last datum instead of vanishing.
struct PlotHover {
    TooltipState state = {};
    float progress = 0;
    bool hovered = false;

    const TooltipState& State() const { return state; }
    // How far the hover has faded in, from 0 to 1: it rises over the active
    // PlotMotion's enter when the cursor lands on a datum and falls back over
    // its exit after it leaves, during which IsHovered is false.
    float Progress() const { return progress; }
    // Deprecated upstream for progress().
    float Focus() const { return progress; }
    bool IsHovered() const { return hovered; }
    // The first frame the cursor is on a datum: the hover has not started
    // fading in yet.
    bool IsEntering() const { return hovered && progress == 0.f; }
    // PlotHover::glide: follow `target` on the pointer spring, adopting the
    // target on the entering frame instead of travelling from where the last
    // hover ended. For a position a plot paints with, such as the centre of a
    // highlighted band or the crosshair a styled tooltip draws.
    float Glide(Ctx* cx, motion::TransitionId id, float target) const;
};

// mod.rs PlotMotion: the timing of a plot's motion — how its data marks
// appear when it is first painted, how its hover progress fades in and out,
// and the spring a pointer follows the hovered datum with. Base installs no
// motion of its own: every duration defaults to zero, so the data is whole
// at once and the hover appears, fades and glides at once. Product timing
// belongs to the styled layer, which projects it through the Base theme's
// PlotTheme.
struct PlotMotion {
    Spring pointer = Spring::New(0);
    motion::Transition enter = motion::Transition::New(0);
    motion::Transition exit = motion::Transition::New(0);
    motion::Transition appear = motion::Transition::New(0);

    PlotMotion WithPointer(Spring value) const {
        PlotMotion copy = *this;
        copy.pointer = value;
        return copy;
    }
    PlotMotion WithEnter(motion::Transition value) const {
        PlotMotion copy = *this;
        copy.enter = value;
        return copy;
    }
    PlotMotion WithExit(motion::Transition value) const {
        PlotMotion copy = *this;
        copy.exit = value;
        return copy;
    }
    // How a plot's data marks appear the first time it is painted; see
    // PlotAppear.
    PlotMotion WithAppear(motion::Transition value) const {
        PlotMotion copy = *this;
        copy.appear = value;
        return copy;
    }
    Spring Pointer() const { return pointer; }
    const motion::Transition& Enter() const { return enter; }
    const motion::Transition& Exit() const { return exit; }
    const motion::Transition& Appear() const { return appear; }
};

// plot/appear.rs PlotAppear: how far a plot's data marks have appeared this
// frame. The appear starts on the first frame a plot's id is painted and
// runs over the active PlotMotion's appear. Base's default duration is zero,
// and reduced motion skips it, so a plot is then complete from its first
// frame.
struct PlotAppear {
    // Linear time through the appear, from 0 to 1.
    float time = 1.f;
    Easing easing = Easing::Linear();

    // A finished appear: every mark is complete.
    static PlotAppear Complete() { return {}; }
    // How far the whole plot has appeared, from 0 to 1, eased. A finished
    // appear skips sampling the curve, since charts read this per mark on
    // every frame long after the appear is done.
    float Progress() const;
    // Whether the appear is still running.
    bool IsAppearing() const { return time < 1.f; }
    // How far mark `index` of `count` has appeared, from 0 to 1, eased. The
    // marks start one after another across the first `spread` of the appear
    // (0..1) and each runs for the rest of it, so the last mark still
    // finishes with the appear however many marks there are. A `spread` of 0
    // moves every mark together.
    float Staggered(int index, int count, float spread) const;
};

// plot/appear.rs PlotAppearScope: remembers which plots inside it have
// finished appearing, so a plot that is painted again after a gap — a row a
// virtual list scrolled out of view and back — shows its data whole instead
// of drawing in again. Wrap the list, or whatever region repaints its plots
// on and off, in one.
//
// A plot is remembered by its id (its GlobalElementId: the chart's id folded
// onto the stack) and its appear generation, once its appear has finished; a
// new generation still replays it, and one taken away mid-appear draws in
// again from the start. The memory lasts while the scope is painted every
// frame and goes with it, so a scope that stops being painted, or whose id
// changes — name it after the content — draws its plots in afresh. The
// innermost scope wins. Without one, every plot draws in each time it is
// painted anew.
//
// The scope takes no part in layout: Rust hands on its child's LayoutId, and
// here the scope is a mark on the child element itself. A child that is
// already some scope's gets a plain box around it for the outer one.
struct PlotAppearScope {
    // PlotAppearScope::new(id, child): `id` unique among its siblings.
    static El* New(Ctx* cx, Str id, El* child);
};

// track_appear: sample the appear of the plot painting under `cx`'s id
// scope; a new `generation` starts it over, and one the innermost
// PlotAppearScope saw finish is complete at once. Rust's PlotElement calls
// it for a plot whose appear_generation is Some; the charts here paint
// themselves and call it under their own id scope, as they do TrackHover.
// The state is keyed on that scope and the generation and dropped with the
// plot, so a remounted plot appears again unless a scope remembers it.
PlotAppear TrackAppear(Ctx* cx, uint64_t generation);

// pointer_spring: the spring a hover pointer — the crosshair, highlight band
// or hover dot — follows the hovered datum with: the active PlotMotion's
// pointer, which snaps unless a styled layer projects one.
Spring PointerSpring(const App* app);

// track_hover: resolve the datum a plot shows this frame from the live state
// the cursor resolved to. While live is set it is shown as is; after the
// cursor leaves, the last state lingers with its progress easing to zero over
// the active PlotMotion's exit, then is dropped. False means nothing is
// hovered and nothing is fading. The returned cursor is the live one, or the
// last one while the state lingers. Rust calls it from PlotElement within the
// plot's element scope; the charts here call it under their own id scope.
bool TrackHover(Ctx* cx, const TooltipState* live, const Point* cursor,
                PlotHover* outHover, Point* outCursor);

// hover_progress: how far the enclosing plot's hover has faded in this frame,
// from 0 to 1, for an overlay that renders within the plot's id scope and
// fades with its hover without being handed the progress. Outside a plot it
// reads no tracked hover and returns 1.
float HoverProgress(Ctx* cx);

// is_hover_entering: whether this frame is the first the enclosing plot's
// cursor is on a datum; false outside a plot.
bool IsHoverEntering(Ctx* cx);

} // namespace plot
} // namespace gpui
#endif // GPUI_BASE_PLOT_H_
