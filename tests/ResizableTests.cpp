/* Ported from crates/base/src/resizable/mod.rs.
 *
 * Rust's two cases there drive a window and a drag; both are checking
 * resize_panel_at_handle, which the drag and the programmatic API share. Its
 * numbers come over directly: two panels of 200 in a 400 container, one
 * resized to 220, leaving 220 and 180. */

#include "Test.h"

static void ResizingOnePanelTakesFromTheNext() {
    float sizes[2] = {200, 200};
    utassert(ResizablePanelResize(sizes, nullptr, nullptr, 2, 0, 220, 400));
    utassertnear(sizes[0], 220.f);
    utassertnear(sizes[1], 180.f);
}

static void TheLastPanelHasNoHandle() {
    float sizes[2] = {200, 200};
    // The handle sits between ix and ix + 1, so there is none below the last.
    utassert(!ResizablePanelResize(sizes, nullptr, nullptr, 2, 1, 300, 400));
    utassertnear(sizes[0], 200.f);
    utassertnear(sizes[1], 200.f);
    // And no move is no resize.
    utassert(!ResizablePanelResize(sizes, nullptr, nullptr, 2, 0, 200, 400));
}

static void GrowingWalksOnPastANeighbourThatIsSpent() {
    // Three panels of 200; the middle one can only give 100 before it hits
    // the default minimum, so the last one gives the rest.
    float sizes[3] = {200, 200, 200};
    utassert(ResizablePanelResize(sizes, nullptr, nullptr, 3, 0, 350, 600));
    utassertnear(sizes[0], 350.f);
    utassertnear(sizes[1], 100.f);
    utassertnear(sizes[2], 150.f);
}

static void APanelWillNotShrinkBelowItsMinimum() {
    float sizes[2] = {200, 200};
    // 40 is under the default minimum of 100, so it stops there.
    utassert(ResizablePanelResize(sizes, nullptr, nullptr, 2, 0, 40, 400));
    utassertnear(sizes[0], 100.f);
    // Rust hands the neighbour what the drag actually asked for, less what
    // the panels before could not absorb; the first panel has nothing before
    // it, so the full remainder stays with it.
    utassertnear(sizes[0] + sizes[1], 400.f);
}

static void ARangeOfItsOwnBeatsTheDefault() {
    float sizes[2] = {200, 200};
    const float mins[2] = {150, 100};
    const float maxs[2] = {250, 1e9f};
    utassert(ResizablePanelResize(sizes, mins, maxs, 2, 0, 400, 400));
    // Clamped to its own ceiling rather than the space available.
    utassertnear(sizes[0], 250.f);
    utassertnear(sizes[1], 150.f);
}

static void EveryPanelKeepsItsShareWhenTheContainerChanges() {
    float sizes[3] = {100, 200, 100};
    ResizableAdjustToContainer(sizes, 3, 800);
    utassertnear(sizes[0], 200.f);
    utassertnear(sizes[1], 400.f);
    utassertnear(sizes[2], 200.f);
    // A container of nothing leaves them alone rather than dividing by zero.
    ResizableAdjustToContainer(sizes, 3, 0);
    utassertnear(sizes[1], 400.f);
}

static void ProgrammaticResizeAndDynamicPanelsUseTheSameState() {
    App app = {};
    Arena* arena = ArenaNew();
    Window* win = new Window();
    win->app = &app;
    Ctx cx = {};
    cx.app = &app;
    cx.a = arena;
    cx.win = win;
    ResizableState state;
    state.bounds = {0, 0, 400, 100};
    VecAppend(state.sizes, 200);
    VecAppend(state.sizes, 200);
    VecAppend(state.mins, 100);
    VecAppend(state.mins, 100);
    VecAppend(state.maxs, 1e9f);
    VecAppend(state.maxs, 1e9f);
    VecAppend(state.grows, false);
    VecAppend(state.grows, false);
    VecAppend(state.shown, true);
    VecAppend(state.shown, true);
    VecAppend(state.laid, {});
    VecAppend(state.laid, {});

    // The last panel is driven through the preceding handle, as upstream.
    utassert(state.ResizePanel(&cx, 1, 180));
    utassertnear(state.Sizes()[0], 220.f);
    utassertnear(state.Sizes()[1], 180.f);
    utassertnear(state.ContainerSize(), 400.f);

    utassert(state.InsertPanel(&cx, 100, 1));
    utassert(state.Sizes().len == 3);
    utassertnear(state.Sizes()[0], 165.f);
    utassertnear(state.Sizes()[1], 100.f);
    utassertnear(state.Sizes()[2], 135.f);
    utassert(state.RemovePanel(&cx, 1));
    utassert(state.Sizes().len == 2);
    utassertnear(state.Sizes()[0] + state.Sizes()[1], 400.f);
    state.mins[0] = 175;
    utassert(state.ResetPanel(&cx, 0));
    utassertnear(state.mins[0], PANEL_MIN_SIZE);
    state.Clear();
    utassert(state.Sizes().len == 0 && state.dragging == -1);
    delete win;
    ArenaDelete(arena);
}

struct ResizeAppearanceProbe {
    int calls = 0;
    Axis axis = Axis::Vertical;
    bool active = true;
    El* rendered = nullptr;
};

static El* RenderResizeAppearance(void* user,
                                  const ResizeHandleContext* context, Ctx* cx) {
    ResizeAppearanceProbe* probe = (ResizeAppearanceProbe*)user;
    probe->calls++;
    probe->axis = context->AxisValue();
    probe->active = context->IsActive();
    probe->rendered = Div(cx->a)->W(3)->H(3);
    return probe->rendered;
}

