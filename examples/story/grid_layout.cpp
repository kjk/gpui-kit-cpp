#include "Story.h"

#include <stdio.h>

struct GridTile {
    const char* id;
    const char* title;
    const char* value;
    const char* detail;
};

static const GridTile kTiles[] = {
    {"revenue", "Revenue", "$48,210", "Up 12% from last week"},
    {"orders", "Orders", "1,284", "312 awaiting shipment"},
    {"visitors", "Visitors", "23,906", "Peak at 14:00"},
    {"conversion", "Conversion", "5.4%", "Target 6%"},
    {"refunds", "Refunds", "18", "Two need review"},
    {"inventory", "Inventory", "92%", "Three products low"},
    {"notes", "Notes", "Pinned", "Locked in place: other tiles flow around it"},
};

static const int kTileCount = (int)(sizeof(kTiles) / sizeof(kTiles[0]));

static void DashboardAreas(GridArea* out) {
    out[0] = GridArea::New(StrL("revenue")).ColSpan(6).RowSpan(4).MinSpan(3, 2);
    out[1] = GridArea::New(StrL("orders")).Col(6).ColSpan(3).RowSpan(2);
    out[2] = GridArea::New(StrL("visitors")).Col(9).ColSpan(3).RowSpan(2);
    out[3] = GridArea::New(StrL("conversion"))
                 .Col(6)
                 .Row(2)
                 .ColSpan(6)
                 .RowSpan(2)
                 .MaxSpan(8, 4);
    out[4] = GridArea::New(StrL("refunds")).Row(4).ColSpan(4).RowSpan(3);
    out[5] =
        GridArea::New(StrL("inventory")).Col(4).Row(4).ColSpan(4).RowSpan(3);
    out[6] = GridArea::New(StrL("notes"))
                 .Col(8)
                 .Row(4)
                 .ColSpan(4)
                 .RowSpan(3)
                 .Locked(true);
}

static void HandleAreas(GridArea* out) {
    out[0] = GridArea::New(StrL("revenue")).ColSpan(3).RowSpan(3);
    out[1] = GridArea::New(StrL("orders")).Col(3).ColSpan(3).RowSpan(3);
    out[2] = GridArea::New(StrL("visitors")).Row(3).ColSpan(2).RowSpan(2);
    out[3] =
        GridArea::New(StrL("conversion")).Col(2).Row(3).ColSpan(4).RowSpan(2);
}

struct GridLayoutStory {
    Entity<GridLayoutState> dashboard = {};
    Entity<GridLayoutState> handles = {};
    Str lastChange = {};
    Str selected = {};
    bool hasSelected = false;
    bool stacked = false;
    bool seeded = false;

    ~GridLayoutStory() {
        StrFree(lastChange);
        StrFree(selected);
    }

    static void OnDashboard(GridLayoutStory* self, Ctx* cx,
                            const GridLayoutEvent* ev);
    static El* Render(GridLayoutStory* self, Ctx* cx);
};

static void Keep(Str* slot, Str next) {
    StrFree(*slot);
    *slot = next;
}

void GridLayoutStory::OnDashboard(GridLayoutStory* self, Ctx* cx,
                                  const GridLayoutEvent* ev) {
    if (!self || !ev) {
        return;
    }
    if (ev->kind == GridLayoutEventKind::Select) {
        self->hasSelected = ev->hasId;
        Keep(&self->selected, ev->hasId ? StrDup(ev->id) : Str{});
        Notify(cx);
        return;
    }
    if (ev->kind != GridLayoutEventKind::Change) {
        return;
    }
    char buf[1024];
    int used = 0;
    buf[0] = 0;
    int n = ev->nAreas;
    for (int i = 0; i < n; i++) {
        const GridArea& area = ev->areas[i];
        const char* sep = i == 0 ? "" : "  \xc2\xb7  ";
        int room = (int)sizeof(buf) - used;
        int wrote =
            snprintf(buf + used, (size_t)room, "%s%s (%d, %d) %d\xc3\x97%d",
                     sep, area.id.s ? area.id.s : "", area.col, area.row,
                     area.colSpan, area.rowSpan);
        if (wrote < 0 || wrote >= room) {
            break;
        }
        used += wrote;
    }
    Keep(&self->lastChange, StrDup(Str(buf, (size_t)used)));
    Notify(cx);
}

