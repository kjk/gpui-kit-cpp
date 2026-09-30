/* Ports of crates/shell/src/component_registry.rs's tests and of
   crates/component-shell's: src/lib.rs, the family modules' own tests, and
   the tests/*_host.rs files as far as each is reachable without
   TestAppContext. A host test here loads the script into a runtime built with
   the catalog and walks the element tree the first render builds. */

#include "Test.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

using shell::ArgumentDescriptor;
using shell::ArgumentSchema;
using shell::ComponentDescriptor;
using shell::ComponentRegistry;
using shell::ConstructorDescriptor;
using shell::FrozenComponentRegistry;
using shell::MethodDescriptor;
using shell::RegistryError;
using shell::RegistryErrorKind;
using shell::StateDescriptor;

namespace {

bool EmptyPayload(shell::PayloadBuild* build, const shell::ComponentArgument*,
                  int) {
    return build->Mark<int>();
}

El* EmptyMaterialize(shell::MaterializeRequest* request) {
    return Div(request->cx->a);
}

bool ZeroState(shell::StateBuild* build, const shell::ComponentArgument*, int) {
    *build->New<size_t>() = 0;
    return true;
}

ComponentRegistry* OpenRegistry() {
    ComponentRegistry* registry = new ComponentRegistry();
    RegistryError error;
    registry->Open(shell::kComponentRegistryApiVersion,
                   shell::kDefaultComponentModule, &error);
    return registry;
}

RegistryError Error(RegistryErrorKind kind, const char* component = nullptr,
                    const char* callable = nullptr,
                    const char* argument = nullptr,
                    const char* reason = nullptr) {
    RegistryError error;
    error.kind = kind;
    error.component = component;
    error.callable = callable;
    error.argument = argument;
    error.reason = reason;
    return error;
}

constexpr ConstructorDescriptor kNullaryButton[] = {
    {"Button", {}, &EmptyPayload}};

// A descriptor whose constructors and methods are the caller's.
ComponentDescriptor Descriptor(const char* name,
                               shell::Slice<ConstructorDescriptor> ctors,
                               shell::Slice<MethodDescriptor> methods = {}) {
    ComponentDescriptor descriptor;
    descriptor.name = name;
    descriptor.constructors = ctors;
    descriptor.methods = methods;
    descriptor.materialize = &EmptyMaterialize;
    return descriptor;
}

StateDescriptor State(const char* exportName, const char* kind,
                      shell::Slice<ArgumentDescriptor> arguments = {}) {
    StateDescriptor state;
    state.exportName = exportName;
    state.kind = kind;
    state.arguments = arguments;
    state.factory = &ZeroState;
    return state;
}

// component_registry.rs state_exports_share_the_component_export_namespace
void StateExportsShareTheComponentExportNamespace() {
    ComponentRegistry* registry = OpenRegistry();
    StateDescriptor state = State("InputState", "InputState");
    RegistryError error;
    utassert(registry->RegisterState(&state, &error));
    constexpr ConstructorDescriptor ctors[] = {
        {"InputState", {}, &EmptyPayload}};
    ComponentDescriptor input = Descriptor("Input", ctors);
    utassert(!registry->Register(&input, &error));
    RegistryError expected = Error(RegistryErrorKind::DuplicateExport);
    expected.callable = "InputState";
    utassert(error == expected);
    delete registry;
}

// state_descriptors_validate_export_kind_and_arguments
void StateDescriptorsValidateExportKindAndArguments() {
    ComponentRegistry* registry = OpenRegistry();
    RegistryError error;
    StateDescriptor reserved = State("class", "State");
    utassert(!registry->RegisterState(&reserved, &error));
    RegistryError expected = Error(RegistryErrorKind::InvalidExport);
    expected.callable = "class";
    utassert(error == expected);

    StateDescriptor badKind = State("State", "bad-kind");
    utassert(!registry->RegisterState(&badKind, &error));
    utassert(error == Error(RegistryErrorKind::InvalidStateKind, "bad-kind"));

    constexpr ArgumentDescriptor reservedArgument[] = {
        {"class", shell::SchemaString()}};
    StateDescriptor badArgument = State("State", "State", reservedArgument);
    utassert(!registry->RegisterState(&badArgument, &error));
    utassert(error == Error(RegistryErrorKind::InvalidArgument, "State",
                            "State", "class"));
    delete registry;

    registry = OpenRegistry();
    StateDescriptor first = State("FirstState", "SharedState");
    StateDescriptor second = State("SecondState", "SharedState");
    utassert(registry->RegisterState(&first, &error));
    utassert(!registry->RegisterState(&second, &error));
    utassert(error ==
             Error(RegistryErrorKind::DuplicateStateKind, "SharedState"));
    delete registry;
}

// frozen_registry_module_declares_state_exports
void FrozenRegistryModuleDeclaresStateExports() {
    ComponentRegistry* registry = OpenRegistry();
    StateDescriptor state = State("InputState", "InputState");
    RegistryError error;
    utassert(registry->RegisterState(&state, &error));
    FrozenComponentRegistry frozen;
    registry->Freeze(&frozen);
    Str source = frozen.JavaScriptModuleSource(StrL("test-proof"));
    utassert(StrContains(source, Str("export { InputState }")));
    StrFree(source);
    delete registry;
}

void Destroyed(void* value) {
    (*(int*)value)++;
}

// retained_state_store_enforces_its_limit_without_overwriting_live_state and
// releasing_an_application_recovers_retained_state_capacity
void RetainedStateStoreEnforcesItsLimit() {
    shell::ComponentStateStore store;
    int destroyed = 0;
    Arena* a = ArenaNew();
    uint64_t handle = 0;
    for (int i = 0; i < shell::kMaxRetainedComponentStates; i++) {
        utassert(store.Insert("State", nullptr, &destroyed, &Destroyed, &handle,
                              nullptr, a));
    }
    Str error;
    utassert(!store.Insert("State", nullptr, &destroyed, &Destroyed, &handle,
                           &error, a));
    utassert(StrContains(error, Str("limit")));
    // The refused value is destroyed rather than leaked.
    utassert(destroyed == 1);
    utassert(store.Kind(0) && strcmp(store.Kind(0), "State") == 0);
    store.Clear();

    int owner = 91;
    destroyed = 0;
    for (int i = 0; i < shell::kMaxRetainedComponentStates; i++) {
        store.Insert("State", &owner, &destroyed, &Destroyed, &handle, nullptr,
                     a);
    }
    store.ReleaseApplication(&owner);
    utassert(store.Len() == 0);
    utassert(destroyed == shell::kMaxRetainedComponentStates);
    utassert(
        store.Insert("State", nullptr, nullptr, nullptr, &handle, nullptr, a));
    ArenaDelete(a);
}

// duplicate_methods_are_rejected_with_the_component_name
void DuplicateMethodsAreRejectedWithTheComponentName() {
    ComponentRegistry* registry = OpenRegistry();
    constexpr MethodDescriptor methods[] = {
        {"disabled", {}, "Disables the button.", &EmptyPayload},
        {"disabled", {}, "Disables the button, again.", &EmptyPayload},
    };
    ComponentDescriptor button = Descriptor("Button", kNullaryButton, methods);
    RegistryError error;
    utassert(!registry->Register(&button, &error));
    utassert(error ==
             Error(RegistryErrorKind::DuplicateMethod, "Button", "disabled"));
    delete registry;
}

// duplicate_exports_inside_one_descriptor_are_rejected
void DuplicateExportsInsideOneDescriptorAreRejected() {
    ComponentRegistry* registry = OpenRegistry();
    constexpr ConstructorDescriptor ctors[] = {{"Button", {}, &EmptyPayload},
                                               {"Button", {}, &EmptyPayload}};
    ComponentDescriptor button = Descriptor("Button", ctors);
    RegistryError error;
    utassert(!registry->Register(&button, &error));
    RegistryError expected = Error(RegistryErrorKind::DuplicateExport);
    expected.callable = "Button";
    utassert(error == expected);
    delete registry;
}

// invalid_and_reserved_javascript_exports_are_rejected
void InvalidAndReservedJavaScriptExportsAreRejected() {
    static constexpr ConstructorDescriptor invalid[] = {
        {"not-valid", {}, &EmptyPayload}};
    static constexpr ConstructorDescriptor reserved[] = {
        {"class", {}, &EmptyPayload}};
    const shell::Slice<ConstructorDescriptor> variants[] = {invalid, reserved};
    for (shell::Slice<ConstructorDescriptor> ctors : variants) {
        ComponentRegistry* registry = OpenRegistry();
        ComponentDescriptor button = Descriptor("Button", ctors);
        RegistryError error;
        utassert(!registry->Register(&button, &error));
        RegistryError expected = Error(RegistryErrorKind::InvalidExport);
        expected.callable = ctors[0].exportName;
        utassert(error == expected);
        delete registry;
    }
}

// method_and_argument_names_must_be_javascript_identifiers
void MethodAndArgumentNamesMustBeJavaScriptIdentifiers() {
    RegistryError error;
    ComponentRegistry* registry = OpenRegistry();
    ComponentDescriptor badName = Descriptor("not-valid", kNullaryButton);
    utassert(!registry->Register(&badName, &error));
    utassert(error == Error(RegistryErrorKind::InvalidComponent, "not-valid"));
    delete registry;

    registry = OpenRegistry();
    constexpr MethodDescriptor badMethods[] = {
        {"not-valid", {}, nullptr, &EmptyPayload}};
    ComponentDescriptor badMethod =
        Descriptor("Button", kNullaryButton, badMethods);
    utassert(!registry->Register(&badMethod, &error));
    utassert(error ==
             Error(RegistryErrorKind::InvalidMethod, "Button", "not-valid"));
    delete registry;

    registry = OpenRegistry();
    static constexpr ArgumentDescriptor classArgument[] = {
        {"class", shell::SchemaString()}};
    constexpr ConstructorDescriptor ctors[] = {
        {"Button", classArgument, &EmptyPayload}};
    ComponentDescriptor badArgument = Descriptor("Button", ctors);
    utassert(!registry->Register(&badArgument, &error));
    utassert(error == Error(RegistryErrorKind::InvalidArgument, "Button",
                            "Button", "class"));
    delete registry;
}

// required_arguments_cannot_follow_optional_arguments
void RequiredArgumentsCannotFollowOptionalArguments() {
    static constexpr ArgumentSchema string = shell::SchemaString();
    static constexpr ArgumentDescriptor arguments[] = {
        {"label", shell::SchemaOptional(&string)},
        {"id", shell::SchemaString()}};
    constexpr ConstructorDescriptor ctors[] = {
        {"Button", arguments, &EmptyPayload}};
    ComponentRegistry* registry = OpenRegistry();
    ComponentDescriptor button = Descriptor("Button", ctors);
    RegistryError error;
    utassert(!registry->Register(&button, &error));
    utassert(error == Error(RegistryErrorKind::RequiredArgumentAfterOptional,
                            "Button", "Button", "id"));
    delete registry;
}

// argument_schemas_are_validated_recursively
void ArgumentSchemasAreValidatedRecursively() {
    static constexpr const char* none[1] = {nullptr};
    static constexpr ArgumentSchema emptyEnum = {
        shell::SchemaKind::Enum, nullptr, {none, 0}, nullptr};
    static constexpr const char* repeated[] = {"quiet", "quiet"};
    static constexpr ArgumentSchema repeatedEnum = shell::SchemaEnum(repeated);
    static constexpr ArgumentSchema repeatedArray =
        shell::SchemaArray(&repeatedEnum);
    static constexpr const char* blank[] = {""};
    static constexpr ArgumentSchema blankEnum = shell::SchemaEnum(blank);
    static constexpr ArgumentSchema string = shell::SchemaString();
    static constexpr ArgumentSchema nestedOptional =
        shell::SchemaOptional(&string);
    struct Case {
        ArgumentSchema schema;
        const char* reason;
    };
    const Case cases[] = {
        {shell::SchemaArray(&emptyEnum),
         "enum must contain at least one literal"},
        {shell::SchemaOptional(&repeatedArray), "enum literals must be unique"},
        {shell::SchemaArray(&blankEnum), "enum literals must not be empty"},
        {shell::SchemaArray(&nestedOptional),
         "optional schemas are only valid for top-level arguments"},
        {shell::SchemaEntity(""), "entity kind must not be empty"},
        {shell::SchemaCallback(" "), "callback signature must not be empty"},
    };
    for (const Case& c : cases) {
        ArgumentDescriptor arguments[] = {{"value", c.schema}};
        ConstructorDescriptor ctors[] = {{"Button", arguments, &EmptyPayload}};
        ComponentRegistry* registry = OpenRegistry();
        ComponentDescriptor button = Descriptor("Button", ctors);
        RegistryError error;
        utassert(!registry->Register(&button, &error));
        utassert(error == Error(RegistryErrorKind::InvalidArgumentSchema,
                                "Button", "Button", "value", c.reason));
        delete registry;
    }
}

// one_signature_cannot_repeat_an_argument_name
void OneSignatureCannotRepeatAnArgumentName() {
    static constexpr ArgumentDescriptor arguments[] = {
        {"value", shell::SchemaString()}, {"value", shell::SchemaBoolean()}};
    constexpr ConstructorDescriptor ctors[] = {
        {"Button", arguments, &EmptyPayload}};
    ComponentRegistry* registry = OpenRegistry();
    ComponentDescriptor button = Descriptor("Button", ctors);
    RegistryError error;
    utassert(!registry->Register(&button, &error));
    utassert(error == Error(RegistryErrorKind::DuplicateArgument, "Button",
                            "Button", "value"));
    delete registry;
}

// deprecated_exports_must_name_another_export_from_the_same_descriptor
void DeprecatedExportsMustNameAnotherExport() {
    const char* const replacements[] = {"MissingButton", "OldButton"};
    for (const char* replacement : replacements) {
        ConstructorDescriptor ctors[] = {
            {"Button", {}, &EmptyPayload},
            {"OldButton", {}, &EmptyPayload, replacement, "Use Button."}};
        ComponentRegistry* registry = OpenRegistry();
        ComponentDescriptor button = Descriptor("Button", ctors);
        RegistryError error;
        utassert(!registry->Register(&button, &error));
        utassert(error ==
                 Error(RegistryErrorKind::InvalidDeprecationReplacement,
                       "Button", "OldButton", replacement));
        delete registry;
    }
}

// ─── MaterializeRequest ────────────────────────────────────────────────────

// A request over one registered node with `children` plain children and the
// named slots, in a description of its own.
struct RequestFixture {
    App app;
    Window window;
    Arena* a = nullptr;
    Ctx cx;
    shell::SpecArena specs;
    shell::SpecId id = 0;
    shell::MaterializeRequest request;
    ComponentDescriptor descriptor = {};

