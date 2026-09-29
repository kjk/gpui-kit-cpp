#ifndef GPUI_UI_PLOT_H_
#define GPUI_UI_PLOT_H_
/* Plotting for the charts — crates/ui/src/plot

   The unstyled primitives — scales, shapes, axes, grids, labels and hover
   tracking — live in Base (base/plot.h) and are re-exported here, as Rust's
   `pub use gpui_base::plot::*` does. This module adds the styled tooltip
   overlay (plot/tooltip.rs) and the chart helpers the runtime's chart
   renderer shares. */

#include "ui/chart.h"
#include "base/plot.h"

namespace gpui {

struct Path;

namespace component {

// The primitives moved to Base (crates/base/src/plot); Rust's component
// plot module re-exports them with `pub use gpui_base::plot::*`. The chart
// façade here names the scales and caches from `component`, as it did
// before the move.
using ::gpui::plot::kPlotAxisGap;
using ::gpui::plot::kPlotTextGap;
using ::gpui::plot::kPlotTextHeight;
using ::gpui::plot::kPlotTextSize;
using ::gpui::plot::PathCache;
using ::gpui::plot::PathCaches;
using ::gpui::plot::ScaleBand;
using ::gpui::plot::ScaleLinear;
using ::gpui::plot::ScaleOrdinal;
using ::gpui::plot::ScalePoint;
using ::gpui::plot::ShapeKey;

// Where a plot's tooltip box goes: it hugs the cursor and flips toward the
// centre past the halfway line, so it never runs off the near edge. Rust
// writes it as four `left/right/top/bottom` branches; the answer here is the
// box's own origin inside the plot.
//
// `gap` is the distance the box keeps from the cursor.
Point PlotTooltipPlace(Point cursor, Size within, Size box, float gap);

// The styled half of crates/ui/src/plot: the tooltip overlay. Its nested
// namespace keeps plot::Tooltip clear of crates/ui/src/tooltip.rs, and
// re-exports everything Base's plot module has.
namespace plot {

using namespace ::gpui::plot;

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

// A single labelled row in a Tooltip: an optional coloured swatch, a muted
// label, and a value, which reads in `valueColor` when it has one.
struct TooltipRow {
    Rgba color = {};
    Str label = {};
    Str value = {};
    Rgba valueColor = {};
    bool hasColor = false;
    bool hasValueColor = false;
};

// tooltip.rs has_swatches: whether the rows keep a swatch slot — when any
// has a swatch, so a plain row's label lines up with the series labels, and
// not when every row is plain.
bool TooltipHasSwatches(const ArenaVec<TooltipRow>& rows);

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
    // Opacity of the whole overlay when set; see Progress. Negative means
    // follow the plot's tracked hover fade.
    float progress = -1.f;
    // Tooltip::glide: whether the crosshair and dots glide between data.
    bool glide = true;

    static Tooltip* New(Ctx* cx, Point cursor, Size within);
    Tooltip* Title(Str value);
    Tooltip* Row(Rgba color, Str label, Str value);
    // plain_row: a row without a swatch, for a figure no series on the plot
    // draws, such as a total or a ratio. Among series rows its label lines
    // up with theirs; without any, the labels sit at the start.
    Tooltip* PlainRow(Str label, Str value);
    // value_color: colour the value of the row added last — by Row or
    // PlainRow — such as green or red by its sign. Before any row it does
    // nothing.
    Tooltip* ValueColor(Rgba color);
    Tooltip* Gap(float value);
    Tooltip* Cross(const CrossLine& value);
    Tooltip* Dots(const Dot* values, int count);
    Tooltip* Appearance(bool value);
    Tooltip* Child(El* value);
    // Fade the whole overlay — crosshair, dots and box — to `value` (0..1).
    // A tooltip rendered by a chart already follows the plot's hover, easing
    // in when the cursor lands on a datum and out after it leaves
    // (PlotHover::Progress); set this to override that, or to fade a tooltip
    // rendered outside a plot.
    Tooltip* Progress(float value);
    // Deprecated upstream for progress().
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
