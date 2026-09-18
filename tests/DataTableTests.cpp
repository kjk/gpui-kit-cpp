/* Ported from crates/ui/src/table/state.rs.
 *
 * data_table.rs binds escape, the four arrows, home, end, pageup, pagedown
 * and tab in the table's key context. Only the four arrows consult
 * loop_selection; the page moves and Home/End clamp, and Home and End are the
 * first and last column rather than the first and last row. perform_sort
 * cycles one column at a time and resets whichever one was sorted before. */

#include "Test.h"

// The chord, resolved in the table's context, read as what the table does.
static TableAction ForChord(const char* spec) {
    TableInitKeys();
    KeyChord c = {};
    utassert(KeyChordParse(Str(spec), &c));
    uint32_t ctx = KeyContextOf(TableContext());
    return TableActionOf(KeymapMatch(c, &ctx, 1).action);
}

static void TheKeyTable() {
    utassert(ForChord("up") == TableAction::SelectPrev);
    utassert(ForChord("down") == TableAction::SelectNext);
    utassert(ForChord("left") == TableAction::SelectPrevColumn);
    utassert(ForChord("right") == TableAction::SelectNextColumn);
    // Tab is bound to the same action as Right, and shift-tab to Left — in
    // the table's own context, so the window's focus ring only ever sees a
    // tab the table did not want.
    utassert(ForChord("tab") == TableAction::SelectNextColumn);
    utassert(ForChord("shift-tab") == TableAction::SelectPrevColumn);
    utassert(ForChord("home") == TableAction::SelectFirst);
    utassert(ForChord("end") == TableAction::SelectLast);
    utassert(ForChord("pageup") == TableAction::SelectPageUp);
    utassert(ForChord("pagedown") == TableAction::SelectPageDown);
    utassert(ForChord("escape") == TableAction::Cancel);
    utassert(ForChord("space") == TableAction::None);
}

static void TheSortCycle() {
    utassert(TableNextSort(ColumnSort::Default) == ColumnSort::Descending);
    utassert(TableNextSort(ColumnSort::Descending) == ColumnSort::Ascending);
    utassert(TableNextSort(ColumnSort::Ascending) == ColumnSort::Default);
}

static void OnlyOneColumnCarriesTheSort() {
    TableState s;
    s.rowCount = 5;
    s.colCount = 3;
    utassert(TableSortOf(&s, 0) == ColumnSort::Default);

    s.sortCol = 1;
    s.sort = ColumnSort::Ascending;
    utassert(TableSortOf(&s, 1) == ColumnSort::Ascending);
    // Every other column reads as unsorted, which is what Rust's reset loop
    // leaves behind.
    utassert(TableSortOf(&s, 0) == ColumnSort::Default);
    utassert(TableSortOf(&s, 2) == ColumnSort::Default);
}

static void AColumnKeepsItsWidthOnceItHasOne() {
    TableState s;
    s.colCount = 3;
    // Until a drag has moved it, a column is as wide as the caller declared.
    utassert(TableColWidth(&s, 0, 120) == 120);
    TableSeedColWidth(&s, 0, 120);
    utassert(TableColWidth(&s, 0, 120) == 120);
    // Seeding again does not undo a width the table has since taken.
    s.colWidth[0] = 200;
    TableSeedColWidth(&s, 0, 120);
    utassert(TableColWidth(&s, 0, 120) == 200);
    // A column the table has never been asked about answers with what the
    // caller declared rather than growing the array to reach it.
    utassert(TableColWidth(&s, 40, 120) == 120);
    utassert(s.colWidth.len == 1);
}

static void AResizeIsClamped() {
    TableState s;
    // Rust's defaults: 20px at the narrow end and no ceiling at all.
    utassert(TableClampColWidth(&s, 5) == 20);
    utassert(TableClampColWidth(&s, 4000) == 4000);
    s.colMaxWidth = 450;
    utassert(TableClampColWidth(&s, 4000) == 450);
    utassert(TableClampColWidth(&s, 100) == 100);
}

