#include "ui/i18n.h"
#include "ui/input.h"
#include "ui/button.h"
#include "ui/highlighter.h"
#include "ui/native_menu.h"
#include "base/input.h"
#include "base/motion.h"

namespace gpui {

namespace component {

static const SyntaxContext kLanguageNotIn[] = {SyntaxContext::String,
                                               SyntaxContext::Comment};
static const BracketPair kJsonBrackets[] = {
    {StrL("{"), StrL("}")},
    {StrL("["), StrL("]")},
};
static const AutoClosingPair kJsonPairs[] = {
    {StrL("{"), StrL("}"), kLanguageNotIn, 2},
    {StrL("["), StrL("]"), kLanguageNotIn, 2},
    {StrL("\""), StrL("\""), kLanguageNotIn, 2},
};

void InputLanguageInit(App* app) {
    LanguageConfig text = LanguageConfig::Default();
    text.brackets = nullptr;
    text.nBrackets = 0;
    text.autoClosingPairs = nullptr;
    text.nAutoClosingPairs = 0;
    text.hasAutoClosingPairs = true;
    InputSetLanguageConfig(app, StrL("text"), text);

    LanguageConfig json = LanguageConfig::Default();
    json.brackets = kJsonBrackets;
    json.nBrackets = 2;
    json.autoClosingPairs = kJsonPairs;
    json.nAutoClosingPairs = 3;
    json.hasAutoClosingPairs = true;
    InputSetLanguageConfig(app, StrL("json"), json);

    LanguageConfig python = LanguageConfig::Default();
    python.indentation = IndentationRules::FromPatterns(
        StrL("[\\{\\(\\[:]\\s*$"), StrL("^\\s*[\\}\\)\\]]"));
    python.hasIndentationRules = true;
    InputSetLanguageConfig(app, StrL("python"), python);
}

InputToken* InputToken::New(Ctx* cx, const InlineTokenContext& context) {
    InputToken* t = ArenaNew<InputToken>(cx->a);
    t->a = cx->a;
    t->cx = cx;
    t->context = context;
    return t;
}

InputToken* InputToken::Icon(IconName name) {
    icon = name;
    hasIcon = true;
    return this;
}

El* InputToken::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    El* el =
        Div(a)
            ->FlexRow()
            ->ItemsCenter()
            ->Gap(4)
            ->PadX(4)
            ->H(context.lineHeight > 0 ? context.lineHeight : 20.f)
            ->MaxW(context.availableWidth > 0 ? context.availableWidth : kFill)
            ->Radius(th.radius)
            ->Border(1, context.selected ? RgbaOpacity(th.selection, 1.f)
                                         : th.border)
            ->Bg(context.selected ? th.selection : th.muted)
            ->Fg(th.foreground);
    if (context.disabled) {
        el->Opacity(0.5f);
    }
    if (hasIcon) {
        el->Child(IconEl(a, icon, 12)->Shrink0());
    }
    el->Child(TextEl(a, context.Token().label)->MinW(0));
    return el;
}

AnyInputState AnyInputState::From(InputState* state) {
    if (!state) return {};
    switch (state->kind) {
        case InputKind::Textarea:
            return FromTextarea(state);
        case InputKind::Editor:
            return FromEditor(state);
        default:
            return FromInput(state);
    }
}

AnyInputState AnyInputState::FromInput(InputState* state) {
    AnyInputState out;
    out.kind = state ? AnyInputKind::Input : AnyInputKind::None;
    out.text = state;
    return out;
}

AnyInputState AnyInputState::FromTextarea(InputState* state) {
    AnyInputState out;
    out.kind = state ? AnyInputKind::Textarea : AnyInputKind::None;
    out.text = state;
    return out;
}

AnyInputState AnyInputState::FromEditor(InputState* state) {
    AnyInputState out;
    out.kind = state ? AnyInputKind::Editor : AnyInputKind::None;
    out.text = state;
    return out;
}

AnyInputState AnyInputState::FromOtp(Entity<OtpState> state) {
    AnyInputState out;
    out.kind = state.IsValid() ? AnyInputKind::Otp : AnyInputKind::None;
    out.otp = state;
    return out;
}

InputState* AnyInputState::AsInput() const {
    return kind == AnyInputKind::Input ? text : nullptr;
}

InputState* AnyInputState::AsTextarea() const {
    return kind == AnyInputKind::Textarea ? text : nullptr;
}

InputState* AnyInputState::AsEditor() const {
    return kind == AnyInputKind::Editor ? text : nullptr;
}

Entity<OtpState> AnyInputState::AsOtp() const {
    return kind == AnyInputKind::Otp ? otp : Entity<OtpState>{};
}

static Str MaskedInputValue(Arena* a, Str text) {
    int chars = 0;
    for (int i = 0; i < len(text); i++) {
        if (((uint8_t)text.s[i] & 0xc0) != 0x80) chars++;
    }
    char* out = (char*)Alloc(a, chars * 3 + 1);
    if (!out) return {};
    int n = 0;
    for (int i = 0; i < chars; i++) {
        memcpy(out + n, "\xE2\x80\xA2", 3);
        n += 3;
    }
    out[n] = 0;
    return Str(out, n);
}

Str AnyInputState::Value(Arena* a, App* app) const {
    if (kind == AnyInputKind::Otp) {
        OtpState* state = otp.Get(app);
        if (!state) return {};
        Str value = OtpValue(state);
        return state->masked ? MaskedInputValue(a, value) : StrDup(a, value);
    }
    if (!text) return {};
    Str value = InputValue(text);
    return text->masked ? MaskedInputValue(a, value) : StrDup(a, value);
}

FocusHandle AnyInputState::FocusHandleOf(const Window* window, App* app) const {
    if (kind == AnyInputKind::Otp) {
        OtpState* state = otp.Get(app);
        return state ? state->focus : FocusHandle{};
    }
    if (!text) return {};
    if (text->focus.IsValid()) return text->focus;
    // A state first observed after an old frame can still recover the handle
    // that frame focused. The next BindInput stores it on the state.
    return text->focused && text->focusWin == window ? WindowFocused(window)
                                                     : FocusHandle{};
}

// input.rs: the frame tracks a handle of its own, not the editor's, and
// reads as focused while the editor has the focus or anything else inside
// it does. The frame here also stays the element bound to the state, since
// its box is the field's geometry and accessibility node, but the handle it
// tracks is this one: a press on its padding no longer focuses the editor
// (Rust's frame has no press handler) and Tab stops only at the editor.
static FocusHandle InputFrameFocus(Ctx* cx, InputState* state) {
    if (!state) return {};
    if (!state->frameFocus.IsValid()) state->frameFocus = FocusHandleNew(cx);
    return state->frameFocus;
}

static bool InputFrameFocused(Ctx* cx, InputState* state, bool disabled) {
    if (!state || disabled) return false;
    return state->focused ||
           FocusHandleContainsFocused(cx->win, InputFrameFocus(cx, state));
}

static void InputTrackFrameFocus(Ctx* cx, El* frame, InputState* state) {
    if (!state) return;
    frame->TrackFocus(InputFrameFocus(cx, state))
        ->TabStop(false)
        ->FocusOnPress(false);
}

bool AnyInputState::operator==(const AnyInputState& other) const {
    return kind == other.kind && text == other.text && otp.id == other.otp.id;
}

// The right-click menu every Input, Textarea and Editor has. input.rs hands
// the state an on_context_menu handler, and the state does the rest: its
// right press moves the caret unless it landed in the selection, and the
// release runs the handler deferred, unless the field is disabled or a
// deferred layer is open (InputHandleRightClickMenu). The menu is the
// caller's context_menu(..) if it set one, and input.rs's built-in one
// otherwise.

// The built-in menu's selection comes back through a listener, which names
// an entity: the field's element state, which knows the InputState. A
// selection after the field stopped rendering finds no entity and is
// dropped.
struct InputContextMenuState {
    InputState* state = nullptr;

    static void OnSelect(InputContextMenuState* self, Ctx* cx,
                         const ClickEvent*, intptr_t id) {
        InputState* s = self->state;
        if (!s) {
            return;
        }
        gpui::NativeMenu rows;
        InputDefaultNativeMenu(s, &rows);
        if (id < 1 || id > rows.items.len) {
            return;
        }
        InputPerformNativeMenuItem(s, cx->app, cx->win,
                                   rows.items[(int)id - 1]);
        Notify(cx);
    }
};

// What the handler captures, owned by the state the way Rust's closure is:
// the state frees it when the handler is replaced or the state goes, and a
// field that renders again updates it in place.
struct InputContextMenu {
    InputState* state = nullptr;
    bool disabled = false;
    EditorContextMenuFn build = nullptr;
    void* data = nullptr;
    EntityId selection = {};

    static void Drop(void* user) { delete (InputContextMenu*)user; }