static void SourceConstructorsAndHandleAppearanceRemainConcrete() {
    App app = {};
    Arena* arena = ArenaNew();
    Window* win = new Window();
    win->app = &app;
    Ctx cx = {};
    cx.app = &app;
    cx.a = arena;
    cx.win = win;

    ResizablePanel* first = resizable_panel(&cx)
                                ->Size(150)
                                ->SizeRange(120, 300)
                                ->FlexNone()
                                ->Child(Div(arena));
    ResizablePanel* second = resizable_panel(&cx)->Child(Div(arena));
    ResizablePanelGroup* horizontal =
        h_resizable(&cx, StrL("source-horizontal"))
            ->Size(40)
            ->Child(first)
            ->Child(second);
    utassert(horizontal->state.Get(&cx)->axis == Axis::Horizontal);
    utassertnear(horizontal->height, 40.f);
    bool growth[2] = {};
    int growthCount = 0;
    for (bool value : horizontal->grows) {
        if (growthCount < 2) growth[growthCount] = value;
        growthCount++;
    }
    utassert(horizontal->panels.len == 2 && growthCount == 2 &&
             growth[0] == false && growth[1] == true);
    El* root = horizontal->IntoEl();
    utassert(root && root->style.dir == FlexDir::Row);

    ResizablePanelGroup* vertical = v_resizable(&cx, StrL("source-vertical"))
                                        ->Size(240);
    utassert(vertical->state.Get(&cx)->axis == Axis::Vertical);
    utassertnear(vertical->width, 240.f);

    ResizeAppearanceProbe probe;
    ResizeHandle* handle =
        resize_handle(&cx, StrL("standalone-handle"), Axis::Horizontal)
            ->Placement(Side::Left)
            ->WithAppearance(&probe, RenderResizeAppearance);
    El* handleEl = handle->IntoEl();
    utassert(handleEl && probe.calls == 1);
    utassert(probe.axis == Axis::Horizontal && !probe.active);
    utassert(handleEl->cursor == CursorKind::ColResize);
    utassertnear(handleEl->style.absRight, 1.f);
    utassertnear(handleEl->style.width,
                 kResizeHandleSize + kResizeHandlePadding);

    ResizeAppearanceProbe groupProbe;
    ResizablePanelGroup* appeared =
        h_resizable(&cx, StrL("appeared"))
            ->WithHandleAppearance(&groupProbe, RenderResizeAppearance)
            ->Child(resizable_panel(&cx)->Size(150)->Child(Div(arena)))
            ->Child(resizable_panel(&cx)->Size(150)->Child(Div(arena)));
    appeared->IntoEl();
    utassert(groupProbe.calls == 1 && groupProbe.rendered);
    utassertnear(groupProbe.rendered->style.width, 3.f);
    utassertnear(groupProbe.rendered->style.height, 3.f);

    Entity<ResizableState> explicitState = EntityNewState<ResizableState>(&app);
    ResizablePanelGroup* configured =
        ResizablePanelGroup::New(&cx, StrL("configured"))
            ->Axis(Axis::Vertical)
            ->WithState(explicitState);
    configured->IntoEl();
    utassert(explicitState.Get(&cx)->axis == Axis::Vertical);

    ResizablePanelEvent event = {explicitState.Get(&cx)->sizes.els,
                                 explicitState.Get(&cx)->sizes.len};
    utassert(event.sizes == explicitState.Get(&cx)->sizes.els);
    utassertnear(PANEL_MIN_SIZE, 100.f);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    delete win;
    ArenaDelete(arena);
}

static void MixedSizingSettlesAfterContainerResize() {
    ExecInit();
    for (int callerOwned = 0; callerOwned < 2; callerOwned++) {
        App app;
        Window* win = new Window();
        win->app = &app;
        win->paint.app = &app;
        win->paint.window = win;
        Arena* a = ArenaNew();
        Ctx cx = {&app, win, a, {}};
        Entity<ResizableState> state;
        if (callerOwned) state = EntityNewState<ResizableState>(&app);
        float widths[5] = {};
        for (int frame = 0; frame < 5; frame++) {
            auto* group = h_resizable(&cx, StrL("mixed-sizing"));
            if (callerOwned) group->WithState(state);
            group->Child(resizable_panel(&cx)->Size(240)->Child(Div(a)))
                ->Child(resizable_panel(&cx)->Child(Div(a)));
            El* root = group->IntoEl();
            LayoutEl(&win->paint, root, 0, 0, frame < 2 ? 800.f : 1200.f, 100,
                     16, Rgba{});
            widths[frame] = root->first->w;
            root->customPaint(&win->paint, root, root->customUser);
            int posted = ExecDrain();
            utassert((frame == 0 || frame == 2) ? posted > 0 : posted == 0);
        }
        utassertnear(widths[0], 240.f);
        utassertnear(widths[1], widths[0]);
        utassertnear(widths[3], 360.f);
        utassertnear(widths[4], widths[3]);
        WindowKeyedFree(win);
        EntityDropAll(&app);
        AppGlobalClear(&app);
        delete win;
        ArenaDelete(a);
    }
    ExecShutdown();
}

void TestResizable() {
    TestSuite("resizable");
    ResizingOnePanelTakesFromTheNext();
    TheLastPanelHasNoHandle();
    GrowingWalksOnPastANeighbourThatIsSpent();
    APanelWillNotShrinkBelowItsMinimum();
    ARangeOfItsOwnBeatsTheDefault();
    EveryPanelKeepsItsShareWhenTheContainerChanges();
    ProgrammaticResizeAndDynamicPanelsUseTheSameState();
    SourceConstructorsAndHandleAppearanceRemainConcrete();
    MixedSizingSettlesAfterContainerResize();
}
