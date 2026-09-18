#include "Story.h"

// One InputState per field on the page, in the order the sections use them.
enum {
    InText = 0,
    InEmail,
    InDisabled,
    InReadonly,
    InMask,
    InCtName,
    InCtUsername,
    InCtPassword,
    InCtNewPassword,
    InCtOtp,
    InCtEmail,
    InCtTel,
    InCtUrl,
    InCtCard,
    InCtCardExp,
    InCtCardCvc,
    InCtPostal,
    InCentered,
    InRight,
    InPrefix,
    InBoth,
    InSuffix,
    InComplete,
    InCompleteDisabled,
    InCurrency,
    InPhone,
    InMaskPattern,
    InValidate,
    InEsc,
    InCustom,
    InCustomMenu,
    InColor,
    InCount
};

struct InputSeed {
    int slot;
    const char* value;
    const char* placeholder;
};

// content_type_inputs, plus every other field the Rust story creates.
static const InputSeed kSeeds[] = {
    {InText, "Hello 世界，this is GPUI Kit, this is a long text.", ""},
    {InEmail, "", "Enter text here..."},
    {InDisabled, "This is disabled input", ""},
    {InReadonly, "This is read-only input", ""},
    {InMask, "this-is-password-中文🚀🎉", "Enter your password..."},
    {InCtName, "Jane Doe", "Full name"},
    {InCtUsername, "jane.doe", "Username"},
    {InCtPassword, "current-password", "Current password"},
    {InCtNewPassword, "new-password", "New password"},
    {InCtOtp, "123456", "123456"},
    {InCtEmail, "jane.doe@example.com", "Email address"},
    {InCtTel, "+1 415 555 0198", "Telephone number"},
    {InCtUrl, "https://example.com", "Website URL"},
    {InCtCard, "4242 4242 4242 4242", "Card number"},
    {InCtCardExp, "12/28", "MM/YY"},
    {InCtCardCvc, "123", "CVC"},
    {InCtPostal, "94107", "Postal code"},
    {InCentered, "Centered Text", "Enter text to test center layout..."},
    {InRight, "Right Aligned Text", "Enter text to test right layout..."},
    {InPrefix, "", "Search some thing..."},
    {InBoth, "", "This input have prefix and suffix."},
    {InSuffix, "", "This input only support [a-zA-Z0-9] characters."},
    {InComplete, "jane.doe@example.com", "Search account..."},
    {InCompleteDisabled, "disabled.account@example.com", "Search account..."},
    {InValidate, "", "validate to limit float number."},
    {InEsc, "", "Enter text and clear it by pressing ESC"},
    {InCustom, "", "Custom Input use monospace, 0123456789."},
    {InCustomMenu, "", "Input with custom context menu..."},
    {InColor, "Custom text color input", "Type something..."},
};

struct ContentTypeRow {
    int slot;
    const char* label;
    bool maskToggle;
    component::InputContentType contentType;
};

static const ContentTypeRow kContentTypes[] = {
    {InCtName, "Name", false, component::InputContentType::Name},
    {InCtUsername, "Username", false, component::InputContentType::Username},
    {InCtPassword, "Password", true, component::InputContentType::Password},
    {InCtNewPassword, "New password", true,
     component::InputContentType::NewPassword},
    {InCtOtp, "One-time code", false, component::InputContentType::OneTimeCode},
    {InCtEmail, "Email", false, component::InputContentType::EmailAddress},
    {InCtTel, "Telephone", false, component::InputContentType::TelephoneNumber},
    {InCtUrl, "URL", false, component::InputContentType::Url},
    {InCtCard, "Credit card number", false,
     component::InputContentType::CreditCardNumber},
    {InCtCardExp, "Credit card expiration", false,
     component::InputContentType::CreditCardExpiration},
    {InCtCardCvc, "Credit card security code", true,
     component::InputContentType::CreditCardSecurityCode},
    {InCtPostal, "Postal code", false, component::InputContentType::PostalCode},
};

struct InputStory {
    InputState fields[InCount];
    // The fields whose value is hidden until the eye is clicked.
    bool revealed[InCount] = {};
    int focusedField = -1;
    StoryToolbarState toolbar;
    bool seeded = false;
    EntityId tokens;

    static El* Render(InputStory* self, Ctx* cx);
    static void OnKey(InputStory* self, Ctx* cx, const KeyEvent* ev);
};

