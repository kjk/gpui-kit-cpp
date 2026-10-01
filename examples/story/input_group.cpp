// crates/story/src/stories/input_group_story.rs and
// input_group_story/examples.rs.

#include "Story.h"

using IgAlign = component::InputGroupAddonAlignment;

enum {
    // input_group_story.rs InputGroupStory's own fields.
    IgSearch = 0,
    IgUrl,
    IgAmount,
    IgPassword,
    IgEmail,
    IgDisabled,
    IgDisabledInvalid,
    IgReadonly,
    IgLoading,
    IgShortcut,
    IgNotes,
    IgMessage,
    IgSizeXs,
    IgSizeSm,
    IgSizeMd,
    IgSizeLg,
    // examples::create_inputs.
    IgAlignStart,
    IgAlignEnd,
    IgAlignTop,
    IgAlignBottom,
    IgIconEmail,
    IgIconVerified,
    IgIconMultiple,
    IgDomain,
    IgUsername,
    IgTooltipPassword,
    IgTooltipEmail,
    IgDropdownFile,
    IgDropdownSearch,
    IgPhone,
    IgPopoverUrl,
    IgLabelUsername,
    IgLabelEmail,
    IgButtonActions,
    IgLoadingEnd,
    IgLoadingStart,
    IgLoadingText,
    IgProfileName,
    IgProfileEmail,
    // examples::create_textareas.
    IgTextareaPlain,
    IgTextareaHeader,
    IgTextareaFooter,
    IgTextareaDisabled,
    IgTextareaInvalid,
    IgComment,
    IgCustom,
    IgCount
};

struct IgSeedRow {
    int field;
    const char* placeholder;
    const char* value;
};

static const IgSeedRow kIgInputs[] = {
    {IgSearch, "Search components…", ""},
    {IgUrl, "", "gpui-kit.com"},
    {IgAmount, "0.00", ""},
    {IgPassword, "Enter password", ""},
    {IgEmail, "you@example.com", ""},
    {IgDisabled, "", "Unavailable"},
    {IgDisabledInvalid, "", "Invalid saved value"},
    {IgReadonly, "", "https://gpui-kit.com"},
    {IgLoading, "Searching…", ""},
    {IgShortcut, "Search…", ""},
    {IgSizeXs, "xs input", ""},
    {IgSizeSm, "sm input", ""},
    {IgSizeMd, "md input", ""},
    {IgSizeLg, "lg input", ""},
    {IgAlignStart, "Search…", ""},
    {IgAlignEnd, "Enter password", ""},
    {IgAlignTop, "Enter your full name", ""},
    {IgAlignBottom, "0.00", ""},
    {IgIconEmail, "Enter your email", ""},
    {IgIconVerified, "Username", "ada"},
    {IgIconMultiple, "Website", "gpui-kit.com"},
    {IgDomain, "example", ""},
    {IgUsername, "Enter your username", ""},
    {IgTooltipPassword, "Enter password", ""},
    {IgTooltipEmail, "Your email address", ""},
    {IgDropdownFile, "Enter file name", "notes.txt"},
    {IgDropdownSearch, "Enter search query", ""},
    {IgPhone, "Phone number", ""},
    {IgPopoverUrl, "example.com", "gpui-kit.com"},
    {IgLabelUsername, "username", ""},
    {IgLabelEmail, "you@example.com", ""},
    {IgButtonActions, "Enter a project name", "Input Group"},
    {IgLoadingEnd, "Searching…", ""},
    {IgLoadingStart, "Processing…", ""},
    {IgLoadingText, "Saving changes…", ""},
    {IgProfileName, "Your name", "Ada Lovelace"},
    {IgProfileEmail, "you@example.com", "ada@example.com"},
};

static const IgSeedRow kIgTextareas[] = {
    {IgTextareaPlain, "Enter your text here…", ""},
    {IgTextareaHeader, "Write your question…", ""},
    {IgTextareaFooter, "Enter your message", ""},
    {IgTextareaDisabled, "", "This textarea is disabled."},
    {IgTextareaInvalid, "Write a short summary…", ""},
    {IgComment, "Share your thoughts…", ""},
    {IgCustom, "An automatically growing textarea…", ""},
};

static const char* kIgSearchScopes[] = {"Documentation", "Blog posts",
                                        "Changelog"};
static const char* kIgCountryCodes[] = {"+1", "+44", "+46"};
static const char* kIgFileActions[] = {"Copy filename", "Reset filename",
                                       "Clear filename"};

struct InputGroupStory {
    InputState fields[IgCount];
    bool seeded = false;
    bool copied = false;
    bool starred = false;
    int runs = 0;
    bool attached = false;
    // Heap copies; null until set, as Rust's Option<SharedString> is.
    Str lastMessage = {};
    Str lastSearch = {};
    Str postedComment = {};
    Str submittedCustom = {};
    Str savedProfile = {};
    int searchScope = 0;
    int countryCode = 0;

    static El* Render(InputGroupStory* self, Ctx* cx);
};

static void IgSetSaved(Str* dst, Str value) {
    StrFree(*dst);
    *dst = StrDup(value);
}

// value.chars().count().
static int IgCharCount(Str s) {
    int n = 0;
    for (int i = 0; i < len(s); i++) {
        if (((uint8_t)s.s[i] & 0xC0) != 0x80) {
            n++;
        }
    }
    return n;
}

static bool IgBlank(Str s) {
    return len(StrTrimAscii(s)) == 0;
}

// Styled refinements Rust chains onto a component: ml_auto, max_w, a text
// colour, a border on one side, and the auto-growing textarea's typography.
static void IgRefMlAuto(El* e, void*) {
    e->MlAuto();
}
static void IgRefMaxW24(El* e, void*) {
    e->MaxW(384);
}
static void IgRefFg(El* e, void* user) {
    e->Fg(*(const Rgba*)user);
}
static void IgRefBorderB(El* e, void* user) {
    e->BorderB(1, *(const Rgba*)user);
}
static void IgRefBorderT(El* e, void* user) {
    e->BorderT(1, *(const Rgba*)user);
}
static void IgRefTextBaseMono(El* e, void*) {
    e->Font(16)->Mono();
}

static ElRefiner IgRefiner(void (*fn)(El*, void*), const void* user = nullptr) {
    ElRefiner r;
    r.apply = fn;
    r.user = (void*)user;
    return r;
}

static component::InputGroupButton* IgMlAuto(
    component::InputGroupButton* button) {
    button->refiner = IgRefiner(&IgRefMlAuto);
    return button;
}

