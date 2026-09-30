// crates/component-shell/src/shell/input_group/mod.rs

#include "component_shell/families.h"
#include "component_shell/input_group/mod.h"
#include "component_shell/input_tokens.h"
#include "component_shell/typed_compound/mod.h"
#include "shell/view.h"

namespace gpui::component_shell::input_group {

using typed_compound::FinishPart;
using typed_compound::IsTypedElementOf;
using typed_compound::TakeElementAs;
using typed_compound::TypedChildElementOf;

// Part.
enum class Part : uint8_t {
    Root,
    Addon,
    Button,
    Input,
    Textarea,
    Text,
};

struct PartPayload {
    Part part = Part::Root;
    Str id;
    // Input and Textarea: the retained state argument.
    ComponentArgument state = {};
};

// BoundControl: the control a group takes from its `input` slot. Its binding
// was applied when the part was materialized, which is when Rust's render
// applies it: a replaced control is never materialized at all.
struct BoundControl {
    component::Input* input = nullptr;
    component::Textarea* textarea = nullptr;

    El* IntoEl() { return input ? input->IntoEl() : textarea->IntoEl(); }
};

// leaf().
static bool Leaf(MaterializeRequest* request, const char* name) {
    if (request->ChildrenLen() == 0) return true;
    request
        ->Fail(fmt("%s does not accept ordinary children; use its named "
                   "parts",
                   Str(name)));
    return false;
}

// The state an Input or Textarea part names, and its handle.
static InputState* TextState(MaterializeRequest* request,
                             const PartPayload* payload, const char* kind,
                             uint64_t* handle) {
    shell::TextStateEntity* state =
        request->StateAs<shell::TextStateEntity>(payload->state, kind);
    if (!state) return nullptr;
    const ComponentArgument* value = payload->state.Some();
    *handle = value ? value->handle : 0;
    return &state->input;
}

static Str ControlId(Ctx* cx, const char* kind, uint64_t handle) {
    return StrDup(cx->a, fmt("gpui-component-%s-%llu", Str(kind),
                             (unsigned long long)handle));
}

static El* MaterializeRoot(MaterializeRequest* request,
                           const PartPayload* payload) {
    Ctx* cx = request->cx;
    if (!Leaf(request, "InputGroup")) return nullptr;
    component::InputGroup* group = component::InputGroup::New(cx, payload->id)
                                       ->Disabled(request->disabled);
    // `input` is a common shell slot, like Empty's header/content; a later
    // call replaces an earlier one before materialization.
    El* control = nullptr;
    while (El* element = request->TakeSlot("input")) control = element;
    if (control) {
        if (!IsTypedElementOf<BoundControl>(control))
            return request
                ->Fail(StrL("InputGroup.input expects InputGroupInput or "
                            "InputGroupTextarea"));
        BoundControl* bound =
            TakeElementAs<BoundControl>(request, control, "InputGroup control");
        if (!bound) return nullptr;
        if (bound->input) {
            group->Input(bound->input);
        } else {
            group->Input(bound->textarea);
        }
    }
    bool ok = true;
    EachMethod<Op>(request, [&](const Op& op) {
        if (!ok) return;
        switch (op.kind) {
            case Op::Addon: {
                El* element = request->ResolveElement(op.argument);
                component::InputGroupAddon* addon =
                    element ? TakeElementAs<component::InputGroupAddon>(
                                  request, element, "InputGroupAddon")
                            : nullptr;
                if (!addon) {
                    ok = false;
                    return;
                }
                group->Addon(addon);
                break;
            }
            case Op::Readonly:
                group->Readonly(op.flag);
                break;
            case Op::Invalid:
                group->Invalid(op.flag);
                break;
            case Op::FocusRing:
                group->FocusRing(op.flag);
                break;
            case Op::AriaLabel:
                group->AriaLabel(op.text);
                break;
            case Op::Size:
                group->WithSize(op.size);
                break;
            default:
                break;
        }
    });
    if (!ok) {
        if (!len(request->failure))
            request->Fail(StrL("InputGroup.addon expects an InputGroupAddon"));
        return nullptr;
    }
    group->refiner = request->TakeStyle();
    return group->IntoEl();
}

static El* MaterializeAddon(MaterializeRequest* request,
                            const PartPayload* payload) {
    Ctx* cx = request->cx;
    component::InputGroupAddon* addon =
        component::InputGroupAddon::New(cx, payload->id);
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        // A direct InputGroupButton child is kept as the button, so the
        // group's disabled state reaches it (Rust's InputGroupButtonElement
        // downcast in render_in_group).
        if (IsTypedElementOf<component::InputGroupButton>(children[i])) {
            addon->Child(TakeElementAs<component::InputGroupButton>(
                request, children[i], "InputGroupButton"));
        } else {
            addon->Child(children[i]);
        }
    }
    EachMethod<Op>(request, [&](const Op& op) {
        if (op.kind == Op::Align) addon->Align(op.align);
    });
    addon->refiner = request->TakeStyle();
    return TypedChildElementOf(cx, addon);
}

