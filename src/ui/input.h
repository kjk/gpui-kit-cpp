#ifndef GPUI_UI_INPUT_H_
#define GPUI_UI_INPUT_H_
/* Themed input — crates/ui/src/input */

#include "ui/sizing.h"
#include "ui/button.h"
#include "ui/icon.h"
#include "base/input_tokens.h"

namespace gpui {

namespace component {

// input/mod.rs's no-tree-sitter projection. Tree-sitter is a standing
// dependency exclusion, and upstream publishes this zero-sized placeholder
// under the same cfg so callers can still name the type.
namespace input_syntax {
struct Tree {};
} // namespace input_syntax

enum class AnyInputKind : uint8_t {
    None,
    Input,
    Textarea,
    Editor,
    Otp
};

// state.rs AnyInputState. Rust's four Entity variants become a tag plus the
// port's retained state handles: the three text modes intentionally share
// InputState, while OTP keeps its own generational entity.
struct AnyInputState {
    AnyInputKind kind = AnyInputKind::None;
    InputState* text = nullptr;
    Entity<OtpState> otp = {};

    static AnyInputState From(InputState* state);
    static AnyInputState FromInput(InputState* state);
    static AnyInputState FromTextarea(InputState* state);
    static AnyInputState FromEditor(InputState* state);
    static AnyInputState FromOtp(Entity<OtpState> state);
    InputState* AsInput() const;
    InputState* AsTextarea() const;
    InputState* AsEditor() const;
    Entity<OtpState> AsOtp() const;
    Str Value(Arena* a, App* app) const;
    FocusHandle FocusHandleOf(const Window* window, App* app) const;
    bool operator==(const AnyInputState& other) const;
    bool operator!=(const AnyInputState& other) const {
        return !(*this == other);
    }
};

enum class InputAlign : uint8_t {
    Left,
    Center,
    Right
};

// crates/ui/src/input/content_type.rs. The role projection below uses the
// semantic variants on every platform; a future native autofill adapter can
// consume the same value without changing the component surface.
enum class InputContentType : uint8_t {
    Name,
    NamePrefix,
    GivenName,
    MiddleName,
    FamilyName,
    NameSuffix,
    Nickname,
    JobTitle,
    OrganizationName,
    Location,
    FullStreetAddress,
    StreetAddressLine1,
    StreetAddressLine2,
    AddressCity,
    AddressState,
    AddressCityAndState,
    Sublocality,
    CountryName,
    PostalCode,
    TelephoneNumber,
    EmailAddress,
    Url,
    CreditCardNumber,
    CreditCardName,
    CreditCardGivenName,
    CreditCardMiddleName,
    CreditCardFamilyName,
    CreditCardSecurityCode,
    CreditCardExpiration,
    CreditCardExpirationMonth,
    CreditCardExpirationYear,
    CreditCardType,
    Username,
    Password,
    NewPassword,
    OneTimeCode,
    ShipmentTrackingNumber,
    FlightNumber,
    DateTime,
    Birthdate,
    BirthdateDay,
    BirthdateMonth,
    BirthdateYear,
    CellularEid,
    CellularImei
};

struct Input {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    InputState* state = nullptr;
    Str label = {};
    float width = kFill;
    El* prefix = nullptr;
    El* suffix = nullptr;
    UiSize size = UiSize::Medium;
    InputAlign align = InputAlign::Left;
    bool disabled = false;
    bool cleanable = false;
    // A masked input draws bullets; mask_toggle adds the eye that flips it.
    bool masked = false;
    bool maskToggle = false;
    bool appearance = true;
    bool focusRing = true;
    bool readonly = false;
    InputContentType contentType = InputContentType::Name;
    bool hasContentType = false;
    AccessibilityRole accessibilityRole = AccessibilityRole::None;
    bool hasAccessibilityRole = false;
    Str accessibilityId = {};
    Str ariaLabel = {};
    Rgba textColor = {};
    bool hasTextColor = false;
    Listener onChange;
    Listener onFocus;
    Listener onClear;
    Listener onToggleMask;
    InputPasteFn onPaste = nullptr;
    void* onPasteData = nullptr;