    // The handler: the menu, built and shown where the press was.
    static void Show(void* user, gpui::NativeMenu*,
                     const InputContextMenuCapabilities&, Point position,
                     App* app, Window* win) {
        auto* self = (InputContextMenu*)user;
        InputState* s = self ? self->state : nullptr;
        if (!s || self->disabled || !win) {
            return;
        }
        Ctx cx = {};
        cx.app = app;
        cx.win = win;
        cx.a = win->frameArena;
        cx.self = self->selection;
        NativeMenu* menu = NativeMenu::New(&cx);
        if (self->build) {
            menu = self->build(&cx, menu, self->data);
        } else {
            // The built-in rows, translated; a row's id is its index + 1 in
            // the menu InputDefaultNativeMenu builds, rebuilt on selection.
            gpui::NativeMenu rows;
            InputDefaultNativeMenu(s, &rows);
            for (int i = 0; i < rows.items.len; i++) {
                const gpui::NativeMenuItem& row = rows.items[i];
                if (row.kind == gpui::NativeMenuItemKind::Separator) {
                    menu->Separator();
                    continue;
                }
                Str label = Tr(fmt("Input.%s", row.label).s);
                menu->MenuWithDisabled(label, row.disabled, i + 1);
            }
            menu->OnSelect(Listen(&cx, &InputContextMenuState::OnSelect));
        }
        if (menu && !menu->IsEmpty()) {
            menu->Show(position.x, position.y);
        }
    }
};

// Install the menu as the state's on_context_menu.
static void BindInputContextMenu(Ctx* cx, El* e, Str id, InputState* state,
                                 bool disabled, EditorContextMenuFn build,
                                 void* data) {
    (void)e;
    if (!state) {
        return;
    }
    Entity<InputContextMenuState> selection =
        ElementStateEntity<InputContextMenuState>(
            cx, id, StrL("component::InputContextMenu"));
    if (InputContextMenuState* st = selection.Get(cx)) {
        st->state = state;
    }
    auto* menu = state->contextMenuHandler == &InputContextMenu::Show
                     ? (InputContextMenu*)state->contextMenuData
                     : nullptr;
    if (!menu) {
        menu = new InputContextMenu();
        InputOnContextMenu(state, &InputContextMenu::Show, menu,
                           &InputContextMenu::Drop);
    }
    menu->state = state;
    menu->disabled = disabled;
    menu->build = build;
    menu->data = data;
    menu->selection = selection.id;
}

Editor* Editor::New(Ctx* cx, InputState* state) {
    return New(cx, StrL("editor"), state);
}

Editor* Editor::New(Ctx* cx, Str id, InputState* state) {
    Editor* editor = ArenaNew<Editor>(cx->a);
    editor->a = cx->a;
    editor->cx = cx;
    editor->id = id;
    editor->state = state;
    return editor;
}

Editor* Editor::H(float value) {
    height = value;
    return this;
}

Editor* Editor::Font(float value) {
    fontSize = value;
    return this;
}
Editor* Editor::FontFamily(Str family) {
    fontFamily = family;
    return this;
}

Editor* Editor::Appearance(bool value) {
    appearance = value;
    return this;
}

Editor* Editor::Bordered(bool value) {
    bordered = value;
    return this;
}

Editor* Editor::Disabled(bool value) {
    disabled = value;
    return this;
}

Editor* Editor::Readonly(bool value) {
    readonly = value;
    return this;
}

Editor* Editor::TabIndex(int value) {
    tabIndex = value;
    return this;
}

Editor* Editor::Role(AccessibilityRole value) {
    accessibilityRole = value;
    return this;
}

Editor* Editor::AriaLabel(Str value) {
    ariaLabel = value;
    return this;
}

Input* Input::ContextMenu(EditorContextMenuFn fn, void* data) {
    contextMenu = fn;
    contextMenuData = data;
    return this;
}

Textarea* Textarea::ContextMenu(EditorContextMenuFn fn, void* data) {
    contextMenu = fn;
    contextMenuData = data;
    return this;
}

Editor* Editor::ContextMenu(EditorContextMenuFn fn, void* data) {
    contextMenu = fn;
    contextMenuData = data;
    return this;
}

Editor* Editor::OnPaste(InputPasteFn fn, void* data) {
    onPaste = fn;
    onPasteData = data;
    return this;
}

Editor* Editor::Language(Str value) {
    language = value;
    return this;
}

Editor* Editor::Decorations(const TextSpan* runs, int n) {
    decorations = runs;
    nDecorations = n;
    return this;
}

Editor* Editor::ActiveLine(bool value) {
    activeLine = value;
    return this;
}

Editor* Editor::IndentGuides(bool value) {
    indentGuides = value;
    return this;
}

Editor* Editor::Searchable(bool value) {
    searchable = value;
    return this;
}

Editor* Editor::Diagnostics(const Diagnostic* items, int n) {
    diagnostics = items;
    nDiagnostics = n;
    return this;
}

Editor* Editor::Folding(bool value) {
    folding = value;
    return this;
}

Editor* Editor::Refine(const gpui::Style& value, uint32_t fields) {
    StyleApplyFields(&style, value, fields);
    styleFields |= fields;
    return this;
}

El* Editor::IntoEl() {
    if (!state) return Div(a);
    state->kind = InputKind::Editor;
    state->mode.kind = LayoutModeKind::CodeEditor;
    state->disabled = disabled;
    state->readonly = readonly;
    state->pasteHandler = !disabled && !readonly ? onPaste : nullptr;
    state->pasteHandlerData = state->pasteHandler ? onPasteData : nullptr;

    Highlighter* highlighter = Highlighter::New(cx, id, state);
    highlighter->Searchable(searchable)
        ->ActiveLine(activeLine)
        ->IndentGuides(indentGuides)
        ->Folding(folding)
        ->Decorations(decorations, nDecorations)
        ->Diagnostics(diagnostics, nDiagnostics);
    if (language.s) highlighter->Language(language);
    if (height > 0 || height == kFill) highlighter->H(height);
    if (fontSize > 0) highlighter->Font(fontSize);
    highlighter->FontFamily(fontFamily);
    // input.rs set_editor_paddings for a multi-line Input of the default
    // Medium size: input_py above and below, input_px on the right, and on
    // the left input_px held to 6 for a code editor, where the gutter is.
    highlighter->editorPad = Edges::New(6, 10, 8, 8);
    El* element = highlighter->IntoEl();

    const Theme& theme = ThemeNow(cx->app);
    element->TabIndex(tabIndex)
        ->Role(accessibilityRole)
        ->AriaDisabled(disabled);
    if (ariaLabel.s) element->AriaLabel(ariaLabel);
    if (appearance) element->Bg(theme.inputBg);
    if (bordered) element->Border(1, theme.inputBorder)->Radius(theme.radius);
    if (disabled) element->Opacity(0.5f);
    if (styleFields) element->Refine(style, styleFields);

    BindInputContextMenu(cx, element, id, state, disabled, contextMenu,
                         contextMenuData);
    return element;
}

Input* Input::New(Ctx* cx, Str id, InputState* state) {
    Arena* a = cx->a;
    Input* i = ArenaNew<Input>(a);
    i->a = a;
    i->cx = cx;
    i->id = id;
    i->state = state;
    return i;
}
Input* Input::Label(Str s) {
    label = s;
    return this;
}
Input* Input::Prefix(El* el) {
    prefix = el;
    return this;
}
Input* Input::Suffix(El* el) {
    suffix = el;
    return this;
}
Input* Input::W(float v) {
    width = v;
    return this;
}
Input* Input::OnChange(Listener fn) {
    onChange = fn;
    return this;
}

Input* Input::OnFocus(Listener fn) {
    onFocus = fn;
    return this;
}
Input* Input::OnPaste(InputPasteFn fn, void* data) {
    onPaste = fn;
    onPasteData = data;
    return this;
}

Input* Input::Token(InlineTokenRenderer fn, void* user) {
    tokenRenderer = fn;
    tokenRendererUser = user;
    return this;
}

Input* Input::OnTokenClick(InlineTokenClickListener fn, void* user) {
    tokenClick = fn;
    tokenClickUser = user;
    return this;
}

static El* DefaultInputTokenRender(Ctx* cx, const InlineTokenContext* ctx,
                                   void*) {
    return InputToken::New(cx, *ctx)->IntoEl();
}
Input* Input::WithSize(UiSize s) {
    size = s;
    return this;
}
Input* Input::Align(InputAlign v) {
    align = v;
    return this;
}
Input* Input::Disabled(bool v) {
    disabled = v;
    return this;
}
Input* Input::Readonly(bool v) {
    readonly = v;
    if (state) {
        state->readonly = v;
    }
    return this;
}
Input* Input::ContentType(InputContentType value) {
    contentType = value;
    hasContentType = true;
    return this;
}
Input* Input::Role(AccessibilityRole value) {
    accessibilityRole = value;
    hasAccessibilityRole = true;
    return this;
}
Input* Input::AccessibilityId(Str value) {
    accessibilityId = value;
    return this;
}
Input* Input::AriaLabel(Str value) {
    ariaLabel = value;
    return this;
}
Input* Input::Cleanable(bool v) {
    cleanable = v;
    return this;
}
Input* Input::Masked(bool v) {
    masked = v;
    return this;
}
Input* Input::MaskToggle(bool v) {
    maskToggle = v;
    return this;
}
Input* Input::Appearance(bool v) {
    appearance = v;
    return this;
}
Input* Input::FocusRing(bool v) {
    focusRing = v;
    return this;
}
Input* Input::TextColor(Rgba c) {
    textColor = c;
    hasTextColor = true;
    return this;
}
Input* Input::OnClear(Listener fn) {
    onClear = fn;
    return this;
}
Input* Input::OnToggleMask(Listener fn) {
    onToggleMask = fn;
    return this;
}

// Size::Medium, the default: input_h is h_8, input_px 10, input_py 8,
// input_text_size text_sm, gap 6 (crates/ui/src/sizing.rs).
static const float kInputHeight = 32;
static const float kInputPadX = 10;
static const float kInputPadY = 8;
static const float kInputGap = 6;
static const float kInputTextSize = 14;

// input_h / input_px / input_py / input_text_size, by size.
static void InputSizeMetrics(UiSize size, float* h, float* padX, float* padY,
                             float* font) {
    *h = kInputHeight;
    *padX = kInputPadX;
    *padY = kInputPadY;
    *font = kInputTextSize;
    if (size == UiSize::Large) {
        *h = 44;
        *padX = 12;
        *padY = 10;
        *font = 16;
    } else if (size == UiSize::Small) {
        *h = 24;
        *padX = 8;
        *padY = 2;
    } else if (size == UiSize::XSmall) {
        *h = 20;
        *padX = 4;
        *padY = 0;
        *font = 12;
    }
}

static AccessibilityRole InputAccessibilityRole(bool hasContentType,
                                                InputContentType contentType) {
    if (!hasContentType) {
        return AccessibilityRole::TextInput;
    }
    switch (contentType) {
        case InputContentType::TelephoneNumber:
            return AccessibilityRole::PhoneNumberInput;
        case InputContentType::EmailAddress:
            return AccessibilityRole::EmailInput;
        case InputContentType::Url:
            return AccessibilityRole::UrlInput;
        case InputContentType::Password:
        case InputContentType::NewPassword:
            return AccessibilityRole::PasswordInput;
        case InputContentType::DateTime:
            return AccessibilityRole::DateTimeInput;
        case InputContentType::Birthdate:
            return AccessibilityRole::DateInput;
        default:
            return AccessibilityRole::TextInput;
    }
}

static bool InputContentIsSecret(bool hasContentType,
                                 InputContentType contentType) {
    return hasContentType && (contentType == InputContentType::Password ||
                              contentType == InputContentType::NewPassword);
}

// content_type.rs::ns_text_content_type. These are WHATWG autocomplete names,
// which are also the values GPUI's NSTextContent adapter publishes on macOS.
static Str InputNativeContentType(bool present, InputContentType value) {
    if (!present) return {};
    switch (value) {
        case InputContentType::Name:
            return StrL("name");
        case InputContentType::NamePrefix:
            return StrL("honorific-prefix");
        case InputContentType::GivenName:
            return StrL("given-name");
        case InputContentType::MiddleName:
            return StrL("additional-name");
        case InputContentType::FamilyName:
            return StrL("family-name");
        case InputContentType::NameSuffix:
            return StrL("honorific-suffix");
        case InputContentType::Nickname:
            return StrL("nickname");
        case InputContentType::JobTitle:
            return StrL("organization-title");
        case InputContentType::OrganizationName:
            return StrL("organization");
        case InputContentType::Location:
            return StrL("location");
        case InputContentType::FullStreetAddress:
            return StrL("street-address");
        case InputContentType::StreetAddressLine1:
            return StrL("address-line1");
        case InputContentType::StreetAddressLine2:
            return StrL("address-line2");
        case InputContentType::AddressCity:
            return StrL("address-level2");
        case InputContentType::AddressState:
            return StrL("address-level1");
        case InputContentType::AddressCityAndState:
            return StrL("address-level1+2");
        case InputContentType::Sublocality:
            return StrL("address-level3");
        case InputContentType::CountryName:
            return StrL("country-name");
        case InputContentType::PostalCode:
            return StrL("postal-code");
        case InputContentType::TelephoneNumber:
            return StrL("tel");
        case InputContentType::EmailAddress:
            return StrL("email");
        case InputContentType::Url:
            return StrL("url");
        case InputContentType::CreditCardNumber:
            return StrL("cc-number");
        case InputContentType::CreditCardName:
            return StrL("cc-name");
        case InputContentType::CreditCardGivenName:
            return StrL("cc-given-name");
        case InputContentType::CreditCardMiddleName:
            return StrL("cc-additional-name");
        case InputContentType::CreditCardFamilyName:
            return StrL("cc-family-name");
        case InputContentType::CreditCardSecurityCode:
            return StrL("cc-csc");
        case InputContentType::CreditCardExpiration:
            return StrL("cc-exp");
        case InputContentType::CreditCardExpirationMonth:
            return StrL("cc-exp-month");
        case InputContentType::CreditCardExpirationYear:
            return StrL("cc-exp-year");
        case InputContentType::CreditCardType:
            return StrL("cc-type");
        case InputContentType::Username:
            return StrL("username");
        case InputContentType::Password:
            return StrL("password");
        case InputContentType::NewPassword:
            return StrL("new-password");
        case InputContentType::OneTimeCode:
            return StrL("one-time-code");
        case InputContentType::ShipmentTrackingNumber:
            return StrL("shipment-tracking-number");
        case InputContentType::FlightNumber:
            return StrL("flight-number");
        case InputContentType::DateTime:
            return StrL("date-time");
        case InputContentType::Birthdate:
            return StrL("bday");
        case InputContentType::BirthdateDay:
            return StrL("bday-day");
        case InputContentType::BirthdateMonth:
            return StrL("bday-month");
        case InputContentType::BirthdateYear:
            return StrL("bday-year");
        case InputContentType::CellularEid:
        case InputContentType::CellularImei:
            return {};
    }
    return {};
}

El* Input::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // Rust's Input is the field and nothing else — `.flex().size_full()` — and
    // a label above it is the caller's own div. `Input::Label` is this tree's
    // addition, so the column only exists when one was asked for, and it is
    // the column that then carries the width. A field wrapped in a box that
    // does not carry it shrinks to its text the moment it is laid out in a
    // row rather than a column.
    El* col = label.s ? Div(a)->FlexCol()->Gap(4)->W(width) : nullptr;
    if (col) {
        col->Child(TextEl(a, label)->Font(12)->Fg(th.foreground));
    }
    bool inputFocused = state && state->focused && !disabled;
    bool focused = InputFrameFocused(cx, state, disabled);
    if (state) {
        bool editable = !disabled && !readonly && !state->readonly;
        state->pasteHandler = editable ? onPaste : nullptr;
        state->pasteHandlerData = state->pasteHandler ? onPasteData : nullptr;
        bool secret =
            hasContentType && InputContentIsSecret(hasContentType, contentType);
        InputSetTokenPresentation(
            state, tokenRenderer ? tokenRenderer : &DefaultInputTokenRender,
            tokenRendererUser, tokenClick, tokenClickUser, secret);
    }
    if (inputFocused && !readonly && !(state && state->readonly)) {
        WindowSetTextContentType(
            cx->win, InputNativeContentType(hasContentType, contentType));
    }
    float h = 0, padX = 0, padY = 0, font = 0;
    InputSizeMetrics(size, &h, &padX, &padY, &font);
    InputEditorStyle editor;
    editor.foreground = hasTextColor ? textColor : th.foreground;
    editor.mutedForeground = th.mutedFg;
    editor.caret = th.caret;
    editor.selection = RgbaOpacity(th.selection, 0.4f);
    editor.fontSize = font;
    editor.mask = masked;
    editor.align = align == InputAlign::Center  ? 1
                   : align == InputAlign::Right ? 2
                                                : 0;
    if (disabled) {
        editor.foreground = th.mutedFg;
    }
    bool password = masked || (state && state->masked) ||
                    InputContentIsSecret(hasContentType, contentType);
    AccessibilityRole role =
        hasAccessibilityRole
            ? accessibilityRole
            : InputAccessibilityRole(hasContentType, contentType);
    El* field = InputBase::New(cx, id, !disabled,
                               password && !hasAccessibilityRole
                                   ? AccessibilityRole::PasswordInput
                                   : role)
                    ->BindInput(disabled ? nullptr : state)
                    ->FlexRow()
                    ->W(col ? kFill : width)
                    ->H(h)
                    ->PadX(padX)
                    ->PadY(padY)
                    ->Gap(kInputGap)
                    ->ItemsCenter()
                    // The editor paints its own line into the field's box;
                    // a value wider than the field scrolls under it rather
                    // than spilling out past whatever is next to it.
                    ->ClipX();
    if (!disabled) InputTrackFrameFocus(cx, field, state);
    if (state) {
        if (state->placeholder.s) {
            field->AriaPlaceholder(state->placeholder);
        }
        if (!password) {
            field->AriaValue(InputValue(state));
        }
    }
    if (accessibilityId.s) {
        field->AccessibilityId(accessibilityId);
    }
    if (ariaLabel.s) {
        field->AriaLabel(ariaLabel);
    } else if (label.s) {
        field->AriaLabel(label);
    } else if (state && state->placeholder.s) {
        field->AriaLabel(state->placeholder);
    }
    // input.rs gates the whole focus appearance on `appearance`: a field with
    // none of its own is one somebody else has framed — a NumberInput's
    // editor, sitting inside a frame that shows the focus for it — and a ring
    // around the editor as well would say it twice.
    field->FocusRing(appearance && focusRing);
    if (appearance) {
        field->Radius(th.radius)
            ->Bg(disabled ? th.muted : th.inputBg)
            // The other half of focus_ring_style: the border takes the ring
            // colour, and the ring itself is painted outside it.
            ->Border(1, focused ? th.ring : th.inputBorder);
        // input.rs: the disabled field is faded as a whole, prefix, suffix
        // and text together.
        if (disabled) {
            field->Opacity(0.5f);
        }
    }
    // input.rs: the prefix is one more child of the padded row, so the
    // field's own input_px is the space to its left.
    if (prefix) {
        field->Child(prefix);
    }
    bool hasValue = state && len(InputValue(state)) > 0;
    bool trailing = suffix || (cleanable && hasValue) || maskToggle;
    if (prefix || trailing) {
        field->Child(
            Div(a)->Flex1()->Child(gpui::Input::New(cx, state, editor)));
    } else {
        field->Child(gpui::Input::New(cx, state, editor));
    }
    if (maskToggle) {
        field->Child(Button::New(cx, StrL("mask"))
                         ->Text()
                         ->WithSize(UiSize::XSmall)
                         ->Icon(IconName::Eye)
                         ->TabStop(false)
                         ->OnClick(onToggleMask)
                         ->IntoEl()
                         ->StopClick());
    }
    if (cleanable && hasValue && !disabled) {
        // clear_button.rs: `.tab_stop(false)`. The X belongs to the field, and
        // Tab should move to the next field rather than stop at it.
        field->Child(Button::New(cx, StrL("clean"))
                         ->Text()
                         ->WithSize(UiSize::XSmall)
                         ->Icon(IconName::X)
                         ->TabStop(false)
                         ->OnClick(onClear)
                         ->IntoEl()
                         ->StopClick());
    }
    if (suffix) {
        field->Child(suffix);
    }
    if (!disabled) {
        if (onFocus.IsValid()) {
            field->OnClick(onFocus);
        } else if (onChange.IsValid()) {
            field->OnClick(onChange);
        }
    }
    BindInputContextMenu(cx, field, id, state, disabled, contextMenu,
                         contextMenuData);
    if (!col) {
        refiner.Apply(field);
        return field;
    }
    col->Child(field);
    refiner.Apply(col);
    return col;
}