static El* MaterializeButton(MaterializeRequest* request,
                             const PartPayload* payload) {
    Ctx* cx = request->cx;
    component::InputGroupButton* button =
        component::InputGroupButton::New(cx, payload->id)
            ->Disabled(request->disabled);
    EachMethod<Op>(request, [&](const Op& op) {
        switch (op.kind) {
            case Op::Size:
                button->WithSize(op.size);
                break;
            case Op::Variant:
                button->WithVariant(op.variant);
                break;
            case Op::Label:
                button->Label(op.text);
                break;
            case Op::Icon:
                button->Icon(op.text);
                break;
            case Op::AriaLabel:
                button->AriaLabel(op.text);
                break;
            case Op::Tooltip:
                button->Tooltip(op.text);
                break;
            case Op::Loading:
                button->Loading(op.flag);
                break;
            case Op::Outline:
                button->Outline();
                break;
            default:
                break;
        }
    });
    if (request->onClick)
        button->OnClick(
            Listen(cx, &ScriptView::OnClick, (intptr_t)request->onClick));
    // Rust returns the button itself, an element an addon recognizes as one;
    // here that is the typed part element carrying it.
    button->refiner = request->TakeStyle();
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) button->Child(children[i]);
    return TypedChildElementOf(cx, button);
}

static El* MaterializeInput(MaterializeRequest* request,
                            const PartPayload* payload) {
    Ctx* cx = request->cx;
    if (!Leaf(request, "InputGroupInput")) return nullptr;
    uint64_t handle = 0;
    InputState* state = TextState(request, payload, "InputState", &handle);
    if (!state) return nullptr;
    binding::Binding bound;
    if (!binding::Prepare(request, state, handle, false, &bound))
        return nullptr;
    input_tokens::Binding tokens;
    if (!input_tokens::Prepare(request, state, handle, &tokens)) return nullptr;
    binding::Apply(bound, cx);
    component::Input* input =
        component::Input::New(cx, ControlId(cx, "input", handle), state);
    input_tokens::ApplyInput(tokens, input);
    input->Disabled(request->disabled);
    EachMethod<Op>(request, [&](const Op& op) {
        switch (op.kind) {
            case Op::AriaLabel:
                input->AriaLabel(op.text);
                break;
            case Op::AccessibilityId:
                input->AccessibilityId(op.text);
                break;
            case Op::Readonly:
                input->Readonly(op.flag);
                break;
            case Op::ContentType:
                input->ContentType(op.contentType);
                break;
            default:
                break;
        }
    });
    input->refiner = request->TakeStyle();
    BoundControl* control = ArenaNew<BoundControl>(cx->a);
    control->input = input;
    return TypedChildElementOf(cx, control);
}