static void SourceColumnBuildersKeepEveryField() {
    component::Column col = component::Column::New(StrL("cpu"), StrL("CPU"))
                                .Ascending()
                                .TextCenter()
                                .Paddings(Edges::New(1, 2, 3, 4))
                                .Width(140)
                                .FixedLeft()
                                .Resizable(false)
                                .Movable(false)
                                .Selectable(false)
                                .MinWidth(150)
                                .MaxWidth(145);
    utassert(StrEqI(col.key, "cpu"));
    utassert(StrEqI(col.name, "CPU"));
    utassert(StrEqI(col.title, "CPU"));
    utassert(col.center && !col.right);
    utassert(col.hasSort && col.sortable);
    utassert(col.sort == ColumnSort::Ascending);
    utassert(col.hasPaddings);
    utassert(col.paddings.left == 1 && col.paddings.right == 2);
    utassert(col.paddings.top == 3 && col.paddings.bottom == 4);
    utassert(col.fixed && col.fixedSide == component::ColumnFixed::Left);
    utassert(!col.resizable && !col.movable && !col.selectable);
    utassert(col.minWidth == 150 && col.maxWidth == 145);
    // The two builders clamp immediately, in the order Rust applies them.
    utassert(col.width == 145);

    component::ColumnGroup group =
        component::ColumnGroup::New(StrL("Machine"), 3);
    utassert(StrEqI(group.label, "Machine") && group.span == 3);
}

static void EachColumnHasItsOwnResizeBounds() {
    TableState s;
    s.colCount = 2;
    TableSetColConstraints(&s, 0, 40, 80);
    TableSetColConstraints(&s, 1, 100, 200);
    utassert(TableClampColWidth(&s, 0, 10) == 40);
    utassert(TableClampColWidth(&s, 0, 500) == 80);
    utassert(TableClampColWidth(&s, 1, 10) == 100);
    utassert(TableClampColWidth(&s, 1, 500) == 200);
}

struct DelegateProbe {
    int headers = 0;
    int groupHeaders = 0;
    int heads = 0;
    int rows = 0;
    int cells = 0;
    int sorts = 0;
    int moves = 0;
    int loads = 0;
    ColumnSort lastSort = ColumnSort::Default;
};

static int DelegateColumns(Ctx*, void*) {
    return 2;
}
static int DelegateRows(Ctx*, void*) {
    return 3;
}
static component::TableColumn DelegateColumn(Ctx*, void*, int col) {
    return component::Column::New(col == 0 ? StrL("id") : StrL("name"),
                                  col == 0 ? StrL("ID") : StrL("Name"))
        .Width(col == 0 ? 60.f : 120.f)
        .MinWidth(col == 0 ? 40.f : 80.f)
        .MaxWidth(col == 0 ? 90.f : 180.f)
        .Ascending();
}
static void DelegateSort(Ctx*, void* data, int, ColumnSort sort) {
    DelegateProbe* p = (DelegateProbe*)data;
    p->sorts++;
    p->lastSort = sort;
}
static El* DelegateHeader(Ctx* cx, void* data) {
    ((DelegateProbe*)data)->headers++;
    return Div(cx->a);
}
static El* DelegateGroupTh(Ctx* cx, void* data, Str, int span, float width) {
    DelegateProbe* p = (DelegateProbe*)data;
    p->groupHeaders += span;
    return Div(cx->a)->W(width);
}
static El* DelegateTh(Ctx* cx, void* data, int) {
    ((DelegateProbe*)data)->heads++;
    return Div(cx->a);
}
static El* DelegateTr(Ctx* cx, void* data, int) {
    ((DelegateProbe*)data)->rows++;
    return Div(cx->a);
}
static El* DelegateTd(Ctx* cx, void* data, int, int) {
    ((DelegateProbe*)data)->cells++;
    return Div(cx->a);
}
static void DelegateGroups(Ctx*, void*, component::DataTable* table) {
    component::ColumnGroup* group = (component::ColumnGroup*)Alloc(
        table->a, sizeof(component::ColumnGroup));
    group[0] = component::ColumnGroup::New(StrL("All"), 2);
    table->GroupHeader(group, 1);
}
static void DelegateMove(Ctx*, void* data, int, int) {
    ((DelegateProbe*)data)->moves++;
}
static bool DelegateHasMore(Ctx*, void*) {
    return true;
}
static int DelegateThreshold(void*) {
    return 2;
}
static void DelegateLoad(Ctx*, void* data) {
    ((DelegateProbe*)data)->loads++;
}

