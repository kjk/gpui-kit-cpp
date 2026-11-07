#include "base/grid_layout.h"

#include "base/actions.h"
#include "gpui/keymap.h"

#include <math.h>
#include <stdio.h>

namespace gpui {

static int AtLeastOne(int value) {
    return value < 1 ? 1 : value;
}

static int ClampTo(int value, int low, int high) {
    if (low > high) {
        low = high;
    }
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

struct GridRect {
    int col = 0;
    int row = 0;
    int colSpan = 1;
    int rowSpan = 1;

    bool Overlaps(const GridRect& other) const {
        return col < other.col + other.colSpan && other.col < col + colSpan &&
               row < other.row + other.rowSpan && other.row < row + rowSpan;
    }
};

static GridRect AreaRect(const GridArea& area) {
    GridRect rect;
    rect.col = area.col;
    rect.row = area.row;
    rect.colSpan = area.colSpan;
    rect.rowSpan = area.rowSpan;
    return rect;
}

static bool ReadingBefore(const GridArea& a, const GridArea& b,
                          bool horizontal) {
    if (horizontal) {
        if (a.col != b.col) {
            return a.col < b.col;
        }
        return a.row < b.row;
    }
    if (a.row != b.row) {
        return a.row < b.row;
    }
    return a.col < b.col;
}

// Stable: an equal key stays behind the one already placed.
static void SortIndices(int* order, int n, const GridArea* items,
                        bool horizontal) {
    for (int i = 1; i < n; i++) {
        int key = order[i];
        int j = i;
        while (j > 0 &&
               ReadingBefore(items[key], items[order[j - 1]], horizontal)) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = key;
    }
}

static void IndicesOf(const GridArea* items, int n, bool horizontal,
                      Vec<int>* order) {
    VecClear(*order);
    for (int i = 0; i < n; i++) {
        VecAppend(*order, i);
    }
    if (n > 1) {
        SortIndices(order->els, n, items, horizontal);
    }
}

static int FindId(const GridArea* items, int n, Str id) {
    for (int i = 0; i < n; i++) {
        if (StrEq(items[i].id, id)) {
            return i;
        }
    }
    return -1;
}

static bool ContainsIndex(const Vec<int>& values, int ix) {
    for (int i = 0; i < len(values); i++) {
        if (values[i] == ix) {
            return true;
        }
    }
    return false;
}

static int FirstCollision(const GridArea* items, const int* among, int nAmong,
                          GridRect rect, int skip) {
    for (int i = 0; i < nAmong; i++) {
        int other = among[i];
        if (other != skip && rect.Overlaps(AreaRect(items[other]))) {
            return other;
        }
    }
    return -1;
}

static bool AnyCollision(const GridArea* items, int n, GridRect rect,
                         int skip) {
    for (int other = 0; other < n; other++) {
        if (other != skip && rect.Overlaps(AreaRect(items[other]))) {
            return true;
        }
    }
    return false;
}

static void Collisions(const GridArea* items, const Vec<int>& order, int ix,
                       Vec<int>* hit) {
    VecClear(*hit);
    GridRect rect = AreaRect(items[ix]);
    for (int i = 0; i < len(order); i++) {
        int other = order[i];
        if (other != ix && rect.Overlaps(AreaRect(items[other]))) {
            VecAppend(*hit, other);
        }
    }
}

struct GridPush {
    GridLayoutRules rules;
    Vec<uint8_t> moved;

    void MoveElement(GridArea* items, int n, int ix, bool hasX, int x,
                     bool hasY, int y, bool userAction);
    void Away(GridArea* items, int n, int obstacle, int mover, bool userAction);
};

void GridPush::MoveElement(GridArea* items, int n, int ix, bool hasX, int x,
                           bool hasY, int y, bool userAction) {
    if (items[ix].locked) {
        return;
    }
    int oldX = items[ix].col;
    int oldY = items[ix].row;
    if (!hasX) {
        x = oldX;
    }
    if (!hasY) {
        y = oldY;
    }
    if (x == oldX && y == oldY) {
        return;
    }
    items[ix].col = x;
    items[ix].row = y;
    moved[ix] = 1;

    Vec<int> order;
    bool horizontal =
        rules.hasCompaction && rules.compaction == Axis::Horizontal;
    IndicesOf(items, n, horizontal, &order);
    bool movingBack = horizontal ? oldX >= x : oldY >= y;
    if (movingBack) {
        for (int i = 0, j = len(order) - 1; i < j; i++, j--) {
            int tmp = order[i];
            order[i] = order[j];
            order[j] = tmp;
        }
    }
    Vec<int> hit;
    Collisions(items, order, ix, &hit);
    for (int i = 0; i < len(hit); i++) {
        int other = hit[i];
        if (moved[other]) {
            continue;
        }
        if (items[other].locked) {
            Away(items, n, other, ix, userAction);
        } else {
            Away(items, n, ix, other, userAction);
        }
    }
}

void GridPush::Away(GridArea* items, int n, int obstacle, int mover,
                    bool userAction) {
    bool horizontal =
        rules.hasCompaction && rules.compaction == Axis::Horizontal;
    if (userAction) {
        const GridArea& o = items[obstacle];
        const GridArea& m = items[mover];
        GridRect probe;
        probe.colSpan = m.colSpan;
        probe.rowSpan = m.rowSpan;
        if (horizontal) {
            probe.col = o.col > m.colSpan ? o.col - m.colSpan : 0;
            probe.row = m.row;
        } else {
            probe.col = m.col;
            probe.row = o.row > m.rowSpan ? o.row - m.rowSpan : 0;
        }
        int blocker = -1;
        for (int other = 0; other < n; other++) {
            if (other != mover && other != obstacle &&
                probe.Overlaps(AreaRect(items[other]))) {
                blocker = other;
                break;
            }
        }
        if (blocker < 0 && !probe.Overlaps(AreaRect(o))) {
            if (horizontal) {
                MoveElement(items, n, mover, true, probe.col, false, 0, false);
            } else {
                MoveElement(items, n, mover, false, 0, true, probe.row, false);
            }
            return;
        }
    }
    if (horizontal) {
        MoveElement(items, n, mover, true, items[obstacle].ColEnd(), false, 0,
                    false);
    } else {
        MoveElement(items, n, mover, false, 0, true, items[obstacle].RowEnd(),
                    false);
    }
}

static void CompactItem(GridArea* items, const Vec<int>& placed, int ix,
                        Axis axis, int columns) {
    if (axis == Axis::Vertical) {
        int floor = 0;
        for (int i = 0; i < len(placed); i++) {
            int end = items[placed[i]].RowEnd();
            if (end > floor) {
                floor = end;
            }
        }
        if (items[ix].row > floor) {
            items[ix].row = floor;
        }
        for (;;) {
            while (items[ix].row > 0) {
                GridRect probe = AreaRect(items[ix]);
                probe.row -= 1;
                if (FirstCollision(items, placed.els, len(placed), probe, ix) >=
                    0) {
                    break;
                }
                items[ix].row -= 1;
            }
            int other = FirstCollision(items, placed.els, len(placed),
                                       AreaRect(items[ix]), ix);
            if (other < 0) {
                break;
            }
            items[ix].row = items[other].RowEnd();
        }
        return;
    }
    for (;;) {
        while (items[ix].col > 0) {
            GridRect probe = AreaRect(items[ix]);
            probe.col -= 1;
            if (FirstCollision(items, placed.els, len(placed), probe, ix) >=
                0) {
                break;
            }
            items[ix].col -= 1;
        }
        if (items[ix].ColEnd() > columns) {
            items[ix].col = 0;
            items[ix].row += 1;
            continue;
        }
        int other = FirstCollision(items, placed.els, len(placed),
                                   AreaRect(items[ix]), ix);
        if (other < 0) {
            break;
        }
        items[ix].col = items[other].ColEnd();
        if (items[ix].ColEnd() > columns) {
            items[ix].col = 0;
            items[ix].row += 1;
        }
    }
}

static void Settle(GridArea* items, int n, GridLayoutRules rules,
                   int priority) {
    if (rules.hasCompaction) {
        GridCompact(items, n, rules);
        return;
    }
    Vec<int> placed;
    for (int ix = 0; ix < n; ix++) {
        if (items[ix].locked) {
            VecAppend(placed, ix);
        }
    }
    if (priority >= 0 && !items[priority].locked) {
        VecAppend(placed, priority);
    }
    Vec<int> order;
    IndicesOf(items, n, false, &order);
    for (int i = 0; i < len(order); i++) {
        int ix = order[i];
        if (ContainsIndex(placed, ix)) {
            continue;
        }
        for (;;) {
            int other = FirstCollision(items, placed.els, len(placed),
                                       AreaRect(items[ix]), ix);
            if (other < 0) {
                break;
            }
            items[ix].row = items[other].RowEnd();
        }
        VecAppend(placed, ix);
    }
}

GridArea GridArea::New(Str id) {
    GridArea area;
    area.id = id;
    return area;
}
GridArea GridArea::Col(int value) const {
    GridArea out = *this;
    out.col = value;
    return out;
}
GridArea GridArea::Row(int value) const {
    GridArea out = *this;
    out.row = value;
    return out;
}
GridArea GridArea::ColSpan(int value) const {
    GridArea out = *this;
    out.colSpan = AtLeastOne(value);
    return out;
}
GridArea GridArea::RowSpan(int value) const {
    GridArea out = *this;
    out.rowSpan = AtLeastOne(value);
    return out;
}
GridArea GridArea::MinSpan(int colSpanValue, int rowSpanValue) const {
    GridArea out = *this;
    out.minColSpan = AtLeastOne(colSpanValue);
    out.minRowSpan = AtLeastOne(rowSpanValue);
    return out;
}
GridArea GridArea::MaxSpan(int colSpanValue, int rowSpanValue) const {
    GridArea out = *this;
    out.maxColSpan = AtLeastOne(colSpanValue);
    out.maxRowSpan = AtLeastOne(rowSpanValue);
    return out;
}
GridArea GridArea::Locked(bool value) const {
    GridArea out = *this;
    out.locked = value;
    return out;
}

static void ClampSize(const GridArea& area, int w, int h, int columns,
                      int* outW, int* outH) {
    int room = columns > area.col ? columns - area.col : 0;
    int maxCol = area.maxColSpan > 0 ? area.maxColSpan : 0x7fffffff;
    if (maxCol > room) {
        maxCol = room;
    }
    if (maxCol < 1) {
        maxCol = 1;
    }
    int maxRow = area.maxRowSpan > 0 ? area.maxRowSpan : 0x7fffffff;
    if (maxRow < 1) {
        maxRow = 1;
    }
    *outW = ClampTo(w, area.minColSpan, maxCol);
    *outH = ClampTo(h, area.minRowSpan, maxRow);
}

int GridRows(const GridArea* items, int n) {
    int rows = 0;
    for (int i = 0; i < n; i++) {
        int end = items[i].RowEnd();
        if (end > rows) {
            rows = end;
        }
    }
    return rows;
}

void GridNormalize(GridArea* items, int n, GridLayoutRules rules) {
    int columns = rules.columns < 1 ? 1 : rules.columns;
    for (int i = 0; i < n; i++) {
        GridArea* item = &items[i];
        int maxCol = item->maxColSpan > 0 ? item->maxColSpan : 0x7fffffff;
        int maxRow = item->maxRowSpan > 0 ? item->maxRowSpan : 0x7fffffff;
        if (maxCol < 1) {
            maxCol = 1;
        }
        if (maxRow < 1) {
            maxRow = 1;
        }
        item->colSpan = ClampTo(item->colSpan, item->minColSpan, maxCol);
        item->rowSpan = ClampTo(item->rowSpan, item->minRowSpan, maxRow);
        item->colSpan = ClampTo(item->colSpan, 1, columns);
        if (item->rowSpan < 1) {
            item->rowSpan = 1;
        }
        if (item->ColEnd() > columns) {
            item->col = columns - item->colSpan;
        }
    }

    Vec<int> order;
    bool horizontal =
        rules.hasCompaction && rules.compaction == Axis::Horizontal;
    IndicesOf(items, n, horizontal, &order);
    Vec<int> locked;
    for (int i = 0; i < len(order); i++) {
        if (items[order[i]].locked) {
            VecAppend(locked, order[i]);
        }
    }
    for (int nLocked = 0; nLocked < len(locked); nLocked++) {
        int ix = locked[nLocked];
        for (;;) {
            int other = FirstCollision(items, locked.els, nLocked,
                                       AreaRect(items[ix]), ix);
            if (other < 0) {
                break;
            }
            items[ix].row = items[other].RowEnd();
        }
    }
    Settle(items, n, rules, -1);
}

void GridCompact(GridArea* items, int n, GridLayoutRules rules) {
    if (!rules.hasCompaction) {
        return;
    }
    int columns = rules.columns < 1 ? 1 : rules.columns;
    Vec<int> placed;
    for (int ix = 0; ix < n; ix++) {
        if (items[ix].locked) {
            VecAppend(placed, ix);
        }
    }
    Vec<int> order;
    bool horizontal = rules.compaction == Axis::Horizontal;
    IndicesOf(items, n, horizontal, &order);
    for (int i = 0; i < len(order); i++) {
        int ix = order[i];
        if (items[ix].locked) {
            continue;
        }
        CompactItem(items, placed, ix, rules.compaction, columns);
        VecAppend(placed, ix);
    }
}

void GridReflowed(const GridArea* items, int n, int columns,
                  Vec<GridArea>* out) {
    VecClear(*out);
    for (int i = 0; i < n; i++) {
        VecAppend(*out, items[i]);
    }
    for (int i = 1; i < len(*out); i++) {
        GridArea key = (*out)[i];
        int j = i;
        while (j > 0 && ReadingBefore(key, (*out)[j - 1], false)) {
            (*out)[j] = (*out)[j - 1];
            j--;
        }
        (*out)[j] = key;
    }
    for (int i = 0; i < len(*out); i++) {
        (*out)[i].locked = false;
    }
    GridLayoutRules rules;
    rules.columns = columns;
    rules.hasCompaction = true;
    rules.compaction = Axis::Vertical;
    rules.preventCollision = false;
    if (len(*out) > 0) {
        GridNormalize(out->els, len(*out), rules);
    }
}

bool GridMoveItem(const GridArea* items, int n, Str id, int x, int y,
                  GridLayoutRules rules, Vec<GridArea>* out) {
    int ix = FindId(items, n, id);
    if (ix < 0 || items[ix].locked) {
        return false;
    }
    Vec<GridArea> next;
    for (int i = 0; i < n; i++) {
        VecAppend(next, items[i]);
    }
    int room =
        rules.columns > next[ix].colSpan ? rules.columns - next[ix].colSpan : 0;
    if (x > room) {
        x = room;
    }
    if (x < 0) {
        x = 0;
    }
    if (rules.preventCollision) {
        GridRect probe = AreaRect(next[ix]);
        probe.col = x;
        probe.row = y;
        if (AnyCollision(next.els, n, probe, ix)) {
            return false;
        }
    }
    GridPush push;
    push.rules = rules;
    for (int i = 0; i < n; i++) {
        VecAppend(push.moved, (uint8_t)0);
    }
    push.MoveElement(next.els, n, ix, true, x, true, y, true);
    Settle(next.els, n, rules, ix);
    VecClear(*out);
    for (int i = 0; i < n; i++) {
        VecAppend(*out, next[i]);
    }
    return true;
}

static bool ResizeFitsLocked(const GridArea* items, int n, int ix, int col,
                             int row, int w, int h) {
    GridRect probe;
    probe.col = col;
    probe.row = row;
    probe.colSpan = w;
    probe.rowSpan = h;
    for (int other = 0; other < n; other++) {
        if (other != ix && items[other].locked &&
            probe.Overlaps(AreaRect(items[other]))) {
            return false;
        }
    }
    return true;
}

bool GridResizeItem(const GridArea* items, int n, Str id, int w, int h,
                    GridLayoutRules rules, Vec<GridArea>* out) {
    int ix = FindId(items, n, id);
    if (ix < 0 || items[ix].locked) {
        return false;
    }
    Vec<GridArea> next;
    for (int i = 0; i < n; i++) {
        VecAppend(next, items[i]);
    }
    int clampedW = w;
    int clampedH = h;
    ClampSize(next[ix], w, h, rules.columns, &clampedW, &clampedH);
    int startColSpan = next[ix].colSpan;
    int startRowSpan = next[ix].rowSpan;
    int startCol = next[ix].col;
    int startRow = next[ix].row;
    int tries[3][2] = {
        {clampedW, clampedH},
        {clampedW, startRowSpan},
        {startColSpan, clampedH},
    };
    int chosen = -1;
    for (int i = 0; i < 3; i++) {
        if (ResizeFitsLocked(next.els, n, ix, startCol, startRow, tries[i][0],
                             tries[i][1])) {
            chosen = i;
            break;
        }
    }
    if (chosen < 0) {
        return false;
    }
    next[ix].colSpan = tries[chosen][0];
    next[ix].rowSpan = tries[chosen][1];
    if (rules.preventCollision &&
        AnyCollision(next.els, n, AreaRect(next[ix]), ix)) {
        return false;
    }
    GridPush push;
    push.rules = rules;
    for (int i = 0; i < n; i++) {
        VecAppend(push.moved, (uint8_t)0);
    }
    push.moved[ix] = 1;
    Vec<int> order;
    bool horizontal =
        rules.hasCompaction && rules.compaction == Axis::Horizontal;
    IndicesOf(next.els, n, horizontal, &order);
    Vec<int> hit;
    Collisions(next.els, order, ix, &hit);
    for (int i = 0; i < len(hit); i++) {
        int below = next[ix].RowEnd();
        push.MoveElement(next.els, n, hit[i], false, 0, true, below, false);
    }
    Settle(next.els, n, rules, ix);
    VecClear(*out);
    for (int i = 0; i < n; i++) {
        VecAppend(*out, next[i]);
    }
    return true;
}

bool GridAreaFromJson(const JsonValue* value, GridArea* out) {
    if (!value || value->kind != JsonKind::Object || !out) {
        return false;
    }
    GridArea area = GridArea::New(JsonString(JsonGet(value, "id")));
    area.col = (int)JsonNumber(JsonGet(value, "col"), 0);
    area.row = (int)JsonNumber(JsonGet(value, "row"), 0);
    const JsonValue* colSpan = JsonGet(value, "col_span");
    const JsonValue* rowSpan = JsonGet(value, "row_span");
    area.colSpan = colSpan ? AtLeastOne((int)JsonNumber(colSpan, 1)) : 1;
    area.rowSpan = rowSpan ? AtLeastOne((int)JsonNumber(rowSpan, 1)) : 1;
    const JsonValue* minCol = JsonGet(value, "min_col_span");
    const JsonValue* minRow = JsonGet(value, "min_row_span");
    area.minColSpan = minCol ? (int)JsonNumber(minCol, 1) : 1;
    area.minRowSpan = minRow ? (int)JsonNumber(minRow, 1) : 1;
    const JsonValue* maxCol = JsonGet(value, "max_col_span");
    const JsonValue* maxRow = JsonGet(value, "max_row_span");
    area.maxColSpan = maxCol && maxCol->kind == JsonKind::Number
                          ? (int)JsonNumber(maxCol, 0)
                          : 0;
    area.maxRowSpan = maxRow && maxRow->kind == JsonKind::Number
                          ? (int)JsonNumber(maxRow, 0)
                          : 0;
    const JsonValue* locked = JsonGet(value, "locked");
    area.locked = locked ? JsonBool(locked, false) : false;
    *out = area;
    return area.id.s != nullptr;
}

void GridAreaWriteJson(JsonWriter* writer, const GridArea& area) {
    if (!writer) {
        return;
    }
    writer->BeginObject();
    writer->String("id", area.id);
    writer->Number("col", area.col);
    writer->Number("row", area.row);
    writer->Number("col_span", area.colSpan);
    writer->Number("row_span", area.rowSpan);
    writer->Number("min_col_span", area.minColSpan);
    writer->Number("min_row_span", area.minRowSpan);
    if (area.maxColSpan > 0) {
        writer->Number("max_col_span", area.maxColSpan);
    } else {
        writer->Null("max_col_span");
    }
    if (area.maxRowSpan > 0) {
        writer->Number("max_row_span", area.maxRowSpan);
    } else {
        writer->Null("max_row_span");
    }
    writer->Bool("locked", area.locked);
    writer->EndObject();
}

static int Snap(float length, float step) {
    if (step <= 0) {
        return 0;
    }
    float rounded = roundf(length / step);
    if (rounded < 0) {
        return 0;
    }
    return (int)rounded;
}

static float AtLeastZero(float value) {
    return value < 0 ? 0 : value;
}

GridMetrics GridMetrics::New(float width, int columns, float rowHeight,
                             float gapWidth, float gapHeight) {
    float cols = (float)(columns < 1 ? 1 : columns);
    GridMetrics metrics;
    metrics.width = width;
    metrics.columnWidth = AtLeastZero((width - gapWidth * (cols - 1.f)) / cols);
    metrics.rowHeight = rowHeight;
    metrics.gapWidth = gapWidth;
    metrics.gapHeight = gapHeight;
    return metrics;
}

float GridMetrics::Left(int col) const {
    return (columnWidth + gapWidth) * (float)col;
}
float GridMetrics::Top(int row) const {
    return (rowHeight + gapHeight) * (float)row;
}
float GridMetrics::SpanWidth(int colSpan) const {
    float gaps = colSpan > 0 ? (float)(colSpan - 1) : 0;
    return AtLeastZero(columnWidth * (float)colSpan + gapWidth * gaps);
}
float GridMetrics::SpanHeight(int rowSpan) const {
    float gaps = rowSpan > 0 ? (float)(rowSpan - 1) : 0;
    return AtLeastZero(rowHeight * (float)rowSpan + gapHeight * gaps);
}
int GridMetrics::ColumnAt(float left) const {
    return Snap(left, columnWidth + gapWidth);
}
int GridMetrics::RowAt(float top) const {
    return Snap(top, rowHeight + gapHeight);
}
int GridMetrics::ColumnsSpanning(float span) const {
    int n = Snap(span + gapWidth, columnWidth + gapWidth);
    return n < 1 ? 1 : n;
}
int GridMetrics::RowsSpanning(float span) const {
    int n = Snap(span + gapHeight, rowHeight + gapHeight);
    return n < 1 ? 1 : n;
}

static bool AddSigned(int value, int delta, int* out) {
    int64_t sum = (int64_t)value + (int64_t)delta;
    if (sum < 0 || sum > 0x7fffffff) {
        return false;
    }
    *out = (int)sum;
    return true;
}

static bool SameLayout(const Vec<GridArea>& a, const Vec<GridArea>& b) {
    if (len(a) != len(b)) {
        return false;
    }
    for (int i = 0; i < len(a); i++) {
        const GridArea& x = a[i];
        const GridArea& y = b[i];
        if (!StrEq(x.id, y.id) || x.col != y.col || x.row != y.row ||
            x.colSpan != y.colSpan || x.rowSpan != y.rowSpan ||
            x.minColSpan != y.minColSpan || x.minRowSpan != y.minRowSpan ||
            x.maxColSpan != y.maxColSpan || x.maxRowSpan != y.maxRowSpan ||
            x.locked != y.locked) {
            return false;
        }
    }
    return true;
}

static const GridArea* FindArea(const GridArea* items, int n, Str id) {
    for (int i = 0; i < n; i++) {
        if (StrEq(items[i].id, id)) {
            return &items[i];
        }
    }
    return nullptr;
}

GridLayoutState::GridLayoutState() {
    focus = FocusHandleNew((App*)nullptr);
}

GridLayoutState::~GridLayoutState() {
    if (ids) {
        ArenaDelete(ids);
    }
}

GridLayoutRules GridLayoutState::Rules() const {
    GridLayoutRules rules;
    rules.columns = columns;
    rules.hasCompaction = hasCompaction;
    rules.compaction = compaction;
    rules.preventCollision = preventCollision;
    return rules;
}

void GridLayoutState::Normalize() {
    if (len(areas) <= 0) {
        return;
    }
    GridNormalize(areas.els, len(areas), Rules());
}

void GridLayoutState::Adopt(const GridArea* items, int n) {
    Arena* fresh = ArenaNew();
    Vec<GridArea> next;
    for (int i = 0; i < n; i++) {
        GridArea area = items[i];
        area.id = StrDup(fresh, area.id);
        VecAppend(next, area);
    }
    Str keep = {};
    bool found = false;
    if (hasSelected) {
        for (int i = 0; i < len(next); i++) {
            if (StrEq(next[i].id, selected)) {
                keep = next[i].id;
                found = true;
                break;
            }
        }
    }
    areas = next;
    if (ids) {
        ArenaDelete(ids);
    }
    ids = fresh;
    selected = keep;
    hasSelected = found;
}

GridLayoutState* GridLayoutState::Cols(int cols) {
    columns = cols < 1 ? 1 : cols;
    Normalize();
    return this;
}
GridLayoutState* GridLayoutState::Compaction(bool has, Axis axis) {
    hasCompaction = has;
    compaction = axis;
    Normalize();
    return this;
}
GridLayoutState* GridLayoutState::PreventCollision(bool prevent) {
    preventCollision = prevent;
    return this;
}
GridLayoutState* GridLayoutState::Draggable(bool value) {
    draggable = value;
    return this;
}
GridLayoutState* GridLayoutState::Resizable(bool value) {
    resizable = value;
    return this;
}
GridLayoutState* GridLayoutState::DefaultAreas(const GridArea* items, int n) {
    Adopt(items, n < 0 ? 0 : n);
    Normalize();
    return this;
}

const GridArea* GridLayoutState::Areas(int* n) const {
    if (n) {
        *n = len(areas);
    }
    return areas.els;
}
const GridArea* GridLayoutState::Area(Str id) const {
    return FindArea(areas.els, len(areas), id);
}
bool GridLayoutState::SelectedItem(Str* out) const {
    if (!hasSelected) {
        return false;
    }
    if (out) {
        *out = selected;
    }
    return true;
}
bool GridLayoutState::ActiveItem(Str* out) const {
    if (!hasDrag) {
        return false;
    }
    if (out) {
        *out = drag.item;
    }
    return true;
}

void GridLayoutState::SetAreas(Ctx* cx, const GridArea* items, int n) {
    hasDrag = false;
    VecClear(drag.start);
    VecClear(drag.preview);
    Adopt(items, n < 0 ? 0 : n);
    Normalize();
    if (cx) {
        Notify(cx);
    }
}
void GridLayoutState::SetCols(Ctx* cx, int cols) {
    columns = cols < 1 ? 1 : cols;
    hasDrag = false;
    VecClear(drag.start);
    VecClear(drag.preview);
    Normalize();
    if (cx) {
        Notify(cx);
    }
}
void GridLayoutState::SetCompaction(Ctx* cx, bool has, Axis axis) {
    hasCompaction = has;
    compaction = axis;
    hasDrag = false;
    VecClear(drag.start);
    VecClear(drag.preview);
    Normalize();
    if (cx) {
        Notify(cx);
    }
}
void GridLayoutState::SetPreventCollision(Ctx* cx, bool prevent) {
    preventCollision = prevent;
    if (cx) {
        Notify(cx);
    }
}
void GridLayoutState::SetDraggable(Ctx* cx, bool value) {
    draggable = value;
    if (cx) {
        Notify(cx);
    }
}
void GridLayoutState::SetResizable(Ctx* cx, bool value) {
    resizable = value;
    if (cx) {
        Notify(cx);
    }
}
void GridLayoutState::SetSelectedItem(Ctx* cx, bool has, Str id) {
    Str next = {};
    bool hasNext = false;
    if (has) {
        const GridArea* area = Area(id);
        if (area) {
            next = area->id;
            hasNext = true;
        }
    }
    if (hasSelected == hasNext && StrEq(selected, next)) {
        return;
    }
    hasSelected = hasNext;
    selected = next;
    if (cx) {
        Notify(cx);
    }
}

bool GridLayoutState::Commit(Ctx* cx, const Vec<GridArea>* next) {
    if (!next || SameLayout(areas, *next)) {
        return false;
    }
    areas = *next;
    GridLayoutEvent ev;
    ev.kind = GridLayoutEventKind::Change;
    ev.areas = areas.els;
    ev.nAreas = len(areas);
    EntityEmit(cx ? cx->app : nullptr, cx ? cx->win : nullptr, self, &ev);
    if (cx) {
        Notify(cx);
    }
    return true;
}

bool GridLayoutState::MoveItem(Ctx* cx, Str id, int col, int row) {
    Vec<GridArea> next;
    if (!GridMoveItem(areas.els, len(areas), id, col, row, Rules(), &next)) {
        return false;
    }
    return Commit(cx, &next);
}

bool GridLayoutState::ResizeItem(Ctx* cx, Str id, int colSpan, int rowSpan) {
    Vec<GridArea> next;
    if (!GridResizeItem(areas.els, len(areas), id, colSpan, rowSpan, Rules(),
                        &next)) {
        return false;
    }
    return Commit(cx, &next);
}

bool GridLayoutState::IsGestureAllowed(Str id, GridGesture gesture) const {
    bool allowed = gesture == GridGesture::Move ? draggable : resizable;
    const GridArea* area = Area(id);
    return allowed && area && !area->locked;
}

bool GridLayoutState::SelectedArea(GridArea* out) const {
    if (!hasSelected || !out) {
        return false;
    }
    const GridArea* area = Area(selected);
    if (!area) {
        return false;
    }
    *out = *area;
    return true;
}

bool GridLayoutState::NudgeSelected(Ctx* cx, int cols, int rows) {
    GridArea area;
    if (!SelectedArea(&area) || !IsGestureAllowed(area.id, GridGesture::Move)) {
        return false;
    }
    int col = 0;
    int row = 0;
    if (!AddSigned(area.col, cols, &col) || !AddSigned(area.row, rows, &row)) {
        return false;
    }
    keyboardActive = true;
    bool moved = MoveItem(cx, area.id, col, row);
    if (cx) {
        Notify(cx);
    }
    return moved;
}

bool GridLayoutState::ResizeSelected(Ctx* cx, int cols, int rows) {
    GridArea area;
    if (!SelectedArea(&area) ||
        !IsGestureAllowed(area.id, GridGesture::Resize)) {
        return false;
    }
    int colSpan = 0;
    int rowSpan = 0;
    if (!AddSigned(area.colSpan, cols, &colSpan) ||
        !AddSigned(area.rowSpan, rows, &rowSpan)) {
        return false;
    }
    keyboardActive = true;
    bool resized = ResizeItem(cx, area.id, colSpan, rowSpan);
    if (cx) {
        Notify(cx);
    }
    return resized;
}

void GridLayoutState::Select(Ctx* cx, bool has, Str id) {
    Str next = {};
    bool hasNext = false;
    if (has) {
        const GridArea* area = Area(id);
        if (area) {
            next = area->id;
            hasNext = true;
        }
    }
    if (hasSelected == hasNext && StrEq(selected, next)) {
        return;
    }
    hasSelected = hasNext;
    selected = next;
    GridLayoutEvent ev;
    ev.kind = GridLayoutEventKind::Select;
    ev.hasId = hasNext;
    ev.id = next;
    EntityEmit(cx ? cx->app : nullptr, cx ? cx->win : nullptr, self, &ev);
}

bool GridLayoutState::SelectAdjacent(Ctx* cx, bool forward) {
    Vec<int> order;
    IndicesOf(areas.els, len(areas), false, &order);
    if (len(order) == 0) {
        return false;
    }
    int current = -1;
    if (hasSelected) {
        for (int i = 0; i < len(order); i++) {
            if (StrEq(areas[order[i]].id, selected)) {
                current = i;
                break;
            }
        }
    }
    int next = -1;
    if (current < 0) {
        next = forward ? 0 : len(order) - 1;
    } else if (forward && current + 1 < len(order)) {
        next = current + 1;
    } else if (!forward && current > 0) {
        next = current - 1;
    } else {
        return false;
    }
    keyboardActive = true;
    Select(cx, true, areas[order[next]].id);
    if (cx) {
        Notify(cx);
    }
    return true;
}

void GridLayoutState::Press(Ctx* cx, Str id, float x, float y) {
    hasPress = true;
    pressX = x;
    pressY = y;
    keyboardActive = false;
    Select(cx, true, id);
    if (cx && cx->win) {
        WindowSetFocusId(cx->win, focus.id);
    }
    if (cx) {
        Notify(cx);
    }
}

void GridLayoutState::BeginDrag(Ctx* cx, Str id, GridGesture gesture) {
    float originX = pressX;
    float originY = pressY;
    if (!hasPress && cx && cx->win) {
        originX = cx->win->mouseX;
        originY = cx->win->mouseY;
    }
    hasPress = false;
    Select(cx, true, id);
    hasDrag = true;
    drag.gesture = gesture;
    drag.originX = originX;
    drag.originY = originY;
    drag.deltaX = 0;
    drag.deltaY = 0;
    drag.hasMetrics = false;
    VecClear(drag.start);
    VecClear(drag.preview);
    for (int i = 0; i < len(areas); i++) {
        VecAppend(drag.start, areas[i]);
        VecAppend(drag.preview, areas[i]);
    }
    const GridArea* area = FindArea(drag.start.els, len(drag.start), id);
    drag.item = area ? area->id : Str{};
    if (cx) {
        Notify(cx);
    }
}

void GridLayoutState::DragTo(Ctx* cx, float x, float y, Bounds bounds,
                             float rowHeight, float gapWidth, float gapHeight) {
    if (!hasDrag) {
        return;
    }
    GridMetrics metrics =
        GridMetrics::New(bounds.w, columns, rowHeight, gapWidth, gapHeight);
    const GridArea* start =
        FindArea(drag.start.els, len(drag.start), drag.item);
    if (!start) {
        return;
    }
    GridArea origin = *start;
    drag.deltaX = x - drag.originX;
    drag.deltaY = y - drag.originY;
    drag.metrics = metrics;
    drag.hasMetrics = true;
    Vec<GridArea> next;
    bool ok = false;
    if (drag.gesture == GridGesture::Move) {
        float left = metrics.Left(origin.col) + drag.deltaX;
        float top = metrics.Top(origin.row) + drag.deltaY;
        int room = columns > origin.colSpan ? columns - origin.colSpan : 0;
        int col = metrics.ColumnAt(left);
        if (col > room) {
            col = room;
        }
        ok = GridMoveItem(drag.start.els, len(drag.start), origin.id, col,
                          metrics.RowAt(top), Rules(), &next);
    } else {
        int colSpan = metrics.ColumnsSpanning(
            metrics.SpanWidth(origin.colSpan) + drag.deltaX);
        int rowSpan = metrics.RowsSpanning(metrics.SpanHeight(origin.rowSpan) +
                                           drag.deltaY);
        ok = GridResizeItem(drag.start.els, len(drag.start), origin.id, colSpan,
                            rowSpan, Rules(), &next);
    }
    if (ok) {
        drag.preview = next;
    }
    if (cx) {
        Notify(cx);
    }
}

void GridLayoutState::EndDrag(Ctx* cx) {
    hasPress = false;
    if (!hasDrag) {
        return;
    }
    Vec<GridArea> preview = drag.preview;
    hasDrag = false;
    VecClear(drag.start);
    VecClear(drag.preview);
    if (cx) {
        Notify(cx);
    }
    Commit(cx, &preview);
}

bool GridLayoutState::CancelDrag(Ctx* cx) {
    if (!hasDrag) {
        return false;
    }
    hasDrag = false;
    VecClear(drag.start);
    VecClear(drag.preview);
    if (cx && cx->win) {
        cx->win->activeDrag = {};
    }
    if (cx) {
        Notify(cx);
    }
    return true;
}

const GridArea* GridLayoutState::DisplayedAreas(Ctx* cx, int* n) const {
    if (hasDrag && WindowActiveDrag(cx)) {
        if (n) {
            *n = len(drag.preview);
        }
        return drag.preview.els;
    }
    if (n) {
        *n = len(areas);
    }
    return areas.els;
}

namespace grid_layout {

#define GPUI_ACTION(fn, name)          \
    uint32_t fn() {                    \
        static uint32_t id = 0;        \
        if (!id) {                     \
            id = ActionOf(StrL(name)); \
        }                              \
        return id;                     \
    }

GPUI_ACTION(MoveLeft, "grid_layout::MoveLeft")
GPUI_ACTION(MoveRight, "grid_layout::MoveRight")
GPUI_ACTION(MoveUp, "grid_layout::MoveUp")
GPUI_ACTION(MoveDown, "grid_layout::MoveDown")
GPUI_ACTION(ShrinkWidth, "grid_layout::ShrinkWidth")
GPUI_ACTION(GrowWidth, "grid_layout::GrowWidth")
GPUI_ACTION(ShrinkHeight, "grid_layout::ShrinkHeight")
GPUI_ACTION(GrowHeight, "grid_layout::GrowHeight")
GPUI_ACTION(SelectNextItem, "grid_layout::SelectNextItem")
GPUI_ACTION(SelectPreviousItem, "grid_layout::SelectPreviousItem")

#undef GPUI_ACTION

const char* Context() {
    return "GridLayout";
}

} // namespace grid_layout

void GridLayoutInitKeys() {
    static uint32_t bound = 0;
    if (bound == KeymapGeneration()) {
        return;
    }
    bound = KeymapGeneration();
    const char* ctx = grid_layout::Context();
    KeyBinding bindings[] = {
        {"left", grid_layout::MoveLeft(), ctx},
        {"right", grid_layout::MoveRight(), ctx},
        {"up", grid_layout::MoveUp(), ctx},
        {"down", grid_layout::MoveDown(), ctx},
        {"shift-left", grid_layout::ShrinkWidth(), ctx},
        {"shift-right", grid_layout::GrowWidth(), ctx},
        {"shift-up", grid_layout::ShrinkHeight(), ctx},
        {"shift-down", grid_layout::GrowHeight(), ctx},
        {"tab", grid_layout::SelectNextItem(), ctx},
        {"shift-tab", grid_layout::SelectPreviousItem(), ctx},
        {"escape", action::Cancel(), ctx},
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

bool GridLayoutHandleAction(GridLayoutState* state, Ctx* cx, uint32_t action) {
    if (!state) {
        return false;
    }
    if (action == grid_layout::MoveLeft()) {
        return state->NudgeSelected(cx, -1, 0);
    }
    if (action == grid_layout::MoveRight()) {
        return state->NudgeSelected(cx, 1, 0);
    }
    if (action == grid_layout::MoveUp()) {
        return state->NudgeSelected(cx, 0, -1);
    }
    if (action == grid_layout::MoveDown()) {
        return state->NudgeSelected(cx, 0, 1);
    }
    if (action == grid_layout::ShrinkWidth()) {
        return state->ResizeSelected(cx, -1, 0);
    }
    if (action == grid_layout::GrowWidth()) {
        return state->ResizeSelected(cx, 1, 0);
    }
    if (action == grid_layout::ShrinkHeight()) {
        return state->ResizeSelected(cx, 0, -1);
    }
    if (action == grid_layout::GrowHeight()) {
        return state->ResizeSelected(cx, 0, 1);
    }
    if (action == grid_layout::SelectNextItem()) {
        return state->SelectAdjacent(cx, true);
    }
    if (action == grid_layout::SelectPreviousItem()) {
        return state->SelectAdjacent(cx, false);
    }
    if (action == action::Cancel()) {
        return state->CancelDrag(cx);
    }
    return false;
}

// ─── element (crates/base/src/grid_layout/grid_layout.rs) ────────────────

static const char* kGridDragKind = "grid-layout";

struct ItemFrame {
    float left = 0;
    float width = 0;
    float top = 0;
    float height = 0;
};

static ItemFrame FrameOf(const GridArea& area, int columns, float rowHeight,
                         float gapH) {
    float cols = (float)(columns < 1 ? 1 : columns);
    ItemFrame frame;
    frame.left = (float)area.col / cols;
    frame.width = (float)area.colSpan / cols;
    frame.top = (rowHeight + gapH) * (float)area.row;
    float gaps = area.rowSpan > 0 ? (float)(area.rowSpan - 1) : 0;
    float height = rowHeight * (float)area.rowSpan + gapH * gaps;
    frame.height = height < 0 ? 0 : height;
    return frame;
}

static bool FollowingFrame(const GridDrag& drag, const GridMetrics& metrics,
                           int columns, ItemFrame* out) {
    const GridArea* start =
        FindArea(drag.start.els, len(drag.start), drag.item);
    if (!start || !out) {
        return false;
    }
    float track = metrics.width + metrics.gapWidth;
    if (!(track > 0)) {
        return false;
    }
    float left = metrics.Left(start->col);
    float top = metrics.Top(start->row);
    float width = metrics.SpanWidth(start->colSpan);
    float height = metrics.SpanHeight(start->rowSpan);
    if (drag.gesture == GridGesture::Move) {
        float room = metrics.width - width;
        if (room < 0) {
            room = 0;
        }
        left = left + drag.deltaX;
        if (left < 0) {
            left = 0;
        }
        if (left > room) {
            left = room;
        }
        top = top + drag.deltaY;
        if (top < 0) {
            top = 0;
        }
    } else {
        int minCols = 1;
        int minRows = 1;
        int maxCols = 1;
        int unused = 1;
        ClampSize(*start, 1, 1, columns, &minCols, &minRows);
        ClampSize(*start, 0x7fffffff, 1, columns, &maxCols, &unused);
        width = width + drag.deltaX;
        float minW = metrics.SpanWidth(minCols);
        float maxW = metrics.SpanWidth(maxCols);
        if (width < minW) {
            width = minW;
        }
        if (width > maxW) {
            width = maxW;
        }
        height = height + drag.deltaY;
        float minH = metrics.SpanHeight(minRows);
        if (height < minH) {
            height = minH;
        }
        if (start->maxRowSpan > 0) {
            float maxH = metrics.SpanHeight(start->maxRowSpan);
            if (height > maxH) {
                height = maxH;
            }
        }
    }
    out->left = left / track;
    out->width = (width + metrics.gapWidth) / track;
    out->top = top;
    out->height = height;
    return true;
}

static El* PlaceItem(El* element, ItemFrame frame, float gapW) {
    return element->Absolute()
        ->LeftRel(frame.left)
        ->WFrac(frame.width)
        ->Top(frame.top)
        ->H(frame.height)
        ->PadR(gapW);
}

static uint32_t FrameChannel(const GridLayoutState* state, Str item,
                             const char* channel) {
    char buf[160];
    int n =
        snprintf(buf, sizeof(buf), "grid:%d:%u:",
                 state && state->self.id.IsValid() ? state->self.id.index : -1,
                 state ? state->self.id.gen : 0);
    if (n < 0) {
        n = 0;
    }
    int room = (int)sizeof(buf) - n - 1;
    int copy = len(item);
    if (copy > room) {
        copy = room;
    }
    if (copy > 0 && item.s && n < (int)sizeof(buf)) {
        memcpy(buf + n, item.s, (size_t)copy);
        n += copy;
    }
    if (n >= (int)sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
    }
    buf[n] = 0;
    return MotionId(Str(buf, n), Str(channel));
}

static ItemFrame AnimateFrame(Ctx* cx, const GridLayoutState* state, Str item,
                              ItemFrame target, const Motion& policy) {
    ItemFrame frame;
    frame.left = motion::transition(
        cx, motion::TransitionId(FrameChannel(state, item, "left")),
        target.left, policy);
    frame.width = motion::transition(
        cx, motion::TransitionId(FrameChannel(state, item, "width")),
        target.width, policy);
    frame.top = motion::transition(
        cx, motion::TransitionId(FrameChannel(state, item, "top")), target.top,
        policy);
    frame.height = motion::transition(
        cx, motion::TransitionId(FrameChannel(state, item, "height")),
        target.height, policy);
    return frame;
}

static int AreaIndexOf(const GridLayoutState* state, Str id) {
    if (!state) {
        return -1;
    }
    for (int i = 0; i < len(state->areas); i++) {
        if (StrEq(state->areas[i].id, id)) {
            return i;
        }
    }
    return -1;
}

static const int64_t kGridHandleOnly = (int64_t)1 << 32;

static void BeginAt(GridLayoutState* self, Ctx* cx, const MouseDownEvent* ev,
                    int ix, bool begin, GridGesture gesture) {
    if (!self || ix < 0 || ix >= len(self->areas)) {
        return;
    }
    Str id = self->areas[ix].id;
    float x = ev ? ev->x : 0;
    float y = ev ? ev->y : 0;
    self->Press(cx, id, x, y);
    if (begin && !self->paintReadOnly && self->IsGestureAllowed(id, gesture)) {
        self->BeginDrag(cx, id, gesture);
    }
}

static void OnGridItemDown(GridLayoutState* self, Ctx* cx,
                           const MouseDownEvent* ev, int64_t arg) {
    bool handleOnly = (arg & kGridHandleOnly) != 0;
    int ix = (int)(arg & 0xffffffff);
    BeginAt(self, cx, ev, ix, !handleOnly, GridGesture::Move);
}

static void OnGridResizeDown(GridLayoutState* self, Ctx* cx,
                             const MouseDownEvent* ev, int64_t arg) {
    WindowStopPropagation(cx);
    BeginAt(self, cx, ev, (int)arg, true, GridGesture::Resize);
}

static void OnGridHandleDown(GridLayoutState* self, Ctx* cx,
                             const MouseDownEvent* ev, int64_t arg) {
    BeginAt(self, cx, ev, (int)arg, true, GridGesture::Move);
}

static void OnGridSurfaceDrag(GridLayoutState* self, Ctx* cx,
                              const DragMoveEvent* ev) {
    if (!self || !ev || self->paintReadOnly || !self->hasDrag) {
        return;
    }
    self->DragTo(cx, ev->event.x, ev->event.y, ev->el, self->paintRowHeight,
                 self->paintGapW, self->paintGapH);
}

static void OnGridSurfaceUp(GridLayoutState* self, Ctx* cx,
                            const MouseUpEvent*) {
    if (self) {
        self->EndDrag(cx);
    }
}

static bool GridMoveAction(uint32_t action) {
    return action == grid_layout::MoveLeft() ||
           action == grid_layout::MoveRight() ||
           action == grid_layout::MoveUp() ||
           action == grid_layout::MoveDown() ||
           action == grid_layout::ShrinkWidth() ||
           action == grid_layout::GrowWidth() ||
           action == grid_layout::ShrinkHeight() ||
           action == grid_layout::GrowHeight();
}

static void OnGridAction(GridLayoutState* self, Ctx* cx,
                         const ActionEvent* ev) {
    if (!self || !ev) {
        return;
    }
    if (self->paintReadOnly && GridMoveAction(ev->action)) {
        const_cast<ActionEvent*>(ev)->propagate = true;
        return;
    }
    if (!GridLayoutHandleAction(self, cx, ev->action)) {
        const_cast<ActionEvent*>(ev)->propagate = true;
    }
}

static void BindGridAction(El* surface, Entity<GridLayoutState> state,
                           uint32_t action) {
    surface->OnAction(action, ListenTo(state, &OnGridAction));
}

GridItem* GridItem::New(Ctx* cx, Str id) {
    GridItem* item = ArenaNew<GridItem>(cx->a);
    item->arena = cx->a;
    item->id = id;
    return item;
}

GridItem* GridItem::Label(Str text) {
    label = text;
    hasLabel = text.s != nullptr;
    return this;
}

GridItem* GridItem::DragHandleOnly(bool value) {
    handleOnly = value;
    return this;
}

GridItem* GridItem::Pad(float x, float y) {
    style.Pad(x, y);
    return this;
}

GridItem* GridItem::Child(El* el) {
    if (!el || !arena) {
        return this;
    }
    GridItemChild* node = ArenaNew<GridItemChild>(arena);
    node->el = el;
    if (lastChild) {
        lastChild->next = node;
    } else {
        children = node;
    }
    lastChild = node;
    return this;
}

El* GridDragHandle(Ctx* cx, Entity<GridLayoutState> state, Str item) {
    El* handle = Div(cx->a)->PathClick(StrL("grid-drag-handle"));
    GridLayoutState* grid = state.Get(cx);
    int ix = AreaIndexOf(grid, item);
    if (ix >= 0) {
        handle->OnMouseDown(ListenTo(state, &OnGridHandleDown, ix))
            ->OnDrag(Str(kGridDragKind), (int)GridGesture::Move);
    }
    return handle;
}

GridLayout* GridLayout::New(Ctx* cx, Str id, Entity<GridLayoutState> state) {
    GridLayout* grid = ArenaNew<GridLayout>(cx->a);
    grid->cx = cx;
    grid->arena = cx->a;
    grid->id = id;
    grid->state = state;
    return grid;
}

GridLayout* GridLayout::Label(Str text) {
    label = text;
    return this;
}

GridLayout* GridLayout::Item(GridItem* item) {
    if (!item || !arena) {
        return this;
    }
    GridItemNode* node = ArenaNew<GridItemNode>(arena);
    node->item = *item;
    if (lastItem) {
        lastItem->next = node;
    } else {
        items = node;
    }
    lastItem = node;
    nItems++;
    return this;
}

GridLayout* GridLayout::RowHeight(float rems) {
    rowHeightRems = rems;
    return this;
}

GridLayout* GridLayout::Gap(float px) {
    gapX = px;
    gapY = px;
    return this;
}

GridLayout* GridLayout::GapX(float px) {
    gapX = px;
    return this;
}

GridLayout* GridLayout::GapY(float px) {
    gapY = px;
    return this;
}

GridLayout* GridLayout::Transition(Motion transition) {
    settle = transition;
    return this;
}

GridLayout* GridLayout::DraggingItem(const StateStyle& next) {
    draggingItem = next;
    return this;
}

GridLayout* GridLayout::SelectedItem(const StateStyle& next) {
    selectedItem = next;
    return this;
}

GridLayout* GridLayout::FocusedItem(const StateStyle& next) {
    focusedItem = next;
    return this;
}

GridLayout* GridLayout::Reflow(int cols) {
    hasReflow = true;
    reflowCols = cols < 1 ? 1 : cols;
    return this;
}

GridLayout* GridLayout::Placeholder(GridPartFn fn, void* data) {
    placeholder = fn;
    placeholderData = data;
    return this;
}

GridLayout* GridLayout::ResizeHandle(GridPartFn fn, void* data) {
    resizeHandle = fn;
    resizeHandleData = data;
    return this;
}

void GridLayout::ResolvedGap(float remSize, float* x, float* y) const {
    (void)remSize;
    float gx = gapX < 0 ? 0 : gapX;
    float gy = gapY < 0 ? 0 : gapY;
    if (x) {
        *x = gx;
    }
    if (y) {
        *y = gy;
    }
}

El* GridLayout::IntoEl() {
    Arena* a = arena ? arena : (cx ? cx->a : nullptr);
    if (!a) {
        return nullptr;
    }
    GridLayoutState* grid = state.Get(cx);
    if (!grid) {
        return Div(a);
    }
    grid->self = state;

    float gapW = 0;
    float gapH = 0;
    ResolvedGap(WindowRemSize(cx ? cx->win : nullptr), &gapW, &gapH);
    float rowH = Rems(cx, rowHeightRems);
    int columns = hasReflow ? reflowCols : grid->ColumnCount();
    if (columns < 1) {
        columns = 1;
    }
    grid->paintRowHeight = rowH;
    grid->paintGapW = gapW;
    grid->paintGapH = gapH;
    grid->paintColumns = columns;
    grid->paintReadOnly = hasReflow;

    Vec<GridArea> reflowed;
    const GridArea* displayed = nullptr;
    int nDisplayed = 0;
    if (hasReflow) {
        int nAreas = 0;
        const GridArea* stored = grid->Areas(&nAreas);
        GridReflowed(stored, nAreas, columns, &reflowed);
        displayed = reflowed.els;
        nDisplayed = len(reflowed);
    } else {
        displayed = grid->DisplayedAreas(cx, &nDisplayed);
    }

    bool showDrag = !hasReflow && grid->hasDrag && WindowActiveDrag(cx);
    Str activeId = {};
    bool hasActive = showDrag && grid->ActiveItem(&activeId);
    ItemFrame following;
    bool hasFollowing = false;
    if (hasActive && grid->drag.hasMetrics) {
        hasFollowing =
            FollowingFrame(grid->drag, grid->drag.metrics, columns, &following);
    }
    bool focusVisible =
        grid->keyboardActive &&
        FocusHandleIsFocused(cx ? cx->win : nullptr, grid->focus);
    Str selected = {};
    bool hasSelected = grid->SelectedItem(&selected);

    int rows = GridRows(displayed, nDisplayed);
    float height = rowH * (float)rows;
    if (rows > 0) {
        height += gapH * (float)(rows - 1);
    }
    if (height < 0) {
        height = 0;
    }

    El* surface = Div(a)
                      ->PathId(StrL("grid-layout-surface"))
                      ->Role(AccessibilityRole::Group)
                      ->AriaLabel(label.s ? label : StrL("Grid layout"))
                      ->KeyContext(Str(grid_layout::Context()))
                      ->TrackFocus(grid->focus)
                      ->TabStop(true)
                      ->W(kFill)
                      ->H(height)
                      ->OnDragMove(ListenTo(state, &OnGridSurfaceDrag))
                      ->OnMouseUp(ListenTo(state, &OnGridSurfaceUp),
                                  DispatchPhase::Capture)
                      ->OnMouseUpOut(ListenTo(state, &OnGridSurfaceUp));
    uint32_t actions[] = {
        grid_layout::MoveLeft(),
        grid_layout::MoveRight(),
        grid_layout::MoveUp(),
        grid_layout::MoveDown(),
        grid_layout::ShrinkWidth(),
        grid_layout::GrowWidth(),
        grid_layout::ShrinkHeight(),
        grid_layout::GrowHeight(),
        grid_layout::SelectNextItem(),
        grid_layout::SelectPreviousItem(),
        action::Cancel(),
    };
    for (int i = 0; i < (int)(sizeof(actions) / sizeof(actions[0])); i++) {
        BindGridAction(surface, state, actions[i]);
    }

    El* track = Div(a)->Absolute()->Top(0)->Left(0)->Bottom(0)->Right(-gapW);
    if (placeholder && hasActive && displayed) {
        const GridArea* mark = FindArea(displayed, nDisplayed, activeId);
        if (mark) {
            El* painted = placeholder(cx, placeholderData);
            El* box =
                PlaceItem(Div(a), FrameOf(*mark, columns, rowH, gapH), gapW);
            if (painted) {
                box->Child(Div(a)->SizeFull()->Child(painted));
            }
            track->Child(box);
        }
    }

    Vec<GridItemNode*> order;
    for (GridItemNode* node = items; node; node = node->next) {
        if (!hasActive || !StrEq(node->item.id, activeId)) {
            VecAppend(order, node);
        }
    }
    if (hasActive) {
        for (GridItemNode* node = items; node; node = node->next) {
            if (StrEq(node->item.id, activeId)) {
                VecAppend(order, node);
            }
        }
    }

    for (int i = 0; i < len(order); i++) {
        const GridItem& item = order[i]->item;
        const GridArea* area =
            displayed ? FindArea(displayed, nDisplayed, item.id) : nullptr;
        if (!area) {
            continue;
        }
        bool active = hasActive && StrEq(item.id, activeId);
        ItemFrame target = (active && hasFollowing)
                               ? following
                               : FrameOf(*area, columns, rowH, gapH);
        Motion policy = active ? MotionNew(0) : settle;
        ItemFrame frame = AnimateFrame(cx, grid, item.id, target, policy);
        bool selectedHere = hasSelected && StrEq(selected, area->id);
        int ix = AreaIndexOf(grid, item.id);
        int64_t arg = ix;
        if (item.handleOnly) {
            arg |= kGridHandleOnly;
        }
        bool movable = !hasReflow && !item.handleOnly &&
                       grid->IsGestureAllowed(item.id, GridGesture::Move);
        bool resizable =
            !hasReflow && grid->IsGestureAllowed(item.id, GridGesture::Resize);

        StateStyle look = item.style;
        if (selectedHere) {
            StateStyleRefine(&look, selectedItem);
            if (focusVisible) {
                StateStyleRefine(&look, focusedItem);
            }
        }
        if (active) {
            StateStyleRefine(&look, draggingItem);
        }

        El* placed = PlaceItem(Div(a)->PathClick(item.id), frame, gapW);
        placed->Role(AccessibilityRole::Group)->AriaSelected(selectedHere);
        if (item.hasLabel) {
            placed->AriaLabel(item.label);
        }
        if (ix >= 0) {
            placed->OnMouseDown(ListenTo(state, &OnGridItemDown, arg));
        }
        if (movable) {
            placed->OnDrag(Str(kGridDragKind), (int)GridGesture::Move);
        }
        El* inner = Div(a)->SizeFull();
        StyleApplyFields(&inner->style, look.style, look.set);
        for (GridItemChild* child = item.children; child; child = child->next) {
            inner->Child(child->el);
        }
        if (resizable && ix >= 0) {
            El* corner =
                Div(a)
                    ->PathClick(StrL("grid-layout-resize"))
                    ->Absolute()
                    ->Right(0)
                    ->Bottom(0)
                    ->W(kGridResizeHandle)
                    ->H(kGridResizeHandle)
                    ->Cursor(CursorKind::ResizeUpLeftDownRight)
                    ->OnMouseDown(ListenTo(state, &OnGridResizeDown, ix))
                    ->OnDrag(Str(kGridDragKind), (int)GridGesture::Resize);
            if (resizeHandle) {
                El* grip = resizeHandle(cx, resizeHandleData);
                if (grip) {
                    corner->Child(grip);
                }
            }
            inner->Child(corner);
        }
        placed->Child(inner);
        track->Child(placed);
    }
    surface->Child(track);

    El* outer = Div(a)->PathId(id)->W(kFill);
    if (gapX != 0 || gapY != 0) {
        outer->GapX(gapX)->GapY(gapY);
    }
    if (gridStyle.set) {
        ElRefine(outer, gridStyle);
    }
    return outer->Child(surface);
}

} // namespace gpui