static El* MaterializeTextarea(MaterializeRequest* request,
                               const PartPayload* payload) {
    Ctx* cx = request->cx;
    if (!Leaf(request, "InputGroupTextarea")) return nullptr;
    uint64_t handle = 0;
    InputState* state = TextState(request, payload, "TextareaState", &handle);
    if (!state) return nullptr;
    binding::Binding bound;
    if (!binding::Prepare(request, state, handle, true, &bound)) return nullptr;
    input_tokens::Binding tokens;
    if (!input_tokens::Prepare(request, state, handle, &tokens)) return nullptr;
    binding::Apply(bound, cx);
    component::Textarea* textarea =
        component::Textarea::New(cx, ControlId(cx, "textarea", handle), state);
    input_tokens::ApplyTextarea(tokens, textarea);
    textarea->Disabled(request->disabled);
    EachMethod<Op>(request, [&](const Op& op) {
        switch (op.kind) {
            case Op::AriaLabel:
                textarea->AriaLabel(op.text);
                break;
            case Op::AccessibilityId:
                textarea->AccessibilityId(op.text);
                break;
            case Op::Readonly:
                textarea->Readonly(op.flag);
                break;
            default:
                break;
        }
    });
    textarea->refiner = request->TakeStyle();
    BoundControl* control = ArenaNew<BoundControl>(cx->a);
    control->textarea = textarea;
    return TypedChildElementOf(cx, control);
}

static El* Materialize(MaterializeRequest* request) {
    const PartPayload* payload = request->PayloadAs<PartPayload>();
    if (!payload)
        return request
            ->Fail(StrL("InputGroup received an incompatible payload"));
    switch (payload->part) {
        case Part::Root:
            return MaterializeRoot(request, payload);
        case Part::Addon:
            return MaterializeAddon(request, payload);
        case Part::Button:
            return MaterializeButton(request, payload);
        case Part::Input:
            return MaterializeInput(request, payload);
        case Part::Textarea:
            return MaterializeTextarea(request, payload);
        case Part::Text:
            return FinishPart(request,
                              component::InputGroupText::New(request->cx));
    }
    return nullptr;
}

// ─── Recorders ─────────────────────────────────────────────────────────────

// A callable's name for its refusal, the way Rust's closures capture it.
struct Callable {
    const char* component;
    const char* method;
};

// id_constructor.
template <Part P, const char* Name>
static bool ConstructId(PayloadBuild* build, const ComponentArgument* args,
                        int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String ||
        len(StrTrimAscii(args[0].string)) == 0) {
        return build->Fail(fmt("%s expects one nonempty string id", Str(Name)));
    }
    PartPayload* payload = build->New<PartPayload>();
    payload->part = P;
    payload->id = args[0].string;
    return true;
}

// state_constructor.
template <Part P, const char* Name, const char* Kind>
static bool ConstructState(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Entity)
        return build
            ->Fail(fmt("%s expects one %s entity", Str(Name), Str(Kind)));
    PartPayload* payload = build->New<PartPayload>();
    payload->part = P;
    payload->state = args[0];
    return true;
}

static bool ConstructText(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<PartPayload>()->part = Part::Text;
    return true;
}

// support.rs bool_method.
template <Op::Kind K, const Callable& C>
static bool RecordBool(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(
            fmt("%s.%s expects one boolean", Str(C.component), Str(C.method)));
    Op* op = build->New<Op>();
    op->kind = K;
    op->flag = args[0].boolean;
    return true;
}

// support.rs string_method.
template <Op::Kind K, const Callable& C>
static bool RecordString(PayloadBuild* build, const ComponentArgument* args,
                         int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build->Fail(
            fmt("%s.%s expects one string", Str(C.component), Str(C.method)));
    Op* op = build->New<Op>();
    op->kind = K;
    op->text = args[0].string;
    return true;
}

// part_method("addon", ...).
static bool RecordAddon(PayloadBuild* build, const ComponentArgument* args,
                        int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Element)
        return build
            ->Fail(StrL("addon expects one registered InputGroup part"));
    Op* op = build->New<Op>();
    op->kind = Op::Addon;
    op->argument = args[0];
    return true;
}