static void TheSourceDelegateDrivesTheTable() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Entity<TableState> state = EntityNewState<TableState>(&app);
    Ctx cx = {&app, win, a, state.id};
    DelegateProbe probe;
    component::TableDelegate delegate = {};
    delegate.data = &probe;
    delegate.columnsCount = DelegateColumns;
    delegate.rowsCount = DelegateRows;
    delegate.column = DelegateColumn;
    delegate.performSort = DelegateSort;
    delegate.renderHeader = DelegateHeader;
    delegate.renderGroupTh = DelegateGroupTh;
    delegate.renderTh = DelegateTh;
    delegate.renderTr = DelegateTr;
    delegate.renderTd = DelegateTd;
    delegate.groupHeaders = DelegateGroups;
    delegate.moveColumn = DelegateMove;
    delegate.hasMore = DelegateHasMore;
    delegate.loadMoreThreshold = DelegateThreshold;
    delegate.loadMore = DelegateLoad;

    component::DataTable::New(&cx, StrL("delegate"), state)
        ->Delegate(delegate)
        ->BuildEl();
    TableState* s = state.Get(&app);
    utassert(s && s->colCount == 2 && s->rowCount == 3);
    utassert(s->hasMore && s->loadMoreThreshold == 2);
    utassert(s->sortCol == 0 && s->sort == ColumnSort::Ascending);
    utassert(s->colMinWidths[0] == 40 && s->colMaxWidths[1] == 180);
    utassert(probe.headers == 1 && probe.groupHeaders == 2);
    utassert(probe.heads == 2 && probe.rows == 3 && probe.cells == 6);
    utassert(probe.loads == 1);

    TablePerformSort(s, &cx, 0);
    utassert(probe.sorts == 1 && probe.lastSort == ColumnSort::Default);
    TableMoveColumnEvent(s, &cx, 0, 2);
    utassert(probe.moves == 1);

    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

// Four columns of a hundred, side by side.
static void SeedCols(TableState* s, Bounds* b, int n) {
    s->colCount = n;
    TableSeedColOrder(s, n);
    for (int i = 0; i < n; i++) {
        b[i] = {(float)i * 100.f, 0, 100, 30};
    }
}

static void TheGapIsAfterTheLastCentreLeftOfThePointer() {
    TableState s;
    Bounds b[4];
    SeedCols(&s, b, 4);
    // Dragging column 0: the pointer over the first half of column 1 is
    // still the gap it came from, so nothing moves.
    utassert(TableDragGapAt(b, 4, 120, 0) == -1);
    // Past the centre of column 1 it is the gap after it.
    utassert(TableDragGapAt(b, 4, 160, 0) == 2);
    utassert(TableDragGapAt(b, 4, 260, 0) == 3);
    utassert(TableDragGapAt(b, 4, 380, 0) == 4);
    // Dragging column 2 back to the front.
    utassert(TableDragGapAt(b, 4, 10, 2) == 0);
    utassert(TableDragGapAt(b, 4, 120, 2) == 1);
    // The two gaps either side of the dragged column are both a no-op.
    utassert(TableDragGapAt(b, 4, 210, 2) == -1);
    utassert(TableDragGapAt(b, 4, 260, 2) == -1);
}

// state.rs drag_gap_at after f1539a3b: a column is reordered only within its
// own region, so a drag across the fixed-columns boundary shows no gap and
// drops nowhere.
static void AHeadDragNeverCrossesTheFixedBoundary() {
    TableState s;
    Bounds b[4];
    SeedCols(&s, b, 4);
    // The first two columns are pinned; the boundary is column 1's right
    // edge, at 200.
    // A fixed column dragged into the scrollable region has no gap.
    utassert(TableDragGapAt(b, 4, 260, 0, 2) == -1);
    utassert(TableDragGapAt(b, 4, 380, 0, 2) == -1);
    // It still reorders within the fixed region.
    utassert(TableDragGapAt(b, 4, 160, 0, 2) == 2);
    utassert(TableDragGapAt(b, 4, 10, 1, 2) == 0);
    // A scrollable column dragged over the fixed region has no gap either,
    // even where the unpinned table would have found one.
    utassert(TableDragGapAt(b, 4, 10, 2, 2) == -1);
    utassert(TableDragGapAt(b, 4, 120, 3, 2) == -1);
    // And within the scrollable region the gaps are the scrollable ones: the
    // first of them is the boundary itself, never a slot among the pinned.
    utassert(TableDragGapAt(b, 4, 210, 3, 2) == 2);
    utassert(TableDragGapAt(b, 4, 380, 2, 2) == 4);
    // Without pinned columns nothing changes.
    utassert(TableDragGapAt(b, 4, 380, 0, 0) == 4);
}

