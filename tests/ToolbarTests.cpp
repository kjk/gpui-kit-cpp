/* Ported from crates/base/src/toolbar.rs and crates/component/src/toolbar.rs.
 *
 * Base's toolbar owns the toolbar role and roving Left/Right focus among the
 * focusables its handle contains, wrapping at either end; a group is a named
 * Group. Component's toolbar passes one density to every sized control,
 * whatever order the builder calls came in, caps it at Medium, and turns a
 * hosted Button into a compact ghost command. */

#include "Test.h"

namespace {

// toolbar.rs's harness: a toolbar of `count` 20px tab stops, here with one
// more tab stop on either side of it, which roving must never reach.
struct RoveView {
    FocusHandle before = {};
    FocusHandle items[3] = {};
    FocusHandle after = {};
    int count = 3;
    bool disabled = false;

    static El* Item(Arena* a, FocusHandle h) {
        return Div(a)->W(20)->H(20)->TrackFocus(h)->TabStop(true);
    }

    static El* Render(RoveView* self, Ctx* cx) {
        Arena* a = cx->a;
        Toolbar* bar = Toolbar::New(cx, StrL("toolbar"))
                           ->Disabled(self->disabled);
        for (int i = 0; i < self->count; i++) {
            bar->Child(Item(a, self->items[i]));
        }
        return Div(a)
            ->FlexRow()
            ->Child(Item(a, self->before))
            ->Child(bar->IntoEl())
            ->Child(Item(a, self->after));
    }
};

struct Rove {
    App* app = nullptr;
    Window* win = nullptr;
    Entity<RoveView> view = {};

    RoveView* View() const { return view.Get(app); }
    bool Focused(FocusHandle h) const { return FocusHandleIsFocused(win, h); }
};

Rove RoveOpen(int count, bool disabled = false) {
    Rove r;
    r.app = TestAppNew();
    r.view = EntityNew<RoveView>(r.app);
    RoveView* v = r.View();
    v->count = count;
    v->disabled = disabled;
    v->before = FocusHandleNew(r.app);
    v->after = FocusHandleNew(r.app);
    for (FocusHandle& h : v->items) {
        h = FocusHandleNew(r.app);
    }
    r.win = TestWindowOpen(r.app, r.view);
    TestFocus(r.win, v->items[0]);
    return r;
}

} // namespace

// toolbar.rs: test_toolbar_builder, test_toolbar_defaults,
// test_toolbar_group_builder.
static void BaseToolbarBuilders() {
    App app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, nullptr, a, {}};
    Toolbar* bar = Toolbar::New(&cx, StrL("toolbar"));
    utassert(!bar->disabled && bar->root->first == nullptr);
    bar->Disabled(true)->Child(Div(a));
    utassert(bar->disabled && bar->root->first != nullptr);

    ToolbarGroup* group = ToolbarGroup::New(&cx, StrL("history-group"))
                              ->Label(StrL("History"))
                              ->Child(Div(a))
                              ->Child(Div(a));
    utassert(StrEq(group->label, StrL("History")));
    utassert(group->root->first && group->root->first->next &&
             !group->root->first->next->next);
    ArenaDelete(a);
    EntityDropAll(&app);
}

// toolbar.rs: arrow_keys_rove_focus_across_items.
static void ArrowKeysRoveFocusAcrossItems() {
    Rove r = RoveOpen(3);
    RoveView* v = r.View();
    TestSimulateKeystrokes(r.win, "right");
    utassert(r.Focused(v->items[1]));
    TestSimulateKeystrokes(r.win, "right");
    utassert(r.Focused(v->items[2]));
    // Wrapping: past the last item, focus returns to the first, and never
    // reaches the tab stops on either side of the toolbar.
    TestSimulateKeystrokes(r.win, "right");
    utassert(r.Focused(v->items[0]));
    TestSimulateKeystrokes(r.win, "left");
    utassert(r.Focused(v->items[2]));
    // The toolbar itself is not a tab stop: Tab leaves through its items.
    TestSimulateKeystrokes(r.win, "tab");
    utassert(r.Focused(v->after));
    TestAppFree(r.app);
}

// toolbar.rs: toolbar_with_a_single_item_keeps_focus_on_it.
static void AToolbarWithOneItemKeepsFocusOnIt() {
    Rove r = RoveOpen(1);
    RoveView* v = r.View();
    TestSimulateKeystrokes(r.win, "right");
    utassert(r.Focused(v->items[0]));
    TestSimulateKeystrokes(r.win, "left");
    utassert(r.Focused(v->items[0]));
    TestAppFree(r.app);
}

// toolbar.rs: disabled_toolbar_ignores_arrow_keys.
static void ADisabledToolbarIgnoresArrowKeys() {
    Rove r = RoveOpen(2, true);
    RoveView* v = r.View();
    TestSimulateKeystrokes(r.win, "right");
    utassert(r.Focused(v->items[0]));
    TestAppFree(r.app);
}

