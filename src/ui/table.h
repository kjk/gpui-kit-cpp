#ifndef GPUI_SRC_UI_TABLE_H_
#define GPUI_SRC_UI_TABLE_H_
/* Themed table — crates/ui/src/table */

#include "ui/sizing.h"
#include "ui/menu.h"
#include "ui/data_table.h"

namespace gpui {

namespace component {

enum class ColumnFixed : uint8_t {
    Left
};

// One column of a DataTable: what its head says, how wide it is, and whether
// it answers to a sort or a selection.
struct TableColumn {
    Str title = {};
    float width = 100;
    bool right = false;
    bool sortable = false;
    bool selectable = true;
    // Column::resizable: whether this column's right edge takes a drag.
    bool resizable = true;
    // Column::fixed(ColumnFixed::Left): the column stays put while the rest
    // scroll under the head. TableState::colFixed is the master switch.
    bool fixed = false;
    // Source fields that the first aggregate representation omitted.
    Str key = {};
    Str name = {};
    bool center = false;
    bool hasSort = false;
    ColumnSort sort = ColumnSort::Default;
    bool hasPaddings = false;
    Edges paddings = {};
    ColumnFixed fixedSide = ColumnFixed::Left;
    bool movable = true;
    float minWidth = 20;
    float maxWidth = 0; // zero is source f32::MAX

    static TableColumn New(Str key, Str name);
    TableColumn Sort(ColumnSort value) const;
    TableColumn Sortable() const;
    TableColumn Ascending() const;
    TableColumn Descending() const;
    TableColumn TextCenter() const;
    TableColumn TextRight() const;
    TableColumn Paddings(Edges value) const;
    TableColumn P0() const;
    TableColumn Width(float value) const;
    TableColumn Fixed(ColumnFixed value = ColumnFixed::Left) const;
    TableColumn FixedLeft() const;
    TableColumn Resizable(bool value) const;
    TableColumn Movable(bool value) const;
    TableColumn Selectable(bool value) const;
    TableColumn MinWidth(float value) const;
    TableColumn MaxWidth(float value) const;
};

using Column = TableColumn;

// ColumnGroup: one band of an upper head row, spanning that many columns of
// the row under it. Every level has to span every column, so the bands line
// up with the columns they name.
struct ColumnGroup {
    Str label = {};
    int span = 1;

