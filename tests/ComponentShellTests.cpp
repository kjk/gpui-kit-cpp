/* Ports of crates/shell/src/component_registry.rs's tests and of
   crates/component-shell's: src/lib.rs, the family modules' own tests, and
   the tests/*_host.rs files as far as each is reachable without
   TestAppContext. A host test here loads the script into a runtime built with
   the catalog and walks the element tree the first render builds. */

#include "Test.h"

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
}
