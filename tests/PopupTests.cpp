/* Ported from crates/base/src/popup.rs and crates/component/src/popover.rs.
 *
 * resolved_corner is still public upstream, with its deliberately unusual
 * `origin.y - height` bottom arithmetic; placement itself now goes through
 * anchor_position, the trigger's opposite edge. */

#include "Test.h"

// A trigger at (100, 50), 80 wide and 20 tall.
static const Bounds kTrigger = {100, 50, 80, 20};

static void TheTopAnchorsTakeTheTopEdge() {
    Point p = PopupResolvedCorner(PopupAnchor::TopLeft, kTrigger);
    utassertnear(p.x, 100.f);
    utassertnear(p.y, 50.f);

    p = PopupResolvedCorner(PopupAnchor::TopCenter, kTrigger);
    utassertnear(p.x, 140.f);
    utassertnear(p.y, 50.f);

    p = PopupResolvedCorner(PopupAnchor::TopRight, kTrigger);
    utassertnear(p.x, 180.f);
    utassertnear(p.y, 50.f);
}

static void TheBottomAnchorsMatchUpstreamsSubtractedHeight() {
    Point p = PopupResolvedCorner(PopupAnchor::BottomLeft, kTrigger);
    utassertnear(p.x, 100.f);
    utassertnear(p.y, 30.f);

    p = PopupResolvedCorner(PopupAnchor::BottomCenter, kTrigger);
    utassertnear(p.x, 140.f);
    utassertnear(p.y, 30.f);

    p = PopupResolvedCorner(PopupAnchor::BottomRight, kTrigger);
    utassertnear(p.x, 180.f);
    utassertnear(p.y, 30.f);
}

static void PopupContentUsesThePinnedCornerMarginAndDeferredLayer() {
    Arena* a = ArenaNew();
    PaintCtx ctx = {};
    ctx.viewW = 400;
    ctx.viewH = 300;

    El* root = Div(a)->FlexCol()->W(400)->H(300);
    root->Child(Div(a)->H(100));
    El* trigger = Div(a)->W(80)->H(20);
    El* content = Div(a)->W(30)->H(10);
    PopupPlaceContent(content, PopupAnchor::BottomRight);
    trigger->Child(content);
    root->Child(trigger);

    LayoutEl(&ctx, root, 0, 0, 400, 300, 14, Rgba{});
    utassert(content->style.deferred);
    utassert(content->style.fixed);
    utassertnear(content->style.anchorMargin, kPopupWindowMargin);
    // Trigger is (0, 100, 80, 20). anchor_position puts BottomRight on the
    // trigger's top-right, (80, 100), and the content's BottomRight corner
    // lands there.
    utassertnear(content->x, 50.f);
    utassertnear(content->y, 90.f);
    int priority = kPopupPriority;
    utassert(priority == 100);
    ArenaDelete(a);
}

static void TriggerCaptureEnablesContentOnTheNextFrame() {
    App app;
    Window* win = new Window();
    Arena* a = ArenaNew();
    win->app = &app;
    Ctx cx = {&app, win, a, {}};

    Popup* first = Popup::New(&cx, StrL("capture"), Div(a)->W(100)->H(100));
    El* firstRoot = first->Content(Div(a)->W(20)->H(20))->IntoEl();
    utassert(firstRoot->first != nullptr);
    utassert(firstRoot->first->next == nullptr);
    utassert(win->animFrame);

    Popup* second = Popup::New(&cx, StrL("capture"), Div(a)->W(100)->H(100));
    El* secondRoot = second->Content(Div(a)->W(20)->H(20))->IntoEl();
    utassert(secondRoot->first != nullptr);
    utassert(secondRoot->first->next != nullptr);
    utassert(secondRoot->first->next->style.deferred);

    WindowKeyedFree(win);
    delete win;
    ArenaDelete(a);
}