    RequestFixture(int children, const char* const* slots, int slotCount) {
        window.app = &app;
        a = ArenaNew();
        cx.app = &app;
        cx.win = &window;
        cx.a = a;
        shell::Component registered = {};
        registered.kind = shell::ComponentKind::Registered;
        registered.text = StrL("Slotted");
        id = specs.Push(registered);
        for (int i = 0; i < children; i++) {
            shell::SpecId child = specs.Push(shell::Component{});
            specs.Attach(id, child);
        }
        for (int i = 0; i < slotCount; i++) {
            shell::SpecId slot = specs.Push(shell::Component{});
            specs.Claim(slot);
            shell::SpecOp op = {};
            op.kind = shell::SpecOpKind::Slot;
            op.name = Str(slots[i]);
            op.node = slot;
            specs.PushOp(id, op);
        }
        descriptor.name = "Slotted";
        request.cx = &cx;
        request.specs = &specs;
        request.node = specs.Node(id);
        request.id = id;
        request.descriptor = &descriptor;
    }
    ~RequestFixture() {
        ArenaDelete(a);
        AppGlobalClear(&app);
    }
};

// materialize_request_keeps_named_slots_separate_until_the_adapter_takes_them
void MaterializeRequestKeepsNamedSlotsSeparate() {
    const char* slots[] = {"trigger"};
    RequestFixture f(1, slots, 1);
    shell::MaterializeRequest& request = f.request;
    utassert(!request.styleTaken);
    utassert(request.ChildrenLen() == 1);
    utassert(request.TakeSlot("content") == nullptr);
    utassert(request.TakeSlot("trigger") != nullptr);
    utassert(!request.TakeSlotFactory("trigger").IsSet());
    El** children = nullptr;
    int count = 0;
    utassert(request.TakeChildren(&children, &count) && count == 1);
    utassert(request.ChildrenLen() == 0);
}

// materialize_request_takes_every_repeated_named_slot_in_order
void MaterializeRequestTakesEveryRepeatedNamedSlotInOrder() {
    const char* slots[] = {"content", "content", "trigger"};
    RequestFixture f(0, slots, 3);
    shell::MaterializeRequest& request = f.request;
    El** taken = nullptr;
    utassert(request.TakeSlots("content", &taken) == 2);
    utassert(request.TakeSlot("content") == nullptr);
    utassert(request.TakeSlot("trigger") != nullptr);
}

// typed_child_failure_keeps_the_token_retryable_and_unread and
// repeated_child_spec_ids_receive_independent_exactly_once_tokens: here a
// child cannot fail to materialize, so what is left is exactly-once.
void TypedChildrenMaterializeExactlyOnce() {
    RequestFixture f(2, nullptr, 0);
    shell::MaterializeRequest& request = f.request;
    shell::ComponentChild* children = nullptr;
    int count = 0;
    utassert(request.TakeTypedChildren(&children, &count) && count == 2);
    utassert(children[0].componentName == nullptr);
    utassert(request.ChildrenLen() == 2);
    utassert(request.MaterializeChild(&children[0]) != nullptr);
    utassert(request.MaterializeChild(&children[0]) == nullptr);
    utassert(StrContains(request.failure, Str("already consumed")));
    utassert(request.MaterializeChild(&children[1]) != nullptr);
    utassert(request.ChildrenLen() == 0);
    shell::ComponentChild foreign = {};
    utassert(request.MaterializeChild(&foreign) == nullptr);
}

// child_lane_stays_exclusive_after_either_lane_is_drained
void ChildLaneStaysExclusiveAfterEitherLaneIsDrained() {
    {
        RequestFixture f(1, nullptr, 0);
        El** children = nullptr;
        int count = 0;
        utassert(f.request.TakeChildren(&children, &count) && count == 1);
        shell::ComponentChild* typed = nullptr;
        utassert(!f.request.TakeTypedChildren(&typed, &count));
    }
    {
        RequestFixture f(1, nullptr, 0);
        shell::ComponentChild* typed = nullptr;
        int count = 0;
        utassert(f.request.TakeTypedChildren(&typed, &count) && count == 1);
        utassert(f.request.MaterializeChild(&typed[0]) != nullptr);
        El** children = nullptr;
        utassert(!f.request.TakeChildren(&children, &count));
    }
}

// ─── crates/component-shell/src/lib.rs ─────────────────────────────────────

// register_exposes_the_first_leaf_component_batch_in_stable_order
void RegisterExposesTheFirstLeafBatchInStableOrder() {
    const FrozenComponentRegistry* frozen = component_shell::Components();
    utassert(frozen->DescriptorCount() >= 3);
    const char* names[] = {"Spinner", "Separator", "Skeleton"};
    for (int i = 0; i < 3; i++) {
        utassert(strcmp(frozen->Descriptor((uint32_t)i)->name, names[i]) == 0);
    }
    const char* exports[] = {"Spinner",
                             "Separator",
                             "VerticalSeparator",
                             "DashedSeparator",
                             "VerticalDashedSeparator",
                             "Skeleton"};
    int at = 0;
    for (int i = 0; i < 3; i++) {
        for (const ConstructorDescriptor& ctor : frozen->Descriptor((uint32_t)i)
                                                     ->constructors) {
            utassert(at < 6 && strcmp(ctor.exportName, exports[at]) == 0);
            at++;
        }
    }
    utassert(at == 6);
    for (int i = 0; i < frozen->DescriptorCount(); i++) {
        utassert(frozen->Descriptor((uint32_t)i)->documentation != nullptr);
    }
}

// leaf_descriptors_publish_only_closed_honest_method_schemas
void LeafDescriptorsPublishOnlyClosedHonestMethodSchemas() {
    const FrozenComponentRegistry* frozen = component_shell::Components();
    const ComponentDescriptor* spinner = frozen->Find(StrL("Spinner"));
    utassert(spinner && spinner->methods.count == 4);
    if (spinner && spinner->methods.count == 4) {
        const char* names[] = {"size", "icon", "color", "ease"};
        for (int i = 0; i < 4; i++) {
            const MethodDescriptor& m = spinner->methods[i];
            utassert(strcmp(m.name, names[i]) == 0);
            utassert(m.arguments.count == 1 &&
                     strcmp(m.arguments[0].name, names[i]) == 0);
        }
        utassert(spinner->methods[0].arguments[0].schema.values.count == 4);
        utassert(spinner->methods[1].arguments[0].schema.values.count == 2);
        utassert(spinner->methods[2].arguments[0].schema.kind ==
                 shell::SchemaKind::String);
        utassert(spinner->methods[3].arguments[0].schema.values.count == 3);
    }
    const ComponentDescriptor* separator = frozen->Find(StrL("Separator"));
    utassert(separator && separator->methods.count == 3 &&
             strcmp(separator->methods[0].name, "label") == 0 &&
             strcmp(separator->methods[1].name, "color") == 0 &&
             strcmp(separator->methods[2].name, "dashed") == 0);
    const ComponentDescriptor* skeleton = frozen->Find(StrL("Skeleton"));
    utassert(skeleton && skeleton->methods.count == 1 &&
             strcmp(skeleton->methods[0].name, "secondary") == 0);
    for (int i = 0; i < frozen->DescriptorCount(); i++) {
        for (const MethodDescriptor& m : frozen->Descriptor((uint32_t)i)
                                             ->methods) {
            utassert(m.documentation != nullptr);
        }
    }
}

static bool HasUpper(const char* name) {
    for (const char* at = name; at && *at; at++) {
        if (*at >= 'A' && *at <= 'Z') return true;
    }
    return false;
}

// descriptor_vocabulary_uses_snake_case_everywhere
void DescriptorVocabularyUsesSnakeCaseEverywhere() {
    const FrozenComponentRegistry* frozen = component_shell::Components();
    int nonSnake = 0;
    for (int i = 0; i < frozen->DescriptorCount(); i++) {
        const ComponentDescriptor* d = frozen->Descriptor((uint32_t)i);
        for (const ConstructorDescriptor& ctor : d->constructors) {
            for (const ArgumentDescriptor& argument : ctor.arguments)
                if (HasUpper(argument.name)) nonSnake++;
        }
        for (const MethodDescriptor& m : d->methods) {
            if (HasUpper(m.name)) nonSnake++;
            for (const ArgumentDescriptor& argument : m.arguments)
                if (HasUpper(argument.name)) nonSnake++;
        }
    }
    utassert(nonSnake == 0);
}

// runtime_typings_include_leaf_exports_and_methods and
// adapter_runtime_owns_the_registered_component_catalog
void RuntimeTypingsIncludeLeafExportsAndMethods() {
    StrBuilder out;
    shell::AppendComponentDeclarations(&out, component_shell::Components(),
                                       Str{});
    Str declarations = out.TakeStr();
    const char* expected[] = {
        "export const Spinner: { new(): SpinnerElement };",
        "size(size: \"xsmall\" | \"small\" | \"medium\" | \"large\"): "
        "SpinnerElement;",
        "export const VerticalDashedSeparator: { new(): SeparatorElement };",
        "label(label: string): SeparatorElement;",
        "secondary(): SkeletonElement;",
        "export const Skeleton: { new(): SkeletonElement };",
    };
    for (const char* line : expected) {
        utassert(StrContains(declarations, Str(line)));
    }
    StrFree(declarations);
}

// The declarations one descriptor or state contributes, without the module
// around them: what the Rust catalog's block must contain verbatim.
Str OneDeclaration(const ComponentDescriptor* descriptor,
                   const StateDescriptor* state) {
    ComponentRegistry registry;
    RegistryError error;
    registry.Open(shell::kComponentRegistryApiVersion,
                  shell::kDefaultComponentModule, &error);
    if (descriptor) registry.Register(descriptor, &error);
    if (state) registry.RegisterState(state, &error);
    FrozenComponentRegistry frozen;
    registry.Freeze(&frozen);
    StrBuilder out;
    shell::AppendComponentDeclarations(&out, &frozen, Str{});
    Str text = out.TakeStr();
    // Past `declare module ...` and its import line, and before `}\n\n`.
    int at = 0;
    for (int lines = 0; lines < 2 && at < len(text); at++) {
        if (text.s[at] == '\n') lines++;
    }
    Str body = StrDup(Str(text.s + at, len(text) - at - 3));
    StrFree(text);
    return body;
}

// The catalog's declarations are byte-for-byte what `gpui-component-shell
// types` writes (tests/ComponentShellTypesData.cpp, generated from the pinned
// crate), which is what keeps every export, method, argument and document
// sentence a script sees the same on both runtimes.
void ComponentDeclarationsMatchRust() {
    StrBuilder rust;
    AppendRustComponentDeclarations(&rust);
    Str expected = rust.TakeStr();
    const FrozenComponentRegistry* frozen = component_shell::Components();
    for (int i = 0; i < frozen->DescriptorCount(); i++) {
        Str one = OneDeclaration(frozen->Descriptor((uint32_t)i), nullptr);
        bool found = StrContains(expected, one);
        if (!found)
            printf("declaration differs from Rust: %s\n",
                   frozen->Descriptor((uint32_t)i)->name);
        utassert(found);
        StrFree(one);
    }
    for (int i = 0; i < frozen->StateCount(); i++) {
        Str one = OneDeclaration(nullptr, frozen->State(i));
        bool found = StrContains(expected, one);
        if (!found)
            printf("declaration differs from Rust: %s\n", frozen->State(i)
                                                              ->exportName);
        utassert(found);
        StrFree(one);
    }
    StrFree(expected);
}

// ─── Family tests: spinner.rs, separator.rs, skeleton.rs ───────────────────

// The request a family materializer is handed for a node recorded from
// `name` with `payload`, and nothing else.
El* MaterializeDirect(const char* name, shell::ComponentPayload payload,
                      Str* failure) {
    const ComponentDescriptor* descriptor = component_shell::Components()
                                                ->Find(Str(name));
    if (!descriptor) return nullptr;
    RequestFixture f(0, nullptr, 0);
    f.request.descriptor = descriptor;
    f.request.payload = payload;
    f.request.elementId = StrL("direct");
    El* element = descriptor->materialize(&f.request);
    if (failure) *failure = StrDup(f.request.failure);
    return element;
}

void LeafComponentsRejectAnIncompatiblePayload() {
    const char* const names[] = {"Spinner", "Separator", "Skeleton"};
    for (const char* name : names) {
        Str failure;
        utassert(MaterializeDirect(name, shell::ComponentPayload{}, &failure) ==
                 nullptr);
        utassert(StrEq(failure,
                       fmt("%s received an incompatible payload", Str(name))));
        StrFree(failure);
    }
}

// ─── Host tests ────────────────────────────────────────────────────────────

El* FindText(El* element, Str text) {
    if (!element) return nullptr;
    if (element->kind == ElKind::Text && StrEq(element->text, text))
        return element;
    for (El* child = element->first; child; child = child->next) {
        if (El* found = FindText(child, text)) return found;
    }
    return nullptr;
}

// Loads `source` into a runtime built with the catalog, renders it once and
// answers the root element (null with `error` set when it failed).
struct Host {
    App app;
    Window window;
    ShellRuntime* runtime = nullptr;
    Entity<ScriptView> view = {};
    Arena* frame = nullptr;
    ShellError error = {};