// callback_method("on_change", ...).
static bool RecordOnChange(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Callback)
        return build->Fail(StrL("on_change expects one callback"));
    Op* op = build->New<Op>();
    op->kind = Op::OnChange;
    op->argument = args[0];
    return true;
}

static bool RecordOutline(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<Op>()->kind = Op::Outline;
    return true;
}

// enum_method's refusal for a literal its parser does not know.
static bool Unsupported(PayloadBuild* build, const char* name, Str value) {
    return build->Fail(fmt("unsupported %s `%s`", Str(name), value));
}

// enum_method's refusal for a call that is not one literal.
static bool ExpectsOneOf(PayloadBuild* build, const char* name,
                         Slice<const char*> values) {
    StrBuilder expected;
    for (int i = 0; i < values.count; i++) {
        if (i) expected.Append(StrL(", "));
        expected.Append(Str(values[i]));
    }
    Str list = expected.TakeStr();
    build->Fail(fmt("%s expects one of %s", Str(name), list));
    StrFree(list);
    return false;
}

static constexpr const char* kAlignLiterals[] = {"inline-start", "inline-end",
                                                 "block-start", "block-end"};
static constexpr const char* kVariantLiterals[] = {
    "default", "primary", "secondary", "danger", "warning",
    "success", "info",    "ghost",     "link",   "text"};

static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return ExpectsOneOf(build, "size", kSizeLiterals);
    for (const char* literal : kSizeLiterals) {
        if (!StrEq(args[0].string, literal)) continue;
        Op* op = build->New<Op>();
        op->kind = Op::Size;
        op->size = SizeOfLiteral(args[0].string);
        return true;
    }
    return Unsupported(build, "size", args[0].string);
}

static bool RecordAlign(PayloadBuild* build, const ComponentArgument* args,
                        int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return ExpectsOneOf(build, "align", kAlignLiterals);
    static const component::InputGroupAddonAlignment kinds[] = {
        component::InputGroupAddonAlignment::InlineStart,
        component::InputGroupAddonAlignment::InlineEnd,
        component::InputGroupAddonAlignment::BlockStart,
        component::InputGroupAddonAlignment::BlockEnd,
    };
    for (int i = 0; i < 4; i++) {
        if (!StrEq(args[0].string, kAlignLiterals[i])) continue;
        Op* op = build->New<Op>();
        op->kind = Op::Align;
        op->align = kinds[i];
        return true;
    }
    return Unsupported(build, "align", args[0].string);
}

static bool RecordVariant(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return ExpectsOneOf(build, "variant", kVariantLiterals);
    static const component::ButtonVariant kinds[] = {
        component::ButtonVariant::Default,   component::ButtonVariant::Primary,
        component::ButtonVariant::Secondary, component::ButtonVariant::Danger,
        component::ButtonVariant::Warning,   component::ButtonVariant::Success,
        component::ButtonVariant::Info,      component::ButtonVariant::Ghost,
        component::ButtonVariant::Link,      component::ButtonVariant::Text,
    };
    for (int i = 0; i < 10; i++) {
        if (!StrEq(args[0].string, kVariantLiterals[i])) continue;
        Op* op = build->New<Op>();
        op->kind = Op::Variant;
        op->variant = kinds[i];
        return true;
    }
    return Unsupported(build, "variant", args[0].string);
}

// ─── Descriptors ───────────────────────────────────────────────────────────

static constexpr char kInputGroup[] = "InputGroup";
static constexpr char kInputGroupAddon[] = "InputGroupAddon";
static constexpr char kInputGroupButton[] = "InputGroupButton";
static constexpr char kInputGroupInput[] = "InputGroupInput";
static constexpr char kInputGroupTextarea[] = "InputGroupTextarea";
static constexpr char kInputState[] = "InputState";
static constexpr char kTextareaState[] = "TextareaState";

