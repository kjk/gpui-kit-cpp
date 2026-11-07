#include "Showcase.h"

using namespace gpui;

static El* Tile(Ctx* cx, const char* title, const char* value) {
    Arena* a = cx->a;
    return Div(a)
        ->SizeFull()
        ->Pad(8)
        ->FlexCol()
        ->Gap(4)
        ->Border(1, ExampleRgb(0x171717))
        ->Bg(ExampleRgb(0xffffff))
        ->Child(TextEl(a, Str(title))->Font(12)->Fg(ExampleRgb(0x737373)))
        ->Child(TextEl(a, Str(value)));
}

static El* Placeholder(Ctx* cx, void*) {
    return Div(cx->a)->SizeFull()->Border(1, ExampleRgb(0x737373))->Dashed();
}

static El* ResizeMark(Ctx* cx, void*) {
    return Div(cx->a)
        ->Absolute()
        ->Right(3)
        ->Bottom(3)
        ->W(6)
        ->H(6)
        ->BorderR(2, ExampleRgb(0x737373))
        ->BorderB(2, ExampleRgb(0x737373));
}

static void SeedGrid(ShowcaseApp* app, Ctx* cx) {
    if (app->gridLayout.IsValid()) {
        return;
    }
    app->gridLayout = EntityNewState<GridLayoutState>(cx->app);
    GridLayoutState* state = app->gridLayout.Get(cx);
    if (!state) {
        return;
    }
    state->self = app->gridLayout;
    state->Cols(6);
    GridArea areas[4] = {
        GridArea::New(StrL("revenue")).ColSpan(4).RowSpan(3),
        GridArea::New(StrL("orders")).Col(4).ColSpan(2).RowSpan(2),
        GridArea::New(StrL("visitors")).Row(3).ColSpan(3).RowSpan(2),
        GridArea::New(StrL("notes"))
            .Col(4)
            .Row(2)
            .ColSpan(2)
            .RowSpan(3)
            .Locked(true),
    };
    state->DefaultAreas(areas, 4);
}

El* ShowcaseGridLayout(ShowcaseApp* app, Ctx* cx) {
    SeedGrid(app, cx);
    Arena* a = cx->a;
    StateStyle dragging = {};
    dragging.Opacity(0.85f);
    StateStyle selected = {};
    selected.Border(2, ExampleRgb(0x2563eb));
    GridLayout* grid =
        GridLayout::New(cx, StrL("example-grid-layout"), app->gridLayout)
            ->Label(StrL("Dashboard"))
            ->Gap(8)
            ->Placeholder(Placeholder, nullptr)
            ->ResizeHandle(ResizeMark, nullptr)
            ->DraggingItem(dragging)
            ->SelectedItem(selected)
            ->Item(GridItem::New(cx, StrL("revenue"))
                       ->Child(Tile(cx, "Revenue", "$48,210")))
            ->Item(GridItem::New(cx, StrL("orders"))
                       ->Child(Tile(cx, "Orders", "1,284")))
            ->Item(GridItem::New(cx, StrL("visitors"))
                       ->Child(Tile(cx, "Visitors", "23,906")))
            ->Item(GridItem::New(cx, StrL("notes"))
                       ->Child(Tile(cx, "Notes (locked)", "Pinned")));
    return Div(a)->W(384)->Font(14)->Child(grid->IntoEl());
}

SHOWCASE_PAGE(CompGridLayout, ShowcaseGridLayout);