// FocusHandle::contains: the toolbar's handle contains its items and not the
// stops beside it, which is what keeps the roving inside.
static void TheToolbarHandleContainsItsItems() {
    Rove r = RoveOpen(3);
    RoveView* v = r.View();
    FocusHandle bar = {};
    for (int i = 0; i < r.win->focusEls.len; i++) {
        if (!r.win->focusEls[i].tabStop) {
            bar.id = r.win->focusEls[i].id;
        }
    }
    utassert(bar.IsValid());
    utassert(FocusHandleContains(r.win, bar, v->items[0]));
    utassert(FocusHandleContains(r.win, bar, v->items[2]));
    utassert(!FocusHandleContains(r.win, bar, v->before));
    utassert(!FocusHandleContains(r.win, bar, v->after));
    utassert(!FocusHandleContains(r.win, v->items[0], bar));
    utassert(FocusHandleContainsFocused(r.win, bar));
    TestFocus(r.win, v->after);
    utassert(!FocusHandleContainsFocused(r.win, bar));
    TestAppFree(r.app);
}

namespace {

// A window for the builders that read keyed element state.
struct Fixture {
    App app;
    Window* win = nullptr;
    Arena* arena = nullptr;
    Ctx cx = {};

    Fixture() {
        win = new Window();
        win->app = &app;
        arena = ArenaNew();
        cx = {&app, win, arena, {}};
    }
    ~Fixture() {
        WindowKeyedFree(win);
        EntityDropAll(&app);
        delete win;
        ArenaDelete(arena);
    }
};

} // namespace

// toolbar.rs: group_exposes_group_role_and_accessible_name. The toolbar
// itself is a horizontal Toolbar.
static void RolesAndNames() {
    Fixture f;
    El* group = ToolbarGroup::New(&f.cx, StrL("history"))
                    ->Label(StrL("History"))
                    ->Child(Div(f.arena)->W(20)->H(20))
                    ->IntoEl();
    utassert(group->accessibility.role == AccessibilityRole::Group);
    utassert(StrEq(group->accessibility.label, StrL("History")));
    El* bar = Toolbar::New(&f.cx, StrL("toolbar"))->IntoEl();
    utassert(bar->accessibility.role == AccessibilityRole::Toolbar);
    utassert(bar->accessibility
                 .orientation == AccessibilityOrientation::Horizontal);
}

namespace {
UiSize gProbeSizes[4];
int gProbeCount = 0;

struct SizeProbe {
    Ctx* cx = nullptr;
    UiSize size = UiSize::Medium;
    SizeProbe* WithSize(UiSize value) {
        size = value;
        return this;
    }
    El* IntoEl() {
        if (gProbeCount < 4) gProbeSizes[gProbeCount++] = size;
        return Div(cx->a);
    }
};
} // namespace

// component toolbar.rs: test_toolbar_builder, test_toolbar_default,
// large_size_falls_back_to_medium,
// toolbar_prepares_buttons_as_compact_ghost_commands,
// toolbar_size_propagates_to_items_independent_of_builder_order and
// toolbar_group_propagates_its_size_to_items.
static void StyledToolbarSizesItsControls() {
    Fixture f;
    component::Init(&f.app);
    component::Toolbar* fresh = component::Toolbar::New(&f.cx, StrL("t"));
    utassert(fresh->size == UiSize::Small && !fresh->disabled &&
             fresh->items.len == 0);
    component::Toolbar* built = component::Toolbar::New(&f.cx, StrL("t"))
                                    ->Content(TextEl(f.arena, StrL("New")))
                                    ->Content(TextEl(f.arena, StrL("Open")))
                                    ->Content(TextEl(f.arena, StrL("Settings")))
                                    ->WithSize(UiSize::Small);
    utassert(built->items.len == 3 && built->size == UiSize::Small);
    utassert(component::Toolbar::New(&f.cx, StrL("t"))
                 ->WithSize(UiSize::Large)
                 ->size == UiSize::Medium);
    utassert(component::ToolbarGroup::New(&f.cx, StrL("g"))
                 ->WithSize(UiSize::Large)
                 ->size == UiSize::Medium);

    component::Button* button =
        component::PrepareForToolbar(component::Button::New(&f.cx, StrL("c")));
    utassert(button->variant == component::ButtonVariant::Ghost);
    utassert(button->compact);

    // The size is applied when the toolbar is built, after every child was
    // added, so it reaches them all whatever came first.
    SizeProbe probes[3];
    gProbeCount = 0;
    component::Toolbar* sized = component::Toolbar::New(&f.cx, StrL("sized"));
    for (SizeProbe& p : probes) {
        p.cx = &f.cx;
        sized->Child(&p);
    }
    sized->WithSize(UiSize::Small)->IntoEl();
    utassert(gProbeCount == 3);
    for (int i = 0; i < 3; i++) utassert(gProbeSizes[i] == UiSize::Small);

    SizeProbe groupProbe;
    groupProbe.cx = &f.cx;
    gProbeCount = 0;
    component::ToolbarGroup::New(&f.cx, StrL("group"))
        ->Child(&groupProbe)
        ->WithSize(UiSize::Small)
        ->IntoEl();
    utassert(gProbeCount == 1 && gProbeSizes[0] == UiSize::Small);
    AppGlobalClear(&f.app);
}

void TestToolbar() {
    TestSuite("toolbar");
    BaseToolbarBuilders();
    ArrowKeysRoveFocusAcrossItems();
    AToolbarWithOneItemKeepsFocusOnIt();
    TheToolbarHandleContainsItsItems();
    ADisabledToolbarIgnoresArrowKeys();
    RolesAndNames();
    StyledToolbarSizesItsControls();
}