static constexpr Callable kGroupReadonly = {kInputGroup, "readonly"};
static constexpr Callable kGroupInvalid = {kInputGroup, "invalid"};
static constexpr Callable kGroupFocusRing = {kInputGroup, "focus_ring"};
static constexpr Callable kGroupAriaLabel = {kInputGroup, "aria_label"};
static constexpr Callable kButtonLabel = {kInputGroupButton, "label"};
static constexpr Callable kButtonIcon = {kInputGroupButton, "icon"};
static constexpr Callable kButtonAriaLabel = {kInputGroupButton, "aria_label"};
static constexpr Callable kButtonTooltip = {kInputGroupButton, "tooltip"};
static constexpr Callable kButtonLoading = {kInputGroupButton, "loading"};
static constexpr Callable kInputReadonly = {kInputGroupInput, "readonly"};
static constexpr Callable kInputAriaLabel = {kInputGroupInput, "aria_label"};
static constexpr Callable kInputAccessibilityId = {kInputGroupInput,
                                                   "accessibility_id"};
static constexpr Callable kInputValue = {kInputGroupInput, "value"};
static constexpr Callable kInputPlaceholder = {kInputGroupInput, "placeholder"};
static constexpr Callable kInputMasked = {kInputGroupInput, "masked"};
static constexpr Callable kTextareaReadonly = {kInputGroupTextarea, "readonly"};
static constexpr Callable kTextareaAriaLabel = {kInputGroupTextarea,
                                                "aria_label"};
static constexpr Callable kTextareaAccessibilityId = {kInputGroupTextarea,
                                                      "accessibility_id"};
static constexpr Callable kTextareaValue = {kInputGroupTextarea, "value"};
static constexpr Callable kTextareaPlaceholder = {kInputGroupTextarea,
                                                  "placeholder"};

static constexpr ArgumentDescriptor kIdArgs[] = {{"id", SchemaString()}};
static constexpr ArgumentDescriptor kInputStateArgs[] = {
    {"state", SchemaEntity("InputState")}};
static constexpr ArgumentDescriptor kTextareaStateArgs[] = {
    {"state", SchemaEntity("TextareaState")}};
static constexpr ArgumentDescriptor kAddonArgs[] = {{"addon", SchemaElement()}};
static constexpr ArgumentDescriptor kReadonlyArgs[] = {
    {"readonly", SchemaBoolean()}};
static constexpr ArgumentDescriptor kInvalidArgs[] = {
    {"invalid", SchemaBoolean()}};
static constexpr ArgumentDescriptor kFocusRingArgs[] = {
    {"focus_ring", SchemaBoolean()}};
static constexpr ArgumentDescriptor kAriaLabelArgs[] = {
    {"aria_label", SchemaString()}};
static constexpr ArgumentDescriptor kAccessibilityIdArgs[] = {
    {"accessibility_id", SchemaString()}};
static constexpr ArgumentDescriptor kValueArgs[] = {{"value", SchemaString()}};
static constexpr ArgumentDescriptor kPlaceholderArgs[] = {
    {"placeholder", SchemaString()}};
static constexpr ArgumentDescriptor kOnChangeArgs[] = {
    {"callback", SchemaCallback("(value: string, cx: Context) => void")}};
static constexpr ArgumentDescriptor kMaskedArgs[] = {
    {"masked", SchemaBoolean()}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};
static constexpr ArgumentDescriptor kAlignArgs[] = {
    {"align", SchemaEnum(kAlignLiterals)}};
static constexpr ArgumentDescriptor kLabelArgs[] = {{"label", SchemaString()}};
static constexpr ArgumentDescriptor kIconArgs[] = {{"icon", SchemaString()}};
static constexpr ArgumentDescriptor kTooltipArgs[] = {
    {"tooltip", SchemaString()}};
static constexpr ArgumentDescriptor kLoadingArgs[] = {
    {"loading", SchemaBoolean()}};
static constexpr ArgumentDescriptor kVariantArgs[] = {
    {"variant", SchemaEnum(kVariantLiterals)}};

