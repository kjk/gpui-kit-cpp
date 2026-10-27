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

    static El* Render(TipButtons* self, Ctx* cx) {
        El* row = Div(cx->a)->W(400)->H(200)->FlexRow()->Gap(20);
        if (self->shown[0]) {
            row->Child(component::Button::New(cx, StrL("a"))
                           ->Label(StrL("A"))
                           ->Tooltip(StrL("First"))
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

void TestTooltip() {
    TestSuite("tooltip");
    RemovingButtonHidesVisibleTooltip();
    RemovingButtonCancelsPendingTooltip();
    PendingButtonShowsAfterTheDelay();
    RemovingOldButtonPreservesNewTooltip();
}
