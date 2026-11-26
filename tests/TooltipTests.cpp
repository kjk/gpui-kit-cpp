/* Ported from crates/component/src/tooltip.rs.
 *
 * A pending or visible tooltip used to stay up when its trigger left the
 * tree without a pointer event. The frame that no longer paints that
 * trigger cancels it. A later trigger, and one that is still there, keep
 * theirs. */

#include "Test.h"

struct TipButtons {
    bool shown[2] = {true, false};
    int delay = 0;
    uint32_t action = 0;

    static El* Render(TipButtons* self, Ctx* cx) {
        El* row = Div(cx->a)->W(400)->H(200)->FlexRow()->Gap(20);
        if (self->shown[0]) {
            row->Child(component::Button::New(cx, StrL("a"))
                           ->Label(StrL("A"))
                           ->TooltipWithAction(StrL("First"), self->action,
                                               "TipButtons")
                           ->TooltipShowDelay(self->delay)
                           ->IntoEl()
                           ->W(80)
                           ->H(30));
        }
        if (self->shown[1]) {
            row->Child(component::Button::New(cx, StrL("b"))
                           ->Label(StrL("B"))
                           ->Tooltip(StrL("Second"))
                           ->TooltipShowDelay(self->delay)
                           ->IntoEl()
                           ->W(80)
                           ->H(30));
        }
        return row;
    }
};

static Window* OpenTips(App* app, Entity<TipButtons> buttons) {
    Window* win = TestWindowOpen(app, buttons, 400, 200);
    TestDraw(win);
    return win;
}

static bool TipVisible(Window* win) {
    const TooltipOverlay* overlay = TooltipShowing(win);
    return overlay && overlay->hasContent;
}

static bool TipPending(Window* win) {
    const TooltipOverlay* overlay = TooltipShowing(win);
    return overlay && overlay->hasPending;
}

// removing_button_hides_visible_tooltip. A redraw while the button is
// still there keeps the tooltip. Removing the button drops it.
static void RemovingButtonHidesVisibleTooltip() {
    App* app = TestAppNew();
    component::Init(app);
    Entity<TipButtons> buttons = EntityNew<TipButtons>(app);
    Window* win = OpenTips(app, buttons);
    TestSimulateMouseMove(win, {20, 15});
    TestDraw(win);
    utassert(TipVisible(win));
    TestDraw(win);
    utassert(TipVisible(win));
    buttons.Get(app)->shown[0] = false;
    NotifyEntity(app, buttons.id, win);
    TestDraw(win);
    utassert(!TipVisible(win));
    TestAppFree(app);
}

// removing_button_cancels_pending_tooltip. The delay has not elapsed, so
// the removed trigger never builds a tooltip.
static void RemovingButtonCancelsPendingTooltip() {
    App* app = TestAppNew();
    component::Init(app);
    Entity<TipButtons> buttons = EntityNew<TipButtons>(app);
    buttons.Get(app)->delay = 500;
    Window* win = OpenTips(app, buttons);
    TestSimulateMouseMove(win, {20, 15});
    TestDraw(win);
    utassert(TipPending(win));
    utassert(!TipVisible(win));
    buttons.Get(app)->shown[0] = false;
    NotifyEntity(app, buttons.id, win);
    TestDraw(win);
    TestAdvanceClock(app, 500);
    TestDraw(win);
    utassert(!TipVisible(win));
    utassert(!TipPending(win));
    TestAppFree(app);
}

// pending_button_shows_after_deferred_work_drains. A trigger that stays
// builds its tooltip when the delay elapses.
static void PendingButtonShowsAfterTheDelay() {
    App* app = TestAppNew();
    component::Init(app);
    Entity<TipButtons> buttons = EntityNew<TipButtons>(app);
    buttons.Get(app)->delay = 500;
    Window* win = OpenTips(app, buttons);
    TestSimulateMouseMove(win, {20, 15});
    TestDraw(win);
    utassert(TipPending(win));
    TestAdvanceClock(app, 500);
    TestDraw(win);
    utassert(TipVisible(win));
    TestAppFree(app);
}

static void ActionTooltipKeepsTheShortcutThroughTheDelay() {
    App* app = TestAppNew();
    component::Init(app);
    uint32_t action = ActionOf(StrL("test::TooltipAction"));
    const KeyBinding bindings[] = {{"secondary-b", action, "TipButtons"}};
    KeymapBind(bindings, dimofi(bindings));
    auto buttons = EntityNew<TipButtons>(app);
    buttons.Get(app)->action = action;
    buttons.Get(app)->delay = 500;
    Window* win = OpenTips(app, buttons);
    TestSimulateMouseMove(win, {20, 15});
    TestDraw(win);
    utassert(TipPending(win));
    TestAdvanceClock(app, 500);
    TestDraw(win);
    const TooltipOverlay* overlay = TooltipShowing(win);
    utassert(overlay && overlay->hasContent &&
             StrEq(overlay->content.text, StrL("First")) &&
             StrEq(overlay->content.shortcut, PlatSecondaryIsCommand()
                                                  ? StrL("\u2318B")
                                                  : StrL("Ctrl+B")));
    TestAppFree(app);
    KeymapClear();
}