    static Input* New(Ctx* cx, Str id, InputState* state);
    Input* Label(Str s);
    Input* WithSize(UiSize s);
    Input* Align(InputAlign v);
    Input* Disabled(bool v);
    Input* Readonly(bool v = true);
    Input* ContentType(InputContentType value);
    // AccessibilityRole::None is the presentational override.
    Input* Role(AccessibilityRole role);
    Input* AccessibilityId(Str id);
    Input* AriaLabel(Str label);
    Input* Cleanable(bool v = true);
    Input* Masked(bool v);
    Input* MaskToggle(bool v = true);
    Input* Appearance(bool v);
    // FocusableExt::focus_ring: no focus appearance on this control.
    Input* FocusRing(bool v);
    Input* TextColor(Rgba c);
    Input* OnClear(Listener fn);
    Input* OnToggleMask(Listener fn);
    // Rust's Input::prefix / Input::suffix: content inside the border box, on
    // either side of the editor. A prefix brings its own left padding.
    Input* Prefix(El* el);
    Input* Suffix(El* el);
    // Rust's Input fills its parent (`size_full`); a caller that puts one in a
    // row next to other content sizes it with `.w(px(..))` instead.
    Input* W(float v);
    Input* OnChange(Listener fn);
    Input* OnFocus(Listener fn);
    Input* OnPaste(InputPasteFn fn, void* data = nullptr);
    El* IntoEl();
};

// The element an InlineToken renders as by default. Editing and activation
// belong to the input.
struct InputToken {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    InlineTokenContext context = {};
    IconName icon = IconName::None;
    bool hasIcon = false;

    static InputToken* New(Ctx* cx, const InlineTokenContext& context);
    InputToken* Icon(IconName name);
    El* IntoEl();
};

// SearchPanel, crates/ui/src/input/search.rs: the find bar over a searchable
// field. Rust hangs it off the input's overlay; here the caller puts it above
// the field it searches, which is where it renders. It owns the two fields it
// holds — the query and the replacement — so the caller names only the field
// being searched, and it answers nothing at all while the session is closed.
struct SearchPanel {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    InputState* target = nullptr;

    static SearchPanel* New(Ctx* cx, Str id, InputState* target);
    El* IntoEl();
};

struct NativeMenu;
using EditorContextMenuFn = NativeMenu* (*)(Ctx * cx, NativeMenu* empty,
                                            void* data);

// editor.rs Editor. Highlighter remains the compatibility spelling for the
// earlier façade; Editor is the source-shaped styled control over the same
// retained InputState engine.
struct Editor {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    InputState* state = nullptr;
    float height = 0;
    float fontSize = 0;
    bool appearance = true;
    bool bordered = true;
    bool disabled = false;
    bool readonly = false;
    int tabIndex = 0;
    AccessibilityRole accessibilityRole = AccessibilityRole::MultilineTextInput;
    Str ariaLabel = {};
    EditorContextMenuFn contextMenu = nullptr;
    void* contextMenuData = nullptr;
    InputPasteFn onPaste = nullptr;
    void* onPasteData = nullptr;
    // The port keeps EditorState's source-language/decorations settings on
    // the frame value which binds that state. These are forwarded to the
    // compatibility Highlighter implementation; callers can stay on the
    // source-shaped Editor surface.
    Str language = {};
    const TextSpan* decorations = nullptr;
    int nDecorations = 0;
    bool activeLine = false;
    bool indentGuides = false;
    bool searchable = true;
    bool folding = false;
    const Diagnostic* diagnostics = nullptr;
    int nDiagnostics = 0;
    gpui::Style style = {};
    uint32_t styleFields = 0;

    static Editor* New(Ctx* cx, InputState* state);
    static Editor* New(Ctx* cx, Str id, InputState* state);
    Editor* H(float value);
    Editor* Font(float value);
    Editor* Appearance(bool value);
    Editor* Bordered(bool value);
    Editor* Disabled(bool value);
    Editor* Readonly(bool value = true);
    Editor* TabIndex(int value);
    Editor* Role(AccessibilityRole value);
    Editor* AriaLabel(Str value);
    Editor* ContextMenu(EditorContextMenuFn fn, void* data = nullptr);
    Editor* OnPaste(InputPasteFn fn, void* data = nullptr);
    Editor* Language(Str value);
    Editor* Decorations(const TextSpan* runs, int n);
    Editor* ActiveLine(bool value = true);
    Editor* IndentGuides(bool value = true);
    Editor* Searchable(bool value);
    Editor* Diagnostics(const Diagnostic* items, int n);
    Editor* Folding(bool value = true);
    Editor* Refine(const gpui::Style& value, uint32_t fields);
    El* IntoEl();
};

// The four retained editor overlays in input/popovers. Their durable state is
// already part of InputState; these source-named values are the themed view
// and operation façade over it.
struct CompletionMenu {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    InputState* editor = nullptr;
    Str query = {};

