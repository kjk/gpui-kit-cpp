/* Ported from crates/base/src/popup.rs resolved_corner.
 *
 * Which point of the trigger the content is placed against. The bottom
 * anchors use the deliberately unusual `origin.y - height` arithmetic in
 * the pinned source. */

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
    // Trigger is (0, 100, 80, 20). resolved_corner is (80, 80), then the
    // content's BottomRight corner lands there.
    utassertnear(content->x, 50.f);
    utassertnear(content->y, 70.f);
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

void TestPopup() {
    TestSuite("popup");
    ThePopupSurfaceBlocksThePanelItCovers();
    TheTopAnchorsTakeTheTopEdge();
    TheBottomAnchorsMatchUpstreamsSubtractedHeight();
    TheSideAnchorsFallBackToTheOrigin();
    PopupContentUsesThePinnedCornerMarginAndDeferredLayer();
    TriggerCaptureEnablesContentOnTheNextFrame();
    PopoverOpenStateOwnsItsDeferredRegistration();
    PopoverOwnsOpenCallbacksAndOutsideDismissal();
    TooltipOverlayOwnsRequestsTransitionsAndPositioning();
    TooltipDelayOwnsAndCancelsPendingText();
    DisabledTooltipOverlayIgnoresEveryShowPath();
}