static void FocusField(InputStory* self, Ctx* cx, const ClickEvent*,
                       intptr_t slot) {
    for (int i = 0; i < InCount; i++) {
        self->fields[i].focused = false;
    }
    self->fields[slot].focused = true;
    self->focusedField = (int)slot;
    Notify(cx);
}
static void ClearField(InputStory* self, Ctx* cx, const ClickEvent*,
                       intptr_t slot) {
    InputSetValue(&self->fields[slot], Str{});
    Notify(cx);
}
static void ToggleMask(InputStory* self, Ctx* cx, const ClickEvent*,
                       intptr_t slot) {
    self->revealed[slot] = !self->revealed[slot];
    Notify(cx);
}

static component::Input* Field(InputStory* self, Ctx* cx, int slot,
                               Listener focus, Listener clear) {
    return component::Input::New(cx, StoryFmt(cx, "input-%d", slot),
                                 &self->fields[slot])
        ->WithSize(self->toolbar.size)
        ->OnFocus(ListenerArg(focus, slot))
        ->OnClear(ListenerArg(clear, slot));
}

// section() is a justify_center wrapping row, so a readout that wraps under
// its field is centered rather than pinned to the field's left edge.
El* InputStory::Render(InputStory* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->seeded = true;
        for (size_t i = 0; i < sizeof(kSeeds) / sizeof(kSeeds[0]); i++) {
            InputState* f = &self->fields[kSeeds[i].slot];
            InputSetValue(f, Str(kSeeds[i].value));
            InputSetPlaceholder(f, Str(kSeeds[i].placeholder));
        }
        // The three masked fields, with the patterns the Rust story names.
        // `mask_pattern()` also puts the derived cue in as the placeholder,
        // which is what shows `(___)-___-____` in an empty phone field.
        InputSetMaskPattern(&self->fields[InPhone],
                            MaskPatternNew(StrL("(999)-999-9999")));
        InputSetMaskPattern(&self->fields[InMaskPattern],
                            MaskPatternNew(StrL("AAA-###-AAA")));
        // MaskPattern::Number { separator: Some(','), fraction: Some(3) }.
        MaskPattern num = MaskPatternNumber(',');
        num.fraction = 3;
        InputSetMaskPattern(&self->fields[InCurrency], num);
    }
    if (self->focusedField >= 0) {
        cx->win->input = &self->fields[self->focusedField];
    }
    Listener focus = Listen(cx, &FocusField);
    Listener clear = Listen(cx, &ClearField);
    Listener mask = Listen(cx, &ToggleMask);

    El* page = Div(a)->FlexCol()->Gap(12)->W(kFill);
    page->Child(StoryToolbar(cx, self));

    El* def = StorySection(cx, "Default", "Text, email, and clearable inputs.");
    StorySectionBody(def)->W(512);
    StorySectionAdd(
        def, Field(self, cx, InText, focus, clear)->Cleanable()->IntoEl());
    StorySectionAdd(def, Field(self, cx, InEmail, focus, clear)->IntoEl());
    page->Child(def);

    El* states = StorySection(
        cx, "States", "Disabled, read-only and revealable password inputs.");
    StorySectionBody(states)->W(512);
    StorySectionAdd(states, Field(self, cx, InDisabled, focus, clear)
                                ->Disabled(true)
                                ->IntoEl());
    StorySectionAdd(states, Field(self, cx, InReadonly, focus, clear)
                                ->Readonly()
                                ->IntoEl());
    StorySectionAdd(states, Field(self, cx, InMask, focus, clear)
                                ->Masked(!self->revealed[InMask])
                                ->MaskToggle()
                                ->OnToggleMask(ListenerArg(mask, InMask))
                                ->Cleanable()
                                ->IntoEl());
    page->Child(states);

    El* ct =
        StorySection(cx, "Content type", "Content types adapt input behavior.");
    StorySectionBody(ct)->W(512);
    for (size_t i = 0; i < sizeof(kContentTypes) / sizeof(kContentTypes[0]);
         i++) {
        const ContentTypeRow& row = kContentTypes[i];
        El* line = Div(a)->FlexRow()->W(kFill)->Gap(12)->ItemsCenter();
        line->Child(StoryTxt(cx, StoryDup(cx, row.label), 14, th.foreground)
                        ->W(192)
                        ->Shrink0());
        component::Input* in = Field(self, cx, row.slot, focus, clear)
                                   ->ContentType(row.contentType);
        if (row.maskToggle) {
            in->Masked(!self->revealed[row.slot])
                ->MaskToggle()
                ->OnToggleMask(ListenerArg(mask, row.slot));
        }
        line->Child(Div(a)->Flex1()->Child(in->IntoEl()));
        StorySectionAdd(ct, line);
    }
    page->Child(ct);

    El* align =
        StorySection(cx, "Alignment", "Align text to the center or end.");
    StorySectionBody(align)->W(512);
    El* alignRow =
        Div(a)->FlexRow()->FlexWrap()->W(512)->Gap(16)->ItemsCenter();
    alignRow->Child(
        Div(a)->Flex1()->Child(Field(self, cx, InCentered, focus, clear)
                                   ->Align(component::InputAlign::Center)
                                   ->IntoEl()));
    alignRow
        ->Child(Div(a)->Flex1()->Child(Field(self, cx, InRight, focus, clear)
                                           ->Align(component::InputAlign::Right)
                                           ->IntoEl()));
    StorySectionAdd(align, alignRow);
    page->Child(align);

    El* affix = StorySection(cx, "Prefix and suffix",
                             "Add icons or actions inside the field.");
    StorySectionBody(affix)->W(512);
    StorySectionAdd(affix,
                    Field(self, cx, InPrefix, focus, clear)
                        ->Prefix(Div(a)->PadL(10)->Child(
                            IconEl(a, IconName::Search, 16)->Fg(th.mutedFg)))
                        ->Cleanable()
                        ->IntoEl());
    StorySectionAdd(affix,
                    Field(self, cx, InBoth, focus, clear)
                        ->Prefix(Div(a)->PadL(10)->Child(
                            IconEl(a, IconName::Search, 16)->Fg(th.mutedFg)))
                        ->Suffix(component::Button::New(cx, StrL("info"))
                                     ->Text()
                                     ->WithSize(UiSize::XSmall)
                                     ->Icon(IconName::Info)
                                     ->IntoEl())
                        ->Cleanable()
                        ->IntoEl());
    StorySectionAdd(affix,
                    Field(self, cx, InSuffix, focus, clear)
                        ->Suffix(component::Button::New(cx, StrL("info2"))
                                     ->Text()
                                     ->WithSize(UiSize::XSmall)
                                     ->Icon(IconName::Info)
                                     ->IntoEl())
                        ->Cleanable()
                        ->IntoEl());
    page->Child(affix);

    El* composed = StorySection(cx, "Composed states",
                                "Composed inputs support disabled state.");
    StorySectionBody(composed)->W(512);
    StorySectionAdd(
        composed,
        Field(self, cx, InComplete, focus, clear)
            ->Prefix(Div(a)->PadL(10)->Child(IconEl(a, IconName::Search, 16)
                                                 ->Fg(th.mutedFg)))
            ->Suffix(component::Button::New(cx, StrL("complete-input-info"))
                         ->Text()
                         ->WithSize(UiSize::XSmall)
                         ->Icon(IconName::Info)
                         ->IntoEl())
            ->Cleanable()
            ->IntoEl());
    StorySectionAdd(
        composed,
        Field(self, cx, InCompleteDisabled, focus, clear)
            ->Disabled(true)
            ->Prefix(Div(a)->PadL(10)->Child(IconEl(a, IconName::Search, 16)
                                                 ->Fg(th.mutedFg)))
            ->Suffix(component::Button::New(cx, StrL("complete-disabled-info"))
                         ->Text()
                         ->WithSize(UiSize::XSmall)
                         ->Icon(IconName::Info)
                         ->IntoEl())
            ->Cleanable()
            ->IntoEl());
    page->Child(composed);

    El* currency = StorySection(cx, "Currency",
                                "Format currency while retaining its value.");
    StorySectionBody(currency)->W(512);
    StorySectionAdd(currency, Field(self, cx, InCurrency, focus, clear)
                                  ->IntoEl());
    StorySectionAdd(currency,
                    StoryTxt(cx,
                             StoryFmt(cx, "Value: \"%s\"",
                                      InputCStr(&self->fields[InCurrency])),
                             16, th.foreground));
    page->Child(currency);

    El* phone = StorySection(cx, "Phone mask",
                             "Expose formatted and raw phone values.");
    StorySectionBody(phone)->W(512);
    StorySectionAdd(phone, Field(self, cx, InPhone, focus, clear)->IntoEl());
    El* phoneVals = Div(a)->FlexCol();
    phoneVals->Child(StoryTxt(
        cx, StoryFmt(cx, "Value: \"%s\"", InputCStr(&self->fields[InPhone])),
        16, th.foreground));
    // unmask_value(): the digits without the pattern around them, which is
    // the other half of what the Rust story shows under the field.
    phoneVals->Child(StoryTxt(
        cx,
        StoryFmt(cx, "Unmask Value: \"%s\"",
                 StrDup(a, InputUnmaskValue(a, &self->fields[InPhone]))),
        16, th.foreground));
    StorySectionAdd(phone, phoneVals);
    page->Child(phone);

    El* pattern = StorySection(cx, "Mask pattern",
                               "Combine letter and number placeholders.");
    StorySectionBody(pattern)->W(512);
    StorySectionAdd(pattern, Field(self, cx, InMaskPattern, focus, clear)
                                 ->IntoEl());
    El* patternVals = Div(a)->FlexCol();
    patternVals->Child(StoryTxt(
        cx,
        StoryFmt(cx, "Value: \"%s\"", InputCStr(&self->fields[InMaskPattern])),
        16, th.foreground));
    patternVals->Child(StoryTxt(
        cx,
        StoryFmt(cx, "Unmask Value: \"%s\"",
                 StrDup(a, InputUnmaskValue(a, &self->fields[InMaskPattern]))),
        16, th.foreground));
    StorySectionAdd(pattern, patternVals);
    page->Child(pattern);

    El* validation =
        StorySection(cx, "Validation", "Validate values while the user types.");
    StorySectionBody(validation)->W(512);
    StorySectionAdd(validation, Field(self, cx, InValidate, focus, clear)
                                    ->IntoEl()
                                    ->W(512));
    page->Child(validation);

    El* esc = StorySection(cx, "Clear on Escape",
                           "Clear a value with its action or Escape.");
    StorySectionBody(esc)->W(512);
    StorySectionAdd(
        esc,
        Field(self, cx, InEsc, focus, clear)->Cleanable()->IntoEl()->W(512));
    page->Child(esc);

    El* focusedSec = StorySection(cx, "Focused value",
                                  "Read the value of the focused input.");
    StorySectionBody(focusedSec)->W(512);
    Str focusedValue =
        self->focusedField >= 0
            ? StoryFmt(cx, "Value: Some(\"%s\")",
                       InputCStr(&self->fields[self->focusedField]))
            : StoryDup(cx, "Value: None");
    StorySectionAdd(focusedSec, StoryTxt(cx, focusedValue, 16, th.foreground)
                                    ->W(512));
    page->Child(focusedSec);

    El* custom = StorySection(cx, "Custom appearance",
                              "Remove the default field appearance.");
    StorySectionBody(custom)->W(512);
    // border_b_2, px_6 py_3, monospace, on the secondary background.
    El* customBox = Div(a)
                        ->W(512)
                        ->PadX(24)
                        ->PadY(12)
                        ->Bg(th.tokens.secondary)
                        ->BorderB(2, th.border)
                        ->Mono()
                        ->Child(Field(self, cx, InCustom, focus, clear)
                                    ->Appearance(false)
                                    ->IntoEl());
    StorySectionAdd(custom, customBox);
    page->Child(custom);

    El* menu =
        StorySection(cx, "Context menu", "Add actions to the editing menu.");
    StorySectionBody(menu)->W(512);
    StorySectionAdd(
        menu, Field(self, cx, InCustomMenu, focus, clear)->IntoEl()->W(512));
    page->Child(menu);

    El* color = StorySection(cx, "Text color", "Apply a semantic text color.");
    StorySectionBody(color)->W(512);
    StorySectionAdd(color, Field(self, cx, InColor, focus, clear)
                               ->TextColor(th.info)
                               ->IntoEl()
                               ->W(512));
    page->Child(color);

    if (!self->tokens.IsValid()) {
        self->tokens = TokenExampleNew(cx->app, false);
    }
    El* tokens = StorySection(
        cx, "Atomic inline tokens",
        "References keep their identity through selection, deletion and undo. "
        "Copy returns the underlying text.");
    StorySectionBody(tokens)->W(kFill);
    StorySectionAdd(tokens,
                    EntityRender(cx->app, cx->win, cx->a, self->tokens));
    page->Child(tokens);
    return page;
}

// Esc clears the field that asks for it, as its section says.
void InputStory::OnKey(InputStory* self, Ctx* cx, const KeyEvent* ev) {
    if (ev->vk != KeyEscape) {
        return;
    }
    InputSetValue(&self->fields[InEsc], Str{});
    Notify(cx);
}

STORY_PAGE_KEYS(StoryInput, InputStory);