Textarea* Textarea::New(Ctx* cx, Str id, InputState* state) {
    Arena* a = cx->a;
    Textarea* t = ArenaNew<Textarea>(a);
    t->a = a;
    t->cx = cx;
    t->id = id;
    t->state = state;
    return t;
}
Textarea* Textarea::Rows(int n) {
    rows = n;
    return this;
}
Textarea* Textarea::WithSize(UiSize s) {
    size = s;
    return this;
}
Textarea* Textarea::H(float px) {
    height = px;
    return this;
}
Textarea* Textarea::SoftWrap(bool v) {
    softWrap = v;
    return this;
}
Textarea* Textarea::Role(AccessibilityRole value) {
    accessibilityRole = value;
    return this;
}
Textarea* Textarea::AriaLabel(Str value) {
    ariaLabel = value;
    return this;
}
Textarea* Textarea::AccessibilityId(Str value) {
    accessibilityId = value;
    return this;
}
Textarea* Textarea::Disabled(bool v) {
    disabled = v;
    return this;
}
Textarea* Textarea::Readonly(bool v) {
    readonly = v;
    return this;
}
Textarea* Textarea::Appearance(bool v) {
    appearance = v;
    return this;
}
Textarea* Textarea::Bordered(bool v) {
    bordered = v;
    return this;
}
Textarea* Textarea::FocusRing(bool v) {
    focusRing = v;
    return this;
}
Textarea* Textarea::OnFocus(Listener fn) {
    onFocus = fn;
    return this;
}