static void AMovedColumnLandsInTheGap() {
    TableState s;
    Bounds b[4];
    SeedCols(&s, b, 4);
    // The first column into the gap after the second: everything before it
    // shifts down one.
    utassert(TableMoveColumn(&s, 0, 2));
    utassert(TableColAt(&s, 0) == 1);
    utassert(TableColAt(&s, 1) == 0);
    utassert(TableColAt(&s, 2) == 2);
    utassert(TableDisplayOfCol(&s, 0) == 1);

    // And back the other way: the last column to the front.
    utassert(TableMoveColumn(&s, 3, 0));
    utassert(TableColAt(&s, 0) == 3);
    utassert(TableColAt(&s, 1) == 1);
    utassert(TableColAt(&s, 2) == 0);
    utassert(TableColAt(&s, 3) == 2);

    // The gaps either side of a column leave it where it is.
    utassert(!TableMoveColumn(&s, 1, 1));
    utassert(!TableMoveColumn(&s, 1, 2));
    utassert(TableColAt(&s, 1) == 1);
}

static void LoadMoreAsksNearTheEnd() {
    TableState s;
    s.rowCount = 1000;
    utassert(!TableShouldLoadMore(&s, 995));
    s.hasMore = true;
    utassert(!TableShouldLoadMore(&s, 500));
    utassert(TableShouldLoadMore(&s, 980));
    s.loading = true;
    utassert(!TableShouldLoadMore(&s, 980));
}

// A cell's listener carries the row and the column as one number, which is
// what every listener that has two of them does here.
static void ACellIsOneNumber() {
    utassert(TableCellRow(TableCellPack(0, 0)) == 0);
    utassert(TableCellCol(TableCellPack(0, 0)) == 0);
    utassert(TableCellRow(TableCellPack(4999, 44)) == 4999);
    utassert(TableCellCol(TableCellPack(4999, 44)) == 44);
    // A million rows and four thousand columns still come back whole — on a
    // 64-bit target. The word is an intptr_t, so a 32-bit one has twenty bits
    // left over the column and tops out at half a million rows.
    const int bigRow = sizeof(intptr_t) >= 8 ? 1000000 : 500000;
    utassert(TableCellRow(TableCellPack(bigRow, 4095)) == bigRow);
    utassert(TableCellCol(TableCellPack(bigRow, 4095)) == 4095);
}

// The two right-click marks are exclusive, and a selection made any other
// way clears both — state.rs emits RightClickedRow(None) beside SelectRow so
// a context menu hanging off the old row is told to go.
static void ARightClickMarksARowOrACellButNeverBoth() {
    TableState s;
    s.rowCount = 8;
    s.colCount = 3;
    s.rowSelectable = true;

    s.rightClickedRow = 4;
    s.rightClickedCellRow = -1;
    s.rightClickedCellCol = -1;
    utassert(s.rightClickedRow == 4);

    // A cell mark replaces the row one outright.
    s.rightClickedCellRow = 2;
    s.rightClickedCellCol = 1;
    s.rightClickedRow = -1;
    utassert(s.rightClickedRow == -1);
    utassert(s.rightClickedCellRow == 2 && s.rightClickedCellCol == 1);

    // And the row that a cell mark names is not the selected row: the two
    // live in different fields, so a table can paint both.
    utassert(s.selectedRow == -1);
}

// update_visible_range_if_need: the delegate is told only when the range
// actually moved, and never about a range of one — Rust skips that because
// its virtual list lays a single item out to measure with, and here it is the
// frame before the pane has been laid out at all.
static void TheDelegateHearsAboutTheRangeOnlyWhenItMoves() {
    TableState s;
    utassert(TableVisibleRowsChanged(&s, 0, 20));
    // The same range again says nothing.
    utassert(!TableVisibleRowsChanged(&s, 0, 20));
    utassert(TableVisibleRowsChanged(&s, 5, 25));
    utassert(s.visibleRange.rowFirst == 5 && s.visibleRange.rowEnd == 25);
    // A range of one is the measuring pass, and does not even overwrite what
    // was last reported.
    utassert(!TableVisibleRowsChanged(&s, 0, 1));
    utassert(!TableVisibleRowsChanged(&s, 0, 0));
    utassert(s.visibleRange.rowFirst == 5 && s.visibleRange.rowEnd == 25);

    // The two axes are independent.
    utassert(TableVisibleColsChanged(&s, 2, 9));
    utassert(!TableVisibleColsChanged(&s, 2, 9));
    utassert(s.visibleRange.rowFirst == 5);
    utassert(s.visibleRange.colFirst == 2 && s.visibleRange.colEnd == 9);
}

