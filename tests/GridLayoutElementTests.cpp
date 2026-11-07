/* crates/base/src/grid_layout/grid_layout.rs */

#include "Test.h"

static GridArea Area(const char* id, int col, int row, int colSpan,
                     int rowSpan) {
    return GridArea::New(Str(id)).Col(col).Row(row).ColSpan(colSpan).RowSpan(
        rowSpan);
}

static Entity<GridLayoutState> NewPair(App* app) {
    Entity<GridLayoutState> state = EntityNewState<GridLayoutState>(app);
    state.Get(app)->self = state;
    GridArea items[2] = {Area("a", 0, 0, 4, 2), Area("b", 4, 0, 4, 2)};
    state.Get(app)->DefaultAreas(items, 2);
    return state;
}

struct GridHarness {
    Entity<GridLayoutState> state;
    int reflow = -1;

    static El* Render(GridHarness* self, Ctx* cx) {
        GridLayout* grid = GridLayout::New(cx, StrL("grid"), self->state);
        grid->Gap(10);
        if (self->reflow >= 0) {
            grid->Reflow(self->reflow);
        }
        grid->Item(GridItem::New(cx, StrL("a"))->Child(Div(cx->a)->SizeFull()));
        grid->Item(GridItem::New(cx, StrL("b"))->Child(Div(cx->a)->SizeFull()));
        return Div(cx->a)->W(1190)->Child(grid->IntoEl());
    }
};

struct GridChangeLog {
    int changes = 0;

    static void OnEvent(GridChangeLog* self, Ctx*, const GridLayoutEvent* ev) {
        if (self && ev && ev->kind == GridLayoutEventKind::Change) {
            self->changes++;
        }
    }
};

struct GridSelectLog {
    int n = 0;
    bool has[4] = {};
    char id[4][8] = {};

    static void OnEvent(GridSelectLog* self, Ctx*, const GridLayoutEvent* ev) {
        if (!self || !ev || ev->kind != GridLayoutEventKind::Select ||
            self->n >= 4) {
            return;
        }
        int i = self->n++;
        self->has[i] = ev->hasId;
        self->id[i][0] = 0;
        if (ev->hasId && ev->id.s) {
            int n = len(ev->id);
            if (n > 7) {
                n = 7;
            }
            memcpy(self->id[i], ev->id.s, (size_t)n);
            self->id[i][n] = 0;
        }
    }
};

static Window* OpenGrid(App* app, Entity<GridLayoutState> state, int reflow,
                        Entity<GridChangeLog> log) {
    BaseInit(app);
    if (log.IsValid()) {
        SubscribeTo(app, state, log, &GridChangeLog::OnEvent);
    }
    Entity<GridHarness> view = EntityNew<GridHarness>(app);
    view.Get(app)->state = state;
    view.Get(app)->reflow = reflow;
    return TestWindowOpen(app, view, 1600, 600, 1);
}

static void Drag(Window* win, Point from, Point mid, Point to) {
    TestSimulateMouseDown(win, from);
    TestDraw(win);
    TestSimulateMouseMove(win, mid, true);
    TestDraw(win);
    TestSimulateMouseMove(win, to, true);
    TestDraw(win);
}

static void Release(Window* win, Point at) {
    TestSimulateMouseUp(win, at);
    TestDraw(win);
}

static const GridArea* PlaceOf(Entity<GridLayoutState> state, App* app,
                               const char* id) {
    return state.Get(app)->Area(Str(id));
}

static void DraggingAnItemMovesItAndCommitsOnRelease() {
    App* app = TestAppNew();
    Entity<GridLayoutState> state = NewPair(app);
    Entity<GridChangeLog> log = EntityNewState<GridChangeLog>(app);
    Window* win = OpenGrid(app, state, -1, log);

    Drag(win, {50, 30}, {60, 30}, {450, 30});
    utassert(PlaceOf(state, app, "a")->col == 0);
    utassert(log.Get(app)->changes == 0);

    Release(win, {450, 30});
    const GridArea* a = PlaceOf(state, app, "a");
    const GridArea* b = PlaceOf(state, app, "b");
    utassert(a->col == 4 && a->row == 0);
    utassert(b->col == 4 && b->row == 2);
    utassert(log.Get(app)->changes == 1);
    utassert(!state.Get(app)->ActiveItem(nullptr));
    TestAppFree(app);
}