// removing_old_button_preserves_new_tooltip. The pointer has moved to the
// second button. Removing the first does not take the second's tooltip.
static void RemovingOldButtonPreservesNewTooltip() {
    App* app = TestAppNew();
    component::Init(app);
    Entity<TipButtons> buttons = EntityNew<TipButtons>(app);
    buttons.Get(app)->shown[1] = true;
    Window* win = OpenTips(app, buttons);
    TestSimulateMouseMove(win, {20, 15});
    TestDraw(win);
    utassert(TipVisible(win));
    TestSimulateMouseMove(win, {120, 15});
    TestDraw(win);
    utassert(TipVisible(win));
    utassert(StrEq(TooltipShowing(win)->content.text, StrL("Second")));
    buttons.Get(app)->shown[0] = false;
    NotifyEntity(app, buttons.id, win);
    TestDraw(win);
    utassert(TipVisible(win));
    utassert(StrEq(TooltipShowing(win)->content.text, StrL("Second")));
    TestAppFree(app);
}

struct WrappingTooltip {
    Str text;
    Bounds surface;

    static El* Build(Ctx* cx, void* data) {
        auto* self = (WrappingTooltip*)data;
        El* tooltip =
            component::Tooltip::New(cx, self->text)->MaxW(200)->IntoEl();
        tooltip->first->BoundsOut(&self->surface);
        return tooltip;
    }

    static El* Render(WrappingTooltip*, Ctx* cx) {
        return Div(cx->a)->SizeFull();
    }
};

static Bounds WrappingTooltipBounds(Str text) {
    App* app = TestAppNew();
    component::Init(app);
    auto view = EntityNew<WrappingTooltip>(app);
    auto* self = view.Get(app);
    self->text = text;
    Window* win = TestWindowOpen(app, view, 500, 400);
    auto entity = EntityNew<TooltipOverlay>(app);
    win->tooltip = entity.id;
    Ctx cx = {app, win, win->frameArena, entity.id};
    auto request =
        TooltipRequest::New({10, 10, 100, 30}, &WrappingTooltip::Build, self);
    request.WithShowDelay(0);
    auto* overlay = entity.Get(app);
    overlay->enabled = true;
    overlay->RequestShow(request, win, &cx);
    TestDraw(win);
    Bounds surface = self->surface;
    TestAppFree(app);
    return surface;
}

static void LongTextWrapsWithinTheTooltipWidth() {
    Bounds shortTip = WrappingTooltipBounds(StrL("tip"));
    Bounds longTip =
        WrappingTooltipBounds(StrL("Band AD_WBGT_20240621T00-20240621T23 - "
                                   "WBGT [time=477499] with a few more words"));
    utassert(shortTip.h > 0.f && shortTip.w < 200.f);
    utassertnear(longTip.w, 200.f);
    utassert(longTip.h > shortTip.h * 2.f);
}

struct NestedTooltipTriggers {
    static El* Render(NestedTooltipTriggers*, Ctx* cx) {
        El* inner = component::Button::New(cx, StrL("inner"))
                        ->Label(StrL("Inner"))
                        ->Tooltip(StrL("inner tooltip"))
                        ->IntoEl()
                        ->Absolute()
                        ->Left(100)
                        ->Top(20)
                        ->W(100)
                        ->H(30);
        inner->rootTooltip = true;
        return Div(cx->a)->SizeFull()->Child(Div(cx->a)
                                                 ->PathClick(StrL("outer"))
                                                 ->W(300)
                                                 ->H(100)
                                                 ->Tip(StrL("outer tooltip"))
                                                 ->Child(inner));
    }
};

// tooltip.rs: a_managed_tooltip_hides_the_native_tooltip_around_it.
static void AManagedTooltipHidesTheNativeTooltipAroundIt() {
    App* app = TestAppNew();
    component::Init(app);
    auto view = EntityNew<NestedTooltipTriggers>(app);
    auto root = ShellRoot::New(app, view.id);
    Window* win = TestWindowOpen(app, root, 400, 220);
    TestDraw(win);
    TestSimulateMouseMove(win, {20, 20});
    TestAdvanceClock(app, kTooltipShowDelayMs + 50);
    auto* outer = (TooltipOverlay*)EntityGet(app, win->tooltip);
    utassert(outer && outer->hasContent &&
             StrEq(outer->content.text, "outer tooltip"));
    TestSimulateMouseMove(win, {120, 30});
    TestAdvanceClock(app, kTooltipShowDelayMs + 50);
    auto* inner = (TooltipOverlay*)EntityGet(app, win->rootTooltip);
    utassert(inner && inner->hasContent &&
             StrEq(inner->content.text, "inner tooltip"));
    utassert(outer && !outer->hasContent && !outer->hasPending);
    utassert(TooltipShowing(win) == inner);
    TestSimulateMouseMove(win, {20, 20});
    TestAdvanceClock(app, kTooltipShowDelayMs + 50);
    utassert(outer && outer->hasContent);
    utassert(inner && !inner->hasContent && !inner->hasPending);
    TestAppFree(app);
}

void TestTooltip() {
    TestSuite("tooltip");
    RemovingButtonHidesVisibleTooltip();
    RemovingButtonCancelsPendingTooltip();
    PendingButtonShowsAfterTheDelay();
    ActionTooltipKeepsTheShortcutThroughTheDelay();
    RemovingOldButtonPreservesNewTooltip();
    LongTextWrapsWithinTheTooltipWidth();
    AManagedTooltipHidesTheNativeTooltipAroundIt();
}