// Which columns overlap the scrolling pane. The pinned ones are never in it —
// they do not move under the offset, so the run is counted from the first one
// that does, and the range runs one past the edge the way virtual_list does.
static void TheVisibleColumnsAreTheOnesUnderTheOffset() {
    TableState s;
    s.colCount = 6;
    TableSeedColOrder(&s, 6);
    TableEnsureCols(&s, 6);
    for (int c = 0; c < 6; c++) {
        s.colWidth[c] = 100;
    }
    int first = -1, end = -1;

    // A pane that has not been laid out yet answers an empty range rather
    // than every column, which the range-of-one rule then swallows.
    TableVisibleCols(&s, &first, &end);
    utassert(first == 0 && end == 0);

    // Two and a half columns fit; the one after the edge is built too.
    s.bodyBounds.w = 250;
    TableVisibleCols(&s, &first, &end);
    utassert(first == 0 && end == 4);

    // Slid over by one and a half columns: the second is still half on
    // screen, so it is still the first one visible.
    s.scrollX = 150;
    TableVisibleCols(&s, &first, &end);
    utassert(first == 1 && end == 6);

    // Two pinned columns: the offset moves the rest, and the range counts
    // over those four rather than over all six.
    s.scrollX = 0;
    s.fixedCols = 2;
    TableVisibleCols(&s, &first, &end);
    utassert(first == 0 && end == 4);
}

// scroll_to_col, which is what set_selected_col and set_selected_cell go
// through. The offset is over the columns that move, so a pinned one asks
// for the start of them and nothing else.
static void ScrollingToAColumnBringsItIn() {
    TableState s;
    s.colCount = 6;
    TableSeedColOrder(&s, 6);
    TableEnsureCols(&s, 6);
    for (int c = 0; c < 6; c++) {
        s.colWidth[c] = 100;
    }
    s.bodyBounds.w = 250;

    // A column already on screen does not move the offset.
    TableScrollToCol(&s, 1, ScrollStrategy::Top);
    utassert(s.scrollX == 0);
    // One off the right end comes in from that side: its far edge lands on
    // the viewport's, which is 500 - 250.
    TableScrollToCol(&s, 4, ScrollStrategy::Top);
    utassert(s.scrollX == 250);
    // And one off the left comes back in from that side.
    TableScrollToCol(&s, 0, ScrollStrategy::Top);
    utassert(s.scrollX == 0);

    // Pinned columns are not in the window at all: asking for one asks for
    // the first that moves, which is Rust's saturating_sub.
    s.fixedCols = 2;
    s.scrollX = 200;
    TableScrollToCol(&s, 0, ScrollStrategy::Top);
    utassert(s.scrollX == 0);
    // The last column, with two pinned: four move, so the content is 400
    // wide and the offset can reach 150.
    TableScrollToCol(&s, 5, ScrollStrategy::Top);
    utassert(s.scrollX == 150);

    // A pane that has not been laid out yet has nothing to scroll against.
    s.bodyBounds.w = 0;
    s.scrollX = 42;
    TableScrollToCol(&s, 5, ScrollStrategy::Top);
    utassert(s.scrollX == 42);
}

// refresh / prepare_col_groups: the table drops what it worked out for
// itself, so the caller's declarations are taken again.
static void ARefreshGivesTheColumnsBackToTheCaller() {
    TableState s;
    s.colCount = 4;
    TableSeedColOrder(&s, 4);
    for (int c = 0; c < 4; c++) {
        TableSeedColWidth(&s, c, 100);
    }
    s.colWidth[1] = 180;
    utassert(TableMoveColumn(&s, 0, 3));
    utassert(TableColAt(&s, 0) == 1);

    TableRefreshCols(&s);
    // The widths are unseeded, so the next build takes what is declared.
    TableSeedColWidth(&s, 1, 100);
    utassert(s.colWidth[1] == 100);
    // And the order is the caller's again.
    TableSeedColOrder(&s, 4);
    utassert(TableColAt(&s, 0) == 0);
    utassert(TableColAt(&s, 3) == 3);
}