    explicit Host(Str source) {
        window.app = &app;
        component_shell::Init(&app);
        runtime =
            ShellRuntime::New(&app, &error, component_shell::Components());
        ViewType* type =
            runtime ? runtime->LoadSource(StrL("main.js"), source, &error)
                    : nullptr;
        if (type) view = ScriptView::New(&app, runtime, type);
        ViewTypeRelease(type);
        frame = ArenaNew();
        window.frameArena = frame;
    }
    El* Render() {
        if (!view.IsValid()) return nullptr;
        return EntityRender(&app, &window, frame, view.id);
    }
    Str ViewError() {
        ScriptView* script = view.Get(&app);
        return script ? script->error.message : Str{};
    }
    ~Host() {
        if (view.IsValid()) EntityDrop(&app, view.id);
        ArenaDelete(frame);
        if (runtime) runtime->Release();
        ShellErrorClear(&error);
        AppGlobalClear(&app);
    }
};

void LeafComponentsMaterializeFromAScript() {
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { Spinner, DashedSeparator, Separator, Skeleton } from "
             "'gpui-component';\n"
             "export default class Main extends View {\n"
             "  render() {\n"
             "    return div().w(300)\n"
             "      .child(new Spinner().size('large').icon('loader_circle')"
             ".color('#ff0000').ease('linear').w(20))\n"
             "      .child(new DashedSeparator().label('Account'))\n"
             "      .child(new Separator().color('blue-600').dashed())\n"
             "      .child(new Skeleton().secondary().h(10));\n"
             "  }\n"
             "}\n"));
    El* root = host.Render();
    utassert(root != nullptr && !host.error.IsSet());
    utassert(len(host.ViewError()) == 0);
    utassert(FindText(root, StrL("Account")) != nullptr);
}

void RegisteredCallsAreValidatedAgainstTheirSchemas() {
    struct Case {
        const char* call;
        const char* message;
    };
    const Case cases[] = {
        {"new Spinner().size('huge')",
         "size(size) expects `xsmall`, `small`, `medium`, `large`"},
        {"new Spinner().color(3)", "color(color) expects a string"},
        {"new Spinner().color('not a color')", "invalid Spinner color"},
        {"new Spinner('extra')", "Spinner(...) expects at most 0 arguments"},
        {"new Spinner().track_focus({ __handle: 1 })",
         "unknown element method `track_focus`"},
        {"new Spinner().on_click(() => {})",
         "unknown element method `on_click`"},
    };
    for (const Case& c : cases) {
        TempStr source =
            fmt("import { View, div } from 'gpui-kit';\n"
                "import { Spinner } from 'gpui-component';\n"
                "export default class Main extends View {\n"
                "  render() { return div().child(%s); }\n"
                "}\n",
                Str(c.call));
        Host host(source);
        host.Render();
        Str message = host.ViewError();
        if (!len(message)) message = host.error.message;
        utassert(StrContains(message, Str(c.message)));
    }
}

// ─── Shared helpers for the family tests below ─────────────────────────────

// A catalog holding only what `family` registers, the way each family's own
// mod.rs test builds one.
struct FamilyCatalog {
    FrozenComponentRegistry frozen;
    bool ok = false;

    explicit FamilyCatalog(component_shell::RegisterFamily family) {
        ComponentRegistry registry;
        RegistryError error;
        registry.Open(shell::kComponentRegistryApiVersion,
                      shell::kDefaultComponentModule, &error);
        ok = family(&registry, &error);
        registry.Freeze(&frozen);
    }
    bool NamesAre(const char* const* names, int count) const {
        if (frozen.DescriptorCount() != count) return false;
        for (int i = 0; i < count; i++) {
            if (strcmp(frozen.Descriptor((uint32_t)i)->name, names[i]) != 0)
                return false;
        }
        return true;
    }
    bool Documented() const {
        for (int i = 0; i < frozen.DescriptorCount(); i++) {
            const ComponentDescriptor* d = frozen.Descriptor((uint32_t)i);
            if (!d->documentation) return false;
            for (const MethodDescriptor& m : d->methods)
                if (!m.documentation) return false;
        }
        return true;
    }
};

// The payload a registered constructor records from `args`, in `a`; the
// failure message when it refuses, in `failure`.
shell::ComponentPayload BuildPayload(const char* component, int constructor,
                                     const shell::ComponentArgument* args,
                                     int count, Arena* a, Str* failure) {
    const ComponentDescriptor* d = component_shell::Components()
                                       ->Find(Str(component));
    shell::PayloadBuild build;
    build.a = a;
    if (!d || !d->constructors[constructor].factory(&build, args, count)) {
        if (failure) *failure = build.error;
        return {};
    }
    return build.out;
}

shell::ComponentArgument StringArgument(const char* value) {
    shell::ComponentArgument argument;
    argument.kind = shell::ComponentArgumentKind::String;
    argument.string = Str(value);
    return argument;
}

// What a materializer says for a node recorded from `payload` with
// `children` ordinary children and nothing else.
Str MaterializeFailure(const char* name, shell::ComponentPayload payload,
                       int children) {
    const ComponentDescriptor* descriptor = component_shell::Components()
                                                ->Find(Str(name));
    if (!descriptor) return {};
    RequestFixture f(children, nullptr, 0);
    f.request.descriptor = descriptor;
    f.request.payload = payload;
    f.request.elementId = StrL("direct");
    El* element = descriptor->materialize(&f.request);
    return element ? Str{} : StrDup(f.request.failure);
}

// The error a script sees when it renders `call` from the catalog: a
// constructor's or method's TypeError, or a schema refusal.
Str CallErrorTemp(const char* imports, const char* call) {
    TempStr source =
        fmt("import { View, div } from 'gpui-kit';\n"
            "import { %s } from 'gpui-component';\n"
            "export default class Main extends View {\n"
            "  render() { return div().child(%s); }\n"
            "}\n",
            Str(imports), Str(call));
    Host host(source);
    host.Render();
    Str message = host.ViewError();
    if (!len(message)) message = host.error.message;
    return fmt("%s", message);
}

// Whether any text in the tree starts with `prefix`.
El* FindTextPrefix(El* element, Str prefix) {
    if (!element) return nullptr;
    if (element->kind == ElKind::Text && StrStartsWith(element->text, prefix))
        return element;
    for (El* child = element->first; child; child = child->next) {
        if (El* found = FindTextPrefix(child, prefix)) return found;
    }
    return nullptr;
}

// Renders `call` once and answers whether it built without an error.
bool RendersCleanly(const char* imports, const char* call,
                    const char* expectText = nullptr) {
    TempStr source =
        fmt("import { View, div } from 'gpui-kit';\n"
            "import { %s } from 'gpui-component';\n"
            "export default class Main extends View {\n"
            "  render() { return div().child(%s); }\n"
            "}\n",
            Str(imports), Str(call));
    Host host(source);
    El* root = host.Render();
    if (!root || host.error.IsSet() || len(host.ViewError()) != 0) return false;
    if (FindTextPrefix(root, StrL("Failed to render"))) return false;
    return !expectText || FindText(root, Str(expectText)) != nullptr;
}

El* FindParentOfText(El* element, Str text) {
    if (!element) return nullptr;
    for (El* child = element->first; child; child = child->next) {
        if (child->kind == ElKind::Text && StrEq(child->text, text))
            return element;
        if (El* found = FindParentOfText(child, text)) return found;
    }
    return nullptr;
}

// Every element carrying a click listener, in tree order: what a pointer
// press on each would dispatch.
int CollectListeners(El* element, El** out, int count, int cap) {
    if (!element) return count;
    if (element->listener.IsValid() && count < cap) out[count++] = element;
    for (El* child = element->first; child; child = child->next)
        count = CollectListeners(child, out, count, cap);
    return count;
}

// The nearest element on the path to `text` that takes a click.
El* ListenerAbove(El* element, Str text, El* best = nullptr) {
    if (!element) return nullptr;
    if (element->listener.IsValid()) best = element;
    if (element->kind == ElKind::Text && StrEq(element->text, text))
        return best;
    for (El* child = element->first; child; child = child->next) {
        if (El* found = ListenerAbove(child, text, best)) return found;
    }
    return nullptr;
}

void Click(Host& host, El* element) {
    ClickEvent event = {};
    ListenerCall(&host.app, &host.window, element->listener, &event);
}

// ─── controls/: mod.rs, action.rs, display.rs, text.rs ─────────────────────

// controls_register_the_supported_public_exports
void ControlsRegisterTheSupportedPublicExports() {
    FamilyCatalog catalog(&component_shell::RegisterControls);
    utassert(catalog.ok);
    const char* expected[] = {"Button", "Checkbox", "Switch", "Toggle", "Badge",
                              "Tag",    "Label",    "Link",   "Kbd"};
    int at = 0;
    bool same = true;
    for (int i = 0; i < catalog.frozen.DescriptorCount(); i++) {
        for (const ConstructorDescriptor& ctor :
             catalog.frozen.Descriptor((uint32_t)i)->constructors) {
            same = same && at < 9 && strcmp(ctor.exportName, expected[at]) == 0;
            at++;
        }
    }
    utassert(same && at == 9);
}

// every_control_uses_closed_argument_schemas_and_documents_its_surface
void EveryControlDocumentsItsSurface() {
    FamilyCatalog catalog(&component_shell::RegisterControls);
    utassert(catalog.Documented());
}

// identity_controls_reject_empty_ids, through the constructors that call it.
void IdentityControlsRejectEmptyIds() {
    utassert(StrContains(CallErrorTemp("Button", "new Button('')"),
                         StrL("Button id must not be empty")));
    utassert(StrContains(CallErrorTemp("Toggle", "new Toggle('')"),
                         StrL("Toggle id must not be empty")));
    utassert(StrContains(CallErrorTemp("Label", "new Label('')"),
                         StrL("Label text must not be empty")));
    utassert(RendersCleanly("Link", "new Link('save').child('Save')", "Save"));
}