static void OnCompaction(GridLayoutStory* self, Ctx* cx,
                         const component::ButtonGroupEvent* ev) {
    bool has = false;
    Axis axis = Axis::Vertical;
    if (ev && ev->count > 0) {
        if (ev->selected[0] == 0) {
            has = true;
            axis = Axis::Vertical;
        } else if (ev->selected[0] == 1) {
            has = true;
            axis = Axis::Horizontal;
        }
    }
    if (GridLayoutState* state = self->dashboard.Get(cx)) {
        state->SetCompaction(cx, has, axis);
    }
}

static void OnPrevent(GridLayoutStory* self, Ctx* cx, const ClickEvent*,
                      int64_t value) {
    if (GridLayoutState* state = self->dashboard.Get(cx)) {
        state->SetPreventCollision(cx, value != 0);
    }
}

static void OnStacked(GridLayoutStory* self, Ctx* cx, const ClickEvent*,
                      int64_t value) {
    self->stacked = value != 0;
    Notify(cx);
}

static void OnEditable(GridLayoutStory* self, Ctx* cx, const ClickEvent*,
                       int64_t value) {
    if (GridLayoutState* state = self->dashboard.Get(cx)) {
        state->SetDraggable(cx, value != 0);
        state->SetResizable(cx, value != 0);
    }
}

static void OnReset(GridLayoutStory* self, Ctx* cx, const ClickEvent*) {
    Keep(&self->lastChange, Str{});
    GridArea areas[7];
    DashboardAreas(areas);
    if (GridLayoutState* state = self->dashboard.Get(cx)) {
        state->SetAreas(cx, areas, 7);
    }
}

static El* TileTitle(Ctx* cx, const GridTile& tile, const Theme& th) {
    return TextEl(cx->a, Str(tile.title))->Font(14)->Medium()->Fg(th.mutedFg);
}

static El* TileBody(Ctx* cx, const GridTile& tile, const Theme& th) {
    return VFlex(cx->a)
        ->Gap(4)
        ->Child(TextEl(cx->a, Str(tile.value))->Font(24)->Semibold())
        ->Child(TextEl(cx->a, Str(tile.detail))->Font(14)->Fg(th.mutedFg));
}

static El* TileCard(Ctx* cx, const Theme& th) {
    return VFlex(cx->a)
        ->SizeFull()
        ->ClipX()
        ->ClipY()
        ->Radius(th.radiusLg)
        ->Border(1, th.border)
        ->Bg(th.background);
}

static void Seed(GridLayoutStory* self, Ctx* cx) {
    if (self->seeded) {
        return;
    }
    self->seeded = true;
    self->dashboard = EntityNewState<GridLayoutState>(cx->app);
    self->handles = EntityNewState<GridLayoutState>(cx->app);
    GridArea dashboard[7];
    DashboardAreas(dashboard);
    if (GridLayoutState* state = self->dashboard.Get(cx)) {
        state->self = self->dashboard;
        state->DefaultAreas(dashboard, 7);
    }
    GridArea handles[4];
    HandleAreas(handles);
    if (GridLayoutState* state = self->handles.Get(cx)) {
        state->self = self->handles;
        state->Cols(6);
        state->DefaultAreas(handles, 4);
    }
    Subscribe(cx, self->dashboard, &GridLayoutStory::OnDashboard);
}

