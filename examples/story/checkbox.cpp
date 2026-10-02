#include "Story.h"

struct CheckboxStory {
    // check1..check6; only check2 starts on.
    bool checks[6] = {false, true};
    StoryToolbarState toolbar;

    static El* Render(CheckboxStory* self, Ctx* cx);
};

// The checkbox fills the listener's value with the state it lands on, so
// which box it was has to come from the handler — one per box, the way each
// Rust closure names its own field.
static void SetCheck(CheckboxStory* self, Ctx* cx, int ix, int64_t v) {
    self->checks[ix] = v != 0;
    Notify(cx);
}
#define STORY_CHECK_HANDLER(N)                                               \
    static void SetCheck##N(CheckboxStory* self, Ctx* cx, const ClickEvent*, \
                            int64_t v) {                                     \
        SetCheck(self, cx, N, v);                                            \
    }
STORY_CHECK_HANDLER(0)
STORY_CHECK_HANDLER(1)
STORY_CHECK_HANDLER(2)
STORY_CHECK_HANDLER(3)
STORY_CHECK_HANDLER(4)
STORY_CHECK_HANDLER(5)
#undef STORY_CHECK_HANDLER

// The muted supporting line under a label.
static El* CheckHint(Ctx* cx, Str s) {
    return TextEl(cx->a, s)->Font(12)->Fg(ThemeNow(cx->app).mutedFg)->Wrap();
}

El* CheckboxStory::Render(CheckboxStory* self, Ctx* cx) {
    Arena* a = cx->a;
    UiSize size = self->toolbar.size;
    El* page = Div(a)->FlexCol()->JustifyStart()->Gap(12)->W(kFill);
    page->Child(StoryToolbar(cx, self));

    El* def = StorySection(
        cx, "Default", "Checked and unchecked options can be mixed freely.");
    // Each checkbox is its own child of the section, which lays them out in
    // a wrapping row.
    StorySectionAdd(def, component::Checkbox::New(cx, StrL("updates"))
                             ->WithSize(size)
                             ->Checked(self->checks[0])
                             ->Label(StrL("Product updates"))
                             ->OnClick(Listen(cx, &SetCheck0))
                             ->IntoEl());
    StorySectionAdd(def, component::Checkbox::New(cx, StrL("remember"))
                             ->WithSize(size)
                             ->Checked(self->checks[1])
                             ->Label(StrL("Remember this device"))
                             ->OnClick(Listen(cx, &SetCheck1))
                             ->IntoEl());
    page->Child(def);

    El* bare =
        StorySection(cx, "Without label",
                     "The label can be supplied by surrounding content.");
    StorySectionAdd(bare, component::Checkbox::New(cx, StrL("unlabelled"))
                              ->WithSize(size)
                              ->Checked(self->checks[2])
                              ->OnClick(Listen(cx, &SetCheck2))
                              ->IntoEl());
    page->Child(bare);

    El* dis = StorySection(cx, "Disabled",
                           "Both checked and unchecked values remain visible.");
    StorySectionBody(dis)->W(512);
    El* disRow = Div(a)->FlexRow()->Gap(24)->ItemsCenter();
    disRow->Child(component::Checkbox::New(cx, StrL("disabled-checked"))
                      ->WithSize(size)
                      ->Label(StrL("Checked"))
                      ->Checked(true)
                      ->Disabled(true)
                      ->IntoEl());
    disRow->Child(component::Checkbox::New(cx, StrL("disabled-unchecked"))
                      ->WithSize(size)
                      ->Label(StrL("Unchecked"))
                      ->Checked(false)
                      ->Disabled(true)
                      ->IntoEl());
    StorySectionAdd(dis, disRow);
    page->Child(dis);

    // .w_128().v_flex().items_center().gap_5(): the section itself is the
    // column, not a div inside it.
    El* labs = StorySection(cx, "Labels",
                            "Labels can wrap and include supporting content.");
    StorySectionBody(labs)->FlexCol()->W(512)->ItemsCenter()->Gap(20);
    StorySectionAdd(labs,
                    component::Checkbox::New(cx, StrL("description"))
                        ->WithSize(size)
                        ->W(320)
                        ->Checked(self->checks[3])
                        ->Label(StrL("Automatic updates"))
                        ->Child(CheckHint(
                            cx, StrL("Download updates when the application is "
                                     "idle.")))
                        ->OnClick(Listen(cx, &SetCheck3))
                        ->IntoEl());
    StorySectionAdd(labs, component::Checkbox::New(cx, StrL("wrapping"))
                              ->WithSize(size)
                              ->W(320)
                              ->Checked(self->checks[5])
                              ->Label(StrL("Notify me when a new device signs "
                                           "in to my account"))
                              ->OnClick(Listen(cx, &SetCheck5))
                              ->IntoEl());
    // The supporting line here is markdown, so the terms of service is a
    // link rather than three more words.
    El* terms = component::TextView::New(
                    cx, StrL("Read the [terms of service](https://github.com) "
                             "before continuing."))
                    ->Font(12)
                    ->IntoEl()
                    ->Fg(ThemeNow(cx->app).mutedFg);
    StorySectionAdd(labs, component::Checkbox::New(cx, StrL("markdown"))
                              ->WithSize(size)
                              ->W(320)
                              ->Checked(self->checks[4])
                              ->Label(StrL("Accept the terms"))
                              ->Child(terms)
                              ->OnClick(Listen(cx, &SetCheck4))
                              ->IntoEl());
    page->Child(labs);
    return page;
}

STORY_PAGE(StoryCheckbox, CheckboxStory);