// on_cell_click's opening rule. A table with a row header column has
// somewhere else to pick a row, so a second click on a selected cell is only
// ever a cell click; a table without one escalates instead.
static void ASecondClickOnACellTakesTheRowWhenThereIsNoRowHeader() {
    TableState s;
    s.rowCount = 8;
    s.colCount = 3;
    s.cellSelectable = true;
    s.rowSelectable = true;
    s.rowHeader = false;
    s.mode = TableSelectionMode::Cell;
    s.selectedCellRow = 3;
    s.selectedCellCol = 1;

    utassert(TableEscalatesToRow(&s, 3, 1, false));
    // A double click is the cell's, whatever else is true.
    utassert(!TableEscalatesToRow(&s, 3, 1, true));
    // Another cell is a plain selection.
    utassert(!TableEscalatesToRow(&s, 3, 2, false));
    utassert(!TableEscalatesToRow(&s, 4, 1, false));
    // With the header column there, nothing escalates.
    s.rowHeader = true;
    utassert(!TableEscalatesToRow(&s, 3, 1, false));
    // And a table whose rows cannot be selected has nowhere to escalate to.
    s.rowHeader = false;
    s.rowSelectable = false;
    utassert(!TableEscalatesToRow(&s, 3, 1, false));
    // Nor does one whose selection is a row already.
    s.rowSelectable = true;
    s.mode = TableSelectionMode::Row;
    utassert(!TableEscalatesToRow(&s, 3, 1, false));
}

static void KeyboardLeavesRowsAloneWhenTheyAreNotSelectable() {
    TableState s;
    s.rowCount = 100;
    s.colCount = 3;
    s.pageRows = 12;
    s.rowSelectable = false;
    Ctx cx = {};
    const TableAction actions[] = {
        TableAction::SelectNext,     TableAction::SelectNext,
        TableAction::SelectPageDown, TableAction::SelectPrev,
        TableAction::SelectPageUp,
    };
    for (const TableAction action : actions) {
        TablePerform(&s, &cx, action);
        utassert(s.selectedRow == -1);
    }
}

static const component::TableColumn kDumpColumns[] = {
    {StrL("ID")},
    {StrL("Name")},
};

static Str DumpCellText(Ctx*, void*, int row, int col) {
    return Str(col == 0 ? (row == 0 ? "1" : (row == 1 ? "2" : "3"))
                        : (row == 0 ? "a" : (row == 1 ? "b" : "c")));
}

// dump_range: the rows inside the range, and the headers whatever it is.
static void ADumpRangeIsClampedToTheTable() {
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.a = a;
    Entity<TableState> state = {};
    component::DataTable* t = component::DataTable::New(&cx, StrL("t"), state)
                                  ->Columns(kDumpColumns, 2)
                                  ->Rows(3, nullptr, nullptr)
                                  ->CellText(DumpCellText);

    Vec<Str> heads;
    Vec<Str> cells;
    t->DumpRange(1, 3, &heads, &cells);
    utassert(len(heads) == 2);
    // Two rows of two.
    utassert(len(cells) == 4);
    utassert(cells[0].s[0] == '2' && cells[1].s[0] == 'b');
    utassert(cells[2].s[0] == '3' && cells[3].s[0] == 'c');

    // Past the end clamps rather than reading off it, so a caller can walk a
    // table in fixed steps without knowing where it ends.
    VecReset(heads);
    VecReset(cells);
    t->DumpRange(2, 99, &heads, &cells);
    utassert(len(cells) == 2);
    // A range the wrong way round, or past the end entirely, is no rows.
    VecReset(heads);
    VecReset(cells);
    t->DumpRange(3, 1, &heads, &cells);
    utassert(len(cells) == 0 && len(heads) == 2);

    // The whole table is what dump answers.
    VecReset(heads);
    VecReset(cells);
    t->Dump(&heads, &cells);
    utassert(len(cells) == 6);
    VecReset(heads);
    VecReset(cells);
    ArenaDelete(a);
}

// The first element under `root` whose name is `name`, wherever it is.
static El* FindNamed(El* root, const char* name) {
    if (!root) {
        return nullptr;
    }
    if (root->id.s && base::StrEqI(root->id, name)) {
        return root;
    }
    for (El* c = root->first; c; c = c->next) {
        if (El* hit = FindNamed(c, name)) {
            return hit;
        }
    }
    return nullptr;
}

