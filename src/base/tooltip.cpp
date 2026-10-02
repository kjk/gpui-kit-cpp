#include "base/tooltip.h"
#include "base/element_ext.h"

namespace gpui {

El* Tooltip::New(Ctx* cx, Str id) {
    return UiRoot(cx->a, id, 0)->Role(AccessibilityRole::Tooltip);
}

TooltipTransition TooltipTransition::Enter(uint64_t value) {
    TooltipTransition out;
    out.epoch = value;
    return out;
}

TooltipTransition TooltipTransition::Switch(uint64_t value, Bounds from,
                                            Bounds to) {
    TooltipTransition out;
    out.kind = TooltipTransitionKind::Switch;
    out.epoch = value;
    out.previous = from;
    out.current = to;
    return out;
}

TooltipRequest TooltipRequest::New(Bounds bounds, TooltipBuilder fn,
                                   void* data) {
    TooltipRequest out;
    out.triggerBounds = bounds;
    out.build = fn;
    out.buildData = data;
    return out;
}

TooltipRequest TooltipRequest::Text(Bounds bounds, Str value) {
    TooltipRequest out;
    out.triggerBounds = bounds;
    out.text = value;
    return out;
}

TooltipRequest& TooltipRequest::Placement(gpui::Placement value) {
    preferredPlacement = value;
    hasPreferredPlacement = true;
    return *this;
}

static void TooltipRequestClear(TooltipRequest* request) {
    if (!request) {
        return;
    }
    StrFree(request->text);
    *request = {};
}

static void TooltipRequestCopy(TooltipRequest* into,
                               const TooltipRequest& request) {
    TooltipRequestClear(into);
    *into = request;
    into->text = StrDup(request.text);
}

static void TooltipRequestMove(TooltipRequest* into, TooltipRequest* from) {
    TooltipRequestClear(into);
    *into = *from;
    *from = {};
}

TooltipOverlay::~TooltipOverlay() {
    TooltipRequestClear(&content);
    TooltipRequestClear(&pending);
}

TooltipOverlay* TooltipOverlay::RenderWith(TooltipRenderer value, void* data) {
    renderer = value;
    rendererData = data;
    return this;
}

uint64_t TooltipOverlay::NextEpoch() {
    return ++epoch;
}

template <typename T, typename E>
static Listener TooltipTimerListener(Ctx* cx, void (*fn)(T*, Ctx*, const E*)) {
    Listener out;
    out.SetFn(fn);
    out.view = cx->self;
    return out;
}

static void TooltipCancelShow(Window* window, TooltipOverlay* overlay) {
    if (overlay->showTask) {
        WindowCancelTimer(window, overlay->showTask);
        overlay->showTask = 0;
    }
}

static void TooltipCancelHide(Window* window, TooltipOverlay* overlay) {
    if (overlay->hideTask) {
        WindowCancelTimer(window, overlay->hideTask);
        overlay->hideTask = 0;
    }
}

void TooltipOverlay::RequestShow(const TooltipRequest& request, Window* window,
                                 Ctx* cx) {
    if (!enabled) {
        return;
    }
    TooltipCancelHide(window, this);
    bool wasVisible = hasContent;
    if (wasVisible || hadRecentTooltip) {
        TooltipCancelShow(window, this);
        hasPreviousBounds = wasVisible;
        if (wasVisible) {
            previousBounds = content.triggerBounds;
        }
        TooltipRequestCopy(&content, request);
        TooltipRequestClear(&pending);
        hasContent = true;
        hasPending = false;
        isSwitching = wasVisible;
        animationEpoch++;
        Notify(cx);
        return;
    }

    TooltipCancelShow(window, this);
    TooltipRequestCopy(&pending, request);
    hasPending = true;
    hasPreviousBounds = false;
    isSwitching = false;
    NextEpoch();
    showTask =
        WindowSetTimeout(window, kTooltipShowDelayMs,
                         TooltipTimerListener(cx, &TooltipOverlay::OnShow));
}

void TooltipOverlay::RequestHide(Window* window, Ctx* cx) {
    TooltipCancelShow(window, this);
    if (hasPending) {
        TooltipRequestClear(&pending);
        hasPending = false;
    }
    if (!hasContent || hideTask) {
        return;
    }
    NextEpoch();
    hadRecentTooltip = true;
    hideTask =
        WindowSetTimeout(window, kTooltipGracePeriodMs,
                         TooltipTimerListener(cx, &TooltipOverlay::OnHide));
}

void TooltipOverlay::Hide(Ctx* cx) {
    bool changed = hasContent || hasPending || hasPreviousBounds ||
                   hadRecentTooltip || showTask || hideTask;
    TooltipCancelShow(cx->win, this);
    TooltipCancelHide(cx->win, this);
    TooltipRequestClear(&content);
    TooltipRequestClear(&pending);
    hasContent = false;
    hasPending = false;
    hasPreviousBounds = false;
    hadRecentTooltip = false;
    isSwitching = false;
    if (changed) {
        Notify(cx);
    }
}

void TooltipOverlay::OnShow(TooltipOverlay* self, Ctx* cx, const TickEvent*) {
    self->showTask = 0;
    if (!self->hasPending) {
        return;
    }
    TooltipRequestMove(&self->content, &self->pending);
    self->hasContent = true;
    self->hasPending = false;
    self->hasPreviousBounds = false;
    self->isSwitching = false;
    self->animationEpoch++;
    Notify(cx);
}

void TooltipOverlay::OnHide(TooltipOverlay* self, Ctx* cx, const TickEvent*) {
    self->hideTask = 0;
    TooltipRequestClear(&self->content);
    self->hasContent = false;
    self->hasPreviousBounds = false;
    self->hadRecentTooltip = false;
    self->isSwitching = false;
    Notify(cx);
}

static El* TooltipTextView(Ctx* cx, Str text) {
    const RuntimeStyle& theme = RuntimeStyleNow(cx->app);
    return Tooltip::New(cx, StrL("tooltip-popup"))
        ->FlexRow()
        ->ItemsCenter()
        ->Margin(12)
        ->Bg(theme.popover)
        ->Fg(theme.popoverForeground)
        ->Border(1, theme.border)
        ->Radius(6)
        ->PadX(8)
        ->PadY(2)
        ->Font(14)
        ->Gap(12)
        ->Child(TextEl(cx->a, text));
}

El* TooltipOverlay::Render(TooltipOverlay* self, Ctx* cx) {
    if (!self->hasContent) {
        return nullptr;
    }
    El* view = self->content.build
                   ? self->content.build(cx, self->content.buildData)
                   : TooltipTextView(cx, self->content.text);
    if (!view) {
        return nullptr;
    }
    TooltipTransition transition =
        self->isSwitching && self->hasPreviousBounds
            ? TooltipTransition::Switch(self->animationEpoch,
                                        self->previousBounds,
                                        self->content.triggerBounds)
            : TooltipTransition::Enter(self->animationEpoch);
    El* rendered = self->renderer ? self->renderer(cx, view, transition,
                                                   self->rendererData)
                                  : Div(cx->a)->Child(view);
    TooltipPositioner* positioner =
        TooltipPositioner::New(cx, self->content.triggerBounds);
    if (self->content.hasPreferredPlacement) {
        positioner->Placement(self->content.preferredPlacement);
    }
    return positioner->Child(rendered)
        ->IntoEl()
        ->DeferredLayer(kPaintLayerTooltip);
}

TooltipPositioner* TooltipPositioner::New(Ctx* cx, Bounds triggerBounds) {
    TooltipPositioner* out = ArenaNew<TooltipPositioner>(cx->a);
    out->positioner = Positioner::Side(cx, triggerBounds)
                          ->Margin(kTooltipWindowMargin);
    return out;
}

TooltipPositioner* TooltipPositioner::Placement(gpui::Placement value) {
    positioner->Placement(value);
    return this;
}

TooltipPositioner* TooltipPositioner::Child(El* child) {
    positioner->Child(child);
    return this;
}

El* TooltipPositioner::IntoEl() {
    return positioner->IntoEl();
}

static TooltipOverlay* TooltipGet(Window* win) {
    if (!win || !win->app) {
        return nullptr;
    }
    if (!win->tooltip.IsValid()) {
        Entity<TooltipOverlay> overlay = EntityNew<TooltipOverlay>(win->app);
        win->tooltip = overlay.id;
    }
    return (TooltipOverlay*)EntityGet(win->app, win->tooltip);
}

static Ctx TooltipContext(Window* win, EntityId self) {
    Ctx cx;
    cx.app = win->app;
    cx.win = win;
    cx.a = win->frameArena;
    cx.self = self;
    return cx;
}

// The overlay the window's root view owns (ShellRoot::tooltip_overlay), while
// it has one and it is alive.
static TooltipOverlay* TooltipRootOverlay(Window* win) {
    if (!win || !win->app || !win->rootTooltip.IsValid()) {
        return nullptr;
    }
    return (TooltipOverlay*)EntityGet(win->app, win->rootTooltip);
}

static TooltipOverlay* TooltipWindowOverlay(Window* win) {
    if (!win || !win->app || !win->tooltip.IsValid()) {
        return nullptr;
    }
    return (TooltipOverlay*)EntityGet(win->app, win->tooltip);
}

void TooltipRequestShow(Window* win, Str text, Bounds triggerBounds,
                        int placement, bool rootLayer) {
    // A trigger the root view drew asks the root's overlay; any other, or one
    // in a window whose root has none, the window's.
    TooltipOverlay* overlay = rootLayer ? TooltipRootOverlay(win) : nullptr;
    EntityId id = overlay ? win->rootTooltip : EntityId{};
    if (!overlay) {
        overlay = TooltipGet(win);
        id = win ? win->tooltip : EntityId{};
    }
    if (!overlay) {
        return;
    }
    // One tooltip at a time: the other layer lets go of whatever it held.
    TooltipOverlay* other = overlay == TooltipRootOverlay(win)
                                ? TooltipWindowOverlay(win)
                                : TooltipRootOverlay(win);
    if (other && (other->hasContent || other->hasPending)) {
        EntityId otherId =
            other == TooltipRootOverlay(win) ? win->rootTooltip : win->tooltip;
        Ctx otherCx = TooltipContext(win, otherId);
        other->Hide(&otherCx);
    }
    // Re-entering the same visible trigger only cancels its pending hide.
    if (overlay->hasContent && overlay->content.text.s &&
        base::StrEq(overlay->content.text, text) &&
        overlay->content.triggerBounds.x == triggerBounds.x &&
        overlay->content.triggerBounds.y == triggerBounds.y &&
        overlay->content.triggerBounds.w == triggerBounds.w &&
        overlay->content.triggerBounds.h == triggerBounds.h) {
        TooltipCancelHide(win, overlay);
        return;
    }
    Ctx cx = TooltipContext(win, id);
    TooltipRequest request = TooltipRequest::Text(triggerBounds, text);
    // managed_tooltip_with_placement: `Some(placement)` is the trigger's
    // preferred side, `None` leaves the overlay's own placement.
    if (placement >= (int)gpui::Placement::Top &&
        placement <= (int)gpui::Placement::Right) {
        request.Placement((gpui::Placement)placement);
    }
    overlay->RequestShow(request, win, &cx);
}

void TooltipRequestHide(Window* win) {
    if (TooltipOverlay* overlay = TooltipWindowOverlay(win)) {
        Ctx cx = TooltipContext(win, win->tooltip);
        overlay->RequestHide(win, &cx);
    }
    if (TooltipOverlay* overlay = TooltipRootOverlay(win)) {
        Ctx cx = TooltipContext(win, win->rootTooltip);
        overlay->RequestHide(win, &cx);
    }
}

void TooltipHide(Window* win) {
    if (TooltipOverlay* overlay = TooltipWindowOverlay(win)) {
        Ctx cx = TooltipContext(win, win->tooltip);
        overlay->Hide(&cx);
    }
    if (TooltipOverlay* overlay = TooltipRootOverlay(win)) {
        Ctx cx = TooltipContext(win, win->rootTooltip);
        overlay->Hide(&cx);
    }
}

const TooltipOverlay* TooltipShowing(Window* win) {
    // The root's overlay when it holds a tooltip, the window's otherwise.
    const TooltipOverlay* root = TooltipRootOverlay(win);
    if (root && (root->hasContent || root->hasPending)) {
        return root;
    }
    return TooltipWindowOverlay(win);
}

} // namespace gpui
