#ifndef GPUI_BASE_DATA_TABLE_H_
#define GPUI_BASE_DATA_TABLE_H_
/* Unstyled data table — crates/ui/src/table/state.rs + table/data_table.rs */

#include "gpui/gpui.h"
#include "base/virtual_list.h"

namespace gpui {

// SelectionMode: what the last selection picked out.
enum class TableSelectionMode : uint8_t {
    None,
    Row,
    Column,
    Cell
};

// ColumnSort. A column with no sort of its own is Default, which is also the
// state the cycle passes back through.
enum class ColumnSort : uint8_t {
    Default,
    Ascending,
    Descending
};

// What a keystroke asks a table to do. data_table.rs binds escape, the four
// arrows, home, end, pageup, pagedown and tab in the table's key context.
enum class TableAction : uint8_t {
    None,
    SelectPrev,
    SelectNext,
    SelectPrevColumn,
    SelectNextColumn,
    SelectFirst,
    SelectLast,
    SelectPageUp,
    SelectPageDown,
    Cancel
};

// data_table.rs::init: escape, the four arrows, home, end, the page keys and
// tab / shift-tab in the "DataTable" key context. Tab is the table's there,
// and it is the table's here too — the window's focus ring only takes a tab
// nothing else wanted.
void TableInitKeys();
Str TableContext();
TableAction TableActionOf(uint32_t id);

// TableEvent, what the table tells whoever is listening.
enum class TableEventKind : uint8_t {
    SelectRow,
    SelectCol,
    SelectCell,
    DoubleClickedRow,
    DoubleClickedCell,
    // RightClickedRow(Option<usize>) / RightClickedCell(usize, usize): what a
    // context menu hangs off. The row one carries -1 for Rust's None, which
    // is what a keyboard selection sends to say the right-clicked row is no
    // longer the one under the pointer.
    RightClickedRow,
    RightClickedCell,
    Sort,
    ColumnWidthsChanged,
    // MoveColumn(from, to): a column head was dragged into another place.
    MoveColumn,
    Cancel
};

struct TableEvent {
    TableEventKind kind = TableEventKind::SelectRow;
    int row = -1;
    int col = -1;
    // What the column was sorted into, for a Sort.
    ColumnSort sort = ColumnSort::Default;
    // ColumnWidthsChanged(Vec<Pixels>): every column's width, once the drag
    // that changed one of them has ended. The array is the table's own, so a
    // listener reads it and does not keep it.
    const float* widths = nullptr;
    int nWidths = 0;
};

// The name a column-resize drag goes by, which is the `ResizeColumn` payload
// type in Rust.
extern const Str kTableResizeDrag;

// What a press on a column head picks up, which is the `DragColumn` payload
// in Rust.
extern const Str kTableColDrag;

// HANDLE_SIZE: how far behind the pointer a resize drag trails the column
// edge.
const float kTableResizeHandleW = 2;
// HANDLE_PADDING: grab room on each side of a resize handle's hairline, so
// the divider can be caught without pixel-hunting. Both halves of the band
// are this wide, so widening the grab area stays symmetric about the
// boundary. `resizable`'s handle pads a hairline the same way, and by the
// same amount.
const float kTableResizeHandlePadding = 4;

// TableVisibleRange: which rows and which columns the table last built, as
// two half-open ranges. Rust keeps the pair on the state and hands each half
// to the delegate when it moves.
struct TableVisibleRange {
    int rowFirst = 0;
    int rowEnd = 0;
    int colFirst = 0;
    int colEnd = 0;
};

// What a table is between frames: the selection, what is sorted by what, and
// the flags that say which of those a user is allowed to move. The rows and
// columns themselves stay with the delegate, which is the caller here.
struct TableState {
    int rowCount = 0;
    int colCount = 0;
    TableSelectionMode mode = TableSelectionMode::None;
    int selectedRow = -1;
    int selectedCol = -1;
    int selectedCellRow = -1;
    int selectedCellCol = -1;
    // right_clicked_row: the row under a secondary press, cleared by the next
    // selection.
    int rightClickedRow = -1;
    // right_clicked_cell. The two are exclusive: the cell one clears the row
    // and the row one clears the cell.
    int rightClickedCellRow = -1;
    int rightClickedCellCol = -1;
    bool rowSelectable = true;
    bool colSelectable = true;
    bool cellSelectable = false;
    // row_header: whether the table has a column of its own for picking rows.
    // Without one, clicking an already-selected cell takes the whole row.
    // Only drawn where the table is cell-selectable; on by default, as Rust's
    // is.
    bool rowHeader = true;
    // loop_selection: whether the ends of the table wrap.
    bool loopSelection = false;
    bool sortable = true;
    // Which column carries the sort, and which way. Rust hangs the sort off
    // each column and resets the others; one pair says the same thing, since
    // only one column is ever sorted.
    int sortCol = -1;
    ColumnSort sort = ColumnSort::Default;
    // How far PageUp and PageDown move.
    int pageRows = 10;
    // col_resizable: whether a column edge can be dragged at all. Whether a
    // particular one can is the column's own business, and shows up as the
    // table putting a handle on it or not — which is also how Rust's
    // render_resize_handle reads it.
    bool colResizable = true;
    // ColGroup::width, seeded from what the caller declared for a column and
    // the table's own from then on. A zero is a column that has not been
    // seeded yet.
    Vec<float> colWidth;
    // Column::min_width / max_width, one pair per logical column. The scalar
    // pair remains the fallback for unseeded/base-only tables.
    Vec<float> colMinWidths;
    Vec<float> colMaxWidths;
    float colMinWidth = 20;
    float colMaxWidth = 0;
    // resizing_col: which edge is being dragged right now, and the flag that
    // decides whether a release has anything to report.
    int resizingCol = -1;
    // col_groups, as the order the columns are shown in: `colOrder[i]` is the
    // caller's column at display position i. Rust reorders the col_groups
    // vector itself; the columns are the caller's array here, so the order is
    // the table's own list of indices into it.
    Vec<int> colOrder;
    bool colOrderSeeded = false;
    // col_movable, and the drag in flight: which column was picked up and
    // which gap it would drop into, or -1 for neither.
    bool colMovable = true;
    int draggingCol = -1;
    int dropGap = -1;
    // Where each head was last painted, which is what a drop position is
    // worked out against — Rust reads the same bounds off its col_groups.
    Vec<Bounds> colBounds;