    static CompletionMenu* New(Ctx* cx, InputState* editor);
    CompletionMenu* UpdateQuery(int startOffset, Str query);
    CompletionMenu* Show(int offset, const CompletionItem* items, int n);
    void Hide();
    bool HandleAction(InputAction action);
    El* IntoEl();
};

struct CodeActionMenu {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    InputState* state = nullptr;

    static CodeActionMenu* New(Ctx* cx, InputState* state);
    CodeActionMenu* Show(int offset, const CodeActionItem* items, int n);
    void Hide();
    bool HandleAction(InputAction action);
    El* IntoEl();
};

struct DiagnosticPopover {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    InputState* state = nullptr;
    int diagnostic = -1;

    static DiagnosticPopover* New(Ctx* cx, InputState* state, int diagnostic);
    El* IntoEl();
};

struct HoverPopover {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    InputState* editor = nullptr;
    Selection symbolRange = {};
    Str hover = {};

    static HoverPopover* New(Ctx* cx, InputState* editor, Selection symbolRange,
                             Str hover);
    El* IntoEl();
};

struct Textarea {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    // Rust's is `Textarea::new(&state)` where the state is a TextareaState —
    // the same engine as an Input's, with InputKind::Textarea.
    InputState* state = nullptr;
    int rows = 0;
    // The editor box height in pixels, or kFill for Rust's h(relative(1.)).
    float height = 0;
    bool softWrap = true;
    AccessibilityRole accessibilityRole = AccessibilityRole::MultilineTextInput;
    Str ariaLabel = {};
    Str accessibilityId = {};
    bool appearance = true;
    bool disabled = false;
    bool readonly = false;
    bool focusRing = true;
    Listener onFocus;
    InputPasteFn onPaste = nullptr;
    void* onPasteData = nullptr;

    static Textarea* New(Ctx* cx, Str id, InputState* state);
    // Rust sizes a textarea by rows (`auto_grow(min, max)`); without one it
    // keeps the two-row default. An explicit height wins, as `.h(px(..))`
    // does there.
    Textarea* Rows(int n);
    Textarea* H(float px);
    Textarea* SoftWrap(bool v);
    Textarea* Role(AccessibilityRole role);
    Textarea* AccessibilityId(Str id);
    Textarea* AriaLabel(Str label);
    Textarea* Disabled(bool v);
    Textarea* Readonly(bool v = true);
    Textarea* Appearance(bool v);
    Textarea* FocusRing(bool v);
    Textarea* OnFocus(Listener fn);
    Textarea* OnPaste(InputPasteFn fn, void* data = nullptr);
    El* IntoEl();
};

// The logical side of an addon relative to the text control.
enum class InputGroupAddonAlignment : uint8_t {
    InlineStart,
    InlineEnd,
    BlockStart,
    BlockEnd
};

// GroupAppearance: border, fill and optional ring for the shared frame.
struct InputGroupAppearance {
    Rgba background = {};
    Rgba border = {};
    Rgba ring = {};
    bool hasRing = false;

    static InputGroupAppearance New(const Theme& th, bool focused,
                                    bool disabled, bool invalid);
};

// InputGroupButton: a Button with compact input-group presentation.
struct InputGroupButton {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Button* button = nullptr;
    UiSize size = UiSize::XSmall;

    static InputGroupButton* New(Ctx* cx, Str id);
    InputGroupButton* Label(Str s);
    InputGroupButton* Icon(IconName n);
    InputGroupButton* Icon(Str path);
    InputGroupButton* Tooltip(Str s);
    InputGroupButton* AriaLabel(Str s);
    InputGroupButton* WithSize(UiSize s);
    InputGroupButton* WithVariant(ButtonVariant v);
    InputGroupButton* Disabled(bool v);
    InputGroupButton* Loading(bool v);
    InputGroupButton* Outline();
    InputGroupButton* OnClick(Listener fn);
    InputGroupButton* Child(El* el);
    El* IntoEl();
};

// InputGroupText: muted helper text inside an input group.
struct InputGroupText {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    ArenaVec<El*> children;

    static InputGroupText* New(Ctx* cx);
    InputGroupText* Child(El* el);
    El* IntoEl();
};

// InputGroupAddon: text, icons and buttons on one side of the frame.
struct InputGroupAddon {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    InputGroupAddonAlignment alignment = InputGroupAddonAlignment::InlineStart;
    UiSize size = UiSize::Medium;
    ArenaVec<El*> children;

