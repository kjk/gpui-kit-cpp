#include "base/data_table.h"
#include "base/actions.h"
#include "gpui/keymap.h"

namespace gpui {

const Str kTableResizeDrag = StrL("table-resize-col");
const Str kTableColDrag = StrL("table-move-col");

Str TableContext() {
    return StrL("DataTable");
}

void TableInitKeys() {
    static uint32_t bound = 0;
    if (bound == KeymapGeneration()) {
        return;
    }
    bound = KeymapGeneration();
    const char* ctx = "DataTable";
    KeyBinding bindings[] = {
        {"escape", action::Cancel(), ctx},
        {"up", action::SelectUp(), ctx},
        {"down", action::SelectDown(), ctx},
        {"left", action::SelectPrevColumn(), ctx},
        {"right", action::SelectNextColumn(), ctx},
        {"home", action::SelectFirst(), ctx},
        {"end", action::SelectLast(), ctx},
        {"pageup", action::SelectPageUp(), ctx},
        {"pagedown", action::SelectPageDown(), ctx},
        {"tab", action::SelectNextColumn(), ctx},
        {"shift-tab", action::SelectPrevColumn(), ctx},
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

TableAction TableActionOf(uint32_t id) {
    if (id == action::SelectUp()) {
        return TableAction::SelectPrev;
    }
    if (id == action::SelectDown()) {
        return TableAction::SelectNext;
    }
    if (id == action::SelectPrevColumn()) {
        return TableAction::SelectPrevColumn;
    }
    if (id == action::SelectNextColumn()) {
        return TableAction::SelectNextColumn;
    }
    if (id == action::SelectFirst()) {
        return TableAction::SelectFirst;
    }
    if (id == action::SelectLast()) {
        return TableAction::SelectLast;
    }
    if (id == action::SelectPageUp()) {
        return TableAction::SelectPageUp;
    }
    if (id == action::SelectPageDown()) {
        return TableAction::SelectPageDown;
    }
    if (id == action::Cancel()) {
        return TableAction::Cancel;
    }
    return TableAction::None;
}

ColumnSort TableNextSort(ColumnSort s) {
    switch (s) {
        case ColumnSort::Ascending:
            return ColumnSort::Default;
        case ColumnSort::Descending:
            return ColumnSort::Ascending;
        default:
            return ColumnSort::Descending;
    }
}

ColumnSort TableSortOf(const TableState* s, int col) {
    return s->sortCol == col ? s->sort : ColumnSort::Default;
}

// cx.emit(TableEvent::..) — see the note on ListEmit.
static void TableEmit(TableState* s, Ctx* cx, TableEventKind kind, int row,
                      int col, ColumnSort sort) {
    TableEvent ev = {kind, row, col, sort};
    if (s->onEvent.IsValid()) {
        ListenerCall(cx->app, cx->win, s->onEvent, &ev);
    }
    EntityEmit(cx->app, cx->win, s->self, &ev);
}

void TableEnsureCols(TableState* s, int n) {
    while (s->colWidth.len < n) {
        VecAppend(s->colWidth, 0.f);
    }
    while (s->colMinWidths.len < n) {
        VecAppend(s->colMinWidths, s->colMinWidth);
    }
    while (s->colMaxWidths.len < n) {
        VecAppend(s->colMaxWidths, s->colMaxWidth);
    }
    while (s->colOrder.len < n) {
        VecAppend(s->colOrder, s->colOrder.len);
    }
    while (s->colBounds.len < n) {
        VecAppend(s->colBounds, Bounds{});
    }
}

float TableColWidth(const TableState* s, int col, float declared) {
    if (col < 0 || col >= s->colWidth.len || s->colWidth[col] <= 0) {
        return declared;
    }
    return s->colWidth[col];
}

void TableSeedColWidth(TableState* s, int col, float declared) {
    if (col < 0) {
        return;
    }
    TableEnsureCols(s, col + 1);
    if (s->colWidth[col] <= 0) {
        s->colWidth[col] = declared;
    }
}

float TableClampColWidth(const TableState* s, float width) {
    if (width < s->colMinWidth) {
        width = s->colMinWidth;
    }
    if (s->colMaxWidth > 0 && width > s->colMaxWidth) {
        width = s->colMaxWidth;
    }
    return width;
}

float TableClampColWidth(const TableState* s, int col, float width) {
    float minWidth = s->colMinWidth;
    float maxWidth = s->colMaxWidth;
    if (col >= 0 && col < s->colMinWidths.len) {
        minWidth = s->colMinWidths[col];
    }
    if (col >= 0 && col < s->colMaxWidths.len) {
        maxWidth = s->colMaxWidths[col];
    }
    if (width < minWidth) {
        width = minWidth;
    }
    if (maxWidth > 0 && width > maxWidth) {
        width = maxWidth;
    }
    return width;
}

void TableSetColConstraints(TableState* s, int col, float minWidth,
                            float maxWidth) {
    if (!s || col < 0) {
        return;
    }
    TableEnsureCols(s, col + 1);
    s->colMinWidths[col] = minWidth;
    s->colMaxWidths[col] = maxWidth;
}

void TableResizeCol(TableState* s, Ctx* cx, int col, float width) {
    if (!s->colResizable || col < 0 || col >= s->colCount) {
        return;
    }
    TableEnsureCols(s, col + 1);
    width = TableClampColWidth(s, col, width);
    // Rust only lays the header out again and notifies when the clamp let
    // something through.
    if (s->colWidth[col] == width) {
        return;
    }
    s->colWidth[col] = width;
    Notify(cx);
}

void TablePerformSort(TableState* s, Ctx* cx, int col) {
    if (!s->sortable || col < 0 || col >= s->colCount) {
        return;
    }
    ColumnSort next = TableNextSort(TableSortOf(s, col));
    // Sorting a column resets every other one, which is what Rust's loop over
    // the column groups does.
    s->sortCol = col;
    s->sort = next;
    if (s->delegateSort) {
        s->delegateSort(cx, s->delegateData, col, next);
    }
    Notify(cx);
    TableEmit(s, cx, TableEventKind::Sort, -1, col, next);
}

void TableSetSelectedRow(TableState* s, Ctx* cx, int row) {
    if (!s->rowSelectable || row < 0 || row >= s->rowCount) {
        return;
    }
    s->mode = TableSelectionMode::Row;
    s->rightClickedRow = -1;
    s->rightClickedCellRow = -1;
    s->rightClickedCellCol = -1;
    s->selectedRow = row;
    s->selectedCellRow = -1;
    s->selectedCellCol = -1;
    // set_selected_row scrolls the row into view. Rust picks Bottom going
    // down and Top coming back up, and both fall into the same branch — the
    // one that scrolls as little as it can — so a walk with the arrow keys
    // moves a row at a time and a row already in view does not move at all.
    TableScrollToRow(s, row, ScrollStrategy::Top);
    Notify(cx);
    TableEmit(s, cx, TableEventKind::SelectRow, row, -1, ColumnSort::Default);
    // set_selected_row emits RightClickedRow(None) beside it: whatever was
    // right-clicked is no longer what the selection is about, so a context
    // menu hanging off it is told to go. The marks themselves were cleared
    // above, before the row moved.
    TableEmit(s, cx, TableEventKind::RightClickedRow, -1, -1,
              ColumnSort::Default);
}

void TableSetSelectedCol(TableState* s, Ctx* cx, int col) {
    if (!s->colSelectable || col < 0 || col >= s->colCount) {
        return;
    }
    s->mode = TableSelectionMode::Column;
    s->selectedCol = col;
    TableScrollToCol(s, col, ScrollStrategy::Top);
    Notify(cx);
    TableEmit(s, cx, TableEventKind::SelectCol, -1, col, ColumnSort::Default);
}

void TableSetSelectedCell(TableState* s, Ctx* cx, int row, int col) {
    if (!s->cellSelectable || row < 0 || row >= s->rowCount || col < 0 ||
        col >= s->colCount) {
        return;
    }
    s->mode = TableSelectionMode::Cell;
    s->selectedCellRow = row;
    s->selectedCellCol = col;
    // Rust centres the row and brings the column in from whichever side it
    // is off, which is the pair of strategies here too.
    TableScrollToRow(s, row, ScrollStrategy::Center);
    TableScrollToCol(s, col, ScrollStrategy::Top);
    Notify(cx);
    TableEmit(s, cx, TableEventKind::SelectCell, row, col, ColumnSort::Default);
}

int TableSelectedRow(const TableState* s) {
    return s->mode == TableSelectionMode::Row ? s->selectedRow : -1;
}

int TableSelectedCol(const TableState* s) {
    return s->mode == TableSelectionMode::Column ? s->selectedCol : -1;
}

bool TableSelectedCell(const TableState* s, int* row, int* col) {
    if (s->mode != TableSelectionMode::Cell || s->selectedCellRow < 0 ||
        s->selectedCellCol < 0) {
        return false;
    }
    if (row) {
        *row = s->selectedCellRow;
    }
    if (col) {
        *col = s->selectedCellCol;
    }
    return true;
}

TableSelection TableSelectionOf(const TableState* s) {
    TableSelection out;
    int row = TableSelectedRow(s);
    int col = TableSelectedCol(s);
    if (row >= 0) {
        out.kind = TableSelectionKind::Row;
        out.row = row;
    } else if (col >= 0) {
        out.kind = TableSelectionKind::Column;
        out.col = col;
    } else if (TableSelectedCell(s, &row, &col)) {
        out.kind = TableSelectionKind::Cell;
        out.row = row;
        out.col = col;
    }
    return out;
}

void TableSetSelection(TableState* s, Ctx* cx, TableSelection selection) {
    switch (selection.kind) {
        case TableSelectionKind::None:
            TableClearSelection(s, cx);
            break;
        case TableSelectionKind::Row:
            TableSetSelectedRow(s, cx, selection.row);
            break;
        case TableSelectionKind::Column:
            TableSetSelectedCol(s, cx, selection.col);
            break;
        case TableSelectionKind::Cell:
            TableSetSelectedCell(s, cx, selection.row, selection.col);
            break;
    }
}

bool TableEscalatesToRow(const TableState* s, int row, int col,
                         bool doubleClick) {
    int cellRow = -1;
    int cellCol = -1;
    bool reselect = TableSelectedCell(s, &cellRow, &cellCol) &&
                    cellRow == row && cellCol == col;
    return !s->rowHeader && s->rowSelectable && reselect && !doubleClick;
}

void TableClearSelection(TableState* s, Ctx* cx) {
    s->mode = TableSelectionMode::None;
    s->selectedRow = -1;
    s->selectedCol = -1;
    s->selectedCellRow = -1;
    s->selectedCellCol = -1;
    Notify(cx);
    TableEmit(s, cx, TableEventKind::Cancel, -1, -1, ColumnSort::Default);
}

static bool TableHasSelection(const TableState* s) {
    return TableSelectionOf(s).kind != TableSelectionKind::None;
}

// The moves, each written the way its Rust handler is. Only up, down, left
// and right consult loop_selection; a page move and Home/End clamp.
static int RowPrev(const TableState* s) {
    int from = s->selectedRow < 0 ? 0 : s->selectedRow;
    if (from > 0) {
        return from - 1;
    }
    return s->loopSelection ? s->rowCount - 1 : 0;
}

static int RowNext(const TableState* s) {
    int last = s->rowCount - 1;
    if (s->selectedRow < 0) {
        return 0;
    }
    if (s->selectedRow < last) {
        return s->selectedRow + 1;
    }
    return s->loopSelection ? 0 : last;
}

static int ColPrev(const TableState* s) {
    int from = s->selectedCol < 0 ? 0 : s->selectedCol;
    if (from > 0) {
        return from - 1;
    }
    return s->loopSelection ? s->colCount - 1 : 0;
}

static int ColNext(const TableState* s) {
    int last = s->colCount - 1;
    if (s->selectedCol < 0) {
        return 0;
    }
    if (s->selectedCol < last) {
        return s->selectedCol + 1;
    }
    return s->loopSelection ? 0 : last;
}

// The same pair over the row of a selected cell.
static int CellRowPrev(const TableState* s) {
    int row = s->selectedCellRow;
    if (row > 0) {
        return row - 1;
    }
    return s->loopSelection ? s->rowCount - 1 : row;
}

static int CellRowNext(const TableState* s) {
    int last = s->rowCount - 1;
    if (s->selectedCellRow < last) {
        return s->selectedCellRow + 1;
    }
    return s->loopSelection ? 0 : last;
}

static int CellColPrev(const TableState* s) {
    int col = s->selectedCellCol;
    if (col > 0) {
        return col - 1;
    }
    return s->loopSelection ? s->colCount - 1 : col;
}

static int CellColNext(const TableState* s) {
    int last = s->colCount - 1;
    if (s->selectedCellCol < last) {
        return s->selectedCellCol + 1;
    }
    return s->loopSelection ? 0 : last;
}

static int Clamp(int v, int lo, int hi) {
    if (v < lo) {
        return lo;
    }
    return v > hi ? hi : v;
}

void TablePerform(TableState* s, Ctx* cx, TableAction act) {
    if (act == TableAction::Cancel) {
        // action_cancel: Escape gives up the selection, and where there is
        // none Rust propagates it to whatever encloses the table.
        if (TableHasSelection(s)) {
            TableClearSelection(s, cx);
        }
        return;
    }
    if (s->rowCount < 1) {
        return;
    }
    bool cellMode = s->mode == TableSelectionMode::Cell;
    // Every cell-mode handler starts the same way: with no cell selected, the
    // first one is.
    bool noCell = cellMode && s->selectedCellRow < 0;
    if (noCell) {
        int col = act == TableAction::SelectLast ? s->colCount - 1 : 0;
        TableSetSelectedCell(s, cx, 0, col);
        return;
    }
    int last = s->rowCount - 1;
    switch (act) {
        case TableAction::SelectPrev:
            if (cellMode) {
                TableSetSelectedCell(s, cx, CellRowPrev(s), s->selectedCellCol);
            } else {
                TableSetSelectedRow(s, cx, RowPrev(s));
            }
            break;
        case TableAction::SelectNext:
            if (cellMode) {
                TableSetSelectedCell(s, cx, CellRowNext(s), s->selectedCellCol);
            } else {
                TableSetSelectedRow(s, cx, RowNext(s));
            }
            break;
        case TableAction::SelectPrevColumn:
            if (cellMode) {
                TableSetSelectedCell(s, cx, s->selectedCellRow, CellColPrev(s));
            } else {
                TableSetSelectedCol(s, cx, ColPrev(s));
            }
            break;
        case TableAction::SelectNextColumn:
            if (cellMode) {
                TableSetSelectedCell(s, cx, s->selectedCellRow, CellColNext(s));
            } else {
                TableSetSelectedCol(s, cx, ColNext(s));
            }
            break;
        case TableAction::SelectFirst:
            // Home and End are the first and last column, not the first and
            // last row.
            if (cellMode) {
                TableSetSelectedCell(s, cx, s->selectedCellRow, 0);
            } else {
                TableSetSelectedCol(s, cx, 0);
            }
            break;
        case TableAction::SelectLast:
            if (cellMode) {
                TableSetSelectedCell(s, cx, s->selectedCellRow,
                                     s->colCount - 1);
            } else {
                TableSetSelectedCol(s, cx, s->colCount - 1);
            }
            break;
        case TableAction::SelectPageUp:
            if (cellMode) {
                TableSetSelectedCell(
                    s, cx, Clamp(s->selectedCellRow - s->pageRows, 0, last),
                    s->selectedCellCol);
            } else {
                int cur = s->selectedRow < 0 ? 0 : s->selectedRow;
                TableSetSelectedRow(s, cx, Clamp(cur - s->pageRows, 0, last));
            }
            break;
        case TableAction::SelectPageDown:
            if (cellMode) {
                TableSetSelectedCell(
                    s, cx, Clamp(s->selectedCellRow + s->pageRows, 0, last),
                    s->selectedCellCol);
            } else {
                int cur = s->selectedRow < 0 ? 0 : s->selectedRow;
                TableSetSelectedRow(s, cx, Clamp(cur + s->pageRows, 0, last));
            }
            break;
        default:
            break;
    }
}

void TableState::OnRowClick(TableState* self, Ctx* cx, const ClickEvent* ev,
                            intptr_t row) {
    TableSetSelectedRow(self, cx, (int)row);
    if (ev->clickCount == 2) {
        TableEmit(self, cx, TableEventKind::DoubleClickedRow, (int)row, -1,
                  ColumnSort::Default);
    }
}

void TableState::OnCellClick(TableState* self, Ctx* cx, const ClickEvent* ev,
                             intptr_t packed) {
    if (!self->cellSelectable) {
        return;
    }
    int row = TableCellRow(packed);
    int col = TableCellCol(packed);
    bool isDouble = ev->clickCount == 2;
    if (TableEscalatesToRow(self, row, col, isDouble)) {
        TableSetSelectedRow(self, cx, row);
        return;
    }
    // A second click on the cell that is already selected: set_selected_cell
    // sees no change and says nothing, so the double click is emitted here
    // whether or not the selection moved.
    TableSetSelectedCell(self, cx, row, col);
    if (isDouble) {
        TableEmit(self, cx, TableEventKind::DoubleClickedCell, row, col,
                  ColumnSort::Default);
    }
}

void TableState::OnRowMouseDown(TableState* self, Ctx* cx,
                                const MouseDownEvent* ev, intptr_t row) {
    if (ev->button != MouseButton::Right) {
        return;
    }
    // on_row_right_click. The two right-click marks are exclusive, so this
    // clears the cell one.
    self->rightClickedRow = (int)row;
    self->rightClickedCellRow = -1;
    self->rightClickedCellCol = -1;
    TableEmit(self, cx, TableEventKind::RightClickedRow, (int)row, -1,
              ColumnSort::Default);
    Notify(cx);
}

void TableState::OnCellMouseDown(TableState* self, Ctx* cx,
                                 const MouseDownEvent* ev, intptr_t packed) {
    if (ev->button != MouseButton::Right || !self->cellSelectable) {
        return;
    }
    // on_cell_right_click stops the press here, so the row under the cell
    // does not also claim it.
    WindowStopPropagation(cx);
    int row = TableCellRow(packed);
    int col = TableCellCol(packed);
    self->rightClickedCellRow = row;
    self->rightClickedCellCol = col;
    self->rightClickedRow = -1;
    TableEmit(self, cx, TableEventKind::RightClickedCell, row, col,
              ColumnSort::Default);
    Notify(cx);
}

void TableState::OnHeadClick(TableState* self, Ctx* cx, const ClickEvent*,
                             intptr_t col) {
    // on_col_head_click selects the column; the sort icon beside it is what
    // sorts, and it is its own hit box.
    TableSetSelectedCol(self, cx, (int)col);
}

void TableState::OnSortClick(TableState* self, Ctx* cx, const ClickEvent*,
                             intptr_t col) {
    TablePerformSort(self, cx, (int)col);
}

void TableSeedColOrder(TableState* s, int colCount) {
    int n = colCount < 0 ? 0 : colCount;
    TableEnsureCols(s, n);
    if (s->colOrderSeeded && n == s->colCount) {
        return;
    }
    // The columns start in the order the caller declared them, and stay in
    // whatever order the drags leave them.
    for (int i = 0; i < n; i++) {
        s->colOrder[i] = i;
    }
    s->colOrderSeeded = true;
}

int TableColAt(const TableState* s, int display) {
    if (display < 0 || display >= s->colCount || display >= s->colOrder.len) {
        return display;
    }
    return s->colOrderSeeded ? s->colOrder[display] : display;
}

int TableDisplayOfCol(const TableState* s, int col) {
    for (int i = 0; i < s->colCount; i++) {
        if (TableColAt(s, i) == col) {
            return i;
        }
    }
    return -1;
}

bool TableMoveColumn(TableState* s, int from, int to) {
    TableEnsureCols(s, s->colCount);
    int n = s->colCount;
    if (from < 0 || from >= n || to < 0 || to > n) {
        return false;
    }
    // A column dropped into the gap either side of itself has not moved.
    if (to == from || to == from + 1) {
        return false;
    }
    TableSeedColOrder(s, s->colCount);
    int col = s->colOrder[from];
    for (int i = from; i < n - 1; i++) {
        s->colOrder[i] = s->colOrder[i + 1];
    }
    // The gap was worked out before the column came out, so a gap after it
    // has moved down one.
    int at = to > from ? to - 1 : to;
    for (int i = n - 1; i > at; i--) {
        s->colOrder[i] = s->colOrder[i - 1];
    }
    s->colOrder[at] = col;
    return true;
}

int TableDragGapAt(const Bounds* colBounds, int n, float x, int dragCol,
                   int fixedCount) {
    if (fixedCount < 0) {
        fixedCount = 0;
    }
    if (fixedCount > n) {
        fixedCount = n;
    }
    // A column can only be reordered within its own region: rendering pins
    // the first `fixedCount` columns, so a cross-region move would change
    // which columns are pinned without updating their `fixed` flags.
    bool dragInFixed = dragCol < fixedCount;
    bool pointerInFixed = fixedCount > 0 && x < colBounds[fixedCount - 1]
                                                    .Right();
    if (dragInFixed != pointerInFixed) {
        return -1;
    }
    // The candidates are the columns of that region alone: the fixed ones
    // when the pointer is over them, the scrollable ones otherwise.
    int first = pointerInFixed ? 0 : fixedCount;
    int end = pointerInFixed ? fixedCount : n;
    // The gap sits after the last column whose centre is left of `x`.
    int gap = first;
    for (int i = first; i < end; i++) {
        if (x < colBounds[i].x + colBounds[i].w * 0.5f) {
            break;
        }
        gap = i + 1;
    }
    if (gap == dragCol || gap == dragCol + 1) {
        return -1;
    }
    return gap;
}

// Both halves of update_visible_range_if_need. The range-of-one guard is
// Rust's `if visible_range.len() <= 1 { return }`: the virtual list lays a
// single item out to measure with, and telling a delegate that one row is
// visible would be a lie it might go and fetch data on.
bool TableVisibleRowsChanged(TableState* s, int first, int end) {
    if (end - first <= 1) {
        return false;
    }
    if (s->visibleRange.rowFirst == first && s->visibleRange.rowEnd == end) {
        return false;
    }
    s->visibleRange.rowFirst = first;
    s->visibleRange.rowEnd = end;
    return true;
}

bool TableVisibleColsChanged(TableState* s, int first, int end) {
    if (end - first <= 1) {
        return false;
    }
    if (s->visibleRange.colFirst == first && s->visibleRange.colEnd == end) {
        return false;
    }
    s->visibleRange.colFirst = first;
    s->visibleRange.colEnd = end;
    return true;
}

void TableVisibleCols(const TableState* s, int* first, int* end) {
    int nFixed = s->fixedCols, nCols = s->colCount;
    // The pinned columns never move, so the sideways virtual list is built
    // over the ones after them and counts from zero — state.rs hands it
    // `col_groups.iter().skip(left_columns_count)`, and the range it reports
    // back is an index into that shorter run, not into the table's columns.
    int nScroll = nCols - nFixed;
    // A pane that has not been laid out yet has no width, and answers an
    // empty range rather than every column.
    float w = s->bodyBounds.w;
    if (w <= 0 || nScroll <= 0) {
        *first = 0;
        *end = 0;
        return;
    }
    float lo = s->scrollX, hi = s->scrollX + w;
    float x = 0;
    int lead = -1, last = nScroll;
    for (int i = 0; i < nScroll; i++) {
        int c = TableColAt(s, nFixed + i);
        float cw = c < s->colWidth.len ? s->colWidth[c] : 0;
        x += cw;
        if (lead < 0 && x > lo) {
            lead = i;
        }
        if (x > hi) {
            // virtual_list.rs stops at the first item past the right edge and
            // then takes one more, so the column sliding in is already built.
            last = i + 2;
            break;
        }
    }
    *first = lead < 0 ? 0 : lead;
    *end = last < nScroll ? last : nScroll;
}

int TableExtraRowsNeeded(float totalH, float actualH, float rowH) {
    float remaining = totalH - actualH;
    if (remaining <= 0 || rowH <= 0) {
        return 0;
    }
    return (int)floorf(remaining / rowH);
}

bool TableShouldLoadMore(const TableState* s, int visibleEnd) {
    if (!s->hasMore || s->loading) {
        return false;
    }
    return s->rowCount - visibleEnd <= s->loadMoreThreshold;
}

void TableScrollToRow(TableState* s, int row, ScrollStrategy strategy) {
    if (s->viewportH <= 0) {
        return;
    }
    s->scrollY = VirtualListScrollToRow(s->rowCount, s->rowH, row, s->scrollY,
                                        s->viewportH, strategy);
}

void TableScrollToCol(TableState* s, int col, ScrollStrategy strategy) {
    float viewport = s->bodyBounds.w;
    if (viewport <= 0) {
        return;
    }
    int d = TableDisplayOfCol(s, col);
    if (d < 0) {
        return;
    }
    // saturating_sub: a pinned column is already on screen, and asking for
    // one asks for the start of what moves.
    d -= s->fixedCols;
    if (d < 0) {
        d = 0;
    }
    int n = s->colCount - s->fixedCols;
    if (n <= 0) {
        return;
    }
    // The widths of the columns that move, in the order they are shown in —
    // which the display order is and the caller's array is not.
    Vec<float> w;
    for (int i = s->fixedCols; i < s->colCount; i++) {
        int c = TableColAt(s, i);
        VecAppend(w, c < s->colWidth.len ? s->colWidth[c] : 0);
    }
    s->scrollX =
        VirtualListScrollToItem(w.els, n, d, s->scrollX, viewport, strategy);
    VecReset(w);
}

void TableRefreshCols(TableState* s) {
    // A zero width is a column that has not been seeded, which is what makes
    // the next build take the caller's declaration again.
    for (int i = 0; i < s->colWidth.len; i++) {
        s->colWidth[i] = 0;
    }
    s->colOrderSeeded = false;
    s->resizingCol = -1;
    s->draggingCol = -1;
    s->dropGap = -1;
}

void TableState::OnResizeDrag(TableState* self, Ctx* cx,
                              const DragMoveEvent* ev) {
    // `match e.drag(cx) { ResizeColumn(..) }`: a drag carrying anything else
    // is not this handler's.
    if (!base::StrEq(ev->drag.kind, kTableResizeDrag)) {
        return;
    }
    int col = ev->drag.ix;
    if (col < 0 || col >= self->colCount) {
        return;
    }
    TableEnsureCols(self, col + 1);
    self->resizingCol = col;
    // col_group.bounds.left(). Each boundary is covered by two bands: the
    // trailing one ends at the column's right edge, and the leading one — in
    // the next column's head, marked by its payload data — starts there. So
    // where the column starts is that edge less the width it has now, and
    // the width it has now is what laid the band out where it is.
    float boundary = ev->drag.data ? ev->el.x : ev->el.Right();
    float left = boundary - self->colWidth[col];
    TableResizeCol(self, cx, col, ev->event.x - kTableResizeHandleW - left);
}

void TableState::OnResizeEnd(TableState* self, Ctx* cx, const MouseUpEvent*) {
    if (self->resizingCol < 0) {
        return;
    }
    self->resizingCol = -1;
    TableEvent ev = {TableEventKind::ColumnWidthsChanged,
                     -1,
                     -1,
                     ColumnSort::Default,
                     self->colWidth.els,
                     self->colCount};
    if (self->onEvent.IsValid()) {
        ListenerCall(cx->app, cx->win, self->onEvent, &ev);
    }
    Notify(cx);
}

void TableMoveColumnEvent(TableState* s, Ctx* cx, int from, int to) {
    if (!TableMoveColumn(s, from, to)) {
        return;
    }
    TableEvent ev = {TableEventKind::MoveColumn, -1, from};
    ev.row = to;
    if (s->onEvent.IsValid()) {
        ListenerCall(cx->app, cx->win, s->onEvent, &ev);
    }
    if (s->delegateMoveColumn) {
        s->delegateMoveColumn(cx, s->delegateData, from, to);
    }
    Notify(cx);
}

void TableState::OnColDragMove(TableState* self, Ctx* cx,
                               const DragMoveEvent* ev) {
    if (!base::StrEq(ev->drag.kind, kTableColDrag) || !self->colMovable) {
        return;
    }
    TableEnsureCols(self, self->colCount);
    int n = self->colCount;
    int gap = TableDragGapAt(self->colBounds.els, n, ev->event.x, ev->drag.ix,
                             self->fixedCols);
    if (self->draggingCol == ev->drag.ix && self->dropGap == gap) {
        return;
    }
    self->draggingCol = ev->drag.ix;
    self->dropGap = gap;
    Notify(cx);
}

void TableState::OnColDrop(TableState* self, Ctx* cx, const DropEvent* ev) {
    if (!base::StrEq(ev->drag.kind, kTableColDrag) || !self->colMovable) {
        return;
    }
    TableEnsureCols(self, self->colCount);
    int n = self->colCount;
    int gap = TableDragGapAt(self->colBounds.els, n, ev->x, ev->drag.ix,
                             self->fixedCols);
    self->draggingCol = -1;
    self->dropGap = -1;
    if (gap >= 0) {
        TableMoveColumnEvent(self, cx, ev->drag.ix, gap);
    } else {
        Notify(cx);
    }
}

void TableState::OnColDragEnd(TableState* self, Ctx* cx, const MouseUpEvent*) {
    if (self->draggingCol < 0) {
        return;
    }
    self->draggingCol = -1;
    self->dropGap = -1;
    Notify(cx);
}

void TableState::OnScroll(TableState* self, Ctx* cx, const ScrollEvent* ev) {
    self->scrollY = ev->offsetY;
    Notify(cx);
}

void TableState::OnScrollXY(TableState* self, Ctx* cx, const ScrollEvent* ev) {
    self->scrollY = ev->offsetY;
    self->scrollX = ev->offsetX;
    Notify(cx);
}

void TableOnAction(TableState* self, Ctx* cx, const ActionEvent* ev) {
    if (!self) {
        return;
    }
    TableAction act = TableActionOf(ev->action);
    if (act == TableAction::None) {
        const_cast<ActionEvent*>(ev)->propagate = true;
        return;
    }
    TablePerform(self, cx, act);
}

void TableBindKeys(Ctx* cx, El* root, Entity<TableState> state) {
    if (!cx || !root || !state.IsValid()) {
        return;
    }
    TableInitKeys();
    Listener onAction = ListenTo(state, &TableOnAction);
    root->KeyContext(TableContext())
        ->OnAction(action::Cancel(), onAction)
        ->OnAction(action::SelectUp(), onAction)
        ->OnAction(action::SelectDown(), onAction)
        ->OnAction(action::SelectPrevColumn(), onAction)
        ->OnAction(action::SelectNextColumn(), onAction)
        ->OnAction(action::SelectFirst(), onAction)
        ->OnAction(action::SelectLast(), onAction)
        ->OnAction(action::SelectPageUp(), onAction)
        ->OnAction(action::SelectPageDown(), onAction);
}

} // namespace gpui