static void PopoverOpenStateOwnsItsDeferredRegistration() {
    App app;
    Window win;
    win.app = &app;
    Ctx cx = {&app, &win, nullptr, {}};
    BaseGlobalStateInit(&app);
    Entity<PopoverState> state = EntityNewState<PopoverState>(&app);

    PopoverSetOpen(&cx, state, true);
    utassert(PopoverIsOpen(&cx, state));
    utassert(BaseIsInDeferredContext(&app));
    PopoverSetOpen(&cx, state, false);
    utassert(!PopoverIsOpen(&cx, state));
    utassert(!BaseIsInDeferredContext(&app));

    // Rust's DeferredPopover is dropped with element state. The C++ global
    // stores the generational handle and sweeps it as soon as it goes stale.
    PopoverSetOpen(&cx, state, true);
    utassert(BaseIsInDeferredContext(&app));
    EntityDrop(&app, state.id);
    utassert(!BaseIsInDeferredContext(&app));
    AppGlobalClear(&app);
}

namespace {

struct TooltipRecorder {
    int builds = 0;
    int renders = 0;
    TooltipTransition transition = {};

    static El* Build(Ctx* cx, void* data) {
        TooltipRecorder* self = (TooltipRecorder*)data;
        self->builds++;
        return Div(cx->a)->W(40)->H(18)->AriaLabel(StrL("custom tip"));
    }

    static El* Render(Ctx* cx, El* view, const TooltipTransition& transition,
                      void* data) {
        TooltipRecorder* self = (TooltipRecorder*)data;
        self->renders++;
        self->transition = transition;
        return Div(cx->a)->Child(view);
    }
};

} // namespace

static void TooltipOverlayOwnsRequestsTransitionsAndPositioning() {
    App app;
    Window* win = new Window();
    Arena* a = ArenaNew();
    win->app = &app;
    Entity<TooltipOverlay> entity = EntityNew<TooltipOverlay>(&app);
    TooltipOverlay* overlay = entity.Get(&app);
    Ctx cx = {&app, win, a, entity.id};
    TooltipRecorder recorder;

    overlay->RenderWith(&TooltipRecorder::Render, &recorder);
    overlay->hadRecentTooltip = true;
    Bounds first = {10, 20, 30, 40};
    TooltipRequest request =
        TooltipRequest::New(first, &TooltipRecorder::Build, &recorder);
    request.Placement(Placement::Right);
    overlay->RequestShow(request, win, &cx);
    utassert(overlay->hasContent);
    utassert(!overlay->hasPending);
    utassert(!overlay->isSwitching);
    utassert(overlay->content.hasPreferredPlacement);
    utassert(overlay->content.preferredPlacement == Placement::Right);

    El* enter = TooltipOverlay::Render(overlay, &cx);
    utassert(enter && enter->style.explicitPositioner);
    utassert(enter->style.deferred);
    utassert(enter->style.deferredLayer == kPaintLayerTooltip);
    utassert(enter->style.positionerPlacement == (int8_t)Placement::Right);
    utassert(recorder.builds == 1 && recorder.renders == 1);
    utassert(recorder.transition.kind == TooltipTransitionKind::Enter);

    Bounds second = {80, 20, 30, 40};
    TooltipRequest next =
        TooltipRequest::New(second, &TooltipRecorder::Build, &recorder);
    overlay->RequestShow(next, win, &cx);
    utassert(overlay->isSwitching && overlay->hasPreviousBounds);
    El* switched = TooltipOverlay::Render(overlay, &cx);
    utassert(switched != nullptr);
    utassert(recorder.transition.kind == TooltipTransitionKind::Switch);
    utassertnear(recorder.transition.previous.x, first.x);
    utassertnear(recorder.transition.current.x, second.x);

    overlay->RequestHide(win, &cx);
    utassert(overlay->hadRecentTooltip && overlay->hideTask != 0);
    overlay->Hide(&cx);
    utassert(!overlay->hasContent && !overlay->hadRecentTooltip);
    utassert(overlay->hideTask == 0);

    EntityDrop(&app, entity.id);
    WindowKeyedFree(win);
    delete win;
    ArenaDelete(a);
}

