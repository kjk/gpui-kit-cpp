#include "ui/table.h"
#include "base/list_settings.h"
#include "ui/i18n.h"
#include "ui/scroll.h"
#include "ui/skeleton.h"

namespace gpui {

namespace component {

TableColumn TableColumn::New(Str keyValue, Str nameValue) {
    TableColumn out;
    out.key = keyValue;
    out.name = nameValue;
    out.title = nameValue;
    return out;
}

TableColumn TableColumn::Sort(ColumnSort value) const {
    TableColumn out = *this;
    out.hasSort = true;
    out.sortable = true;
    out.sort = value;
    return out;
}

TableColumn TableColumn::Sortable() const {
    return Sort(ColumnSort::Default);
}

TableColumn TableColumn::Ascending() const {
    return Sort(ColumnSort::Ascending);
}

TableColumn TableColumn::Descending() const {
    return Sort(ColumnSort::Descending);
}

TableColumn TableColumn::TextCenter() const {
    TableColumn out = *this;
    out.center = true;
    out.right = false;
    return out;
}

TableColumn TableColumn::TextRight() const {
    TableColumn out = *this;
    out.right = true;
    out.center = false;
    return out;
}

TableColumn TableColumn::Paddings(Edges value) const {
    TableColumn out = *this;
    out.hasPaddings = true;
    out.paddings = value;
    return out;
}

TableColumn TableColumn::P0() const {
    return Paddings({});
}

TableColumn TableColumn::Width(float value) const {
    TableColumn out = *this;
    out.width = value;
    return out;
}

TableColumn TableColumn::Fixed(ColumnFixed value) const {
    TableColumn out = *this;
    out.fixed = true;
    out.fixedSide = value;
    return out;
}

TableColumn TableColumn::FixedLeft() const {
    return Fixed(ColumnFixed::Left);
}

TableColumn TableColumn::Resizable(bool value) const {
    TableColumn out = *this;
    out.resizable = value;
    return out;
}

TableColumn TableColumn::Movable(bool value) const {
    TableColumn out = *this;
    out.movable = value;
    return out;
}

TableColumn TableColumn::Selectable(bool value) const {
    TableColumn out = *this;
    out.selectable = value;
    return out;
}

TableColumn TableColumn::MinWidth(float value) const {
    TableColumn out = *this;
    out.minWidth = value;
    if (out.width < value) {
        out.width = value;
    }
    return out;
}

TableColumn TableColumn::MaxWidth(float value) const {
    TableColumn out = *this;
    out.maxWidth = value;
    if (out.width > value) {
        out.width = value;
    }
    return out;
}

ColumnGroup ColumnGroup::New(Str value, size_t spanValue) {
    ColumnGroup out;
    out.label = value;
    out.span = spanValue > 0 ? (int)spanValue : 1;
    return out;
}

// ─── the simple table (crates/ui/src/table/table.rs) ──────────────────────

// MIN_CELL_WIDTH: a cell never narrows past this, whatever the row does.
static const float kMinCellWidth = 100.f;

static TableCellEl* NewCell(Ctx* cx, bool head) {
    Arena* a = cx->a;
    TableCellEl* c = ArenaNew<TableCellEl>(a);
    c->a = a;
    c->cx = cx;
    c->head = head;
    return c;
}

TableCellEl* TableHead::New(Ctx* cx) {
    return NewCell(cx, true);
}
TableCellEl* TableCell::New(Ctx* cx) {
    return NewCell(cx, false);
}

TableCellEl* TableCellEl::ColSpan(int n) {
    colSpan = n > 1 ? n : 1;
    return this;
}
TableCellEl* TableCellEl::TextCenter() {
    align = TableAlign::Center;
    return this;
}
TableCellEl* TableCellEl::TextRight() {
    align = TableAlign::Right;
    return this;
}
TableCellEl* TableCellEl::W(float v) {
    width = v;
    return this;
}
TableCellEl* TableCellEl::WithSize(UiSize s) {
    size = s;
    return this;
}
TableCellEl* TableCellEl::Child(El* e) {
    children.Append(a, e);
    return this;
}

El* TableCellEl::IntoEl() {
    Edges p = UiTableCellPadding(size);
    Str id = StrDup(a, fmt(head ? "head-%d" : "cell-%d", ix));
    El* e = head ? gpui::TableHead::New(cx, id, ix + 1)
                 : gpui::TableCell::New(cx, id, ix + 1);
    e->FlexRow()->ItemsCenter();
    // `.when(self.style.size.width.is_none(), ..)`: a cell the caller sized
    // keeps that width and shrinks from it like any flex item; one that was
    // not sized starts from the whole row and shares it with its siblings,
    // a span counting for that many columns. A width in the refinement is
    // the caller's too.
    bool styledWidth = false;
    if (width == kAuto && refiner.IsSet()) {
        El* probe = Div(a);
        refiner.Apply(probe);
        styledWidth = probe->style.width != kAuto;
    }
    if (width != kAuto) {
        e->W(width);
    } else if (!styledWidth) {
        e->Shrink(1)->BasisFrac((float)colSpan);
    }
    e->MinW(kMinCellWidth * (float)colSpan);
    e->PadX(p.left)->PadY(p.top);
    if (align == TableAlign::Center) {
        e->JustifyCenter();
    } else if (align == TableAlign::Right) {
        e->JustifyEnd();
    }
    refiner.Apply(e);
    for (El* c : children) {
        e->Child(c);
    }
    return e;
}

TableRow* TableRow::New(Ctx* cx) {
    Arena* a = cx->a;
    TableRow* r = ArenaNew<TableRow>(a);
    r->a = a;
    r->cx = cx;
    return r;
}
TableRow* TableRow::Bg(Background c) {
    bg = c;
    hasBg = true;
    return this;
}
TableRow* TableRow::Child(TableCellEl* c) {
    cells.Append(a, c);
    return this;
}

El* TableRow::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    Str rowId = ElementIdNamed(a, StrL("row"), (uint64_t)ix);
    El* row = gpui::TableRow::New(cx, rowId, ix + 1)->W(kFill)->FlexRow();
    if (hasBg) {
        row->Bg(bg);
    }
    refiner.Apply(row);
    // `.when(self.ix > 0, |this| this.border_t_1())`: the rule goes between
    // the rows of a group, never above its first one — the header carries
    // its own rule underneath, and the footer one above.
    if (ix > 0) {
        row->BorderT(1, th.tableRowBorder);
    }
    for (int i = 0; i < cells.len; i++) {
        cells[i]->ix = i;
        cells[i]->size = size;
        row->Child(cells[i]->IntoEl());
    }
    return row;
}

static TableGroup* NewGroup(Ctx* cx, TableGroupKind kind) {
    Arena* a = cx->a;
    TableGroup* g = ArenaNew<TableGroup>(a);
    g->a = a;
    g->cx = cx;
    g->kind = kind;
    return g;
}

TableGroup* TableHeader::New(Ctx* cx) {
    return NewGroup(cx, TableGroupKind::Header);
}
TableGroup* TableBody::New(Ctx* cx) {
    return NewGroup(cx, TableGroupKind::Body);
}
TableGroup* TableFooter::New(Ctx* cx) {
    return NewGroup(cx, TableGroupKind::Footer);
}

TableGroup* TableGroup::Child(TableRow* r) {
    rows.Append(a, r);
    return this;
}

El* TableGroup::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    El* g = nullptr;
    if (kind == TableGroupKind::Header) {
        g = gpui::TableHeader::New(
                cx, ElementIdNamed(a, StrL("table-header"), (uint64_t)ix))
                ->W(kFill)
                ->FlexCol()
                ->Bg(th.tokens.tableHead)
                ->Fg(th.tableHeadFg);
        refiner.Apply(g);
        g->BorderB(1, th.tableRowBorder);
    } else if (kind == TableGroupKind::Footer) {
        // A footer is a plain div in Rust, not one of the semantic parts.
        g = Div(a)
                ->Id(ElementIdNamed(a, StrL("table-footer"), (uint64_t)ix))
                ->W(kFill)
                ->FlexCol()
                ->Bg(th.tokens.tableFoot)
                ->Fg(th.tableFootFg)
                ->BorderT(1, th.tableRowBorder);
        refiner.Apply(g);
    } else {
        g = gpui::TableBody::New(
                cx, ElementIdNamed(a, StrL("table-body"), (uint64_t)ix))
                ->W(kFill)
                ->FlexCol();
        refiner.Apply(g);
    }
    for (int i = 0; i < rows.len; i++) {
        rows[i]->ix = i;
        rows[i]->size = size;
        g->Child(rows[i]->IntoEl());
    }
    return g;
}

