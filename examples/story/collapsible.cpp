#include "Story.h"

// The panels, in the order the sections use them. Rust keys them by name and
// starts SETTINGS, API_KEYS, COMPONENTS_DIR and PROFILE open.
enum {
    CollOrder = 0,
    CollFaq,
    CollUsage,
    CollSettings,
    CollProfile,
    CollApiKeys,
    CollComponentsDir,
    CollUiDir,
    CollCount
};

// The settings rows, and which of them start checked.
enum {
    CollPush = 0,
    CollEmail,
    CollSms,
    CollNoteCount
};

struct CollapsibleStory {
    bool open[CollCount] = {};
    bool checked[CollNoteCount] = {};
    bool seeded = false;
    static El* Render(CollapsibleStory* self, Ctx* cx);
};

static void OnColl(CollapsibleStory* self, Ctx* cx, const ClickEvent*,
                   intptr_t ix) {
    if (ix >= 0 && ix < CollCount) {
        self->open[ix] = !self->open[ix];
    }
    Notify(cx);
}

// One handler per settings row: the checkbox fills the listener's value with
// the state it lands on, so the row has to come from the handler.
static void SetNote(CollapsibleStory* self, Ctx* cx, int ix, intptr_t v) {
    self->checked[ix] = v != 0;
    Notify(cx);
}
static void SetPush(CollapsibleStory* self, Ctx* cx, const ClickEvent*,
                    intptr_t v) {
    SetNote(self, cx, CollPush, v);
}
static void SetEmail(CollapsibleStory* self, Ctx* cx, const ClickEvent*,
                     intptr_t v) {
    SetNote(self, cx, CollEmail, v);
}
static void SetSms(CollapsibleStory* self, Ctx* cx, const ClickEvent*,
                   intptr_t v) {
    SetNote(self, cx, CollSms, v);
}

// chevron(): pointing right when collapsed, down when open, xsmall and muted.
static El* Chevron(Ctx* cx, bool open) {
    Arena* a = cx->a;
    return IconEl(a, open ? IconName::ChevronDown : IconName::ChevronRight,
                  UiIconPx(cx, UiSize::XSmall))
        ->Fg(ThemeNow(cx->app).mutedFg);
}

// panel_row(): a bordered row that frames a piece of summary content.
static El* PanelRow(Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    return Div(a)
        ->FlexRow()
        ->W(kFill)
        ->PadX(12)
        ->PadY(8)
        ->Gap(8)
        ->ItemsCenter()
        ->Radius(th.radiusLg)
        ->Border(1, th.border)
        ->Font(14);
}

// A leaf of the tree: a file icon and its name, indented past the chevron
// the folder rows carry.
static El* FileRow(Ctx* cx, Str name) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    return Div(a)
        ->FlexRow()
        ->H(28)
        ->PadX(8)
        ->Gap(8)
        ->ItemsCenter()
        ->Radius(th.radius)
        ->HoverBg(th.tokens.accent)
        ->Font(14)
        ->Child(Div(a)->W(12)->Shrink0())
        ->Child(IconEl(a, IconName::File, UiIconPx(cx, UiSize::XSmall))
                    ->Fg(th.mutedFg))
        ->Child(TextEl(a, name));
}

// A branch: the chevron, an open or closed folder, and the name.
static El* FolderRow(CollapsibleStory* self, Ctx* cx, int key, Str name) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    bool open = self->open[key];
    return Div(a)
        ->FlexRow()
        ->H(28)
        ->W(kFill)
        ->PadX(8)
        ->Gap(8)
        ->ItemsCenter()
        ->Radius(th.radius)
        ->HoverBg(th.tokens.accent)
        ->Font(14)
        ->OnClick(Listen(cx, &OnColl, key))
        ->Child(Chevron(cx, open))
        ->Child(IconEl(a, open ? IconName::FolderOpen : IconName::Folder,
                       UiIconPx(cx, UiSize::XSmall))
                    ->Fg(th.mutedFg))
        ->Child(TextEl(a, name));
}