Textarea* Textarea::Token(InlineTokenRenderer fn, void* user) {
    tokenRenderer = fn;
    tokenRendererUser = user;
    return this;
}

Textarea* Textarea::OnTokenClick(InlineTokenClickListener fn, void* user) {
    tokenClick = fn;
    tokenClickUser = user;
    return this;
}

Textarea* Textarea::OnPaste(InputPasteFn fn, void* data) {
    onPaste = fn;
    onPasteData = data;
    return this;
}

El* Textarea::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    bool focused = InputFrameFocused(cx, state, disabled);
    // The Input a Textarea wraps: a multi-line field takes input_py above and
    // below the rows and input_px beside them, and input_text_size for them.
    float inputH = 0, padX = 0, padY = 0, font = 0;
    InputSizeMetrics(size, &inputH, &padX, &padY, &font);
    InputEditorStyle editor;
    editor.foreground = th.foreground;
    editor.mutedForeground = th.mutedFg;
    editor.caret = th.caret;
    editor.selection = RgbaOpacity(th.selection, 0.4f);
    editor.fontSize = font;
    if (state) {
        bool editable =
            !disabled && !readonly && !state->disabled && !state->readonly;
        state->pasteHandler = editable ? onPaste : nullptr;
        state->pasteHandlerData = state->pasteHandler ? onPasteData : nullptr;
        InputSetTokenPresentation(
            state, tokenRenderer ? tokenRenderer : &DefaultInputTokenRender,
            tokenRendererUser, tokenClick, tokenClickUser, false);
        state->softWrap = softWrap;
        if (rows > 0) {
            LayoutModeSetRows(&state->mode, rows);
        }
    }
    // A row is one 1.25rem line box, like the single-line input; the border
    // sits outside the padded content, as in GPUI.
    //
    // Without a Rows() here, the state's mode decides as element.rs
    // request_layout does: an auto-grow textarea is at least its current
    // rows (capped at max_rows) tall, and any other multi-line one at least
    // one line — PlainText's own `rows` sizes nothing upstream.
    int shownRows = rows;
    if (shownRows <= 0) {
        shownRows = 1;
        if (state && state->mode.kind == LayoutModeKind::AutoGrow) {
            int grown = LayoutModeRows(state->mode);
            int maxRows = state->mode.maxRows;
            shownRows = maxRows > 0 && maxRows < grown ? maxRows : grown;
        }
    }
    // `.h(px(..))` or `.h(relative(1.))`: a caller that gives the editor a
    // height means it, and kFill is the relative one — the inspector's pane
    // is what asks for it. Everything else is `rows` line boxes.
    float h = (height > 0 || height == kFill)
                  ? height
                  : (float)shownRows * 20.f + 2 * padY + 2;
    // The rows are virtualized against this, and paint only learns it after
    // the frame it measured — so the first frame of a long document would
    // build every row of it. The builder knows the box it is about to make,
    // so it says so here and paint refreshes it.
    if (state && h > 0) {
        state->viewH = h - 2 * padY;
    }
    bool interactive = state && !state->disabled && !disabled;
    El* box = InputBase::New(cx, id, interactive, accessibilityRole)
                  ->BindInput(interactive ? state : nullptr)
                  ->W(kFill)
                  ->H(h)
                  ->PadX(padX)
                  ->PadY(padY)
                  ->ClipY()
                  // scroll_handle: the rows slide under the box as the caret
                  // moves, and the wheel moves them too.
                  ->ScrollY(state ? state->scrollY : 0)
                  // The bar drag finds the box again by this id in the
                  // frame that is on screen; the box is already named, so
                  // its place in the tree is what it is found by.
                  ->ScrollFromPath()
                  ->Child(gpui::Textarea::New(cx, state, editor));
    if (interactive) InputTrackFrameFocus(cx, box, state);
    if (accessibilityId.s) {
        box->AccessibilityId(accessibilityId);
    }
    if (appearance) {
        box->Radius(th.radius)->Bg(th.inputBg);
        if (bordered) {
            box->Border(1, focused ? th.ring : th.inputBorder)
                ->FocusRing(focusRing);
        }
    }
    if (state) {
        box->AriaValue(InputValue(state));
        if (state->placeholder.s) {
            box->AriaPlaceholder(state->placeholder);
            if (!ariaLabel.s) {
                box->AriaLabel(state->placeholder);
            }
        }
    }
    if (ariaLabel.s) {
        box->AriaLabel(ariaLabel);
    }
    BindInputContextMenu(cx, box, id, state, disabled, contextMenu,
                         contextMenuData);
    // `Scrollbar::new(..)` against `Scrollbar::vertical(..)`: a field that
    // wraps has nothing to reach sideways, and one that does not is as wide
    // as its longest row and scrolls to the end of it.
    if (!softWrap) {
        box->ScrollX(state ? state->scrollX : 0);
    }
    if (onFocus.IsValid()) {
        box->OnClick(onFocus);
    }
    refiner.Apply(box);
    return box;
}