static El* Controls(GridLayoutStory* self, Ctx* cx) {
    Arena* a = cx->a;
    GridLayoutState* state = self->dashboard.Get(cx);
    bool vertical = state && state->HasCompaction() &&
                    state->CompactionAxis() == Axis::Vertical;
    bool horizontal = state && state->HasCompaction() &&
                      state->CompactionAxis() == Axis::Horizontal;
    bool free = state && !state->HasCompaction();
    bool prevent = state && state->IsCollisionPrevented();
    bool editable = state && state->IsDraggable() && state->IsResizable();
    El* row = HFlex(a)->W(kFill)->FlexWrap()->Gap(16);
    row->Child(
        component::ButtonGroup::New(cx, StrL("grid-layout-compaction"))
            ->Outline()
            ->WithSize(UiSize::Small)
            ->Child(component::Button::New(cx, StrL("compaction-vertical"))
                        ->Label(StrL("Vertical"))
                        ->Selected(vertical))
            ->Child(component::Button::New(cx, StrL("compaction-horizontal"))
                        ->Label(StrL("Horizontal"))
                        ->Selected(horizontal))
            ->Child(component::Button::New(cx, StrL("compaction-none"))
                        ->Label(StrL("Free"))
                        ->Selected(free))
            ->OnClick(Listen(cx, &OnCompaction))
            ->IntoEl());
    row->Child(component::Switch::New(cx, StrL("grid-layout-prevent-collision"))
                   ->Label(StrL("Prevent collision"))
                   ->Checked(prevent)
                   ->OnClick(Listen(cx, &OnPrevent))
                   ->IntoEl());
    row->Child(component::Switch::New(cx, StrL("grid-layout-one-column"))
                   ->Label(StrL("One column"))
                   ->Checked(self->stacked)
                   ->OnClick(Listen(cx, &OnStacked))
                   ->IntoEl());
    row->Child(component::Switch::New(cx, StrL("grid-layout-editable"))
                   ->Label(StrL("Editable"))
                   ->Checked(editable)
                   ->OnClick(Listen(cx, &OnEditable))
                   ->IntoEl());
    row->Child(component::Button::New(cx, StrL("grid-layout-reset"))
                   ->Outline()
                   ->WithSize(UiSize::Small)
                   ->Label(StrL("Reset layout"))
                   ->OnClick(Listen(cx, &OnReset))
                   ->IntoEl());
    return row;
}

static El* StatusLine(GridLayoutStory* self, Ctx* cx, const Theme& th) {
    Str text = self->lastChange;
    if (!text.s) {
        text = self->hasSelected
                   ? StoryFmt(cx, "Selected: %s",
                              self->selected.s ? self->selected.s : "")
                   : StrL("Rearrange a tile to see the saved layout.");
    }
    return TextEl(cx->a, text)->Font(12)->Mono()->Fg(th.mutedFg);
}

El* GridLayoutStory::Render(GridLayoutStory* self, Ctx* cx) {
    Seed(self, cx);
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    float radius = th.radiusLg;

    GridLayout* dashboard =
        component::grid_layout(cx, StrL("dashboard-grid"), self->dashboard);
    if (self->stacked) {
        dashboard->Reflow(1);
    }
    dashboard->Label(StrL("Dashboard"));
    for (int i = 0; i < kTileCount; i++) {
        const GridTile& tile = kTiles[i];
        El* card = TileCard(cx, th)
                       ->Pad(16)
                       ->Gap(12)
                       ->JustifyBetween()
                       ->Child(TileTitle(cx, tile, th))
                       ->Child(TileBody(cx, tile, th));
        dashboard->Item(GridItem::New(cx, Str(tile.id))
                            ->Label(Str(tile.title))
                            ->Radius(radius)
                            ->Child(card));
    }

    GridLayout* handles =
        component::grid_layout(cx, StrL("handle-grid"), self->handles);
    for (int i = 0; i < 4; i++) {
        const GridTile& tile = kTiles[i];
        El* head = GridDragHandle(cx, self->handles, Str(tile.id))
                       ->PadX(16)
                       ->PadY(8)
                       ->BorderB(1, th.border)
                       ->Bg(th.muted)
                       ->Child(TileTitle(cx, tile, th));
        El* card = TileCard(cx, th)
                       ->Child(head)
                       ->Child(Div(a)->Pad(16)->Child(TileBody(cx, tile, th)));
        handles->Item(GridItem::New(cx, Str(tile.id))
                          ->Label(Str(tile.title))
                          ->Radius(radius)
                          ->DragHandleOnly(true)
                          ->Child(card));
    }

    El* board = VFlex(a)->W(kFill)->Gap(12);
    board->Child(Controls(self, cx));
    board->Child(dashboard->IntoEl());
    board->Child(StatusLine(self, cx, th));

    El* page = VFlex(a)->W(kFill)->Gap(24);
    El* dash = StorySection(
        cx, "Dashboard",
        "Drag a tile to move it and drag its corner to resize it. Click a "
        "tile, then use the arrow keys to move it and Shift with the arrow "
        "keys to resize it.");
    StorySectionBody(dash)->W(kFill);
    StorySectionAdd(dash, board);
    El* grip = StorySection(
        cx, "Drag Handle",
        "Tiles move only by their header, leaving the body free for its own "
        "pointer interaction.");
    StorySectionBody(grip)->W(kFill);
    StorySectionAdd(grip, handles->IntoEl());
    page->Child(dash);
    page->Child(grip);
    return page;
}

STORY_PAGE(StoryGridLayout, GridLayoutStory);
