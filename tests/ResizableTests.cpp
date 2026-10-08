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
    bool hasEdge = false;
    HandleEdge edge = HandleEdge::Leading;
    El* rendered = nullptr;
};

static El* RenderResizeAppearance(void* user,
                                  const ResizeHandleContext* context, Ctx* cx) {
    ResizeAppearanceProbe* probe = (ResizeAppearanceProbe*)user;
    probe->calls++;
    probe->axis = context->AxisValue();
    probe->active = context->IsActive();
    probe->hasEdge = context->Edge(&probe->edge);
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
            ->Inside(HandleEdge::Trailing)
            ->WithAppearance(&probe, RenderResizeAppearance);
    El* handleEl = handle->IntoEl();
    utassert(handleEl && probe.calls == 1);
    utassert(probe.axis == Axis::Horizontal && !probe.active);
    utassert(handleEl->cursor == CursorKind::ColResize);
    // Hugging its trailing edge: the whole band inside, border-box, padded
    // on the inner side only. The renderer's element stays in tree order,
    // under the container's clip (a_hugging_handle_paints_beneath_a_popover_
    // deferred_before_it): a deferred one would paint over a popover the
    // application deferred from a panel drawn before it.
    utassertnear(handleEl->style.absRight, 0.f);
    utassertnear(handleEl->style.width,
                 kResizeHandleSize + kResizeHandlePadding);
    utassertnear(handleEl->style.pad.left, kResizeHandlePadding);
    utassertnear(handleEl->style.pad.right, 0.f);
    utassert(probe.rendered && !probe.rendered->style.deferred);
    utassert(probe.hasEdge && probe.edge == HandleEdge::Trailing);

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

static El* RenderSeamLine(void* user, const ResizeHandleContext* context,
                          Ctx* cx) {
    El** out = (El**)user;
    *out = Div(cx->a)->FlexNone();
    if (AxisIsHorizontal(context->AxisValue()))
        (*out)->W(1)->H(kFill);
    else
        (*out)->H(1)->W(kFill);
    return *out;
}

// resize_handle.rs a_hugging_handle_draws_its_line_on_the_seam: a 200px
// dock between two 100px neighbours, clipped to itself the way dock_frame
// is; the hairline of a handle hugging either edge is the pixel against the
// seam. a_hugging_handle_paints_beneath_a_popover_deferred_before_it is the
// renderer's element staying out of the deferred layer, asserted above;
// mod.rs a_covered_handle_stays_idle holds by construction here, since the
// handle's listeners belong to its hit rect and an occluding overlay in
// front of it takes the pointer.
static void AHuggingHandleDrawsItsLineOnTheSeam() {
    App app = {};
    Arena* arena = ArenaNew();
    Window* win = new Window();
    win->app = &app;
    win->paint.app = &app;
    win->paint.window = win;
    Ctx cx = {&app, win, arena, {}};
    Axis axes[2] = {Axis::Horizontal, Axis::Vertical};
    HandleEdge edges[2] = {HandleEdge::Leading, HandleEdge::Trailing};
    for (Axis axis : axes) {
        bool horizontal = AxisIsHorizontal(axis);
        for (HandleEdge edge : edges) {
            El* line = nullptr;
            El* dock = Div(arena)->ClipX()->ClipY()->Child(
                resize_handle(&cx, StrL("hugging"), axis)
                    ->Inside(edge)
                    ->WithAppearance(&line, RenderSeamLine)
                    ->IntoEl());
            El* root = Div(arena);
            if (horizontal) {
                dock->W(200)->H(kFill);
                root->FlexRow()
                    ->W(400)
                    ->H(100)
                    ->Child(Div(arena)->W(100)->H(kFill))
                    ->Child(dock)
                    ->Child(Div(arena)->W(100)->H(kFill));
            } else {
                dock->H(200)->W(kFill);
                root->FlexCol()
                    ->H(400)
                    ->W(100)
                    ->Child(Div(arena)->H(100)->W(kFill))
                    ->Child(dock)
                    ->Child(Div(arena)->H(100)->W(kFill));
            }
            LayoutEl(&win->paint, root, 0, 0, 400, 400, 16, Rgba{});
            utassert(line != nullptr);
            float start = horizontal ? line->x : line->y;
            float end = start + (horizontal ? line->w : line->h);
            float seam = edge == HandleEdge::Leading ? 100.f : 300.f;
            if (edge == HandleEdge::Leading) {
                utassertnear(start, seam);
                utassertnear(end, seam + 1);
            } else {
                utassertnear(start, seam - 1);
                utassertnear(end, seam);
            }
        }
    }
    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    delete win;
    ArenaDelete(arena);
}

// resize_handle.rs: a_listener_writes_its_progress_back_into_the_stored_state,
// setting_the_state_it_already_has_asks_for_no_repaint and
// only_a_held_handle_is_active. The handle's state is one element-state
// entity every listener reaches, so there is no copy for a listener to write
// into; what remains is the change report and the activity rule.
static void AHandleStateReportsOnlyRealChanges() {
    SharedHandleState stored;
    SharedHandleState* listener = &stored;
    utassert(listener->Set(ResizeHandleState::Pressed));
    utassert(stored.Get() == ResizeHandleState::Pressed);
    utassert(ResizeHandleStateIsActive(stored.Get()));

    SharedHandleState state;
    utassert(state.Set(ResizeHandleState::Hovered));
    utassert(!state.Set(ResizeHandleState::Hovered));

    utassert(!ResizeHandleStateIsActive(ResizeHandleState::Idle));
    utassert(!ResizeHandleStateIsActive(ResizeHandleState::Hovered));
    utassert(ResizeHandleStateIsActive(ResizeHandleState::Pressed));
    utassert(ResizeHandleStateIsActive(ResizeHandleState::Dragging));
}

// mod.rs: a_handle_reports_the_press_and_the_drag_to_its_renderer. The
// listeners, called the way the window calls them, walk the handle through
// hovered, pressed, dragging and back to idle.
static void AHandleReportsThePressAndTheDragToItsRenderer() {
    App app = {};
    Arena* arena = ArenaNew();
    Window* win = new Window();
    win->app = &app;
    Ctx cx = {&app, win, arena, {}};
    Entity<SharedHandleState> hs =
        ResizeHandleStateFor(&cx, StrL("handle-state"));
    SharedHandleState* s = hs.Get(&cx);
    utassert(s && s->Get() == ResizeHandleState::Idle);
    ResizeHandleState seen[5] = {s->Get()};
    HoverEvent hover;
    hover.hovered = true;
    SharedHandleState::OnHover(s, &cx, &hover);
    seen[1] = s->Get();
    MouseDownEvent down = {};
    SharedHandleState::OnDown(s, &cx, &down);
    seen[2] = s->Get();
    // Out of the band already: the drag is still the handle's.
    hover.hovered = false;
    SharedHandleState::OnHover(s, &cx, &hover);
    DragMoveEvent drag = {};
    SharedHandleState::OnDragMove(s, &cx, &drag);
    seen[3] = s->Get();
    MouseUpEvent up = {};
    SharedHandleState::OnUpOut(s, &cx, &up);
    seen[4] = s->Get();
    utassert(seen[0] == ResizeHandleState::Idle);
    utassert(seen[1] == ResizeHandleState::Hovered);
    utassert(seen[2] == ResizeHandleState::Pressed);
    utassert(seen[3] == ResizeHandleState::Dragging);
    utassert(seen[4] == ResizeHandleState::Idle);
    // A release over the handle leaves it hovered.
    SharedHandleState::OnDown(s, &cx, &down);
    SharedHandleState::OnUp(s, &cx, &up);
    utassert(s->Get() == ResizeHandleState::Hovered);
    WindowKeyedFree(win);
    EntityDropAll(&app);
    delete win;
    ArenaDelete(arena);
}

// resize_handle.rs a_renderer_is_told_the_edge_a_handle_hugs, for both axes
// and both edges, and none for a handle straddling its boundary.
// resizable.rs: the styled renderer defers only the pill, and only for a
// hugging handle, so the pill's overhanging pixel survives the dock's clip
// while the hairline stays beneath a popover.
static void ARendererIsToldTheEdgeAHandleHugs() {
    App app = {};
    component::Init(&app);
    Arena* arena = ArenaNew();
    Window* win = new Window();
    win->app = &app;
    Ctx cx = {&app, win, arena, {}};
    Axis axes[2] = {Axis::Horizontal, Axis::Vertical};
    HandleEdge edges[2] = {HandleEdge::Leading, HandleEdge::Trailing};
    for (Axis axis : axes) {
        for (HandleEdge edge : edges) {
            ResizeAppearanceProbe probe;
            resize_handle(&cx, StrL("told"), axis)
                ->Inside(edge)
                ->WithAppearance(&probe, RenderResizeAppearance)
                ->IntoEl();
            utassert(probe.hasEdge && probe.edge == edge);
        }
        ResizeAppearanceProbe straddling;
        resize_handle(&cx, StrL("straddling"), axis)
            ->WithAppearance(&straddling, RenderResizeAppearance)
            ->IntoEl();
        utassert(straddling.calls == 1 && !straddling.hasEdge);
    }

    for (int hugging = 0; hugging < 2; hugging++) {
        ResizeHandleContext context = {Axis::Horizontal,
                                       ResizeHandleState::Dragging,
                                       HandleEdge::Trailing, hugging == 1};
        IdScope scope(&cx, hugging ? StrL("hugging") : StrL("straddle"));
        El* line = component::RenderResizeHandle(nullptr, &context, &cx);
        El* pill = line ? line->first : nullptr;
        utassert(line && !line->style.deferred);
        utassert(pill && (pill->style.deferred != 0) == (hugging == 1));
    }
    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

struct ResizeHeard {
    int container = -1;
    int handle = -1;
    int doubles = 0;
    int drags = 0;
    int callerDrags = 0;
};

static ResizeHeard gResizeHeard;

struct ResizeCaller {
    static void OnBox(ResizeCaller*, Ctx*, const HoverEvent* ev) {
        gResizeHeard.container = ev && ev->hovered ? 1 : 0;
    }
    static void OnHandle(ResizeCaller*, Ctx*, const HoverEvent* ev) {
        gResizeHeard.handle = ev && ev->hovered ? 1 : 0;
    }
    static void OnDouble(ResizeCaller*, Ctx*, const MouseDownEvent*) {
        gResizeHeard.doubles++;
    }
    static void OnDrag(ResizeCaller*, Ctx*, const DragMoveEvent*) {
        gResizeHeard.drags++;
    }
    static void OnCallerDrag(ResizeCaller*, Ctx*, const DragMoveEvent*) {
        gResizeHeard.callerDrags++;
    }
    static El* Render(ResizeCaller*, Ctx* cx) {
        return Div(cx->a)
            ->W(200)
            ->H(100)
            ->Click(1)
            ->OnHover(Listen(cx, &ResizeCaller::OnBox))
            ->Child(resize_handle(cx, StrL("edge"), Axis::Horizontal)
                        ->Inside(HandleEdge::Trailing)
                        ->OnHover(Listen(cx, &ResizeCaller::OnHandle))
                        ->OnDoubleClick(Listen(cx, &ResizeCaller::OnDouble))
                        ->OnDrag(StrL("resize-sidebar"), 0,
                                 Listen(cx, &ResizeCaller::OnDrag))
                        ->OnDragMove(Listen(cx, &ResizeCaller::OnCallerDrag))
                        ->IntoEl());
    }
};

// a_callers_listeners_reach_the_band: hover, double click and drag on a
// standalone handle. The band occludes the container it hugs.
static void ACallersListenersReachTheBand() {
    gResizeHeard = {};
    gResizeHeard.container = -1;
    gResizeHeard.handle = -1;
    App* app = TestAppNew();
    Window* win =
        TestWindowOpen(app, EntityNew<ResizeCaller>(app), 400, 100, 1);
    TestDraw(win);
    TestSimulateMouseMove(win, {100, 50});
    utassert(gResizeHeard.container == 1);
    const HitRect* band = HitTestRect(&win->paint, 198, 50);
    utassert(band && band->bounds.w < 10.f);
    TestSimulateMouseMove(win, {band->bounds.x + band->bounds.w * 0.5f, 50});
    utassert(gResizeHeard.handle == 1);
    utassert(gResizeHeard.container == 0);
    float x = band->bounds.x + band->bounds.w * 0.5f;
    PlatformInput second =
        InputMouseDown(MouseButton::Left, x, 50, {}, 2, false);
    WindowDispatchInput(win, &second);
    TestFlushEffects(app);
    utassert(gResizeHeard.doubles == 1);
    TestSimulateMouseMove(win, {x + 24, 50}, true);
    utassert(gResizeHeard.drags >= 1);
    utassert(gResizeHeard.callerDrags >= 1);
    TestAppFree(app);
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
    AHandleStateReportsOnlyRealChanges();
    AHandleReportsThePressAndTheDragToItsRenderer();
    AHuggingHandleDrawsItsLineOnTheSeam();
    ARendererIsToldTheEdgeAHandleHugs();
    ACallersListenersReachTheBand();
}