TableCaption* TableCaption::New(Ctx* cx) {
    Arena* a = cx->a;
    TableCaption* c = ArenaNew<TableCaption>(a);
    c->a = a;
    c->cx = cx;
    return c;
}
TableCaption* TableCaption::Child(El* e) {
    children.Append(a, e);
    return this;
}

El* TableCaption::IntoEl() {
    Edges p = UiTableCellPadding(size);
    El* e = gpui::TableCaption::New(
                cx, ElementIdNamed(a, StrL("table-caption"), (uint64_t)ix))
                ->W(kFill)
                ->FlexRow()
                ->JustifyCenter()
                ->TextCenter()
                ->PadX(p.left)
                ->PadY(p.top)
                ->Font(14)
                ->Fg(ThemeNow(cx->app).mutedFg);
    refiner.Apply(e);
    for (El* c : children) {
        e->Child(c);
    }
    return e;
}

// The id is the caller's, the way `Table::new("example-table")` is upstream.
// Everything under it is named locally and told apart by the path, so two
// tables on one page are two id spaces — which they were not while every one
// of them opened with the constant "table".
Table* Table::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    Table* t = ArenaNew<Table>(a);
    t->a = a;
    t->cx = cx;
    t->id = id;
    return t;
}
Table* Table::WithSize(UiSize s) {
    size = s;
    return this;
}
Table* Table::Bordered(bool v) {
    bordered = v;
    return this;
}
Table* Table::AccessibilityLabel(Str s) {
    accessibilityLabel = s;
    return this;
}
Table* Table::Child(TableGroup* g) {
    TablePart part;
    part.group = g;
    parts.Append(a, part);
    return this;
}
Table* Table::Child(TableCaption* c) {
    TablePart part;
    part.caption = c;
    parts.Append(a, part);
    return this;
}

El* Table::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    El* t = gpui::Table::New(cx, id, -1, -1, accessibilityLabel)
                ->FlexCol()
                ->W(kFill)
                ->Font(14)
                ->ClipY()
                ->ClipX()
                ->Bg(th.tokens.tableBg);
    if (bordered) {
        t->Border(1, th.border)->Radius(th.radius);
    }
    // Every child in the order it was added, numbered by its place.
    for (int i = 0; i < parts.len; i++) {
        if (TableGroup* g = parts[i].group) {
            g->ix = i;
            g->size = size;
            t->Child(g->IntoEl());
        } else if (TableCaption* c = parts[i].caption) {
            c->ix = i;
            c->size = size;
            t->Child(c->IntoEl());
        }
    }
    return t;
}

DataTable* DataTable::New(Ctx* cx, Str id, Entity<TableState> state) {
    Arena* a = cx->a;
    DataTable* t = ArenaNew<DataTable>(a);
    t->a = a;
    t->cx = cx;
    t->id = id;
    t->state = state;
    return t;
}
DataTable* DataTable::Columns(const TableColumn* cols, int n) {
    columns = cols;
    nColumns = n;
    return this;
}
DataTable* DataTable::Delegate(const TableDelegate& value) {
    delegate = value;
    hasDelegate = true;
    data = value.data;
    contextMenu = value.contextMenu;
    lastEmptyCol = value.renderLastEmptyCol;
    visibleRowsChanged = value.visibleRowsChanged;
    visibleColsChanged = value.visibleColumnsChanged;
    cellText = value.cellText;
    return this;
}
DataTable* DataTable::Rows(int n, void* d,
                           El* (*fn)(Ctx* cx, void* data, int row, int col)) {
    nRows = n;
    data = d;
    cell = fn;
    return this;
}
DataTable* DataTable::Stripe(bool v) {
    stripe = v;
    return this;
}
DataTable* DataTable::Bordered(bool v) {
    bordered = v;
    return this;
}
DataTable* DataTable::ScrollbarVisible(bool vertical, bool horizontal) {
    scrollbarV = vertical;
    scrollbarH = horizontal;
    return this;
}
DataTable* DataTable::WithSize(UiSize sz) {
    size = sz;
    rowHeight = UiTableRowHeight(sz);
    return this;
}
DataTable* DataTable::RowHeight(float px) {
    rowHeight = px;
    return this;
}
DataTable* DataTable::H(float px) {
    h = px;
    return this;
}
DataTable* DataTable::Empty(El* e) {
    empty = e;
    return this;
}
DataTable* DataTable::LastEmptyCol(El* (*fn)(Ctx*, void*)) {
    lastEmptyCol = fn;
    return this;
}
DataTable* DataTable::OnVisibleRows(void (*fn)(Ctx*, void*, int, int)) {
    visibleRowsChanged = fn;
    return this;
}
DataTable* DataTable::OnVisibleCols(void (*fn)(Ctx*, void*, int, int)) {
    visibleColsChanged = fn;
    return this;
}
DataTable* DataTable::CellText(Str (*fn)(Ctx*, void*, int, int)) {
    cellText = fn;
    return this;
}

