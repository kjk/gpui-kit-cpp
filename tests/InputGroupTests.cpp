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

void TestInputGroup() {
    TestSuite("input_group");
    ValidationTakesPrecedenceOverFocusAndRemainsVisibleWhenDisabled();
    TheBuilderKeepsTheLastControlAndAddonAlignment();
}