// Every section is .w(px(360.)).v_flex().
static El* CollSection(Ctx* cx, const char* title, const char* desc) {
    El* sec = StorySection(cx, title, desc);
    StorySectionBody(sec)->FlexCol()->W(360);
    return sec;
}

El* CollapsibleStory::Render(CollapsibleStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        self->open[CollSettings] = true;
        self->open[CollApiKeys] = true;
        self->open[CollComponentsDir] = true;
        self->open[CollProfile] = true;
        self->checked[CollPush] = true;
    }
    El* page = Div(a)->FlexCol()->W(kFill)->ItemsCenter()->Gap(24);

    // Basic: a trigger beside the title, over a summary that stays visible.
    El* basic = CollSection(
        cx, "Basic",
        "A trigger beside the title, with a summary that stays visible.");
    El* orderHead = Div(a)
                        ->FlexRow()
                        ->W(kFill)
                        ->ItemsCenter()
                        ->JustifyBetween()
                        ->Gap(16)
                        ->PadX(4);
    orderHead->Child(TextEl(a, StrL("Order #4189"))->Font(14)->Semibold());
    orderHead->Child(component::Button::New(cx, StrL("order"))
                         ->Ghost()
                         ->WithSize(UiSize::XSmall)
                         ->Icon(IconName::ChevronsUpDown)
                         ->Tooltip(StrL("Toggle details"))
                         ->OnClick(Listen(cx, &OnColl, CollOrder))
                         ->IntoEl());
    El* status =
        PanelRow(cx)->JustifyBetween()->Bg(RgbaOpacity(th.muted, 0.3f));
    status->Child(TextEl(a, StrL("Status"))->Fg(th.mutedFg));
    status->Child(component::Tag::New(cx, StrL("Shipped"))
                      ->Success()
                      ->WithSize(UiSize::Small)
                      ->IntoEl());
    static const char* const kOrderDetails[2][2] = {
        {"Shipping address", "100 Market St, San Francisco"},
        {"Items", "2x Studio Headphones"},
    };
    El* orderBody = Div(a)->FlexCol()->W(kFill)->Gap(8);
    for (int i = 0; i < 2; i++) {
        orderBody->Child(
            PanelRow(cx)
                ->FlexCol()
                ->ItemsStart()
                ->Gap(0)
                ->Child(TextEl(a, Str(kOrderDetails[i][0]))->Medium())
                ->Child(TextEl(a, Str(kOrderDetails[i][1]))->Fg(th.mutedFg)));
    }
    StorySectionAdd(basic, component::Collapsible::New(cx)
                               ->W(kFill)
                               ->Gap(8)
                               ->Open(self->open[CollOrder])
                               ->Trigger(Div(a)
                                             ->FlexCol()
                                             ->W(kFill)
                                             ->Gap(8)
                                             ->Child(orderHead)
                                             ->Child(status))
                               ->Content(orderBody)
                               ->IntoEl());
    page->Child(basic);

    El* row =
        CollSection(cx, "Row trigger",
                    "The whole row is the trigger, as used by FAQ entries.");
    El* faqTrig =
        Div(a)
            ->FlexRow()
            ->W(kFill)
            ->ItemsCenter()
            ->JustifyBetween()
            ->Gap(8)
            ->OnClick(Listen(cx, &OnColl, CollFaq))
            ->Child(TextEl(a, StrL("How do I reset my password?"))->Font(14))
            ->Child(Chevron(cx, self->open[CollFaq]));
    El* faqBody = Div(a)->PadT(12)->W(kFill)->Child(
        TextEl(a, StrL("Click the Forgot Password link on the sign in "
                       "page, and we will send you an email with "
                       "instructions to create a new one."))
            ->Font(14)
            ->Fg(th.mutedFg)
            ->Wrap());
    StorySectionAdd(row, component::GroupBox::New(cx, Str{})
                             ->Outline()
                             ->Child(component::Collapsible::New(cx)
                                         ->W(kFill)
                                         ->Open(self->open[CollFaq])
                                         ->Trigger(faqTrig)
                                         ->Content(faqBody)
                                         ->IntoEl())
                             ->IntoEl());
    page->Child(row);

    // Bottom trigger: a usage card whose chevron sits on its bottom edge.
    El* bottom = CollSection(
        cx, "Bottom trigger",
        "The trigger sits on the bottom edge of the card it opens.");
    El* usageHead =
        Div(a)->FlexRow()->W(kFill)->ItemsCenter()->JustifyBetween();
    usageHead->Child(TextEl(a, StrL("3 days remaining in cycle"))->Font(14));
    usageHead->Child(component::Button::New(cx, StrL("billing"))
                         ->Outline()
                         ->WithSize(UiSize::XSmall)
                         ->Label(StrL("Billing"))
                         ->IntoEl());
    El* usagePanel =
        PanelRow(cx)->FlexCol()->Gap(8)->Bg(RgbaOpacity(th.muted, 0.6f));
    El* usageTop = Div(a)->FlexRow()->W(kFill)->JustifyBetween()->Medium();
    usageTop->Child(TextEl(a, StrL("$18.08 / $20")));
    usageTop->Child(TextEl(a, StrL("$200")));
    usagePanel->Child(usageTop);
    usagePanel
        ->Child(component::Progress::New(cx)->Value(90)->W(kFill)->IntoEl());

    static const char* const kUsage[4][2] = {{"Requests", "$210.84"},
                                             {"Active CPU", "$21.95"},
                                             {"Events", "$21.20"},
                                             {"Storage", "$20.45"}};
    El* usageItems = Div(a)->FlexCol()->W(kFill)->Gap(8);
    for (int i = 0; i < 4; i++) {
        El* line =
            Div(a)->FlexRow()->W(kFill)->JustifyBetween()->Font(12)->Medium();
        line->Child(TextEl(a, Str(kUsage[i][0]))->Fg(th.mutedFg));
        line->Child(TextEl(a, Str(kUsage[i][1])));
        usageItems->Child(line);
    }

    El* card = Div(a)->FlexCol()->W(kFill)->Child(
        component::GroupBox::New(cx, Str{})
            ->Outline()
            ->Title(usageHead)
            ->Child(component::Collapsible::New(cx)
                        ->W(kFill)
                        ->Open(self->open[CollUsage])
                        ->Trigger(usagePanel)
                        ->Content(usageItems)
                        ->IntoEl())
            ->IntoEl());
    // The toggle straddles the card's bottom border.
    card->Child(
        Div(a)
            ->Absolute()
            ->Bottom(-12)
            ->Left(0)
            ->W(kFill)
            ->FlexRow()
            ->JustifyCenter()
            ->Child(component::Button::New(cx, StrL("toggle-usage"))
                        ->Outline()
                        ->WithSize(UiSize::XSmall)
                        ->Icon(self->open[CollUsage] ? IconName::ChevronUp
                                                     : IconName::ChevronDown)
                        ->Tooltip(StrL("Toggle details"))
                        ->OnClick(Listen(cx, &OnColl, CollUsage))
                        ->IntoEl()
                        ->Radius(12)
                        ->Bg(th.tokens.background)));
    StorySectionAdd(bottom, card);
    page->Child(bottom);

    El* settings =
        CollSection(cx, "Settings",
                    "Holds optional controls, keeping the default view short.");
    El* setTrig = component::Button::New(cx, StrL("settings"))
                      ->Outline()
                      ->Icon(self->open[CollSettings] ? IconName::ChevronDown
                                                      : IconName::ChevronRight)
                      ->Label(StrL("Notification settings"))
                      ->JustifyStart()
                      ->OnClick(Listen(cx, &OnColl, CollSettings))
                      ->IntoEl()
                      ->W(kFill);
    El* setBody =
        Div(a)->FlexCol()->W(kFill)->Border(1, th.border)->Radius(th.radiusLg);
    struct NoteRow {
        const char* key;
        const char* label;
        Listener (*bind)(Ctx*);
    };
    static const char* const kNoteKeys[] = {"push", "email", "sms"};
    static const char* const kNoteLabels[] = {
        "Push notifications", "Email notifications", "SMS notifications"};
    for (int i = 0; i < CollNoteCount; i++) {
        El* line = Div(a)->W(kFill)->PadX(12)->PadY(8);
        if (i > 0) {
            line->BorderT(1, th.border);
        }
        Listener on = i == CollPush    ? Listen(cx, &SetPush)
                      : i == CollEmail ? Listen(cx, &SetEmail)
                                       : Listen(cx, &SetSms);
        line->Child(component::Checkbox::New(cx, Str(kNoteKeys[i]))
                        ->Label(Str(kNoteLabels[i]))
                        ->Checked(self->checked[i])
                        ->OnClick(on)
                        ->IntoEl());
        setBody->Child(line);
    }
    StorySectionAdd(settings, component::Collapsible::New(cx)
                                  ->W(kFill)
                                  ->Open(self->open[CollSettings])
                                  ->Trigger(setTrig)
                                  ->Content(setBody)
                                  ->IntoEl());
    page->Child(settings);

    // Row actions: buttons beside the trigger, in the header and every row.
    El* rowActions =
        CollSection(cx, "Row actions",
                    "Actions live beside the trigger, in the header and in "
                    "every row.");
    El* keysHead = Div(a)->FlexRow()->W(kFill)->Gap(8)->ItemsCenter();
    El* keysTrig = Div(a)->FlexRow()->Flex1()->Gap(8)->ItemsCenter()->OnClick(
        Listen(cx, &OnColl, CollApiKeys));
    keysTrig->Child(Chevron(cx, self->open[CollApiKeys]));
    keysTrig->Child(TextEl(a, StrL("API Keys"))->Font(14)->Medium());
    keysHead->Child(keysTrig);
    keysHead->Child(component::Button::New(cx, StrL("add-key"))
                        ->Ghost()
                        ->WithSize(UiSize::XSmall)
                        ->Icon(IconName::Plus)
                        ->Tooltip(StrL("Add key"))
                        ->IntoEl());
    static const char* const kKeys[3][2] = {
        {"Production", "PRDK230454*242SDIFPPL"},
        {"Development", "DUILO30454*242SDIFUIP"},
        {"Staging", "IPPODAS230454*242SDI"},
    };
    El* keysBody = Div(a)->FlexCol()->Gap(8)->W(kFill);
    for (int i = 0; i < 3; i++) {
        El* keyRow = Div(a)->FlexRow()->Gap(8)->ItemsCenter()->W(kFill);
        keyRow->Child(Div(a)
                          ->W(20)
                          ->H(20)
                          ->Shrink0()
                          ->ItemsCenter()
                          ->JustifyCenter()
                          ->Radius(th.radius)
                          ->Bg(th.tokens.muted)
                          ->Child(IconEl(a, IconName::Asterisk,
                                         UiIconPx(cx, UiSize::XSmall))
                                      ->Fg(th.green)));
        keyRow->Child(TextEl(a, Str(kKeys[i][0]))->Font(12)->W(80)->Shrink0());
        keyRow->Child(Div(a)
                          ->Flex1()
                          ->ClipX()
                          ->PadX(8)
                          ->PadY(2)
                          ->Radius(th.radius)
                          ->Bg(th.tokens.muted)
                          ->Child(TextEl(a, Str(kKeys[i][1]))->Font(12)));
        keyRow->Child(component::Button::New(cx, Str(kKeys[i][0]))
                          ->Ghost()
                          ->WithSize(UiSize::XSmall)
                          ->Icon(IconName::Ellipsis)
                          ->Tooltip(StrL("More"))
                          ->IntoEl());
        keysBody->Child(keyRow);
    }
    StorySectionAdd(rowActions, component::GroupBox::New(cx, Str{})
                                    ->Outline()
                                    ->Child(component::Collapsible::New(cx)
                                                ->W(kFill)
                                                ->Open(self->open[CollApiKeys])
                                                ->Trigger(keysHead)
                                                ->Content(keysBody)
                                                ->IntoEl())
                                    ->IntoEl());
    page->Child(rowActions);

    // Nested: panels inside panels, as a file tree.
    El* nested = CollSection(cx, "Nested",
                             "Panels nest to any depth, here as a file tree.");
    El* tree = Div(a)->FlexCol()->W(kFill);
    tree->Child(
        component::Collapsible::New(cx)
            ->W(kFill)
            ->Open(self->open[CollComponentsDir])
            ->Trigger(
                FolderRow(self, cx, CollComponentsDir, StrL("components")))
            ->Content(
                Div(a)
                    ->FlexCol()
                    ->W(kFill)
                    ->PadL(12)
                    ->Child(component::Collapsible::New(cx)
                                ->W(kFill)
                                ->Open(self->open[CollUiDir])
                                ->Trigger(
                                    FolderRow(self, cx, CollUiDir, StrL("ui")))
                                ->Content(
                                    Div(a)
                                        ->FlexCol()
                                        ->W(kFill)
                                        ->PadL(12)
                                        ->Child(FileRow(cx, StrL("button.rs")))
                                        ->Child(FileRow(cx, StrL("card.rs")))
                                        ->Child(FileRow(cx, StrL("dialog.rs"))))
                                ->IntoEl())
                    ->Child(FileRow(cx, StrL("login_form.rs"))))
            ->IntoEl());
    tree->Child(FileRow(cx, StrL("main.rs")));
    StorySectionAdd(
        nested,
        component::GroupBox::New(cx, Str{})->Outline()->Child(tree)->IntoEl());
    page->Child(nested);

    El* profile =
        CollSection(cx, "Profile",
                    "Shows who someone is, and their details only on request.");
    El* profTrig = Div(a)
                       ->FlexRow()
                       ->W(kFill)
                       ->ItemsCenter()
                       ->JustifyBetween()
                       ->Gap(8)
                       ->OnClick(Listen(cx, &OnColl, CollProfile));
    El* who = Div(a)->FlexRow()->Gap(8)->ItemsCenter();
    who->Child(component::Avatar::New(cx)
                   ->Name(StrL("Jason Lee"))
                   ->WithSize(UiSize::XSmall)
                   ->IntoEl());
    who->Child(TextEl(a, StrL("@huacnlee"))->Font(14)->Medium());
    profTrig->Child(who)->Child(Chevron(cx, self->open[CollProfile]));
    El* profBody = Div(a)->FlexCol()->Gap(8)->W(kFill);
    struct Field {
        IconName icon;
        const char* label;
        const char* value;
    };
    static const Field kFields[] = {
        {IconName::Inbox, "Last activity", "2 hours ago"},
        {IconName::Calendar, "Online since", "Today, 9:00 AM"},
        {IconName::Globe, "Location", "Hong Kong"}};
    for (const Field& fld : kFields) {
        El* f = Div(a)->FlexRow()->Gap(8)->ItemsCenter()->Font(12);
        f->Child(IconEl(a, fld.icon, UiIconPx(cx, UiSize::XSmall))
                     ->Fg(th.mutedFg));
        f->Child(TextEl(a, Str(fld.label))->Fg(th.mutedFg));
        f->Child(TextEl(a, Str(fld.value))->Medium());
        profBody->Child(f);
    }
    StorySectionAdd(profile, component::GroupBox::New(cx, Str{})
                                 ->Outline()
                                 ->Child(component::Collapsible::New(cx)
                                             ->W(kFill)
                                             ->Open(self->open[CollProfile])
                                             ->Trigger(profTrig)
                                             ->Content(profBody)
                                             ->IntoEl())
                                 ->IntoEl());
    page->Child(profile);
    return page;
}

STORY_PAGE(StoryCollapsible, CollapsibleStory);