static constexpr ConstructorDescriptor kGroupConstructors[] = {
    {kInputGroup, kIdArgs, &ConstructId<Part::Root, kInputGroup>}};
static constexpr ConstructorDescriptor kAddonConstructors[] = {
    {kInputGroupAddon, kIdArgs, &ConstructId<Part::Addon, kInputGroupAddon>}};
static constexpr ConstructorDescriptor kButtonConstructors[] = {
    {kInputGroupButton, kIdArgs,
     &ConstructId<Part::Button, kInputGroupButton>}};
static constexpr ConstructorDescriptor kInputConstructors[] = {
    {kInputGroupInput, kInputStateArgs,
     &ConstructState<Part::Input, kInputGroupInput, kInputState>}};
static constexpr ConstructorDescriptor kTextareaConstructors[] = {
    {kInputGroupTextarea, kTextareaStateArgs,
     &ConstructState<Part::Textarea, kInputGroupTextarea, kTextareaState>}};
static constexpr ConstructorDescriptor kTextConstructors[] = {
    {"InputGroupText", {}, &ConstructText}};

static constexpr MethodDescriptor kGroupMethods[] = {
    {"addon", kAddonArgs, "Appends an InputGroupAddon at its logical side.",
     &RecordAddon},
    kDisabledMethod,
    {"readonly", kReadonlyArgs,
     "Prevents text edits while leaving addon actions available.",
     &RecordBool<Op::Readonly, kGroupReadonly>},
    {"invalid", kInvalidArgs,
     "Displays the caller's validation result without rejecting input.",
     &RecordBool<Op::Invalid, kGroupInvalid>},
    {"focus_ring", kFocusRingArgs,
     "Controls the outward focus and error ring, also respecting "
     "Theme.focus_ring.",
     &RecordBool<Op::FocusRing, kGroupFocusRing>},
    {"aria_label", kAriaLabelArgs,
     "Names the group independently of its text control.",
     &RecordString<Op::AriaLabel, kGroupAriaLabel>},
    {"size", kSizeArgs, "Sets the group's semantic control size.", &RecordSize},
};

static constexpr MethodDescriptor kAddonMethods[] = {
    {"align", kAlignArgs,
     "Places the addon relative to the control; default is inline-start.",
     &RecordAlign},
};

static constexpr MethodDescriptor kButtonMethods[] = {
    kDisabledMethod,
    kOnClickMethod,
    {"label", kLabelArgs, "Sets the visible label.",
     &RecordString<Op::Label, kButtonLabel>},
    {"icon", kIconArgs, "Sets the icon path in the application's asset bundle.",
     &RecordString<Op::Icon, kButtonIcon>},
    {"aria_label", kAriaLabelArgs, "Names an icon-only action.",
     &RecordString<Op::AriaLabel, kButtonAriaLabel>},
    {"tooltip", kTooltipArgs, "Sets the native tooltip.",
     &RecordString<Op::Tooltip, kButtonTooltip>},
    {"loading", kLoadingArgs,
     "Displays progress and prevents duplicate activation.",
     &RecordBool<Op::Loading, kButtonLoading>},
    {"outline",
     {},
     "Uses the native outlined button treatment.",
     &RecordOutline},
    {"size", kSizeArgs,
     "Sets the button size; xsmall and small are the compact input-group "
     "sizes.",
     &RecordSize},
    {"variant", kVariantArgs, "Selects a native semantic button variant.",
     &RecordVariant},
};