static void TooltipDelayOwnsAndCancelsPendingText() {
    App app;
    Window* win = new Window();
    Arena* a = ArenaNew();
    win->app = &app;
    Entity<TooltipOverlay> entity = EntityNew<TooltipOverlay>(&app);
    TooltipOverlay* overlay = entity.Get(&app);
    Ctx cx = {&app, win, a, entity.id};

    TooltipRequest request =
        TooltipRequest::Text({2, 4, 20, 10}, StrL("delayed"));
    overlay->RequestShow(request, win, &cx);
    utassert(!overlay->hasContent && overlay->hasPending);
    utassert(overlay->showTask != 0);
    utassert(base::StrEq(overlay->pending.text, StrL("delayed")));
    utassert(overlay->pending.text.s != request.text.s);
    overlay->RequestHide(win, &cx);
    utassert(!overlay->hasPending && overlay->showTask == 0);

    EntityDrop(&app, entity.id);
    WindowKeyedFree(win);
    delete win;
    ArenaDelete(a);
}

static void DisabledTooltipOverlayIgnoresEveryShowPath() {
    App app;
    Window* win = new Window();
    Arena* a = ArenaNew();
    win->app = &app;
    Entity<TooltipOverlay> entity = EntityNew<TooltipOverlay>(&app);
    TooltipOverlay* overlay = entity.Get(&app);
    Ctx cx = {&app, win, a, entity.id};
    TooltipRecorder recorder;
    TooltipRequest request =
        TooltipRequest::New({2, 4, 20, 10}, &TooltipRecorder::Build, &recorder);
    overlay->enabled = false;

    overlay->RequestShow(request, win, &cx);
    utassert(!overlay->hasContent && !overlay->hasPending);
    utassert(overlay->showTask == 0 && overlay->hideTask == 0);
    overlay->hadRecentTooltip = true;
    overlay->RequestShow(request, win, &cx);
    utassert(!overlay->hasContent && !overlay->hasPending);
    utassert(overlay->showTask == 0 && overlay->hideTask == 0);
    utassert(recorder.builds == 0);

    EntityDrop(&app, entity.id);
    WindowKeyedFree(win);
    delete win;
    ArenaDelete(a);
}

namespace {

struct PopoverRecorder {
    int changes = 0;
    bool lastOpen = false;
    int dismisses = 0;
    float dismissX = 0;

    static void OnOpenChange(PopoverRecorder* self, Ctx*,
                             const PopoverOpenChangeEvent* ev) {
        self->changes++;
        self->lastOpen = ev->open;
    }

    static void OnDismiss(PopoverRecorder* self, Ctx*, const ClickEvent* ev) {
        self->dismisses++;
        self->dismissX = ev->x;
    }
};

} // namespace