NumberInput* NumberInput::New(Ctx* cx, InputState* state) {
    Arena* a = cx->a;
    NumberInput* n = ArenaNew<NumberInput>(a);
    n->a = a;
    n->cx = cx;
    n->state = state;
    return n;
}
NumberInput* NumberInput::New(Ctx* cx, Str id, InputState* state) {
    NumberInput* n = New(cx, state);
    n->id = id;
    return n;
}
NumberInput* NumberInput::WithSize(UiSize s) {
    size = s;
    return this;
}
NumberInput* NumberInput::Disabled(bool v) {
    disabled = v;
    return this;
}
NumberInput* NumberInput::Appearance(bool v) {
    appearance = v;
    return this;
}
NumberInput* NumberInput::FocusRing(bool v) {
    focusRing = v;
    return this;
}
NumberInput* NumberInput::Suffix(El* el) {
    suffix = el;
    return this;
}
NumberInput* NumberInput::Bg(Background c) {
    bg = c;
    hasBg = true;
    return this;
}
NumberInput* NumberInput::TextColor(Rgba c) {
    textColor = c;
    hasTextColor = true;
    return this;
}
NumberInput* NumberInput::Step(double value) {
    numberStep = NumberStep::Fixed(value);
    hasNumberStep = true;
    return this;
}
NumberInput* NumberInput::StepBy(NumberStepByValueFn fn, intptr_t arg) {
    numberStep = NumberStep::ByValue(fn, arg);
    hasNumberStep = true;
    return this;
}
NumberInput* NumberInput::NoStep() {
    hasNumberStep = false;
    return this;
}
NumberInput* NumberInput::Min(double value) {
    min = value;
    hasMin = true;
    return this;
}
NumberInput* NumberInput::Max(double value) {
    max = value;
    hasMax = true;
    return this;
}
NumberInput* NumberInput::OnStep(Listener fn) {
    onStep = fn;
    return this;
}
NumberInput* NumberInput::OnFocus(Listener fn) {
    onFocus = fn;
    return this;
}
NumberInput* NumberInput::W(float v) {
    width = v;
    return this;
}
NumberInput* NumberInput::OnInc(Listener fn) {
    onInc = fn;
    return this;
}
NumberInput* NumberInput::OnDec(Listener fn) {
    onDec = fn;
    return this;
}
El* NumberInput::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    float h = 32, btn = 32, font = 14;
    if (size == UiSize::Large) {
        h = 44;
        btn = 32;
        font = 16;
    } else if (size == UiSize::Small) {
        h = 24;
        btn = 24;
    } else if (size == UiSize::XSmall) {
        h = 20;
        btn = 24;
        font = 12;
    }
    Rgba border = disabled ? RgbaOpacity(th.inputBorder, 0.5f) : th.inputBorder;
    // number_input.rs tints the frame on focus, the same as an Input's own
    // border: the editor inside wears no appearance of its own, so the frame
    // is the only thing that can say the editor has the keyboard.
    bool focused = state && state->focused && !disabled;
    if (state) {
        // InputState owns the range semantics: stepping and accessibility use
        // this frame's policy, while blur can clamp after the element tree is
        // no longer being consulted.
        state->numberHasMin = hasMin;
        state->numberMin = min;
        state->numberHasMax = hasMax;
        state->numberMax = max;
    }
    Str base = id.s ? id : StrL("number");
    const NumberStep* policy = hasNumberStep ? &numberStep : nullptr;
    Func0 decDirect =
        NumberInputStepCallback(cx, state, StepAction::Decrement, policy,
                                hasMin, min, hasMax, max, disabled, onStep);
    Func0 incDirect =
        NumberInputStepCallback(cx, state, StepAction::Increment, policy,
                                hasMin, min, hasMax, max, disabled, onStep);
    // The visual frame is the UI layer; BaseNumberInput below owns the
    // spinbutton semantics and fixed three-part structure.
    El* frame = Div(a)->FlexRow()->W(width)->H(h);
    if (appearance) {
        frame->Radius(th.radius)
            ->Bg(hasBg ? bg
                       : Background(disabled ? th.tokens.muted.color
                                             : th.inputBg))
            ->Border(1, focused && focusRing ? th.ring : border);
    } else if (hasBg) {
        frame->Radius(th.radius)->Bg(bg);
    }
    // The disabled frame is faded as a whole, the way an Input's is: the
    // value, the two step buttons and the suffix all dim together.
    if (disabled) {
        frame->Opacity(0.5f);
    }
    // The step buttons are transparent until hovered, and fill the frame.
    // `rounded_tl`/`rounded_bl` on the one and `rounded_tr`/`rounded_br` on
    // the other: only the outer corners are rounded, to follow the frame. The
    // border is a hairline inside the frame's own, so the button's radius is
    // the frame's less that.
    float stepR = appearance ? th.radius - 1.f : th.radius;
    if (stepR < 0) {
        stepR = 0;
    }
    Rgba stepFg = disabled ? RgbaOpacity(th.secondaryFg, 0.5f) : th.secondaryFg;
    // Both are gpui_base::Buttons that decline focus: pressing one must leave
    // the editor focused, or the frame's ring flickers on every click. That is
    // what number_input.rs pins with
    // `pressing_a_step_button_never_takes_focus_off_the_editor`.
    El* dec = gpui::Button::New(cx, StrL("decrement"), disabled, onDec, false)
                  ->AriaLabel(Tr("Input.Decrement"))
                  ->W(btn)
                  ->H(kFill)
                  ->Corners(stepR, 0, 0, stepR)
                  ->ItemsCenter()
                  ->JustifyCenter()
                  ->Child(IconEl(a, IconName::Minus, font)->Fg(stepFg));
    El* inc = gpui::Button::New(cx, StrL("increment"), disabled, onInc, false)
                  ->AriaLabel(Tr("Input.Increment"))
                  ->W(btn)
                  ->H(kFill)
                  ->Corners(0, stepR, stepR, 0)
                  ->ItemsCenter()
                  ->JustifyCenter()
                  ->Child(IconEl(a, IconName::Plus, font)->Fg(stepFg));
    if (!disabled && !onDec.IsValid()) {
        dec->OnClick(decDirect);
    }
    if (!disabled && !onInc.IsValid()) {
        inc->OnClick(incDirect);
    }
    if (!disabled) {
        dec->HoverBg(RgbaOpacity(th.inputBorder, 0.4f));
        inc->HoverBg(RgbaOpacity(th.inputBorder, 0.4f));
    }
    // The editor sits between them, centered and without its own frame.
    Input* editor = Input::New(cx, StrL("input"), state)
                        ->WithSize(size)
                        ->Align(InputAlign::Center)
                        ->Appearance(false)
                        ->Disabled(disabled)
                        ->OnFocus(onFocus);
    if (hasTextColor) {
        editor->TextColor(textColor);
    }
    if (suffix) {
        editor->Suffix(suffix);
    }
    El* content = gpui::NumberInput::Compose(
        cx, base, state, disabled, dec,
        Div(a)->H(kFill)->Child(editor->IntoEl()), inc);
    if (onInc.IsValid()) {
        content->OnAccessibilityIncrement(onInc);
    } else {
        content->OnAccessibilityIncrement(incDirect);
    }
    if (onDec.IsValid()) {
        content->OnAccessibilityDecrement(onDec);
    } else {
        content->OnAccessibilityDecrement(decDirect);
    }
    if (state) {
        Str inputValue = InputValue(state);
        content->AriaValue(inputValue);
        double numeric = 0;
        if (NumberParseValue(inputValue, &numeric)) {
            content->AriaNumericValue((float)numeric);
        }
    }
    frame->Child(content);
    return frame;
}

OtpInput* OtpInput::New(Ctx* cx, const char* value, int len) {
    Arena* a = cx->a;
    OtpInput* o = ArenaNew<OtpInput>(a);
    o->a = a;
    o->cx = cx;
    o->value = value;
    o->len = len;
    return o;
}
OtpInput* OtpInput::New(Ctx* cx, Str id, Entity<OtpState> state) {
    OtpInput* o = New(cx, nullptr, 0);
    o->id = id;
    o->state = state;
    if (OtpState* s = state.Get(cx)) {
        o->value = OtpValue(s).s;
        o->len = s->len;
        o->slots = s->length;
        o->masked = s->masked;
        o->disabled = s->disabled;
    }
    return o;
}

OtpInput* OtpInput::Id(Str s) {
    id = s;
    return this;
}
OtpInput* OtpInput::Slots(int n) {
    slots = n;
    return this;
}
OtpInput* OtpInput::Groups(int n) {
    groups = n;
    return this;
}
OtpInput* OtpInput::Masked(bool v) {
    masked = v;
    return this;
}
OtpInput* OtpInput::Disabled(bool v) {
    disabled = v;
    return this;
}
OtpInput* OtpInput::FocusRing(bool v) {
    focusRing = v;
    return this;
}
OtpInput* OtpInput::WithSize(UiSize s) {
    size = s;
    return this;
}
OtpInput* OtpInput::CellSize(float px) {
    cellPx = px;
    return this;
}
OtpInput* OtpInput::OnFocus(Listener fn) {
    onFocus = fn;
    return this;
}

El* OtpInput::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // otp_input.rs: input_text_size, the Input/Select ladder, and half the
    // cell for a custom Size. The masked icon follows the same size.
    float cell = 32, text = UiInputFontPx(size);
    if (cellPx > 0) {
        cell = cellPx;
        text = cellPx * 0.5f;
    } else if (size == UiSize::Large) {
        cell = 44;
    } else if (size == UiSize::Small || size == UiSize::XSmall) {
        cell = 24;
    }
    int nGroups = groups < 1 ? 1 : (groups > slots ? slots : groups);
    int per = (slots + nGroups - 1) / nGroups;
    if (per < 1) {
        per = 1;
    }
    Rgba fg = disabled ? th.mutedFg : th.secondaryFg;
    // gap_5 between the groups, gap_1 inside one.
    Str rowId = id.s ? id : StrL("otp");
    El* row = (state.IsValid() ? gpui::OtpInput::New(cx, rowId, state)
                               : gpui::OtpInput::New(cx, rowId))
                  ->FocusRing(focusRing)
                  ->FlexRow()
                  ->ItemsCenter()
                  ->Gap(20);
    if (onFocus.IsValid() && !disabled) {
        row->OnClick(onFocus);
    }
    El* group = nullptr;
    for (int i = 0; i < slots; i++) {
        if (i % per == 0) {
            group = Div(a)->FlexRow()->ItemsCenter()->Gap(4);
            row->Child(group);
        }
        El* box = Div(a)
                      ->W(cell)
                      ->H(cell)
                      ->ItemsCenter()
                      ->JustifyCenter()
                      ->Radius(th.radius)
                      ->Bg(disabled ? th.muted : th.inputBg)
                      ->Border(1, th.inputBorder);
        // The caret sits in the first empty cell of a focused field, which is
        // where the next digit lands.
        OtpState* st = state.Get(cx);
        if (st && st->focused && i == st->len && !disabled &&
            OtpCursorVisible(st, cx->app)) {
            box->Border(1, th.ring);
        }
        if (value && i < len) {
            if (masked) {
                box->Child(IconEl(a, IconName::Asterisk, text)->Fg(fg));
            } else {
                Str ch(value + i, 1);
                box->Child(TextEl(a, StrDup(a, ch))
                               ->Font(text)
                               ->LineHeight(1.f)
                               ->Fg(fg));
            }
        }
        group->Child(box);
    }
    return row;
}

