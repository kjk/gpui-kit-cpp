#include "Story.h"

struct AccordionStory {
    // open_ixs / styled_open_ixs, both `vec![0]`.
    bool accordionOpen[3] = {true, false, false};
    bool accordionStyledOpen[3] = {true, false, false};
    StoryToolbarState toolbar;
    StoryAccordionOptions options;

    static El* Render(AccordionStory* self, Ctx* cx);
};

static void ToggleOpen(bool* flags, int n, int i, bool multiple);
static void OnAccDefault(AccordionStory* self, Ctx*, const ClickEvent*,
                         intptr_t i) {
    ToggleOpen(self->accordionOpen, 3, (int)i, self->options.multiple);
}
static void OnAccStyled(AccordionStory* self, Ctx*, const ClickEvent*,
                        intptr_t i) {
    ToggleOpen(self->accordionStyledOpen, 3, (int)i, self->options.multiple);
}

static void ToggleOpen(bool* flags, int n, int i, bool multiple) {
    if (i < 0 || i >= n) {
        return;
    }
    if (multiple) {
        flags[i] = !flags[i];
        return;
    }
    bool next = !flags[i];
    for (int k = 0; k < n; k++) {
        flags[k] = false;
    }
    flags[i] = next;
}

// settings_item: the icon sits in a rounded square, and the content lines up
// with the title rather than with the icon.
static component::AccordionItem* SettingsItem(Ctx* cx, IconName icon,
                                              const char* title, Str tag,
                                              const char* body, bool open) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    Rgba iconBg = RgbaOpacity(th.secondary, 0.5f);
    El* head = Div(a)->FlexRow()->Gap(8)->ItemsCenter();
    head->Child(Div(a)
                    ->FlexNone()
                    ->W(32)
                    ->H(32)
                    ->FlexRow()
                    ->ItemsCenter()
                    ->JustifyCenter()
                    ->Radius(8)
                    ->Bg(iconBg)
                    ->Child(IconEl(a, icon, UiIconPx(cx, UiSize::Small))
                                ->Fg(th.mutedFg)));
    head->Child(TextEl(a, StoryDup(cx, title))->Semibold());
    if (tag.s) {
        head->Child(component::Tag::New(cx, tag)
                        ->Success()
                        ->Outline()
                        ->WithSize(UiSize::Small)
                        ->IntoEl());
    }

    component::AccordionStyle titleStyle;
    titleStyle.padT = 8;
    titleStyle.padB = 8;
    component::AccordionStyle contentStyle;
    contentStyle.fg = th.mutedFg;
    // Past the icon square, so the text starts under the title.
    contentStyle.padL = 52;
    contentStyle.padT = 0;
    contentStyle.padB = 12;

    return component::AccordionItem::New(cx)
        ->Title(head)
        ->TitleStyle(titleStyle)
        ->ContentStyle(contentStyle)
        ->Child(StoryDup(cx, body))
        ->Open(open);
}

El* AccordionStory::Render(AccordionStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    El* page = Div(a)->FlexCol()->Gap(24)->W(kFill)->ItemsCenter();
    page->Child(StoryToolbarWithOptions(cx, self));

    const char* titles[] = {"Is it accessible?", "Can it hold any content?",
                            "Is it animated?"};
    const IconName icons[] = {IconName::Info, IconName::Inbox, IconName::Moon};
    const char* bodies[] = {
        "Yes. Each item is a button with an aria-expanded state, so screen "
        "readers announce whether the section is open, and the whole group can "
        "be reached with the keyboard.",
        nullptr, // a v_flex, below
        "Yes. Expanding and collapsing animates the height of the content, and "
        "the chevron rotates to follow. Items below move along with it.",
    };

    // Item 1 takes any element as its content, not just text — and says so.
    El* body1 = Div(a)->FlexCol()->Gap(12);
    body1->Child(TextEl(a, StrL("An item takes any element as its content, "
                                "not just text. The height animation measures "
                                "whatever you put in it."))
                     ->Wrap());
    body1->Child(Div(a)
                     ->FlexRow()
                     ->Gap(16)
                     ->ItemsCenter()
                     ->Child(component::Switch::New(cx, StrL("switch1"))
                                 ->Label(StrL("Switch"))
                                 ->IntoEl())
                     ->Child(component::Checkbox::New(cx, StrL("checkbox1"))
                                 ->Label(StrL("Or a Checkbox"))
                                 ->IntoEl()));

    component::Accordion* acc = component::Accordion::New(cx, StrL("test"))
                                    ->Multiple(self->options.multiple)
                                    ->Bordered(self->options.bordered)
                                    ->Disabled(self->options.disabled)
                                    ->WithSize(self->toolbar.size)
                                    ->OnToggle(Listen(cx, &OnAccDefault));
    for (int i = 0; i < 3; i++) {
        component::AccordionItem* it = component::AccordionItem::New(cx)
                                           ->Open(self->accordionOpen[i])
                                           ->Title(Str(titles[i]));
        if (self->options.icon) {
            it->Icon(icons[i]);
        }
        if (i == 1) {
            it->Child(body1);
        } else {
            it->Child(Str(bodies[i]));
        }
        acc->Item(it);
    }
    acc->Item(component::AccordionItem::New(cx)
                  ->Title(StrL("Disabled item"))
                  ->Disabled(true)
                  ->Child(StrL("This item cannot be expanded.")));

    El* def =
        StorySection(cx, "Default", "Expand one item at a time by default.");
    StorySectionAdd(def, Div(a)->W(480)->Child(acc->IntoEl()));
    page->Child(def);

    component::Accordion* styled =
        component::Accordion::New(cx, StrL("custom-style"))
            ->Multiple(self->options.multiple)
            ->Disabled(self->options.disabled)
            ->OnToggle(Listen(cx, &OnAccStyled));
    styled->Item(SettingsItem(
        cx, IconName::Settings, "Account Settings", StrL("New"),
        "Manage your account preferences, security settings, and personal "
        "information. You can also configure two-factor authentication here.",
        self->accordionStyledOpen[0]));
    styled->Item(SettingsItem(
        cx, IconName::Eye, "Privacy & Security", Str{},
        "Control who can see your profile and how your data is used.",
        self->accordionStyledOpen[1]));
    styled->Item(SettingsItem(
        cx, IconName::Info, "Help & Support", Str{},
        "Browse the documentation, or get in touch with the support team.",
        self->accordionStyledOpen[2]));
    El* custom = StorySection(cx, "Custom style", nullptr);
    // A tinted frame around the card.
    El* frame = Div(a)
                    ->W(480)
                    ->Pad(4)
                    ->Radius(16)
                    ->Bg(RgbaOpacity(th.secondary, 0.5f))
                    ->Border(1, RgbaOpacity(th.border, 0.5f))
                    ->Child(styled->IntoEl());
    StorySectionAdd(custom, frame);
    page->Child(custom);
    return page;
}

STORY_PAGE(StoryAccordion, AccordionStory);