// control_methods(name), then what each control adds, then
// input_tokens::methods(false).
static constexpr MethodDescriptor kInputMethods[] = {
    kDisabledMethod,
    {"readonly", kReadonlyArgs,
     "Prevents edits while preserving selection and copying.",
     &RecordBool<Op::Readonly, kInputReadonly>},
    {"aria_label", kAriaLabelArgs, "Names the text control for accessibility.",
     &RecordString<Op::AriaLabel, kInputAriaLabel>},
    {"accessibility_id", kAccessibilityIdArgs,
     "Sets the developer-assigned accessibility identifier.",
     &RecordString<Op::AccessibilityId, kInputAccessibilityId>},
    {"value", kValueArgs,
     "Controls the retained text. Equal values preserve the caret and undo "
     "history; programmatic changes do not emit on_change.",
     &RecordString<Op::Value, kInputValue>},
    {"placeholder", kPlaceholderArgs,
     "Sets the empty-value prompt on the retained state.",
     &RecordString<Op::Placeholder, kInputPlaceholder>},
    {"on_change", kOnChangeArgs,
     "Reports edits from the retained input without duplicating "
     "subscriptions across renders.",
     &RecordOnChange},
    {"masked", kMaskedArgs, "Controls password masking on InputState.",
     &RecordBool<Op::Masked, kInputMasked>},
    content_type::kMethod,
    input_tokens::kTokenMethod,
    input_tokens::kTokenClickMethod,
};

static constexpr MethodDescriptor kTextareaMethods[] = {
    kDisabledMethod,
    {"readonly", kReadonlyArgs,
     "Prevents edits while preserving selection and copying.",
     &RecordBool<Op::Readonly, kTextareaReadonly>},
    {"aria_label", kAriaLabelArgs, "Names the text control for accessibility.",
     &RecordString<Op::AriaLabel, kTextareaAriaLabel>},
    {"accessibility_id", kAccessibilityIdArgs,
     "Sets the developer-assigned accessibility identifier.",
     &RecordString<Op::AccessibilityId, kTextareaAccessibilityId>},
    {"value", kValueArgs,
     "Controls the retained text. Equal values preserve the caret and undo "
     "history; programmatic changes do not emit on_change.",
     &RecordString<Op::Value, kTextareaValue>},
    {"placeholder", kPlaceholderArgs,
     "Sets the empty-value prompt on the retained state.",
     &RecordString<Op::Placeholder, kTextareaPlaceholder>},
    {"on_change", kOnChangeArgs,
     "Reports edits from the retained input without duplicating "
     "subscriptions across renders.",
     &RecordOnChange},
    binding::kRowsMethod,
    binding::kAutoGrowMethod,
    input_tokens::kTokenMethod,
    input_tokens::kTokenClickMethod,
};

static constexpr ComponentDescriptor kParts[] = {
    {kInputGroup, kGroupConstructors, kGroupMethods,
     "A shared themed frame. The common input(...) slot accepts "
     "InputGroupInput or InputGroupTextarea; addons accumulate in insertion "
     "order.",
     &Materialize},
    {kInputGroupAddon, kAddonConstructors, kAddonMethods,
     "An aligned addon. Children retain insertion order; direct "
     "InputGroupButton children inherit the group disabled state.",
     &Materialize},
    {kInputGroupButton, kButtonConstructors, kButtonMethods,
     "A native Button with compact input-group presentation and group "
     "disabled inheritance. Defaults to the ghost variant and xsmall size; a "
     "button with only an icon is square.",
     &Materialize},
    {kInputGroupInput, kInputConstructors, kInputMethods,
     "An unframed single-line input using the existing retained InputState "
     "and native editing engine.",
     &Materialize},
    {kInputGroupTextarea, kTextareaConstructors, kTextareaMethods,
     "An unframed multiline input using the existing retained TextareaState "
     "and native editing engine.",
     &Materialize},
    {"InputGroupText",
     kTextConstructors,
     {},
     "Muted text or rich helper content inside an input group.",
     &Materialize},
};

} // namespace gpui::component_shell::input_group

namespace gpui::component_shell {

bool RegisterInputGroup(shell::ComponentRegistry* registry,
                        shell::RegistryError* error) {
    for (const ComponentDescriptor& descriptor : input_group::kParts) {
        if (!registry->Register(&descriptor, error)) return false;
    }
    return true;
}

} // namespace gpui::component_shell