    static ColumnGroup New(Str label, size_t span);
};

using TableGroupCell = ColumnGroup;

struct TableGroupHeader {
    const TableGroupCell* cells = nullptr;
    int n = 0;
};

struct DataTable;

// Rust's generic TableDelegate becomes a POD function table. Every hook is
// optional except the counts, column and cell renderers; absent hooks retain
// the same defaults as the Rust trait.
struct TableDelegate {
    void* data = nullptr;
    int (*columnsCount)(Ctx* cx, void* data) = nullptr;
    int (*rowsCount)(Ctx* cx, void* data) = nullptr;
    TableColumn (*column)(Ctx* cx, void* data, int col) = nullptr;
    void (*performSort)(Ctx* cx, void* data, int col,
                        ColumnSort sort) = nullptr;
    El* (*renderHeader)(Ctx* cx, void* data) = nullptr;
    El* (*renderGroupTh)(Ctx* cx, void* data, Str label, int span,
                         float width) = nullptr;
    El* (*renderTh)(Ctx* cx, void* data, int col) = nullptr;
    El* (*renderTr)(Ctx* cx, void* data, int row) = nullptr;
    El* (*renderTd)(Ctx* cx, void* data, int row, int col) = nullptr;
    void (*groupHeaders)(Ctx* cx, void* data, DataTable* table) = nullptr;
    PopupMenu* (*contextMenu)(Ctx* cx, void* data, int row,
                              PopupMenu* menu) = nullptr;
    void (*moveColumn)(Ctx* cx, void* data, int from, int to) = nullptr;
    El* (*renderEmpty)(Ctx* cx, void* data) = nullptr;
    bool (*loading)(Ctx* cx, void* data) = nullptr;
    El* (*renderLoading)(Ctx* cx, void* data, UiSize size) = nullptr;
    bool (*hasMore)(Ctx* cx, void* data) = nullptr;
    int (*loadMoreThreshold)(void* data) = nullptr;
    void (*loadMore)(Ctx* cx, void* data) = nullptr;
    El* (*renderLastEmptyCol)(Ctx* cx, void* data) = nullptr;
    void (*visibleRowsChanged)(Ctx* cx, void* data, int first,
                               int end) = nullptr;
    void (*visibleColumnsChanged)(Ctx* cx, void* data, int first,
                                  int end) = nullptr;
    Str (*cellText)(Ctx* cx, void* data, int row, int col) = nullptr;
};

// The themed data table. The rows are the caller's — a delegate renders a
// cell in Rust and a callback does the same here — and the table owns the
// head, the sort icons, the selection and the striping.
struct DataTable {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Entity<TableState> state = {};
    const TableColumn* columns = nullptr;
    int nColumns = 0;
    // render_td: the cell at (row, col), built by the caller.
    El* (*cell)(Ctx* cx, void* data, int row, int col) = nullptr;
    void* data = nullptr;
    int nRows = 0;
    bool stripe = false;
    // TableOptions::bordered: the rounded 1px border round the table, on by
    // default as in Rust.
    bool bordered = true;
    // TableOptions::scrollbar_visible: the right (vertical) and bottom
    // (horizontal) scrollbars of the scrolling body, both on by default.
    bool scrollbarV = true;
    bool scrollbarH = true;
    // group_headers: the extra head rows a caller stacks over the columns,
    // outermost first.
    ArenaVec<TableGroupHeader> groupHeaders;
    // The height the body scrolls inside. 0 leaves every row built, which is
    // what a table small enough not to need a viewport wants.
    float h = 0;
    // render_empty: what a table with no rows shows. Null takes Rust's own.
    El* empty = nullptr;
    // TableDelegate::context_menu(row_ix, menu). A secondary press marks a
    // row — which is what `right_clicked_row` is — and the table hands that
    // row and a menu of its own to the caller, which fills the menu in and
    // hands it back. The trait's default answers it back untouched, so a
    // table that sets nothing here has no menu; null is the same thing.
    PopupMenu* (*contextMenu)(Ctx* cx, void* data, int row,
                              PopupMenu* menu) = nullptr;
    // render_last_empty_col: what sits past the last column, on the
    // scrolling side. The trait's default is a 12px blank and shows nothing,
    // so this is an override point rather than an appearance — a table that
    // wants a row action or an add-column button puts it here. Null takes
    // the default.
    El* (*lastEmptyCol)(Ctx* cx, void* data) = nullptr;
    // visible_rows_changed / visible_columns_changed. Both fire from the
    // build, so both have to be fast — Rust says so on the trait, and here
    // they are called while the frame's element tree is being made.
    void (*visibleRowsChanged)(Ctx* cx, void* data, int first,
                               int end) = nullptr;
    void (*visibleColsChanged)(Ctx* cx, void* data, int first,
                               int end) = nullptr;
    // cell_text: what a cell holds as text, which is what an export reads.
    // The default answers nothing, so a table that has not set it dumps
    // empty cells the way Rust's does.
    Str (*cellText)(Ctx* cx, void* data, int row, int col) = nullptr;
    // Size::table_row_height, which is what with_size(..) comes to here: 26 /
    // 30 / 32 / 40, or an explicit pixel height.
    float rowHeight = 32;
    // options.size, kept because render_loading is handed it.
    UiSize size = UiSize::Medium;
    TableDelegate delegate = {};
    bool hasDelegate = false;

    static DataTable* New(Ctx* cx, Str id, Entity<TableState> state);
    DataTable* Columns(const TableColumn* cols, int n);
    DataTable* Delegate(const TableDelegate& value);
    DataTable* Rows(int n, void* data,
                    El* (*cell)(Ctx* cx, void* data, int row, int col));
    DataTable* Stripe(bool v);
    DataTable* Bordered(bool v);
    DataTable* ScrollbarVisible(bool vertical, bool horizontal);
    DataTable* WithSize(UiSize s);
    // Size::Size(px), which the story's 48px row offers.
    DataTable* RowHeight(float px);
    DataTable* GroupHeader(const TableGroupCell* cells, int n);
    DataTable* H(float px);
    DataTable* Empty(El* e);
    DataTable* ContextMenu(PopupMenu* (*fn)(Ctx*, void*, int, PopupMenu*));
    DataTable* LastEmptyCol(El* (*fn)(Ctx*, void*));
    DataTable* OnVisibleRows(void (*fn)(Ctx*, void*, int, int));
    DataTable* OnVisibleCols(void (*fn)(Ctx*, void*, int, int));
    DataTable* CellText(Str (*fn)(Ctx*, void*, int, int));
    // TableState::dump. Rust answers `(Vec<String>, Vec<Vec<String>>)`; the
    // rows come back flat here, `nColumns` to a row, since a Vec of Vecs is
    // not a thing this tree builds. Both are arena strings, so neither is
    // freed. It lives on the table rather than the state because the table
    // *is* the delegate here, and `cell_text` is the delegate's.
    void Dump(Vec<Str>* heads, Vec<Str>* cells);
    // TableState::headers. A batched export reads them once here and streams
    // the rows through DumpRange.
    void Headers(Vec<Str>* heads);
    // TableState::dump_range: the same two, for the rows in `[lo, hi)`. The
    // range is clamped to what the table holds, so a caller can walk a big
    // table in bounded steps without checking the end itself.
    void DumpRange(int lo, int hi, Vec<Str>* heads, Vec<Str>* cells);
    El* IntoEl();
    // The table itself, before the context menu is hung off it.
    El* BuildEl();
    // The width a column is drawn at: the table's own once a drag has moved
    // it, and what the caller declared until then.
    float ColWidth(const TableState* s, int col) const;
};

// ─── the simple table (crates/ui/src/table/table.rs) ──────────────────────
//
// The stateless, composable table: the semantic parts from `crates/base`,
// themed and sized. Unlike DataTable it has no state, no virtual scrolling
// and no column management — a caller writes the rows out.
//
// Rust hands the Table's size down to every child as it renders
// (`ChildElement::into_any(ix, size)`), which is also where a child learns
// which index it is. A builder here is finished by its parent instead: the
// parent stamps its size and index onto each child in `IntoEl()`, so the
// caller names the size once, on the Table, and never numbers a row.
//
// One thing the port cannot copy: `text_color` on the header and the footer
// is inherited by everything inside them in Rust. Colour is not inherited
// here, so a caller paints its own text — the two colours are on the theme
// (`tableHeadFg`, `tableFootFg`) for exactly that.

enum class TableAlign : uint8_t {
    Left,
    Center,
    Right
};

// A header cell or a data cell: `TableHead` and `TableCell` differ in their
// accessibility role; the parent stamps their one-based column index.
struct TableCellEl {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    // Which of the two this is, which is what names it in the frame.
    bool head = false;
    int ix = 0;
    int colSpan = 1;
    TableAlign align = TableAlign::Left;
    UiSize size = UiSize::Medium;
    // The width the caller asked for. Unset — kAuto — is Rust's "no width in
    // the style", which is what makes the cell share the row by its span.
    float width = kAuto;
    ArenaVec<El*> children;
    // The part's own StyleRefinement (Styled), applied where Rust's render
    // calls refine_style. A width in it counts as the caller's width.
    ElRefiner refiner = {};