// button_operations_replay_in_recorded_order_on_a_real_component
void ButtonOperationsReplayInRecordedOrder() {
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { Button } from 'gpui-component';\n"
             "export default class Main extends View {\n"
             "  render() { return div().child(new Button('ordered')"
             ".label('First').primary().label('Last').outline()); }\n"
             "}\n"));
    El* root = host.Render();
    utassert(root && len(host.ViewError()) == 0);
    utassert(FindText(root, StrL("Last")) != nullptr);
    utassert(FindText(root, StrL("First")) == nullptr);
}

// badge_numbers_reject_fractional_negative_and_overflow_values
void BadgeNumbersRejectFractionalNegativeAndOverflow() {
    utassert(RendersCleanly("Badge", "new Badge().count(7).child('inbox')",
                            "inbox"));
    utassert(StrContains(CallErrorTemp("Badge", "new Badge().count(1.5)"),
                         StrL("Badge.count expects a non-negative integer")));
    utassert(StrContains(CallErrorTemp("Badge", "new Badge().count(-1)"),
                         StrL("Badge.count expects a non-negative integer")));
    utassert(StrContains(CallErrorTemp("Badge", "new Badge().max(2 ** 64)"),
                         StrL("Badge.max expects a non-negative integer")));
}

// later_tag_variant_preserves_earlier_size_outline_and_rounding
void LaterTagVariantPreservesEarlierSizeOutlineAndRounding() {
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { Tag } from 'gpui-component';\n"
             "export default class Main extends View {\n"
             "  render() { return div().child(new Tag().size('large')"
             ".outline().rounded_full().variant('danger').child('x')); }\n"
             "}\n"));
    El* root = host.Render();
    utassert(root && len(host.ViewError()) == 0);
    El* tag = FindParentOfText(root, StrL("x"));
    utassert(tag != nullptr);
    if (tag) {
        const Theme& th = ThemeNow(&host.app);
        // Large, outlined, pill-shaped, and in the danger colours.
        utassertnear(tag->style.fontSize, 14.f);
        utassertnear(tag->style.radius, 16.f);
        utassert(RgbaEq(tag->style.borderColor, th.danger));
        utassert(RgbaEq(tag->style.color, th.danger));
    }
}

// badge_operations_materialize_a_real_component_in_recorded_order
void BadgeOperationsMaterializeInRecordedOrder() {
    utassert(RendersCleanly(
        "Badge", "new Badge().count(2).dot().count(9).child('inbox')",
        "inbox"));
}

// Kbd's constructor is gpui's Keystroke::parse, and its error is gpui's.
void KbdParsesItsKeystroke() {
    utassert(StrContains(CallErrorTemp("Kbd", "new Kbd('a-b')"),
                         StrL("invalid Kbd keystroke: Invalid keystroke "
                              "\"a-b\". Expected a sequence of modifiers")));
#if !GPUI_OS_MAC
    utassert(RendersCleanly("Kbd", "new Kbd('ctrl-s')", "Ctrl+S"));
    utassert(
        RendersCleanly("Kbd", "new Kbd('secondary-shift-P')", "Ctrl+Shift+P"));
#endif
}

// controls_host.rs: clicking_a_two_state_control_reports_its_new_state. The
// click is the listener each control put on its element, invoked the way a
// press on it would be.
void ClickingATwoStateControlReportsItsNewState() {
    Host host(StrL(
        "import { View, div } from 'gpui-kit';\n"
        "import { Checkbox, Switch, Toggle } from 'gpui-component';\n"
        "export default class App extends View {\n"
        "  init(_props, _cx) { this.checkbox = false; this.switch = false; "
        "this.toggle = false; }\n"
        "  render() {\n"
        "    return div().size_full()\n"
        "      .child(new Checkbox('cb').label('Checkbox')"
        ".checked(this.checkbox)\n"
        "        .on_change((checked, cx) => { this.checkbox = checked; "
        "cx.notify(); }))\n"
        "      .child(new Switch('sw').label('Switch').checked(this.switch)\n"
        "        .on_change((checked, cx) => { this.switch = checked; "
        "cx.notify(); }))\n"
        "      .child(new Toggle('tg').label('Toggle').checked(this.toggle)\n"
        "        .on_change((checked, cx) => { this.toggle = checked; "
        "cx.notify(); }))\n"
        "      .child(`state: ${this.checkbox}|${this.switch}|"
        "${this.toggle}`);\n"
        "  }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && len(host.ViewError()) == 0);
    utassert(FindText(root, StrL("state: false|false|false")) != nullptr);
    const char* labels[] = {"Checkbox", "Switch", "Toggle"};
    for (const char* label : labels) {
        El* control = ListenerAbove(root, Str(label));
        utassert(control != nullptr);
        if (control) Click(host, control);
        root = host.Render();
    }
    utassert(FindText(root, StrL("state: true|true|true")) != nullptr);
    // And back: each reports the state its click produces from the one it
    // was rendered with.
    El* checkbox = ListenerAbove(root, StrL("Checkbox"));
    if (checkbox) Click(host, checkbox);
    root = host.Render();
    utassert(FindText(root, StrL("state: false|true|true")) != nullptr);
}

// controls_host.rs: clicking_a_button_reaches_the_script
void ClickingAButtonReachesTheScript() {
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { Button } from 'gpui-component';\n"
             "export default class App extends View {\n"
             "  init(_props, _cx) { this.hits = 0; }\n"
             "  render() {\n"
             "    return div().size_full()\n"
             "      .child(new Button('press').primary().label('Press me')\n"
             "        .on_click((_event, cx) => { this.hits++; cx.notify(); "
             "}))\n"
             "      .child(`hits: ${this.hits}`);\n"
             "  }\n"
             "}\n"));
    El* root = host.Render();
    utassert(root && FindText(root, StrL("hits: 0")) != nullptr);
    El* button = ListenerAbove(root, StrL("Press me"));
    utassert(button != nullptr);
    if (button) Click(host, button);
    root = host.Render();
    utassert(FindText(root, StrL("hits: 1")) != nullptr);
}

// ─── display/: mod.rs, common.rs and the seven modules ─────────────────────

// registers_the_display_catalog_with_documented_callables
void RegistersTheDisplayCatalogWithDocumentedCallables() {
    FamilyCatalog catalog(&component_shell::RegisterDisplay);
    utassert(catalog.ok);
    const char* names[] = {"Alert",  "Breadcrumb", "Clipboard", "GroupBox",
                           "Rating", "StatusBar",  "Toolbar"};
    utassert(catalog.NamesAre(names, 7));
    utassert(catalog.Documented());
}

// common.rs rejects_an_empty_component_id, through the constructors.
void DisplayRejectsAnEmptyComponentId() {
    utassert(StrContains(CallErrorTemp("Rating", "new Rating('')"),
                         StrL("Rating id must not be empty")));
    utassert(StrContains(CallErrorTemp("InfoAlert", "new InfoAlert('', 'x')"),
                         StrL("Alert id must not be empty")));
    utassert(StrContains(CallErrorTemp("Toolbar", "new Toolbar('')"),
                         StrL("Toolbar id must not be empty")));
}

// builds_real_* and alert_payload_and_operations_build_the_real_component:
// every display component, with its operations, from a script.
void DisplayComponentsBuildFromAScript() {
    Host host(StrL(
        "import { View, div } from 'gpui-kit';\n"
        "import { WarningAlert, Breadcrumb, Clipboard, GroupBox, Rating, "
        "StatusBar, Toolbar } from 'gpui-component';\n"
        "export default class Main extends View {\n"
        "  render() {\n"
        "    return div().w(600)\n"
        "      .child(new WarningAlert('network', 'Offline')"
        ".title('Connection').banner().visible(true).size('small'))\n"
        "      .child(new Breadcrumb(['Home', 'Settings']))\n"
        "      .child(new Clipboard('copy').value('value').tooltip('Copy'))\n"
        "      .child(new GroupBox().title('Options').variant('outline')"
        ".child('Grouped'))\n"
        "      .child(new Rating('quality').value(3).max(5).size('small')"
        ".color('#ff0000'))\n"
        "      .child(new StatusBar().left_content(div().child('Left'))"
        ".right_content(div().child('Right')).child('Center'))\n"
        "      .child(new Toolbar('toolbar').child('Tool'));\n"
        "  }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(FindTextPrefix(root, StrL("Failed to render")) == nullptr);
    // A banner never shows its title, so "Connection" is not drawn.
    const char* texts[] = {"Offline", "Home",  "Settings", "Options", "Grouped",
                           "Left",    "Right", "Center",   "Tool"};
    for (const char* text : texts) {
        utassert(FindText(root, Str(text)) != nullptr);
    }
}

// alert_rejects_an_incompatible_payload and each module's
// rejects_an_incompatible_payload.
void DisplayComponentsRejectAnIncompatiblePayload() {
    const char* const names[] = {"Alert",    "Breadcrumb", "Clipboard",
                                 "GroupBox", "Rating",     "StatusBar",
                                 "Toolbar"};
    for (const char* name : names) {
        Str failure = MaterializeFailure(name, shell::ComponentPayload{}, 0);
        utassert(StrEq(failure,
                       fmt("%s received an incompatible payload", Str(name))));
        StrFree(failure);
    }
}

// ensure_no_children, which runs before the payload is read.
void DisplayLeavesRefuseChildren() {
    Arena* a = ArenaNew();
    shell::ComponentArgument args[2] = {StringArgument("copy"),
                                        StringArgument("x")};
    const char* const names[] = {"Alert", "Clipboard", "Rating"};
    for (const char* name : names) {
        shell::ComponentPayload payload = BuildPayload(
            name, 0, args, StrEq(Str(name), "Alert") ? 2 : 1, a, nullptr);
        utassert(payload.type != nullptr);
        Str failure = MaterializeFailure(name, payload, 1);
        utassert(StrEq(failure,
                       fmt("%s does not accept child elements", Str(name))));
        StrFree(failure);
    }
    ArenaDelete(a);
}

// The stars a rating rendered: how many, and how many are filled.
void CountStars(El* element, Str filledIcon, int* stars, int* filled) {
    if (!element) return;
    if (element->kind == ElKind::Icon) {
        (*stars)++;
        if (StrEq(element->iconPath, filledIcon)) (*filled)++;
    }
    for (El* child = element->first; child; child = child->next)
        CountStars(child, filledIcon, stars, filled);
}

// later_value_and_max_operations_win_in_call_order
void LaterValueAndMaxOperationsWinInCallOrder() {
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { Rating } from 'gpui-component';\n"
             "export default class Main extends View {\n"
             "  render() { return div().child(new Rating('q').value(4).max(3)"
             ".value(2)); }\n"
             "}\n"));
    El* root = host.Render();
    utassert(root && len(host.ViewError()) == 0);
    Arena* a = ArenaNew();
    Str filledIcon = IconEl(a, IconName::StarFill, 16)->iconPath;
    int stars = 0;
    int filled = 0;
    CountStars(root, filledIcon, &stars, &filled);
    utassert(stars == 3);
    utassert(filled == 2);
    ArenaDelete(a);
}

// rejects_the_rounded_usize_overflow_boundary, and the Display Rust gives
// the number in the message.
void RatingRejectsTheRoundedUsizeOverflowBoundary() {
    utassert(
        StrContains(CallErrorTemp("Rating", "new Rating('r').max(2 ** 64)"),
                    StrL("Rating.max(max) expects a non-negative integer, got "
                         "18446744073709552000")));
    utassert(StrContains(
        CallErrorTemp("Rating", "new Rating('r').value(1.5)"),
        StrL("Rating.value(value) expects a non-negative integer, got 1.5")));
    utassert(StrContains(
        CallErrorTemp("Rating", "new Rating('r').value(-1)"),
        StrL("Rating.value(value) expects a non-negative integer, got -1")));
}