    // The rows are virtualized, and uniform_list wants them all one height.
    // `viewportH` is what the body was last laid out at.
    float rowH = 32;
    float scrollY = 0;
    float viewportH = 0;
    // How far the un-fixed columns are slid to the left, and the width they
    // were last laid out in. A table wide enough to need it scrolls sideways
    // under a head that goes with it.
    float scrollX = 0;
    float viewportW = 0;
    // fixed_left_cols_count(): how many columns are pinned to the left right
    // now, which the themed table works out from the columns and writes here
    // as it builds. It has to be on the state because a scroll is a click,
    // and a click has no columns to count.
    int fixedCols = 0;
    // col_fixed: whether the columns that asked to be pinned actually are.
    // Rust keeps the flag on the state rather than the column so one toggle
    // releases all of them at once.
    bool colFixed = true;
    // Where the scrolling pane was last painted, which is the width a column
    // is on-screen or not against. It is a frame behind, the way any bounds
    // read at build time is; the range-of-one rule below covers the first
    // frame, where it is still zero.
    Bounds bodyBounds = {};
    // visible_range: what was last built, and what the delegate was last
    // told about.
    TableVisibleRange visibleRange = {};
    // delegate.loading() / has_more() / load_more_threshold().
    bool loading = false;
    bool hasMore = false;
    int loadMoreThreshold = 20;
    Listener onEvent = {};
    // cx.emit needs to know who is emitting, and Rust's Context<Self> does.
    // The element stamps this as it builds, so a state can send an event to
    // its subscribers without the caller carrying its handle around.
    Entity<TableState> self = {};
    // The table's own focus handle — `self.state.focus_handle(cx)`, which
    // data_table.rs tracks on the element that declares the key context. Asked
    // for once and kept, because the state is what outlives the frame the
    // element is built in; the port derived it from the caller's id instead.
    FocusHandle focus = {};

    ~TableState() {
        VecReset(colWidth);
        VecReset(colMinWidths);
        VecReset(colMaxWidths);
        VecReset(colOrder);
        VecReset(colBounds);
    }
    Listener onLoadMore = {};
    // UI TableDelegate hooks stored in base-shaped signatures so retained
    // interaction state can call them after the frame builder is gone.
    void* delegateData = nullptr;
    void (*delegateSort)(Ctx* cx, void* data, int col,
                         ColumnSort sort) = nullptr;
    void (*delegateMoveColumn)(Ctx* cx, void* data, int from, int to) = nullptr;
    void (*delegateLoadMore)(Ctx* cx, void* data) = nullptr;

