#include "Story.h"

// Options rows this page adds to the toolbar.
enum {
    FormOptHorizontal = ToolbarOptHorizontal,
    FormOptColumns = ToolbarOptColumns,
};

// The four titles the Name field's prefix select offers.
static const component::SearchableItem kNamePrefixes[] = {
    {StrL("Mr."), StrL("Mr."), 0, false, IconName::None},
    {StrL("Mrs."), StrL("Mrs."), 0, false, IconName::None},
    {StrL("Ms."), StrL("Ms."), 0, false, IconName::None},
    {StrL("Dr."), StrL("Dr."), 0, false, IconName::None},
};

struct FormStory {
    Entity<component::SelectState> namePrefix = {};
    InputState name;
    InputState email;
    // TextareaState: the same engine, told it spans more than one line.
    InputState bio;
    Entity<ColorPickerState> color = {};
    // Rust binds both the switch and the checkbox to this one field.
    bool subscribe = false;
    bool horizontal = false;
    bool twoColumns = false;
    StoryToolbarState toolbar;
    bool seeded = false;

    static El* Render(FormStory* self, Ctx* cx);
};

static void FormToolbarAct(FormStory* self, Ctx* cx, const ClickEvent*,
                           intptr_t act) {
    if (act == FormOptHorizontal) {
        self->horizontal = !self->horizontal;
    } else if (act == FormOptColumns) {
        self->twoColumns = !self->twoColumns;
    } else {
        StoryToolbarApply(&self->toolbar, nullptr, (int)act);
    }
    Notify(cx);
}

static void FocusName(FormStory* self, Ctx* cx, const ClickEvent*) {
    self->name.focused = true;
    self->email.focused = false;
    Notify(cx);
}
static void FocusEmail(FormStory* self, Ctx* cx, const ClickEvent*) {
    self->email.focused = true;
    self->name.focused = false;
    Notify(cx);
}
static void ToggleSubscribe(FormStory* self, Ctx* cx, const ClickEvent*) {
    self->subscribe = !self->subscribe;
    Notify(cx);
}
static void TogglePrefix(FormStory* self, Ctx* cx, const ClickEvent*) {
    component::SelectToggleOpen(self->namePrefix.Get(cx), cx);
    Notify(cx);
}

El* FormStory::Render(FormStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        self->namePrefix = component::SelectState::New(cx->app);
        if (component::SelectState* st = self->namePrefix.Get(cx)) {
            component::SearchableListSelectOnly(st->List(), 0);
        }
        InputSetValue(&self->name, StrL("Jason Lee"));
        self->bio.kind = InputKind::Textarea;
        InputSetValue(&self->bio,
                      StrL("Hello \xe4\xb8\x96\xe7\x95\x8c\xef\xbc\x8cthis "
                           "is GPUI Kit."));
        InputSetPlaceholder(&self->email, StrL("Enter text here..."));
        // default_value(indigo_500())
        self->color = ColorPickerStateNew(cx);
        if (ColorPickerState* state = self->color.Get(cx)) {
            ColorPickerSetValue(state, 0x6366f1);
        }
    }
    if (self->name.focused) {
        cx->win->input = &self->name;
    } else if (self->email.focused) {
        cx->win->input = &self->email;
    }

    El* page = Div(a)->FlexCol()->Gap(12)->Pad(16)->W(kFill);
    StoryToolbarOpt opts[2] = {
        {"Horizontal", self->horizontal, FormOptHorizontal},
        {"Multiple columns", self->twoColumns, FormOptColumns},
    };
    page->Child(
        StoryToolbarOptions(cx, self, opts, 2, Listen(cx, &FormToolbarAct)));
    page->Child(component::Separator::Horizontal(cx)->IntoEl());

    // Name carries a title select inside the input: Rust's
    // `Input::pl_0().prefix(div().w(px(90.)).child(Select::pr_0()
    // .appearance(false)))`.
    El* prefix = Div(a)->W(90)->Child(
        component::Select::New(cx, StrL("name-prefix"), self->namePrefix)
            ->Items(kNamePrefixes, 4)
            ->Appearance(false)
            ->WithSize(self->toolbar.size)
            ->W(90)
            ->OnToggle(Listen(cx, &TogglePrefix))
            ->IntoEl());

    component::Form* form =
        component::Form::New(cx)
            ->WithSize(self->toolbar.size)
            ->Horizontal(self->horizontal)
            ->Columns(self->twoColumns ? 2 : 1)
            ->Field(StrL("Name"),
                    component::Input::New(cx, StrL("form-name"), &self->name)
                        ->Prefix(prefix)
                        ->OnFocus(Listen(cx, &FocusName))
                        ->IntoEl())
            ->Field(StrL("Email"),
                    component::Input::New(cx, StrL("form-email"), &self->email)
                        ->OnFocus(Listen(cx, &FocusEmail))
                        ->IntoEl())
            ->Required()
            ->Field(StrL("Bio"),
                    component::Textarea::New(cx, StrL("form-bio"), &self->bio)
                        ->Rows(5)
                        ->IntoEl())
            ->Align(component::FieldAlign::Start)
            ->Description(StrL("Use at most 100 words to describe yourself."))
            // label_indent(false): no label column, so the field starts at
            // the form's own edge.
            ->Field(Str{}, StoryTxt(cx,
                                    StrL("This is a full width form "
                                         "field."),
                                    14, th.foreground))
            ->LabelIndent(false)
            ->SpanAll()
            ->Field(StrL("Please select your birthday"),
                    component::DatePicker::New(cx)->Day(0)->IntoEl())
            ->Description(
                StrL("Select your birthday, we will send you a gift."))
            ->Field(Str{}, component::Switch::New(cx, StrL("subscribe"))
                               ->Label(StrL("Subscribe our newsletter"))
                               ->Checked(self->subscribe)
                               ->OnClick(Listen(cx, &ToggleSubscribe))
                               ->IntoEl())
            ->LabelIndent(!self->horizontal || !self->twoColumns)
            ->Field(StrL("Theme color"),
                    component::ColorSelect::New(cx, self->color)
                        ->WithSize(self->toolbar.size)
                        ->IntoEl())
            ->Field(Str{}, component::Checkbox::New(cx, StrL("future-events"))
                               ->Label(StrL("Use this color for future "
                                            "events"))
                               ->Checked(self->subscribe)
                               ->OnClick(Listen(cx, &ToggleSubscribe))
                               ->IntoEl())
            ->LabelIndent(!self->horizontal || !self->twoColumns);
    page->Child(form->IntoEl());
    return page;
}

STORY_PAGE(StoryForm, FormStory);
