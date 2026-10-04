/* Ported from the Radio tests in crates/kit/tests/controls.rs.
 *
 * Rust drives the group through its test window (`window.within("plan")
 * .click(ix)`); this goes through the headless test platform and clicks the
 * row the group laid out for that radio. */

#include "Test.h"

struct Plans {
    bool groupDisabled = false;
    int selected = -1;
    // The column the group lays its radios out in, from the last frame.
    // Frame-arena memory, read only between frames.
    El* column = nullptr;

    static void OnPick(Plans* self, Ctx* cx, const ClickEvent*, int64_t ix) {
        self->selected = (int)ix;
        Notify(cx);
    }

    static El* Render(Plans* self, Ctx* cx) {
        El* group = component::RadioGroup::Vertical(cx, StrL("plan"))
                        ->Disabled(self->groupDisabled)
                        ->Selected(self->selected)
                        ->Child(component::Radio::New(cx, StrL("free"))
                                    ->Label(StrL("Free")))
                        ->Child(component::Radio::New(cx, StrL("pro"))
                                    ->Label(StrL("Pro"))
                                    ->Disabled(true))
                        ->OnClick(Listen(cx, &Plans::OnPick))
                        ->IntoEl();
        self->column = group->first;
        return group;
    }
};

// `window.within("plan").click(ix)`.
static void ClickRadio(Window* win, Plans* plans, int ix) {
    El* row = plans->column ? plans->column->first : nullptr;
    for (int i = 0; row && i < ix; i++) {
        row = row->next;
    }
    utassert(row != nullptr);
    if (!row) {
        return;
    }
    Bounds b = row->Bounds();
    TestSimulateClick(win, Point{b.x + 8.f, b.y + b.h / 2.f});
}

// radio_group_keeps_a_disabled_item_disabled.
static void RadioGroupKeepsADisabledItemDisabled() {
    App* app = TestAppNew();
    component::Init(app);
    Entity<Plans> view = EntityNew<Plans>(app);
    Window* win = TestWindowOpen(app, view, 320, 240);
    TestRunUntilParked(app);
    TestDraw(win);
    Plans* plans = view.Get(app);

    ClickRadio(win, plans, 1);
    // "a disabled item inside an enabled group"
    utassert(plans->selected == -1);

    TestDraw(win);
    ClickRadio(win, plans, 0);
    utassert(plans->selected == 0);

    // A disabled group still disables every item.
    plans->groupDisabled = true;
    plans->selected = -1;
    AppInvalidate(win);
    TestFlushEffects(app);
    TestDraw(win);
    ClickRadio(win, plans, 0);
    // "an enabled item inside a disabled group"
    utassert(plans->selected == -1);
    TestAppFree(app);
}

void TestRadio() {
    TestSuite("radio");
    RadioGroupKeepsADisabledItemDisabled();
}