    static InputGroupAddon* New(Ctx* cx, Str id);
    InputGroupAddon* Align(InputGroupAddonAlignment v);
    InputGroupAddon* Child(El* el);
    El* IntoEl();
};

// InputGroup: a shared frame around one text control and its addons. The
// caller keeps the InputState; the group owns only composition.
using InputGroupInput = Input;
using InputGroupTextarea = Textarea;

// A single-line input or textarea accepted by InputGroup::Input.
struct InputGroupControl {
    Input* input = nullptr;
    Textarea* textarea = nullptr;
};

struct InputGroup {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Input* input = nullptr;
    Textarea* textarea = nullptr;
    ArenaVec<InputGroupAddon*> addons;
    UiSize size = UiSize::Medium;
    bool disabled = false;
    bool readonly = false;
    bool invalid = false;
    bool focusRing = true;
    Str ariaLabel = {};
    // Filled by IntoEl so a host can apply the control's own style ops
    // after the group has stripped appearance and the focus ring.
    El* controlEl = nullptr;

    static InputGroup* New(Ctx* cx, Str id);
    InputGroup* Input(Input* control);
    InputGroup* Input(Textarea* control);
    InputGroup* Addon(InputGroupAddon* addon);
    InputGroup* Disabled(bool v);
    InputGroup* Readonly(bool v = true);
    InputGroup* Invalid(bool v);
    InputGroup* FocusRing(bool v);
    InputGroup* AriaLabel(Str label);
    InputGroup* WithSize(UiSize s);
    El* IntoEl();
};

struct NumberInput {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    InputState* state = nullptr;
    float width = kFill;
    UiSize size = UiSize::Medium;
    bool disabled = false;
    bool appearance = true;
    bool focusRing = true;
    El* suffix = nullptr;
    Background bg = {};
    bool hasBg = false;
    Rgba textColor = {};
    bool hasTextColor = false;
    NumberStep numberStep = {};
    bool hasNumberStep = true;
    bool hasMin = false;
    double min = 0;
    bool hasMax = false;
    double max = 0;
    Listener onStep;
    Listener onInc;
    Listener onDec;
    Listener onFocus;

    static NumberInput* New(Ctx* cx, InputState* state);
    static NumberInput* New(Ctx* cx, Str id, InputState* state);
    // Fills its parent unless the caller sizes it, as in Rust.
    NumberInput* W(float v);
    NumberInput* WithSize(UiSize s);
    NumberInput* Disabled(bool v);
    NumberInput* Appearance(bool v);
    // FocusableExt::focus_ring: no focus appearance on this control.
    NumberInput* FocusRing(bool v);
    NumberInput* Suffix(El* el);
    NumberInput* Bg(Background c);
    NumberInput* TextColor(Rgba c);
    // InputState::step / step_by / set_step and its directional bounds.
    NumberInput* Step(double value);
    NumberInput* StepBy(NumberStepByValueFn fn, intptr_t arg = 0);
    NumberInput* NoStep();
    NumberInput* Min(double value);
    NumberInput* Max(double value);
    // NumberInput::on_step, receiving NumberInputEvent.
    NumberInput* OnStep(Listener fn);
    NumberInput* OnFocus(Listener fn);
    NumberInput* OnInc(Listener fn);
    NumberInput* OnDec(Listener fn);
    El* IntoEl();
};

struct OtpInput {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    const char* value = nullptr;
    int len = 0;
    int slots = 6;
    // The cells are split into this many groups, spaced further apart.
    int groups = 2;
    bool masked = false;
    bool disabled = false;
    bool focusRing = true;
    UiSize size = UiSize::Medium;
    float cellPx = 0; // with_size(px(..)): a custom cell edge
    Listener onFocus;
    // The field's own state, when it has one: the value, the focus and the
    // caret are its, and typing into it edits them. A caller with a fixed
    // value passes none and gets the cells with nothing behind them.
    Entity<OtpState> state = {};

    static OtpInput* New(Ctx* cx, const char* value, int len);
    static OtpInput* New(Ctx* cx, Str id, Entity<OtpState> state);
    OtpInput* Id(Str s);
    OtpInput* Slots(int n);
    OtpInput* Groups(int n);
    OtpInput* Masked(bool v);
    OtpInput* Disabled(bool v);
    // FocusableExt::focus_ring: no focus appearance on this control.
    OtpInput* FocusRing(bool v);
    OtpInput* WithSize(UiSize s);
    OtpInput* CellSize(float px);
    OtpInput* OnFocus(Listener fn);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_INPUT_H_
