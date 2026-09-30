// crates/component-shell/src/shell/layout/textarea.rs

#include "component_shell/families.h"
#include "component_shell/input_tokens.h"
#include "component_shell/layout/mod.h"

namespace gpui::component_shell::layout::textarea {

struct Op {
    enum Kind : uint8_t {
        Appearance,
        Bordered,
        Readonly,
        AriaLabel,
    } kind = Appearance;
    bool flag = false;
    Str text;
};

bool RequireLeaf(int children, Str* error) {
    if (children == 0) return true;
    *error = StrL("Textarea does not accept children");
    return false;
}

static El* Materialize(MaterializeRequest* request) {
    const ComponentArgument* argument = request->PayloadAs<ComponentArgument>();
    if (!argument)
        return request->Fail(StrL("Textarea received an incompatible payload"));
    shell::TextStateEntity* state =
        request->StateAs<shell::TextStateEntity>(*argument, "TextareaState");
    if (!state) return nullptr;
    const ComponentArgument* value = argument->Some();
    uint64_t handle = value ? value->handle : 0;
    input_tokens::Binding binding;
    if (!input_tokens::Prepare(request, &state->input, handle, &binding))
        return nullptr;
    Ctx* cx = request->cx;
    component::Textarea* textarea = component::Textarea::New(
        cx,
        StrDup(cx->a,
               fmt("gpui-component-textarea-%llu", (unsigned long long)handle)),
        &state->input);
    input_tokens::ApplyTextarea(binding, textarea);
    textarea->Disabled(request->disabled);
    EachMethod<Op>(request, [&](const Op& op) {
        switch (op.kind) {
            case Op::Appearance:
                textarea->Appearance(op.flag);
                break;
            case Op::Bordered:
                textarea->Bordered(op.flag);
                break;
            case Op::Readonly:
                textarea->Readonly(op.flag);
                break;
            case Op::AriaLabel:
                textarea->AriaLabel(op.text);
                break;
        }
    });
    Str error;
    if (!RequireLeaf(request->ChildrenLen(), &error))
        return request->Fail(error);
    textarea->refiner = request->TakeStyle();
    return input_tokens::Wrap(binding, cx, textarea->IntoEl());
}

// ─── State and recorders ───────────────────────────────────────────────────

static bool NewTextareaState(shell::StateBuild* build,
                             const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Optional)
        return build
            ->Fail(StrL("TextareaState expects an optional initial value"));
    Str value;
    if (const ComponentArgument* initial = args[0].Some()) {
        if (initial->kind != shell::ComponentArgumentKind::String)
            return build
                ->Fail(StrL("TextareaState initial_value expects text"));
        value = initial->string;
    }
    shell::TextStateInit(build->New<shell::TextStateEntity>(), build->app, true,
                         {}, value);
    return true;
}

static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Entity)
        return build->Fail(StrL("Textarea expects one TextareaState entity"));
    *build->New<ComponentArgument>() = args[0];
    return true;
}

// support.rs bool_method("Textarea", name, ...).
template <Op::Kind K>
static bool RecordBool(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    const char* name = K == Op::Appearance ? "appearance"
                       : K == Op::Bordered ? "bordered"
                                           : "readonly";
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(fmt("Textarea.%s expects one boolean", Str(name)));
    Op* op = build->New<Op>();
    op->kind = K;
    op->flag = args[0].boolean;
    return true;
}

static bool RecordAriaLabel(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String ||
        len(StrTrimAscii(args[0].string)) == 0) {
        return build->Fail(StrL("Textarea.aria_label expects non-empty text"));
    }
    Op* op = build->New<Op>();
    op->kind = Op::AriaLabel;
    op->text = args[0].string;
    return true;
}

// ─── Descriptors ───────────────────────────────────────────────────────────

static constexpr ArgumentSchema kInitialValue = SchemaString();
static constexpr ArgumentDescriptor kStateArgs[] = {
    {"initial_value", SchemaOptional(&kInitialValue)}};

static constexpr shell::StateDescriptor kState = {
    "TextareaState",
    "TextareaState",
    kStateArgs,
    "Retained multi-line editing state with an optional initial value.",
    &NewTextareaState,
    shell::kTextareaTokenStateMethods};

static constexpr ArgumentDescriptor kConstructorArgs[] = {
    {"state", SchemaEntity("TextareaState")}};
static constexpr ConstructorDescriptor kConstructors[] = {
    {"Textarea", kConstructorArgs, &Construct}};

static constexpr ArgumentDescriptor kDisabledArgs[] = {
    {"disabled", SchemaBoolean()}};
static constexpr ArgumentDescriptor kAppearanceArgs[] = {
    {"appearance", SchemaBoolean()}};
static constexpr ArgumentDescriptor kBorderedArgs[] = {
    {"bordered", SchemaBoolean()}};
static constexpr ArgumentDescriptor kReadonlyArgs[] = {
    {"readonly", SchemaBoolean()}};
static constexpr ArgumentDescriptor kAriaLabelArgs[] = {
    {"label", SchemaString()}};

static constexpr const char* kPolicyDoc =
    "Sets the corresponding native textarea presentation or editing policy.";

static constexpr MethodDescriptor kMethods[] = {
    // Rust records `()`: `disabled` is a common behavior the materializer
    // reads from the request.
    {"disabled", kDisabledArgs, "Sets the common disabled state.",
     &RecordCommonBehavior},
    {"appearance", kAppearanceArgs, kPolicyDoc, &RecordBool<Op::Appearance>},
    {"bordered", kBorderedArgs, kPolicyDoc, &RecordBool<Op::Bordered>},
    {"readonly", kReadonlyArgs, kPolicyDoc, &RecordBool<Op::Readonly>},
    {"aria_label", kAriaLabelArgs, "Sets the accessibility label.",
     &RecordAriaLabel},
    input_tokens::kTokenMethod,
    input_tokens::kTokenClickMethod,
    input_tokens::kChangeMethod,
};

static constexpr ComponentDescriptor kDescriptor = {
    "Textarea", kConstructors, kMethods,
    "A retained native multi-line text editor. Shell style and common "
    "disabled state are honored; children are rejected.",
    &Materialize};

} // namespace gpui::component_shell::layout::textarea

namespace gpui::component_shell {

bool RegisterLayoutTextarea(shell::ComponentRegistry* registry,
                            shell::RegistryError* error) {
    return registry->RegisterState(&layout::textarea::kState, error) &&
           registry->Register(&layout::textarea::kDescriptor, error);
}

} // namespace gpui::component_shell