static void DraggingTheCornerResizesAnItem() {
    App* app = TestAppNew();
    Entity<GridLayoutState> state = NewPair(app);
    Entity<GridChangeLog> log = EntityNewState<GridChangeLog>(app);
    Window* win = OpenGrid(app, state, -1, log);
    float rowHeight = Rems(nullptr, 2.5f);
    Point corner = {784, rowHeight * 2.f + 4.f};

    Drag(win, corner, {corner.x + 10, corner.y}, {corner.x + 200, corner.y});
    Release(win, {corner.x + 200, corner.y});

    const GridArea* b = PlaceOf(state, app, "b");
    utassert(b->col == 4 && b->colSpan == 6 && b->rowSpan == 2);
    utassert(PlaceOf(state, app, "a")->col == 0);
    utassert(log.Get(app)->changes == 1);
    TestAppFree(app);
}

static void EscapeCancelsADrag() {
    App* app = TestAppNew();
    Entity<GridLayoutState> state = NewPair(app);
    Entity<GridChangeLog> log = EntityNewState<GridChangeLog>(app);
    Window* win = OpenGrid(app, state, -1, log);

    Drag(win, {50, 30}, {60, 30}, {450, 30});
    utassert(state.Get(app)->ActiveItem(nullptr));
    TestSimulateKeystrokes(win, "escape");
    TestDraw(win);
    Release(win, {450, 30});

    utassert(PlaceOf(state, app, "a")->col == 0);
    utassert(log.Get(app)->changes == 0);
    TestAppFree(app);
}

static void ArrowKeysMoveTheClickedItemAndTabLeavesTheGrid() {
    App* app = TestAppNew();
    Entity<GridLayoutState> state = NewPair(app);
    Entity<GridChangeLog> log = EntityNewState<GridChangeLog>(app);
    Window* win = OpenGrid(app, state, -1, log);

    TestSimulateClick(win, {50, 30});
    TestDraw(win);
    TestSimulateKeystrokes(win, "right shift-down");
    TestDraw(win);

    const GridArea* a = PlaceOf(state, app, "a");
    utassert(a->col == 1 && a->rowSpan == 3);
    utassert(log.Get(app)->changes == 2);

    TestSimulateKeystrokes(win, "tab");
    Str selected = {};
    utassert(state.Get(app)->SelectedItem(&selected) && StrEq(selected, "b"));
    TestSimulateKeystrokes(win, "tab");
    utassert(state.Get(app)->SelectedItem(&selected) && StrEq(selected, "b"));
    TestAppFree(app);
}

static void SelectingAnItemIsAnnouncedOncePerChange() {
    App* app = TestAppNew();
    Entity<GridLayoutState> state = NewPair(app);
    Window* win = OpenGrid(app, state, -1, {});
    Entity<GridSelectLog> log = EntityNewState<GridSelectLog>(app);
    SubscribeTo(app, state, log, &GridSelectLog::OnEvent);

    TestSimulateClick(win, {50, 30});
    TestDraw(win);
    TestSimulateClick(win, {60, 30});
    TestDraw(win);
    TestSimulateKeystrokes(win, "tab");
    TestSimulateKeystrokes(win, "tab");
    Ctx cx;
    cx.app = app;
    cx.win = win;
    cx.self = state.id;
    state.Get(app)->SetSelectedItem(&cx, true, StrL("a"));

    GridSelectLog* seen = log.Get(app);
    utassert(seen->n == 2);
    utassert(seen->has[0] && StrEq(Str(seen->id[0]), "a"));
    utassert(seen->has[1] && StrEq(Str(seen->id[1]), "b"));
    TestAppFree(app);
}