static void PopoverOwnsOpenCallbacksAndOutsideDismissal() {
    App app;
    Window* win = new Window();
    Arena* a = ArenaNew();
    win->app = &app;
    win->frameArena = a;
    BaseGlobalStateInit(&app);
    Entity<PopoverState> state = EntityNewState<PopoverState>(&app);
    Entity<PopoverRecorder> recorder = EntityNewState<PopoverRecorder>(&app);
    Ctx cx = {&app, win, a, recorder.id};

    El* trigger = Div(a)->W(50)->H(20);
    El* content = Div(a)->W(100)->H(100);
    gpui::Popover::New(&cx, StrL("lifecycle"), state)
        ->OnOpenChange(Listen(&cx, &PopoverRecorder::OnOpenChange))
        ->OnDismiss(Listen(&cx, &PopoverRecorder::OnDismiss))
        ->Trigger(trigger)
        ->Content(content)
        ->IntoEl();
    utassert(content->onMouseDownOut.IsValid());

    MouseDownEvent triggerPress = {};
    triggerPress.button = MouseButton::Left;
    ListenerCall(&app, win, trigger->onMouseDown, &triggerPress);
    PopoverRecorder* seen = recorder.Get(&app);
    utassert(PopoverIsOpen(&cx, state));
    utassert(seen->changes == 1 && seen->lastOpen);

    // Feed the runtime a currently rendered content hitbox. A press anywhere
    // outside it reaches the per-element on_mouse_down_out listener even
    // though no ordinary element handled that location.
    HitRect hr = {};
    hr.bounds = {0, 0, 100, 100};
    hr.onMouseDownOut = content->onMouseDownOut;
    VecAppend(win->paint.hits, hr);
    PlatformInput outside = {};
    outside.kind = PlatformInputKind::MouseDown;
    outside.mouseDown.x = 240;
    outside.mouseDown.y = 180;
    outside.mouseDown.button = MouseButton::Left;
    WindowDispatchInput(win, &outside);
    utassert(!PopoverIsOpen(&cx, state));
    utassert(seen->changes == 2 && !seen->lastOpen);
    utassert(seen->dismisses == 1);
    utassertnear(seen->dismissX, 240.f);

    // The trigger is outside the popup content. Its ordinary mouse-down must
    // toggle first; the content's mouse-down-out then sees the already closed
    // state and does not turn the same press into a second dismissal.
    VecClear(win->paint.hits);
    El* closeTrigger = Div(a)->W(50)->H(20);
    El* closeContent = Div(a)->W(100)->H(100);
    gpui::Popover::New(&cx, StrL("lifecycle"), state)
        ->OnOpenChange(Listen(&cx, &PopoverRecorder::OnOpenChange))
        ->OnDismiss(Listen(&cx, &PopoverRecorder::OnDismiss))
        ->Trigger(closeTrigger)
        ->Content(closeContent);
    ListenerCall(&app, win, closeTrigger->onMouseDown, &triggerPress);
    utassert(PopoverIsOpen(&cx, state));
    HitRect openContent = {};
    openContent.bounds = {100, 100, 100, 100};
    openContent.onMouseDownOut = closeContent->onMouseDownOut;
    VecAppend(win->paint.hits, openContent);
    HitRect openTrigger = {};
    openTrigger.bounds = {0, 0, 50, 20};
    openTrigger.onMouseDown = closeTrigger->onMouseDown;
    VecAppend(win->paint.hits, openTrigger);
    PlatformInput triggerAgain = {};
    triggerAgain.kind = PlatformInputKind::MouseDown;
    triggerAgain.mouseDown.x = 10;
    triggerAgain.mouseDown.y = 10;
    triggerAgain.mouseDown.button = MouseButton::Left;
    WindowDispatchInput(win, &triggerAgain);
    utassert(!PopoverIsOpen(&cx, state));
    utassert(seen->changes == 4 && !seen->lastOpen);
    utassert(seen->dismisses == 1);

    // overlay_closable(false) omits the observer rather than occupying a
    // global slot with a handler which has to branch at dispatch time.
    El* fixed = Div(a)->W(100)->H(100);
    gpui::Popover::New(&cx, StrL("fixed"), state)
        ->OverlayClosable(false)
        ->Content(fixed);
    utassert(!fixed->onMouseDownOut.IsValid());

    // Enter and Space are Confirm in the Popover context. The action lives on
    // Popup's wrapper, so a focused trigger finds it along its focus path.
    El* keyboardRoot =
        gpui::Popover::New(&cx, StrL("keyboard"), state)
            ->OnOpenChange(Listen(&cx, &PopoverRecorder::OnOpenChange))
            ->Trigger(Div(a)->W(50)->H(20))
            ->Content(Div(a)->W(100)->H(100))
            ->IntoEl();
    utassert(keyboardRoot->style.keyContext == KeyContextOf(StrL("Popover")));
    ActionSlot* confirm = keyboardRoot->actions;
    while (confirm && confirm->action != action::Confirm()) {
        confirm = confirm->next;
    }
    utassert(confirm && confirm->fn.IsValid());
    ActionEvent ev = {action::Confirm()};
    ListenerCall(&app, win, confirm->fn, &ev);
    utassert(PopoverIsOpen(&cx, state));
    utassert(seen->changes == 5 && seen->lastOpen);

    VecReset(win->paint.hits);
    AppGlobalClear(&app);
    WindowKeyedFree(win);
    delete win;
    ArenaDelete(a);
    EntityDropAll(&app);
}

