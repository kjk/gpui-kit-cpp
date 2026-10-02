#include "Story.h"

struct GroupBoxStory {
    bool email[3] = {false, false, false};
    bool profilePrivate = true;
    bool privateContrib = false;
    bool compactPrivate = true;
    int theme = 2;

    static El* Render(GroupBoxStory* self, Ctx* cx);
};

static void ToggleEmail(GroupBoxStory* self, Ctx* cx, const ClickEvent*,
                        int64_t i) {
    if (i >= 0 && i < 3) {
        self->email[i] = !self->email[i];
    }
    Notify(cx);
}
static void TogglePrivate(GroupBoxStory* self, Ctx* cx, const ClickEvent*) {
    self->profilePrivate = !self->profilePrivate;
    Notify(cx);
}
static void ToggleContrib(GroupBoxStory* self, Ctx* cx, const ClickEvent*) {
    self->privateContrib = !self->privateContrib;
    Notify(cx);
}
static void ToggleCompact(GroupBoxStory* self, Ctx* cx, const ClickEvent*) {
    self->compactPrivate = !self->compactPrivate;
    Notify(cx);
}
static void PickTheme(GroupBoxStory* self, Ctx* cx, const ClickEvent*,
                      int64_t i) {
    self->theme = (int)i;
    Notify(cx);
}

// A row of text with a switch pushed to the far edge.
static El* SwitchRow(Ctx* cx, Str label, Str id, bool on, Listener onClick) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* row = Div(a)->FlexRow()->W(kFill)->ItemsCenter()->JustifyBetween();
    row->Child(StoryTxt(cx, label, 16, th.foreground));
    row->Child(component::Switch::New(cx, id)
                   ->Checked(on)
                   ->OnClick(onClick)
                   ->IntoEl());
    return row;
}

El* GroupBoxStory::Render(GroupBoxStory* self, Ctx* cx) {
    Arena* a = cx->a;
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill)->ItemsCenter();

    // Default: the email options, with the primary action under them.
    El* def = StorySection(cx, "Default", nullptr);
    // Rust adds the three checkboxes and the button straight to the
    // GroupBox, so what separates them is its content pane's `gap_4`.
    component::GroupBox* mail =
        component::GroupBox::New(cx, StrL("Email notifications"));
    static const char* kMail[3] = {"All activity", "Product updates",
                                   "Account activity"};
    static const char* kMailIds[3] = {"all", "news-letter", "account-activity"};
    for (int i = 0; i < 3; i++) {
        mail->Child(component::Checkbox::New(cx, Str(kMailIds[i]))
                        ->Label(Str(kMail[i]))
                        ->Checked(self->email[i])
                        ->OnClick(Listen(cx, &ToggleEmail, i))
                        ->IntoEl());
    }
    mail->Child(component::Button::New(cx, StrL("ok"))
                    ->Primary()
                    ->Label(StrL("Save preferences"))
                    ->IntoEl());
    StorySectionBody(def)->W(512);
    StorySectionAdd(def, mail->IntoEl());
    page->Child(def);

    // Filled: two switch rows and a Save.
    El* filled = StorySection(cx, "Filled", nullptr);
    component::GroupBox* activity =
        component::GroupBox::New(cx, StrL("Contributions & activity"))
            ->Id(StrL("activity"))
            ->Fill()
            ->Footer(TextEl(a, StrL("Private contributions never reveal "
                                    "repository names.")));
    activity
        ->Child(SwitchRow(cx, StrL("Make profile private and hide activity"),
                          StrL("profile-private"), self->profilePrivate,
                          Listen(cx, &TogglePrivate)));
    activity->Child(
        SwitchRow(cx, StrL("Include private contributions on my profile"),
                  StrL("private-contributions"), self->privateContrib,
                  Listen(cx, &ToggleContrib)));
    activity->Child(component::Button::New(cx, StrL("btn-1"))
                        ->Primary()
                        ->Label(StrL("Save"))
                        ->IntoEl());
    StorySectionBody(filled)->W(512);
    StorySectionAdd(filled, activity->IntoEl());
    page->Child(filled);

    // Outlined: a vertical radio group.
    El* outlined = StorySection(cx, "Outlined", nullptr);
    // RadioGroup::vertical("theme"): the group owns the selection and reports
    // which index was clicked.
    El* themes = component::RadioGroup::Vertical(cx, StrL("theme"))
                     ->Child(component::Radio::New(cx, StrL("light"))
                                 ->Label(StrL("Light")))
                     ->Child(component::Radio::New(cx, StrL("dark"))
                                 ->Label(StrL("Dark")))
                     ->Child(component::Radio::New(cx, StrL("system"))
                                 ->Label(StrL("System")))
                     ->Selected(self->theme)
                     ->OnClick(Listen(cx, &PickTheme))
                     ->IntoEl();
    StorySectionBody(outlined)->W(512);
    StorySectionAdd(outlined, component::GroupBox::New(cx, StrL("Appearance"))
                                  ->Id(StrL("appearance"))
                                  ->Outline()
                                  ->Child(themes)
                                  ->IntoEl());
    page->Child(outlined);

    El* untitled = StorySection(cx, "Without Title", nullptr);
    StorySectionBody(untitled)->W(512);
    StorySectionAdd(untitled,
                    component::GroupBox::New(cx, Str{})
                        ->Outline()
                        ->Child(SwitchRow(
                            cx, StrL("Make profile private and hide activity"),
                            StrL("compact-private"), self->compactPrivate,
                            Listen(cx, &ToggleCompact)))
                        ->IntoEl());
    page->Child(untitled);

    // Custom Style: title_style and content_style, refined independently.
    El* custom = StorySection(cx, "Custom Style", nullptr);
    StorySectionBody(custom)->W(512);
    Style rootStyle = {};
    rootStyle.bg = ThemeNow(cx->app).groupBox;
    rootStyle.radius = 12;
    rootStyle.pad = EdgesAll(20);
    Style contentStyle = {};
    contentStyle.radius = 12;
    contentStyle.pad = Edges::New(16, 16, 12, 12);
    contentStyle.border = 2;
    StorySectionAdd(
        custom,
        component::GroupBox::New(cx, StrL("This is a custom style"))
            ->Outline()
            ->Refine(rootStyle, StyleFieldBg | StyleFieldRadius | StyleFieldPad)
            ->TitleSemibold()
            ->TitlePadX(12)
            ->ContentStyle(contentStyle,
                           StyleFieldRadius | StyleFieldPad | StyleFieldBorder)
            ->Child(component::TextView::New(
                        cx, StrL("You can use `title_style` to customize "
                                 "the style of the title. And any style in "
                                 "`GroupBox` will apply to the content "
                                 "container."))
                        ->IntoEl())
            ->IntoEl());
    page->Child(custom);
    return page;
}

STORY_PAGE(StoryGroupBox, GroupBoxStory);
