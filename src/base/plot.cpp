#include "base/plot.h"
#include "base/theme.h"

#include <math.h>
#include <string.h>

namespace gpui {

namespace plot {

PathCache* PathCaches::Slot(int index) {
    if (index < 0) {
        return nullptr;
    }
    while (slots.len <= index) {
        VecAppend(slots, PathCache{});
    }
    return &slots[index];
}

void PathCaches::SlotPair(int index, PathCache** first, PathCache** second) {
    if (first) *first = Slot(index * 2);
    if (second) *second = Slot(index * 2 + 1);
}

ShapeKey& ShapeKey::U64(uint64_t v) {
    for (int i = 0; i < 8; i++) {
        value = (value ^ (uint8_t)(v >> (i * 8))) * 1099511628211ull;
    }
    return *this;
}

ShapeKey& ShapeKey::PointValue(Point point) {
    return Float(point.x).Float(point.y);
}

ShapeKey& ShapeKey::Float(float v) {
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    return U64(bits);
}

static void MinMax(const float* v, int n, float* outMin, float* outMax) {
    if (n <= 0) {
        *outMin = 0;
        *outMax = 0;
        return;
    }
    float lo = v[0];
    float hi = v[0];
    for (int i = 1; i < n; i++) {
        if (v[i] < lo) {
            lo = v[i];
        }
        if (v[i] > hi) {
            hi = v[i];
        }
    }
    *outMin = lo;
    *outMax = hi;
}

// The two ends of a range handed as a pointer and a count: Rust's `[f32; 2]`.
// A range with fewer than two entries is `[0, 0]`.
static void RangeEnds(const float* range, int rangeN, float* r0, float* r1) {
    if (!range || rangeN < 2) {
        *r0 = 0;
        *r1 = 0;
        return;
    }
    *r0 = range[0];
    *r1 = range[1];
}

ScaleLinear ScaleLinear::New(const float* domain, int domainN,
                             const float* range, int rangeN) {
    float domainMin = 0, domainMax = 0;
    MinMax(domain, domainN, &domainMin, &domainMax);
    float r0 = 0, r1 = 0;
    RangeEnds(range, rangeN, &r0, &r1);

    ScaleLinear s;
    s.domainStart = domainMin;
    s.domainDiff = domainMax - domainMin;
    s.rangeStart = r0;
    s.rangeDiff = r1 - r0;
    return s;
}

bool ScaleLinear::Tick(float value, float* out) const {
    if (domainDiff == 0) {
        return false;
    }
    float ratio = (value - domainStart) / domainDiff;
    *out = ratio * rangeDiff + rangeStart;
    return true;
}

ScalePoint ScalePoint::New(const float* domain, int domainN, const float* range,
                           int rangeN) {
    ScalePoint s;
    s.domain = domain;
    s.domainLen = domainN;
    if (domainN == 0) {
        return s;
    }
    float r0 = 0, r1 = 0;
    RangeEnds(range, rangeN, &r0, &r1);
    float diff = r1 - r0;
    s.rangeStart = r0;
    s.rangeTick = domainN == 1 ? diff : diff / (float)(domainN - 1);
    return s;
}

bool ScalePoint::Tick(float value, float* out) const {
    int index = -1;
    for (int i = 0; i < domainLen; i++) {
        if (domain[i] == value) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        return false;
    }
    return TickAt(index, out);
}

bool ScalePoint::TickAt(int index, float* out) const {
    if (index < 0 || index >= domainLen) {
        return false;
    }
    // A single point has no spacing to step by, so it sits in the middle.
    *out = domainLen == 1 ? rangeStart + rangeTick * 0.5f
                          : rangeStart + (float)index * rangeTick;
    return true;
}

int ScalePoint::NearestIndex(float tick) const {
    if (domainLen <= 0 || rangeTick == 0) {
        return 0;
    }
    // roundf, not rint: a tick exactly between two points belongs to the
    // later one, which is what Rust's f32::round does and what the ties in
    // the reference tests assert. A reversed range steps backwards, and a
    // negative index saturates at 0 as Rust's `as usize` does.
    float index = roundf((tick - rangeStart) / rangeTick);
    if (index < 0) {
        return 0;
    }
    if (index > (float)(domainLen - 1)) {
        return domainLen - 1;
    }
    return (int)index;
}

// The width of one band before the padding between them comes off, which is
// what Rust's `avg_width` is.
ScaleBand ScaleBand::New(int domainN, const float* range, int rangeN) {
    ScaleBand b;
    b.domainLen = domainN < 0 ? 0 : domainN;
    float r0 = 0, r1 = 0;
    RangeEnds(range, rangeN, &r0, &r1);
    b.rangeStart = r0 < r1 ? r0 : r1;
    b.rangeDiff = r1 > r0 ? r1 - r0 : r0 - r1;
    b.avgWidth = b.domainLen > 0 ? b.rangeDiff / (float)b.domainLen : 0;
    return b;
}

ScaleBand ScaleBand::BandCount(int count) const {
    ScaleBand out = *this;
    if (count > out.domainLen) {
        out.domainLen = count;
        out.avgWidth = out.rangeDiff / (float)count;
    }
    return out;
}

ScaleBand ScaleBand::MaxBandWidth(float width) const {
    ScaleBand out = *this;
    out.maxBandWidth = width;
    return out;
}

float ScaleBand::BandWidth() const {
    float w = avgWidth * (1.f - paddingInner);
    if (maxBandWidth >= 0 && w > maxBandWidth) {
        return maxBandWidth;
    }
    return w;
}

// The gap the inner padding opens between bands, spread over the ones that
// are left.
static float ScaleBandRatio(const ScaleBand& b) {
    if (b.domainLen <= 1) {
        return 1.f;
    }
    return 1.f + b.paddingInner / (float)(b.domainLen - 1);
}

// display_avg_width: what one band takes once the outer padding is off both
// ends.
static float ScaleBandDisplayAvgWidth(const ScaleBand& b) {
    if (b.domainLen <= 0) {
        return 0;
    }
    float outer = b.avgWidth * b.paddingOuter;
    return (b.rangeDiff - outer * 2.f) / (float)b.domainLen;
}

float ScaleBand::Step() const {
    if (domainLen <= 1) {
        return rangeDiff;
    }
    return ScaleBandDisplayAvgWidth(*this) * ScaleBandRatio(*this);
}

bool ScaleBand::Tick(int index, float* out) const {
    if (index < 0 || index >= domainLen) {
        return false;
    }
    if (domainLen == 1) {
        // One band sits in the middle of the range.
        *out = rangeStart + (rangeDiff - BandWidth()) / 2.f;
        return true;
    }
    float avg = ScaleBandDisplayAvgWidth(*this);
    float outer = avgWidth * paddingOuter;
    *out = rangeStart + (float)index * avg * ScaleBandRatio(*this) + outer;
    return true;
}

int ScaleBand::NearestIndex(float tick) const {
    if (domainLen <= 1) {
        return 0;
    }
    float avg = ScaleBandDisplayAvgWidth(*this);
    float outer = avgWidth * paddingOuter;
    float step = avg * ScaleBandRatio(*this);
    if (step == 0) {
        return 0;
    }
    int index = (int)lroundf((tick - rangeStart - outer) / step);
    if (index < 0) {
        index = 0;
    }
    if (index > domainLen - 1) {
        index = domainLen - 1;
    }
    return index;
}

int ScaleOrdinal::Map(int domainIndex) const {
    if (domainIndex < 0) {
        return unknown;
    }
    if (rangeLen <= 0) {
        return -1;
    }
    return domainIndex % rangeLen;
}

static uint8_t PlotFontWeight(FontWeight weight) {
    switch (weight) {
        case FontWeight::Thin:
            return kFontWeightThin;
        case FontWeight::ExtraLight:
            return kFontWeightExtraLight;
        case FontWeight::Light:
            return kFontWeightLight;
        case FontWeight::Normal:
            return kFontWeightExplicitNormal;
        case FontWeight::Medium:
            return kFontWeightMedium;
        case FontWeight::Semibold:
            return kFontWeightSemibold;
        case FontWeight::Bold:
            return kFontWeightBold;
        case FontWeight::ExtraBold:
            return kFontWeightExtraBold;
        case FontWeight::Black:
            return kFontWeightBlack;
    }
    return kFontWeightExplicitNormal;
}

static void PaintPathFill(PaintCtx* ctx, Path* path, Background fill,
                          Bounds bounds) {
    if (!path) {
        return;
    }
    if (!fill.gradient) {
        PathFill(ctx, path, fill.color);
        return;
    }
    Point p0 = {}, p1 = {};
    BackgroundLine(fill, bounds, &p0, &p1);
    PathFillGradient(ctx, path, p0.x, p0.y, p1.x, p1.y, fill.from.color,
                     fill.to.color);
}

// GPUI accepts Background for a path stroke. The portable backend's stroke
// primitive is a solid brush, so the representative first stop is used for
// gradient strokes; fills retain the complete two-stop gradient above.
static void PaintPathStroke(PaintCtx* ctx, Path* path, float width,
                            Background stroke) {
    if (path) {
        PathStroke(ctx, path, width, stroke.color);
    }
}

Path* Polygon(PaintCtx* ctx, const Point* points, int count, Bounds bounds) {
    if (!ctx || !points || count <= 0) {
        return nullptr;
    }
    Path* path = PathNew(ctx, false);
    if (!path) {
        return nullptr;
    }
    PathMoveTo(path, bounds.x + points[0].x, bounds.y + points[0].y);
    for (int i = 1; i < count; i++) {
        PathLineTo(path, bounds.x + points[i].x, bounds.y + points[i].y);
    }
    return path;
}

Text Text::New(Str value, Point at, Rgba ink) {
    Text out;
    out.text = value;
    out.origin = at;
    out.color = ink;
    return out;
}

Text* Text::FontSize(float value) {
    fontSize = value;
    return this;
}

Text* Text::Weight(FontWeight value) {
    fontWeight = value;
    return this;
}

Text* Text::Align(PlotTextAlign value) {
    align = value;
    return this;
}

float MeasureTextWidth(PaintCtx* ctx, Str text, float fontSize) {
    if (!ctx || !ctx->pa || !text.s || len(text) <= 0) {
        return 0;
    }
    return MeasureText(ctx, text, fontSize, 0, false, kFontWeightExplicitNormal,
                       0)
        .w;
}

static Str PrefixEllipsis(Arena* arena, Str text, int prefix) {
    static const char kEllipsis[] = "\xe2\x80\xa6";
    char* out = (char*)Alloc(arena, prefix + 4);
    if (!out) {
        return {};
    }
    if (prefix > 0) {
        memcpy(out, text.s, (size_t)prefix);
    }
    memcpy(out + prefix, kEllipsis, 3);
    out[prefix + 3] = 0;
    return Str(out, prefix + 3);
}

Str TruncateTextToWidth(PaintCtx* ctx, Arena* arena, Str text, float fontSize,
                        float maxWidth) {
    if (maxWidth <= 0 || MeasureTextWidth(ctx, text, fontSize) <= maxWidth) {
        return text;
    }
    Arena* outArena = arena ? arena : GetTempArena();
    Vec<int> cuts;
    for (int at = 0; at < len(text);) {
        uint32_t rune = 0;
        int n = Utf8At(text, at, &rune);
        (void)rune;
        if (n <= 0) {
            n = 1;
        }
        at += n;
        if (at < len(text)) {
            VecAppend(cuts, at);
        }
    }
    int best = -1;
    int lo = 0;
    int hi = len(cuts);
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        Str candidate = PrefixEllipsis(outArena, text, cuts[mid]);
        if (MeasureTextWidth(ctx, candidate, fontSize) <= maxWidth) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return PrefixEllipsis(outArena, text, best >= 0 ? cuts[best] : 0);
}

PlotLabel PlotLabel::New(Arena* arena) {
    PlotLabel out;
    out.a = arena;
    return out;
}

PlotLabel* PlotLabel::Add(const Text& text) {
    items.Append(a, text);
    return this;
}

PlotLabel* PlotLabel::AddMany(const Text* text, int count) {
    if (text && count > 0) {
        items.AppendMany(a, text, count);
    }
    return this;
}

void PlotLabel::Paint(PaintCtx* ctx, Bounds bounds) const {
    if (!ctx || !ctx->pa) {
        return;
    }
    for (const Text& item : items) {
        if (!item.text.s || len(item.text) <= 0) {
            continue;
        }
        // Use the window's shaped-text cache, as GPUI's text_system does.
        // Fresh layouts here reshaped every tick and gave an unchanged label
        // a new scene resource generation, forcing a full repaint.
        int weight = PlotFontWeight(item.fontWeight);
        Size measured =
            MeasureText(ctx, item.text, item.fontSize, 0, false, weight, 0);
        float x = bounds.x + item.origin.x;
        if (item.align == PlotTextAlign::Right) {
            x -= measured.w;
        } else if (item.align == PlotTextAlign::Center) {
            x -= measured.w * 0.5f;
        }
        DrawTextAt(ctx, item.text, x, bounds.y + item.origin.y, measured.w,
                   measured.h, item.fontSize, item.color, false, false, 0,
                   weight, 0);
    }
}

AxisText AxisText::New(Str value, float at, Rgba ink) {
    AxisText out;
    out.text = value;
    out.tick = at;
    out.color = ink;
    return out;
}

AxisText* AxisText::FontSize(float value) {
    fontSize = value;
    return this;
}

AxisText* AxisText::Align(PlotTextAlign value) {
    align = value;
    return this;
}

PlotAxis PlotAxis::New(Arena* arena) {
    PlotAxis out;
    out.a = arena;
    out.xAxis = true;
    return out;
}

PlotAxis* PlotAxis::X(float value) {
    hasX = true;
    x = value;
    return this;
}

PlotAxis* PlotAxis::ShowXAxis(bool value) {
    xAxis = value;
    return this;
}

PlotAxis* PlotAxis::XLabel(const AxisText* labels, int count) {
    xLabels = {};
    if (labels && count > 0) {
        xLabels.AppendMany(a, labels, count);
    }
    return this;
}

PlotAxis* PlotAxis::XLabelSide(AxisLabelSide value) {
    xLabelSide = value;
    return this;
}

PlotAxis* PlotAxis::Y(float value) {
    hasY = true;
    y = value;
    return this;
}

PlotAxis* PlotAxis::ShowYAxis(bool value) {
    yAxis = value;
    return this;
}

PlotAxis* PlotAxis::YLabel(const AxisText* labels, int count) {
    yLabels = {};
    if (labels && count > 0) {
        yLabels.AppendMany(a, labels, count);
    }
    return this;
}

PlotAxis* PlotAxis::YLabelSide(AxisLabelSide value) {
    yLabelSide = value;
    return this;
}

PlotAxis* PlotAxis::Stroke(Background value) {
    stroke = value;
    return this;
}

// axis_text: `label` as the text PlotLabel draws at `origin`.
static Text AxisTextAt(const AxisText& label, Point origin) {
    Text text = Text::New(label.text, origin, label.color);
    text.fontSize = label.fontSize;
    text.fontWeight = FontWeight::Normal;
    text.align = label.align;
    return text;
}

ArenaVec<Text> PlotAxis::XTexts(Arena* arena, float at) const {
    ArenaVec<Text> out;
    float labelY = xLabelSide == AxisLabelSide::End
                       ? at + kPlotTextGap * 3.f
                       : at - (kPlotTextGap + kPlotTextHeight);
    for (const AxisText& label : xLabels) {
        out.Append(arena, AxisTextAt(label, {label.tick, labelY}));
    }
    return out;
}

ArenaVec<Text> PlotAxis::YTexts(Arena* arena, float at) const {
    ArenaVec<Text> out;
    float labelX = yLabelSide == AxisLabelSide::End ? at + kPlotTextGap
                                                    : at - kPlotTextGap;
    for (const AxisText& label : yLabels) {
        out.Append(
            arena,
            AxisTextAt(label, {labelX, label.tick - kPlotTextSize * 0.5f}));
    }
    return out;
}

void PlotAxis::Paint(PaintCtx* ctx, Bounds bounds) const {
    Arena* arena = a ? a : GetTempArena();
    if (hasX) {
        if (xAxis) {
            CanvasLine(ctx, bounds.x, bounds.y + x, bounds.x + bounds.w,
                       bounds.y + x, 1, stroke.color);
        }
        PlotLabel label = PlotLabel::New(arena);
        label.items = XTexts(arena, x);
        label.Paint(ctx, bounds);
    }
    if (hasY) {
        if (yAxis) {
            CanvasLine(ctx, bounds.x + y, bounds.y, bounds.x + y,
                       bounds.y + bounds.h, 1, stroke.color);
        }
        PlotLabel label = PlotLabel::New(arena);
        label.items = YTexts(arena, y);
        label.Paint(ctx, bounds);
    }
}

Grid Grid::New() {
    return Grid{};
}

Grid* Grid::X(const float* values, int count) {
    x = values;
    xCount = count > 0 ? count : 0;
    return this;
}

Grid* Grid::Y(const float* values, int count) {
    y = values;
    yCount = count > 0 ? count : 0;
    return this;
}

Grid* Grid::Stroke(Background value) {
    stroke = value;
    return this;
}

Grid* Grid::DashArray(const float* values, int count) {
    dashArray = values;
    dashCount = count > 0 ? count : 0;
    return this;
}

static void PaintPlotLine(PaintCtx* ctx, Point a, Point b, Rgba color,
                          const float* dash, int dashCount) {
    if (!dash || dashCount <= 0) {
        CanvasLine(ctx, a.x, a.y, b.x, b.y, 1, color);
        return;
    }
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float length = sqrtf(dx * dx + dy * dy);
    float pattern = 0;
    for (int i = 0; i < dashCount; i++) {
        if (dash[i] > 0) {
            pattern += dash[i];
        }
    }
    if (length <= 0 || pattern <= 0) {
        CanvasLine(ctx, a.x, a.y, b.x, b.y, 1, color);
        return;
    }
    float ux = dx / length;
    float uy = dy / length;
    float at = 0;
    int ix = 0;
    bool draw = true;
    while (at < length) {
        float run = dash[ix] > 0 ? dash[ix] : 0;
        float end = at + run;
        if (end > length) {
            end = length;
        }
        if (draw && end > at) {
            CanvasLine(ctx, a.x + ux * at, a.y + uy * at, a.x + ux * end,
                       a.y + uy * end, 1, color);
        }
        at = end;
        ix = (ix + 1) % dashCount;
        draw = !draw;
        if (run <= 0) {
            // A whole zero pattern was handled above; advancing the index is
            // enough to avoid stalling on an individual zero entry.
            continue;
        }
    }
}

void Grid::Paint(PaintCtx* ctx, Bounds bounds) const {
    for (int i = 0; i < xCount; i++) {
        float px = bounds.x + x[i];
        PaintPlotLine(ctx, {px, bounds.y}, {px, bounds.y + bounds.h},
                      stroke.color, dashArray, dashCount);
    }
    for (int i = 0; i < yCount; i++) {
        float py = bounds.y + y[i];
        PaintPlotLine(ctx, {bounds.x, py}, {bounds.x + bounds.w, py},
                      stroke.color, dashArray, dashCount);
    }
}

const void* PlotItems::At(int index) const {
    if (!data || index < 0 || index >= count || stride <= 0) {
        return nullptr;
    }
    return (const uint8_t*)data + (size_t)index * (size_t)stride;
}

static bool PlotValue(PlotValueFn fn, const PlotItems& items, int index,
                      void* user, float* out) {
    const void* item = items.At(index);
    return item && fn && fn(item, index, user, out);
}

Line Line::New() {
    Line out;
    out.stroke = RgbaTransparent();
    return out;
}

Line* Line::Data(const void* values, int count, int stride) {
    items = PlotItems{values, count > 0 ? count : 0, stride};
    return this;
}

Line* Line::X(PlotValueFn fn, void* user) {
    x = fn;
    xUser = user;
    return this;
}

Line* Line::Y(PlotValueFn fn, void* user) {
    y = fn;
    yUser = user;
    return this;
}

Line* Line::Stroke(Background value) {
    stroke = value;
    return this;
}

Line* Line::StrokeWidth(float value) {
    strokeWidth = value;
    return this;
}

Line* Line::Curve(::gpui::plot::Curve value) {
    curve = value;
    return this;
}

Line* Line::Dots(bool value) {
    dot = value;
    return this;
}

Line* Line::DotSize(float value) {
    dotSize = value;
    return this;
}

Line* Line::DotFill(Background value) {
    dotFill = value;
    return this;
}

Line* Line::DotStroke(Rgba value) {
    hasDotStroke = true;
    dotStroke = value;
    return this;
}

int Line::Points(Bounds bounds, Point* out, int capacity) const {
    int count = 0;
    for (int i = 0; i < items.count; i++) {
        float px = 0, py = 0;
        if (!PlotValue(x, items, i, xUser, &px) ||
            !PlotValue(y, items, i, yUser, &py)) {
            continue;
        }
        if (out && count < capacity) {
            out[count] = OriginPoint(px, py, {bounds.x, bounds.y});
        }
        count++;
    }
    return count;
}

static void PlotRun(Path* path, const Vec<Point>& points,
                    ::gpui::plot::Curve style) {
    if (!path || len(points) <= 0) {
        return;
    }
    PathMoveTo(path, points[0].x, points[0].y);
    if (len(points) == 1) {
        return;
    }
    if (style == ::gpui::plot::Curve::Linear) {
        for (int i = 1; i < len(points); i++) {
            PathLineTo(path, points[i].x, points[i].y);
        }
        return;
    }
    if (style == ::gpui::plot::Curve::StepAfter) {
        for (int i = 0; i < len(points) - 1; i++) {
            PathLineTo(path, points[i + 1].x, points[i].y);
            if (i < len(points) - 2) {
                PathLineTo(path, points[i + 1].x, points[i + 1].y);
            }
        }
        return;
    }
    for (int i = 0; i < len(points) - 1; i++) {
        const Point& p0 = i == 0 ? points[0] : points[i - 1];
        const Point& p1 = points[i];
        const Point& p2 = points[i + 1];
        const Point& p3 =
            i + 2 < len(points) ? points[i + 2] : points[len(points) - 1];
        PathCubicTo(path, p1.x + (p2.x - p0.x) / 6.f,
                    p1.y + (p2.y - p0.y) / 6.f, p2.x - (p3.x - p1.x) / 6.f,
                    p2.y - (p3.y - p1.y) / 6.f, p2.x, p2.y);
    }
}

static Vec<Point> ResolveLinePoints(const Line& line, Bounds bounds) {
    Vec<Point> points;
    for (int i = 0; i < line.items.count; i++) {
        float x = 0, y = 0;
        if (PlotValue(line.x, line.items, i, line.xUser, &x) &&
            PlotValue(line.y, line.items, i, line.yUser, &y)) {
            VecAppend(points, OriginPoint(x, y, {bounds.x, bounds.y}));
        }
    }
    return points;
}

// paint_dot: a round quad in the dot fill with a 1px border in the dot
// stroke, which defaults to the fill when that is a solid color and to
// nothing when it is a gradient. The runtime's ellipse takes one color, so a
// gradient fill paints its first stop.
static void PaintDot(PaintCtx* ctx, Point point, float size, Background fill,
                     bool hasStroke, Rgba stroke) {
    float radius = size * 0.5f;
    Rgba edge = hasStroke        ? stroke
                : !fill.gradient ? fill.color
                                 : RgbaTransparent();
    CanvasEllipse(ctx, point.x, point.y, radius, radius, 0, fill.color);
    CanvasEllipse(ctx, point.x, point.y, radius, radius, 1, edge);
}

void Line::Paint(PaintCtx* ctx, Bounds bounds) const {
    Vec<Point> points = ResolveLinePoints(*this, bounds);
    if (len(points) > 0) {
        Path* path = PathNew(ctx, false);
        PlotRun(path, points, curve);
        PaintPathStroke(ctx, path, strokeWidth, stroke);
        PathFree(path);
    }
    if (dot) {
        for (int i = 0; i < len(points); i++) {
            PaintDot(ctx, points[i], dotSize, dotFill, hasDotStroke, dotStroke);
        }
    }
}

Area Area::New() {
    Area out;
    out.fill = RgbaTransparent();
    out.stroke = RgbaTransparent();
    return out;
}

Area* Area::Data(const void* values, int count, int stride) {
    items = PlotItems{values, count > 0 ? count : 0, stride};
    return this;
}

Area* Area::X(PlotValueFn fn, void* user) {
    x = fn;
    xUser = user;
    return this;
}

Area* Area::Y0(float value) {
    hasY0 = true;
    y0 = value;
    return this;
}

Area* Area::Y1(PlotValueFn fn, void* user) {
    y1 = fn;
    y1User = user;
    return this;
}

Area* Area::Fill(Background value) {
    fill = value;
    return this;
}

Area* Area::Stroke(Background value) {
    stroke = value;
    return this;
}

Area* Area::Curve(::gpui::plot::Curve value) {
    curve = value;
    return this;
}

void Area::Paint(PaintCtx* ctx, Bounds bounds) const {
    Vec<Point> points;
    bool hasFirst = false, hasLast = false;
    float first = 0, last = 0;
    for (int i = 0; i < items.count; i++) {
        float px = 0;
        bool hasPx = PlotValue(x, items, i, xUser, &px);
        if (i == 0 && hasPx) {
            hasFirst = true;
            first = px;
        }
        if (i == items.count - 1 && hasPx) {
            hasLast = true;
            last = px;
        }
        float py = 0;
        if (hasPx && PlotValue(y1, items, i, y1User, &py)) {
            VecAppend(points, OriginPoint(px, py, {bounds.x, bounds.y}));
        }
    }
    if (len(points) <= 0) {
        return;
    }
    Path* area = PathNew(ctx, true);
    Path* line = PathNew(ctx, false);
    PlotRun(area, points, curve);
    PlotRun(line, points, curve);
    if (len(points) > 1 && hasY0 && hasFirst && hasLast) {
        PathLineTo(area, bounds.x + last, bounds.y + y0);
        PathLineTo(area, bounds.x + first, bounds.y + y0);
        PathClose(area);
    }
    PaintPathFill(ctx, area, fill, bounds);
    PaintPathStroke(ctx, line, 1, stroke);
    PathFree(area);
    PathFree(line);
}

bool BarAlignmentIsHorizontal(BarAlignment value) {
    return value == BarAlignment::Left || value == BarAlignment::Right;
}

float BarAlignmentGradientAngle(BarAlignment value) {
    switch (value) {
        case BarAlignment::Bottom:
            return 0;
        case BarAlignment::Top:
            return 180;
        case BarAlignment::Left:
            return 90;
        case BarAlignment::Right:
            return 270;
    }
    return 0;
}

Point BarLabelOrigin(BarAlignment alignment, float cross, float base,
                     float value, float bandWidth) {
    if (alignment == BarAlignment::Bottom) {
        float center = cross + bandWidth * 0.5f;
        return {center,
                value <= base ? value - kPlotTextHeight : value + kPlotTextGap};
    }
    if (alignment == BarAlignment::Top) {
        float center = cross + bandWidth * 0.5f;
        return {center,
                value >= base ? value + kPlotTextGap : value - kPlotTextHeight};
    }
    float center = cross + bandWidth * 0.5f - kPlotTextSize * 0.5f;
    if (alignment == BarAlignment::Left) {
        return {value >= base ? value + kPlotTextGap : value - kPlotTextGap,
                center};
    }
    return {value <= base ? value - kPlotTextGap : value + kPlotTextGap,
            center};
}

Bar Bar::New() {
    return Bar{};
}

Bar* Bar::Data(const void* values, int count, int stride) {
    items = PlotItems{values, count > 0 ? count : 0, stride};
    return this;
}

Bar* Bar::Alignment(BarAlignment next) {
    alignment = next;
    return this;
}

Bar* Bar::Cross(PlotValueFn fn, void* user) {
    cross = fn;
    crossUser = user;
    return this;
}

Bar* Bar::BandWidth(float width) {
    bandWidth = width;
    return this;
}

Bar* Bar::Base(PlotValueFn fn, void* user) {
    base = fn;
    baseUser = user;
    return this;
}

Bar* Bar::Value(PlotValueFn fn, void* user) {
    value = fn;
    valueUser = user;
    return this;
}

Bar* Bar::Fill(PlotBarFillFn fn, void* user) {
    fill = fn;
    fillUser = user;
    return this;
}

Bar* Bar::Label(PlotBarLabelFn fn, void* user) {
    label = fn;
    labelUser = user;
    return this;
}

Bar* Bar::CornerRadii(Corners radii) {
    cornerRadii = radii;
    return this;
}

static void PlotCornersPath(Path* path, Bounds box, Corners corners) {
    float limit = (box.w < box.h ? box.w : box.h) * 0.5f;
    if (limit < 0) {
        limit = 0;
    }
    float tl = corners.tl < limit ? corners.tl : limit;
    float tr = corners.tr < limit ? corners.tr : limit;
    float br = corners.br < limit ? corners.br : limit;
    float bl = corners.bl < limit ? corners.bl : limit;
    float right = box.x + box.w;
    float bottom = box.y + box.h;
    PathMoveTo(path, box.x + tl, box.y);
    PathLineTo(path, right - tr, box.y);
    if (tr > 0)
        PathArcTo(path, right - tr, box.y + tr, tr, -kPi * .5f, 0, true);
    PathLineTo(path, right, bottom - br);
    if (br > 0)
        PathArcTo(path, right - br, bottom - br, br, 0, kPi * .5f, true);
    PathLineTo(path, box.x + bl, bottom);
    if (bl > 0)
        PathArcTo(path, box.x + bl, bottom - bl, bl, kPi * .5f, kPi, true);
    PathLineTo(path, box.x, box.y + tl);
    if (tl > 0)
        PathArcTo(path, box.x + tl, box.y + tl, tl, kPi, kPi * 1.5f, true);
    PathClose(path);
}

static void PaintPlotBackground(PaintCtx* ctx, Bounds box, Corners corners,
                                Background fill) {
    if (box.w <= 0 || box.h <= 0) {
        return;
    }
    Path* path = PathNew(ctx, true);
    if (!path) {
        return;
    }
    PlotCornersPath(path, box, corners);
    PaintPathFill(ctx, path, fill, box);
    PathFree(path);
}

void Bar::Paint(PaintCtx* ctx, Bounds bounds) const {
    Vec<Text> labels;
    for (int i = 0; i < items.count; i++) {
        float crossAt = 0, end = 0;
        if (!PlotValue(cross, items, i, crossUser, &crossAt) ||
            !PlotValue(value, items, i, valueUser, &end)) {
            continue;
        }
        float start = 0;
        if (base) {
            float candidate = 0;
            if (PlotValue(base, items, i, baseUser, &candidate)) {
                start = candidate;
            }
        }
        bool horizontal = BarAlignmentIsHorizontal(alignment);
        Bounds local =
            horizontal
                ? Bounds{end < start ? end : start, crossAt,
                         end < start ? start - end : end - start, bandWidth}
                : Bounds{crossAt, end < start ? end : start, bandWidth,
                         end < start ? start - end : end - start};
        Background background = Rgb(0, 0, 0);
        const void* item = items.At(i);
        if (fill) {
            background = fill(item, i, local, alignment, fillUser);
        }
        Bounds painted = {bounds.x + local.x, bounds.y + local.y, local.w,
                          local.h};
        PaintPlotBackground(ctx, painted, cornerRadii, background);
        if (label) {
            Point origin =
                BarLabelOrigin(alignment, crossAt, start, end, bandWidth);
            label(item, i, origin, labelUser, &labels);
        }
    }
    for (int i = 0; i < len(labels); i++) {
        PlotLabel one = PlotLabel::New(GetTempArena());
        one.Add(labels[i]);
        one.Paint(ctx, bounds);
    }
}

Arc Arc::New() {
    return Arc{};
}

Arc* Arc::InnerRadius(float value) {
    innerRadius = value;
    return this;
}

Arc* Arc::OuterRadius(float value) {
    outerRadius = value;
    return this;
}

Point Arc::Centroid(const ArcData& arc) const {
    float start = arc.startAngle - kPi * .5f;
    float end = arc.endAngle - kPi * .5f;
    float radius = (innerRadius + outerRadius) * .5f;
    float angle = (start + end) * .5f;
    return {radius * cosf(angle), radius * sinf(angle)};
}

Path* Arc::PathFor(PaintCtx* ctx, const ArcData& arc, Bounds bounds) const {
    const float epsilon = 1e-12f;
    float start = arc.startAngle - kPi * .5f;
    float end = arc.endAngle - kPi * .5f;
    float delta = end - start;
    float pad = delta >= kPi ? .0001f : arc.padAngle;
    float r0 = innerRadius > 0 ? innerRadius : 0;
    float r1 = outerRadius > 0 ? outerRadius : 0;
    if (r1 < epsilon || fabsf(delta) < epsilon) {
        return nullptr;
    }
    float a0Outer, a1Outer, a0Inner, a1Inner;
    if (r0 > epsilon && pad > 0) {
        float width = r1 * pad;
        float outerPad = width / r1;
        float innerPad = width / r0;
        float maxInner = delta * .8f;
        if (innerPad > maxInner) innerPad = maxInner;
        a0Outer = start + outerPad * .5f;
        a1Outer = end - outerPad * .5f;
        a0Inner = start + innerPad * .5f;
        a1Inner = end - innerPad * .5f;
    } else {
        float half = pad * .5f;
        a0Outer = start + half;
        a1Outer = end - half;
        a0Inner = a0Outer;
        a1Inner = a1Outer;
    }
    if (a1Outer - a0Outer <= 0) {
        return nullptr;
    }
    float cx = bounds.x + bounds.w * .5f;
    float cy = bounds.y + bounds.h * .5f;
    Path* path = PathNew(ctx, true);
    if (!path) {
        return nullptr;
    }
    PathMoveTo(path, cx + r1 * cosf(a0Outer), cy + r1 * sinf(a0Outer));
    PathArcTo(path, cx, cy, r1, a0Outer, a1Outer, true);
    if (r0 > epsilon) {
        PathLineTo(path, cx + r0 * cosf(a1Inner), cy + r0 * sinf(a1Inner));
        PathArcTo(path, cx, cy, r0, a1Inner, a0Inner, false);
    } else {
        PathLineTo(path, cx, cy);
    }
    PathClose(path);
    return path;
}

void Arc::Paint(PaintCtx* ctx, const ArcData& arc, Background fill,
                Bounds bounds) const {
    Path* path = PathFor(ctx, arc, bounds);
    if (path) {
        PaintPathFill(ctx, path, fill, bounds);
        PathFree(path);
    }
}

static float RemEuclid(float v, float m) {
    float r = fmodf(v, m);
    return r < 0 ? r + m : r;
}

bool Arc::Contains(const ArcData& arc, Point position, Bounds bounds) const {
    float dx = position.x - bounds.w * .5f;
    float dy = position.y - bounds.h * .5f;
    float radius = sqrtf(dx * dx + dy * dy);
    float r0 = innerRadius > 0 ? innerRadius : 0;
    float r1 = outerRadius > 0 ? outerRadius : 0;
    if (radius < r0 || radius > r1) {
        return false;
    }
    // Screen angle -> pie angle (0 at 12 o'clock, clockwise), in [0, TAU).
    float angle = RemEuclid(atan2f(dy, dx) + kPi * .5f, 2.f * kPi);
    return angle >= arc.startAngle && angle < arc.endAngle;
}

void Arc::PaintCached(PaintCtx* ctx, const ArcData& arc, Background fill,
                      Bounds bounds, PathCache* cache) const {
    uint64_t key = ShapeKey::New()
                       .Float(bounds.w)
                       .Float(bounds.h)
                       .Float(arc.startAngle)
                       .Float(arc.endAngle)
                       .Float(arc.padAngle)
                       .Float(innerRadius)
                       .Float(outerRadius)
                       .Finish();
    if (cache) {
        cache->Touch(key);
    }
    Paint(ctx, arc, fill, bounds);
}

Pie Pie::New() {
    return Pie{};
}

Pie* Pie::Value(PlotValueFn fn, void* user) {
    value = fn;
    valueUser = user;
    return this;
}

Pie* Pie::StartAngle(float angle) {
    startAngle = angle;
    return this;
}

Pie* Pie::EndAngle(float angle) {
    endAngle = angle;
    return this;
}

Pie* Pie::PadAngle(float angle) {
    padAngle = angle;
    return this;
}

void Pie::Arcs(Arena* arena, PlotItems items, ArenaVec<ArcData>* out) const {
    if (!out) {
        return;
    }
    float sum = 0;
    for (int i = 0; i < items.count; i++) {
        float v = 0;
        if (PlotValue(value, items, i, valueUser, &v) && v > 0) {
            sum += v;
        }
    }
    float angle = startAngle;
    for (int i = 0; i < items.count; i++) {
        float v = 0;
        if (!PlotValue(value, items, i, valueUser, &v) || v <= 0) {
            continue;
        }
        float from = angle;
        angle += sum > 0 ? v / sum * (endAngle - startAngle) : 0;
        out->Append(arena, ArcData{items.At(i), i, v, from, angle, padAngle});
    }
}

RadialLine RadialLine::New() {
    RadialLine out;
    out.fill = RgbaTransparent();
    out.stroke = RgbaTransparent();
    return out;
}

RadialLine* RadialLine::Data(const void* values, int count, int stride) {
    items = PlotItems{values, count > 0 ? count : 0, stride};
    return this;
}

RadialLine* RadialLine::Angle(PlotValueFn fn, void* user) {
    angle = fn;
    angleUser = user;
    return this;
}

RadialLine* RadialLine::Radius(PlotValueFn fn, void* user) {
    radius = fn;
    radiusUser = user;
    return this;
}

RadialLine* RadialLine::Closed(bool value) {
    closed = value;
    return this;
}

RadialLine* RadialLine::Fill(Background value) {
    hasFill = true;
    fill = value;
    return this;
}

RadialLine* RadialLine::Stroke(Background value) {
    stroke = value;
    return this;
}

RadialLine* RadialLine::StrokeWidth(float value) {
    strokeWidth = value;
    return this;
}

RadialLine* RadialLine::Dots(bool value) {
    dot = value;
    return this;
}

RadialLine* RadialLine::DotSize(float value) {
    dotSize = value;
    return this;
}

RadialLine* RadialLine::DotFill(Background value) {
    dotFill = value;
    return this;
}

RadialLine* RadialLine::DotStroke(Rgba value) {
    hasDotStroke = true;
    dotStroke = value;
    return this;
}

int RadialLine::Points(Bounds bounds, Point* out, int capacity) const {
    float cx = bounds.x + bounds.w * .5f;
    float cy = bounds.y + bounds.h * .5f;
    int count = 0;
    for (int i = 0; i < items.count; i++) {
        float a = 0, r = 0;
        if (!PlotValue(angle, items, i, angleUser, &a) ||
            !PlotValue(radius, items, i, radiusUser, &r)) {
            continue;
        }
        a -= kPi * .5f;
        if (out && count < capacity) {
            out[count] = {cx + r * cosf(a), cy + r * sinf(a)};
        }
        count++;
    }
    return count;
}

void RadialLine::Paint(PaintCtx* ctx, Bounds bounds) const {
    Vec<Point> points;
    for (int i = 0; i < items.count; i++) {
        float a = 0, r = 0;
        if (PlotValue(angle, items, i, angleUser, &a) &&
            PlotValue(radius, items, i, radiusUser, &r)) {
            a -= kPi * .5f;
            VecAppend(points, {bounds.x + bounds.w * .5f + r * cosf(a),
                               bounds.y + bounds.h * .5f + r * sinf(a)});
        }
    }
    if (hasFill && len(points) >= 3) {
        Path* path = PathNew(ctx, true);
        PathMoveTo(path, points[0].x, points[0].y);
        for (int i = 1; i < len(points); i++) {
            PathLineTo(path, points[i].x, points[i].y);
        }
        PathClose(path);
        PaintPathFill(ctx, path, fill, bounds);
        PathFree(path);
    }
    if (len(points) > 0) {
        Path* path = PathNew(ctx, false);
        PathMoveTo(path, points[0].x, points[0].y);
        for (int i = 1; i < len(points); i++) {
            PathLineTo(path, points[i].x, points[i].y);
        }
        if (closed && len(points) > 2) {
            PathClose(path);
        }
        PaintPathStroke(ctx, path, strokeWidth, stroke);
        PathFree(path);
    }
    if (dot) {
        for (int i = 0; i < len(points); i++) {
            PaintDot(ctx, points[i], dotSize, dotFill, hasDotStroke, dotStroke);
        }
    }
}

Stack Stack::New() {
    return Stack{};
}

Stack* Stack::Data(const void* values, int count, int stride) {
    items = PlotItems{values, count > 0 ? count : 0, stride};
    return this;
}

Stack* Stack::Keys(const Str* values, int count) {
    keys = values;
    keyCount = count > 0 ? count : 0;
    return this;
}

Stack* Stack::Value(PlotStackValueFn fn, void* user) {
    value = fn;
    valueUser = user;
    return this;
}

void Stack::Series(Arena* arena, ArenaVec<StackSeries>* out) const {
    if (!arena || !out || items.count <= 0 || !keys || keyCount <= 0) {
        return;
    }
    Vec<float> baseline;
    for (int i = 0; i < items.count; i++) {
        VecAppend(baseline, 0);
    }
    for (int keyIndex = 0; keyIndex < keyCount; keyIndex++) {
        StackSeries series = {};
        series.key = keys[keyIndex];
        series.index = keyIndex;
        for (int itemIndex = 0; itemIndex < items.count; itemIndex++) {
            float v = 0;
            const void* item = items.At(itemIndex);
            if (!value ||
                !value(item, itemIndex, keys[keyIndex], valueUser, &v)) {
                v = 0;
            }
            float y0 = baseline[itemIndex];
            float y1 = y0 + v;
            baseline[itemIndex] = y1;
            series.points.Append(arena, StackPoint{y0, y1, item});
        }
        out->Append(arena, series);
    }
}

Path* SankeyLinkPath(PaintCtx* ctx, const SankeyNodeLayout& source,
                     const SankeyNodeLayout& target,
                     const SankeyLinkLayout& link, float minWidth,
                     Point origin) {
    float sourceHalf =
        (link.sourceWidth > minWidth ? link.sourceWidth : minWidth) * .5f;
    float targetHalf =
        (link.targetWidth > minWidth ? link.targetWidth : minWidth) * .5f;
    float sx = source.x1 + origin.x;
    float tx = target.x0 + origin.x;
    float mx = (sx + tx) * .5f;
    float sy = link.y0 + origin.y;
    float ty = link.y1 + origin.y;
    Path* path = PathNew(ctx, true);
    if (!path) {
        return nullptr;
    }
    PathMoveTo(path, sx, sy - sourceHalf);
    PathCubicTo(path, mx, sy - sourceHalf, mx, ty - targetHalf, tx,
                ty - targetHalf);
    PathLineTo(path, tx, ty + targetHalf);
    PathCubicTo(path, mx, ty + targetHalf, mx, sy + sourceHalf, sx,
                sy + sourceHalf);
    PathClose(path);
    return path;
}

TooltipState TooltipState::New(int value, Point cross, const Point* dotValues,
                               int count) {
    TooltipState out;
    out.index = value;
    out.crossLine = cross;
    out.dots = dotValues;
    out.dotCount = count > 0 ? count : 0;
    return out;
}

// ─── plot/appear.rs ──────────────────────────────────────────────────────

float PlotAppear::Progress() const {
    if (time >= 1.f) {
        return 1.f;
    }
    return easing.Sample(time);
}

float PlotAppear::Staggered(int index, int count, float spread) const {
    if (count <= 1 || time >= 1.f) {
        return Progress();
    }
    spread = spread < 0.f ? 0.f : (spread > 0.95f ? 0.95f : spread);
    int ix = index < count - 1 ? index : count - 1;
    float start = spread * (float)ix / (float)(count - 1);
    float t = (time - start) / (1.f - spread);
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return easing.Sample(t);
}

PlotAppear TrackAppear(Ctx* cx, uint64_t generation) {
    if (!cx || !cx->win || !cx->app) {
        return PlotAppear::Complete();
    }
    // try_global: borrowed rather than cloned, since every plot asks on
    // every frame; without a theme there is no appear.
    const BaseTheme* theme = BaseThemeGlobal((const App*)cx->app);
    if (!theme) {
        return PlotAppear::Complete();
    }
    const motion::Transition& policy = theme->plot.Motion().Appear();
    // APPEAR, under ElementId::Integer(generation), within the plot's scope.
    uint32_t key = KeyedName(cx, StrL("__plot-appear")) * 31u +
                   (uint32_t)(generation ^ (generation >> 32));
    // Presence keeps the linear time so the marks can each ease over their
    // own slice of it; see PlotAppear::Staggered.
    PresenceSample sample = Presence::New(key, true)
                                .Transition(policy.Ease(Easing::Linear()))
                                .Sample(cx);
    PlotAppear out;
    out.time = sample.progress;
    out.easing = policy.easing;
    return out;
}

// The last datum the cursor resolved to, where the cursor was and how far the
// hover has faded in, kept in element state so the hover can fade out over it
// after the cursor leaves and so an overlay can read the fade without being
// handed it; see HoverProgress.
struct HoverMemory {
    bool hasState = false;
    TooltipState state = {};
    Point storedDots[8] = {};
    Point cursor = {};
    // An overlay rendered outside a plot's tracking is fully opaque.
    float progress = 1.f;
    // The first frame the cursor is on a datum; see PlotHover::IsEntering.
    bool entering = false;
};

// HOVER_MEMORY: the element-state key of a plot's HoverMemory, within the
// plot's scope.
static HoverMemory* PlotHoverMemory(Ctx* cx) {
    return ElementState<HoverMemory>(cx, StrL("__plot-hover"), StrL("memory"));
}

Spring PointerSpring(const App* app) {
    return base_theme::Theme::Global(app).plot.Motion().Pointer();
}

bool TrackHover(Ctx* cx, const TooltipState* live, const Point* cursor,
                PlotHover* outHover, Point* outCursor) {
    if (!cx || !cx->win || !outHover) {
        return false;
    }
    HoverMemory* memory = PlotHoverMemory(cx);
    if (!memory) {
        return false;
    }
    bool hovered = live != nullptr;
    base_theme::Theme theme = base_theme::Theme::Global(cx->app);
    const PlotMotion& motion = theme.plot.Motion();
    float progress = motion::transition(
        cx, motion::TransitionId(StrL("__plot-hover"), StrL("progress")),
        hovered ? 1.f : 0.f, hovered ? motion.Enter() : motion.Exit());
    if (live && cursor) {
        memory->state = *live;
        int n = live->dotCount;
        if (n > 8) {
            n = 8;
        }
        if (live->dots && n > 0) {
            memcpy(memory->storedDots, live->dots, sizeof(Point) * (size_t)n);
            memory->state.dots = memory->storedDots;
            memory->state.dotCount = n;
        } else {
            memory->state.dots = nullptr;
            memory->state.dotCount = 0;
        }
        memory->cursor = *cursor;
        memory->hasState = true;
    }
    memory->progress = progress;
    memory->entering = hovered && progress == 0.f;
    if (!hovered && progress <= 0.f) {
        memory->hasState = false;
    }
    if (!memory->hasState) {
        return false;
    }
    outHover->state = memory->state;
    outHover->progress = progress;
    outHover->hovered = hovered;
    if (outCursor) {
        *outCursor = memory->cursor;
    }
    return true;
}

float HoverProgress(Ctx* cx) {
    if (!cx || !cx->win) {
        return 1.f;
    }
    HoverMemory* memory = PlotHoverMemory(cx);
    return memory ? memory->progress : 1.f;
}

bool IsHoverEntering(Ctx* cx) {
    if (!cx || !cx->win) {
        return false;
    }
    HoverMemory* memory = PlotHoverMemory(cx);
    return memory && memory->entering;
}

float PlotHover::Glide(Ctx* cx, motion::TransitionId id, float target) const {
    Spring policy = PointerSpring(cx->app).WithTravel(!IsEntering());
    return motion::spring(cx, id, target, policy);
}

} // namespace plot
} // namespace gpui