static void TheSideAnchorsFallBackToTheOrigin() {
    // Rust hands back the origin for both: a popup anchored sideways is
    // placed by the positioner rather than by a corner.
    Point p = PopupResolvedCorner(PopupAnchor::LeftCenter, kTrigger);
    utassertnear(p.x, 100.f);
    utassertnear(p.y, 50.f);

    p = PopupResolvedCorner(PopupAnchor::RightCenter, kTrigger);
    utassertnear(p.x, 100.f);
    utassertnear(p.y, 50.f);
}

// the_popup_surface_blocks_the_panel_it_covers.
//
// A caller that styles its own surface — a hover card, a dropdown — does not
// have to remember to block the mouse. The host does it, so the panel a popup
// covers stops reacting to a pointer that is over the popup. Rust inserts a
// BlockMouse hitbox at the resolved bounds during prepaint; here the surface
// says it stops the press, which is what the dispatch chain reads.
static void ThePopupSurfaceBlocksThePanelItCovers() {
    App app;
    Window* win = new Window();
    Arena* a = ArenaNew();
    win->app = &app;
    win->frameArena = a;
    BaseGlobalStateInit(&app);
    Ctx cx = {&app, win, a, {}};

    El* trigger = Div(a)->W(100)->H(100);
    El* content = Div(a)->W(40)->H(40);
    // The first frame captures the trigger; the second is the one that
    // carries the content, which is where the occlusion lands.
    Popup::New(&cx, StrL("occlusion"), trigger)->Content(content)->IntoEl();
    El* laterContent = Div(a)->W(40)->H(40);
    Popup::New(&cx, StrL("occlusion"), Div(a)->W(100)->H(100))
        ->Content(laterContent)
        ->IntoEl();

    // The surface blocks what is behind it, and it is the surface itself that
    // does so — the content inside it is an ordinary child.
    utassert(laterContent->stopMouseDown);
    utassert(!content->stopMouseDown);

    // A tooltip shares the positioner and must not occlude: one that swallowed
    // the pointer would un-hover the very trigger keeping it open.
    Positioner* tip = Positioner::Corner(&cx, Anchor::TopLeft, {0, 0});
    utassert(!tip->occlude);
    utassert(!tip->Child(Div(a))->IntoEl()->stopMouseDown);
    // An interactive surface asks for it explicitly.
    utassert(Positioner::Corner(&cx, Anchor::TopLeft, {0, 0})
                 ->Occlude()
                 ->Child(Div(a))
                 ->IntoEl()
                 ->stopMouseDown);

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

// crates/component/src/popover.rs, mod tests: the AnchorHarness. A 40 px
// trigger in an absolutely placed box at `origin`, a 60 px surface, and the
// popover open by default. The first frame captures the trigger; the second
// carries the content, which is what is measured. `offset` < 0 leaves
// Popover::offset unset.
static Bounds PositionedContent(PopupAnchor anchor, float offset, Point origin,
                                bool arrow, bool* hasArrowCanvas = nullptr) {
    App app;
    Window* win = new Window();
    Arena* a = ArenaNew();
    win->app = &app;
    win->frameArena = a;
    BaseGlobalStateInit(&app);
    component::Init(&app);
    Ctx cx = {&app, win, a, {}};
    PaintCtx ctx = {};
    ctx.viewW = 1024;
    ctx.viewH = 768;
    El* content = nullptr;
    El* root = nullptr;
    for (int frame = 0; frame < 2; frame++) {
        content = Div(a)->W(60)->H(60);
        component::Popover* popover =
            component::Popover::New(&cx, StrL("positioned-popover"))
                ->DefaultOpen(true)
                ->Arrow(arrow)
                ->Anchor(anchor)
                ->Trigger(Div(a)->W(40)->H(40))
                ->Content(content);
        if (offset >= 0) {
            popover->Offset(offset);
        }
        root = Div(a)->W(1024)->H(768)->Child(
            Div(a)->Absolute()->Left(origin.x)->Top(origin.y)->Child(
                popover->IntoEl()));
    }
    LayoutEl(&ctx, root, 0, 0, 1024, 768, 14, Rgba{});
    Bounds out = {content->x, content->y, content->w, content->h};
    if (hasArrowCanvas) {
        *hasArrowCanvas = content->last && content->last->customPaint;
    }
    WindowKeyedFree(win);
    delete win;
    ArenaDelete(a);
    EntityDropAll(&app);
    return out;
}

// anchor_and_offset_position_the_surface_on_each_trigger_edge.
static void AnchorAndOffsetPositionTheSurfaceOnEachTriggerEdge() {
    // Legacy TopLeft means below the trigger, including the default 0.25rem
    // gap.
    Bounds legacy =
        PositionedContent(PopupAnchor::TopLeft, -1, {200, 200}, false);
    utassertnear(legacy.x, 200.f);
    utassertnear(legacy.y, 244.f);

    struct {
        PopupAnchor anchor;
        float x, y;
    } cases[] = {
        {PopupAnchor::BottomLeft, 200, 128},
        {PopupAnchor::BottomCenter, 190, 128},
        {PopupAnchor::BottomRight, 180, 128},
        {PopupAnchor::TopLeft, 200, 252},
        {PopupAnchor::TopCenter, 190, 252},
        {PopupAnchor::TopRight, 180, 252},
        {PopupAnchor::RightCenter, 128, 190},
        {PopupAnchor::LeftCenter, 252, 190},
    };
    for (auto& c : cases) {
        Bounds b = PositionedContent(c.anchor, 12, {200, 200}, false);
        utassertnear(b.x, c.x);
        utassertnear(b.y, c.y);
    }
    // Current-frame trigger bounds are used after the owner moves.
    Bounds moved =
        PositionedContent(PopupAnchor::LeftCenter, 12, {260, 240}, false);
    utassertnear(moved.x, 312.f);
    utassertnear(moved.y, 230.f);
}

// arrow_reserves_space_without_changing_anchor_alignment.
static void ArrowReservesSpaceWithoutChangingAnchorAlignment() {
    bool canvas = false;
    // Bottom edge 48 + tip gap 12 + arrow depth 6.
    Bounds b =
        PositionedContent(PopupAnchor::TopLeft, 12, {200, 8}, true, &canvas);
    utassertnear(b.x, 200.f);
    utassertnear(b.y, 66.f);
    utassert(canvas);
    b = PositionedContent(PopupAnchor::TopRight, 12, {200, 8}, false, &canvas);
    utassertnear(b.x, 180.f);
    utassertnear(b.y, 60.f);
    utassert(!canvas);
}

// anchor_does_not_flip_when_offset_or_arrow_is_enabled.
static void AnchorDoesNotFlipWhenOffsetOrArrowIsEnabled() {
    // Clamp to the window margin instead of flipping below the trigger.
    Bounds b = PositionedContent(PopupAnchor::BottomCenter, 12, {200, 8}, true);
    utassertnear(b.y, 8.f);
}

// arrow_alignment_uses_the_anchor_instead_of_trigger_center.
static void ArrowAlignmentUsesTheAnchorInsteadOfTriggerCenter() {
    using gpui::Placement;
    Bounds trigger = {120, 120, 40, 20};
    struct {
        PopupAnchor anchor;
        Placement side;
        float x, y;
    } cases[] = {
        {PopupAnchor::TopLeft, Placement::Bottom, 120, 140},
        {PopupAnchor::TopCenter, Placement::Bottom, 140, 140},
        {PopupAnchor::TopRight, Placement::Bottom, 160, 140},
        {PopupAnchor::BottomLeft, Placement::Top, 120, 120},
        {PopupAnchor::BottomCenter, Placement::Top, 140, 120},
        {PopupAnchor::BottomRight, Placement::Top, 160, 120},
        {PopupAnchor::LeftCenter, Placement::Right, 160, 130},
        {PopupAnchor::RightCenter, Placement::Left, 120, 130},
    };
    for (auto& c : cases) {
        component::PopoverArrowAnchor at =
            component::ArrowAnchor(c.anchor, trigger);
        utassert(at.side == c.side);
        utassertnear(at.target.x, c.x);
        utassertnear(at.target.y, c.y);
    }
}

// arrows_point_toward_the_trigger_on_every_resolved_side.
static void ArrowsPointTowardTheTriggerOnEveryResolvedSide() {
    using gpui::Placement;
    Bounds surface = {100, 100, 80, 60};
    struct {
        Placement side;
        Bounds trigger;
        float x, y;
    } cases[] = {
        {Placement::Bottom, {120, 50, 40, 20}, 140, 94},
        {Placement::Top, {120, 180, 40, 20}, 140, 166},
        {Placement::Right, {40, 120, 40, 20}, 94, 130},
        {Placement::Left, {200, 120, 40, 20}, 186, 130},
    };
    Point points[3];
    for (auto& c : cases) {
        component::ArrowPoints(surface, c.trigger, c.side, 6, 4, points);
        utassertnear(points[1].x, c.x);
        utassertnear(points[1].y, c.y);
    }
    component::ArrowPoints(surface, {0, 50, 20, 20}, Placement::Bottom, 6, 4,
                           points);
    utassertnear(points[0].x, 104.f);
    utassertnear(points[0].y, 100.f);
    utassertnear(points[1].x, 110.f);
    utassertnear(points[1].y, 94.f);
}

// arrow_join_covers_both_sides_of_the_surface_edge.
static void ArrowJoinCoversBothSidesOfTheSurfaceEdge() {
    using gpui::Placement;
    Bounds surface = {100, 100, 80, 60};
    Bounds trigger = {120, 120, 40, 20};
    struct {
        Placement side;
        Bounds expected;
    } cases[] = {
        {Placement::Bottom, {135, 99, 10, 2}},
        {Placement::Top, {135, 159, 10, 2}},
        {Placement::Right, {99, 125, 2, 10}},
        {Placement::Left, {179, 125, 2, 10}},
    };
    for (auto& c : cases) {
        Point points[3];
        component::ArrowPoints(surface, trigger, c.side, 6, 4, points);
        Bounds join = component::ArrowJoinBounds(points, c.side, 1);
        utassertnear(join.x, c.expected.x);
        utassertnear(join.y, c.expected.y);
        utassertnear(join.w, c.expected.w);
        utassertnear(join.h, c.expected.h);
    }
}

void TestPopup() {
    TestSuite("popup");
    ThePopupSurfaceBlocksThePanelItCovers();
    TheTopAnchorsTakeTheTopEdge();
    TheBottomAnchorsMatchUpstreamsSubtractedHeight();
    TheSideAnchorsFallBackToTheOrigin();
    PopupContentUsesThePinnedCornerMarginAndDeferredLayer();
    AnchorAndOffsetPositionTheSurfaceOnEachTriggerEdge();
    ArrowReservesSpaceWithoutChangingAnchorAlignment();
    AnchorDoesNotFlipWhenOffsetOrArrowIsEnabled();
    ArrowAlignmentUsesTheAnchorInsteadOfTriggerCenter();
    ArrowsPointTowardTheTriggerOnEveryResolvedSide();
    ArrowJoinCoversBothSidesOfTheSurfaceEdge();
    TriggerCaptureEnablesContentOnTheNextFrame();
    PopoverOpenStateOwnsItsDeferredRegistration();
    PopoverOwnsOpenCallbacksAndOutsideDismissal();
    TooltipOverlayOwnsRequestsTransitionsAndPositioning();
    TooltipDelayOwnsAndCancelsPendingText();
    DisabledTooltipOverlayIgnoresEveryShowPath();
}