// --- SearchPanel, crates/ui/src/input/search.rs ---------------------------

// The bar's own two fields, and what it needs to remember between frames.
// The session itself lives on the field being searched, so closing the bar
// and opening it again keeps the query.
struct SearchPanelState {
    InputState query;
    InputState replacement;
    // The field the bar is over, pointed at as the bar builds. It outlives
    // the frame — a caller's member, or an entity's — so the handlers reach
    // it between frames the way Rust's WeakEntity does.
    InputState* target = nullptr;
    // The box the query field was laid out in, which the replacement field
    // below it is given so the two line up. Rust reads it in on_prepaint,
    // and this is a frame behind for the same reason any laid-out box is.
    Bounds queryBounds = {};
    bool seeded = false;
    // Whether the session was open on the last build, so opening it puts the
    // caret in the query field exactly once.
    bool wasOpen = false;
    uint64_t activationRevision = 0;

    static void OnQueryEvent(SearchPanelState* self, Ctx* cx,
                             const InputEvent* ev);
    static void OnReplacementEvent(SearchPanelState* self, Ctx* cx,
                                   const InputEvent* ev);
    static void OnToggleCase(SearchPanelState* self, Ctx* cx,
                             const ClickEvent*);
    static void OnToggleReplace(SearchPanelState* self, Ctx* cx,
                                const ClickEvent*);
    static void OnPrev(SearchPanelState* self, Ctx* cx, const ClickEvent*);
    static void OnNext(SearchPanelState* self, Ctx* cx, const ClickEvent*);
    static void OnClose(SearchPanelState* self, Ctx* cx, const ClickEvent*);
    static void OnReplaceOne(SearchPanelState* self, Ctx* cx,
                             const ClickEvent*);
    static void OnReplaceAll(SearchPanelState* self, Ctx* cx,
                             const ClickEvent*);
    static void OnKey(SearchPanelState* self, Ctx* cx, const KeyEvent* ev);
};

// The bar closes and the field it was over takes the caret back, which is
// what makes escape put you where you were.
static void PanelClose(SearchPanelState* self, Ctx* cx) {
    if (!self->target) {
        return;
    }
    InputCloseSearch(self->target, cx->app, cx->win);
    InputFocus(self->target, cx->app, cx->win);
}

void SearchPanelState::OnQueryEvent(SearchPanelState* self, Ctx* cx,
                                    const InputEvent* ev) {
    if (!self->target) {
        return;
    }
    if (ev->kind == InputEventKind::Change) {
        InputSetSearchQuery(self->target, cx->app, cx->win,
                            InputValue(&self->query),
                            self->target->search.caseInsensitive);
        return;
    }
    if (ev->kind == InputEventKind::PressEnter) {
        // on_action_enter: enter walks forward, shift-enter back.
        if (ev->shift) {
            InputSearchPrev(self->target, cx->app, cx->win, nullptr);
        } else {
            InputSearchNext(self->target, cx->app, cx->win, nullptr);
        }
    }
}

void SearchPanelState::OnReplacementEvent(SearchPanelState* self, Ctx* cx,
                                          const InputEvent* ev) {
    if (!self->target) {
        return;
    }
    if (ev->kind == InputEventKind::Change) {
        SearchSessionSetReplacement(&self->target->search,
                                    InputValue(&self->replacement));
        Notify(cx);
        return;
    }
    if (ev->kind == InputEventKind::PressEnter) {
        InputSearchReplaceOne(self->target, cx->app, cx->win,
                              InputValue(&self->replacement));
    }
}

void SearchPanelState::OnToggleCase(SearchPanelState* self, Ctx* cx,
                                    const ClickEvent*) {
    if (!self->target) {
        return;
    }
    SearchSession* ss = &self->target->search;
    InputSetSearchQuery(self->target, cx->app, cx->win,
                        InputValue(&self->query), !ss->caseInsensitive);
}

void SearchPanelState::OnToggleReplace(SearchPanelState* self, Ctx* cx,
                                       const ClickEvent*) {
    if (self->target) {
        InputSetSearchReplaceMode(self->target, cx->app, cx->win,
                                  !self->target->search.replaceMode);
    }
}

void SearchPanelState::OnPrev(SearchPanelState* self, Ctx* cx,
                              const ClickEvent*) {
    if (self->target) {
        InputSearchPrev(self->target, cx->app, cx->win, nullptr);
    }
}

void SearchPanelState::OnNext(SearchPanelState* self, Ctx* cx,
                              const ClickEvent*) {
    if (self->target) {
        InputSearchNext(self->target, cx->app, cx->win, nullptr);
    }
}

void SearchPanelState::OnClose(SearchPanelState* self, Ctx* cx,
                               const ClickEvent*) {
    PanelClose(self, cx);
}

void SearchPanelState::OnReplaceOne(SearchPanelState* self, Ctx* cx,
                                    const ClickEvent*) {
    if (self->target) {
        InputSearchReplaceOne(self->target, cx->app, cx->win,
                              InputValue(&self->replacement));
    }
}

void SearchPanelState::OnReplaceAll(SearchPanelState* self, Ctx* cx,
                                    const ClickEvent*) {
    if (self->target) {
        InputSearchReplaceAll(self->target, cx->app, cx->win,
                              InputValue(&self->replacement));
    }
}

void SearchPanelState::OnKey(SearchPanelState* self, Ctx* cx,
                             const KeyEvent* ev) {
    if (!ev->down || !self->target) {
        return;
    }
    if (ev->vk == KeyEscape) {
        // on_action_escape.
        PanelClose(self, cx);
        WindowStopPropagation(cx);
        return;
    }
    if (ev->vk == KeyTab && self->target->search.replaceMode) {
        // on_action_tab / on_action_tab_prev: the two fields, and nothing
        // outside them — the focus ring never sees this tab.
        InputState* to =
            self->query.focused ? &self->replacement : &self->query;
        InputFocus(to, cx->app, cx->win);
        InputSelectAll(to, cx->app, cx->win);
        WindowStopPropagation(cx);
    }
}

SearchPanel* SearchPanel::New(Ctx* cx, Str id, InputState* target) {
    Arena* a = cx->a;
    SearchPanel* p = ArenaNew<SearchPanel>(a);
    p->a = a;
    p->cx = cx;
    p->id = id;
    p->target = target;
    return p;
}

El* SearchPanel::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // The state is the window's, keyed by the bar's id, so two editors on one
    // page each get their own.
    Entity<SearchPanelState> ent =
        ElementStateEntity<SearchPanelState>(cx, id, StrL("search-panel"));
    SearchPanelState* st = ent.Get(cx);
    if (!st || !target) {
        return Div(a);
    }
    st->target = target;
    if (!st->seeded) {
        st->seeded = true;
        st->query.onChange = ListenTo(ent, &SearchPanelState::OnQueryEvent);
        st->replacement
            .onChange = ListenTo(ent, &SearchPanelState::OnReplacementEvent);
    }
    SearchSession* ss = &target->search;
    if (!ss->open) {
        st->wasOpen = false;
        return Div(a);
    }
    // Opening it puts the query in the field and picks it out, which is what
    // makes typing over it the next search rather than an edit of the last.
    if (!st->wasOpen ||
        st->activationRevision != target->searchActivationRevision) {
        st->wasOpen = true;
        st->activationRevision = target->searchActivationRevision;
        InputSetValue(&st->query, ss->query);
        InputFocus(&st->query, cx->app, cx->win);
        InputSelectAll(&st->query, cx->app, cx->win);
    }
    bool hasMatches = !SearchMatcherIsEmpty(&ss->matcher);
    bool allowReplace = InputIsReplaceable(target);
    if (!allowReplace) {
        ss->replaceMode = false;
    }

    // `v_flex().id("search-panel")`: the bar names itself, so the buttons and
    // the two fields under it are named by their place in it rather than by
    // the bar's id spelled into each one. The id is the caller's, standing in
    // for the fold upstream gets from the tree the panel is rendered in.
    El* panel = Div(a)
                    ->Id(id)
                    ->FlexCol()
                    ->W(kFill)
                    ->PadY(8)
                    ->PadX(12)
                    ->Gap(4)
                    ->Bg(th.tokens.popover)
                    ->BorderB(1, th.border)
                    ->Radius(th.radius * 0.5f)
                    ->OnKeyDown(ListenTo(ent, &SearchPanelState::OnKey));

    El* row = Div(a)->FlexRow()->W(kFill)->Gap(8)->ItemsCenter();
    El* caseBtn = Button::New(cx, StrL("case-insensitive"))
                      ->Text()
                      ->Compact()
                      ->WithSize(UiSize::XSmall)
                      ->Icon(IconName::CaseSensitive)
                      ->Selected(!ss->caseInsensitive)
                      ->OnClick(ListenTo(ent, &SearchPanelState::OnToggleCase))
                      ->IntoEl();
    El* queryBox = Div(a)->FlexRow()->Flex1()->Gap(4);
    queryBox->Child(Input::New(cx, StrL("q"), &st->query)
                        ->WithSize(UiSize::Small)
                        ->FocusRing(false)
                        ->Suffix(caseBtn)
                        ->IntoEl()
                        ->BoundsOut(&st->queryBounds));
    row->Child(queryBox);
    if (allowReplace) {
        row->Child(
            Button::New(cx, StrL("replace-mode"))
                ->Ghost()
                ->WithSize(UiSize::XSmall)
                ->Icon(IconName::Replace)
                ->Selected(ss->replaceMode)
                ->OnClick(ListenTo(ent, &SearchPanelState::OnToggleReplace))
                ->IntoEl());
    }
    row->Child(Button::New(cx, StrL("prev"))
                   ->Ghost()
                   ->WithSize(UiSize::XSmall)
                   ->Icon(IconName::ChevronLeft)
                   ->Disabled(!hasMatches)
                   ->OnClick(ListenTo(ent, &SearchPanelState::OnPrev))
                   ->IntoEl());
    row->Child(Button::New(cx, StrL("next"))
                   ->Ghost()
                   ->WithSize(UiSize::XSmall)
                   ->Icon(IconName::ChevronRight)
                   ->Disabled(!hasMatches)
                   ->OnClick(ListenTo(ent, &SearchPanelState::OnNext))
                   ->IntoEl());
    row->Child(TextEl(a, SearchMatcherLabel(a, &ss->matcher))
                   ->Font(14)
                   ->MinW(64)
                   ->Fg(hasMatches ? th.foreground : th.mutedFg));
    // div().w_7(): the gap that keeps the close button off the counter.
    row->Child(Div(a)->W(28));
    row->Child(Button::New(cx, StrL("close"))
                   ->Ghost()
                   ->WithSize(UiSize::XSmall)
                   ->Icon(IconName::WindowClose)
                   ->OnClick(ListenTo(ent, &SearchPanelState::OnClose))
                   ->IntoEl());
    panel->Child(row);

    if (ss->replaceMode && allowReplace) {
        El* row2 = Div(a)->FlexRow()->W(kFill)->Gap(8)->ItemsCenter();
        // The replacement field is as wide as the query field above it,
        // which is what Rust's `input_width` is for. Zero on the first frame,
        // before the query field has been laid out; growing stands in.
        float w = st->queryBounds.w > 1 ? st->queryBounds.w : kFill;
        El* rep = Input::New(cx, StrL("r"), &st->replacement)
                      ->WithSize(UiSize::Small)
                      ->FocusRing(false)
                      ->W(w)
                      ->IntoEl();
        row2->Child(rep);
        row2->Child(
            Button::New(cx, StrL("replace-one"))
                ->WithSize(UiSize::Small)
                ->Label(Tr("Input.Replace"))
                ->Disabled(!hasMatches)
                ->OnClick(ListenTo(ent, &SearchPanelState::OnReplaceOne))
                ->IntoEl());
        row2->Child(
            Button::New(cx, StrL("replace-all"))
                ->WithSize(UiSize::Small)
                ->Label(Tr("Input.Replace All"))
                ->Disabled(!hasMatches)
                ->OnClick(ListenTo(ent, &SearchPanelState::OnReplaceAll))
                ->IntoEl());
        panel->Child(row2);
    }
    return panel;
}

