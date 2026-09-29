#include "ui/plot.h"
#include "ui/popover.h"
#include "base/motion.h"

#include <math.h>
#include <string.h>

namespace gpui {

namespace component {

Point PlotTooltipPlace(Point cursor, Size within, Size box, float gap) {
    Point at;
    // Left of the middle, the box sits to the right of the cursor; past it,
    // the box's right edge is what hugs the cursor instead.
    if (cursor.x < within.w * 0.5f) {
        at.x = cursor.x + gap;
    } else {
        at.x = cursor.x - gap - box.w;
    }
    if (cursor.y < within.h * 0.5f) {
        at.y = cursor.y + gap;
    } else {
        at.y = cursor.y - gap - box.h;
    }
    // The flip is what keeps it inside; this is only for a box too big for
    // the plot to hold either way.
    if (at.x < 0) {
        at.x = 0;
    }
    if (at.y < 0) {
        at.y = 0;
    }
    return at;
}

namespace plot {

CrossLine CrossLine::New(Point value) {
    CrossLine out;
    out.point = value;
    return out;
}

CrossLine* CrossLine::Band(float value) {
    thickness = value;
    dashed = false;
    return this;
}

CrossLine* CrossLine::Horizontal() {
    direction = CrossLineAxis::Horizontal;
    return this;
}

CrossLine* CrossLine::Both() {
    direction = CrossLineAxis::Both;
    return this;
}

CrossLine* CrossLine::Height(float value) {
    hasVerticalLength = true;
    verticalLength = value;
    return this;
}

CrossLine* CrossLine::Width(float value) {
    hasHorizontalLength = true;
    horizontalLength = value;
    return this;
}

CrossLine* CrossLine::Span(float start, float length) {
    verticalStart = start;
    hasVerticalLength = true;
    verticalLength = length;
    return this;
}

CrossLine* CrossLine::HSpan(float start, float length) {
    horizontalStart = start;
    hasHorizontalLength = true;
    horizontalLength = length;
    return this;
}

bool CrossLine::ShowVertical() const {
    return direction == CrossLineAxis::Vertical ||
           direction == CrossLineAxis::Both;
}

bool CrossLine::ShowHorizontal() const {
    return direction == CrossLineAxis::Horizontal ||
           direction == CrossLineAxis::Both;
}

El* CrossLine::IntoEl(Ctx* cx) const {
    const Theme& theme = ThemeNow(cx->app);
    Rgba color = dashed ? RgbaMixHsl(theme.border, theme.foreground, .8f)
                        : RgbaOpacity(theme.foreground, .08f);
    float width = dashed ? 0 : thickness;
    El* root = Div(cx->a)->SizeFull()->Absolute()->Top(0)->Left(0);
    if (ShowVertical()) {
        El* line = Div(cx->a)
                       ->Absolute()
                       ->Left(point.x - width * .5f)
                       ->Top(verticalStart)
                       ->W(width)
                       ->H(hasVerticalLength ? verticalLength : kFill);
        if (dashed) {
            line->BorderL(1, color)->Dashed();
        } else {
            line->Bg(color);
        }
        root->Child(line);
    }
    if (ShowHorizontal()) {
        El* line = Div(cx->a)
                       ->Absolute()
                       ->Left(horizontalStart)
                       ->Top(point.y - width * .5f)
                       ->W(hasHorizontalLength ? horizontalLength : kFill)
                       ->H(width);
        if (dashed) {
            line->BorderT(1, color)->Dashed();
        } else {
            line->Bg(color);
        }
        root->Child(line);
    }
    return root;
}

Dot Dot::New(Point value) {
    Dot out;
    out.point = value;
    return out;
}

Dot* Dot::Size(float value) {
    size = value;
    return this;
}

Dot* Dot::Stroke(Rgba value) {
    stroke = value;
    return this;
}

Dot* Dot::Fill(Rgba value) {
    fill = value;
    return this;
}

Dot* Dot::Halo(float value) {
    halo = value;
    return this;
}

El* Dot::IntoEl(Ctx* cx) const {
    float offset = size * .5f - .5f;
    El* dot = Div(cx->a)
                  ->Absolute()
                  ->W(size)
                  ->H(size)
                  ->Radius(size * .5f)
                  ->Border(1, stroke)
                  ->Bg(fill)
                  ->Left(point.x - offset)
                  ->Top(point.y - offset);
    if (halo <= 0) {
        return dot;
    }
    El* ring = Div(cx->a)
                   ->Absolute()
                   ->W(halo)
                   ->H(halo)
                   ->Radius(halo * .5f)
                   ->Bg(RgbaOpacity(fill, 0.2f))
                   ->Left(point.x - halo * .5f)
                   ->Top(point.y - halo * .5f);
    return Div(cx->a)->Absolute()->Top(0)->Left(0)->Child(ring)->Child(dot);
}

Tooltip* Tooltip::New(Ctx* cx, Point cursor, Size within) {
    Tooltip* out = ArenaNew<Tooltip>(cx->a);
    out->a = cx->a;
    out->cx = cx;
    out->cursor = cursor;
    out->within = within;
    return out;
}

Tooltip* Tooltip::Title(Str value) {
    hasTitle = true;
    title = value;
    return this;
}

bool TooltipHasSwatches(const ArenaVec<TooltipRow>& rows) {
    for (const TooltipRow& row : rows) {
        if (row.hasColor) {
            return true;
        }
    }
    return false;
}

Tooltip* Tooltip::PlainRow(Str label, Str value) {
    TooltipRow row;
    row.label = label;
    row.value = value;
    rows.Append(a, row);
    return this;
}

Tooltip* Tooltip::ValueColor(Rgba color) {
    if (rows.len > 0) {
        rows[rows.len - 1].valueColor = color;
        rows[rows.len - 1].hasValueColor = true;
    }
    return this;
}

Tooltip* Tooltip::Row(Rgba color, Str label, Str value) {
    TooltipRow row;
    row.color = color;
    row.hasColor = true;
    row.label = label;
    row.value = value;
    rows.Append(a, row);
    return this;
}

Tooltip* Tooltip::Gap(float value) {
    gap = value;
    return this;
}

Tooltip* Tooltip::Cross(const plot::CrossLine& value) {
    hasCrossLine = true;
    crossLine = value;
    return this;
}

Tooltip* Tooltip::Dots(const Dot* values, int count) {
    if (values && count > 0) {
        dots.AppendMany(a, values, count);
    }
    return this;
}

Tooltip* Tooltip::Appearance(bool value) {
    appearance = value;
    return this;
}

Tooltip* Tooltip::Child(El* value) {
    if (value) {
        children.Append(a, value);
    }
    return this;
}

Tooltip* Tooltip::Progress(float value) {
    if (value < 0) {
        value = 0;
    }
    if (value > 1) {
        value = 1;
    }
    progress = value;
    return this;
}

Tooltip* Tooltip::Focus(float value) {
    return Progress(value);
}

Tooltip* Tooltip::Glide(bool value) {
    glide = value;
    return this;
}

El* Tooltip::IntoEl() {
    const Theme& theme = ThemeNow(cx->app);
    // Rendered within the plot's id scope, so this is the fade the plot
    // tracked for it this frame; fully opaque outside a plot.
    float overlay = progress >= 0 ? progress : HoverProgress(cx);
    bool entering = IsHoverEntering(cx);
    // Glide the crosshair along the axis it marks and each dot on both, on
    // the pointer spring, adopting the datum on the entering frame. GLIDE:
    // the spring ids a tooltip glides its crosshair with.
    if (glide && cx && cx->win) {
        Spring policy = PointerSpring(cx->app).WithTravel(!entering);
        if (hasCrossLine) {
            if (crossLine.ShowVertical()) {
                crossLine.point.x =
                    motion::spring(cx,
                                   motion::TransitionId(
                                       StrL("__plot-tooltip-glide"), StrL("x")),
                                   crossLine.point.x, policy);
            }
            if (crossLine.ShowHorizontal()) {
                crossLine.point.y =
                    motion::spring(cx,
                                   motion::TransitionId(
                                       StrL("__plot-tooltip-glide"), StrL("y")),
                                   crossLine.point.y, policy);
            }
        }
        for (int i = 0; i < dots.len; i++) {
            Str ix = StrDup(a, fmt("%d", i));
            dots[i].point.x = motion::spring(
                cx, motion::TransitionId(StrL("__plot-hover-dot-x"), ix),
                dots[i].point.x, policy);
            dots[i].point.y = motion::spring(
                cx, motion::TransitionId(StrL("__plot-hover-dot-y"), ix),
                dots[i].point.y, policy);
        }
    }
    El* root =
        Div(a)->SizeFull()->Absolute()->Top(0)->Left(0)->Opacity(overlay);
    if (hasCrossLine) {
        root->Child(crossLine.IntoEl(cx));
    }
    for (const Dot& dot : dots) {
        // The ring grows out of the dot as the hover fades in.
        Dot shown = dot;
        shown.halo = dot.halo * overlay;
        root->Child(shown.IntoEl(cx));
    }
    // v_flex().gap_y_1(): the row rhythm the structured content lays out
    // with, so a tooltip built from freeform children keeps it too. And one
    // size for every tooltip, structured or freeform, boxed or bare: the
    // compact text_xs tier.
    El* content = Div(a)->FlexCol()->Gap(4)->Font(12);
    if (hasTitle || rows.len > 0) {
        if (hasTitle) {
            content->Child(TextEl(a, title)->Semibold());
        }
        bool swatched = TooltipHasSwatches(rows);
        for (const TooltipRow& row : rows) {
            El* left = Div(a)->FlexRow()->ItemsCenter()->Gap(6);
            if (swatched) {
                El* swatch = Div(a)->W(8)->H(8)->Radius(theme.radius * .5f);
                if (row.hasColor) {
                    swatch->Bg(row.color);
                }
                left->Child(swatch);
            }
            left->Child(TextEl(a, row.label)->Fg(theme.mutedFg));
            El* value = TextEl(a, row.value);
            if (row.hasValueColor) {
                value->Fg(row.valueColor);
            }
            content->Child(Div(a)
                               ->FlexRow()
                               ->ItemsCenter()
                               ->JustifyBetween()
                               ->Gap(12)
                               ->Child(left)
                               ->Child(value));
        }
    } else {
        for (El* child : children) {
            content->Child(child);
        }
    }
    if (!appearance) {
        content->SizeFull()->Opacity(overlay);
    } else {
        PopoverSurface(cx, content)
            ->Absolute()
            ->MinW(150)
            ->Pad(8)
            ->Opacity(overlay);
        if (cursor.x < within.w * .5f) {
            content->Left(cursor.x + gap);
        } else {
            content->Right(within.w - cursor.x + gap);
        }
        if (cursor.y < within.h * .5f) {
            content->Top(cursor.y + gap);
        } else {
            content->Bottom(within.h - cursor.y + gap);
        }
    }
    root->Child(content->Deferred());
    return root;
}

} // namespace plot

int ChartAxisPointCount(int pointCount, int dataLen) {
    int count = pointCount > 0 ? pointCount : dataLen;
    return count > dataLen ? count : dataLen;
}

void ChartPointRange(float start, float width, int dataLen, int pointCount,
                     float out[2]) {
    float end = width;
    if (pointCount > 1) {
        int before = dataLen > 1 ? dataLen - 1 : 0;
        end = width * (float)before / (float)(pointCount - 1);
    }
    out[0] = start;
    out[1] = start + end;
}

double ChartValueExtent::ValueAt(float y) const {
    if (bottom == top) {
        return lo;
    }
    return lo + (hi - lo) * (double)((bottom - y) / (bottom - top));
}

bool ChartValueExtent::PositionOf(double value, float* out) const {
    if (hi == lo) {
        return false;
    }
    *out = bottom - (float)((value - lo) / (hi - lo)) * (bottom - top);
    return true;
}

ScaleLinear ChartPointValueScale(const ChartSeries& chart, float height,
                                 ChartValueExtent* extent) {
    const float range[2] = {height - chart.yPaddingBottom, chart.yPaddingTop};
    float lo = 0;
    float hi = 0;
    if (chart.pinnedDomain) {
        lo = chart.domainMin < chart.domainMax ? chart.domainMin
                                               : chart.domainMax;
        hi = chart.domainMin < chart.domainMax ? chart.domainMax
                                               : chart.domainMin;
    } else {
        // Every series from zero: the extent of the values with a zero
        // chained on, which is all a ScaleLinear keeps of its domain.
        for (int k = -1; k < chart.nMore; k++) {
            const float* ys = k < 0 ? chart.ys : chart.more[k].ys;
            for (int i = 0; ys && i < chart.n; i++) {
                lo = ys[i] < lo ? ys[i] : lo;
                hi = ys[i] > hi ? ys[i] : hi;
            }
        }
    }
    if (extent) {
        extent->lo = lo;
        extent->hi = hi;
        extent->bottom = range[0];
        extent->top = range[1];
    }
    const float domain[2] = {lo, hi};
    return ScaleLinear::New(domain, 2, range, 2);
}

Str ChartFormatTick(Arena* a, double value) {
    double rounded = (double)llround(value);
    double d = value - rounded;
    if ((d < 0 ? -d : d) < 0.001) {
        return StrDup(a, fmt("%.0f", value));
    }
    return StrDup(a, fmt("%.1f", value));
}

Str ChartTickLabel(Arena* a, const ChartSeries& chart, double value) {
    if (chart.tickFormat) {
        return chart.tickFormat(a, value, chart.tickFormatUser);
    }
    return ChartFormatTick(a, value);
}

void ChartLabeledItems(int len, int labelCount, int tickMargin, bool* out) {
    for (int i = 0; i < len; i++) {
        out[i] = false;
    }
    if (labelCount < 0) {
        int margin = tickMargin > 0 ? tickMargin : 1;
        for (int i = 0; i < len; i++) {
            out[i] = (i + 1) % margin == 0;
        }
        return;
    }
    if (labelCount == 0 || len == 0) {
        return;
    }
    if (labelCount == 1) {
        out[0] = true;
        return;
    }
    if (labelCount >= len) {
        for (int i = 0; i < len; i++) {
            out[i] = true;
        }
        return;
    }
    for (int k = 0; k < labelCount; k++) {
        int ix =
            (int)lroundf((float)k * (float)(len - 1) / (float)(labelCount - 1));
        out[ix] = true;
    }
}

int ChartTickPositions(int count, float height, float* out, int cap) {
    if (count < 2) {
        count = 2;
    }
    int n = 0;
    for (int i = 0; i < count && n < cap; i++) {
        out[n++] = height * (float)i / (float)(count - 1);
    }
    return n;
}

int ChartBarValueTickLabels(Arena* a, const ChartSeries& chart, Str* out,
                            int cap) {
    // The data plus zero, as the value scale spans, from the far end down.
    float lo = 0;
    float hi = 0;
    ChartValueDomain(chart, &lo, &hi);
    int count = chart.valueTickCount > 2 ? chart.valueTickCount : 2;
    float steps = (float)(count - 1);
    int n = 0;
    for (int i = 0; i < count && n < cap; i++) {
        double value = (double)(hi - (hi - lo) * (float)i / steps);
        out[n++] = ChartTickLabel(a, chart, value);
    }
    return n;
}

float ChartBarValueAxisGap(const ChartSeries& chart, float measured) {
    if (!chart.valueAxis ||
        chart.axisLabelPlacement != AxisLabelPlacement::Outside) {
        return 0;
    }
    if (chart.barAlign == BarAlign::Left || chart.barAlign == BarAlign::Right) {
        // Below the plot, where the gap is a line of text tall.
        return kChartValueAxisGap;
    }
    return measured;
}

plot::PlotTextAlign ChartPointLabelAlign(int index, int pointCount) {
    if (index == 0) {
        return pointCount == 1 ? plot::PlotTextAlign::Center
                               : plot::PlotTextAlign::Left;
    }
    if (index == pointCount - 1) {
        return plot::PlotTextAlign::Right;
    }
    return plot::PlotTextAlign::Center;
}

float BarExtendToMinLength(float tick, float zero, bool negative,
                           BarAlign alignment, float min) {
    float d = tick - zero;
    if ((d < 0 ? -d : d) >= min) {
        return tick;
    }
    bool towardOrigin =
        alignment == BarAlign::Bottom || alignment == BarAlign::Right;
    return towardOrigin != negative ? zero - min : zero + min;
}

int ChartValueTickPositions(float farEdge, float baseline, int count,
                            float* out, int cap) {
    if (count < 2) {
        count = 2;
    }
    float steps = (float)(count - 1);
    int written = 0;
    for (int i = 0; i < count && written < cap; i++) {
        out[written++] = farEdge + (baseline - farEdge) * (float)i / steps;
    }
    return written;
}

} // namespace component
} // namespace gpui
