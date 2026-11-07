/* crates/base/src/grid_layout/state.rs */

#include "Test.h"

static GridArea Area(const char* id, int col, int row, int colSpan,
                     int rowSpan) {
    return GridArea::New(Str(id)).Col(col).Row(row).ColSpan(colSpan).RowSpan(
        rowSpan);
}

static Ctx StateCx(App* app, Entity<GridLayoutState> state) {
    Ctx cx;
    cx.app = app;
    cx.self = state.id;
    return cx;
}

static Entity<GridLayoutState> NewGrid(App* app) {
    Entity<GridLayoutState> state = EntityNewState<GridLayoutState>(app);
    state.Get(app)->self = state;
    GridArea items[2] = {Area("a", 0, 0, 4, 2), Area("b", 4, 0, 4, 2)};
    state.Get(app)->DefaultAreas(items, 2);
    return state;
}

static void MetricsShareTheWidthBetweenColumnsAndGaps() {
    GridMetrics metrics = GridMetrics::New(1190.f, 12, 40.f, 10.f, 20.f);
    utassertnear(metrics.columnWidth, 90.f);
    utassertnear(metrics.Left(1), 100.f);
    utassertnear(metrics.SpanWidth(12), 1190.f);
    utassertnear(metrics.Top(1), 60.f);
    utassertnear(metrics.SpanHeight(2), 100.f);
    utassert(metrics.ColumnAt(140.f) == 1);
    utassert(metrics.ColumnAt(160.f) == 2);
    utassert(metrics.ColumnAt(-50.f) == 0);
    utassert(metrics.ColumnsSpanning(metrics.SpanWidth(3) + 20.f) == 3);
    utassert(metrics.RowsSpanning(0.f) == 1);
}

static void TestGridLayoutStateBuilder() {
    App* app = TestAppNew();
    Entity<GridLayoutState> state = EntityNewState<GridLayoutState>(app);
    GridArea items[1] = {Area("a", 4, 5, 4, 1)};
    GridLayoutState* grid = state.Get(app);
    grid->Cols(6)
        ->Compaction(false, Axis::Vertical)
        ->PreventCollision(true)
        ->Draggable(false)
        ->Resizable(false)
        ->DefaultAreas(items, 1);
    utassert(grid->ColumnCount() == 6);
    utassert(!grid->HasCompaction());
    utassert(grid->IsCollisionPrevented());
    utassert(!grid->IsDraggable());
    utassert(!grid->IsResizable());
    const GridArea* area = grid->Area(StrL("a"));
    utassert(area != nullptr);
    if (area) {
        utassert(area->col == 2 && area->row == 5);
    }
    TestAppFree(app);
}

struct GridEventLog {
    int changes = 0;
    int bColSpan = -1;
    int bRowSpan = -1;

    static void OnEvent(GridEventLog* self, Ctx*, const GridLayoutEvent* ev) {
        if (!self || !ev || ev->kind != GridLayoutEventKind::Change) {
            return;
        }
        self->changes++;
        for (int i = 0; i < ev->nAreas; i++) {
            if (StrEq(ev->areas[i].id, "b")) {
                self->bColSpan = ev->areas[i].colSpan;
                self->bRowSpan = ev->areas[i].rowSpan;
            }
        }
    }
};

static void ProgrammaticChangesEmitTheNewLayout() {
    App* app = TestAppNew();
    Entity<GridLayoutState> state = NewGrid(app);
    Entity<GridEventLog> log = EntityNewState<GridEventLog>(app);
    SubscribeTo(app, state, log, &GridEventLog::OnEvent);
    Ctx cx = StateCx(app, state);
    GridLayoutState* grid = state.Get(app);
    utassert(grid->MoveItem(&cx, StrL("a"), 8, 0));
    utassert(!grid->MoveItem(&cx, StrL("a"), 8, 0));
    utassert(grid->ResizeItem(&cx, StrL("b"), 2, 3));
    GridArea replaced[1] = {Area("c", 0, 0, 1, 1)};
    grid->SetAreas(&cx, replaced, 1);
    GridEventLog* seen = log.Get(app);
    utassert(seen->changes == 2);
    utassert(seen->bColSpan == 2 && seen->bRowSpan == 3);
    TestAppFree(app);
}

static void KeyboardCommandsActOnTheSelectedItem() {
    App* app = TestAppNew();
    Entity<GridLayoutState> state = NewGrid(app);
    Ctx cx = StateCx(app, state);
    GridLayoutState* grid = state.Get(app);
    utassert(!grid->NudgeSelected(&cx, 1, 0));
    utassert(grid->SelectAdjacent(&cx, true));
    Str selected = {};
    utassert(grid->SelectedItem(&selected) && StrEq(selected, "a"));
    utassert(grid->SelectAdjacent(&cx, true));
    utassert(grid->SelectedItem(&selected) && StrEq(selected, "b"));
    utassert(!grid->SelectAdjacent(&cx, true));
    utassert(grid->SelectedItem(&selected) && StrEq(selected, "b"));

    utassert(grid->NudgeSelected(&cx, 1, 0));
    utassert(grid->Area(StrL("b"))->col == 5);
    utassert(!grid->NudgeSelected(&cx, 0, -1));

    utassert(grid->ResizeSelected(&cx, 0, 1));
    utassert(grid->Area(StrL("b"))->rowSpan == 3);
    utassert(grid->IsKeyboardActive());

    grid->SetDraggable(&cx, false);
    utassert(!grid->NudgeSelected(&cx, -1, 0));

    grid->SetSelectedItem(&cx, true, StrL("a"));
    utassert(!grid->SelectAdjacent(&cx, false));
    TestAppFree(app);
}

void TestGridLayoutState() {
    TestSuite("grid_layout::state");
    MetricsShareTheWidthBetweenColumnsAndGaps();
    TestGridLayoutStateBuilder();
    ProgrammaticChangesEmitTheNewLayout();
    KeyboardCommandsActOnTheSelectedItem();
}