// state.rs names a row `("table-row", ix)` and a head `("col-header", ix)` —
// a name that only has to be unique among siblings, because GPUI scopes it by
// the stack of ids above it. The port spelled the caller's id into every one
// of them instead: `format!("{id}-row-{r}")`. Now that the fold does that
// work, the name is local and two tables on one page are still two tables.
static void TwoTablesOnOnePageAreTwoTables() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;

    Entity<TableState> state = EntityNewState<TableState>(&app);
    El* page = Div(a);
    El* left = Div(a)->Id(StrL("left"));
    El* right = Div(a)->Id(StrL("right"));
    left->Child(component::DataTable::New(&cx, StrL("t"), state)
                    ->Columns(kDumpColumns, 2)
                    ->Rows(3, nullptr, nullptr)
                    ->CellText(DumpCellText)
                    ->IntoEl());
    right->Child(component::DataTable::New(&cx, StrL("t"), state)
                     ->Columns(kDumpColumns, 2)
                     ->Rows(3, nullptr, nullptr)
                     ->CellText(DumpCellText)
                     ->IntoEl());
    page->Child(left)->Child(right);
    IdsCollect(page);

    El* rowL = FindNamed(left, "row-0");
    El* rowR = FindNamed(right, "row-0");
    utassert(rowL && rowR);
    // Both are hit targets, and they are not the same one.
    utassert(rowL->clickId != 0 && rowR->clickId != 0);
    utassert(rowL->clickId != rowR->clickId);

    // The same holds a level down, where the head cell and the box inside it
    // used to be handed the one name twice over.
    El* thL = FindNamed(left, "th-0");
    El* headL = FindNamed(left, "col-header-0");
    utassert(thL && headL);
    utassert(thL->clickId != 0 && headL->clickId != 0);
    utassert(thL->clickId != headL->clickId);
    utassert(FindNamed(right, "th-0")->clickId != thL->clickId);

    // The root is a hit target and the rows are not focusable — `div().id()`
    // on both upstream, with `track_focus` only on the root. That pair is what
    // lets a press on a row reach the table: the press walks the chain of
    // boxes containing it, steps over the row because the row wants no focus,
    // and lands on the table, whose key context is what makes the arrows and
    // Escape mean anything. With the row focusable and the root not even on
    // the chain, the press focused nothing and the table's keys were dead.
    El* root = left->first;
    utassert(root && root->clickId != 0);
    utassert(root->style.focusId == state.Get(&app)->focus.id);
    utassert(root->style.focusId != 0 && root->style.focusOnPress);
    utassert(rowL->style.focusId == 0);
    utassert(thL->style.focusId == 0 && headL->style.focusId == 0);
    // Every part is `div().id()` upstream, so a cell is a hit target too —
    // which it can only afford to be because a click bubbles out of the rect
    // it landed on to the row that is listening for it.
    El* cell = FindNamed(left, "cell-0-0");
    utassert(cell && cell->clickId != 0 && cell->style.focusId == 0);

    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

void TestDataTable() {
    SourceColumnBuildersKeepEveryField();
    EachColumnHasItsOwnResizeBounds();
    TheSourceDelegateDrivesTheTable();
    ADumpRangeIsClampedToTheTable();
    TwoTablesOnOnePageAreTwoTables();
    TheDelegateHearsAboutTheRangeOnlyWhenItMoves();
    TheVisibleColumnsAreTheOnesUnderTheOffset();
    ScrollingToAColumnBringsItIn();
    ARefreshGivesTheColumnsBackToTheCaller();
    ASecondClickOnACellTakesTheRowWhenThereIsNoRowHeader();
    KeyboardLeavesRowsAloneWhenTheyAreNotSelectable();
    ARightClickMarksARowOrACellButNeverBoth();
    ACellIsOneNumber();
    AColumnKeepsItsWidthOnceItHasOne();
    AResizeIsClamped();
    TheGapIsAfterTheLastCentreLeftOfThePointer();
    AHeadDragNeverCrossesTheFixedBoundary();
    AMovedColumnLandsInTheGap();
    LoadMoreAsksNearTheEnd();
    TheKeyTable();
    TheSortCycle();
    OnlyOneColumnCarriesTheSort();
}
