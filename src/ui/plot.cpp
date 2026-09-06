#include "ui/plot.h"
#include "ui/popover.h"

#include <math.h>
#include <string.h>

namespace gpui {

namespace component {

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

static int FirstIndexOf(const float* v, int n, float want) {
    for (int i = 0; i < n; i++) {
        if (v[i] == want) {
            return i;
        }
    }
    return 0;
}

ScaleLinear ScaleLinear::New(const float* domain, int domainN,
                             const float* range, int rangeN) {
    float domainMin = 0, domainMax = 0;
    MinMax(domain, domainN, &domainMin, &domainMax);

    float rangeMin = 0, rangeMax = 0;
    MinMax(range, rangeN, &rangeMin, &rangeMax);
    float rangeFrom = rangeMin;
    float rangeTo = rangeMax;
    if (rangeN > 0) {
        // Whichever of the two comes first is where the range starts, so a
        // descending range keeps mapping the domain backwards. A range with
        // more than two stops is still only its ends.
        if (FirstIndexOf(range, rangeN, rangeMin) >
            FirstIndexOf(range, rangeN, rangeMax)) {
            rangeFrom = rangeMax;
            rangeTo = rangeMin;
        }
    }

    ScaleLinear s;
    s.domainLen = domainN;
    s.domainStart = domainMin;
    s.domainDiff = domainMax - domainMin;
    s.rangeStart = rangeFrom;
    s.rangeDiff = rangeTo - rangeFrom;
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

void ScaleLinear::LeastIndexWithDomain(float tick, const float* domain,
                                       int domainN, int* outIndex,
                                       float* outTick) const {
    *outIndex = 0;
    *outTick = 0;
    if (domainLen == 0 || domainN <= 0) {
        return;
    }
    // Rust enumerates after dropping the domain values that have no tick, so
    // the index counts the ones that resolved.
    int seen = 0;
    bool any = false;
    float bestDist = 0;
    for (int i = 0; i < domainN; i++) {
        float t = 0;
        if (!Tick(domain[i], &t)) {
            continue;
        }
        float dist = t - tick;
        if (dist < 0) {
            dist = -dist;
        }
        if (!any || dist < bestDist) {
            any = true;
            bestDist = dist;
            *outIndex = seen;
            *outTick = t;
        }
        seen++;
    }
}

ScalePoint ScalePoint::New(const float* domain, int domainN, const float* range,
                           int rangeN) {
    ScalePoint s;
    s.domain = domain;
    s.domainLen = domainN;
    if (domainN == 0) {
        return s;
    }
    float rangeMin = 0, rangeMax = 0;
    MinMax(range, rangeN, &rangeMin, &rangeMax);
    float diff = rangeMax - rangeMin;
    s.rangeStart = rangeMin;
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
    // A single point has no spacing to step by, so it sits in the middle.
    *out = domainLen == 1 ? rangeStart + rangeTick * 0.5f
                          : rangeStart + (float)index * rangeTick;
    return true;
}

int ScalePoint::LeastIndex(float tick) const {
    if (domainLen <= 0 || rangeTick == 0) {
        return 0;
    }
    // roundf, not rint: a tick exactly between two points belongs to the
    // later one, which is what Rust's f32::round does and what the ties in
    // the reference tests assert.
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
    float lo = 0;
    float hi = 0;
    for (int i = 0; i < rangeN; i++) {
        if (i == 0 || range[i] < lo) {
            lo = range[i];
        }
        if (i == 0 || range[i] > hi) {
            hi = range[i];
        }
    }
    b.rangeDiff = rangeN > 0 ? hi - lo : 0;
    b.avgWidth = b.domainLen > 0 ? b.rangeDiff / (float)b.domainLen : 0;
    return b;
}

float ScaleBand::BandWidth() const {
    float w = avgWidth * (1.f - paddingInner);
    return w < 30.f ? w : 30.f;
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

bool ScaleBand::Tick(int index, float* out) const {
    if (index < 0 || index >= domainLen) {
        return false;
    }
    if (domainLen == 1) {
        // One band sits in the middle of the range.
        *out = (rangeDiff - BandWidth()) / 2.f;
        return true;
    }
    float avg = ScaleBandDisplayAvgWidth(*this);
    float outer = avgWidth * paddingOuter;
    *out = (float)index * avg * ScaleBandRatio(*this) + outer;
    return true;
}

int ScaleBand::LeastIndex(float tick) const {
    if (domainLen <= 1) {
        return 0;
    }
    float avg = ScaleBandDisplayAvgWidth(*this);
    float outer = avgWidth * paddingOuter;
    float step = avg * ScaleBandRatio(*this);
    if (step == 0) {
        return 0;
    }
    int index = (int)lroundf((tick - outer) / step);
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
    if (!ctx || !ctx->pa || !text.s || text.len <= 0) {
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
    for (int at = 0; at < text.len;) {
        uint32_t rune = 0;
        int n = Utf8At(text, at, &rune);
        (void)rune;
        if (n <= 0) {
            n = 1;
        }
        at += n;
        if (at < text.len) {
            VecAppend(cuts, at);
        }
    }
    int best = -1;
    int lo = 0;
    int hi = cuts.len;
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
        if (!item.text.s || item.text.len <= 0) {
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
    out.xLabel = PlotLabel::New(arena);
    out.yLabel = PlotLabel::New(arena);
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
    if (!hasX || !labels || count <= 0) {
        return this;
    }
    float labelY = xLabelSide == AxisLabelSide::End
                       ? x + kPlotTextGap * 3.f
                       : x - (kPlotTextGap + kPlotTextHeight);
    for (int i = 0; i < count; i++) {
        Text text = Text::New(labels[i].text, {labels[i].tick, labelY},
                              labels[i].color);
        text.fontSize = labels[i].fontSize;
        text.align = labels[i].align;
        xLabel.Add(text);
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
    if (!hasY || !labels || count <= 0) {
        return this;
    }
    float labelX =
        yLabelSide == AxisLabelSide::End ? y + kPlotTextGap : y - kPlotTextGap;
    for (int i = 0; i < count; i++) {
        Text text = Text::New(labels[i].text,
                              {labelX, labels[i].tick - kPlotTextSize * 0.5f},
                              labels[i].color);
        text.fontSize = labels[i].fontSize;
        text.align = labels[i].align;
        yLabel.Add(text);
    }
    return this;
}

PlotAxis* PlotAxis::YLabelSide(AxisLabelSide value) {
    yLabelSide = value;
    return this;
}

PlotAxis* PlotAxis::Stroke(Rgba value) {
    stroke = value;
    return this;
}

void PlotAxis::Paint(PaintCtx* ctx, Bounds bounds) const {
    if (hasX && xAxis) {
        CanvasLine(ctx, bounds.x, bounds.y + x, bounds.x + bounds.w,
                   bounds.y + x, 1, stroke);
    }
    xLabel.Paint(ctx, bounds);
    if (hasY && yAxis) {
        CanvasLine(ctx, bounds.x + y, bounds.y, bounds.x + y,
                   bounds.y + bounds.h, 1, stroke);
    }
    yLabel.Paint(ctx, bounds);
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

Grid* Grid::Stroke(Rgba value) {
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
        PaintPlotLine(ctx, {px, bounds.y}, {px, bounds.y + bounds.h}, stroke,
                      dashArray, dashCount);
    }
    for (int i = 0; i < yCount; i++) {
        float py = bounds.y + y[i];
        PaintPlotLine(ctx, {bounds.x, py}, {bounds.x + bounds.w, py}, stroke,
                      dashArray, dashCount);
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

Line* Line::Style(StrokeStyle value) {
    strokeStyle = value;
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

Line* Line::DotFill(Rgba value) {
    dotFillColor = value;
    return this;
}

Line* Line::DotStroke(Rgba value) {
    hasDotStrokeColor = true;
    dotStrokeColor = value;
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

static void PlotRun(Path* path, const Vec<Point>& points, StrokeStyle style) {
    if (!path || points.len <= 0) {
        return;
    }
    PathMoveTo(path, points[0].x, points[0].y);
    if (points.len == 1) {
        return;
    }
    if (style == StrokeStyle::Linear) {
        for (int i = 1; i < points.len; i++) {
            PathLineTo(path, points[i].x, points[i].y);
        }
        return;
    }
    if (style == StrokeStyle::StepAfter) {
        for (int i = 0; i < points.len - 1; i++) {
            PathLineTo(path, points[i + 1].x, points[i].y);
            if (i < points.len - 2) {
                PathLineTo(path, points[i + 1].x, points[i + 1].y);
            }
        }
        return;
    }
    for (int i = 0; i < points.len - 1; i++) {
        const Point& p0 = i == 0 ? points[0] : points[i - 1];
        const Point& p1 = points[i];
        const Point& p2 = points[i + 1];
        const Point& p3 =
            i + 2 < points.len ? points[i + 2] : points[points.len - 1];
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

static void PaintDot(PaintCtx* ctx, Point point, float size, Rgba fill,
                     Rgba stroke) {
    float radius = size * 0.5f;
    CanvasEllipse(ctx, point.x, point.y, radius, radius, 0, fill);
    CanvasEllipse(ctx, point.x, point.y, radius, radius, 1, stroke);
}

void Line::Paint(PaintCtx* ctx, Bounds bounds) const {
    Vec<Point> points = ResolveLinePoints(*this, bounds);
    if (points.len > 0) {
        Path* path = PathNew(ctx, false);
        PlotRun(path, points, strokeStyle);
        PaintPathStroke(ctx, path, strokeWidth, stroke);
        PathFree(path);
    }
    if (dot) {
        Rgba edge = hasDotStrokeColor ? dotStrokeColor : dotFillColor;
        for (int i = 0; i < points.len; i++) {
            PaintDot(ctx, points[i], dotSize, dotFillColor, edge);
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

Area* Area::Style(StrokeStyle value) {
    strokeStyle = value;
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
    if (points.len <= 0) {
        return;
    }
    Path* area = PathNew(ctx, true);
    Path* line = PathNew(ctx, false);
    PlotRun(area, points, strokeStyle);
    PlotRun(line, points, strokeStyle);
    if (points.len > 1 && hasY0 && hasFirst && hasLast) {
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
    for (int i = 0; i < labels.len; i++) {
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

Path* Arc::PathFor(PaintCtx* ctx, const ArcData& arc, Bounds bounds,
                   float innerOverride, float outerOverride) const {
    const float epsilon = 1e-12f;
    float start = arc.startAngle - kPi * .5f;
    float end = arc.endAngle - kPi * .5f;
    float delta = end - start;
    float pad = delta >= kPi ? .0001f : arc.padAngle;
    float r0 = innerOverride >= 0 ? innerOverride : innerRadius;
    float r1 = outerOverride >= 0 ? outerOverride : outerRadius;
    if (r0 < 0) r0 = 0;
    if (r1 < 0) r1 = 0;
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

void Arc::Paint(PaintCtx* ctx, const ArcData& arc, Rgba color, Bounds bounds,
                float innerOverride, float outerOverride) const {
    Path* path = PathFor(ctx, arc, bounds, innerOverride, outerOverride);
    if (path) {
        PathFill(ctx, path, color);
        PathFree(path);
    }
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

RadialLine* RadialLine::DotFill(Rgba value) {
    dotFillColor = value;
    return this;
}

RadialLine* RadialLine::DotStroke(Rgba value) {
    hasDotStrokeColor = true;
    dotStrokeColor = value;
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
    if (hasFill && points.len >= 3) {
        Path* path = PathNew(ctx, true);
        PathMoveTo(path, points[0].x, points[0].y);
        for (int i = 1; i < points.len; i++) {
            PathLineTo(path, points[i].x, points[i].y);
        }
        PathClose(path);
        PaintPathFill(ctx, path, fill, bounds);
        PathFree(path);
    }
    if (points.len > 0) {
        Path* path = PathNew(ctx, false);
        PathMoveTo(path, points[0].x, points[0].y);
        for (int i = 1; i < points.len; i++) {
            PathLineTo(path, points[i].x, points[i].y);
        }
        if (closed && points.len > 2) {
            PathClose(path);
        }
        PaintPathStroke(ctx, path, strokeWidth, stroke);
        PathFree(path);
    }
    if (dot) {
        Rgba edge = hasDotStrokeColor ? dotStrokeColor : dotFillColor;
        for (int i = 0; i < points.len; i++) {
            PaintDot(ctx, points[i], dotSize, dotFillColor, edge);
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

El* Dot::IntoEl(Ctx* cx) const {
    float offset = size * .5f - .5f;
    return Div(cx->a)
        ->Absolute()
        ->W(size)
        ->H(size)
        ->Radius(size * .5f)
        ->Border(1, stroke)
        ->Bg(fill)
        ->Left(point.x - offset)
        ->Top(point.y - offset);
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

Tooltip* Tooltip::Row(Rgba color, Str label, Str value) {
    rows.Append(a, TooltipRow{color, label, value});
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

El* Tooltip::IntoEl() {
    const Theme& theme = ThemeNow(cx->app);
    El* root = Div(a)->SizeFull()->Absolute()->Top(0)->Left(0);
    if (hasCrossLine) {
        root->Child(crossLine.IntoEl(cx));
    }
    for (const Dot& dot : dots) {
        root->Child(dot.IntoEl(cx));
    }
    El* content = Div(a)->FlexCol();
    if (hasTitle || rows.len > 0) {
        content->Font(14)->Gap(4);
        if (hasTitle) {
            content->Child(TextEl(a, title)->Semibold());
        }
        for (const TooltipRow& row : rows) {
            El* left = Div(a)
                           ->FlexRow()
                           ->ItemsCenter()
                           ->Gap(6)
                           ->Child(Div(a)
                                       ->W(8)
                                       ->H(8)
                                       ->Radius(theme.radius * .5f)
                                       ->Bg(row.color))
                           ->Child(TextEl(a, row.label)->Fg(theme.mutedFg));
            content->Child(Div(a)
                               ->FlexRow()
                               ->ItemsCenter()
                               ->JustifyBetween()
                               ->Gap(12)
                               ->Child(left)
                               ->Child(TextEl(a, row.value)));
        }
    } else {
        for (El* child : children) {
            content->Child(child);
        }
    }
    if (!appearance) {
        content->SizeFull();
    } else {
        PopoverSurface(cx, content)->Absolute()->MinW(150)->Pad(8);
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

} // namespace component
} // namespace gpui