// ─── InputGroup, crates/ui/src/input/group.rs ─────────────────────────────

InputGroupAppearance InputGroupAppearance::New(const Theme& th, bool focused,
                                               bool disabled, bool invalid) {
    InputGroupAppearance out;
    bool dark = th.mode == ThemeMode::Dark;
    // `theme.input` is the input's border colour, which is what the
    // surface is tinted with.
    if (disabled) {
        out.background = RgbaOpacity(th.inputBorder, dark ? 0.8f : 0.5f);
    } else if (dark) {
        out.background = RgbaOpacity(th.inputBorder, 0.3f);
    } else {
        out.background = th.transparent;
    }
    // Validation remains visible when editing is disabled. Focus alone never
    // reactivates a disabled control, and never replaces its validation color.
    if (invalid) {
        out.border = th.danger;
        out.ring = RgbaOpacity(th.danger, dark ? 0.4f : 0.2f);
        out.hasRing = true;
    } else if (focused && !disabled) {
        out.border = th.ring;
        out.ring = RgbaOpacity(th.ring, 0.5f);
        out.hasRing = true;
    } else {
        out.border = th.inputBorder;
    }
    return out;
}

InputGroupButton* InputGroupButton::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    InputGroupButton* b = ArenaNew<InputGroupButton>(a);
    b->a = a;
    b->cx = cx;
    b->button = Button::New(cx, id)->Ghost();
    return b;
}

InputGroupButton* InputGroupButton::Label(Str s) {
    if (button) {
        button->Label(s);
    }
    return this;
}

InputGroupButton* InputGroupButton::Icon(IconName n) {
    if (button) {
        button->Icon(n);
    }
    return this;
}

InputGroupButton* InputGroupButton::Icon(Str path) {
    if (button && path) {
        button->Icon(ButtonIcon::New(cx, Icon::Empty(cx)->Path(path)));
    }
    return this;
}

InputGroupButton* InputGroupButton::Tooltip(Str s) {
    if (button) {
        button->Tooltip(s);
    }
    return this;
}

InputGroupButton* InputGroupButton::AriaLabel(Str s) {
    if (button) {
        button->AccessibilityLabel(s);
    }
    return this;
}

InputGroupButton* InputGroupButton::WithSize(UiSize s) {
    size = s;
    return this;
}

InputGroupButton* InputGroupButton::WithVariant(ButtonVariant v) {
    if (button) {
        button->WithVariant(v);
    }
    return this;
}

InputGroupButton* InputGroupButton::Disabled(bool v) {
    if (button) {
        button->Disabled(v);
    }
    return this;
}

InputGroupButton* InputGroupButton::Loading(bool v) {
    if (button) {
        button->Loading(v);
    }
    return this;
}

InputGroupButton* InputGroupButton::Outline() {
    if (button) {
        button->Outline();
    }
    return this;
}

InputGroupButton* InputGroupButton::OnClick(Listener fn) {
    if (button) {
        button->OnClick(fn);
    }
    return this;
}

InputGroupButton* InputGroupButton::Child(El* el) {
    if (button && el) {
        button->Child(el);
    }
    return this;
}

El* InputGroupButton::IntoEl() {
    return RenderInGroup(false);
}

El* InputGroupButton::RenderInGroup(bool disabled) {
    if (!button) {
        return Div(a);
    }
    const Theme& th = ThemeNow(cx->app);
    disabled = disabled || button->disabled;
    if (disabled) {
        button->Disabled(true);
    }
    bool selected = button->selected;
    bool iconOnly = !button->label.s && button->children.len == 0 &&
                    (button->icon != IconName::None || button->buttonIcon);
    bool ghost = button->variant == ButtonVariant::Ghost && !button->outline;
    bool compact = size == UiSize::XSmall || size == UiSize::Small;
    if (compact) {
        // A compact group button is laid out as a Medium one with the content
        // row's style handed down: text_sm on a 1.25rem line, gap_1 or
        // gap_1p5, and the icon one size up from the group's.
        button->WithSize(UiSize::Medium);
        button->contentTextPx = 14.f;
        button->contentLineH = 20.f;
        button->contentGap = size == UiSize::XSmall ? 4.f : 6.f;
        // icon_size: XSmall -> Small, Small -> Medium.
        button->contentIconPx = size == UiSize::XSmall ? 14.f : 16.f;
    } else {
        button->WithSize(size);
    }
    if (ghost) {
        // The group's own ghost: no surface at rest, the muted wash under the
        // pointer — half of it in the dark — and the full muted when a
        // selected one is pressed.
        bool dark = th.mode == ThemeMode::Dark;
        Rgba hover = RgbaOpacity(th.muted, dark ? 0.5f : 1.f);
        button->Custom(ButtonCustomVariant::New(cx->app)
                           .Color(th.transparent)
                           .Foreground(th.foreground)
                           .Hover(hover)
                           .Active(selected ? th.muted : hover));
    }
    if (disabled) {
        button->FocusRing(false);
    }
    El* el = button->IntoEl();
    if (ghost) {
        el->Fg(th.foreground);
        if (disabled) {
            el->Opacity(0.5f);
        }
    }
    // text_sm, font_medium, border_1, shadow_none, then the compact box: a
    // 24 or 32 square for an icon alone, otherwise that height with px_2 or
    // px_2p5, rounded to radius_tokens().sm or the radius.
    el->Font(14)->Medium()->Shadows(nullptr, 0);
    // A variant that drew no border still takes the 1px, transparent, as
    // the custom ghost's border is its transparent fill.
    if (el->style.border < 1) {
        el->Border(1, th.transparent);
    }
    if (compact) {
        float side = size == UiSize::XSmall ? 24.f : 32.f;
        float radius = size == UiSize::XSmall ? th.radius * 0.5f : th.radius;
        el->H(side)->Radius(radius);
        if (iconOnly) {
            el->W(side)->Pad(0);
        } else {
            el->PadX(size == UiSize::XSmall ? 8.f : 10.f);
        }
    }
    refiner.Apply(el);
    return el;
}

InputGroupText* InputGroupText::New(Ctx* cx) {
    Arena* a = cx->a;
    InputGroupText* t = ArenaNew<InputGroupText>(a);
    t->a = a;
    t->cx = cx;
    return t;
}

InputGroupText* InputGroupText::Child(El* el) {
    if (el) {
        children.Append(a, el);
    }
    return this;
}

// An icon without an explicit size follows the addon's one-rem default;
// an explicit size stays with the icon (group.rs addon_child).
static El* InputGroupAddonChildEl(Arena* a, El* child) {
    if (child && child->kind == ElKind::Icon && child->style.width == kAuto &&
        child->style.height == kAuto) {
        return Div(a)->Shrink0()->Font(16)->Child(child);
    }
    return child;
}

El* InputGroupText::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    // h_flex().gap_2().text_sm() in the muted foreground.
    El* row =
        Div(a)->FlexRow()->Gap(8)->ItemsCenter()->Font(14)->Fg(th.mutedFg);
    refiner.Apply(row);
    for (El* c : children) {
        if (c) {
            row->Child(InputGroupAddonChildEl(a, c));
        }
    }
    return row;
}