static void AReflowedGridShowsACopyAndEditsNothing() {
    App* app = TestAppNew();
    Entity<GridLayoutState> state = NewPair(app);
    Entity<GridChangeLog> log = EntityNewState<GridChangeLog>(app);
    int before[2][4];
    const char* ids[2] = {"a", "b"};
    for (int i = 0; i < 2; i++) {
        const GridArea* area = PlaceOf(state, app, ids[i]);
        before[i][0] = area->col;
        before[i][1] = area->row;
        before[i][2] = area->colSpan;
        before[i][3] = area->rowSpan;
    }
    Window* win = OpenGrid(app, state, 1, log);

    Drag(win, {50, 30}, {60, 30}, {450, 200});
    Release(win, {450, 200});
    TestSimulateKeystrokes(win, "right shift-down");
    TestDraw(win);

    for (int i = 0; i < 2; i++) {
        const GridArea* area = PlaceOf(state, app, ids[i]);
        utassert(area->col == before[i][0] && area->row == before[i][1] &&
                 area->colSpan == before[i][2] &&
                 area->rowSpan == before[i][3]);
    }
    utassert(log.Get(app)->changes == 0);
    Str selected = {};
    utassert(state.Get(app)->SelectedItem(&selected) && StrEq(selected, "a"));
    TestAppFree(app);
}

static El* EmptyPart(Ctx* cx, void*) {
    return Div(cx->a);
}

static void TestGridLayoutBuilder() {
    App* app = TestAppNew();
    Arena* arena = ArenaNew();
    Ctx cx;
    cx.app = app;
    cx.a = arena;
    Entity<GridLayoutState> state = EntityNewState<GridLayoutState>(app);
    StateStyle dragging;
    dragging.Opacity(0.5f);
    StateStyle selected;
    selected.Opacity(0.75f);
    StateStyle focused;
    focused.Opacity(0.6f);
    GridLayout* grid =
        GridLayout::New(&cx, StrL("grid"), state)
            ->RowHeight(3)
            ->GapX(8)
            ->GapY(12)
            ->DraggingItem(dragging)
            ->SelectedItem(selected)
            ->FocusedItem(focused)
            ->Reflow(0)
            ->Placeholder(EmptyPart, nullptr)
            ->ResizeHandle(EmptyPart, nullptr)
            ->Item(GridItem::New(&cx, StrL("a"))->DragHandleOnly(true))
            ->Item(GridItem::New(&cx, StrL("b")))
            ->Item(GridItem::New(&cx, StrL("c"))->Pad(8, 8));
    utassert(grid->nItems == 3);
    utassert(grid->items->item.handleOnly);
    utassertnear(grid->rowHeightRems, 3.f);
    utassert(grid->draggingItem.Has(StateFieldOpacity));
    utassertnear(grid->draggingItem.style.opacity, 0.5f);
    utassertnear(grid->selectedItem.style.opacity, 0.75f);
    utassertnear(grid->focusedItem.style.opacity, 0.6f);
    utassert(grid->hasReflow && grid->reflowCols == 1);
    GridItemNode* c = grid->items->next->next;
    utassert(c->item.style.Has(StateFieldPad) && c->item.style.style.pad
                                                         .left > 0);
    utassert(grid->placeholder != nullptr && grid->resizeHandle != nullptr);
    float gx = -1;
    float gy = -1;
    grid->ResolvedGap(16, &gx, &gy);
    utassertnear(gx, 8.f);
    utassertnear(gy, 12.f);
    ArenaDelete(arena);
    TestAppFree(app);
}

void TestGridLayoutElement() {
    DraggingAnItemMovesItAndCommitsOnRelease();
    DraggingTheCornerResizesAnItem();
    EscapeCancelsADrag();
    ArrowKeysMoveTheClickedItemAndTabLeavesTheGrid();
    SelectingAnItemIsAnnouncedOncePerChange();
    AReflowedGridShowsACopyAndEditsNothing();
    TestGridLayoutBuilder();
}