static El* IgIcon(Ctx* cx, IconName name) {
    return IconEl(cx->a, name, 16);
}

static El* IgText(Ctx* cx, Str s) {
    return component::InputGroupText::New(cx)
        ->Child(TextEl(cx->a, s))
        ->IntoEl();
}

// column(): v_flex().w_full().max_w(rems(24.)).gap_4().
static El* IgColumn(Ctx* cx) {
    return Div(cx->a)->FlexCol()->W(kFill)->MaxW(384)->Gap(16);
}

static El* IgNote(Ctx* cx, Str s, Rgba c) {
    return StoryTxt(cx, s, 14, c);
}

// labeled(): Field::new().label(..).description(..).child(control). A Field
// renders only inside a Form here, so it is given a one-field vertical Form,
// which lays it out with the default FieldProps a bare Rust Field uses.
static El* IgLabeled(Ctx* cx, const char* label, const char* description,
                     El* control) {
    component::Field field = component::Field::New(control);
    field.Label(Str(label)).Description(Str(description));
    return component::Form::New(cx)->Child(field)->IntoEl();
}

static component::Input* IgInput(Ctx* cx, InputGroupStory* self, int field,
                                 const char* id, const char* label) {
    component::Input* input =
        component::Input::New(cx, StoryFmt(cx, "%s-input", id),
                              &self->fields[field])
            ->AriaLabel(Str(label));
    if (field == IgAlignEnd || field == IgTooltipPassword) {
        input->ContentType(component::InputContentType::Password);
    }
    return input;
}

// extra_input / extra_textarea.
static component::InputGroup* IgExtraInput(Ctx* cx, InputGroupStory* self,
                                           int field, const char* id,
                                           const char* label) {
    return component::InputGroup::New(cx, Str(id))
        ->Input(IgInput(cx, self, field, id, label));
}

static component::InputGroup* IgExtraTextarea(Ctx* cx, InputGroupStory* self,
                                              int field, const char* id,
                                              const char* label) {
    return component::InputGroup::New(cx, Str(id))
        ->Input(component::Textarea::New(cx, StoryFmt(cx, "%s-textarea", id),
                                         &self->fields[field])
                    ->AriaLabel(Str(label)));
}

static component::InputGroupAddon* IgAddon(
    Ctx* cx, const char* id, IgAlign align = IgAlign::InlineStart) {
    return component::InputGroupAddon::New(cx, Str(id))->Align(align);
}

static void Seed(InputGroupStory* self, Ctx* cx);

// ─── handlers ───────────────────────────────────────────────────────────────

static void OnStar(InputGroupStory* self, Ctx* cx, const ClickEvent*) {
    self->starred = !self->starred;
    Notify(cx);
}

static void OnCopyUrl(InputGroupStory* self, Ctx* cx, const ClickEvent*) {
    ClipboardSetText(cx->win, InputValue(&self->fields[IgReadonly]));
    self->copied = true;
    Notify(cx);
}

static void OnSelectUrl(InputGroupStory* self, Ctx* cx, const ClickEvent*) {
    InputState* s = &self->fields[IgReadonly];
    InputFocus(s, cx->app, cx->win);
    InputSelectAll(s, cx);
    Notify(cx);
}

static void OnTogglePassword(InputGroupStory* self, Ctx* cx,
                             const ClickEvent*) {
    InputState* s = &self->fields[IgPassword];
    s->masked = !s->masked;
    Notify(cx);
}

static void OnShortcutEvent(InputGroupStory* self, Ctx* cx,
                            const InputEvent* ev) {
    if (ev && ev->kind == InputEventKind::PressEnter) {
        IgSetSaved(&self->lastSearch, InputValue(&self->fields[IgShortcut]));
        Notify(cx);
    }
}

static void OnCopyNotes(InputGroupStory* self, Ctx* cx, const ClickEvent*) {
    ClipboardSetText(cx->win, InputValue(&self->fields[IgNotes]));
}

static void OnRunNotes(InputGroupStory* self, Ctx* cx, const ClickEvent*) {
    self->runs++;
    Notify(cx);
}

static void OnAttach(InputGroupStory* self, Ctx* cx, const ClickEvent*) {
    self->attached = !self->attached;
    Notify(cx);
}

static void OnSend(InputGroupStory* self, Ctx* cx, const ClickEvent*) {
    InputState* s = &self->fields[IgMessage];
    Str message = InputValue(s);
    if (IgBlank(message) || IgCharCount(message) > 280) {
        return;
    }
    IgSetSaved(&self->lastMessage, message);
    InputSetValue(s, StrL(""));
    InputFocus(s, cx->app, cx->win);
    self->attached = false;
    Notify(cx);
}

static void OnFileMenu(InputGroupStory* self, Ctx* cx, const ClickEvent*,
                       intptr_t action) {
    InputState* s = &self->fields[IgDropdownFile];
    if (action == 0) {
        ClipboardSetText(cx->win, InputValue(s));
    } else {
        InputSetValue(s, action == 1 ? StrL("notes.txt") : StrL(""));
    }
    Notify(cx);
}

static void OnScope(InputGroupStory* self, Ctx* cx, const ClickEvent*,
                    intptr_t ix) {
    self->searchScope = (int)ix;
    Notify(cx);
}

static void OnCountry(InputGroupStory* self, Ctx* cx, const ClickEvent*,
                      intptr_t ix) {
    self->countryCode = (int)ix;
    Notify(cx);
}

static void OnProjectClear(InputGroupStory* self, Ctx* cx, const ClickEvent*) {
    InputSetValue(&self->fields[IgButtonActions], StrL(""));
    Notify(cx);
}

static void OnProjectReset(InputGroupStory* self, Ctx* cx, const ClickEvent*) {
    InputSetValue(&self->fields[IgButtonActions], StrL("Input Group"));
    Notify(cx);
}

static void OnProjectCopy(InputGroupStory* self, Ctx* cx, const ClickEvent*) {
    ClipboardSetText(cx->win, InputValue(&self->fields[IgButtonActions]));
}

static void ClearComment(InputGroupStory* self, Ctx* cx) {
    InputState* s = &self->fields[IgComment];
    InputSetValue(s, StrL(""));
    InputFocus(s, cx->app, cx->win);
    Notify(cx);
}

static void OnCommentCancel(InputGroupStory* self, Ctx* cx, const ClickEvent*) {
    ClearComment(self, cx);
}