InputGroupAddon* InputGroupAddon::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    InputGroupAddon* g = ArenaNew<InputGroupAddon>(a);
    g->a = a;
    g->cx = cx;
    g->id = id;
    return g;
}

InputGroupAddon* InputGroupAddon::Align(InputGroupAddonAlignment v) {
    alignment = v;
    return this;
}

InputGroupAddon* InputGroupAddon::Child(El* el) {
    if (el) {
        InputGroupAddonChild child;
        child.el = el;
        children.Append(a, child);
    }
    return this;
}

InputGroupAddon* InputGroupAddon::Child(InputGroupButton* button) {
    if (button) {
        InputGroupAddonChild child;
        child.button = button;
        children.Append(a, child);
    }
    return this;
}

El* InputGroupAddon::IntoEl() {
    return RenderInGroup(false);
}

El* InputGroupAddon::RenderInGroup(bool disabled) {
    const Theme& th = ThemeNow(cx->app);
    float inputH = 0, inputPx = 0, inputPy = 0, inputFont = 0;
    InputSizeMetrics(size, &inputH, &inputPx, &inputPy, &inputFont);
    // flex_none, gap_2, py_1p5, justify_center, font_medium, the input's
    // text size, muted, and the text cursor.
    El* row = Div(a)
                  ->Id(id)
                  ->FlexRow()
                  ->Gap(8)
                  ->ItemsCenter()
                  ->Shrink0()
                  ->JustifyCenter()
                  ->Medium()
                  ->Font(inputFont)
                  ->Fg(th.mutedFg)
                  ->Cursor(CursorKind::IBeam);
    bool compact = size == UiSize::XSmall || size == UiSize::Small;
    row->PadY(compact ? 0.f : 6.f);
    switch (alignment) {
        case InputGroupAddonAlignment::InlineStart:
            row->PadL(6);
            break;
        case InputGroupAddonAlignment::InlineEnd:
            row->PadR(6);
            break;
        // Block addons share the control's horizontal inset.
        case InputGroupAddonAlignment::BlockStart:
            row->W(kFill)->JustifyStart()->PadX(inputPx)->PadT(8);
            break;
        case InputGroupAddonAlignment::BlockEnd:
            row->W(kFill)->JustifyStart()->PadX(inputPx)->PadB(8);
            break;
    }
    refiner.Apply(row);
    for (const InputGroupAddonChild& c : children) {
        row->Child(c.button ? c.button->RenderInGroup(disabled)
                            : InputGroupAddonChildEl(a, c.el));
    }
    return row;
}

InputGroup* InputGroup::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    InputGroup* g = ArenaNew<InputGroup>(a);
    g->a = a;
    g->cx = cx;
    g->id = id;
    return g;
}

InputGroup* InputGroup::Input(component::Input* control) {
    input = control;
    textarea = nullptr;
    return this;
}

InputGroup* InputGroup::Input(Textarea* control) {
    textarea = control;
    input = nullptr;
    return this;
}

InputGroup* InputGroup::Addon(InputGroupAddon* addon) {
    if (addon) {
        addons.Append(a, addon);
    }
    return this;
}

InputGroup* InputGroup::Disabled(bool v) {
    disabled = v;
    return this;
}

InputGroup* InputGroup::Readonly(bool v) {
    readonly = v;
    return this;
}

InputGroup* InputGroup::Invalid(bool v) {
    invalid = v;
    return this;
}

InputGroup* InputGroup::FocusRing(bool v) {
    focusRing = v;
    return this;
}

InputGroup* InputGroup::AriaLabel(Str label) {
    ariaLabel = label;
    return this;
}

InputGroup* InputGroup::WithSize(UiSize s) {
    size = s;
    return this;
}

struct InputGroupHost {
    InputState* target = nullptr;

    static void OnDown(InputGroupHost* self, Ctx* cx, const MouseDownEvent*) {
        if (self && self->target) {
            InputFocus(self->target, cx->app, cx->win);
        }
    }
};

El* InputGroup::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    InputState* state = nullptr;
    bool controlDisabled = false;
    bool multiline = false;
    // The control's own style wins over the group's presets, so it is
    // applied after them rather than by the control.
    ElRefiner controlStyle = {};
    if (input) {
        controlStyle = input->refiner;
        input->refiner = {};
        state = input->state;
        controlDisabled = input->disabled;
        input->WithSize(size)
            ->Appearance(false)
            ->FocusRing(false)
            ->Disabled(disabled || input->disabled)
            ->Readonly(readonly || input->readonly);
    } else if (textarea) {
        controlStyle = textarea->refiner;
        textarea->refiner = {};
        state = textarea->state;
        controlDisabled = textarea->disabled;
        multiline = true;
        textarea->Appearance(false)
            ->FocusRing(false)
            ->Disabled(disabled || textarea->disabled)
            ->Readonly(readonly || textarea->readonly);
        if (textarea->id.s == nullptr || len(textarea->id) == 0) {
            textarea->id = id;
        }
    }
    bool groupDisabled = disabled || controlDisabled;
    bool focused = state && state->focused && !groupDisabled;
    InputGroupAppearance appearance =
        InputGroupAppearance::New(th, focused, groupDisabled, invalid);
    // The border and background colours transition, duration_fast along
    // easing_move, under the group's own id; the ring outside them changes
    // at once, like the standalone input's.
    Motion fast = MotionNew(th.motion.durationFastMs)
                      .Ease(th.motion.easingMove);
    Rgba border = MotionValue(cx, MotionId(id, StrL("border-color")),
                              appearance.border, fast);
    Rgba background = MotionValue(cx, MotionId(id, StrL("background-color")),
                                  appearance.background, fast);
    bool inlineStart = false, inlineEnd = false, blockStart = false,
         blockEnd = false;
    for (InputGroupAddon* addon : addons) {
        switch (addon->alignment) {
            case InputGroupAddonAlignment::InlineStart:
                inlineStart = true;
                break;
            case InputGroupAddonAlignment::InlineEnd:
                inlineEnd = true;
                break;
            case InputGroupAddonAlignment::BlockStart:
                blockStart = true;
                break;
            case InputGroupAddonAlignment::BlockEnd:
                blockEnd = true;
                break;
        }
    }
    if (input) {
        input->W(kFill);
    }

    El* frame = Div(a)
                    ->Id(id)
                    ->Role(AccessibilityRole::Group)
                    ->FlexCol()
                    ->W(kFill)
                    ->Radius(th.radius)
                    ->Border(1, border)
                    ->Bg(background)
                    ->Fg(th.foreground);
    if (ariaLabel.s) {
        frame->AriaLabel(ariaLabel);
    }
    if (!multiline && !blockStart && !blockEnd) {
        float h = size == UiSize::Large    ? 44.f
                  : size == UiSize::Small  ? 24.f
                  : size == UiSize::XSmall ? 20.f
                                           : 32.f;
        frame->H(h);
    }
    refiner.Apply(frame);
    if (groupDisabled) {
        frame->Opacity(0.5f);
    }
    if (appearance.hasRing && th.focusRing && focusRing) {
        frame->Child(Div(a)
                         ->Absolute()
                         ->Top(-3)
                         ->Left(-3)
                         ->Right(-3)
                         ->Bottom(-3)
                         ->Radius(th.radius + 3)
                         ->Border(3, appearance.ring));
    }
    if (state && !groupDisabled) {
        Entity<InputGroupHost> host = ElementStateEntity<InputGroupHost>(
            cx, id, StrL("gpui::InputGroupHost"));
        if (InputGroupHost* h = host.Get(cx)) {
            h->target = state;
        }
        frame->OnMouseDown(ListenTo(host, &InputGroupHost::OnDown));
    }

    El* row = Div(a)->FlexRow()->W(kFill)->ItemsCenter();
    if (!multiline && !blockStart && !blockEnd) {
        row->H(kFill);
    }
    for (InputGroupAddon* addon : addons) {
        addon->size = size;
        if (addon->alignment == InputGroupAddonAlignment::BlockStart) {
            frame->Child(addon->RenderInGroup(groupDisabled));
        }
    }
    for (InputGroupAddon* addon : addons) {
        if (addon->alignment == InputGroupAddonAlignment::InlineStart) {
            row->Child(addon->RenderInGroup(groupDisabled));
        }
    }
    if (input) {
        controlEl = input->IntoEl()->Flex1();
        // render_control: an inline addon takes over part of the control's
        // horizontal inset — pl_2 / pr_2 in place of input_px on that side —
        // and the caller's own style still wins.
        if (inlineStart) {
            controlEl->PadL(8);
        }
        if (inlineEnd) {
            controlEl->PadR(8);
        }
        controlStyle.Apply(controlEl);
        row->Child(controlEl);
    } else if (textarea) {
        controlEl = textarea->IntoEl();
        controlEl->Flex1()->MinH(64);
        controlStyle.Apply(controlEl);
        row->Child(controlEl);
    }
    for (InputGroupAddon* addon : addons) {
        if (addon->alignment == InputGroupAddonAlignment::InlineEnd) {
            row->Child(addon->RenderInGroup(groupDisabled));
        }
    }
    frame->Child(row);
    for (InputGroupAddon* addon : addons) {
        if (addon->alignment == InputGroupAddonAlignment::BlockEnd) {
            frame->Child(addon->RenderInGroup(groupDisabled));
        }
    }
    (void)inlineStart;
    (void)inlineEnd;
    return frame;
}

} // namespace component
} // namespace gpui