    static void OnRowClick(TableState* self, Ctx* cx, const ClickEvent* ev,
                           int64_t row);
    // A click on one cell, when the table is cell-selectable: `packed` is the
    // row and the column together, the way every listener that carries two
    // numbers does here. A second click on the same cell is
    // `DoubleClickedCell`, which `state.rs` emits beside the row's.
    static void OnCellClick(TableState* self, Ctx* cx, const ClickEvent* ev,
                            int64_t packed);
    static void OnRowMouseDown(TableState* self, Ctx* cx,
                               const MouseDownEvent* ev, int64_t row);
    // on_cell_right_click, when the table is cell-selectable. It stops the
    // press, so the row under it does not also mark itself.
    static void OnCellMouseDown(TableState* self, Ctx* cx,
                                const MouseDownEvent* ev, int64_t packed);
    static void OnHeadClick(TableState* self, Ctx* cx, const ClickEvent* ev,
                            int64_t col);
    static void OnSortClick(TableState* self, Ctx* cx, const ClickEvent* ev,
                            int64_t col);
    // Which column the drag moves is the payload's, the way Rust reads it out
    // of the ResizeColumn it matched on.
    static void OnResizeDrag(TableState* self, Ctx* cx,
                             const DragMoveEvent* ev);
    static void OnResizeEnd(TableState* self, Ctx* cx, const MouseUpEvent* ev);
    static void OnColDragMove(TableState* self, Ctx* cx,
                              const DragMoveEvent* ev);
    static void OnColDrop(TableState* self, Ctx* cx, const DropEvent* ev);
    static void OnColDragEnd(TableState* self, Ctx* cx, const MouseUpEvent* ev);
    static void OnScroll(TableState* self, Ctx* cx, const ScrollEvent* ev);
    // The scrolling pane moves both ways; the pinned one only moves down, so
    // it keeps the plain handler and never writes the sideways offset back.
    static void OnScrollXY(TableState* self, Ctx* cx, const ScrollEvent* ev);
};

// The order the columns are shown in. `TableColAt(s, i)` is the caller's
// column at display position i, which is what every render and every hit test
// goes through.
void TableSeedColOrder(TableState* s, int colCount);
int TableColAt(const TableState* s, int display);
// Where the caller's column `col` is being shown, or -1.
int TableDisplayOfCol(const TableState* s, int col);
// move_column: take the column at `from` out and put it back at `to`, where
// `to` is a display position. Answers false when there was nothing to move.
bool TableMoveColumn(TableState* s, int from, int to);
void TableMoveColumnEvent(TableState* s, Ctx* cx, int from, int to);
// drag_gap_at: the gap a head dropped at `x` would go into — the one after
// the last column whose centre is left of `x` — or -1 when dropping there
// would put the column back where it already is. A column can only be
// reordered within its own region: rendering pins the first `fixedCount`
// columns, so a cross-region move would change which columns are pinned
// without updating their `fixed` flags. The pointer is in the fixed region
// when it is left of the last fixed column's right edge.
int TableDragGapAt(const Bounds* colBounds, int n, float x, int dragCol,
                   int fixedCount = 0);
// The three per-column arrays grown to hold `n` columns. Rust's col_groups is
// one Vec of structs; these are three, since a column's width, its place in
// the order and where its head was painted are written at different moments.
void TableEnsureCols(TableState* s, int n);

// A row and a column as one number, which is what a cell's listener carries.
// Twelve bits of column and the rest of the word for the row: every row a
// 64-bit target can index, and half a million on a 32-bit one, which is more
// than a wasm page has the memory to hold anyway.
inline int64_t TableCellPack(int row, int col) {
    return ((int64_t)row << 12) | (int64_t)(col & 0xfff);
}
inline int TableCellRow(int64_t packed) {
    return (int)(packed >> 12);
}
inline int TableCellCol(int64_t packed) {
    return (int)(packed & 0xfff);
}
// update_visible_range_if_need, one axis at a time: the range is written
// down and true comes back when it moved, which is when the delegate wants
// telling. A range of one is skipped when there is more than one item —
// the virtual list measures with a single item. The end never passes
// `items`. An empty range while items remain is stale and is not written.
// `0..0` with no items is a real report.
bool TableVisibleRowsChanged(TableState* s, int first, int end, int items);
bool TableVisibleColsChanged(TableState* s, int first, int end, int items);
// The columns whose slot overlaps the scrolling pane, in display positions.
// Rust culls the ones outside it; this tree builds them all, so this answers
// the same question without being what decides anything.
void TableVisibleCols(const TableState* s, int* first, int* end);

// calculate_extra_rows_needed: how many empty rows a striped table adds under
// its last one so the stripes run to the bottom of the body — the whole rows
// that fit in what the data rows leave over, and none when they overflow it.
int TableExtraRowsNeeded(float totalH, float actualH, float rowH);

// load_more_if_need: the last row built is within the threshold of the end,
// and the delegate says there is more.
bool TableShouldLoadMore(const TableState* s, int visibleEnd);
// scroll_to_row, against the height the body was last laid out at.
void TableScrollToRow(TableState* s, int row, ScrollStrategy strategy);
// scroll_to_col, against the width the scrolling pane was last laid out at.
// The column is the caller's, and what moves is its place in the display
// order less the pinned ones — Rust's `col_ix.saturating_sub(fixed_left_
// cols_count())`, since the pinned columns do not move under the offset.
void TableScrollToCol(TableState* s, int col, ScrollStrategy strategy);

// TableState::refresh, which is `prepare_col_groups`: the widths and the
// order the table has of its own are dropped, so the caller's declarations
// are taken again on the next build. Rust rebuilds `col_groups` from the
// delegate, which loses a dragged width and a moved column the same way.
//
// `refresh_header_layout` has no counterpart. Rust caches the header cells
// and that call is what invalidates the cache; here the group bands are
// summed from the current widths in the current order every time the table
// is built, so there is nothing to invalidate.
void TableRefreshCols(TableState* s);

// The width a column is being drawn at, which is the caller's until the table
// has one of its own.
float TableColWidth(const TableState* s, int col, float declared);

// The width a column keeps, seeded once from what the caller declared.
void TableSeedColWidth(TableState* s, int col, float declared);

// size.clamp(min_width, max_width), which is what decides how far a drag on a
// column edge is allowed to get.
float TableClampColWidth(const TableState* s, float width);
float TableClampColWidth(const TableState* s, int col, float width);
void TableSetColConstraints(TableState* s, int col, float minWidth,
                            float maxWidth);

// resize_cols: the column takes the new width, clamped, and the table is only
// notified if that changed anything.
void TableResizeCol(TableState* s, Ctx* cx, int col, float width);

// perform_sort's cycle: Default becomes Descending, Descending becomes
// Ascending, and Ascending goes back to Default.
ColumnSort TableNextSort(ColumnSort s);

// The sort a column is showing, given which column carries it.
ColumnSort TableSortOf(const TableState* s, int col);

void TablePerformSort(TableState* s, Ctx* cx, int col);

// TableSelection: the current selection as one value, mirroring the
// SelectRow / SelectCol / SelectCell events. `row` and `col` are what the
// kind carries and -1 where it carries none.
enum class TableSelectionKind : uint8_t {
    None,
    Row,
    Column,
    Cell
};

struct TableSelection {
    TableSelectionKind kind = TableSelectionKind::None;
    int row = -1;
    int col = -1;
};

inline bool operator==(const TableSelection& a, const TableSelection& b) {
    return a.kind == b.kind && a.row == b.row && a.col == b.col;
}
inline bool operator!=(const TableSelection& a, const TableSelection& b) {
    return !(a == b);
}

// selection(): match on it instead of combining the three getters below.
TableSelection TableSelectionOf(const TableState* s);
// set_selection: the matching TableSetSelected* or TableClearSelection, so
// scrolling and events are theirs.
void TableSetSelection(TableState* s, Ctx* cx, TableSelection selection);
// selected_row / selected_col / selected_cell: each answers only when its own
// kind of thing is selected — a selected cell is not a selected row. The
// cached positions behind them stay for keyboard navigation across modes.
// -1 / false is None.
int TableSelectedRow(const TableState* s);
int TableSelectedCol(const TableState* s);
bool TableSelectedCell(const TableState* s, int* row, int* col);

void TableSetSelectedRow(TableState* s, Ctx* cx, int row);
void TableSetSelectedCol(TableState* s, Ctx* cx, int col);
void TableSetSelectedCell(TableState* s, Ctx* cx, int row, int col);
void TableClearSelection(TableState* s, Ctx* cx);
// The escalation `on_cell_click` opens with: with no row header column there
// is nothing else to pick a whole row with, so a single click on the cell
// that is already selected takes the row instead. A double click is passed
// through to DoubleClickedCell and never escalates.
bool TableEscalatesToRow(const TableState* s, int row, int col,
                         bool doubleClick);

// The action, applied.
void TablePerform(TableState* s, Ctx* cx, TableAction act);

void TableOnAction(TableState* self, Ctx* cx, const ActionEvent* ev);
void TableBindKeys(Ctx* cx, El* root, Entity<TableState> state);

template <>
struct EventEmitter<TableState, TableEvent> {};

} // namespace gpui
#endif // GPUI_BASE_DATA_TABLE_H_