static void OnCommentPost(InputGroupStory* self, Ctx* cx, const ClickEvent*) {
    IgSetSaved(&self->postedComment, InputValue(&self->fields[IgComment]));
    ClearComment(self, cx);
}

static void OnCustomSubmit(InputGroupStory* self, Ctx* cx, const ClickEvent*) {
    InputState* s = &self->fields[IgCustom];
    IgSetSaved(&self->submittedCustom, InputValue(s));
    InputSetValue(s, StrL(""));
    Notify(cx);
}

static void OnSaveProfile(InputGroupStory* self, Ctx* cx, const ClickEvent*) {
    Str v = StoryFmt(cx, "%s — %s", InputValue(&self->fields[IgProfileName]),
                     InputValue(&self->fields[IgProfileEmail]));
    IgSetSaved(&self->savedProfile, v);
    Notify(cx);
}

static void Seed(InputGroupStory* self, Ctx* cx) {
    if (self->seeded) {
        return;
    }
    self->seeded = true;
    for (const IgSeedRow& row : kIgInputs) {
        InputState* s = &self->fields[row.field];
        InputSetPlaceholder(s, Str(row.placeholder));
        InputSetValue(s, Str(row.value));
        s->masked = row.field == IgPassword || row.field == IgAlignEnd ||
                    row.field == IgTooltipPassword;
    }
    for (const IgSeedRow& row : kIgTextareas) {
        InputState* s = &self->fields[row.field];
        s->kind = InputKind::Textarea;
        InputSetPlaceholder(s, Str(row.placeholder));
        InputSetValue(s, Str(row.value));
        TextareaSetRows(s, 3);
        if (row.field == IgCustom) {
            TextareaSetAutoGrow(s, 1, 8);
        }
    }
    InputState* notes = &self->fields[IgNotes];
    notes->kind = InputKind::Textarea;
    InputSetValue(notes, StrL("console.log('Hello, GPUI Kit!');"));
    TextareaSetRows(notes, 4);
    InputState* message = &self->fields[IgMessage];
    message->kind = InputKind::Textarea;
    InputSetPlaceholder(message, StrL("Write a message…"));
    TextareaSetAutoGrow(message, 2, 6);
    self->fields[IgShortcut].onChange = Listen(cx, &OnShortcutEvent);
}

// ─── sections ───────────────────────────────────────────────────────────────

static El* RenderDefault(InputGroupStory* self, Ctx* cx) {
    static const char* kNames[] = {
        "Button", "Input",    "Textarea", "Input Group",
        "Select", "Combobox", "Checkbox", "Radio",
        "Slider", "Switch",   "Calendar", "Color Picker",
    };
    Str query = InputValue(&self->fields[IgSearch]);
    int count = 0;
    for (const char* name : kNames) {
        // str::contains("") is true: an empty query matches every name.
        if (len(query) == 0 || StrContainsI(Str(name), query)) {
            count++;
        }
    }
    El* sec = StorySection(cx, "Default", nullptr);
    component::InputGroup* group =
        component::InputGroup::New(cx, StrL("input-group-search"))
            ->Input(component::Input::New(cx, StrL("input-group-search-input"),
                                          &self->fields[IgSearch])
                        ->AriaLabel(StrL("Search components")))
            ->Addon(IgAddon(cx, "search-icon")
                        ->Child(IgIcon(cx, IconName::Search)))
            ->Addon(IgAddon(cx, "search-results", IgAlign::InlineEnd)
                        ->Child(IgText(cx, StoryFmt(cx, "%d results", count))));
    group->refiner = IgRefiner(&IgRefMaxW24);
    StorySectionAdd(sec, group->IntoEl());
    return sec;
}

