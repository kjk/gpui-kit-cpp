#include "base/grid_layout.h"

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

} // namespace gpui
