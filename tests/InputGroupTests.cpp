/* Ported from crates/ui/src/input/group.rs and group/tests.rs.

   GroupAppearance is the seam for the one #[test]: validation colour wins
   over focus and stays visible when the group is disabled. The gpui::test
   gallery clicks need TestAppContext; builder fields are checked here. */

#include "Test.h"

using namespace gpui::component;

static void ValidationTakesPrecedenceOverFocusAndRemainsVisibleWhenDisabled() {
    for (int mode = 0; mode < 2; mode++) {
        App app;
        ThemeSet(&app, mode ? ThemeMode::Dark : ThemeMode::Light);
        const Theme& th = ThemeNow(&app);

        InputGroupAppearance focused =
            InputGroupAppearance::New(th, true, false, false);
        utassert(focused.border.r == th.ring.r && focused.hasRing);

        InputGroupAppearance disabled =
            InputGroupAppearance::New(th, true, true, false);
        utassert(disabled.border.r == th.inputBorder.r && !disabled.hasRing);

        for (int f = 0; f < 2; f++) {
            for (int d = 0; d < 2; d++) {
                InputGroupAppearance invalid =
                    InputGroupAppearance::New(th, f != 0, d != 0, true);
                utassert(invalid.border.r == th.danger.r && invalid.hasRing);
            }
        }
        AppGlobalClear(&app);
    }
}

static void TheBuilderKeepsTheLastControlAndAddonAlignment() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    InputState field;
    InputState notes;
    notes.kind = InputKind::Textarea;

    InputGroup* group =
        InputGroup::New(&cx, StrL("group"))
            ->Input(component::Input::New(&cx, StrL("replaced"), &field)
                        ->AriaLabel(StrL("Replaced input")))
            ->Input(component::Textarea::New(&cx, StrL("message"), &notes)
                        ->AriaLabel(StrL("Message"))
                        ->Readonly(true))
            ->Disabled(true)
            ->Invalid(true)
            ->WithSize(UiSize::Small)
            ->Addon(InputGroupAddon::New(&cx, StrL("footer"))
                        ->Align(InputGroupAddonAlignment::BlockEnd)
                        ->Child(InputGroupText::New(&cx)
                                    ->Child(TextEl(arena, StrL("Help")))
                                    ->IntoEl())
                        ->Child(InputGroupButton::New(&cx, StrL("send"))
                                    ->WithVariant(ButtonVariant::Primary)
                                    ->Label(StrL("Send"))
                                    ->IntoEl()));
    utassert(group->textarea && group->textarea->state == &notes);
    utassert(!group->input);
    utassert(group->disabled && group->invalid);
    utassert(group->size == UiSize::Small);
    utassert(group->addons.len == 1);
    utassert(group->addons[0]->alignment == InputGroupAddonAlignment::BlockEnd);
    utassert(group->addons[0]->children.len == 2);

    InputGroupButton* icon = InputGroupButton::New(&cx, StrL("icon"))
                                 ->Icon(IconName::Copy)
                                 ->WithSize(UiSize::Small);
    utassert(icon->size == UiSize::Small);
    utassert(icon->button && icon->button->icon == IconName::Copy);
    utassert(!icon->button->label.s);

    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

// render_control: an inline addon takes pl_2 / pr_2 of the control's inset.
// InputGroupButton::render_in_group: a ghost XSmall button is a 24px box
// (a square for an icon alone) with the muted hover, a 14px icon and a
// transparent 1px border.
static void InlineAddonsInsetTheControlAndButtonsAreCompact() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    const Theme& th = ThemeNow(&app);
    InputState field;
    InputGroupButton* star = InputGroupButton::New(&cx, StrL("star"))
                                 ->Icon(IconName::Star);
    InputGroupButton* reset = InputGroupButton::New(&cx, StrL("reset"))
                                  ->Label(StrL("Reset"));
    InputGroup* group =
        InputGroup::New(&cx, StrL("group"))
            ->Input(component::Input::New(&cx, StrL("url"), &field))
            ->Addon(InputGroupAddon::New(&cx, StrL("scheme"))
                        ->Child(TextEl(arena, StrL("https://"))))
            ->Addon(InputGroupAddon::New(&cx, StrL("actions"))
                        ->Align(InputGroupAddonAlignment::InlineEnd)
                        ->Child(star)
                        ->Child(reset));
    group->IntoEl();
    utassert(group->controlEl != nullptr);
    if (group->controlEl) {
        utassertnear(group->controlEl->style.pad.left, 8);
        utassertnear(group->controlEl->style.pad.right, 8);
    }
    utassert(star && reset);
    if (star && reset) {
        utassert(star->button->variant == ButtonVariant::Custom);
        utassert(star->button->customVariant.hover.r == th.muted.r);
        utassertnear(star->button->contentIconPx, 14);
        utassertnear(reset->button->contentGap, 4);
    }
    El* button = InputGroupButton::New(&cx, StrL("solo"))
                     ->Icon(IconName::Star)
                     ->RenderInGroup(false);
    utassertnear(button->style.width, 24);
    utassertnear(button->style.height, 24);
    utassertnear(button->style.radius, th.radius * 0.5f);
    utassertnear(button->style.border, 1);
    utassert(button->style.borderColor.a == 0);

    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

// element.rs request_layout: a multi-line field is at least its rows tall —
// the configured rows of a plain one, and an auto-grow one's current rows,
// up to max_rows. A Rows() on the builder still wins.
static void ATextareaIsAtLeastItsRowsTall() {
    App app;
    component::Init(&app);
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    // Medium: input_py 8 above and below, a 1px border each side.
    const float kPadded = 2 * 8.f + 2;
    InputState plain;
    plain.kind = InputKind::Textarea;
    TextareaSetRows(&plain, 3);
    El* e = component::Textarea::New(&cx, StrL("plain"), &plain)->IntoEl();
    utassertnear(e->style.height, 3 * 20 + kPadded);

    InputState grow;
    grow.kind = InputKind::Textarea;
    TextareaSetAutoGrow(&grow, 2, 3);
    LayoutModeSetRows(&grow.mode, 5);
    e = component::Textarea::New(&cx, StrL("grow"), &grow)->IntoEl();
    utassertnear(e->style.height, 3 * 20 + kPadded);

    e = component::Textarea::New(&cx, StrL("asked"), &plain)->Rows(4)->IntoEl();
    utassertnear(e->style.height, 4 * 20 + kPadded);

    // crates/kit/tests/input/textarea.rs
    // rows_set_the_minimum_height_of_a_plain_textarea: five rows add exactly
    // four times what the second row adds.
    float heights[3] = {};
    const int kRows[3] = {1, 2, 5};
    InputState counted[3];
    for (int i = 0; i < 3; i++) {
        counted[i].kind = InputKind::Textarea;
        TextareaSetRows(&counted[i], kRows[i]);
        heights[i] = component::Textarea::New(&cx, StrL("rows"), &counted[i])
                         ->IntoEl()
                         ->style.height;
    }
    float line = heights[1] - heights[0];
    // "a second row adds height"
    utassert(line > 0);
    utassertnear(heights[2] - heights[0], line * 4);

    WindowKeyedFree(win);
    EntityDropAll(&app);
    AppGlobalClear(&app);
    ArenaDelete(arena);
    delete win;
}

void TestInputGroup() {
    TestSuite("input_group");
    ValidationTakesPrecedenceOverFocusAndRemainsVisibleWhenDisabled();
    TheBuilderKeepsTheLastControlAndAddonAlignment();
    InlineAddonsInsetTheControlAndButtonsAreCompact();
    ATextareaIsAtLeastItsRowsTall();
}