static El* RenderAlignment(InputGroupStory* self, Ctx* cx) {
    El* sec = StorySection(
        cx, "Alignment",
        "Place addons before, after, above, or below the control.");
    El* col = IgColumn(cx);
    col->Child(IgLabeled(cx, "Inline start", "A leading search icon.",
                         IgExtraInput(cx, self, IgAlignStart, "align-start",
                                      "Leading icon search")
                             ->Addon(IgAddon(cx, "align-start-addon")
                                         ->Child(IgIcon(cx, IconName::Search)))
                             ->IntoEl()));
    col->Child(
        IgLabeled(cx, "Inline end", "A trailing icon with a masked input.",
                  IgExtraInput(cx, self, IgAlignEnd, "align-end",
                               "Trailing icon password")
                      ->Addon(IgAddon(cx, "align-end-addon", IgAlign::InlineEnd)
                                  ->Child(IgIcon(cx, IconName::EyeOff)))
                      ->IntoEl()));
    col->Child(IgLabeled(
        cx, "Block start", "The header is inside the shared frame.",
        IgExtraInput(cx, self, IgAlignTop, "align-top", "Full name")
            ->Addon(IgAddon(cx, "align-top-addon", IgAlign::BlockStart)
                        ->Child(IgText(cx, StrL("Full Name"))))
            ->IntoEl()));
    col->Child(IgLabeled(
        cx, "Block end", "The unit sits below the single-line input.",
        IgExtraInput(cx, self, IgAlignBottom, "align-bottom",
                     "Amount with footer")
            ->Addon(IgAddon(cx, "align-bottom-addon", IgAlign::BlockEnd)
                        ->Child(IgText(cx, StrL("USD"))))
            ->IntoEl()));
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderIcons(InputGroupStory* self, Ctx* cx) {
    El* sec = StorySection(cx, "Icons",
                           "Leading, paired, and multiple trailing icons.");
    El* col = IgColumn(cx);
    col->Child(
        IgExtraInput(cx, self, IgIconEmail, "icon-email", "Email with icon")
            ->Addon(IgAddon(cx, "icon-email-addon")
                        ->Child(IgIcon(cx, IconName::Inbox)))
            ->IntoEl());
    col->Child(IgExtraInput(cx, self, IgIconVerified, "icon-verified",
                            "Verified username")
                   ->Addon(IgAddon(cx, "icon-user-addon")
                               ->Child(IgIcon(cx, IconName::User)))
                   ->Addon(IgAddon(cx, "icon-check-addon", IgAlign::InlineEnd)
                               ->Child(IgIcon(cx, IconName::Check)))
                   ->IntoEl());
    col->Child(
        IgExtraInput(cx, self, IgIconMultiple, "icon-multiple",
                     "Website with multiple icons")
            ->Addon(IgAddon(cx, "icon-multiple-addon", IgAlign::InlineEnd)
                        ->Child(IgIcon(cx, IconName::Star))
                        ->Child(IgIcon(cx, IconName::Info)))
            ->IntoEl());
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderText(InputGroupStory* self, Ctx* cx) {
    const Theme& th = ThemeNow(cx->app);
    El* sec = StorySection(cx, "Text",
                           "Leading and trailing text share the input frame.");
    El* col = IgColumn(cx);
    component::InputGroupButton* star =
        component::InputGroupButton::New(cx, StrL("favorite-url"))
            ->Icon(IconName::Star)
            ->AriaLabel(StrL("Favorite website"))
            ->Tooltip(StrL("Favorite website"))
            ->OnClick(Listen(cx, &OnStar));
    if (self->starred) {
        star->refiner = IgRefiner(&IgRefFg, &th.primary);
    }
    col->Child(
        component::InputGroup::New(cx, StrL("input-group-url"))
            ->Input(component::Input::New(cx, StrL("input-group-url-input"),
                                          &self->fields[IgUrl])
                        ->AriaLabel(StrL("Website"))
                        ->ContentType(component::InputContentType::Url))
            ->Addon(IgAddon(cx, "url-scheme")
                        ->Child(IgText(cx, StrL("https://"))))
            ->Addon(IgAddon(cx, "url-action", IgAlign::InlineEnd)->Child(star))
            ->IntoEl());
    col->Child(
        component::InputGroup::New(cx, StrL("input-group-amount"))
            ->Input(component::Input::New(cx, StrL("input-group-amount-input"),
                                          &self->fields[IgAmount])
                        ->AriaLabel(StrL("Amount")))
            ->Addon(IgAddon(cx, "currency-symbol")
                        ->Child(IgText(cx, StrL("$"))))
            ->Addon(IgAddon(cx, "currency-code", IgAlign::InlineEnd)
                        ->Child(IgText(cx, StrL("USD"))))
            ->IntoEl());
    col->Child(IgExtraInput(cx, self, IgDomain, "domain", "Domain")
                   ->Addon(IgAddon(cx, "domain-protocol")
                               ->Child(IgText(cx, StrL("https://"))))
                   ->Addon(IgAddon(cx, "domain-suffix", IgAlign::InlineEnd)
                               ->Child(IgText(cx, StrL(".com"))))
                   ->IntoEl());
    col->Child(IgExtraInput(cx, self, IgUsername, "username", "Work username")
                   ->Addon(IgAddon(cx, "username-domain", IgAlign::InlineEnd)
                               ->Child(IgText(cx, StrL("@company.com"))))
                   ->IntoEl());
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderButtons(InputGroupStory* self, Ctx* cx) {
    El* sec =
        StorySection(cx, "Buttons",
                     "Multiple native actions retain their own behavior and "
                     "focus.");
    El* col = IgColumn(cx);
    col->Child(
        component::InputGroup::New(cx, StrL("input-group-copy"))
            ->Readonly(true)
            ->Input(component::Input::New(cx, StrL("input-group-copy-input"),
                                          &self->fields[IgReadonly])
                        ->AriaLabel(StrL("Documentation URL")))
            ->Addon(IgAddon(cx, "copy-actions", IgAlign::InlineEnd)
                        ->Child(component::InputGroupButton::New(
                                    cx, StrL("copy-url"))
                                    ->Icon(self->copied ? IconName::Check
                                                        : IconName::Copy)
                                    ->AriaLabel(StrL("Copy URL"))
                                    ->Tooltip(StrL("Copy URL"))
                                    ->OnClick(Listen(cx, &OnCopyUrl)))
                        ->Child(component::InputGroupButton::New(
                                    cx, StrL("select-url"))
                                    ->Label(StrL("Select all"))
                                    ->OnClick(Listen(cx, &OnSelectUrl))))
            ->IntoEl());
    bool masked = self->fields[IgPassword].masked;
    col->Child(
        component::InputGroup::New(cx, StrL("input-group-password"))
            ->Input(component::Input::New(cx,
                                          StrL("input-group-password-input"),
                                          &self->fields[IgPassword])
                        ->AriaLabel(StrL("Password"))
                        ->ContentType(component::InputContentType::Password))
            ->Addon(
                IgAddon(cx, "password-action", IgAlign::InlineEnd)
                    ->Child(
                        component::InputGroupButton::New(
                            cx, StrL("toggle-password"))
                            ->Icon(masked ? IconName::Eye : IconName::EyeOff)
                            ->AriaLabel(StrL("Toggle password visibility"))
                            ->Tooltip(StrL("Toggle password visibility"))
                            ->OnClick(Listen(cx, &OnTogglePassword))))
            ->IntoEl());
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderTooltips(InputGroupStory* self, Ctx* cx) {
    struct Row {
        int field;
        const char* id;
        const char* label;
        const char* help;
    };
    static const Row kRows[] = {
        {IgTooltipPassword, "tooltip-password", "Password help",
         "Use at least 8 characters."},
        {IgTooltipEmail, "tooltip-email", "Email help",
         "Used for notifications about this workspace."},
    };
    El* sec = StorySection(
        cx, "Tooltips",
        "Compact help triggers keep the explanation beside its field.");
    El* col = IgColumn(cx);
    for (const Row& row : kRows) {
        col->Child(
            IgExtraInput(cx, self, row.field, row.id, row.label)
                ->Addon(component::InputGroupAddon::New(
                            cx, StoryFmt(cx, "tooltip-addon-%s", row.id))
                            ->Align(IgAlign::InlineEnd)
                            ->Child(component::InputGroupButton::New(
                                        cx, StoryFmt(cx, "tooltip-button-%s",
                                                     row.id))
                                        ->Icon(IconName::Info)
                                        ->AriaLabel(Str(row.label))
                                        ->Tooltip(Str(row.help))))
                ->IntoEl());
    }
    StorySectionAdd(sec, col);
    return sec;
}

// compact_trigger(id, label), with Button::dropdown_menu over it.
static El* IgDropdown(Ctx* cx, const char* id, Str label, bool caret,
                      component::PopupMenu* menu) {
    component::InputGroupButton* trigger =
        component::InputGroupButton::New(cx, Str(id))->Label(label);
    if (caret) {
        trigger->button->DropdownCaret(true);
    }
    return component::DropdownMenu::New(cx, StoryFmt(cx, "%s-dropdown", id))
        ->Trigger(trigger->IntoEl())
        ->Menu(menu)
        ->IntoEl();
}

static El* RenderDropdowns(InputGroupStory* self, Ctx* cx) {
    El* sec = StorySection(cx, "Dropdown menus",
                           "Menus act on the filename or choose the search "
                           "scope and country code.");
    El* col = IgColumn(cx);

    component::PopupMenu* fileMenu =
        component::PopupMenu::New(cx, StrL("file-menu"));
    for (int i = 0; i < 3; i++) {
        fileMenu->Menu(Str(kIgFileActions[i]))
            ->OnClick(Listen(cx, &OnFileMenu, (intptr_t)i));
    }
    col->Child(
        IgExtraInput(cx, self, IgDropdownFile, "dropdown-file", "File name")
            ->Addon(IgAddon(cx, "file-menu-addon", IgAlign::InlineEnd)
                        ->Child(IgDropdown(cx, "file-menu", StrL("More"), false,
                                           fileMenu)))
            ->IntoEl());

    component::PopupMenu* scopeMenu =
        component::PopupMenu::New(cx, StrL("scope-menu"));
    for (int i = 0; i < 3; i++) {
        scopeMenu->Menu(Str(kIgSearchScopes[i]))
            ->Checked(i == self->searchScope)
            ->OnClick(Listen(cx, &OnScope, (intptr_t)i));
    }
    col->Child(IgExtraInput(cx, self, IgDropdownSearch, "dropdown-search",
                            "Scoped search")
                   ->Addon(IgAddon(cx, "scope-menu-addon", IgAlign::InlineEnd)
                               ->Child(IgDropdown(
                                   cx, "scope-menu",
                                   Str(kIgSearchScopes[self->searchScope]),
                                   true, scopeMenu)))
                   ->IntoEl());

    component::PopupMenu* countryMenu =
        component::PopupMenu::New(cx, StrL("country-menu"));
    for (int i = 0; i < 3; i++) {
        countryMenu->Menu(Str(kIgCountryCodes[i]))
            ->Checked(i == self->countryCode)
            ->OnClick(Listen(cx, &OnCountry, (intptr_t)i));
    }
    col->Child(IgExtraInput(cx, self, IgPhone, "phone", "Phone number")
                   ->Addon(IgAddon(cx, "country-menu-addon")
                               ->Child(IgDropdown(
                                   cx, "country-menu",
                                   Str(kIgCountryCodes[self->countryCode]),
                                   true, countryMenu)))
                   ->IntoEl());
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderPopover(InputGroupStory* self, Ctx* cx) {
    Arena* a = cx->a;
    Str address = InputValue(&self->fields[IgPopoverUrl]);
    El* sec = StorySection(
        cx, "Popover",
        "A native popover keeps contextual details attached to its trigger.");
    // Popover's surface: popover_style().p_3(), then .w(rems(18.)).gap_2()
    // .text_sm() from the story.
    El* content = component::PopoverSurface(
        cx, Div(a)->FlexCol()->Pad(12)->W(288)->Gap(8)->Font(14));
    content->Child(TextEl(a, StrL("Address details"))->Semibold());
    content->Child(TextEl(a, StoryFmt(cx, "https://%s", address))->Wrap());
    content->Child(
        TextEl(a, StrL("The protocol prefix stays separate from the editable "
                       "hostname."))
            ->Wrap());
    El* popover = component::Popover::New(cx, StrL("address-details"))
                      ->Trigger(component::InputGroupButton::New(
                                    cx, StrL("address-details-trigger"))
                                    ->Icon(IconName::Info)
                                    ->AriaLabel(StrL("Address details"))
                                    ->Tooltip(StrL("Address details"))
                                    ->IntoEl())
                      ->Content(content)
                      ->IntoEl();
    El* col = IgColumn(cx);
    col->Child(IgExtraInput(cx, self, IgPopoverUrl, "popover-url",
                            "Website with details")
                   ->Addon(IgAddon(cx, "address-details-addon")
                               ->Child(popover)
                               ->Child(IgText(cx, StrL("https://"))))
                   ->IntoEl());
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderLabels(InputGroupStory* self, Ctx* cx) {
    const Theme& th = ThemeNow(cx->app);
    El* sec = StorySection(cx, "Labels and descriptions", nullptr);
    El* col = IgColumn(cx);
    col->Child(IgLabeled(
        cx, "Username", "Clicking the @ addon focuses the input.",
        IgExtraInput(cx, self, IgLabelUsername, "label-username",
                     "Username with label")
            ->Addon(IgAddon(cx, "label-username-addon")
                        ->Child(component::Label::New(cx, StrL("@"))->IntoEl()))
            ->IntoEl()));
    col->Child(
        IgExtraInput(cx, self, IgLabelEmail, "label-email",
                     "Notification email")
            ->Addon(IgAddon(cx, "label-email-addon", IgAlign::BlockStart)
                        ->Child(component::Label::New(cx, StrL("Email"))
                                    ->IntoEl()
                                    ->Fg(th.foreground))
                        ->Child(IgMlAuto(
                            component::InputGroupButton::New(
                                cx, StrL("label-email-help"))
                                ->Icon(IconName::Info)
                                ->AriaLabel(StrL("Notification email help"))
                                ->Tooltip(StrL("We'll use this address for "
                                               "workspace notifications.")))))
            ->IntoEl());
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderButtonActions(InputGroupStory* self, Ctx* cx) {
    El* sec = StorySection(cx, "Text and icon actions",
                           "Use larger buttons for a clear text action; keep "
                           "secondary actions compact.");
    El* col = IgColumn(cx);
    col->Child(
        IgExtraInput(cx, self, IgButtonActions, "button-actions",
                     "Project name")
            ->Addon(IgAddon(cx, "project-actions", IgAlign::BlockEnd)
                        ->Child(component::InputGroupButton::New(
                                    cx, StrL("project-clear"))
                                    ->WithSize(UiSize::Small)
                                    ->Label(StrL("Clear"))
                                    ->OnClick(Listen(cx, &OnProjectClear)))
                        ->Child(component::InputGroupButton::New(
                                    cx, StrL("project-reset"))
                                    ->WithSize(UiSize::Small)
                                    ->WithVariant(
                                        component::ButtonVariant::Secondary)
                                    ->Label(StrL("Reset"))
                                    ->OnClick(Listen(cx, &OnProjectReset)))
                        ->Child(IgMlAuto(
                            component::InputGroupButton::New(
                                cx, StrL("project-copy"))
                                ->WithSize(UiSize::Small)
                                ->Icon(IconName::Copy)
                                ->AriaLabel(StrL("Copy project name"))
                                ->Tooltip(StrL("Copy project name"))
                                ->OnClick(Listen(cx, &OnProjectCopy)))))
            ->IntoEl());
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderShortcut(InputGroupStory* self, Ctx* cx) {
    const Theme& th = ThemeNow(cx->app);
    El* sec = StorySection(cx, "Keyboard shortcut and loading", nullptr);
    El* col = IgColumn(cx);
    col->Child(component::InputGroup::New(cx, StrL("input-group-shortcut"))
                   ->Input(component::Input::New(
                               cx, StrL("input-group-shortcut-input"),
                               &self->fields[IgShortcut])
                               ->AriaLabel(StrL("Search")))
                   ->Addon(IgAddon(cx, "shortcut-icon")
                               ->Child(IgIcon(cx, IconName::Search)))
                   ->Addon(IgAddon(cx, "shortcut-key", IgAlign::InlineEnd)
                               ->Child(component::Kbd::New(cx, StrL("enter"))
                                           ->IntoEl()))
                   ->IntoEl());
    if (self->lastSearch.s) {
        col->Child(
            IgNote(cx, StoryFmt(cx, "Submitted search: %s", self->lastSearch),
                   th.mutedFg));
    }
    col->Child(
        component::InputGroup::New(cx, StrL("input-group-loading"))
            ->Readonly(true)
            ->Input(component::Input::New(cx, StrL("input-group-loading-input"),
                                          &self->fields[IgLoading])
                        ->AriaLabel(StrL("Search in progress")))
            ->Addon(IgAddon(cx, "loading-spinner")
                        ->Child(component::Spinner::New(cx)
                                    ->Id(StrL("loading-spinner"))
                                    ->WithSize(UiSize::Small)
                                    ->IntoEl()))
            ->Addon(IgAddon(cx, "loading-text", IgAlign::InlineEnd)
                        ->Child(IgText(cx, StrL("Please wait…"))))
            ->IntoEl());
    StorySectionAdd(sec, col);
    return sec;
}

static El* IgSpinner(Ctx* cx, const char* id) {
    return component::Spinner::New(cx)
        ->Id(Str(id))
        ->WithSize(UiSize::Small)
        ->IntoEl();
}

static El* RenderLoading(InputGroupStory* self, Ctx* cx) {
    El* sec = StorySection(cx, "Spinner placement",
                           "Progress can lead, trail, or sit next to status "
                           "text.");
    El* col = IgColumn(cx);
    col->Child(IgExtraInput(cx, self, IgLoadingEnd, "loading-end",
                            "Search loading state")
                   ->Readonly(true)
                   ->Addon(IgAddon(cx, "spinner-end", IgAlign::InlineEnd)
                               ->Child(IgSpinner(cx, "spinner-end")))
                   ->IntoEl());
    col->Child(IgExtraInput(cx, self, IgLoadingStart, "loading-start",
                            "Processing loading state")
                   ->Readonly(true)
                   ->Addon(IgAddon(cx, "spinner-start")
                               ->Child(IgSpinner(cx, "spinner-start")))
                   ->IntoEl());
    col->Child(IgExtraInput(cx, self, IgLoadingText, "loading-text",
                            "Saving loading state")
                   ->Readonly(true)
                   ->Addon(IgAddon(cx, "spinner-text", IgAlign::InlineEnd)
                               ->Child(IgText(cx, StrL("Saving…")))
                               ->Child(IgSpinner(cx, "spinner-text")))
                   ->IntoEl());
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderValidation(InputGroupStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    Str email = InputValue(&self->fields[IgEmail]);
    bool invalid = len(email) > 0 && (!StrContains(email, StrL("@")) ||
                                      !StrContains(email, StrL(".")));
    El* sec = StorySection(cx, "Validation and disabled", nullptr);
    El* col = IgColumn(cx);
    El* emailCol = Div(a)->FlexCol()->Gap(8);
    emailCol->Child(
        component::InputGroup::New(cx, StrL("input-group-email"))
            ->Invalid(invalid)
            ->Input(
                component::Input::New(cx, StrL("input-group-email-input"),
                                      &self->fields[IgEmail])
                    ->AriaLabel(StrL("Email address"))
                    ->ContentType(component::InputContentType::EmailAddress))
            ->Addon(IgAddon(cx, "email-icon")
                        ->Child(IgIcon(cx, IconName::Info)))
            ->IntoEl());
    emailCol->Child(IgNote(
        cx,
        invalid ? StrL("Enter an email address such as you@example.com.")
                : StrL("Validation comes from the application's field value."),
        invalid ? th.danger : th.mutedFg));
    col->Child(emailCol);
    col->Child(component::InputGroup::New(cx, StrL("input-group-disabled"))
                   ->Disabled(true)
                   ->Input(component::Input::New(
                               cx, StrL("input-group-disabled-input"),
                               &self->fields[IgDisabled])
                               ->AriaLabel(StrL("Disabled input")))
                   ->Addon(IgAddon(cx, "disabled-icon")
                               ->Child(IgIcon(cx, IconName::Search)))
                   ->Addon(IgAddon(cx, "disabled-action", IgAlign::InlineEnd)
                               ->Child(component::InputGroupButton::New(
                                           cx, StrL("disabled-search"))
                                           ->Label(StrL("Search"))))
                   ->IntoEl());
    El* invalidCol = Div(a)->FlexCol()->Gap(8);
    invalidCol->Child(
        component::InputGroup::New(cx, StrL("input-group-disabled-invalid"))
            ->Disabled(true)
            ->Invalid(true)
            ->Input(component::Input::New(
                        cx, StrL("input-group-disabled-invalid-input"),
                        &self->fields[IgDisabledInvalid])
                        ->AriaLabel(StrL("Disabled invalid input")))
            ->IntoEl());
    invalidCol->Child(IgNote(cx,
                             StrL("The error remains visible while editing is "
                                  "unavailable."),
                             th.danger));
    col->Child(invalidCol);
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderTextareaExamples(InputGroupStory* self, Ctx* cx) {
    int remaining =
        120 - IgCharCount(InputValue(&self->fields[IgTextareaFooter]));
    Str summary = InputValue(&self->fields[IgTextareaInvalid]);
    El* sec = StorySection(cx, "Textarea variants", nullptr);
    El* col = IgColumn(cx);
    col->Child(IgLabeled(
        cx, "Without addons", "The text viewport owns wrapping and scrolling.",
        IgExtraTextarea(cx, self, IgTextareaPlain, "textarea-plain",
                        "Plain grouped textarea")
            ->IntoEl()));
    col->Child(
        IgExtraTextarea(cx, self, IgTextareaHeader, "textarea-header",
                        "Textarea with header")
            ->Addon(IgAddon(cx, "textarea-header-addon", IgAlign::BlockStart)
                        ->Child(IgText(cx, StrL("Ask, search, or chat…"))))
            ->IntoEl());
    col->Child(
        IgExtraTextarea(cx, self, IgTextareaFooter, "textarea-footer",
                        "Textarea with remaining count")
            ->Invalid(remaining < 0)
            ->Addon(IgAddon(cx, "textarea-footer-addon", IgAlign::BlockEnd)
                        ->Child(IgText(
                            cx, StoryFmt(cx, "%d characters left", remaining))))
            ->IntoEl());
    col->Child(IgLabeled(cx, "Invalid", "Enter a summary to clear the error.",
                         IgExtraTextarea(cx, self, IgTextareaInvalid,
                                         "textarea-invalid", "Required summary")
                             ->Invalid(IgBlank(summary))
                             ->IntoEl()));
    col->Child(IgLabeled(
        cx, "Disabled", "Text and addon actions are unavailable.",
        IgExtraTextarea(cx, self, IgTextareaDisabled, "textarea-disabled",
                        "Disabled textarea")
            ->Disabled(true)
            ->Addon(IgAddon(cx, "textarea-disabled-addon", IgAlign::BlockEnd)
                        ->Child(component::InputGroupButton::New(
                                    cx, StrL("textarea-disabled-post"))
                                    ->Label(StrL("Post"))))
            ->IntoEl()));
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderToolbars(InputGroupStory* self, Ctx* cx) {
    const Theme& th = ThemeNow(cx->app);
    El* sec = StorySection(cx, "Textarea toolbars", nullptr);
    El* col = IgColumn(cx);
    component::InputGroupAddon* header =
        IgAddon(cx, "notes-header", IgAlign::BlockStart)
            ->Child(component::InputGroupText::New(cx)
                        ->Child(IgIcon(cx, IconName::File))
                        ->Child(TextEl(cx->a, StrL("script.js")))
                        ->IntoEl())
            ->Child(IgMlAuto(
                component::InputGroupButton::New(cx, StrL("copy-notes"))
                    ->Icon(IconName::Copy)
                    ->AriaLabel(StrL("Copy script"))
                    ->Tooltip(StrL("Copy script"))
                    ->OnClick(Listen(cx, &OnCopyNotes))));
    header->refiner = IgRefiner(&IgRefBorderB, &th.border);
    component::InputGroupAddon* footer =
        IgAddon(cx, "notes-footer", IgAlign::BlockEnd)
            ->Child(IgText(cx, StoryFmt(cx, "Run count: %d", self->runs)))
            ->Child(
                IgMlAuto(component::InputGroupButton::New(cx, StrL("run-notes"))
                             ->WithVariant(component::ButtonVariant::Secondary)
                             ->Label(StrL("Run"))
                             ->OnClick(Listen(cx, &OnRunNotes))));
    footer->refiner = IgRefiner(&IgRefBorderT, &th.border);
    col->Child(component::InputGroup::New(cx, StrL("input-group-notes"))
                   ->Input(component::Textarea::New(
                               cx, StrL("input-group-notes-textarea"),
                               &self->fields[IgNotes])
                               ->AriaLabel(StrL("Script text")))
                   ->Addon(header)
                   ->Addon(footer)
                   ->IntoEl());
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderComment(InputGroupStory* self, Ctx* cx) {
    const Theme& th = ThemeNow(cx->app);
    Str value = InputValue(&self->fields[IgComment]);
    El* sec = StorySection(cx, "Comment composer",
                           "Cancel clears the draft; Post keeps the submitted "
                           "text below the composer.");
    El* col = IgColumn(cx);
    col->Child(
        IgExtraTextarea(cx, self, IgComment, "comment", "Comment draft")
            ->Addon(
                IgAddon(cx, "comment-actions", IgAlign::BlockEnd)
                    ->Child(IgText(
                        cx, StoryFmt(cx, "%d characters", IgCharCount(value))))
                    ->Child(
                        IgMlAuto(component::InputGroupButton::New(
                                     cx, StrL("comment-cancel"))
                                     ->WithSize(UiSize::Small)
                                     ->Label(StrL("Cancel"))
                                     ->Disabled(len(value) == 0)
                                     ->OnClick(Listen(cx, &OnCommentCancel))))
                    ->Child(component::InputGroupButton::New(
                                cx, StrL("comment-post"))
                                ->WithSize(UiSize::Small)
                                ->WithVariant(component::ButtonVariant::Primary)
                                ->Label(StrL("Post"))
                                ->Disabled(IgBlank(value))
                                ->OnClick(Listen(cx, &OnCommentPost))))
            ->IntoEl());
    if (self->postedComment.s) {
        col->Child(IgNote(cx, StoryFmt(cx, "Posted: %s", self->postedComment),
                          th.foreground));
    }
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderCustomTextarea(InputGroupStory* self, Ctx* cx) {
    const Theme& th = ThemeNow(cx->app);
    InputState* state = &self->fields[IgCustom];
    El* sec = StorySection(cx, "Auto-growing textarea",
                           "The textarea keeps its own typography; the footer "
                           "holds a primary submit action.");
    El* col = IgColumn(cx);
    // .text_base().font_family(theme.mono_font_family): the control's own
    // style, which the group applies after its presets.
    component::Textarea* textarea =
        component::Textarea::New(cx, StrL("custom-textarea"), state)
            ->AriaLabel(StrL("Auto-growing draft"));
    textarea->refiner = IgRefiner(&IgRefTextBaseMono);
    col->Child(
        component::InputGroup::New(cx, StrL("custom"))
            ->Input(textarea)
            ->Addon(IgAddon(cx, "custom-footer", IgAlign::BlockEnd)
                        ->Child(IgText(cx, StrL("Plain text")))
                        ->Child(IgMlAuto(
                            component::InputGroupButton::New(
                                cx, StrL("custom-submit"))
                                ->WithVariant(component::ButtonVariant::Primary)
                                ->Label(StrL("Submit"))
                                ->Icon(IconName::ArrowUp)
                                ->Disabled(IgBlank(InputValue(state)))
                                ->OnClick(Listen(cx, &OnCustomSubmit)))))
            ->IntoEl());
    if (self->submittedCustom.s) {
        col->Child(IgNote(cx,
                          StoryFmt(cx, "Submitted: %s", self->submittedCustom),
                          th.foreground));
    }
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderProfile(InputGroupStory* self, Ctx* cx) {
    const Theme& th = ThemeNow(cx->app);
    El* sec = StorySection(cx, "Form composition",
                           "Use Field and GroupBox to keep labels, "
                           "descriptions, and the save action together.");
    El* col = IgColumn(cx);
    component::Field name = component::Field::New(
        component::Input::New(cx, StrL("profile-name-input"),
                              &self->fields[IgProfileName])
            ->AriaLabel(StrL("Profile display name"))
            ->IntoEl());
    name.Label(StrL("Display name"));
    component::Field email = component::Field::New(
        IgExtraInput(cx, self, IgProfileEmail, "profile-email", "Profile email")
            ->Addon(IgAddon(cx, "profile-email-icon")
                        ->Child(IgIcon(cx, IconName::Inbox)))
            ->IntoEl());
    email.Label(StrL("Email")).Description(StrL("Shown in this example only."));
    El* footer = Div(cx->a)->FlexRow()->ItemsCenter()->JustifyEnd()->Child(
        component::Button::New(cx, StrL("profile-save"))
            ->WithVariant(component::ButtonVariant::Primary)
            ->Label(StrL("Save contact"))
            ->OnClick(Listen(cx, &OnSaveProfile))
            ->IntoEl());
    col->Child(component::GroupBox::New(cx, StrL("Contact details"))
                   ->Child(component::Form::New(cx)
                               ->Child(name)
                               ->Child(email)
                               ->Footer(footer)
                               ->IntoEl())
                   ->IntoEl());
    if (self->savedProfile.s) {
        col->Child(IgNote(cx, StoryFmt(cx, "Saved: %s", self->savedProfile),
                          th.foreground));
    }
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderChat(InputGroupStory* self, Ctx* cx) {
    const Theme& th = ThemeNow(cx->app);
    Str message = InputValue(&self->fields[IgMessage]);
    int characters = IgCharCount(message);
    El* sec = StorySection(cx, "Chat textarea",
                           "The retained textarea grows with the message; its "
                           "actions remain below it.");
    El* col = IgColumn(cx);
    component::InputGroup* group =
        component::InputGroup::New(cx, StrL("input-group-message"))
            ->Invalid(characters > 280)
            ->Input(component::Textarea::New(
                        cx, StrL("input-group-message-textarea"),
                        &self->fields[IgMessage])
                        ->AriaLabel(StrL("Message")));
    if (self->attached) {
        group->Addon(IgAddon(cx, "message-attachment", IgAlign::BlockStart)
                         ->Child(component::InputGroupText::New(cx)
                                     ->Child(IgIcon(cx, IconName::File))
                                     ->Child(TextEl(cx->a, StrL("notes.txt")))
                                     ->IntoEl()));
    }
    group->Addon(
        IgAddon(cx, "message-actions", IgAlign::BlockEnd)
            ->Child(IgText(cx, StoryFmt(cx, "%d/280", characters)))
            ->Child(IgMlAuto(
                component::InputGroupButton::New(cx, StrL("attach-message"))
                    ->Icon(IconName::Plus)
                    ->AriaLabel(StrL("Toggle sample attachment"))
                    ->Tooltip(StrL("Toggle sample attachment"))
                    ->OnClick(Listen(cx, &OnAttach))))
            ->Child(component::InputGroupButton::New(cx, StrL("send-message"))
                        ->WithVariant(component::ButtonVariant::Primary)
                        ->Label(StrL("Send"))
                        ->Disabled(IgBlank(message) || characters > 280)
                        ->OnClick(Listen(cx, &OnSend))));
    col->Child(group->IntoEl());
    if (self->lastMessage.s) {
        col->Child(IgNote(cx, StoryFmt(cx, "Sent: %s", self->lastMessage),
                          th.mutedFg));
    }
    StorySectionAdd(sec, col);
    return sec;
}

static El* RenderSizes(InputGroupStory* self, Ctx* cx) {
    static const UiSize kSizes[] = {UiSize::XSmall, UiSize::Small,
                                    UiSize::Medium, UiSize::Large};
    static const char* kNames[] = {"xs", "sm", "md", "lg"};
    El* sec = StorySection(cx, "Sizes", nullptr);
    El* col = IgColumn(cx);
    for (int i = 0; i < 4; i++) {
        col->Child(
            component::InputGroup::New(cx,
                                       StoryFmt(cx, "input-group-size-%d", i))
                ->WithSize(kSizes[i])
                ->Input(component::Input::New(
                            cx, StoryFmt(cx, "input-group-size-input-%d", i),
                            &self->fields[IgSizeXs + i])
                            ->AriaLabel(StoryFmt(cx, "%s input", kNames[i])))
                ->Addon(component::InputGroupAddon::New(
                            cx, StoryFmt(cx, "size-icon-%d", i))
                            ->Child(IgIcon(cx, IconName::Search)))
                ->IntoEl());
    }
    StorySectionAdd(sec, col);
    return sec;
}

El* InputGroupStory::Render(InputGroupStory* self, Ctx* cx) {
    Seed(self, cx);
    // v_flex().w_full().gap_4(), in input_group_story.rs's order.
    El* page = Div(cx->a)->FlexCol()->Gap(16)->W(kFill);
    page->Child(RenderDefault(self, cx));
    page->Child(RenderAlignment(self, cx));
    page->Child(RenderIcons(self, cx));
    page->Child(RenderText(self, cx));
    page->Child(RenderButtons(self, cx));
    page->Child(RenderTooltips(self, cx));
    page->Child(RenderDropdowns(self, cx));
    page->Child(RenderPopover(self, cx));
    page->Child(RenderLabels(self, cx));
    page->Child(RenderButtonActions(self, cx));
    page->Child(RenderShortcut(self, cx));
    page->Child(RenderLoading(self, cx));
    page->Child(RenderValidation(self, cx));
    page->Child(RenderTextareaExamples(self, cx));
    page->Child(RenderToolbars(self, cx));
    page->Child(RenderComment(self, cx));
    page->Child(RenderCustomTextarea(self, cx));
    page->Child(RenderProfile(self, cx));
    page->Child(RenderChat(self, cx));
    page->Child(RenderSizes(self, cx));
    return page;
}

STORY_PAGE(StoryInputGroup, InputGroupStory);
