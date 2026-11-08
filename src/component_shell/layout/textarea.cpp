// crates/component-shell/src/shell/layout/textarea.rs

#include "component_shell/families.h"
#include "component_shell/input_tokens.h"
#include "component_shell/layout/mod.h"
#include "component_shell/text_editing.h"

namespace gpui::component_shell::layout::textarea {

using text_editing::Op;

static constexpr char kName[] = "Textarea";

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
    text_editing::ApplyOps(request, textarea);
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
    return text_editing::RecordState(build, kName, "TextareaState", args,
                                     count);
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
    shell::kTextareaTokenStateMethods,
    true};

static constexpr ArgumentDescriptor kConstructorArgs[] = {
    {"state", SchemaEntity("TextareaState")}};
static constexpr ConstructorDescriptor kConstructors[] = {
    {"Textarea", kConstructorArgs, &Construct}};

using text_editing::kAppearanceArgs;
using text_editing::kAriaLabelArgs;
using text_editing::kBorderedArgs;
using text_editing::kReadonlyArgs;

static constexpr const char* kPolicyDoc =
    "Sets the corresponding native textarea presentation or editing policy.";

static constexpr MethodDescriptor kMethods[] = {
    // Rust records `()`: `disabled` is a common behavior the materializer
    // reads from the request.
    {"disabled", kDisabledArguments, "Sets the common disabled state.",
     &RecordCommonBehavior},
    {"appearance", kAppearanceArgs, kPolicyDoc,
     &text_editing::RecordBoolFor<kName, Op::Appearance>},
    {"bordered", kBorderedArgs, kPolicyDoc,
     &text_editing::RecordBoolFor<kName, Op::Bordered>},
    {"readonly", kReadonlyArgs, kPolicyDoc,
     &text_editing::RecordBoolFor<kName, Op::Readonly>},
    {"aria_label", kAriaLabelArgs, "Sets the accessibility label.",
     &text_editing::RecordAriaLabelFor<kName>},
    input_tokens::kTokenMethod,
    input_tokens::kTokenClickMethod,
    input_tokens::kTokenHoverMethod,
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