// Rating.on_change reports the star the reader clicked, which the rating
// only knows once the click lands.
void RatingReportsTheClickedStar() {
    Host host(StrL(
        "import { View, div } from 'gpui-kit';\n"
        "import { Rating } from 'gpui-component';\n"
        "export default class Main extends View {\n"
        "  init() { this.value = 0; }\n"
        "  render() {\n"
        "    return div().child(new Rating('q').value(this.value)\n"
        "      .on_change((value, cx) => { this.value = value; cx.notify(); "
        "}))\n"
        "      .child(`value: ${this.value}`);\n"
        "  }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && FindText(root, StrL("value: 0")) != nullptr);
    El* stars[8] = {};
    int count = CollectListeners(root, stars, 0, 8);
    utassert(count == 5);
    if (count == 5) Click(host, stars[1]);
    root = host.Render();
    utassert(FindText(root, StrL("value: 2")) != nullptr);
}

// ─── basic/: mod.rs, text.rs, dropdown_button.rs ───────────────────────────

// registers_only_the_two_closed_renderable_surfaces
void RegistersOnlyTheTwoClosedRenderableSurfaces() {
    FamilyCatalog catalog(&component_shell::RegisterBasic);
    utassert(catalog.ok);
    const char* names[] = {"Text", "DropdownButton"};
    utassert(catalog.NamesAre(names, 2));
    utassert(catalog.Documented());
}

// text_constructor_is_closed_and_preserves_content
void TextConstructorIsClosedAndPreservesContent() {
    const ComponentDescriptor* text = component_shell::Components()
                                          ->Find(StrL("Text"));
    utassert(text && text->constructors[0].arguments[0].schema.kind ==
                         shell::SchemaKind::String);
    utassert(RendersCleanly("Text", "new Text('hello')", "hello"));
    Arena* a = ArenaNew();
    shell::ComponentArgument value = StringArgument("hello");
    shell::ComponentPayload payload =
        BuildPayload("Text", 0, &value, 1, a, nullptr);
    Str failure = MaterializeFailure("Text", payload, 1);
    utassert(StrEq(failure, StrL("Text does not accept children; pass its "
                                 "content to Text(value)")));
    StrFree(failure);
    ArenaDelete(a);
}

// menu_item_keeps_label_and_closed_callback_handle and
// descriptor_uses_only_closed_schemas
void DropdownButtonDescriptorIsClosed() {
    const ComponentDescriptor* d = component_shell::Components()
                                       ->Find(StrL("DropdownButton"));
    utassert(d != nullptr);
    if (!d) return;
    utassert(d->constructors[0].arguments[0].schema.kind ==
             shell::SchemaKind::String);
    const MethodDescriptor& item = d->methods[d->methods.count - 1];
    utassert(strcmp(item.name, "menu_item") == 0);
    utassert(item.arguments[1].schema.kind == shell::SchemaKind::Callback &&
             strcmp(item.arguments[1].schema.text, "(cx: Context) => void") ==
                 0);
    Arena* a = ArenaNew();
    shell::ComponentArgument args[2] = {StringArgument("Open"), {}};
    args[1].kind = shell::ComponentArgumentKind::Callback;
    args[1].callback = 42;
    shell::PayloadBuild build;
    build.a = a;
    utassert(item.recorder(&build, args, 2) && build.out.type != nullptr);
    Str failure;
    shell::ComponentArgument blank[2] = {StringArgument("  "),
                                         StringArgument("Actions")};
    BuildPayload("DropdownButton", 0, blank, 2, a, &failure);
    utassert(StrEq(failure, StrL("DropdownButton id must not be empty")));
    shell::ComponentPayload payload =
        BuildPayload("DropdownButton", 0, args, 2, a, nullptr);
    Str children = MaterializeFailure("DropdownButton", payload, 1);
    utassert(StrEq(children, StrL("DropdownButton does not accept children")));
    StrFree(children);
    ArenaDelete(a);
}

// basic_public_host.rs: basic_text_and_dropdown_materialize_through_public
// _host. Rust reads the recording back as a debug tree; here the element
// tree is what is checked, and the clicks are the listeners the action half,
// the caret and the menu row carry.
void BasicTextAndDropdownMaterializeThroughTheHost() {
    Host host(
        StrL("import { div, View } from 'gpui-kit';\n"
             "import { DropdownButton, Text } from 'gpui-component';\n"
             "export default class BasicRemaining extends View {\n"
             "  init() { this.action_hits = 0; this.menu_hits = 0; }\n"
             "  render() {\n"
             "    return div()\n"
             "      .child(new Text('Plain component text').p(2))\n"
             "      .child(new DropdownButton('actions', 'Actions')\n"
             "        .absolute().left(0).top(60).w(180).h(40)\n"
             "        .outline().disabled(false).selected(true)\n"
             "        .size('small').variant('primary').menu_anchor"
             "('bottom_right')\n"
             "        .on_click((_event, cx) => { this.action_hits += 1; "
             "cx.notify(); })\n"
             "        .menu_item('Open', (cx) => { this.menu_hits += 1; "
             "cx.notify(); }))\n"
             "      .child(`Counts: ${this.action_hits}|${this.menu_hits}`);\n"
             "  }\n"
             "}\n"));
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(FindText(root, StrL("Plain component text")) != nullptr);
    utassert(FindText(root, StrL("Counts: 0|0")) != nullptr);

    El* action = ListenerAbove(root, StrL("Actions"));
    utassert(action != nullptr);
    if (action) Click(host, action);
    root = host.Render();
    utassert(FindText(root, StrL("Counts: 1|0")) != nullptr);

    // The caret is the listener after the action half's.
    El* listeners[8] = {};
    int count = CollectListeners(root, listeners, 0, 8);
    utassert(count >= 2);
    if (count >= 2) Click(host, listeners[1]);
    root = host.Render();
    El* open = ListenerAbove(root, StrL("Open"));
    utassert(open != nullptr);
    if (open) Click(host, open);
    root = host.Render();
    utassert(FindText(root, StrL("Counts: 1|1")) != nullptr);
}

// ─── typed_compound/mod.rs ─────────────────────────────────────────────────

// wrong_registered_child_identity_is_rejected
void WrongRegisteredChildIdentityIsRejected() {
    const char* accordion[] = {"AccordionItem"};
    const char* radio[] = {"Radio"};
    const char* stepper[] = {"StepperItem"};
    {
        RequestFixture f(0, nullptr, 0);
        utassert(!component_shell::RequireChild(&f.request, "Accordion", "Tab",
                                                accordion));
    }
    {
        RequestFixture f(0, nullptr, 0);
        utassert(!component_shell::RequireChild(&f.request, "Accordion",
                                                nullptr, accordion));
        utassert(StrContains(f.request.failure, StrL("ordinary element")));
    }
    {
        RequestFixture f(0, nullptr, 0);
        utassert(!component_shell::RequireChild(&f.request, "RadioGroup",
                                                nullptr, radio));
        utassert(component_shell::RequireChild(&f.request, "Stepper",
                                               "StepperItem", stepper));
    }
}

// callback_operation_preserves_the_script_callback_handle
void CallbackOperationPreservesTheScriptCallbackHandle() {
    Arena* a = ArenaNew();
    shell::ComponentArgument callback;
    callback.kind = shell::ComponentArgumentKind::Callback;
    callback.callback = 42;
    shell::PayloadBuild build;
    build.a = a;
    utassert(component_shell::typed_compound::IndexCallbackPayload(
        &build, &callback, 1, "RadioGroup"));
    shell::CallbackId id = 0;
    utassert(component_shell::typed_compound::RadioGroupOnChangeCallback(
        build.out, &id));
    utassert(id == 42);

    shell::ComponentArgument number;
    number.kind = shell::ComponentArgumentKind::Number;
    number.number = 42;
    shell::PayloadBuild refused;
    refused.a = a;
    utassert(!component_shell::typed_compound::IndexCallbackPayload(
        &refused, &number, 1, "RadioGroup"));
    utassert(StrEq(refused.error,
                   StrL("RadioGroup.on_change(callback) expects a callback")));
    ArenaDelete(a);
}

// typed_elements_are_extracted_from_real_any_elements
void TypedElementsAreExtractedFromRealElements() {
    using namespace component_shell::typed_compound;
    RequestFixture f(0, nullptr, 0);
    Ctx* cx = &f.cx;
    El* accordionItem =
        TypedChildElementOf(cx, component::AccordionItem::New(cx));
    El* radio =
        TypedChildElementOf(cx, component::Radio::New(cx, StrL("radio")));
    El* tab = TypedChildElementOf(cx, component::Tab::New(cx));
    El* stepperItem = TypedChildElementOf(cx, component::StepperItem::New(cx));

    shell::MaterializeRequest& request = f.request;
    utassert(TakeElementAs<component::AccordionItem>(&request, accordionItem,
                                                     "AccordionItem"));
    utassert(TakeElementAs<component::Radio>(&request, radio, "Radio"));
    utassert(TakeElementAs<component::Tab>(&request, tab, "Tab"));
    utassert(TakeElementAs<component::StepperItem>(&request, stepperItem,
                                                   "StepperItem"));
    utassert(len(request.failure) == 0);

    utassert(
        !TakeElementAs<component::StepperItem>(&request, tab, "StepperItem"));
    utassert(StrEq(request.failure,
                   StrL("registered StepperItem materialized an incompatible "
                        "element")));
    RequestFixture g(0, nullptr, 0);
    utassert(!TakeElementAs<component::Tab>(&g.request, tab, "Tab"));
    utassert(StrEq(g.request.failure,
                   StrL("registered Tab child was already consumed")));
    RequestFixture h(0, nullptr, 0);
    utassert(!TakeElementAs<component::Tab>(&h.request, Div(cx->a), "Tab"));
}

// batch_publishes_closed_documented_descriptors
void BatchPublishesClosedDocumentedDescriptors() {
    FamilyCatalog catalog(&component_shell::RegisterTypedCompound);
    utassert(catalog.ok);
    const char* names[] = {"AccordionItem", "Accordion",   "RadioGroup", "Tab",
                           "TabBar",        "StepperItem", "Stepper"};
    utassert(catalog.NamesAre(names, 7));
    utassert(catalog.Documented());
    const ComponentDescriptor* tabBar = catalog.frozen.Find(StrL("TabBar"));
    utassert(tabBar != nullptr);
    if (!tabBar) return;
    utassert(tabBar->constructors[0].arguments[0].schema.kind ==
             shell::SchemaKind::String);
    const ArgumentSchema& variant = tabBar->methods[1].arguments[0].schema;
    const char* literals[] = {"tab", "outline", "pill", "segmented",
                              "underline"};
    utassert(variant.kind == shell::SchemaKind::Enum && variant.values
                                                                .count == 5);
    for (int i = 0; i < variant.values.count && i < 5; i++)
        utassert(strcmp(variant.values[i], literals[i]) == 0);
}

// The index a typed container reports reaches the script, and the accordion
// reports the open set Rust's click leaves behind.
void TypedContainersReportTheirSelection() {
    Host host(StrL(
        "import { View, div } from 'gpui-kit';\n"
        "import { Accordion, AccordionItem, Tab, TabBar, Stepper, StepperItem "
        "} from 'gpui-component';\n"
        "export default class Main extends View {\n"
        "  init() { this.tab = 0; this.step = 0; this.open = 'none'; }\n"
        "  render() {\n"
        "    return div().w(600)\n"
        "      .child(new TabBar('tabs').selected_index(this.tab)\n"
        "        .on_change((i, cx) => { this.tab = i; cx.notify(); })\n"
        "        .child(new Tab().label('First'))"
        ".child(new Tab().label('Second')))\n"
        "      .child(new Stepper('steps').selected_index(this.step)\n"
        "        .on_change((i, cx) => { this.step = i; cx.notify(); })\n"
        "        .child(new StepperItem().child('Account'))"
        ".child(new StepperItem().child('Profile')))\n"
        "      .child(new Accordion('faq')\n"
        "        .on_toggle((open, cx) => { this.open = open.join(',') || "
        "'none'; cx.notify(); })\n"
        "        .child(new AccordionItem().title(div().child('Question A'))"
        ".open(true).child('Answer A'))\n"
        "        .child(new AccordionItem().title(div().child('Question B'))"
        ".child('Answer B')))\n"
        "      .child(`state: ${this.tab}|${this.step}|${this.open}`);\n"
        "  }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(FindTextPrefix(root, StrL("Failed to render")) == nullptr);
    utassert(FindText(root, StrL("state: 0|0|none")) != nullptr);

    El* second = ListenerAbove(root, StrL("Second"));
    utassert(second != nullptr);
    if (second) Click(host, second);
    root = host.Render();
    utassert(FindText(root, StrL("state: 1|0|none")) != nullptr);

    El* profile = ListenerAbove(root, StrL("Profile"));
    utassert(profile != nullptr);
    if (profile) Click(host, profile);
    root = host.Render();
    utassert(FindText(root, StrL("state: 1|1|none")) != nullptr);

    // Single-open: opening B closes A.
    El* question = ListenerAbove(root, StrL("Question B"));
    utassert(question != nullptr);
    if (question) Click(host, question);
    root = host.Render();
    utassert(FindText(root, StrL("state: 1|1|1")) != nullptr);
}

// A typed container refuses a child that is not its part, and a part that
// materialized as something else, by failing to render.
void TypedContainersRefuseForeignChildren() {
    utassert(!RendersCleanly("TabBar, Tab, AccordionItem",
                             "new TabBar('t').child(new AccordionItem())"));
    utassert(!RendersCleanly("Stepper", "new Stepper('s').child(div())"));
    utassert(StrContains(CallErrorTemp("TabBar", "new TabBar(' ')"),
                         StrL("TabBar(id) expects a nonempty string id")));
    utassert(StrContains(CallErrorTemp("Tabs", "new Tabs('')"),
                         StrL("Tabs(id) expects a nonempty string id")));
    utassert(StrContains(
        CallErrorTemp("RadioGroup", "new RadioGroup('r').selected_index(1.5)"),
        StrL("RadioGroup.selected_index(index) expects a nonnegative "
             "integer")));
    // Parts render on their own outside their container.
    utassert(RendersCleanly("Tab", "new Tab().label('Alone')", "Alone"));
    utassert(RendersCleanly("StepperItem",
                            "new StepperItem().child('Lonely step')",
                            "Lonely step"));
}

// ─── compound/: mod.rs, common.rs and the five modules ─────────────────────

// registers_only_the_honestly_materializable_compound_batch
void RegistersOnlyTheHonestlyMaterializableCompoundBatch() {
    FamilyCatalog catalog(&component_shell::RegisterCompound);
    utassert(catalog.ok);
    const char* names[] = {"Avatar", "Collapsible", "Pagination", "Progress",
                           "Radio"};
    utassert(catalog.NamesAre(names, 5));
    utassert(catalog.Documented());
}

// numeric_and_controlled_arguments_have_closed_schemas
void NumericAndControlledArgumentsHaveClosedSchemas() {
    FamilyCatalog catalog(&component_shell::RegisterCompound);
    // By name, not by position.
    auto schemaOf = [&](const char* component,
                        const char* method) -> shell::SchemaKind {
        const ComponentDescriptor* d = catalog.frozen.Find(Str(component));
        if (!d) return shell::SchemaKind::Optional;
        for (const MethodDescriptor& m : d->methods) {
            if (strcmp(m.name, method) == 0) return m.arguments[0].schema.kind;
        }
        return shell::SchemaKind::Optional;
    };
    utassert(schemaOf("Pagination", "current_page") ==
             shell::SchemaKind::Number);
    utassert(schemaOf("Radio", "checked") == shell::SchemaKind::Boolean);
}

// common.rs usize_conversion_rejects_fractional_negative_and_overflow_values
void CompoundUsizeConversionRejectsFractionalNegativeAndOverflow() {
    using component_shell::compound::common::NonnegativeUsize;
    uint64_t out = 7;
    Str error;
    utassert(NonnegativeUsize(0.0, StrL("value"), &out, &error) && out == 0);
    utassert(NonnegativeUsize(42.0, StrL("value"), &out, &error) && out == 42);
    utassert(!NonnegativeUsize(-1.0, StrL("value"), &out, &error));
    utassert(!NonnegativeUsize(1.5, StrL("value"), &out, &error));
    utassert(!NonnegativeUsize(INFINITY, StrL("value"), &out, &error));
    utassert(
        !NonnegativeUsize(18446744073709551616.0, StrL("value"), &out, &error));
    utassert(StrEq(error, StrL("value expects an exactly representable "
                               "nonnegative integer")));
}

// common.rs f32_conversion_rejects_values_that_would_become_infinite, and
// progress.rs value_rejects_f64_values_outside_the_f32_range
void CompoundF32ConversionRejectsValuesThatWouldBecomeInfinite() {
    using component_shell::compound::common::FiniteF32;
    float out = 0;
    Str error;
    utassert(FiniteF32(42.5, StrL("value"), &out, &error) && out == 42.5f);
    utassert(!FiniteF32((double)FLT_MAX * 2.0, StrL("value"), &out, &error));
    utassert(!FiniteF32(-(double)FLT_MAX * 2.0, StrL("value"), &out, &error));
    utassert(!FiniteF32(NAN, StrL("value"), &out, &error));
    utassert(
        !FiniteF32(-INFINITY, StrL("Progress.value(value)"), &out, &error));
    utassert(StrEq(error, StrL("Progress.value(value) expects a finite "
                               "number representable as f32")));
}

// common.rs ids_must_contain_non_whitespace_text, and each module's
// id_rejects_empty_and_whitespace_only_values through its constructor.
void CompoundIdsMustContainNonWhitespaceText() {
    using component_shell::compound::common::NonemptyId;
    Str error;
    utassert(!NonemptyId(StrL(""), "Widget", &error));
    utassert(!NonemptyId(StrL("  \t"), "Widget", &error));
    utassert(StrEq(error, StrL("Widget(id) expects a nonempty string id")));
    utassert(NonemptyId(StrL("widget-1"), "Widget", &error));
    utassert(StrContains(CallErrorTemp("Pagination", "new Pagination('\\n')"),
                         StrL("Pagination(id) expects a nonempty string id")));
    utassert(StrContains(CallErrorTemp("Progress", "new Progress('   ')"),
                         StrL("Progress(id) expects a nonempty string id")));
    utassert(StrContains(CallErrorTemp("Radio", "new Radio(' \\t ')"),
                         StrL("Radio(id) expects a nonempty string id")));
    utassert(RendersCleanly("Radio", "new Radio('choice-a').label('A')", "A"));
}

// pagination.rs positive_integer_validation, through the recorder that
// calls it.
void PaginationPositiveIntegerValidation() {
    utassert(
        RendersCleanly("Pagination", "new Pagination('p').total_pages(3)"));
    const char* refused[] = {"0", "1.5", "2 ** 64"};
    for (const char* value : refused) {
        TempStr call = fmt("new Pagination('p').total_pages(%s)", Str(value));
        TempStr message = CallErrorTemp("Pagination", call.s);
        utassert(StrContains(message, StrL("Pagination.total_pages")));
    }
    utassert(StrContains(
        CallErrorTemp("Pagination", "new Pagination('p').total_pages(0)"),
        StrL("Pagination.total_pages(total_pages) expects a positive "
             "integer")));
    utassert(StrContains(
        CallErrorTemp("Pagination", "new Pagination('p').visible_pages(1.5)"),
        StrL("Pagination.visible_pages(visible_pages) expects an exactly "
             "representable nonnegative integer")));
}

// avatar.rs incompatible_payload_is_rejected, progress.rs
// invalid_payload_fails, and the three other modules' payload checks.
void CompoundComponentsRejectAnIncompatiblePayload() {
    const char* const names[] = {"Avatar", "Collapsible", "Pagination",
                                 "Progress", "Radio"};
    for (const char* name : names) {
        Str failure = MaterializeFailure(name, shell::ComponentPayload{}, 0);
        utassert(StrEq(failure,
                       fmt("%s received an incompatible payload", Str(name))));
        StrFree(failure);
    }
    Arena* a = ArenaNew();
    shell::ComponentArgument id = StringArgument("p");
    const char* const leaves[] = {"Avatar", "Pagination", "Progress"};
    for (const char* name : leaves) {
        shell::ComponentPayload payload = BuildPayload(
            name, 0, &id, StrEq(Str(name), "Avatar") ? 0 : 1, a, nullptr);
        Str failure = MaterializeFailure(name, payload, 1);
        utassert(StrEq(failure, fmt("%s does not accept children", Str(name))));
        StrFree(failure);
    }
    ArenaDelete(a);
}

// avatar.rs real_avatar_accepts_recorded_operations and progress.rs
// real_progress_accepts_value, with the rest of the family, from a script.
void CompoundComponentsBuildFromAScript() {
    Host host(StrL(
        "import { View, div } from 'gpui-kit';\n"
        "import { Avatar, Collapsible, Pagination, Progress, Radio } from "
        "'gpui-component';\n"
        "export default class Main extends View {\n"
        "  init() { this.page = 1; this.checked = false; }\n"
        "  render() {\n"
        "    return div().w(600)\n"
        "      .child(new Avatar().name('Ada Lovelace').size('large'))\n"
        "      .child(new Collapsible().open(true).motion_id('details')\n"
        "        .child('Trigger').content(div().child('Revealed')))\n"
        "      .child(new Collapsible().child('Closed trigger')"
        ".content(div().child('Hidden')))\n"
        "      .child(new Pagination('pages').total_pages(5)"
        ".current_page(this.page)\n"
        "        .on_change((page, cx) => { this.page = page; cx.notify(); "
        "}))\n"
        "      .child(new Progress('p').value(42).size('small')"
        ".accessibility_label('Upload'))\n"
        "      .child(new Radio('alone').label('Alone').checked(this.checked)\n"
        "        .on_change((checked, cx) => { this.checked = checked; "
        "cx.notify(); }))\n"
        "      .child(`state: ${this.page}|${this.checked}`);\n"
        "  }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(FindTextPrefix(root, StrL("Failed to render")) == nullptr);
    utassert(FindText(root, StrL("Trigger")) != nullptr);
    utassert(FindText(root, StrL("Revealed")) != nullptr);
    utassert(FindText(root, StrL("Closed trigger")) != nullptr);
    utassert(FindText(root, StrL("Hidden")) == nullptr);
    utassert(FindText(root, StrL("state: 1|false")) != nullptr);

    El* page = ListenerAbove(root, StrL("3"));
    utassert(page != nullptr);
    if (page) Click(host, page);
    root = host.Render();
    utassert(FindText(root, StrL("state: 3|false")) != nullptr);

    El* radio = ListenerAbove(root, StrL("Alone"));
    utassert(radio != nullptr);
    if (radio) Click(host, radio);
    root = host.Render();
    utassert(FindText(root, StrL("state: 3|true")) != nullptr);
}

// The text elements under `element`, in tree order, joined by `|`.
void CollectTexts(El* element, StrBuilder* out) {
    if (!element) return;
    if (element->kind == ElKind::Text) {
        out->Append(element->text);
        out->Append(StrL("|"));
    }
    for (El* child = element->first; child; child = child->next)
        CollectTexts(child, out);
}

// public_host.rs public_host_materializes_real_typed_compound_children_in
// _script_order. Rust counts the recorded `.w(500)` calls in the debug tree;
// here each container's style is what lands on its root.
void PublicHostMaterializesTypedCompoundChildrenInScriptOrder() {
    Host host(StrL(
        "import { View, div } from 'gpui-kit';\n"
        "import {\n"
        "  Accordion, AccordionItem, Radio, RadioGroup,\n"
        "  Stepper, StepperItem, Tab, TabBar,\n"
        "} from 'gpui-component';\n"
        "export default class TypedCompounds extends View {\n"
        "  render() {\n"
        "    return div().v_flex().gap(8)\n"
        "      .child(new Accordion('faq').w(500).multiple(true)\n"
        "        .child(new AccordionItem().px(2).title(div().child('Question "
        "A')).open(true).child('Answer A'))\n"
        "        .child(new AccordionItem().title(div().child('Question B'))"
        ".child('Answer B')))\n"
        "      .child(new TabBar('sections').w(500).selected_index(1)"
        ".variant('underline')\n"
        "        .child(new Tab().label('First'))\n"
        "        .child(new Tab().label('Second')))\n"
        "      .child(new Stepper('setup').w(500).selected_index(1)\n"
        "        .child(new StepperItem().child('Account'))"
        ".child(new StepperItem().child('Profile')))\n"
        "      .child(new RadioGroup('density').w(500).selected_index(1)\n"
        "        .child(new Radio('comfortable').px(2).label('Comfortable'))\n"
        "        .child(new Radio('compact').label('Compact')));\n"
        "  }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(FindTextPrefix(root, StrL("Failed to render")) == nullptr);
    StrBuilder texts;
    CollectTexts(root, &texts);
    Str tree = texts.TakeStr();
    const char* ordered[] = {"Question A", "Answer A",    "Question B",
                             "First",      "Second",      "Account",
                             "Profile",    "Comfortable", "Compact"};
    int at = 0;
    for (const char* text : ordered) {
        TempStr needle = fmt("%s|", Str(text));
        Str rest = Str(tree.s + at, len(tree) - at);
        int found = StrFind(rest, needle);
        utassert(found >= 0);
        if (found >= 0) at += found + len(needle);
    }
    StrFree(tree);
    int wide = 0;
    for (El* child = root->first; child; child = child->next) {
        if (child->style.width == 500) wide++;
    }
    utassert(wide == 4);
}

// A RadioGroup reports the radio clicked, and a radio's own style reaches
// it inside the group.
void RadioGroupReportsTheClickedIndex() {
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { Radio, RadioGroup } from 'gpui-component';\n"
             "export default class Main extends View {\n"
             "  init() { this.sel = 0; }\n"
             "  render() {\n"
             "    return div()\n"
             "      .child(new RadioGroup('g').selected_index(this.sel)\n"
             "        .on_change((i, cx) => { this.sel = i; cx.notify(); })\n"
             "        .child(new Radio('a').label('Alpha'))\n"
             "        .child(new Radio('b').label('Beta').w(123)))\n"
             "      .child(`sel: ${this.sel}`);\n"
             "  }\n"
             "}\n"));
    El* root = host.Render();
    utassert(root && len(host.ViewError()) == 0);
    utassert(FindText(root, StrL("sel: 0")) != nullptr);
    El* beta = ListenerAbove(root, StrL("Beta"));
    utassert(beta != nullptr && beta->style.width == 123);
    if (beta) Click(host, beta);
    root = host.Render();
    utassert(FindText(root, StrL("sel: 1")) != nullptr);
    utassert(!RendersCleanly("RadioGroup, Radio",
                             "new RadioGroup('g').child(div())"));
}

// ─── empty.rs and tests/empty_host.rs ──────────────────────────────────────

// empty_publishes_all_parts_with_closed_documented_methods
void EmptyPublishesAllPartsWithClosedDocumentedMethods() {
    FamilyCatalog catalog(&component_shell::RegisterEmpty);
    utassert(catalog.ok);
    const char* names[] = {"Empty",      "EmptyHeader",      "EmptyMedia",
                           "EmptyTitle", "EmptyDescription", "EmptyContent"};
    utassert(catalog.NamesAre(names, 6));
    const char* methods[6][3] = {
        {}, {"media", "title", "description"}, {"variant"}, {}, {}, {}};
    const int counts[6] = {0, 3, 1, 0, 0, 0};
    for (int i = 0; i < catalog.frozen.DescriptorCount() && i < 6; i++) {
        const ComponentDescriptor* d = catalog.frozen.Descriptor((uint32_t)i);
        utassert(d->documentation != nullptr);
        utassert(d->constructors[0].arguments.count == 0);
        utassert(d->methods.count == counts[i]);
        for (int m = 0; m < d->methods.count && m < counts[i]; m++) {
            const MethodDescriptor& method = d->methods[m];
            utassert(strcmp(method.name, methods[i][m]) == 0);
            utassert(method.documentation != nullptr);
            const ArgumentSchema& schema = method.arguments[0].schema;
            if (strcmp(method.name, "variant") == 0) {
                utassert(schema.kind == shell::SchemaKind::Enum &&
                         schema.values.count == 2 &&
                         strcmp(schema.values[0], "default") == 0 &&
                         strcmp(schema.values[1], "icon") == 0);
            } else {
                utassert(schema.kind == shell::SchemaKind::Element);
            }
        }
    }
}

// empty_host.rs empty_slots_replace_previous_parts_and_preserve_child
// _actions. Rust clicks at the button's laid-out position; here the click is
// the listener the button carries. The media holds text where Rust's holds
// an Icon, which the catalog here does not register yet.
void EmptySlotsReplacePreviousPartsAndPreserveChildActions() {
    Host host(StrL(
        "import { div, View } from 'gpui-kit';\n"
        "import {\n"
        "  Empty, EmptyHeader, EmptyMedia, EmptyTitle, EmptyDescription, "
        "EmptyContent, Button,\n"
        "} from 'gpui-component';\n"
        "export default class EmptyHost extends View {\n"
        "  init() { this.hits = 0; }\n"
        "  render() {\n"
        "    return div()\n"
        "      .child(new Empty().relative().w(400).h(260).p(0)\n"
        "        .child(div().child(`Hits: ${this.hits}`))\n"
        "        // Invalid, overwritten values must not be materialized.\n"
        "        .header(new EmptyHeader().child('discarded header'))\n"
        "        .content(new EmptyTitle())\n"
        "        .content(new EmptyContent().absolute().left(0).top(100)"
        ".w(180).h(40).p(0)\n"
        "          .child(new Button('create-project').w(180).h(40)"
        ".label('Create project')\n"
        "            .on_click((_event, cx) => { this.hits += 1; cx.notify(); "
        "})))\n"
        "        .header(new EmptyHeader().items_start().gap(4)\n"
        "          .media(new EmptyContent())\n"
        "          .title(new EmptyContent())\n"
        "          .description(new EmptyContent())\n"
        "          .description(new EmptyDescription().text_size(12)"
        ".child('Create your first project.'))\n"
        "          .title(new EmptyTitle().font_semibold().child('No "
        "projects'))\n"
        "          .media(new EmptyMedia().variant('icon').variant('default')"
        ".p(2)\n"
        "            .child('Folder icon'))))\n"
        "      // Parts also render directly, outside the typed slots.\n"
        "      .child(new EmptyHeader().title(new "
        "EmptyTitle().child('Standalone "
        "header')))\n"
        "      .child(new EmptyMedia().child('Standalone media'))\n"
        "      .child(new EmptyTitle().child('Standalone title'))\n"
        "      .child(new EmptyDescription().child('Standalone description'))\n"
        "      .child(new EmptyContent().child('Standalone content'));\n"
        "  }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    if (!root) return;
    utassert(FindTextPrefix(root, StrL("Failed to render")) == nullptr);
    const char* texts[] = {"Hits: 0",
                           "Folder icon",
                           "Create project",
                           "No projects",
                           "Create your first project.",
                           "Standalone header",
                           "Standalone media",
                           "Standalone title",
                           "Standalone description",
                           "Standalone content"};
    for (const char* text : texts) utassert(FindText(root, Str(text)));
    utassert(FindText(root, StrL("discarded header")) == nullptr);
    // The header's own style reached the header the Empty rendered.
    El* title = FindParentOfText(root, StrL("No projects"));
    El* header = nullptr;
    for (El* e = root->first ? root->first->first : nullptr; e; e = e->next) {
        for (El* c = e->first; c; c = c->next)
            if (c == title) header = e;
    }
    utassert(header != nullptr && header->style.gapY == 4);

    El* button = ListenerAbove(root, StrL("Create project"));
    utassert(button != nullptr);
    if (button) Click(host, button);
    root = host.Render();
    utassert(FindText(root, StrL("Hits: 1")) != nullptr);
}

// empty_host.rs empty_rejects_wrong_slot_types_and_ordinary_header_children.
// Rust reads each diagnostic from `check`; the host here renders the refused
// part as "Failed to render <part>", and the diagnostics themselves are the
// TakeElement messages typed_compound's tests pin, plus the header's own.
void EmptyRejectsWrongSlotTypesAndOrdinaryHeaderChildren() {
    struct Case {
        const char* expression;
        const char* failed;
    };
    const Case cases[] = {
        {"new Empty().header(new EmptyTitle())", "Failed to render Empty"},
        {"new Empty().content(new EmptyTitle())", "Failed to render Empty"},
        {"new EmptyHeader().media(new EmptyTitle())",
         "Failed to render EmptyHeader"},
        {"new EmptyHeader().title(new EmptyContent())",
         "Failed to render EmptyHeader"},
        {"new EmptyHeader().description(new EmptyTitle())",
         "Failed to render EmptyHeader"},
        {"new Empty().header(div())", "Failed to render Empty"},
        {"new EmptyHeader().child(div())", "Failed to render EmptyHeader"},
    };
    for (const Case& c : cases) {
        TempStr source =
            fmt("import { View, div } from 'gpui-kit';\n"
                "import { Empty, EmptyHeader, EmptyTitle, EmptyContent, "
                "EmptyMedia } from 'gpui-component';\n"
                "export default class Invalid extends View {\n"
                "  render() { return div().child(%s); }\n"
                "}\n",
                Str(c.expression));
        Host host(source);
        El* root = host.Render();
        utassert(root && len(host.ViewError()) == 0);
        utassert(FindText(root, Str(c.failed)) != nullptr);
    }
    utassert(StrContains(
        CallErrorTemp("EmptyMedia", "new EmptyMedia().variant('avatar')"),
        StrL("variant")));

    Arena* a = ArenaNew();
    shell::ComponentPayload header =
        BuildPayload("EmptyHeader", 0, nullptr, 0, a, nullptr);
    Str failure = MaterializeFailure("EmptyHeader", header, 1);
    utassert(StrEq(failure, StrL("EmptyHeader does not accept children; use "
                                 "media, title, and description")));
    StrFree(failure);
    Str payload =
        MaterializeFailure("EmptyTitle", shell::ComponentPayload{}, 0);
    utassert(StrEq(payload, StrL("Empty received an incompatible payload")));
    StrFree(payload);
    ArenaDelete(a);
}

// ─── Shared helpers for the text-control families ──────────────────────────

// Every element bound to a text field, in tree order.
void CollectInputs(El* element, InputState** out, int* count, int max) {
    if (!element) return;
    if (element->input && *count < max) {
        bool seen = false;
        for (int i = 0; i < *count; i++)
            seen = seen || out[i] == element->input;
        if (!seen) out[(*count)++] = element->input;
    }
    for (El* child = element->first; child; child = child->next)
        CollectInputs(child, out, count, max);
}

// Types `text` one character at a time, the way simulated input arrives:
// one edit, and so one change, per character.
void TypeInto(Host& host, InputState* state, const char* text) {
    for (const char* at = text; *at; at++) {
        InputReplaceTextInRange(state, &host.app, &host.window, nullptr,
                                Str(at, 1));
    }
}

El* FindById(El* element, Str id) {
    if (!element) return nullptr;
    if (StrEq(element->id, id)) return element;
    for (El* child = element->first; child; child = child->next) {
        if (El* found = FindById(child, id)) return found;
    }
    return nullptr;
}

// The order `a` comes before `b` in a depth-first walk.
bool Precedes(El* root, Str a, Str b) {
    struct Walk {
        static void Visit(El* element, Str a, Str b, int* at, int* ia,
                          int* ib) {
            if (!element) return;
            int here = (*at)++;
            if (*ia < 0 && StrEq(element->id, a)) *ia = here;
            if (*ib < 0 && StrEq(element->id, b)) *ib = here;
            for (El* child = element->first; child; child = child->next)
                Visit(child, a, b, at, ia, ib);
        }
    };
    int at = 0, ia = -1, ib = -1;
    Walk::Visit(root, a, b, &at, &ia, &ib);
    return ia >= 0 && ib >= 0 && ia < ib;
}

// What a script that renders `source` was refused with: a TypeError from the
// description, or the reason its registered component failed to render.
Str RenderRefusal(Host& host) {
    host.Render();
    Str message = host.ViewError();
    if (!len(message)) message = host.error.message;
    if (!len(message) && host.runtime)
        message = host.runtime->LastComponentFailure();
    return message;
}

// ─── input_group/: mod.rs, binding.rs, content_type.rs ─────────────────────

void InputGroupRegistersItsSixDocumentedParts() {
    FamilyCatalog catalog(&component_shell::RegisterInputGroup);
    utassert(catalog.ok);
    const char* names[] = {"InputGroup",         "InputGroupAddon",
                           "InputGroupButton",   "InputGroupInput",
                           "InputGroupTextarea", "InputGroupText"};
    utassert(catalog.NamesAre(names, 6));
    utassert(catalog.Documented());
    utassert(catalog.frozen.StateCount() == 0);
}

// binding.rs rows / auto_grow.
void TextareaRowsArePositiveWholeCounts() {
    shell::ComponentArgument arg;
    arg.kind = shell::ComponentArgumentKind::Number;
    int rows = 0;
    Str error;
    arg.number = 3;
    utassert(component_shell::input_group::binding::Rows(arg, &rows, &error) &&
             rows == 3);
    const double refused[] = {0.0, -1.0,     1.5,
                              NAN, INFINITY, 18446744073709551616.0};
    for (double value : refused) {
        arg.number = value;
        utassert(
            !component_shell::input_group::binding::Rows(arg, &rows, &error));
        utassert(
            StrEq(error, StrL("textarea rows must be a positive integer")));
    }
}

// input_group_host.rs input_group_retains_text_callbacks_and_routes_addon_
// actions. Typing is the retained state's own edit path, one edit per
// character as simulated input delivers; clicks are the controls' listeners.
// Layout bounds are not measured here, so the addon order is the tree's.
// input_group_host.rs input_group_rejects_wrong_part_types_and_invalid_
// layout_options.

// Walks for the first element carrying a click Func0 (a token chip's
// activation) and runs it.
// inline_tokens_host.rs inline_tokens_script_operations_and_click_reentry.
// The token chip's click is its activation handler, run directly.
// ─── retained_forms/mod.rs ─────────────────────────────────────────────────

// retained_forms_publish_matching_state_and_component_contracts
void RetainedFormsPublishMatchingStateAndComponentContracts() {
    FamilyCatalog catalog(&component_shell::RegisterRetainedForms);
    utassert(catalog.ok);
    const char* states[] = {
        "InputState",       "CalendarState",   "OtpState",      "SliderState",
        "ColorPickerState", "DatePickerState", "TimeFieldState"};
    utassert(catalog.frozen.StateCount() == 7);
    for (int i = 0; i < catalog.frozen.StateCount() && i < 7; i++) {
        utassert(strcmp(catalog.frozen.State(i)->exportName, states[i]) == 0);
        utassert(catalog.frozen.State(i)->documentation != nullptr);
    }
    const char* components[] = {"Input",      "NumberInput", "OtpInput",
                                "Slider",     "ColorPicker", "Calendar",
                                "DatePicker", "TimeField"};
    utassert(catalog.NamesAre(components, 8));
    utassert(catalog.Documented());
}

// state_arguments_are_closed_and_component_state_kinds_match
void StateArgumentsAreClosedAndComponentStateKindsMatch() {
    FamilyCatalog catalog(&component_shell::RegisterRetainedForms);
    const StateDescriptor* otp = catalog.frozen.StateOfKind(StrL("OtpState"));
    utassert(otp && otp->arguments.count == 1 &&
             otp->arguments[0].schema.kind == shell::SchemaKind::Number);
    for (int i = 0; i < catalog.frozen.DescriptorCount(); i++) {
        const ConstructorDescriptor& constructor =
            catalog.frozen.Descriptor((uint32_t)i)->constructors[0];
        utassert(constructor.arguments.count == 1);
        utassert(constructor.arguments[0]
                     .schema.kind == shell::SchemaKind::Entity);
    }
}

shell::ComponentArgument NumberArgument(double value) {
    shell::ComponentArgument argument;
    argument.kind = shell::ComponentArgumentKind::Number;
    argument.number = value;
    return argument;
}

// positive_usize_rejects_values_that_round_past_usize_max
void PositiveUsizeRejectsValuesThatRoundPastUsizeMax() {
    shell::ComponentArgument rounded = NumberArgument(18446744073709551616.0);
    uint64_t value = 0;
    Str error;
    utassert(!component_shell::retained_forms::PositiveUsize(
        &rounded, 1, "OtpState", &value, &error));
    utassert(StrContains(error, StrL("positive integer")));
}

// positive_usize_accepts_only_exact_positive_integers
void PositiveUsizeAcceptsOnlyExactPositiveIntegers() {
    shell::ComponentArgument six = NumberArgument(6.0);
    uint64_t value = 0;
    Str error;
    utassert(component_shell::retained_forms::PositiveUsize(&six, 1, "OtpState",
                                                            &value, &error) &&
             value == 6);
    const double refused[] = {0.0, -1.0, 1.5, NAN, INFINITY};
    for (double number : refused) {
        shell::ComponentArgument argument = NumberArgument(number);
        utassert(!component_shell::retained_forms::PositiveUsize(
            &argument, 1, "OtpState", &value, &error));
    }
}

// otp_leaf_contract_rejects_ordinary_children
void OtpLeafContractRejectsOrdinaryChildren() {
    Str error;
    utassert(
        !component_shell::retained_forms::EnsureLeaf(1, "OtpInput", &error));
    utassert(StrEq(error, StrL("OtpInput does not accept children")));
    utassert(
        component_shell::retained_forms::EnsureLeaf(0, "OtpInput", &error));
}

// public_host.rs all_retained_form_bindings_materialize_and_keep_their_
// state_across_frames: two renders, each building every control.
void AllRetainedFormBindingsMaterializeAcrossFrames() {
    Host host(
        StrL("import { div, View } from 'gpui-kit';\n"
             "import {\n"
             "  Calendar, CalendarState, ColorPicker, ColorPickerState,\n"
             "  DatePicker, DatePickerState, Input, InputState, NumberInput,\n"
             "  OtpInput, OtpState, Slider, SliderState, TimeField, "
             "TimeFieldState,\n"
             "} from 'gpui-component';\n"
             "export default class RetainedForms extends View {\n"
             "  init() {\n"
             "    this.input = InputState();\n"
             "    this.otp = OtpState(6);\n"
             "    this.slider = SliderState();\n"
             "    this.color = ColorPickerState();\n"
             "    this.calendar = CalendarState();\n"
             "    this.date = DatePickerState();\n"
             "    this.time = TimeFieldState();\n"
             "  }\n"
             "  render() {\n"
             "    return div()\n"
             "      .child(new Input(this.input).aria_label('Project')"
             ".disabled(false))\n"
             "      .child(new NumberInput(this.input).placeholder('Quantity')"
             ".disabled(true))\n"
             "      .child(new OtpInput(this.otp).w(320).groups(3)"
             ".disabled(false))\n"
             "      .child(new Slider(this.slider).vertical().reverse()"
             ".disabled(false))\n"
             "      .child(new ColorPicker(this.color).label('Accent')"
             ".accessibility_label('Accent color'))\n"
             "      .child(new Calendar(this.calendar).number_of_months(2))\n"
             "      .child(new DatePicker(this.date).placeholder('Choose date')"
             ".disabled(false))\n"
             "      .child(new TimeField(this.time));\n"
             "  }\n"
             "}\n"));
    for (int frame = 0; frame < 2; frame++) {
        El* root = host.Render();
        utassert(root && len(host.ViewError()) == 0);
        utassert(!FindTextPrefix(root, StrL("Failed to render")));
        utassert(FindText(root, StrL("Accent")) != nullptr);
        utassert(FindText(root, StrL("Choose date")) != nullptr);
    }
}

// retained_otp_rejects_an_ordinary_child_during_public_host_materialization
void RetainedOtpRejectsAnOrdinaryChild() {
    Host host(
        StrL("import { div, View } from 'gpui-kit';\n"
             "import { OtpInput, OtpState } from 'gpui-component';\n"
             "export default class InvalidOtp extends View {\n"
             "  init() { this.otp = OtpState(6); }\n"
             "  render() { return new OtpInput(this.otp).child(div()"
             ".child('not allowed')); }\n"
             "}\n"));
    El* root = host.Render();
    utassert(root && len(host.ViewError()) == 0);
    utassert(FindText(root, StrL("Failed to render OtpInput")) != nullptr);
    utassert(StrEq(host.runtime->LastComponentFailure(),
                   StrL("OtpInput does not accept children")));
}

// retained_state_constructor_rejects_rounded_overflow_from_js
void RetainedStateConstructorRejectsRoundedOverflowFromJs() {
    Host host(
        StrL("import { View } from 'gpui-kit';\n"
             "import { OtpInput, OtpState } from 'gpui-component';\n"
             "export default class InvalidOtpState extends View {\n"
             "  init() { this.otp = OtpState(18446744073709551616); }\n"
             "  render() { return new OtpInput(this.otp); }\n"
             "}\n"));
    Str refusal = RenderRefusal(host);
    utassert(StrContains(refusal, StrL("positive integer")));
}

// component_state_exports_do_not_shadow_same_named_gpui_base_exports
void ComponentStateExportsDoNotShadowGpuiBaseExports() {
    Host host(StrL(
        "import { View } from 'gpui-kit';\n"
        "import { InputState as BaseInputState } from 'gpui-base';\n"
        "import { Input, InputState as ComponentInputState } from "
        "'gpui-component';\n"
        "export default class CoexistingStates extends View {\n"
        "  init() {\n"
        "    this.base_state = BaseInputState.new({ placeholder: 'Search' });\n"
        "    this.component_state = ComponentInputState();\n"
        "  }\n"
        "  render() { return new Input(this.component_state)"
        ".aria_label('Name'); }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && len(host.ViewError()) == 0);
    utassert(!FindTextPrefix(root, StrL("Failed to render")));
    InputState* inputs[2] = {};
    int count = 0;
    CollectInputs(root, inputs, &count, 2);
    utassert(count == 1);
}

// registers_only_real_constructible_layout_surfaces
// resizable.rs numeric_contracts_are_closed
// resizable.rs group_rejects_style_and_wrong_children
// textarea.rs textarea_is_an_exact_leaf
// layout_host.rs layout_catalog_has_closed_real_state_and_typed_layout_
// contracts
// layout_host.rs textarea_state_survives_two_native_draws_with_methods_and_
// style: the same retained state, with its methods, across two renders.
// layout_host.rs resizable_consumes_two_real_typed_panels_with_methods_
// style_and_children
} // namespace

void TestComponentShell() {
    TestSuite("component_registry");
    StateExportsShareTheComponentExportNamespace();
    StateDescriptorsValidateExportKindAndArguments();
    FrozenRegistryModuleDeclaresStateExports();
    RetainedStateStoreEnforcesItsLimit();
    DuplicateMethodsAreRejectedWithTheComponentName();
    DuplicateExportsInsideOneDescriptorAreRejected();
    InvalidAndReservedJavaScriptExportsAreRejected();
    MethodAndArgumentNamesMustBeJavaScriptIdentifiers();
    RequiredArgumentsCannotFollowOptionalArguments();
    ArgumentSchemasAreValidatedRecursively();
    OneSignatureCannotRepeatAnArgumentName();
    DeprecatedExportsMustNameAnotherExport();
    MaterializeRequestKeepsNamedSlotsSeparate();
    MaterializeRequestTakesEveryRepeatedNamedSlotInOrder();
    TypedChildrenMaterializeExactlyOnce();
    ChildLaneStaysExclusiveAfterEitherLaneIsDrained();

    TestSuite("component-shell");
    RegisterExposesTheFirstLeafBatchInStableOrder();
    LeafDescriptorsPublishOnlyClosedHonestMethodSchemas();
    DescriptorVocabularyUsesSnakeCaseEverywhere();
    RuntimeTypingsIncludeLeafExportsAndMethods();
    ComponentDeclarationsMatchRust();
    LeafComponentsRejectAnIncompatiblePayload();
    LeafComponentsMaterializeFromAScript();
    RegisteredCallsAreValidatedAgainstTheirSchemas();

    TestSuite("controls");
    ControlsRegisterTheSupportedPublicExports();
    EveryControlDocumentsItsSurface();
    IdentityControlsRejectEmptyIds();
    ButtonOperationsReplayInRecordedOrder();
    BadgeNumbersRejectFractionalNegativeAndOverflow();
    LaterTagVariantPreservesEarlierSizeOutlineAndRounding();
    BadgeOperationsMaterializeInRecordedOrder();
    KbdParsesItsKeystroke();
    ClickingATwoStateControlReportsItsNewState();
    ClickingAButtonReachesTheScript();

    TestSuite("display");
    RegistersTheDisplayCatalogWithDocumentedCallables();
    DisplayRejectsAnEmptyComponentId();
    DisplayComponentsBuildFromAScript();
    DisplayComponentsRejectAnIncompatiblePayload();
    DisplayLeavesRefuseChildren();
    LaterValueAndMaxOperationsWinInCallOrder();
    RatingRejectsTheRoundedUsizeOverflowBoundary();
    RatingReportsTheClickedStar();

    TestSuite("basic");
    RegistersOnlyTheTwoClosedRenderableSurfaces();
    TextConstructorIsClosedAndPreservesContent();
    DropdownButtonDescriptorIsClosed();
    BasicTextAndDropdownMaterializeThroughTheHost();

    TestSuite("empty");
    EmptyPublishesAllPartsWithClosedDocumentedMethods();
    EmptySlotsReplacePreviousPartsAndPreserveChildActions();
    EmptyRejectsWrongSlotTypesAndOrdinaryHeaderChildren();

    TestSuite("compound");
    RegistersOnlyTheHonestlyMaterializableCompoundBatch();
    NumericAndControlledArgumentsHaveClosedSchemas();
    CompoundUsizeConversionRejectsFractionalNegativeAndOverflow();
    CompoundF32ConversionRejectsValuesThatWouldBecomeInfinite();
    CompoundIdsMustContainNonWhitespaceText();
    PaginationPositiveIntegerValidation();
    CompoundComponentsRejectAnIncompatiblePayload();
    CompoundComponentsBuildFromAScript();
    PublicHostMaterializesTypedCompoundChildrenInScriptOrder();
    RadioGroupReportsTheClickedIndex();

    TestSuite("typed_compound");
    WrongRegisteredChildIdentityIsRejected();
    CallbackOperationPreservesTheScriptCallbackHandle();
    TypedElementsAreExtractedFromRealElements();
    BatchPublishesClosedDocumentedDescriptors();
    TypedContainersReportTheirSelection();
    TypedContainersRefuseForeignChildren();

    TestSuite("input_group");
    InputGroupRegistersItsSixDocumentedParts();
    TextareaRowsArePositiveWholeCounts();

    TestSuite("retained_forms");
    RetainedFormsPublishMatchingStateAndComponentContracts();
    StateArgumentsAreClosedAndComponentStateKindsMatch();
    PositiveUsizeRejectsValuesThatRoundPastUsizeMax();
    PositiveUsizeAcceptsOnlyExactPositiveIntegers();
    OtpLeafContractRejectsOrdinaryChildren();
    AllRetainedFormBindingsMaterializeAcrossFrames();
    RetainedOtpRejectsAnOrdinaryChild();
    RetainedStateConstructorRejectsRoundedOverflowFromJs();
    ComponentStateExportsDoNotShadowGpuiBaseExports();
}
