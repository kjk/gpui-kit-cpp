#include "Story.h"

struct StepperStory {
    // One step per stepper, the way the Rust story keeps four.
    int step[4] = {1, 0, 2, 0};
    bool disabled = false;
    StoryToolbarState toolbar;

    static El* Render(StepperStory* self, Ctx* cx);
};

// One handler per stepper, which is what the Rust story's four closures are:
// the argument is the step the stepper reports.
static void SetStep0(StepperStory* self, Ctx* cx, const ClickEvent*,
                     int64_t step) {
    self->step[0] = (int)step;
    Notify(cx);
}
static void SetStep1(StepperStory* self, Ctx* cx, const ClickEvent*,
                     int64_t step) {
    self->step[1] = (int)step;
    Notify(cx);
}
static void SetStep2(StepperStory* self, Ctx* cx, const ClickEvent*,
                     int64_t step) {
    self->step[2] = (int)step;
    Notify(cx);
}
static void SetStep3(StepperStory* self, Ctx* cx, const ClickEvent*,
                     int64_t step) {
    self->step[3] = (int)step;
    Notify(cx);
}

// ToggleDisabled: the page's one Options row.
enum {
    StepperActDisabled = 3700
};

static void StepperAct(StepperStory* self, Ctx* cx, const ClickEvent*,
                       int64_t act) {
    if (act == StepperActDisabled) {
        self->disabled = !self->disabled;
    } else {
        StoryToolbarApply(&self->toolbar, nullptr, (int)act);
    }
    Notify(cx);
}

// The label under a step, and the description under that.
static El* StepText(Ctx* cx, Str title, const char* desc, bool center) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* col = Div(a)->FlexCol();
    if (center) {
        col->ItemsCenter();
    }
    col->Child(StoryTxt(cx, title, 14, th.foreground));
    if (desc) {
        // `v_flex().child("Step 1").child("Description for step 1.")`: the
        // description is a plain child and takes the stepper's own colour,
        // not muted_foreground.
        col->Child(StoryTxt(cx, Str(desc), 14, th.foreground));
    }
    return col;
}

El* StepperStory::Render(StepperStory* self, Ctx* cx) {
    Arena* a = cx->a;
    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);
    StoryToolbarOpt opts[1] = {
        {"Disabled", self->disabled, StepperActDisabled}};
    page->Child(
        StoryToolbarOptions(cx, self, opts, 1, Listen(cx, &StepperAct)));

    El* h = StorySection(cx, "Horizontal Stepper", nullptr);
    StorySectionBody(h)->FlexCol()->W(480);
    StorySectionAdd(
        h, component::Stepper::New(cx, StrL("stepper0"))
               ->WithSize(self->toolbar.size)
               ->Disabled(self->disabled)
               ->SelectedIndex(self->step[0])
               ->Item(component::StepperItem::New(cx)
                          ->Child(StepText(cx, StrL("Step 1"), nullptr, false)))
               ->Item(component::StepperItem::New(cx)
                          ->Child(StepText(cx, StrL("Step 2"), nullptr, false)))
               ->Item(component::StepperItem::New(cx)
                          ->Child(StepText(cx, StrL("Step 3"), nullptr, false)))
               ->OnClick(Listen(cx, &SetStep0))
               ->IntoEl());
    page->Child(h);

    El* ic = StorySection(cx, "Icon Stepper", nullptr);
    StorySectionBody(ic)->FlexCol()->W(480);
    static const IconName kIcons[4] = {IconName::Calendar, IconName::Inbox,
                                       IconName::Frame, IconName::Info};
    static const char* kIconLabels[4] = {"Order Details", "Shipping", "Preview",
                                         "Finish"};
    component::Stepper* icons = component::Stepper::New(cx, StrL("stepper1"))
                                    ->WithSize(self->toolbar.size)
                                    ->Disabled(self->disabled)
                                    ->SelectedIndex(self->step[1])
                                    ->OnClick(Listen(cx, &SetStep1));
    for (int i = 0; i < 4; i++) {
        icons->Item(component::StepperItem::New(cx)->Icon(kIcons[i])->Child(
            StepText(cx, Str(kIconLabels[i]), nullptr, false)));
    }
    StorySectionAdd(ic, icons->IntoEl());
    page->Child(ic);

    El* v = StorySection(cx, "Vertical Stepper", nullptr);
    StorySectionBody(v)->FlexCol()->W(480);
    static const IconName kVIcons[4] = {IconName::Building2, IconName::Asterisk,
                                        IconName::Folder,
                                        IconName::CircleCheck};
    static const char* kVTitles[4] = {"Step 1", "Step 2", "Step 3", "Step 4"};
    static const char* kVDescs[4] = {
        "Description for step 1.", "Description for step 2.",
        "Description for step 3.", "Description for step 4."};
    component::Stepper* vert = component::Stepper::New(cx, StrL("stepper3"))
                                   ->Vertical()
                                   ->WithSize(self->toolbar.size)
                                   ->Disabled(self->disabled)
                                   ->SelectedIndex(self->step[2])
                                   // .items_center() on the vertical stepper.
                                   ->ItemsCenter()
                                   ->OnClick(Listen(cx, &SetStep2));
    for (int i = 0; i < 4; i++) {
        // pb_8 on all but the last: the room the connector runs down.
        El* text = StepText(cx, Str(kVTitles[i]), kVDescs[i], false);
        if (i < 3) {
            text->PadB(32);
        }
        vert->Item(
            component::StepperItem::New(cx)->Icon(kVIcons[i])->Child(text));
    }
    StorySectionAdd(v, vert->IntoEl());
    page->Child(v);

    El* tc = StorySection(cx, "Text Center", nullptr);
    StorySectionBody(tc)->FlexCol()->W(480);
    static const char* kTcDescs[3] = {"Desc for step 1.", "Desc for step 2.",
                                      "Desc for step 3."};
    component::Stepper* center = component::Stepper::New(cx, StrL("stepper4"))
                                     ->WithSize(self->toolbar.size)
                                     ->Disabled(self->disabled)
                                     ->TextCenter(true)
                                     ->SelectedIndex(self->step[3])
                                     ->OnClick(Listen(cx, &SetStep3));
    for (int i = 0; i < 3; i++) {
        center->Item(component::StepperItem::New(cx)->Child(
            StepText(cx, StoryFmt(cx, "Step %d", i + 1), kTcDescs[i], true)));
    }
    StorySectionAdd(tc, center->IntoEl());
    page->Child(tc);
    return page;
}

STORY_PAGE(StoryStepper, StepperStory);
