/* Ported from crates/base/src/toolbar.rs and crates/component/src/toolbar.rs.
 *
 * Base's toolbar owns the toolbar role and roving Left/Right focus among the
 * tab stops inside it, wrapping at either end; a group is a named Group.
 * Component's toolbar passes one density to every sized control, whatever
 * order the builder calls came in, caps it at Medium, and turns a hosted
 * Button into a compact ghost command. */

#include "Test.h"

namespace {

// Three tab stops in a row, laid out inside a 100x20 toolbar at the origin,
// and one outside it.
struct RoveFixture {
    App app;
    Window* win = nullptr;
    Arena* arena = nullptr;
    Ctx cx = {};

    RoveFixture(int items) {
        win = new Window();
        win->app = &app;
        arena = ArenaNew();
        cx = {&app, win, arena, {}};
        for (int i = 0; i < items; i++) {
            FocusRect fr;
            fr.id = 100 + i;
            fr.bounds = {(float)(i * 25), 0, 20, 20};
            VecAppend(win->focusEls, fr);
        }
        FocusRect outside;
        outside.id = 999;
        outside.bounds = {300, 0, 20, 20};
        VecAppend(win->focusEls, outside);
        win->focusId = 100;
    }
    ~RoveFixture() {
        WindowKeyedFree(win);
        EntityDropAll(&app);
        delete win;
        ArenaDelete(arena);
    }
};

const Bounds kToolbarBox = {0, 0, 100, 20};

KeyEvent Arrow(int vk) {
    KeyEvent ev;
    ev.vk = vk;
    ev.down = true;
    return ev;
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

// toolbar.rs: arrow_keys_rove_focus_across_items,
// toolbar_with_a_single_item_keeps_focus_on_it and
// disabled_toolbar_ignores_arrow_keys. The key handler is called as the
// window calls it once a toolbar item has focus.
static void ArrowKeysRoveFocusAcrossItems() {
    RoveFixture f(3);
    ToolbarState state;
    state.bounds = kToolbarBox;
    KeyEvent right = Arrow(KeyRight);
    KeyEvent left = Arrow(KeyLeft);
    ToolbarState::OnKeyDown(&state, &f.cx, &right);
    utassert(f.win->focusId == 101);
    utassert(!right.propagate);
    ToolbarState::OnKeyDown(&state, &f.cx, &right);
    utassert(f.win->focusId == 102);
    // Wrapping: past the last item, focus returns to the first, and never
    // reaches the tab stop outside the toolbar.
    ToolbarState::OnKeyDown(&state, &f.cx, &right);
    utassert(f.win->focusId == 100);
    ToolbarState::OnKeyDown(&state, &f.cx, &left);
    utassert(f.win->focusId == 102);
}

static void AToolbarWithOneItemKeepsFocusOnIt() {
    RoveFixture f(1);
    utassert(!ToolbarMoveFocus(f.win, kToolbarBox, true));
    utassert(!ToolbarMoveFocus(f.win, kToolbarBox, false));
    utassert(f.win->focusId == 100);
}

static void ADisabledToolbarIgnoresArrowKeys() {
    RoveFixture f(2);
    ToolbarState state;
    state.bounds = kToolbarBox;
    state.disabled = true;
    KeyEvent right = Arrow(KeyRight);
    ToolbarState::OnKeyDown(&state, &f.cx, &right);
    utassert(f.win->focusId == 100);
    utassert(right.propagate);
}

// toolbar.rs: group_exposes_group_role_and_accessible_name. The toolbar
// itself is a horizontal Toolbar.
static void RolesAndNames() {
    RoveFixture f(0);
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
    RoveFixture f(0);
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
    ADisabledToolbarIgnoresArrowKeys();
    RolesAndNames();
    StyledToolbarSizesItsControls();
}