    TableCellEl* ColSpan(int n);
    TableCellEl* TextCenter();
    TableCellEl* TextRight();
    TableCellEl* W(float v);
    TableCellEl* WithSize(UiSize s);
    TableCellEl* Child(El* e);
    El* IntoEl();
};

struct TableHead {
    static TableCellEl* New(Ctx* cx);
};
struct TableCell {
    static TableCellEl* New(Ctx* cx);
};

struct TableRow {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    int ix = 0;
    UiSize size = UiSize::Medium;
    bool hasBg = false;
    Background bg = {};
    ArenaVec<TableCellEl*> cells;
    ElRefiner refiner = {};

    static TableRow* New(Ctx* cx);
    TableRow* Bg(Background c);
    TableRow* Child(TableCellEl* c);
    El* IntoEl();
};

// The three row groups. A header is a band of its own colour with a rule
// under it, a footer the same at the other end, and a body is neither.
enum class TableGroupKind : uint8_t {
    Header,
    Body,
    Footer
};

struct TableGroup {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    TableGroupKind kind = TableGroupKind::Body;
    int ix = 0;
    UiSize size = UiSize::Medium;
    ArenaVec<TableRow*> rows;
    ElRefiner refiner = {};

    TableGroup* Child(TableRow* r);
    El* IntoEl();
};

struct TableHeader {
    static TableGroup* New(Ctx* cx);
};
struct TableBody {
    static TableGroup* New(Ctx* cx);
};
struct TableFooter {
    static TableGroup* New(Ctx* cx);
};

struct TableCaption {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    // The caption's place among the table's children, stamped by the table.
    int ix = 0;
    UiSize size = UiSize::Medium;
    ArenaVec<El*> children;
    ElRefiner refiner = {};

    static TableCaption* New(Ctx* cx);
    TableCaption* Child(El* e);
    El* IntoEl();
};

// One of a Table's children: a row group or a caption. Rust keeps them as
// one list of AnyChildElement, in the order they were added, and numbers
// each by its place in it.
struct TablePart {
    TableGroup* group = nullptr;
    TableCaption* caption = nullptr;
};

struct Table {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    UiSize size = UiSize::Medium;
    bool bordered = false;
    ArenaVec<TablePart> parts;
    // What every part under it is scoped by. Rust takes one for the same
    // reason: a name only has to be unique among its siblings, and the table
    // is what makes two rows called `row-0` two different rows.
    Str id = {};
    // The accessible name. A TableCaption is visible descriptive content and
    // is not used automatically as the table's accessible name.
    Str accessibilityLabel = {};

    static Table* New(Ctx* cx, Str id);
    Table* WithSize(UiSize s);
    // Set the name a screen reader announces for the table.
    Table* AccessibilityLabel(Str s);
    // `.border_1().border_color(..).rounded(..)`, which the story's second
    // table asks for by hand. Kept as one flag rather than as a style
    // refinement, since a frame is the only refinement anything makes.
    Table* Bordered(bool v = true);
    Table* Child(TableGroup* g);
    Table* Child(TableCaption* c);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_TABLE_H_