static Str TableColumnLabel(const TableColumn& column) {
    return column.name.s ? column.name : column.title;
}

// t!("Table.SortBy", column = name). The catalogue keeps rust_i18n's
// %{column} placeholder; the column's own name fills it.
static TempStr SortByLabel(Str column) {
    Str t = Tr("Table.SortBy");
    int at = StrFind(t, "%{column}");
    if (at < 0) {
        return fmt("%s", t);
    }
    return fmt("%s%s%s", Str(t.s, at), column,
               Str(t.s + at + 9, len(t) - at - 9));
}

static El* TableColumnPadding(El* element, const TableColumn& column) {
    if (!column.hasPaddings) {
        return element->PadX(8)->PadY(6);
    }
    return element->PadL(column.paddings.left)
        ->PadR(column.paddings.right)
        ->PadT(column.paddings.top)
        ->PadB(column.paddings.bottom);
}

void DataTable::Headers(Vec<Str>* heads) {
    if (!heads) {
        return;
    }
    for (int c = 0; c < nColumns; c++) {
        VecAppend(*heads, TableColumnLabel(columns[c]));
    }
}

void DataTable::DumpRange(int lo, int hi, Vec<Str>* heads, Vec<Str>* cells) {
    Headers(heads);
    if (!cells) {
        return;
    }
    lo = ClampI(lo, 0, nRows);
    if (hi > nRows) {
        hi = nRows;
    }
    if (hi < lo) {
        hi = lo;
    }
    for (int r = lo; r < hi; r++) {
        for (int c = 0; c < nColumns; c++) {
            VecAppend(*cells, cellText ? cellText(cx, data, r, c) : Str());
        }
    }
}

// The whole table at once, which is every row of it in memory. A big one is
// what DumpRange is for.
void DataTable::Dump(Vec<Str>* heads, Vec<Str>* cells) {
    DumpRange(0, nRows, heads, cells);
}
DataTable* DataTable::GroupHeader(const TableGroupCell* cells, int n) {
    if (cells && n > 0) {
        groupHeaders.Append(a, TableGroupHeader{cells, n});
    }
    return this;
}

// render_sort_icon: which way the column is sorted, and half-lit when it is
// not sorted at all. Rust has SortAscending / SortDescending glyphs; the two
// chevrons stand in for them here.
static El* SortIcon(Ctx* cx, const Theme& th, ColumnSort sort) {
    Arena* a = cx->a;
    IconName name = IconName::ChevronsUpDown;
    bool on = true;
    switch (sort) {
        case ColumnSort::Ascending:
            name = IconName::ChevronUp;
            break;
        case ColumnSort::Descending:
            name = IconName::ChevronDown;
            break;
        default:
            on = false;
            break;
    }
    return Div(a)
        ->Pad(2)
        ->Radius(th.radius * 0.5f)
        ->HoverBg(th.tokens.secondary)
        ->Child(
            IconEl(a, name, Rems(cx, 0.75f))
                ->Fg(on ? th.secondaryFg : RgbaOpacity(th.secondaryFg, 0.5f)));
}

float DataTable::ColWidth(const TableState* s, int col) const {
    float declared = columns[col].width;
    return s ? TableColWidth(s, col, declared) : declared;
}

// resize_handle_band: the interactive band that drags the boundary on the
// right of column `col`, carrying no styling that places or paints it.
//
// Paint order decides which element is offered a drag first: later siblings
// win, and a header cell starts a column reorder. So a single band straddling
// the boundary would lose its outer half to the next column's cell. Each
// boundary is covered by two bands instead, one in the head on either side,
// each built after that head's own cell and so above everything it overlaps.
// `leading` marks the band that reaches back over the boundary from the next
// column's head, which the drag handler reads off the payload's data to know
// which of its edges is the boundary.
static El* ResizeHandleBand(Ctx* cx, Str id, int col, bool leading,
                            Entity<TableState> state) {
    Arena* a = cx->a;
    El* e = Div(a)->PathClick(id)->FlexRow()->Cursor(CursorKind::ColResize);
    // on_drag(ResizeColumn((entity_id, ix))): the press picks the column up,
    // and every move afterwards carries it back to the handler.
    e->OnDrag(kTableResizeDrag, col, leading ? (void*)e : nullptr);
    e->OnDragMove(ListenTo(state, &TableState::OnResizeDrag));
    // on_mouse_up_out ends the drag. A release back over the band it started
    // on is not "out", so the handle listens for that one too — the handler
    // does nothing when no drag is going on.
    e->OnMouseUpOut(ListenTo(state, &TableState::OnResizeEnd));
    e->OnMouseUp(ListenTo(state, &TableState::OnResizeEnd));
    return e;
}

// render_resize_handle: the half of column `col`'s resize handle that lies
// inside column `col`, along with the hairline that marks the boundary. It
// is HANDLE_PADDING wide, like the half on the far side, so the grab area is
// symmetric about the boundary. The negative margin keeps the band's net
// contribution to the header row zero, and `justify_end` pins the hairline
// to the column edge.
static El* ResizeHandle(Ctx* cx, Str id, int col, Entity<TableState> state) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    TableState* s = state.Get(cx);
    bool on = s && s->resizingCol == col;
    // Nothing sets a height: a flex row stretches its children, so the handle
    // and the line down it are as tall as the head, which is what h_full says
    // in Rust.
    El* e = ResizeHandleBand(cx, id, col, false, state)
                ->W(kTableResizeHandlePadding)
                ->MarginL(-kTableResizeHandlePadding)
                ->JustifyEnd()
                ->ItemsCenter();
    // group_hover: the line under the pointer, or under a drag, is the darker
    // of the two borders, and runs the full height while it is.
    e->Child(Div(a)->W(1)->H(kFill)->Bg(on ? th.border : th.tableRowBorder));
    e->HoverBg(th.border);
    return e;
}

// render_leading_resize_handle: the other half of that band, for the
// boundary on the *left* of the head it sits in — the one owned by the
// column before it. It is positioned absolutely so that reaching back over
// the boundary costs the header row no width, and it is built after this
// column's own cell so that it, and not a column reorder, receives the drag.
static El* LeadingResizeHandle(Ctx* cx, Str id, int prevCol,
                               Entity<TableState> state) {
    return ResizeHandleBand(cx, id, prevCol, true, state)
        ->Absolute()
        ->Top(0)
        ->Left(0)
        ->H(kFill)
        ->W(kTableResizeHandlePadding);
}

