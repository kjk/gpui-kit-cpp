#include "Showcase.h"
#include "gpui.h"

using namespace gpui;

// crates/base/examples/showcase/components/dock.rs. The whole of that page is
// a `DockAreaRenderer` of its own — base owns the tree, the drag, the drop and
// the resize and draws none of it, so a page that wants a dock brings its own
// skin. This is that skin, in `DockRenderer`'s shape.

namespace {

// The page's own palette, the way every base showcase page supplies its own:
// crates/base is the unstyled layer and has no theme to read.
Rgba Surface() {
    return ExampleRgb(0xffffff);
}
Rgba Chrome() {
    return ExampleRgb(0xf4f4f5);
}
Rgba Border() {
    return ExampleRgb(0xd4d4d8);
}
Rgba Muted() {
    return ExampleRgb(0x71717a);
}
Rgba Accent() {
    return ExampleRgb(0x2563eb);
}
// DROP_TARGET: the accent at 0x33.
Rgba DropTarget() {
    return RgbaOpacity(Accent(), 0x33 / 255.f);
}

const float kTabBarH = 26;
const float kResizeStrip = 4;

struct ShowcasePanelData {
    const char* name;
    const char* title;
    const char* body;
};

const ShowcasePanelData kPanels[] = {
    {"Explorer", "Explorer",
     "Drag this tab into the other group to move it there."},
    {"Search", "Search",
     "Two panels share this tab group. Click a tab to switch."},
    {"Editor", "Editor",
     "Drag a tab towards an edge of this group to split there."},
    {"Terminal", "Terminal",
     "The bottom dock shares the column with the center region."},
    {"Problems", "Problems", "Nothing here."},
};
const int kNPanels = (int)(sizeof(kPanels) / sizeof(kPanels[0]));

// ShowcasePanel::render.
El* RenderPanel(Ctx* cx, void* data) {
    Arena* a = cx->a;
    const auto* d = (const ShowcasePanelData*)data;
    return Div(a)
        ->SizeFull()
        ->FlexCol()
        ->Gap(4)
        ->Pad(12)
        ->Child(ScTxt(cx, Str(d->title), 12, ExampleRgb(0x171717)))
        ->Child(ScTxt(cx, Str(d->body), 12, Muted())->Wrap());
}

// ---------------------------------------------------------------------------
// ShowcaseDockSkin

El* SkinFrame(Ctx* cx, void*) {
    // Rust's frame is `flex_row`; base lays the three Docks around the centre
    // itself and sets the direction after this, so what is left here is the
    // ground the area is drawn on.
    return Div(cx->a)->ClipX()->ClipY()->Bg(Chrome());
}

El* SkinCenterFrame(Ctx* cx, void*) {
    return Div(cx->a)->ClipY();
}

El* SkinSplitFrame(Ctx* cx, void*, int, Axis) {
    return Div(cx->a)->MinH(0)->ClipY();
}

// render_split_handle: only the paint — base keeps the hit area, the cursor
// and the drag. A hairline, so the edge reads as a line rather than a bar.
El* SkinSplitHandle(Ctx* cx, void*, const DockHandleCtx* h) {
    El* line = Div(cx->a)->Bg(h->active ? Accent() : Border());
    if (AxisIsHorizontal(h->axis)) {
        return line->W(1)->H(kFill);
    }
    return line->H(1)->W(kFill);
}

// The strip on a dock's inner edge that resizes it: a wide hit area with a
// hairline inside.
El* ResizeStrip(Ctx* cx, const DockCtx* d) {
    Arena* a = cx->a;
    El* strip = Div(a)->Absolute()->ItemsCenter()->JustifyCenter();
    El* line = Div(a)->Bg(Border());
    switch (d->placement) {
        case DockPlacement::Left:
            strip->Top(0)->Right(0)->H(kFill)->W(kResizeStrip);
            line->W(1)->H(kFill);
            break;
        case DockPlacement::Bottom:
            strip->Top(0)->Left(0)->W(kFill)->H(kResizeStrip);
            line->H(1)->W(kFill);
            break;
        default:
            strip->Top(0)->Left(0)->H(kFill)->W(kResizeStrip);
            line->W(1)->H(kFill);
            break;
    }
    return DockBindResizeStrip(d, strip->Child(line));
}

// render_dock. A closed dock takes no space; the toolbar is what brings it
// back.
El* SkinDock(Ctx* cx, void*, const DockCtx* d, El* content) {
    if (!d->open) {
        return nullptr;
    }
    Arena* a = cx->a;
    El* box = Div(a)->Shrink0()->ClipX()->ClipY();
    if (d->placement == DockPlacement::Bottom) {
        box->W(kFill)->H(d->size)->FlexCol();
    } else {
        box->H(kFill)->W(d->size)->FlexRow();
    }
    return box->Child(content)->Child(ResizeStrip(cx, d));
}

El* SkinTabGroupFrame(Ctx* cx, void*, const DockTabGroup*) {
    return Div(cx->a)->MinH(0)->ClipY()->Bg(Surface());
}

El* SkinTabContentFrame(Ctx* cx, void*, const DockTabGroup*) {
    return Div(cx->a)->MinH(0)->ClipY();
}

// render_tab_bar. A hidden panel keeps its place in the tree and its tab
// slot; it is the skin that leaves it undrawn.
El* SkinTabBar(Ctx* cx, void*, const DockTabGroup* g) {
    Arena* a = cx->a;
    El* bar = Div(a)
                  ->FlexRow()
                  ->ItemsCenter()
                  ->W(kFill)
                  ->H(kTabBarH)
                  ->ClipX()
                  ->Bg(Chrome())
                  ->BorderB(1, Border());
    int activeIx = DockGroupActiveIx(g);
    int count = DockGroupCount(g);
    for (int i = 0; i < count; i++) {
        const DockPanelDef* def = DockGroupPanel(g, i);
        if (!def->visible) {
            continue;
        }
        bool on = i == activeIx && !g->collapsed;
        El* tab = Div(a)
                      ->FlexRow()
                      ->ItemsCenter()
                      ->Shrink0()
                      ->PadX(8)
                      ->H(kFill)
                      ->Cursor(CursorKind::Pointer);
        if (on) {
            tab->Bg(Surface());
        }
        tab->Child(ScTxt(cx, def->title, 12, on ? Accent() : Muted()));
        bar->Child(DockBindTab(g, i, tab));
    }
    return bar;
}

// Base resolves where a drop would land; painting it is all that is left.
El* SkinDropIndicator(Ctx* cx, void*, Bounds) {
    return Div(cx->a)->Bg(DropTarget());
}

// DragPreview: the box that follows the cursor while a tab is dragged. Base's
// own renders nothing, because a preview is appearance.
El* SkinDragPreview(Ctx* cx, void*, const DockPanelDef* def) {
    return Div(cx->a)
        ->PadX(8)
        ->PadY(4)
        ->Bg(Surface())
        ->Border(1, Accent())
        ->Child(ScTxt(cx, def->title, 12, Accent()));
}

const DockRenderer& Skin() {
    static DockRenderer r;
    static bool inited = false;
    if (!inited) {
        inited = true;
        r.frame = SkinFrame;
        r.centerFrame = SkinCenterFrame;
        r.splitFrame = SkinSplitFrame;
        r.splitHandle = SkinSplitHandle;
        r.dock = SkinDock;
        r.tabGroupFrame = SkinTabGroupFrame;
        r.tabContentFrame = SkinTabContentFrame;
        r.tabBar = SkinTabBar;
        r.dropIndicator = SkinDropIndicator;
        r.dragPreview = SkinDragPreview;
    }
    return r;
}

// build_dock: the area is built once, since a `DockArea` is an entity and
// rebuilding it every frame would discard the layout the viewer arranged.
Entity<DockState> DockStateOf(ShowcaseApp* app, Ctx* cx) {
    if (!app->dock.IsValid()) {
        app->dock = EntityNewState<DockState>(cx->app);
    }
    DockState* s = app->dock.Get(cx);
    if (!s || len(s->panels) > 0) {
        return app->dock;
    }
    s->hasVersion = true;
    s->version = 1;
    // The area draws its own toggle in the toolbar above it, so the three the
    // dock hangs off a tab bar are off — Rust's skin never draws them either.
    s->toggleButtonVisible = false;
    int panel[kNPanels];
    for (int i = 0; i < kNPanels; i++) {
        DockPanelDef def;
        def.name = Str(kPanels[i].name);
        def.title = Str(kPanels[i].title);
        def.render = RenderPanel;
        def.data = (void*)&kPanels[i];
        panel[i] = DockAddPanelDef(s, def);
    }
    // The centre: two panels in one group beside a group of one, 200 wide.
    int left = DockNewTabs(s);
    DockTabsAdd(s, left, panel[0]);
    DockTabsAdd(s, left, panel[1]);
    int right = DockNewTabs(s);
    DockTabsAdd(s, right, panel[2]);
    int center = DockNewSplit(s, Axis::Horizontal);
    DockSplitAdd(s, center, left, 200);
    DockSplitAdd(s, center, right, 300);
    s->center = center;

    int bottom = DockNewTabs(s);
    DockTabsAdd(s, bottom, panel[3]);
    DockTabsAdd(s, bottom, panel[4]);
    s->bottom.node = bottom;
    s->bottom.size = 140;
    // Rust's page builds no left or right Dock at all.
    s->left.node = -1;
    s->right.node = -1;
    return app->dock;
}

void ToggleBottom(ShowcaseApp* app, Ctx* cx, const ClickEvent*) {
    if (DockState* s = app->dock.Get(cx)) {
        DockToggleSide(s, cx, DockPlacement::Bottom);
    }
    Notify(cx);
}

// A toggle for one dock, so a closed dock can be brought back.
El* DockToggle(ShowcaseApp* app, Ctx* cx, DockPlacement p, const char* label) {
    Arena* a = cx->a;
    DockState* s = app->dock.Get(cx);
    DockSide* side = s ? DockSideOf(s, p) : nullptr;
    bool open = side && side->open;
    El* e = Div(a)
                ->PadX(8)
                ->PadY(4)
                ->Cursor(CursorKind::Pointer)
                ->Border(1, Border())
                ->OnClick(Listen(cx, &ToggleBottom))
                ->FocusId(HashClickId(StrL("sc-dock-toggle-bottom")));
    if (open) {
        e->Bg(Surface());
    }
    return e->Child(ScTxt(cx, Str(label), 12, open ? Accent() : Muted()));
}

} // namespace

El* ShowcaseDock(ShowcaseApp* app, Ctx* cx) {
    Arena* a = cx->a;
    Entity<DockState> state = DockStateOf(app, cx);
    // Fills whatever the showcase gives it — the surrounding container opts
    // this example out of the centered, intrinsically-sized box the smaller
    // parts use, so a percentage size resolves here.
    return Div(a)
        ->SizeFull()
        ->FlexCol()
        ->ClipX()
        ->ClipY()
        ->Border(1, Border())
        ->Child(
            Div(a)
                ->FlexRow()
                ->Shrink0()
                ->ItemsCenter()
                ->Gap(8)
                ->Pad(8)
                ->Bg(Chrome())
                ->BorderB(1, Border())
                ->Child(DockToggle(app, cx, DockPlacement::Bottom, "Bottom"))
                ->Child(ScTxt(cx,
                              StrL("Drag a tab onto another group to merge "
                                   "it, or towards an edge to split"),
                              12, Muted())))
        ->Child(Div(a)->Flex1()->MinH(0)->W(kFill)->Child(
            DockArea::New(cx, StrL("showcase-dock"), state, &Skin())));
}

SHOWCASE_PAGE(CompDock, ShowcaseDock);