// The pane a column is drawn in: the pinned one holds the leading columns
// that asked for it, and everything else scrolls.
static int FixedColCount(const DataTable* t, const TableState* s) {
    if (!s || !s->colFixed) {
        return 0;
    }
    int n = 0;
    for (int d = 0; d < t->nColumns; d++) {
        if (!t->columns[TableColAt(s, d)].fixed) {
            break;
        }
        n++;
    }
    return n;
}

// One band of an upper head row. The width is the sum of the columns under
// it, so it follows them when one is dragged wider.
static El* GroupBand(Ctx* cx, Str label, float w, bool last) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* e = Div(a)->FlexRow()->W(w)->Shrink0()->JustifyCenter()->ItemsCenter();
    if (!last) {
        e->BorderR(1, th.border);
    }
    if (label.s) {
        e->Child(
            TextEl(a, label)->Font(16)->Fg(th.foreground)->LineHeight(1.f));
    }
    return e;
}

DataTable* DataTable::ContextMenu(PopupMenu* (*fn)(Ctx*, void*, int,
                                                   PopupMenu*)) {
    contextMenu = fn;
    return this;
}

// table/loading.rs. The loading view is a table rather than a stack of bars:
// a head row on the head colour and four rows under it, each holding three
// skeletons on the left and one on the right, at half the row height. That
// shape is the point — it stands where the real table will, so the layout
// does not jump when the rows arrive.
static El* LoadingRow(Ctx* cx, UiSize size, bool header) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    Edges pad = UiTableCellPadding(size);
    float rowH = UiTableRowHeight(size);
    float barH = rowH * 0.5f;
    auto bar = [&](float w) {
        Skeleton* sk = Skeleton::New(cx)->W(w)->H(barH);
        if (header) {
            sk->Secondary();
        }
        return sk->IntoEl();
    };
    El* row = Div(a)
                  ->FlexRow()
                  ->W(kFill)
                  ->Gap(Rems(cx, 0.75f))
                  ->H(rowH)
                  ->ClipX()
                  ->PadT(pad.top)
                  ->PadB(pad.bottom)
                  ->PadL(pad.left)
                  ->PadR(pad.right)
                  ->ItemsCenter()
                  ->JustifyBetween();
    if (header) {
        row->Bg(th.tokens.tableHead);
    } else {
        row->BorderT(1, th.tableRowBorder);
    }
    // w_24 / w_48 / w_16, which is what makes the three read as a name, a
    // description and a number rather than three of the same thing.
    row->Child(Div(a)
                   ->FlexRow()
                   ->Gap(Rems(cx, 0.75f))
                   ->Flex1()
                   ->ItemsCenter()
                   ->Child(bar(Rems(cx, 6)))
                   ->Child(bar(Rems(cx, 12)))
                   ->Child(bar(Rems(cx, 4))));
    row->Child(bar(Rems(cx, 6)));
    return row;
}

static El* LoadingView(Ctx* cx, UiSize size) {
    El* v = Div(cx->a)->FlexCol()->W(kFill);
    v->Child(LoadingRow(cx, size, true));
    for (int i = 0; i < 4; i++) {
        v->Child(LoadingRow(cx, size, false));
    }
    return v;
}

// render_last_empty_col: h_flex().w_3().h_full().flex_shrink_0(), which is
// the blank past the last column. It sits on the scrolling side only — the
// pinned pane ends where its columns do.
static El* LastEmptyColEl(Ctx* cx, El* (*fn)(Ctx*, void*), void* data) {
    if (fn) {
        El* e = fn(cx, data);
        if (e) {
            return e;
        }
    }
    return Div(cx->a)->FlexRow()->W(Rems(cx, 0.75f))->Shrink0();
}

// `.context_menu(..)` on the inner table: the menu is built from the row the
// last secondary press marked, and the wrapper is what catches the press —
// so it has to be there before there is a row, which is why the menu is
// built empty when nothing is marked. A menu with no items renders nothing,
// which is the trait default answering the menu back untouched.
static El* WrapContextMenu(Ctx* cx, Str id, El* box, const TableState* s,
                           PopupMenu* (*build)(Ctx*, void*, int, PopupMenu*),
                           void* data) {
    Arena* a = cx->a;
    // Everything the wrapper builds is the table's, so the table's name goes
    // on the stack of ids around it and the menu's state is named by its
    // place inside. The wrapper element itself keeps the qualified name, and
    // that is what Rust does too: `ContextMenuExt::context_menu` builds its
    // id as `format!("context-menu-{:?}", id)` off the element it wraps,
    // because the wrapper sits *above* that element and has nothing named
    // over it to fold under.
    IdScope scope(cx, id);
    PopupMenu* menu = PopupMenu::New(cx, StrL("ctx-menu"));
    if (s && s->rightClickedRow >= 0) {
        menu = build(cx, data, s->rightClickedRow, menu);
    }
    if (!menu) {
        return box;
    }
    El* wrap = Div(a)->FlexCol()->W(kFill)->Child(box);
    return ContextMenu::New(cx, StrDup(a, fmt("%s-ctx", id)))
        ->Child(wrap)
        ->Menu(menu)
        ->IntoEl();
}

// The table's uniform_list virtualizes at prepaint from the bounds layout
// gave it. The body here is built before layout with the height the box had
// last frame; a box laid out at another height builds the table again, with
// that height (UseLaidOutHeight now answers it), and takes its parts.
static bool DataTableAt(void* data, Ctx* cx, El* box, float) {
    auto* table = (DataTable*)data;
    table->cx = cx;
    El* fresh = table->IntoEl();
    if (!fresh) {
        return false;
    }
    box->first = fresh->first;
    box->last = fresh->last;
    return true;
}

El* DataTable::IntoEl() {
    El* box = BuildEl();
    if (!contextMenu) {
        return box;
    }
    return WrapContextMenu(cx, id, box, state.Get(cx), contextMenu, data);
}

// render_row_header_cell: `w_3`, a rule down its right edge and the head's
// own surface. On a body row it takes a click and selects that row, which is
// the whole reason it is there; the one in the head does nothing.
static El* RowHeaderCell(Ctx* cx, Entity<TableState> state, int row,
                         bool head) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* e = Div(a)
                ->W(Rems(cx, 0.75f))
                ->H(kFill)
                ->Shrink0()
                ->BorderR(1, th.tableRowBorder)
                ->Bg(th.tokens.tableHead);
    TableState* s = state.Get(cx);
    if (head || !s || !s->rowSelectable) {
        return e;
    }
    // selected_row() is None outside row mode, so a selected cell or column
    // never highlights its row.
    if (TableSelectedRow(s) == row) {
        e->Bg(th.tokens.tableActive);
    }
    BindPathClick(e, ElementIdNamed(a, StrL("row-header"), (uint64_t)row),
                  ListenTo(state, &TableState::OnRowClick, (int64_t)row));
    return e;
}

El* DataTable::BuildEl() {
    const Theme& th = ThemeNow(cx->app);
    if (hasDelegate) {
        data = delegate.data;
        nColumns = delegate.columnsCount ? delegate.columnsCount(cx, data) : 0;
        nRows = delegate.rowsCount ? delegate.rowsCount(cx, data) : 0;
        if (nColumns > 0) {
            TableColumn* generated =
                (TableColumn*)Alloc(a, nColumns * (int)sizeof(TableColumn));
            for (int c = 0; c < nColumns; c++) {
                generated[c] = delegate.column ? delegate.column(cx, data, c)
                                               : TableColumn{};
            }
            columns = generated;
        } else {
            columns = nullptr;
        }
        cell = delegate.renderTd;
        contextMenu = delegate.contextMenu;
        lastEmptyCol = delegate.renderLastEmptyCol;
        visibleRowsChanged = delegate.visibleRowsChanged;
        visibleColsChanged = delegate.visibleColumnsChanged;
        cellText = delegate.cellText;
        if (delegate.groupHeaders) {
            delegate.groupHeaders(cx, data, this);
        }
    }
    TableState* s = state.Get(cx);
    if (s) {
        s->self = state;
        // The counts are the caller's every frame, which is what keeps the
        // keys inside the rows and columns there actually are.
        s->rowCount = nRows;
        s->colCount = nColumns;
        s->rowH = rowHeight;
        if (hasDelegate) {
            s->delegateData = data;
            s->delegateSort = delegate.performSort;
            s->delegateMoveColumn = delegate.moveColumn;
            s->delegateLoadMore = delegate.loadMore;
            s->loading = delegate.loading && delegate.loading(cx, data);
            s->hasMore = delegate.hasMore && delegate.hasMore(cx, data);
            s->loadMoreThreshold = delegate.loadMoreThreshold
                                       ? delegate.loadMoreThreshold(data)
                                       : 20;
        } else {
            s->delegateData = nullptr;
            s->delegateSort = nullptr;
            s->delegateMoveColumn = nullptr;
            s->delegateLoadMore = nullptr;
        }
        TableSeedColOrder(s, nColumns);
        for (int c = 0; c < nColumns; c++) {
            TableSeedColWidth(s, c, columns[c].width);
            TableSetColConstraints(s, c, columns[c].minWidth,
                                   columns[c].maxWidth);
            if (s->sortCol < 0 && columns[c].hasSort &&
                columns[c].sort != ColumnSort::Default) {
                s->sortCol = c;
                s->sort = columns[c].sort;
            }
        }
    }
    int nFixed = FixedColCount(this, s);
    if (s) {
        // fixed_left_cols_count, written down so a scroll between frames can
        // read it — the columns are the caller's and a click has none.
        s->fixedCols = nFixed;
    }
    float scrollX = s ? s->scrollX : 0;

    El* box = gpui::Table::New(cx, id, nRows, nColumns)->FlexCol()->W(kFill);
    if (bordered) {
        box->Radius(th.radius)->Border(1, th.border);
    }
    // The body's height. GPUI's table is `size_full()` and its uniform_list
    // virtualizes at prepaint from the bounds layout gave it; the rows here
    // are built before layout, so the body is what the table's box held last
    // frame less its border and the head rows (UseLaidOutHeight), and a box
    // laid out at another height builds the table again at prepaint
    // (DataTableAt). A fixed
    // height is the first frame's and keeps the box to it; without one the
    // box fills, and the first frame builds a window's worth of rows — or,
    // with no window to ask, every row.
    bool fill = h <= 0;
    if (fill) {
        box->H(kFill);
    }
    float headsH = (float)(groupHeaders.len + 1) * rowHeight;
    float first = h > 0 ? h : (cx->win ? WindowSize(cx->win).dipH : 0.f);
    LaidOutHeight* laid = UseLaidOutHeight(cx, id, first);
    if (laid) {
        laid->contentBox = true;
        laid->inset = headsH;
    }
    float viewH = laid ? laid->built : first;
    TrackLaidOutHeight(cx, box, laid, &DataTableAt, this);

    // render_loading stands in for the whole table, head and all — its first
    // row is the fake head, which is why that row is painted the head colour.
    // Rust puts it beside `inner_table` and builds only one of the two.
    if (s && s->loading) {
        El* loading = hasDelegate && delegate.renderLoading
                          ? delegate.renderLoading(cx, data, size)
                          : nullptr;
        box->Child(loading ? loading : LoadingView(cx, size));
        return box;
    }

    // The table is two panes side by side: the pinned columns, which only
    // ever move down, and the rest, which move both ways under a head that
    // goes with them. Rust gets the same split out of one element tree by
    // giving the fixed columns an offset of their own; two panes are the
    // shape that falls out of a tree with no such offset in it.
    El* main = Div(a)->FlexRow()->W(kFill)->ItemsStart();
    El* fixedPane = Div(a)->FlexCol()->Shrink0();
    El* scrollPane = Div(a)->FlexCol()->Flex1()->ClipX();
    // render_row_header_cell: the narrow strip down the left a cell-selecting
    // table picks whole rows with. It never scrolls sideways, so it belongs
    // to the fixed pane — which is then there whether or not any column is.
    bool rowHeaderOn = s && s->cellSelectable && s->rowHeader;
    if (nFixed > 0 || rowHeaderOn) {
        main->Child(fixedPane);
    }
    main->Child(scrollPane);
    if (s) {
        // The clipping box, which is the width a column is on screen or not
        // against. Read next frame, the way any laid-out box is.
        scrollPane->BoundsOut(&s->bodyBounds);
    }
    box->Child(main);

    // A sideways-slid band that is not itself a scroll target: the wheel
    // dispatcher only offers the event to a box with a handler, so a head
    // with an offset and no handler simply follows the body.
    auto follow = [&](El* e) {
        e->ScrollX(scrollX);
        e->noScrollbar = true;
        return e;
    };

    for (int g = 0; g < groupHeaders.len; g++) {
        // Every head row is one table row tall — state.rs renders the group
        // bands and the leaf heads through the same `h(table_row_height())`,
        // which is what makes the head block a whole number of rows.
        El* gf =
            Div(a)->FlexRow()->Shrink0()->H(rowHeight)->BorderB(1, th.border);
        // The band fills the pane's width; it must not be a flex item along
        // the pane's own axis, which is down. A basis of zero there would
        // leave the band out of the pane's intrinsic height and the body
        // would give that height back by shrinking.
        El* gsWrap = follow(
            Div(a)->FlexRow()->W(kFill)->H(rowHeight)->BorderB(1, th.border));
        El* gs = Div(a)->FlexRow()->Shrink0();
        gsWrap->Child(gs);
        int col = 0;
        const TableGroupCell* cells = groupHeaders[g].cells;
        for (int i = 0; i < groupHeaders[g].n; i++) {
            int span = cells[i].span;
            float w = 0;
            for (int k = 0; k < span && col + k < nColumns; k++) {
                w += ColWidth(s, TableColAt(s, col + k));
            }
            bool last = i == groupHeaders[g].n - 1;
            // A band that ends where the pinned columns do belongs to that
            // pane; one that straddles the seam rides with the scrolling
            // side, which is where most of it is.
            El* band =
                hasDelegate && delegate.renderGroupTh
                    ? delegate.renderGroupTh(cx, data, cells[i].label, span, w)
                    : nullptr;
            if (!band) {
                band = GroupBand(cx, cells[i].label, w, last);
            } else {
                band->W(w)->H(kFill)->Shrink0();
            }
            if (col + span <= nFixed) {
                gf->Child(band);
            } else {
                gs->Child(band);
            }
            col += span;
        }
        fixedPane->Child(gf);
        scrollPane->Child(gsWrap);
    }

    Listener headClick = ListenTo(state, &TableState::OnHeadClick, 0);
    Listener sortClick = ListenTo(state, &TableState::OnSortClick, 0);
    // render_table_head: the head row is on table_head, in
    // table_head_foreground, pinned pane and scrolling pane alike.
    El* headFixed = gpui::TableHeader::New(cx, StrL("head-fixed"))
                        ->FlexRow()
                        ->Shrink0()
                        ->H(rowHeight)
                        ->Bg(th.tokens.tableHead)
                        ->Fg(th.tableHeadFg)
                        ->BorderB(1, th.border);
    El* headWrap = follow(Div(a)
                              ->FlexRow()
                              ->W(kFill)
                              ->H(rowHeight)
                              ->Bg(th.tokens.tableHead)
                              ->Fg(th.tableHeadFg)
                              ->BorderB(1, th.border));
    El* headScroll = hasDelegate && delegate.renderHeader
                         ? delegate.renderHeader(cx, data)
                         : nullptr;
    if (!headScroll) {
        headScroll = gpui::TableHeader::New(cx, StrL("head"));
    }
    headScroll->PathClick(StrL("head"))
        ->Role(AccessibilityRole::Row)
        ->FlexRow()
        ->Shrink0();
    headWrap->Child(headScroll);
    // Every column's slot in one go: `BoundsOut` keeps a pointer into the
    // array, so it must not grow again while the heads are being built.
    if (s) {
        TableEnsureCols(s, nColumns);
    }
    if (rowHeaderOn) {
        headFixed->Child(RowHeaderCell(cx, state, 0, true));
    }
    for (int d = 0; d < nColumns; d++) {
        // The columns are drawn in the order the table keeps, which is what a
        // head drag rewrites.
        int c = s ? TableColAt(s, d) : d;
        const TableColumn& col = columns[c];
        Str colLabel = TableColumnLabel(col);
        El* th_ = gpui::TableHead::New(cx, StrDup(a, fmt("th-%d", c)), c + 1)
                      ->AriaLabel(colLabel)
                      ->FlexRow()
                      ->Shrink0()
                      ->W(ColWidth(s, c));
        if (s) {
            // The box a drop position is worked out against, which Rust reads
            // off its col_groups.
            th_->BoundsOut(&s->colBounds[d]);
        }
        // No rule between two heads of its own: render_th draws none, and the
        // hairline a resizable column shows is its resize handle's.
        // The gap the dragged head would drop into, drawn down the edge it
        // would land on.
        if (s && s->dropGap == d) {
            th_->BorderL(2, th.primary);
        } else if (s && s->dropGap == d + 1 && d == nColumns - 1) {
            th_->BorderR(2, th.primary);
        }
        if (s && TableSelectedCol(s) == c) {
            th_->Bg(th.tokens.accent);
        }
        // render_th: the head is the content and the resize handle beside it,
        // and only the content carries the padding — the handle has to reach
        // the column's edge.
        El* content =
            Div(a)->FlexRow()->Flex1()->ItemsCenter()->JustifyBetween();
        TableColumnPadding(content, col);
        if (!col.sortable) {
            if (col.center) {
                content->JustifyCenter();
            } else if (col.right) {
                content->JustifyEnd();
            }
        }
        El* customHead = hasDelegate && delegate.renderTh
                             ? delegate.renderTh(cx, data, c)
                             : nullptr;
        content->Child(customHead ? customHead
                                  : TextEl(a, colLabel)
                                        ->Font(UiTableCellFontPx(size) > 0
                                                   ? UiTableCellFontPx(size)
                                                   : 14.f)
                                        ->LineHeight(1.f));
        if (col.selectable) {
            BindPathClick(content,
                          ElementIdNamed(a, StrL("col-header"), (uint64_t)c),
                          ListenerArg(headClick, c));
        }
        // on_drag(DragColumn(..)): a press on the head picks the column up,
        // and the whole head is the drop target for another one.
        if (s && s->colMovable && col.movable) {
            content->OnDrag(kTableColDrag, d);
            content->OnDragMove(ListenTo(state, &TableState::OnColDragMove));
            content->OnMouseUpOut(ListenTo(state, &TableState::OnColDragEnd));
            content->OnMouseUp(ListenTo(state, &TableState::OnColDragEnd));
            th_->OnDrop(kTableColDrag, ListenTo(state, &TableState::OnColDrop));
        }
        if (col.sortable && s && s->sortable) {
            // The sort icon is its own hit box. The click still bubbles to
            // the head, so it sorts and selects the column. Role and label
            // are what a screen reader and a UI test use to find it.
            El* icon = SortIcon(cx, th, TableSortOf(s, c));
            icon->Role(AccessibilityRole::Button)
                ->AriaLabel(SortByLabel(colLabel));
            BindPathClick(icon,
                          ElementIdNamed(a, StrL("icon-sort"), (uint64_t)c),
                          ListenerArg(sortClick, c));
            content->Child(icon);
        }
        th_->Child(content);
        // resize handle cell right side
        if (s && s->colResizable && col.resizable) {
            th_->Child(ResizeHandle(
                cx, StrDup(a, fmt("resizable-handle-%d", c)), c, state));
        }
        // resize handle cell left side: is_col_resizable(ix - 1), the column
        // shown before this one.
        if (s && s->colResizable && d > 0) {
            int prev = TableColAt(s, d - 1);
            if (columns[prev].resizable) {
                th_->Child(LeadingResizeHandle(
                    cx, StrDup(a, fmt("resizable-handle-leading-%d", c)), prev,
                    state));
            }
        }
        (d < nFixed ? headFixed : headScroll)->Child(th_);
    }
    headScroll->Child(LastEmptyColEl(cx, lastEmptyCol, data));
    fixedPane->Child(headFixed);
    scrollPane->Child(headWrap);

    // render_empty: a table with no rows shows this instead of a body.
    if (nRows == 0) {
        El* delegateEmpty = hasDelegate && delegate.renderEmpty
                                ? delegate.renderEmpty(cx, data)
                                : nullptr;
        scrollPane->Child(
            delegateEmpty ? delegateEmpty
            : empty       ? empty
                    : Div(a)
                          ->FlexCol()
                          ->W(kFill)
                          ->H(viewH > 0 ? viewH : 160)
                          ->ItemsCenter()
                          ->JustifyCenter()
                          ->Child(IconEl(a, IconName::Inbox, Rems(cx, 3))
                                      ->Fg(RgbaOpacity(th.mutedFg, 0.6f))));
        return box;
    }

    Listener rowClick = ListenTo(state, &TableState::OnRowClick, 0);
    Listener rowDown = ListenTo(state, &TableState::OnRowMouseDown, 0);
    Listener cellClick = ListenTo(state, &TableState::OnCellClick, 0);
    Listener cellDown = ListenTo(state, &TableState::OnCellMouseDown, 0);
    El* bodyFixed =
        gpui::TableBody::New(cx, StrL("body-fixed"))->FlexCol()->Shrink0();
    El* bodyScroll =
        gpui::TableBody::New(cx, StrL("body"))->FlexCol()->W(kFill);
    // The rows are virtualized when the caller gave the body a height: only
    // the ones it can show are built, with a spacer at each end standing in
    // for the rest. Without one every row is built, which is what a short
    // table wants.
    VirtualRange range = {0, nRows};
    if (s && viewH > 0) {
        s->viewportH = viewH;
        range = VirtualListVisibleRows(nRows, s->rowH, s->scrollY, viewH);
        // ListSizingBehavior::Auto: a table that fills its box and has fewer
        // rows than it can show is as tall as its rows, unless stripes run
        // on under them.
        float bodyH = viewH;
        float rowsH = (float)nRows * s->rowH;
        if (fill && !stripe && rowsH < bodyH) {
            bodyH = rowsH;
        }
        // Both panes move down together off the one offset; only the wide one
        // takes the sideways wheel back.
        bodyFixed->H(bodyH)->ClipY()->ScrollY(s->scrollY)->ScrollFromPath();
        bodyFixed->OnScroll(ListenTo(state, &TableState::OnScroll));
        bodyFixed->noScrollbar = true;
        ScrollableMask::Apply(bodyFixed, Axis::Vertical);
        bodyScroll->H(bodyH)
            ->ClipY()
            ->ScrollY(s->scrollY)
            ->ScrollX(s->scrollX)
            ->ScrollFromPath()
            ->OnScroll(ListenTo(state, &TableState::OnScrollXY));
        // The two transparent ScrollableMask siblings in Rust collapse onto
        // this integrated scroll record. Horizontal gestures stay with the
        // table even at its edge; vertical ones chain to an outer scroller.
        ScrollableMask::Apply(bodyScroll, Axis::Horizontal);
        ScrollableMask::Apply(bodyScroll, Axis::Vertical);
        bodyScroll->noScrollbarY = !scrollbarV;
        bodyScroll->noScrollbarX = !scrollbarH;
        if (range.first > 0) {
            float pad = (float)range.first * s->rowH;
            bodyFixed->Child(Div(a)->H(pad));
            bodyScroll->Child(Div(a)->W(kFill)->H(pad));
        }
    }
    // update_visible_range_if_need. Rust hangs both off its virtual lists;
    // here the row range is what the body was built from and the column range
    // is worked out from the offset, since this tree builds every column.
    if (s) {
        if (TableVisibleRowsChanged(s, range.first, range.end) &&
            visibleRowsChanged) {
            visibleRowsChanged(cx, data, range.first, range.end);
        }
        int cFirst = 0, cEnd = 0;
        TableVisibleCols(s, &cFirst, &cEnd);
        if (TableVisibleColsChanged(s, cFirst, cEnd) && visibleColsChanged) {
            visibleColsChanged(cx, data, cFirst, cEnd);
        }
    }
    for (int r = range.first; r < range.end; r++) {
        El* rowFixed =
            gpui::TableRow::New(cx, StrDup(a, fmt("row-fixed-%d", r)), r + 1)
                ->FlexRow()
                ->Shrink0()
                ->BorderB(1, th.tableRowBorder);
        El* rowScroll = hasDelegate && delegate.renderTr
                            ? delegate.renderTr(cx, data, r)
                            : nullptr;
        if (!rowScroll) {
            rowScroll = gpui::TableRow::New(
                cx, ElementIdNamed(a, StrL("row"), (uint64_t)r), r + 1);
        }
        rowScroll->PathClick(ElementIdNamed(a, StrL("row"), (uint64_t)r))
            ->Role(AccessibilityRole::Row)
            ->AriaRowIndex(r + 1)
            ->FlexRow()
            ->Shrink0()
            ->BorderB(1, th.tableRowBorder);
        El* rows[2] = {rowFixed, rowScroll};
        for (El* row : rows) {
            if (s && viewH > 0) {
                // uniform_list: every row the same height, which is what lets
                // the two spacers stand in for the ones that were not built.
                row->H(s->rowH);
            }
            if (stripe && (r % 2) == 1) {
                row->Bg(th.tokens.tableEven);
            }
            if (s && TableSelectedRow(s) == r) {
                // state.rs paints the selected row the same way a list item
                // does, off the table's own pair of colors.
                ListActiveStyle sel = ListActiveStyleOf(
                    ListSettingsNow(cx->app), th.tokens.tableActive,
                    th.tableActiveBorder, th.tokens.accent, true);
                row->Bg(sel.bg);
            } else if (s && s->rightClickedRow == r) {
                row->Bg(BackgroundOpacity(th.tokens.accent, 0.5f));
            }
        }
        if (rowHeaderOn) {
            rowFixed->Child(RowHeaderCell(cx, state, r, false));
        }
        for (int d = 0; d < nColumns; d++) {
            int c = s ? TableColAt(s, d) : d;
            El* cellEl = cell ? cell(cx, data, r, c) : nullptr;
            Str cellId = StrDup(a, fmt("cell-%d-%d", r, c));
            El* td = gpui::TableCell::New(cx, cellId, c + 1)
                         ->FlexRow()
                         ->Shrink0()
                         ->W(ColWidth(s, c))
                         ->ItemsCenter();
            TableColumnPadding(td, columns[c]);
            // render_cell's table_cell_size: the text half of it, the Input
            // ladder. The padding stays the table's own, above.
            if (float font = UiTableCellFontPx(size)) {
                td->Font(font);
            }
            if (columns[c].right) {
                td->JustifyEnd();
            } else if (columns[c].center) {
                td->JustifyCenter();
            }
            if (s && TableSelectedCol(s) == c) {
                td->Bg(BackgroundOpacity(th.tokens.accent, 0.5f));
            }
            // The selected cell, painted the way the selected row is: the
            // table's own active pair rather than a plain accent block.
            if (s && s->rightClickedCellRow == r &&
                s->rightClickedCellCol == c) {
                td->Bg(BackgroundOpacity(th.tokens.accent, 0.5f));
            }
            int cellRow = -1;
            int cellCol = -1;
            if (s && TableSelectedCell(s, &cellRow, &cellCol) && cellRow == r &&
                cellCol == c) {
                ListActiveStyle sel = ListActiveStyleOf(
                    ListSettingsNow(cx->app), th.tokens.tableActive,
                    th.tableActiveBorder, th.tokens.accent, true);
                td->Bg(sel.bg);
            }
            // A cell takes the click when the table is cell-selectable, which
            // is what `SelectCell` and `DoubleClickedCell` come from.
            if (s && s->cellSelectable) {
                BindPathClick(td, cellId,
                              ListenerArg(cellClick, TableCellPack(r, c)));
                td->OnMouseDown(ListenerArg(cellDown, TableCellPack(r, c)));
            }
            if (cellEl) {
                // render_td clips what it holds to the column. Dragging an
                // edge in makes a column narrower than its text, and without
                // this the text would spill over the next column.
                float padding =
                    columns[c].hasPaddings
                        ? columns[c].paddings.left + columns[c].paddings.right
                        : 16.f;
                float inner = ColWidth(s, c) - padding;
                cellEl->MaxW(inner > 1 ? inner : 1)->Truncate();
                td->Child(cellEl);
            }
            (d < nFixed ? rowFixed : rowScroll)->Child(td);
        }
        rowScroll->Child(LastEmptyColEl(cx, lastEmptyCol, data));
        if (s && s->rowSelectable && !s->cellSelectable) {
            BindPathClick(rowScroll,
                          ElementIdNamed(a, StrL("row"), (uint64_t)r),
                          ListenerArg(rowClick, r));
            rowScroll->OnMouseDown(ListenerArg(rowDown, r));
            BindPathClick(rowFixed, StrDup(a, fmt("row-fixed-%d", r)),
                          ListenerArg(rowClick, r));
            rowFixed->OnMouseDown(ListenerArg(rowDown, r));
        }
        bodyFixed->Child(rowFixed);
        bodyScroll->Child(rowScroll);
    }
    if (s && viewH > 0 && range.end < nRows) {
        float pad = (float)(nRows - range.end) * s->rowH;
        bodyFixed->Child(Div(a)->H(pad));
        bodyScroll->Child(Div(a)->W(kFill)->H(pad));
    }
    // calculate_extra_rows_needed: a striped table whose rows do not fill the
    // body runs the stripes on to the bottom with empty rows — the whole rows
    // that fit under the last one, each bordered and striped by its index the
    // way a data row is. They only exist when every data row fits, so the
    // range above is all of them and nothing is virtualized away. Rust's fake
    // row is the delegate's `render_tr(ix)` past the end, a spacer as wide as
    // the columns and the last empty column; here it is a bare row, since a
    // delegate here indexes its data by the row it is handed.
    int extraRows =
        stripe && s && viewH > 0
            ? TableExtraRowsNeeded(viewH, (float)nRows * s->rowH, s->rowH)
            : 0;
    float colsW = 0;
    for (int d = nFixed; extraRows > 0 && d < nColumns; d++) {
        colsW += ColWidth(s, TableColAt(s, d));
    }
    for (int r = nRows; r < nRows + extraRows; r++) {
        El* fillFixed = Div(a)
                            ->Id(StrDup(a, fmt("row-fixed-%d", r)))
                            ->FlexRow()
                            ->W(kFill)
                            ->Shrink0()
                            ->H(s->rowH)
                            ->BorderB(1, th.tableRowBorder);
        El* fillScroll = Div(a)
                             ->Id(ElementIdNamed(a, StrL("row"), (uint64_t)r))
                             ->FlexRow()
                             ->Shrink0()
                             ->H(s->rowH)
                             ->BorderB(1, th.tableRowBorder)
                             ->Child(Div(a)->Shrink0()->H(kFill)->W(colsW))
                             ->Child(LastEmptyColEl(cx, lastEmptyCol, data));
        if (r % 2 == 1) {
            fillFixed->Bg(th.tokens.tableEven);
            fillScroll->Bg(th.tokens.tableEven);
        }
        bodyFixed->Child(fillFixed);
        bodyScroll->Child(fillScroll);
    }
    fixedPane->Child(bodyFixed);
    scrollPane->Child(bodyScroll);
    // load_more_if_need: the last row built is near the end, and the delegate
    // says there is more to come.
    if (s && TableShouldLoadMore(s, range.end)) {
        if (s->delegateLoadMore) {
            s->delegateLoadMore(cx, s->delegateData);
        }
        if (s->onLoadMore.IsValid()) {
            TableEvent ev = {TableEventKind::SelectRow, s->rowCount, -1};
            ListenerCall(cx->app, cx->win, s->onLoadMore, &ev);
        }
    }
    // data_table.rs declares its context on the element it tracks focus on,
    // and binds tab to the column walk there — which is why the window's
    // focus ring only takes a tab the table did not want.
    if (s) {
        if (!s->focus.IsValid()) {
            s->focus = FocusHandleNew(cx);
        }
        box->TrackFocus(s->focus);
    }
    // data_table.rs: `.when(focus_handle.is_focused(window) &&
    // window.last_input_was_keyboard(), focus_ring_style)`. Clicking a row
    // focuses the table too, so only keyboard focus shows it, as CSS
    // `:focus-visible` does; the runtime paints it only while focused.
    box->FocusRing(WindowLastInputWasKeyboard(cx->win))->FocusOnPress();
    TableBindKeys(cx, box, state);
    return box;
}

} // namespace component
} // namespace gpui
