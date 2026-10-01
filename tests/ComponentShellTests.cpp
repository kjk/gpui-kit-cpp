/* Ports of crates/shell/src/component_registry.rs's tests and of
   crates/component-shell's: src/lib.rs, the family modules' own tests, and
   the tests/ *_host.rs files as far as each is reachable without
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
using shell::FsEntry;
using shell::FsOperation;
using shell::FsResult;
using shell::FsRun;
using shell::MethodDescriptor;
using shell::RegistryError;
using shell::RegistryErrorKind;
using shell::ShellCheckApplication;
using shell::ShellWriteTypeDeclarations;
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
        ("size(size: \"xsmall\" | \"small\" | \"medium\" | \"large\"): "
         "SpinnerElement;"),
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

// An application directory and the entry inside it, loaded the way the
// public host's `load_application` loads one.
struct AppDir {
    Str directory;
    Str entry;
};

// Loads `source` into a runtime built with the catalog, renders it once and
// answers the root element (null with `error` set when it failed).
struct Host {
    App app;
    Window window;
    ShellRuntime* runtime = nullptr;
    Entity<ScriptView> view = {};
    Arena* frame = nullptr;
    ShellError error = {};

    explicit Host(Str source,
                  const FrozenComponentRegistry* catalog = nullptr) {
        Start(catalog);
        ViewType* type =
            runtime ? runtime->LoadSource(StrL("main.js"), source, &error)
                    : nullptr;
        if (type) view = ScriptView::New(&app, runtime, type);
        ViewTypeRelease(type);
    }
    explicit Host(AppDir dir) {
        Start(nullptr);
        ViewType* type =
            runtime ? runtime->LoadApp(dir.directory, dir.entry, &error)
                    : nullptr;
        if (type) view = ScriptView::New(&app, runtime, type);
        ViewTypeRelease(type);
    }
    void Start(const FrozenComponentRegistry* catalog) {
        window.app = &app;
        component_shell::Init(&app);
        runtime = ShellRuntime::New(
            &app, &error, catalog ? catalog : component_shell::Components());
        frame = ArenaNew();
        window.frameArena = frame;
    }
    El* Render() {
        if (!view.IsValid()) return nullptr;
        return EntityRender(&app, &window, frame, view.id);
    }
    // Rebuilds the description on the next render even though nothing the
    // view owns changed: what Rust's `view.refresh(cx)` asks for.
    void Refresh() {
        ScriptView* live = view.Get(&app);
        if (!live) return;
        Ctx cx = {&app, &window, frame, view.id};
        ScriptView::Refresh(live, &cx);
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

static bool PathTo(El* element, El* target, El** path, int* depth, int cap) {
    if (!element || *depth >= cap) return false;
    path[(*depth)++] = element;
    if (element == target) return true;
    for (El* child = element->first; child; child = child->next) {
        if (PathTo(child, target, path, depth, cap)) return true;
    }
    (*depth)--;
    return false;
}

// A click as the window dispatches it: on_click bubbles, so every element
// from `target` out to `root` that takes one hears it, innermost first.
void ClickBubbling(Host& host, El* root, El* target) {
    El* path[256];
    int depth = 0;
    if (!PathTo(root, target, path, &depth, 256)) return;
    ClickEvent event = {};
    for (int i = depth - 1; i >= 0; i--) {
        if (path[i]->listener.IsValid())
            ListenerCall(&host.app, &host.window, path[i]->listener, &event);
    }
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

// link.rs legacy_link_preserves_disabled_behavior: `disabled` is stored
// and inert, so a disabled Link still runs its handler, in the link colour.
void ADisabledLinkStillTakesItsClick() {
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { Link } from 'gpui-component';\n"
             "export default class Main extends View {\n"
             "  init() { this.hits = 0; }\n"
             "  render() {\n"
             "    return div()\n"
             "      .child(new Link('legacy-link').disabled(true)\n"
             "        .on_click((_event, cx) => { this.hits += 1; "
             "cx.notify(); })\n"
             "        .child('Visible link'))\n"
             "      .child(`Hits: ${this.hits}`);\n"
             "  }\n"
             "}\n"));
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    El* link = ListenerAbove(root, StrL("Visible link"));
    utassert(link != nullptr);
    if (link) {
        utassert(link->style.hasColor &&
                 link->style.color.r == ThemeNow(&host.app).link.r &&
                 link->style.color.b == ThemeNow(&host.app).link.b);
        Click(host, link);
    }
    root = host.Render();
    utassert(FindText(root, StrL("Hits: 1")) != nullptr);
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

// The first element placed against its parent's edge rather than in flow:
// an open dropdown's menu.
static El* FindAnchoredMenu(El* e) {
    if (!e) return nullptr;
    if (e->style.anchorBelow || e->style.anchorAbove) return e;
    for (El* c = e->first; c; c = c->next) {
        if (El* found = FindAnchoredMenu(c)) return found;
    }
    return nullptr;
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
    // menu_anchor('bottom_right') names the menu's own corner: it stands on
    // the caret and opens upward, right edges lined up.
    El* menu = FindAnchoredMenu(root);
    utassert(menu != nullptr);
    if (menu) {
        utassert(menu->style.anchorAbove && !menu->style.anchorBelow);
        utassert(menu->style.absRight == 0);
    }
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
    if (question) ClickBubbling(host, root, question);
    root = host.Render();
    utassert(FindText(root, StrL("state: 1|1|1")) != nullptr);
}

// accordion.rs: on_toggle is the root's on_click, so every click in the
// accordion reports the open set — one through a trigger after the item
// flipped, one in an item's content with the set as it was.
void AccordionReportsEveryClickInIt() {
    Host host(StrL(
        "import { View, div } from 'gpui-kit';\n"
        "import { Accordion, AccordionItem } from 'gpui-component';\n"
        "export default class Main extends View {\n"
        "  init() { this.calls = 0; this.open = 'none'; }\n"
        "  render() {\n"
        "    return div().w(600)\n"
        "      .child(new Accordion('faq')\n"
        "        .on_toggle((open, cx) => { this.calls += 1; "
        "this.open = open.join(',') || 'none'; cx.notify(); })\n"
        "        .child(new AccordionItem().title(div().child('Question A'))"
        ".open(true).child(div().child('Answer A')))\n"
        "        .child(new AccordionItem().title(div().child('Question B'))"
        ".child('Answer B')))\n"
        "      .child(`calls: ${this.calls}|${this.open}`);\n"
        "  }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(FindText(root, StrL("calls: 0|none")) != nullptr);
    El* answer = ListenerAbove(root, StrL("Answer A"));
    utassert(answer != nullptr);
    if (answer) ClickBubbling(host, root, answer);
    root = host.Render();
    utassert(FindText(root, StrL("calls: 1|0")) != nullptr);
    El* question = ListenerAbove(root, StrL("Question B"));
    utassert(question != nullptr);
    if (question) ClickBubbling(host, root, question);
    root = host.Render();
    utassert(FindText(root, StrL("calls: 2|1")) != nullptr);
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
// the listener the button carries.
void EmptySlotsReplacePreviousPartsAndPreserveChildActions() {
    Host host(StrL(
        "import { div, View } from 'gpui-kit';\n"
        "import {\n"
        "  Empty, EmptyHeader, EmptyMedia, EmptyTitle, EmptyDescription, "
        "EmptyContent, Button, Icon,\n"
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
        "            .child(new Icon('folder')))))\n"
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
    const char* texts[] = {"Hits: 0",           "Create project",
                           "No projects",       "Create your first project.",
                           "Standalone header", "Standalone media",
                           "Standalone title",  "Standalone description",
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
// input_tokens.rs: the change host is keyed by the element's path and the
// state, so two components rendering one InputState both hear its change,
// and one that stops rendering stops hearing it.
void TwoComponentsOnOneStateBothHearItsChange() {
    Host host(StrL(
        "import { div, View } from 'gpui-kit';\n"
        "import { InputGroup, InputGroupInput, InputState } "
        "from 'gpui-component';\n"
        "export default class Shared extends View {\n"
        "  init() { this.input = InputState('Shared'); this.a = 0; this.b = 0;"
        " this.second = true; }\n"
        "  render() {\n"
        "    return div().w(400)\n"
        "      .child(new InputGroup('first').input(new InputGroupInput("
        "this.input)\n"
        "        .on_change((_value, cx) => { this.a += 1; cx.notify(); })))\n"
        "      .when(this.second, (d) => d.child(new InputGroup('second')"
        ".input(new InputGroupInput(this.input)\n"
        "        .on_change((_value, cx) => { this.b += 1; this.second = false;"
        " cx.notify(); }))))\n"
        "      .child(`a:${this.a};b:${this.b}`);\n"
        "  }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && len(host.ViewError()) == 0);
    InputState* inputs[4] = {};
    int count = 0;
    CollectInputs(root, inputs, &count, 4);
    utassert(count == 1);
    if (count < 1) return;
    InputState* shared = inputs[0];
    TypeInto(host, shared, "x");
    root = host.Render();
    utassert(FindText(root, StrL("a:1;b:1")) != nullptr);
    // The second group is gone now, and with it its subscription.
    TypeInto(host, shared, "y");
    root = host.Render();
    utassert(FindText(root, StrL("a:2;b:1")) != nullptr);
}

void InputGroupRetainsTextCallbacksAndRoutesAddonActions() {
    Host host(StrL(
        "import { div, View } from 'gpui-kit';\n"
        "import { InputGroup, InputGroupInput, InputGroupTextarea, "
        "InputGroupAddon,\n"
        "  InputGroupButton, InputGroupText, InputState, TextareaState, Button "
        "} from 'gpui-component';\n"
        "export default class InputGroupHost extends View {\n"
        "  init() {\n"
        "    this.input = InputState('Search');\n"
        "    this.textarea = TextareaState();\n"
        "    this.value = ''; this.message = ''; this.changes = 0;\n"
        "    this.clicks = 0; this.disabled = false;\n"
        "  }\n"
        "  render() {\n"
        "    return div().relative().w(500).h(400)\n"
        "      .child(new InputGroup('search-group').absolute().left(0).top(0)"
        ".w(400).disabled(this.disabled)\n"
        "        .input(new InputGroupInput(this.input).child('discarded'))\n"
        "        .input(new InputGroupInput(this.input).value(this.value)"
        ".aria_label('Search').px(16)\n"
        "          .on_change((value, cx) => { this.value = value; "
        "this.changes += 1; cx.notify(); }))\n"
        "        .addon(new InputGroupAddon('leading').w(64)\n"
        "          .child(new InputGroupText().child('Find')))\n"
        "        .addon(new InputGroupAddon('actions').align('inline-end')\n"
        "          .child(new InputGroupButton('replace').w(80)"
        ".label('Replace').size('small')\n"
        "            .on_click((_event, cx) => { this.clicks += 1; "
        "this.value = 'server'; cx.notify(); }))\n"
        "          .child(new Button('between-actions').w(24).label('/')"
        ".disabled(true))\n"
        "          .child(new InputGroupButton('last-action').label('Last')"
        ".icon('icons/check.svg'))))\n"
        "      .child(new InputGroup('message-group').absolute().left(0)"
        ".top(80).w(400)\n"
        "        .input(new InputGroupTextarea(this.textarea)"
        ".value(this.message).placeholder('Message')\n"
        "          .auto_grow(1, 4).aria_label('Message').text_base()\n"
        "          .on_change((value, cx) => { this.message = value; "
        "cx.notify(); }))\n"
        "        .addon(new InputGroupAddon('header').align('block-start')"
        ".child('Message header'))\n"
        "        .addon(new InputGroupAddon('footer').align('block-end')"
        ".child('Message footer')))\n"
        "      .child(new Button('disable').absolute().left(0).top(300)"
        ".w(100).h(30).label('Disable')\n"
        "        .on_click((_event, cx) => { this.disabled = !this.disabled; "
        "cx.notify(); }))\n"
        "      .child(div().absolute().left(0).top(340)\n"
        "        .child(`value:${this.value};changes:${this.changes};"
        "clicks:${this.clicks};message:${this.message}`));\n"
        "  }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && len(host.ViewError()) == 0);
    utassert(!FindTextPrefix(root, StrL("Failed to render")));
    utassert(FindText(root, StrL("value:;changes:0;clicks:0;message:")));
    // The replaced control is never materialized.
    utassert(!FindText(root, StrL("discarded")));
    utassert(FindText(root, StrL("Find")) && FindText(root, StrL("Last")));
    utassert(FindText(root, StrL("Message header")) &&
             FindText(root, StrL("Message footer")));
    utassert(Precedes(root, StrL("replace"), StrL("between-actions")) &&
             Precedes(root, StrL("between-actions"), StrL("last-action")));

    InputState* inputs[4] = {};
    int count = 0;
    CollectInputs(root, inputs, &count, 4);
    utassert(count == 2);
    if (count != 2) return;
    InputState* search = inputs[0];
    InputState* message = inputs[1];
    utassert(search->kind == InputKind::Input &&
             message->kind == InputKind::Textarea);
    utassert(StrEq(search->placeholder, StrL("Search")));
    utassert(StrEq(message->placeholder, StrL("Message")));
    utassert(message->mode.kind == LayoutModeKind::AutoGrow &&
             message->mode.minRows == 1 && message->mode.maxRows == 4);

    TypeInto(host, search, "abc");
    root = host.Render();
    utassert(FindText(root, StrL("value:abc;changes:3;clicks:0;message:")));

    El* replace = ListenerAbove(root, StrL("Replace"));
    utassert(replace != nullptr);
    if (replace) Click(host, replace);
    root = host.Render();
    utassert(FindText(root, StrL("value:server;changes:3;clicks:1;message:")));
    utassert(StrEq(InputValue(search), StrL("server")));
    // Programmatic value synchronization must not echo a change or loop.
    root = host.Render();
    utassert(FindTextPrefix(root, StrL("value:server;changes:3;")));

    El* disable = ListenerAbove(root, StrL("Disable"));
    utassert(disable != nullptr);
    if (disable) Click(host, disable);
    root = host.Render();
    // A disabled group disables its addon buttons: nothing to click.
    utassert(ListenerAbove(root, StrL("Replace")) == nullptr);
    utassert(FindText(root, StrL("value:server;changes:3;clicks:1;message:")));

    TypeInto(host, message, "hello");
    root = host.Render();
    utassert(FindTextPrefix(root, StrL("value:server;changes:3;clicks:1;"
                                       "message:hello")));
}

// input_group_host.rs input_group_rejects_wrong_part_types_and_invalid_
// layout_options.
void InputGroupRejectsWrongPartTypesAndInvalidLayoutOptions() {
    struct Case {
        const char* expression;
        const char* diagnostic;
    };
    const Case cases[] = {
        {"new InputGroup('g').child(div())",
         "InputGroup does not accept ordinary children"},
        {"new InputGroup('g').input(new InputGroupText())", "InputGroupInput"},
        {"new InputGroup('g').addon(new InputGroupText())", "InputGroupAddon"},
        {"new InputGroupInput(this.textarea)", "InputState"},
        {"new InputGroupTextarea(this.textarea).auto_grow(4, 2)", "max_rows"},
        {"new InputGroupTextarea(this.textarea).rows(0)", "positive integer"},
        {"new InputGroupAddon('a').align('left')", "align"},
        {"new InputGroupButton('b').size('giant')", "size"},
        {"new InputGroupButton('b').size('icon-small')", "size"},
        {"new InputGroupInput(this.input).content_type('unknown')",
         "content_type"},
    };
    for (const Case& c : cases) {
        TempStr source = fmt(
            "import { View, div } from 'gpui-kit';\n"
            "import { InputGroup, InputGroupInput, InputGroupTextarea, "
            "InputGroupAddon,\n"
            "  InputGroupButton, InputGroupText, InputState, TextareaState, "
            "Button } from 'gpui-component';\n"
            "export default class Invalid extends View {\n"
            "  init() { this.input = InputState(); this.textarea = "
            "TextareaState(); }\n"
            "  render() { return %s; }\n"
            "}\n",
            Str(c.expression));
        Host host(source);
        Str refusal = RenderRefusal(host);
        if (!StrContains(refusal, Str(c.diagnostic)))
            printf("%s: %.*s\n", c.expression, len(refusal),
                   refusal.s ? refusal.s : "");
        utassert(StrContains(refusal, Str(c.diagnostic)));
    }
}

// ─── input_tokens.rs, and inline_tokens_host.rs ────────────────────────────

// Walks for the first element carrying a click Func0 (a token chip's
// activation) and runs it.
bool ClickFirstToken(El* element) {
    if (!element) return false;
    if (element->onClick.IsValid()) {
        element->onClick.Call();
        return true;
    }
    for (El* child = element->first; child; child = child->next) {
        if (ClickFirstToken(child)) return true;
    }
    return false;
}

// inline_tokens_host.rs inline_tokens_script_operations_and_click_reentry.
// The token chip's click is its activation handler, run directly.
void InlineTokensScriptOperationsAndClickReentry() {
    Host host(StrL(
        "import { div, View } from 'gpui-kit';\n"
        "import { Input, InputState, Textarea, TextareaState } from "
        "'gpui-component';\n"
        "import { Button as BaseButton, InputState as BaseInputState, "
        "TextareaState as BaseTextareaState } from 'gpui-base';\n"
        "function assert(value, message) { if (!value) throw new "
        "Error(message); }\n"
        "function exercise(state) {\n"
        "  state.set_value('🙂 @a!');\n"
        "  state.replace_range_with_token({start: 3, end: 5}, {id: 'a', text: "
        "'@a', label: 'Alice'});\n"
        "  const saved = state.content();\n"
        "  assert(saved.tokens[0].range.start === 3, 'UTF-16 range');\n"
        "  let code = '';\n"
        "  try { state.replace_range_with_token({start: 1, end: 2}, {id: "
        "'bad', text: 'x'}); } catch (error) { code = error.code; }\n"
        "  assert(code === 'InvalidBoundary', 'surrogate boundary must fail "
        "with code');\n"
        "  assert(JSON.stringify(state.content()) === JSON.stringify(saved), "
        "'failure must be atomic');\n"
        "  state.set_selected_range({start: 4, end: 5}); state.replace('');\n"
        "  assert(state.value() === '🙂 !' && state.tokens().length === 0, "
        "'partial token deletion');\n"
        "  state.set_value(saved);\n"
        "  state.set_value(state.value());\n"
        "  assert(state.tokens().length === 0, 'explicit same value clears "
        "identity');\n"
        "  state.set_value(saved);\n"
        "  return state;\n"
        "}\n"
        "export default class TokenHost extends View {\n"
        "  init() {\n"
        "    this.input = exercise(InputState());\n"
        "    this.textarea = exercise(TextareaState());\n"
        "    this.child = exercise(InputState());\n"
        "    this.base = exercise(BaseInputState.new());\n"
        "    this.baseArea = exercise(BaseTextareaState.new());\n"
        "    this.status = 'verified';\n"
        "  }\n"
        "  render() {\n"
        "    return div().relative().w(400).h(260)\n"
        "      .child(new Input(this.input).w(350).aria_label('Token input')\n"
        "        .token(token => div().w(80).h(20).child(token.token.label))\n"
        "        .on_token_click((event, cx) => {\n"
        "          assert(event.token.id === 'a', 'current identity');\n"
        "          this.input.set_value('opened'); this.status = 'clicked'; "
        "cx.notify();\n"
        "        }))\n"
        "      .child(new Textarea(this.textarea).w(350).h(60))\n"
        "      .child(new "
        "Input(this.child).absolute().top(140).left(0).w(350)\n"
        "        .token(token => div().flex().w(100).h(20).child(div().w(70)"
        ".child(token.token.label))\n"
        "          .child(BaseButton.new('remove-token-child').w(30).h(20)"
        ".child('×')\n"
        "            .on_mouse_down('left', (_event, cx) => "
        "cx.stop_propagation())\n"
        "            .on_click((_event, cx) => { "
        "this.child.set_value('removed'); this.status = 'child'; "
        "cx.stop_propagation(); cx.notify(); })))\n"
        "        .on_token_click((_event, cx) => { this.status = 'wrong body "
        "activation'; cx.notify(); }))\n"
        "      .child(div().child(`${this.status}:${this.input.value()}:"
        "${this.input.tokens().length};child=${this.child.tokens().length}:"
        "${this.child.value()}`));\n"
        "  }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && len(host.ViewError()) == 0);
    utassert(FindTextPrefix(root, StrL("verified:🙂 @a!:1")) != nullptr);
    utassert(FindText(root, StrL("Alice")) != nullptr);
    // A rerender preserves identity.
    root = host.Render();
    utassert(FindTextPrefix(root, StrL("verified:🙂 @a!:1")) != nullptr);
    utassert(ClickFirstToken(root));
    root = host.Render();
    utassert(FindTextPrefix(root, StrL("clicked:opened:0")) != nullptr);
    // The custom child's callback survives the frame and consumes its own
    // gesture.
    El* remove = ListenerAbove(root, StrL("×"));
    utassert(remove != nullptr);
    if (remove) Click(host, remove);
    root = host.Render();
    utassert(FindTextPrefix(root, StrL("child:opened:0")) != nullptr);
}

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

// ─── layout/: mod.rs, textarea.rs, resizable.rs, and layout_host.rs ────────

// registers_only_real_constructible_layout_surfaces
void RegistersOnlyRealConstructibleLayoutSurfaces() {
    FamilyCatalog catalog(&component_shell::RegisterLayout);
    utassert(catalog.ok);
    utassert(catalog.frozen.StateCount() == 1 &&
             strcmp(catalog.frozen.State(0)->exportName, "TextareaState") == 0);
    const char* names[] = {"Textarea", "ResizablePanel", "Resizable"};
    utassert(catalog.NamesAre(names, 3));
}

// resizable.rs numeric_contracts_are_closed
void ResizableNumericContractsAreClosed() {
    using component_shell::layout::resizable::FinitePositive;
    float value = 0;
    Str error;
    utassert(FinitePositive(1.0, "x", &value, &error));
    utassert(!FinitePositive(0.0, "x", &value, &error));
    utassert(!FinitePositive(INFINITY, "x", &value, &error));
}

// resizable.rs group_rejects_style_and_wrong_children
void ResizableGroupRejectsStyleAndWrongChildren() {
    using namespace component_shell::layout::resizable;
    Str error;
    utassert(RequireGroupStyle(false, &error));
    utassert(!RequireGroupStyle(true, &error));
    utassert(RequirePanelChild("ResizablePanel", &error));
    utassert(!RequirePanelChild("Textarea", &error));
    utassert(!RequirePanelChild(nullptr, &error));
    utassert(StrContains(error, StrL("ordinary element")));
}

// textarea.rs textarea_is_an_exact_leaf
void TextareaIsAnExactLeaf() {
    Str error;
    utassert(component_shell::layout::textarea::RequireLeaf(0, &error));
    utassert(!component_shell::layout::textarea::RequireLeaf(1, &error));
    utassert(StrEq(error, StrL("Textarea does not accept children")));
}

// layout_host.rs layout_catalog_has_closed_real_state_and_typed_layout_
// contracts
void LayoutCatalogHasClosedStateAndTypedLayoutContracts() {
    FamilyCatalog catalog(&component_shell::RegisterLayout);
    const ComponentDescriptor* textarea = catalog.frozen.Find(StrL("Textarea"));
    utassert(textarea != nullptr);
    if (!textarea) return;
    const ArgumentSchema& state = textarea->constructors[0].arguments[0].schema;
    utassert(state.kind == shell::SchemaKind::Entity &&
             strcmp(state.text, "TextareaState") == 0);
    bool readonly = false;
    for (const MethodDescriptor& method : textarea->methods)
        readonly = readonly || strcmp(method.name, "readonly") == 0;
    utassert(readonly);
    const ComponentDescriptor* resizable = catalog.frozen
                                               .Find(StrL("Resizable"));
    utassert(resizable != nullptr);
    if (!resizable) return;
    const ArgumentSchema& axis = resizable->methods[0].arguments[0].schema;
    utassert(axis.kind == shell::SchemaKind::Enum && axis.values.count == 2 &&
             strcmp(axis.values[0], "horizontal") == 0 &&
             strcmp(axis.values[1], "vertical") == 0);
    utassert(!catalog.frozen.Find(StrL("Scrollbar")) &&
             !catalog.frozen.Find(StrL("Scroll")));
}

// layout_host.rs textarea_state_survives_two_native_draws_with_methods_and_
// style: the same retained state, with its methods, across two renders.
void TextareaStateSurvivesTwoNativeDraws() {
    Host host(StrL(
        "import { View } from 'gpui-kit';\n"
        "import { Textarea, TextareaState } from 'gpui-component';\n"
        "export default class App extends View {\n"
        "  init() { this.editor = TextareaState('Draft'); }\n"
        "  render() { return new Textarea(this.editor).appearance(true)"
        ".bordered(false).readonly(true).aria_label('Notes').disabled(false)"
        ".p(2).h(120); }\n"
        "}\n"));
    InputState* first = nullptr;
    for (int frame = 0; frame < 2; frame++) {
        El* root = host.Render();
        utassert(root && len(host.ViewError()) == 0);
        utassert(!FindTextPrefix(root, StrL("Failed to render")));
        InputState* inputs[2] = {};
        int count = 0;
        CollectInputs(root, inputs, &count, 2);
        utassert(count == 1);
        if (count != 1) return;
        utassert(inputs[0]->kind == InputKind::Textarea);
        utassert(StrEq(InputValue(inputs[0]), StrL("Draft")));
        if (!first) first = inputs[0];
        utassert(first == inputs[0]);
        // The style refines the textarea's own box.
        utassert(root->style.height == 120);
    }
}

// layout_host.rs resizable_consumes_two_real_typed_panels_with_methods_
// style_and_children
void ResizableConsumesTwoTypedPanels() {
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { Resizable, ResizablePanel } from 'gpui-component';\n"
             "export default class App extends View { render() { return new "
             "Resizable('workspace').axis('horizontal').cross_size(240)\n"
             "  .child(new ResizablePanel().size(180).size_range(100,260).p(2)"
             ".child(div().child('Navigation')))\n"
             "  .child(new ResizablePanel().visible(true).child(div()"
             ".child('Content'))); } }\n"));
    El* root = host.Render();
    utassert(root && len(host.ViewError()) == 0);
    utassert(!FindTextPrefix(root, StrL("Failed to render")));
    utassert(FindText(root, StrL("Navigation")) != nullptr);
    utassert(FindText(root, StrL("Content")) != nullptr);
    utassert(FindById(root, StrL("resizable-panel-0")) != nullptr);
    utassert(FindById(root, StrL("resizable-panel-1")) != nullptr);
    utassert(FindById(root, StrL("resizable-panel-2")) == nullptr);

    // A group has no box of its own to style, and takes only panels.
    Host styled(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { Resizable, ResizablePanel } from 'gpui-component';\n"
             "export default class App extends View { render() { return new "
             "Resizable('workspace').p(2).child(new ResizablePanel()); } }\n"));
    utassert(StrContains(RenderRefusal(styled),
                         StrL("Resizable does not implement Styled")));
    Host foreign(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { Resizable } from 'gpui-component';\n"
             "export default class App extends View { render() { return new "
             "Resizable('workspace').child(div()); } }\n"));
    utassert(StrContains(RenderRefusal(foreign),
                         StrL("received an ordinary element")));
}

// The description a Host's view last rendered, as Rust's
// `snapshot().debug_tree()` prints it.
Str DebugTreeTemp(Host& host) {
    ScriptView* script = host.view.Get(&host.app);
    if (!script || !script->snapshot) return {};
    Arena* a = ArenaNew();
    Str tree = script->snapshot->DebugTree(a);
    TempStr out = fmt("%s", tree);
    ArenaDelete(a);
    return out;
}

// ─── chat.rs ───────────────────────────────────────────────────────────────

// lib.rs main_chat_components_are_registered
void MainChatComponentsAreRegistered() {
    const FrozenComponentRegistry* frozen = component_shell::Components();
    const char* expected[] = {"Attachment", "Bubble",          "Marker",
                              "Message",    "MessageScroller", "ShimmerText"};
    for (const char* name : expected)
        utassert(frozen->Find(Str(name)) != nullptr);
    utassert(frozen->StateOfKind(StrL("MessageScrollerState")) != nullptr);
}

// chat_host.rs chat_components_materialize_through_the_public_host
void ChatComponentsMaterializeThroughThePublicHost() {
    Host host(StrL(
        "import { div, View } from 'gpui-kit';\n"
        "import {\n"
        "  Attachment, Bubble, Marker, Message, MessageScroller,\n"
        "  MessageScrollerState, ShimmerText,\n"
        "} from 'gpui-component';\n"
        "export default class ChatHost extends View {\n"
        "  init() { this.scroller = MessageScrollerState(2); }\n"
        "  render() {\n"
        "    const rows = ['first', 'second'];\n"
        "    return div()\n"
        "      .child(new Attachment('attachment').status('complete')"
        ".child('report.pdf'))\n"
        "      .child(new Bubble().alignment('end').variant('filled')"
        ".child('bubble'))\n"
        "      .child(new Marker('marker').variant('separator').loading(true)"
        ".child('marker'))\n"
        "      .child(new Message().alignment('start').child('message'))\n"
        "      .child(new ShimmerText('thinking').id('shimmer')"
        ".duration_ms(900))\n"
        "      .child(new MessageScroller('messages', this.scroller,\n"
        "        (index) => div().child(rows[index]))\n"
        "        .h(120).scrollbar(true).jump_button_label('Latest'));\n"
        "  }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(!FindTextPrefix(root, StrL("Failed to render")));
    utassert(!host.runtime || len(host.runtime->LastComponentFailure()) == 0);
    Str tree = DebugTreeTemp(host);
    const char* expected[] = {"Attachment", "Bubble",      "Marker",
                              "Message",    "ShimmerText", "MessageScroller"};
    for (const char* name : expected) utassert(StrContains(tree, Str(name)));
    utassert(FindText(root, StrL("report.pdf")) != nullptr);
    utassert(FindText(root, StrL("bubble")) != nullptr);
    utassert(FindText(root, StrL("message")) != nullptr);
}

// The recorders' own refusals, which the schemas let through.
void ChatRecordersRefuseWhatRustRefuses() {
    struct Case {
        const char* call;
        const char* message;
    };
    const Case cases[] = {
        {"new Attachment(' ')", "Attachment expects a non-empty stable id"},
        {"new Marker('')", "Marker expects a non-empty stable id"},
        {"new ShimmerText('x').id(' ')",
         "ShimmerText.id expects a non-empty stable id"},
        {"new ShimmerText('x').duration_ms(-1)",
         "ShimmerText.duration_ms expects a finite non-negative duration"},
        {"new ShimmerText('x').spread(1e300)",
         "ShimmerText.spread expects a finite f32 fraction"},
        {"new MessageScroller(' ', MessageScrollerState(1), () => null)",
         "MessageScroller expects a non-empty id, MessageScrollerState, and "
         "row renderer"},
        {"new MessageScroller('m', MessageScrollerState(1), () => null)"
         ".jump_button_label(' ')",
         "MessageScroller.jump_button_label expects non-empty text"},
        {"new MessageScroller('m', MessageScrollerState(1.5), () => null)",
         "MessageScrollerState expects a non-negative integer item_count"},
    };
    for (const Case& c : cases) {
        Str message = CallErrorTemp(
            "Attachment, Marker, ShimmerText, MessageScroller, "
            "MessageScrollerState",
            c.call);
        if (!StrContains(message, Str(c.message)))
            printf("chat refusal: %s -> %s\n", c.call, message.s);
        utassert(StrContains(message, Str(c.message)));
    }
    Host leaf(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { ShimmerText } from 'gpui-component';\n"
             "export default class App extends View { render() { "
             "return new ShimmerText('x').child(div()); } }\n"));
    utassert(StrContains(RenderRefusal(leaf),
                         StrL("ShimmerText does not accept children")));
    Str failure;
    utassert(MaterializeDirect("Bubble", shell::ComponentPayload{}, &failure) ==
             nullptr);
    utassert(StrEq(failure,
                   StrL("chat component received an incompatible payload")));
    StrFree(failure);
}

// The rows a MessageScroller renders come from the script's renderer.
void MessageScrollerRendersScriptRows() {
    Host host(
        StrL("import { div, View } from 'gpui-kit';\n"
             "import { MessageScroller, MessageScrollerState } from "
             "'gpui-component';\n"
             "export default class App extends View {\n"
             "  init() { this.scroller = MessageScrollerState(2); }\n"
             "  render() { const rows = ['first', 'second'];\n"
             "    return new MessageScroller('messages', this.scroller,\n"
             "      (index) => div().child(rows[index])).h(120); }\n"
             "}\n"));
    El* root = host.Render();
    utassert(root && len(host.ViewError()) == 0);
    utassert(root && root->style.height == 120);
    // The virtual list builds the visible rows while it lays out.
    host.window.paint.app = &host.app;
    host.window.paint.window = &host.window;
    if (root) LayoutEl(&host.window.paint, root, 0, 0, 300, 400, 14, Rgba{});
    utassert(FindText(root, StrL("first")) != nullptr);
    utassert(FindText(root, StrL("second")) != nullptr);
}

// ─── media/: mod.rs, image.rs, editor.rs ───────────────────────────────────

// image.rs image_sources_are_confined_to_the_asset_root
void ImageSourcesAreConfinedToTheAssetRoot() {
    using component_shell::media::image::AssetPath;
    Str out, error;
    utassert(AssetPath(StrL("assets/pixel.svg"), &out, &error) &&
             StrEq(out, StrL("assets/pixel.svg")));
    const char* denied[] = {"",
                            "/tmp/pixel.png",
                            "../pixel.png",
                            "a/../../pixel.png",
                            "https://example.com/a.png",
                            "https:example.com/a.png",
                            "data:image/png;base64,x",
                            "file:/tmp/a.png",
                            "..\\pixel.png"};
    for (const char* path : denied)
        utassert(!AssetPath(Str(path), &out, &error));
}

// editor.rs editor_is_an_exact_leaf
void EditorIsAnExactLeaf() {
    Str error;
    utassert(component_shell::media::editor::RequireLeaf(0, &error));
    utassert(!component_shell::media::editor::RequireLeaf(1, &error));
    utassert(StrEq(error, StrL("Editor does not accept children")));
}

// media_public_host.rs catalog_exposes_only_renderable_media_surfaces
void CatalogExposesOnlyRenderableMediaSurfaces() {
    FamilyCatalog catalog(&component_shell::RegisterMedia);
    utassert(catalog.ok);
    const char* names[] = {"Image", "Editor"};
    utassert(catalog.NamesAre(names, 2));
    utassert(catalog.frozen.StateCount() == 1 &&
             strcmp(catalog.frozen.State(0)->exportName, "EditorState") == 0);
}

// media_public_host.rs's EditorProbe: what each render saw of the state.
struct EditorObservation {
    const void* state;
    Str value;
};
EditorObservation gEditorObservations[8];
int gEditorObservationCount = 0;

bool EditorProbePayload(shell::PayloadBuild* build,
                        const shell::ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Entity)
        return build->Fail(StrL("EditorProbe expects EditorState"));
    *build->New<shell::ComponentArgument>() = args[0];
    return true;
}

El* EditorProbeMaterialize(shell::MaterializeRequest* request) {
    const shell::ComponentArgument* argument =
        request->PayloadAs<shell::ComponentArgument>();
    if (!argument) return request->Fail(StrL("probe payload"));
    auto* state =
        request->StateAs<component_shell::media::editor::EditorStateValue>(
            *argument, "EditorState");
    if (!state) return nullptr;
    if (gEditorObservationCount < 8) {
        gEditorObservations[gEditorObservationCount++] = {
            state, StrDup(InputValue(&state->text.input))};
    }
    return Div(request->cx->a);
}

constexpr ArgumentDescriptor kEditorProbeArgs[] = {
    {"state", shell::SchemaEntity("EditorState")}};
constexpr ConstructorDescriptor kEditorProbeConstructors[] = {
    {"EditorProbe", kEditorProbeArgs, &EditorProbePayload}};
constexpr ComponentDescriptor kEditorProbe = {"EditorProbe",
                                              kEditorProbeConstructors,
                                              {},
                                              "Test-only retained state probe.",
                                              &EditorProbeMaterialize};

// media_public_host.rs local_image_and_retained_editor_cross_the_public_
// host_and_draw
void LocalImageAndRetainedEditorCrossThePublicHost() {
    ComponentRegistry registry;
    RegistryError error;
    registry.Open(shell::kComponentRegistryApiVersion,
                  shell::kDefaultComponentModule, &error);
    utassert(component_shell::RegisterMedia(&registry, &error));
    utassert(registry.Register(&kEditorProbe, &error));
    FrozenComponentRegistry frozen;
    registry.Freeze(&frozen);
    gEditorObservationCount = 0;
    {
        Host host(
            StrL("import { div, View } from 'gpui-kit';\n"
                 "import { Editor, EditorProbe, EditorState, Image } from "
                 "'gpui-component';\n"
                 "export default class Media extends View {\n"
                 "  init() { this.editor = EditorState('fn main() {}'); }\n"
                 "  render() { return div()\n"
                 "    .child(new Image('assets/pixel.svg').w(24).h(24))\n"
                 "    .child(new Editor(this.editor).appearance(true)"
                 ".bordered(false).readonly(true).aria_label('Source')"
                 ".disabled(false).p(2).h(180))\n"
                 "    .child(new EditorProbe(this.editor)); }\n"
                 "}\n"),
            &frozen);
        for (int frame = 0; frame < 2; frame++) {
            El* root = host.Render();
            utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
            utassert(!FindTextPrefix(root, StrL("Failed to render")));
            Str tree = DebugTreeTemp(host);
            const char* expected[] = {"Image", "Editor",
                                      ":readonly(registered)"};
            for (const char* part : expected)
                utassert(StrContains(tree, Str(part)));
            // The image is the asset path, styled; the editor is the
            // retained state's text.
            El* image = root ? root->first : nullptr;
            utassert(
                image && image->kind == ElKind::Image &&
                StrEq(image->imageSource.resource, StrL("assets/pixel.svg")) &&
                image->style.width == 24);
            InputState* inputs[2] = {};
            int count = 0;
            CollectInputs(root, inputs, &count, 2);
            utassert(count == 1 && inputs[0]->kind == InputKind::Editor &&
                     inputs[0]->readonly);
        }
    }
    utassert(gEditorObservationCount >= 2);
    for (int i = 0; i < gEditorObservationCount; i++) {
        utassert(StrEq(gEditorObservations[i].value, StrL("fn main() {}")));
        utassert(gEditorObservations[i].state == gEditorObservations[0].state);
        StrFree(gEditorObservations[i].value);
    }
}

// The recorders' own refusals, and an Image refusing its children.
void MediaRecordersRefuseWhatRustRefuses() {
    Str message = CallErrorTemp("Image", "new Image('../x.png')");
    utassert(StrContains(message, StrL("Image path must stay inside the "
                                       "application asset root")));
    message = CallErrorTemp("Image", "new Image(' ')");
    utassert(StrContains(message, StrL("Image path must not be empty")));
    message =
        CallErrorTemp("Editor, EditorState",
                      "new Editor(EditorState('x', 'json')).aria_label(' ')");
    utassert(StrContains(message, StrL("Editor.aria_label expects non-empty "
                                       "text")));
    Host leaf(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { Image } from 'gpui-component';\n"
             "export default class App extends View { render() { "
             "return new Image('a.png').child(div()); } }\n"));
    utassert(StrContains(RenderRefusal(leaf),
                         StrL("Image does not accept children")));
}

// ─── scroll/: mod.rs, scroll.rs ────────────────────────────────────────────

// mod.rs catalog_is_bounded_to_capability_and_real_surfaces and
// scroll_host.rs catalog_exposes_state_and_two_closed_surfaces_only
void ScrollCatalogIsBoundedToCapabilityAndRealSurfaces() {
    FamilyCatalog catalog(&component_shell::RegisterScroll);
    utassert(catalog.ok);
    utassert(catalog.frozen.StateCount() == 1 &&
             strcmp(catalog.frozen.State(0)->exportName, "ScrollbarHandle") ==
                 0);
    const char* names[] = {"Scroll", "Scrollbar"};
    utassert(catalog.NamesAre(names, 2));
    const ComponentDescriptor* scroll = catalog.frozen.Find(StrL("Scroll"));
    utassert(scroll != nullptr);
    if (!scroll) return;
    const ArgumentSchema& handle = scroll->constructors[0].arguments[0].schema;
    utassert(handle.kind == shell::SchemaKind::Entity &&
             strcmp(handle.text, "ScrollbarHandle") == 0);
}

// scroll.rs scrollbar_leaf_contract_is_exact
void ScrollbarLeafContractIsExact() {
    using component_shell::scroll::scroll::RequireLeaf;
    Str error;
    utassert(RequireLeaf(0, false, &error));
    utassert(!RequireLeaf(1, false, &error));
    utassert(!RequireLeaf(0, true, &error));
}

// scroll.rs repeated_configuration_is_last_call_wins
void RepeatedScrollConfigurationIsLastCallWins() {
    using component_shell::scroll::scroll::Op;
    using component_shell::scroll::scroll::ResolvedOps;
    Op ops[6];
    ops[0].kind = Op::Axis;
    ops[0].axis = ScrollbarAxis::Horizontal;
    ops[1].kind = Op::Mode;
    ops[1].mode = ScrollbarMode::Hover;
    ops[2].kind = Op::ViewportFromLayout;
    ops[2].flag = true;
    ops[3].kind = Op::Axis;
    ops[3].axis = ScrollbarAxis::Vertical;
    ops[4].kind = Op::Mode;
    ops[4].mode = ScrollbarMode::Always;
    ops[5].kind = Op::ViewportFromLayout;
    ops[5].flag = false;
    ResolvedOps resolved;
    for (const Op& op : ops) resolved.Fold(op);
    utassert(resolved.hasAxis && resolved.axis == ScrollbarAxis::Vertical);
    utassert(resolved.hasMode && resolved.mode == ScrollbarMode::Always);
    utassert(!resolved.viewportFromLayout);
}

El* FindIdPrefix(El* element, Str prefix) {
    if (!element) return nullptr;
    if (StrStartsWith(element->id, prefix)) return element;
    for (El* child = element->first; child; child = child->next) {
        if (El* found = FindIdPrefix(child, prefix)) return found;
    }
    return nullptr;
}

// scroll_host.rs shared_native_handle_scrolls_and_preserves_offset_across_
// refresh. The wheel is the offset the viewport's own scroll listener is
// handed; the refresh is the next render.
void SharedNativeHandleScrollsAndPreservesOffset() {
    FamilyCatalog catalog(&component_shell::RegisterScroll);
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { ScrollbarHandle, Scroll, Scrollbar } from "
             "'gpui-component';\n"
             "export default class App extends View {\n"
             "  init() { this.scroll = ScrollbarHandle(); }\n"
             "  render() { return div().relative().w(160).h(100)\n"
             "    .child(new Scroll(this.scroll).scroll_axis('vertical')"
             ".size_full()\n"
             "      .child(div().h(400).flex_shrink(0).child('Tall shared "
             "content')))\n"
             "    .child(new Scrollbar('main-scrollbar', this.scroll)"
             ".scroll_axis('vertical').mode('always')"
             ".viewport_from_layout(true)); }\n"
             "}\n"),
        &catalog.frozen);
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(FindText(root, StrL("Tall shared content")) != nullptr);
    El* viewport = FindIdPrefix(root, StrL("shell-scroll-"));
    utassert(viewport && viewport->scrollY == 0 &&
             viewport->onScroll.IsValid());
    if (!viewport) return;
    ScrollEvent wheel = {};
    wheel.id = viewport->scrollId;
    wheel.offsetY = 60;
    ListenerCall(&host.app, &host.window, viewport->onScroll, &wheel);
    for (int frame = 0; frame < 2; frame++) {
        root = host.Render();
        utassert(root && len(host.ViewError()) == 0);
        viewport = FindIdPrefix(root, StrL("shell-scroll-"));
        utassert(viewport && viewport->scrollY == 60);
        // The Scrollbar sharing the handle: its bar, in its mode, painted
        // by the viewport.
        utassert(viewport && !viewport->noScrollbarY &&
                 viewport->scrollModeSet &&
                 viewport->scrollMode == ScrollbarMode::Always);
    }
}

// scroll_host.rs native_scrollbar_rejects_children_and_shell_style_at_
// materializer_boundary
void NativeScrollbarRejectsChildrenAndShellStyle() {
    FamilyCatalog catalog(&component_shell::RegisterScroll);
    struct Case {
        const char* expression;
        const char* message;
    };
    const Case cases[] = {
        {"new Scrollbar('child-error', this.h).child(div())",
         "does not accept children"},
        {"new Scrollbar('style-error', this.h).p(2)",
         "does not support shell style"},
    };
    for (const Case& c : cases) {
        TempStr source =
            fmt("import { View, div } from 'gpui-kit';\n"
                "import { ScrollbarHandle, Scrollbar } from 'gpui-component';\n"
                "export default class App extends View { init() { this.h = "
                "ScrollbarHandle(); } render() { return %s; } }\n",
                Str(c.expression));
        Host host(source, &catalog.frozen);
        utassert(StrContains(RenderRefusal(host), Str(c.message)));
    }
}

// ─── settings/mod.rs ───────────────────────────────────────────────────────

// settings/mod.rs numeric_and_structural_contracts_are_closed
void SettingsNumericAndStructuralContractsAreClosed() {
    using component_shell::settings::Positive;
    float value = 0;
    Str error;
    utassert(Positive(1.0, "width", &value, &error) && value == 1.0f);
    utassert(!Positive(0.0, "width", &value, &error));
    utassert(!Positive(INFINITY, "width", &value, &error));
    utassert(StrEq(error, StrL("width expects a positive finite pixel value")));
    static constexpr const char* kPages[] = {"SettingPage"};
    {
        RequestFixture f(0, nullptr, 0);
        utassert(component_shell::RequireChild(&f.request, "Settings",
                                               "SettingPage", kPages));
        utassert(!component_shell::RequireChild(&f.request, "Settings",
                                                "SettingGroup", kPages));
    }
    {
        RequestFixture f(0, nullptr, 0);
        utassert(!component_shell::RequireChild(&f.request, "SettingPage",
                                                nullptr, kPages));
        utassert(StrContains(f.request.failure,
                             StrL("received an ordinary element")));
    }
    {
        RequestFixture f(0, nullptr, 0);
        utassert(component_shell::RejectStyle(&f.request, "Settings"));
    }
    {
        RequestFixture f(0, nullptr, 0);
        shell::SpecOp style = {};
        style.kind = shell::SpecOpKind::NullaryStyle;
        style.name = StrL("p_2");
        f.specs.PushOp(f.id, style);
        f.request.node = f.specs.Node(f.id);
        utassert(!component_shell::RejectStyle(&f.request, "Settings"));
        utassert(StrEq(f.request.failure,
                       StrL("Settings carries data rather than a box, so it "
                            "does not implement Styled")));
    }
}

// settings_public_host.rs catalog_names_the_real_native_hierarchy
void SettingsCatalogNamesTheRealNativeHierarchy() {
    FamilyCatalog catalog(&component_shell::RegisterSettings);
    utassert(catalog.ok);
    const char* names[] = {"SettingItem", "SettingGroup", "SettingPage",
                           "Settings"};
    utassert(catalog.NamesAre(names, 4));
    utassert(catalog.frozen.StateCount() == 0);
}

// settings_public_host.rs's LazyMarker: every label it was built with.
Str gLazyBuilds[16];
int gLazyBuildCount = 0;

bool LazyMarkerPayload(shell::PayloadBuild* build,
                       const shell::ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build->Fail(StrL("LazyMarker expects text"));
    build->New<Str>()[0] = args[0].string;
    return true;
}

El* LazyMarkerMaterialize(shell::MaterializeRequest* request) {
    const Str* label = request->PayloadAs<Str>();
    if (!label)
        return request->Fail(StrL("LazyMarker received incompatible payload"));
    if (gLazyBuildCount < 16) gLazyBuilds[gLazyBuildCount++] = *label;
    return request
        ->Finish(Div(request->cx->a)->Child(TextEl(request->cx->a, *label)));
}

constexpr ArgumentDescriptor kLazyMarkerArgs[] = {
    {"label", shell::SchemaString()}};
constexpr ConstructorDescriptor kLazyMarkerConstructors[] = {
    {"LazyMarker", kLazyMarkerArgs, &LazyMarkerPayload}};
constexpr ComponentDescriptor kLazyMarker = {
    "LazyMarker",
    kLazyMarkerConstructors,
    {},
    "Test-only lazy materialization marker.",
    &LazyMarkerMaterialize};

int LazyBuilds(const char* label) {
    int count = 0;
    for (int i = 0; i < gLazyBuildCount; i++)
        count += StrEq(gLazyBuilds[i], label) ? 1 : 0;
    return count;
}

// settings_public_host.rs full_settings_hierarchy_rebuilds_lazy_native_
// slots_across_draws. A render here is the draw; Rust's "refresh alone must
// not build" has no separate step to check.
void FullSettingsHierarchyRebuildsLazySlotsAcrossDraws() {
    ComponentRegistry registry;
    RegistryError error;
    registry.Open(shell::kComponentRegistryApiVersion,
                  shell::kDefaultComponentModule, &error);
    utassert(component_shell::RegisterSettings(&registry, &error));
    utassert(registry.Register(&kLazyMarker, &error));
    FrozenComponentRegistry frozen;
    registry.Freeze(&frozen);
    gLazyBuildCount = 0;
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { LazyMarker, Settings, SettingPage, SettingGroup, "
             "SettingItem } from 'gpui-component';\n"
             "export default class App extends View { render(){ return new "
             "Settings('prefs').size('small').sidebar_width(220)"
             ".sidebar_size_range(160,320)\n"
             " .child(new SettingPage('General').description('Application "
             "preferences').default_open(true).content(new "
             "LazyMarker('suffix-built'))\n"
             "  .child(new SettingGroup().title('Appearance')"
             ".description('Visual choices').p(2)\n"
             "   .child(new SettingItem('Theme').description('Choose "
             "appearance').layout('vertical').keywords(['color','theme'])"
             ".disabled(false).content(new LazyMarker('field-built'))))); } "
             "}\n"),
        &frozen);
    utassert(LazyBuilds("suffix-built") == 0 && LazyBuilds("field-built") == 0);
    for (int frame = 1; frame <= 2; frame++) {
        El* root = host.Render();
        utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
        utassert(!FindTextPrefix(root, StrL("Failed to render")));
        utassert(!host.runtime ||
                 len(host.runtime->LastComponentFailure()) == 0);
        Str tree = DebugTreeTemp(host);
        const char* expected[] = {"Settings", "SettingPage", "SettingGroup",
                                  "SettingItem",
                                  ":sidebar_size_range(registered)"};
        for (const char* part : expected)
            utassert(StrContains(tree, Str(part)));
        utassert(FindText(root, StrL("General")) != nullptr);
        utassert(FindText(root, StrL("Theme")) != nullptr);
        utassert(FindText(root, StrL("suffix-built")) != nullptr);
        utassert(FindText(root, StrL("field-built")) != nullptr);
        utassert(LazyBuilds("suffix-built") == frame);
        utassert(LazyBuilds("field-built") == frame);
    }
}

// The typed hierarchy's refusals.
void SettingsRejectStyleAndForeignChildren() {
    struct Case {
        const char* expression;
        const char* message;
    };
    const Case cases[] = {
        {"new Settings('s').p(2)",
         "Settings carries data rather than a box, so it does not implement "
         "Styled"},
        {"new Settings('s').child(new SettingGroup())",
         "SettingPage accepts only registered SettingPage children; received "
         "SettingGroup"},
        {"new SettingPage('P').p(1)",
         "SettingPage carries data rather than a box"},
        {"new SettingPage('P').child(div())",
         "SettingGroup accepts only registered SettingGroup children; received "
         "an ordinary element"},
        {"new SettingItem('I')", "SettingItem requires content(element)"},
    };
    for (const Case& c : cases) {
        TempStr source =
            fmt("import { View, div } from 'gpui-kit';\n"
                "import { Settings, SettingPage, SettingGroup, SettingItem } "
                "from 'gpui-component';\n"
                "export default class App extends View { render() { return "
                "%s; } }\n",
                Str(c.expression));
        Host host(source);
        Str refusal = RenderRefusal(host);
        if (!StrContains(refusal, Str(c.message)))
            printf("settings refusal: %s -> %s\n", c.expression, refusal.s);
        utassert(StrContains(refusal, Str(c.message)));
    }
    Str message = CallErrorTemp("Settings",
                                "new Settings('s')"
                                ".sidebar_size_range(300, 200)");
    utassert(StrContains(message, StrL("minimum must not exceed maximum")));
    message = CallErrorTemp("Settings", "new Settings('s').sidebar_width(0)");
    utassert(StrContains(
        message, StrL("sidebar_width expects a positive finite pixel value")));
    message = CallErrorTemp("Settings",
                            "new Settings('s').default_selected_page(1.5)");
    utassert(StrContains(
        message, StrL("default_selected_page expects a nonnegative integer")));
    message = CallErrorTemp("SettingGroup", "new SettingGroup().title(' ')");
    utassert(StrContains(message, StrL("title expects non-empty text")));
}

// ─── lifecycle/: mod.rs, tooltip.rs, menu.rs ───────────────────────────────

// Registers a Host's window with its App for as long as it lives, so what is
// posted to it (WindowPost: Rust's window.defer) runs on ExecDrain.
struct LiveWindow {
    Host& host;
    explicit LiveWindow(Host& host) : host(host) {
        VecAppend(host.app.windows, &host.window);
    }
    ~LiveWindow() { VecReset(host.app.windows); }
};

// The element carrying tooltip `text`, if any.
El* FindTooltip(El* element, Str text) {
    if (!element) return nullptr;
    if (StrEq(element->style.tooltip, text)) return element;
    for (El* child = element->first; child; child = child->next) {
        if (El* found = FindTooltip(child, text)) return found;
    }
    return nullptr;
}

// lifecycle_host.rs tooltip_uses_the_native_managed_overlay_on_hover, up to
// the hover: the Button is real and its tooltip text is managed by it rather
// than drawn into the tree. The hover itself needs simulated pointer input
// and an advancing clock.
void TooltipBuildsARealButtonWithAManagedTooltip() {
    FamilyCatalog catalog(&component_shell::RegisterLifecycle);
    utassert(catalog.ok);
    Host host(StrL("import { View } from 'gpui-kit';\n"
                   "import { Tooltip } from 'gpui-component';\n"
                   "export default class Example extends View {\n"
                   "  render() { return new Tooltip('help', 'Help', 'Open "
                   "documentation'); }\n"
                   "}\n"),
              &catalog.frozen);
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(FindText(root, StrL("Help")) != nullptr);
    utassert(FindText(root, StrL("Open documentation")) == nullptr);
    utassert(FindTooltip(root, StrL("Open documentation")) != nullptr);
}

// lifecycle_host.rs menu_bar_installs_native_and_component_menu_models_after_
// render: nothing is installed while rendering; the deferred effect installs
// the native and component model, a repeated render with the same menus does
// not install again, and releasing the root view restores what was there.
void MenuBarInstallsNativeAndComponentMenuModelsAfterRender() {
    FamilyCatalog catalog(&component_shell::RegisterLifecycle);
    utassert(catalog.ok);
    int count = -1;
    {
        Host host(StrL("import { View } from 'gpui-kit';\n"
                       "import { MenuBar, Menu, MenuItem, MenuSeparator } "
                       "from 'gpui-component';\n"
                       "export default class Example extends View {\n"
                       "  render() {\n"
                       "    return new MenuBar('main-menu').child(\n"
                       "      new Menu('File')\n"
                       "        .child(new MenuItem('Open', 'file.open'))\n"
                       "        .child(new MenuSeparator())\n"
                       "        .child(new MenuItem('Quit', "
                       "'app.quit').disabled(true))\n"
                       "    );\n"
                       "  }\n"
                       "}\n"),
                  &catalog.frozen);
        LiveWindow live(host);
        El* root = host.Render();
        utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
        utassert(!FindTextPrefix(root, StrL("Failed to render")));
        utassert(StrContains(DebugTreeTemp(host), StrL("MenuBar")));
        BaseAppMenus(&host.app, &count);
        utassert(count == 0);
        ExecDrain();
        const MenuDef* menus = BaseAppMenus(&host.app, &count);
        utassert(count == 1);
        if (count == 1) {
            utassert(StrEq(menus[0].name, "File"));
            utassert(menus[0].n == 3);
            utassert(menus[0].items[0].action ==
                     shell::ShellActionOf(StrL("file.open")));
            utassert(menus[0].items[1].separator);
            utassert(StrEq(menus[0].items[2].label, "Quit"));
        }
        // The in-window bar reads the installed model.
        ScriptView* script = host.view.Get(&host.app);
        if (script) script->dirty = true;
        root = host.Render();
        utassert(FindText(root, StrL("File")) != nullptr);
        ExecDrain();
        BaseAppMenus(&host.app, &count);
        utassert(count == 1);
        // observe_release: dropping the root view runs the cleanup.
        EntityDrop(&host.app, host.view.id);
        host.view = {};
        BaseAppMenus(&host.app, &count);
        utassert(count == 0);
        AppMenuClear(&host.app);
    }
}

// engine/quickjs/mod.rs returning_to_the_installed_app_effect_cancels_a_
// pending_replacement.
void ReturningToTheInstalledAppEffectCancelsAPendingReplacement() {
    shell::ComponentAppEffectQueue queue;
    shell::InstalledAppEffect installed;
    installed.key = StrDup(StrL("menu"));
    installed.revision = StrDup(StrL("a"));
    VecAppend(queue.installed, installed);
    Str key = StrL("menu");
    utassert(shell::QueueComponentAppEffect(&queue, key, StrL("b")));
    const Str* pending = queue.Pending(key);
    utassert(pending && StrEq(*pending, "b"));
    utassert(!shell::QueueComponentAppEffect(&queue, key, StrL("a")));
    utassert(queue.Pending(key) == nullptr);
    StrFree(queue.Installed(key)->revision);
    queue.Installed(key)->revision = StrDup(StrL("b"));
    utassert(shell::QueueComponentAppEffect(&queue, key, StrL("a")));
}

int gAppEffectInstalls = 0;
int gAppEffectCleanups = 0;

void CountAppEffectCleanup(App*, void*) {
    gAppEffectCleanups++;
}

shell::ComponentAppEffectCleanup CountAppEffectInstall(App*, void*) {
    gAppEffectInstalls++;
    shell::ComponentAppEffectCleanup cleanup;
    cleanup.run = &CountAppEffectCleanup;
    return cleanup;
}

// crates/shell tests/render.rs retiring_an_application_generation_runs_its_
// app_effect_cleanups.
void RetiringAnApplicationGenerationRunsItsAppEffectCleanups() {
    gAppEffectInstalls = 0;
    gAppEffectCleanups = 0;
    Host host(
        StrL("export default class Effectful { render() { return "
             "'effect'; } }\n"));
    LiveWindow live(host);
    utassert(host.Render() != nullptr);
    void* application = host.runtime
                            ->ScriptViewApplication(host.view.id, &host.app);
    utassert(application != nullptr);
    shell::ComponentAppEffectInstall install;
    install.run = &CountAppEffectInstall;
    Arena* a = ArenaNew();
    Str error;
    utassert(host.runtime->ScheduleComponentAppEffect(
        application, host.view.id, StrL("menu-bar"), StrL("revision-1"),
        &host.window, &host.app, install, &error, a));
    ExecDrain();
    utassert(gAppEffectInstalls == 1);
    utassert(gAppEffectCleanups == 0);
    ScriptView* script = host.view.Get(&host.app);
    host.runtime->ReleaseApplicationState(script->object);
    utassert(gAppEffectCleanups == 1);
    ArenaDelete(a);
}

// menu.rs's refusals, which Rust states in its recorders and materializers.
void LifecycleMenuRefusesWhatRustRefuses() {
    Str message = CallErrorTemp("MenuItem", "new MenuItem(' ', 'open')");
    utassert(StrContains(message,
                         StrL("MenuItem expects non-empty label and action")));
    message = CallErrorTemp("Tooltip", "new Tooltip('id', ' ', 'text')");
    utassert(StrContains(
        message, StrL("Tooltip id, label, and text must not be empty")));
    message = CallErrorTemp("Menu", "new Menu('')");
    utassert(StrContains(message, StrL("Menu expects a non-empty label")));
    Host foreign(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { Menu } from 'gpui-component';\n"
             "export default class App extends View { render() { "
             "return new Menu('File').child(div()); } }\n"));
    utassert(StrContains(RenderRefusal(foreign),
                         StrL("Menu accepts only MenuItem or MenuSeparator "
                              "children")));
    Host styled(
        StrL("import { View } from 'gpui-kit';\n"
             "import { MenuItem } from 'gpui-component';\n"
             "export default class App extends View { render() { "
             "return new MenuItem('Open', 'open').p(2); } }\n"));
    utassert(StrContains(RenderRefusal(styled),
                         StrL("MenuItem carries data rather than a box")));
}

// ─── command/: mod.rs, command.rs, native_menu.rs ──────────────────────────

// command_host.rs command_catalog_is_closed and mod.rs
// catalog_is_the_retained_command_family_only.
void CommandCatalogIsClosed() {
    FamilyCatalog catalog(&component_shell::RegisterCommand);
    utassert(catalog.ok);
    const char* expected[] = {
        "CommandItem",    "CommandGroup",        "CommandSeparator", "Command",
        "NativeMenuItem", "NativeMenuSeparator", "NativeMenuTrigger"};
    utassert(catalog.NamesAre(expected, 7));
    utassert(catalog.frozen.StateCount() == 1);
    utassert(catalog.Documented());
}

// The refusal a script rendering `body` from the command family meets.
Str CommandRefusalTemp(const char* body) {
    TempStr source =
        fmt("import { View, div } from 'gpui-kit';\n"
            "import { CommandState, Command, CommandItem, CommandGroup, "
            "CommandSeparator, NativeMenuTrigger, NativeMenuItem, "
            "NativeMenuSeparator } from 'gpui-component';\n"
            "export default class App extends View {\n"
            "  init() { this.state = CommandState(); }\n"
            "  render() { return %s; }\n"
            "}\n",
            Str(body));
    Host host(source);
    return fmt("%s", RenderRefusal(host));
}

// command.rs typed_boundaries_are_closed, native_menu.rs typed_lane_is_closed
// and the item refusal of item_operations_are_last_call_wins_and_combination_
// is_honest, through the scripts that reach them.
void CommandTypedBoundariesAreClosed() {
    utassert(StrContains(
        CommandRefusalTemp("new Command(this.state).child(div())"),
        StrL("Command accepts only registered CommandItem or CommandGroup or "
             "CommandSeparator children; received an ordinary element")));
    utassert(StrContains(
        CommandRefusalTemp(
            "new CommandGroup('g').child(new CommandGroup('h'))"),
        StrL("CommandGroup accepts only registered CommandItem children; "
             "received CommandGroup")));
    utassert(StrContains(
        CommandRefusalTemp("new NativeMenuTrigger('n', 'Actions')"
                           ".on_effect_error(() => {}).child(div())"),
        StrL("NativeMenuTrigger accepts only registered NativeMenuItem or "
             "NativeMenuSeparator children; received an ordinary element")));
    utassert(StrContains(
        CommandRefusalTemp("new NativeMenuTrigger('n', 'Actions')"),
        StrL("NativeMenuTrigger requires on_effect_error(callback)")));
    utassert(StrContains(
        CommandRefusalTemp("new NativeMenuItem('Open', 'open')"
                           ".disabled(true).checked(true)"),
        StrL("NativeMenuItem cannot be both disabled and checked because the "
             "native API has no combined constructor")));
    utassert(StrContains(CommandRefusalTemp("new CommandItem('Alpha').p(2)"),
                         StrL("CommandItem carries data rather than a box")));
    Str message = CallErrorTemp("CommandItem",
                                "new CommandItem('a')"
                                ".keyword(' ')");
    utassert(StrContains(message,
                         StrL("CommandItem.keyword expects non-empty text")));
    message = CallErrorTemp("NativeMenuTrigger",
                            "new NativeMenuTrigger(' ', 'Actions')");
    utassert(StrContains(
        message, StrL("NativeMenuTrigger expects non-empty id and label")));
}

// Re-renders a host whose script asked for one (cx.notify()).
El* Rerender(Host& host) {
    ScriptView* script = host.view.Get(&host.app);
    if (script) script->dirty = true;
    return host.Render();
}

// Renders and lays the frame out, which is when a virtual list builds the
// rows it shows.
El* RenderLaidOut(Host& host) {
    El* root = Rerender(host);
    host.window.paint.app = &host.app;
    host.window.paint.window = &host.window;
    if (root) LayoutEl(&host.window.paint, root, 0, 0, 400, 600, 14, Rgba{});
    return root;
}

// command_host.rs retained_command_typed_entries_query_and_confirm_callbacks_
// are_native. The header, the footer and each custom row are built into the
// frame that shows them; typing into the retained query reaches on_query, and
// confirming the one match reports its path. Rust also counts the lazy
// factory builds per phase through a #[cfg(test)] probe, which has no
// counterpart here (the port builds header and footer eagerly each render),
// and confirms with the keyboard, which needs window key dispatch: a click
// on the row is the same ConfirmMatch. The action the row dispatches needs
// the window's dispatch tree, which a render alone does not build, so the
// action count is not asserted.
void RetainedCommandTypedEntriesQueryAndConfirmCallbacksAreNative() {
    FamilyCatalog catalog(&component_shell::RegisterCommand);
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { CommandState, Command, CommandItem, CommandGroup, "
             "CommandSeparator } from 'gpui-component';\n"
             "export default class App extends View {\n"
             " init(){this.state=CommandState();this.query='';"
             "this.confirm='none';this.actions=0;}\n"
             " render(){return div().on_action('open',(_event,cx)=>"
             "{this.actions++;cx.notify();})\n"
             "  .child(new Command(this.state).placeholder('Find command')"
             ".max_height(240).p(2).header(div().child('Header factory'))"
             ".footer(div().child('Footer factory'))\n"
             "   .on_query((query,cx)=>{this.query=query;cx.notify();})\n"
             "   .on_confirm((section,row,cx)=>{this.confirm=`${section}:"
             "${row}`;cx.notify();})\n"
             "   .child(new CommandItem('Alpha').keyword('first')"
             ".action('open').content(div().child('Alpha custom row')))\n"
             "   .child(new CommandSeparator())\n"
             "   .child(new CommandGroup('Group').child(new CommandItem('Beta')"
             ".checked(true).action('open').content(div().child('Beta custom "
             "row')))))\n"
             "  .child(`State: ${this.query}|${this.confirm}|${this.actions}`)"
             ";}\n"
             "}\n"),
        &catalog.frozen);
    El* root = RenderLaidOut(host);
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(!FindTextPrefix(root, StrL("Failed to render")));
    utassert(FindText(root, StrL("Header factory")) != nullptr);
    utassert(FindText(root, StrL("Footer factory")) != nullptr);
    utassert(FindText(root, StrL("Alpha custom row")) != nullptr);
    utassert(FindText(root, StrL("Beta custom row")) != nullptr);
    utassert(FindText(root, StrL("State: |none|0")) != nullptr);

    InputState* inputs[4] = {};
    int count = 0;
    CollectInputs(root, inputs, &count, 4);
    utassert(count == 1);
    if (count != 1) return;
    TypeInto(host, inputs[0], "b");
    RenderLaidOut(host);
    root = RenderLaidOut(host);
    utassert(FindText(root, StrL("State: b|none|0")) != nullptr);
    utassert(FindText(root, StrL("Alpha custom row")) == nullptr);

    El* row = ListenerAbove(root, StrL("Beta custom row"));
    utassert(row != nullptr);
    if (!row) return;
    Click(host, row);
    root = RenderLaidOut(host);
    utassert(FindTextPrefix(root, StrL("State: b|1:0|")) != nullptr);

    // A refresh rebuilds the callbacks; the retained query and the state
    // survive it.
    root = RenderLaidOut(host);
    utassert(FindTextPrefix(root, StrL("State: b|1:0|")) != nullptr);
    utassert(FindText(root, StrL("Beta custom row")) != nullptr);
}

// What the NativeMenuTrigger's show effect built, as the probe saw it.
int gNativeMenuShown = 0;
bool gNativeMenuRefuse = false;
int gNativeMenuRows = 0;
component::NativeMenuItem gNativeMenuRow[8];
Listener gNativeMenuSelect = {};

bool RecordNativeMenu(const component::NativeMenu* menu, Str* error, Arena* a) {
    gNativeMenuShown++;
    gNativeMenuRows = 0;
    for (const component::NativeMenuItem& item : menu->items) {
        if (gNativeMenuRows < 8) gNativeMenuRow[gNativeMenuRows++] = item;
    }
    gNativeMenuSelect = menu->onSelect;
    if (gNativeMenuRefuse) {
        *error = StrDup(a, StrL("the menu could not be shown"));
        return false;
    }
    return true;
}

int TakeNativeMenuShown() {
    int shown = gNativeMenuShown;
    gNativeMenuShown = 0;
    return shown;
}

const char* kNativeMenuSource =
    "import { View, div } from 'gpui-kit';\n"
    "import { NativeMenuTrigger, NativeMenuItem, NativeMenuSeparator } from "
    "'gpui-component';\n"
    "export default class App extends View { init(_props,cx){this.hits=0;"
    "this.errors=0;this.focus=cx.focus_handle();this.focus.focus();} "
    "render(){return div().size_full().track_focus(this.focus)"
    ".on_action('open',(_event,cx)=>{this.hits++;cx.notify();})\n"
    " .child(new NativeMenuTrigger('native','Actions').absolute().left(0)"
    ".top(0).w(140).h(40).on_effect_error((_message,cx)=>{this.errors+=10;"
    "cx.notify();}).on_effect_error((_message,cx)=>{this.errors++;"
    "cx.notify();})\n"
    "  .child(new NativeMenuItem('Open','open')).child(new "
    "NativeMenuSeparator()).child(new NativeMenuItem('Disabled','disabled')"
    ".disabled(true)))\n"
    " .child(`Menu: ${this.hits}|${this.errors}`);}}\n";

// command_host.rs native_menu_trigger_runs_one_keyed_show_effect_per_click:
// one click, one keyed show effect, and a generation that survives a
// refresh. The rows carry their ShellAction and the disabled one is greyed.
// A failing effect reports through the last on_effect_error only (native_
// menu.rs last_reporter). native_menu_selection_dispatches_a_shell_action
// needs simulated keystrokes into the drawn menu and window action dispatch;
// Rust itself runs it only where the menu is drawn in the window.
void NativeMenuTriggerRunsOneKeyedShowEffectPerClick() {
    FamilyCatalog catalog(&component_shell::RegisterCommand);
    component_shell::SetNativeMenuShowProbe(&RecordNativeMenu);
    gNativeMenuRefuse = false;
    TakeNativeMenuShown();
    {
        Host host(Str(kNativeMenuSource), &catalog.frozen);
        El* root = host.Render();
        utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
        utassert(!FindTextPrefix(root, StrL("Failed to render")));
        // Rust's debug_tree prints the op as `:disabled[Bool(true)]`; the
        // port's DebugTree spells a behavior op `:disabled(true)`.
        utassert(StrContains(DebugTreeTemp(host),
                             StrL("NativeMenuItem :disabled(true)")));
        El* trigger = ListenerAbove(root, StrL("Actions"));
        utassert(trigger != nullptr);
        if (!trigger) return;
        Click(host, trigger);
        utassert(TakeNativeMenuShown() == 1);
        utassert(gNativeMenuRows == 3);
        if (gNativeMenuRows == 3) {
            utassert(StrEq(gNativeMenuRow[0].label, "Open"));
            utassert(gNativeMenuRow[0]
                         .id == (intptr_t)shell::ShellActionOf(StrL("open")));
            utassert(!gNativeMenuRow[0].disabled);
            utassert(gNativeMenuRow[1]
                         .kind == component::NativeMenuItemKind::Separator);
            utassert(gNativeMenuRow[2].disabled);
        }
        utassert(gNativeMenuSelect.IsValid());
        root = Rerender(host);
        utassert(FindText(root, StrL("Menu: 0|0")) != nullptr);

        // The effect fails: the last reporter is told, once.
        gNativeMenuRefuse = true;
        trigger = ListenerAbove(root, StrL("Actions"));
        if (trigger) Click(host, trigger);
        gNativeMenuRefuse = false;
        utassert(TakeNativeMenuShown() == 1);
        root = Rerender(host);
        utassert(FindText(root, StrL("Menu: 0|1")) != nullptr);

        // A refresh rebuilds the callback and the effect generation.
        root = Rerender(host);
        trigger = ListenerAbove(root, StrL("Actions"));
        utassert(trigger != nullptr);
        if (trigger) Click(host, trigger);
        utassert(TakeNativeMenuShown() == 1);
    }
    component_shell::SetNativeMenuShowProbe(nullptr);
}

// native_menu.rs item_operations_are_last_call_wins_and_combination_is_
// honest: checked and action are last-call-wins.
void NativeMenuItemOperationsAreLastCallWins() {
    FamilyCatalog catalog(&component_shell::RegisterCommand);
    component_shell::SetNativeMenuShowProbe(&RecordNativeMenu);
    TakeNativeMenuShown();
    {
        Host host(StrL("import { View } from 'gpui-kit';\n"
                       "import { NativeMenuTrigger, NativeMenuItem } from "
                       "'gpui-component';\n"
                       "export default class App extends View { render() { "
                       "return new NativeMenuTrigger('n', 'Actions')"
                       ".on_effect_error(() => {}).child(new "
                       "NativeMenuItem('Open', 'old').action('first')"
                       ".checked(false).action('last').checked(true)); } }\n"),
                  &catalog.frozen);
        El* root = host.Render();
        El* trigger = ListenerAbove(root, StrL("Actions"));
        utassert(trigger != nullptr);
        if (trigger) Click(host, trigger);
        utassert(TakeNativeMenuShown() == 1);
        utassert(gNativeMenuRows == 1);
        if (gNativeMenuRows == 1) {
            utassert(gNativeMenuRow[0].checked);
            utassert(gNativeMenuRow[0]
                         .id == (intptr_t)shell::ShellActionOf(StrL("last")));
        }
    }
    component_shell::SetNativeMenuShowProbe(nullptr);
}

// ─── overlays/: mod.rs, hover_card.rs, popover.rs, dropdown_menu.rs ────────

// hover_card.rs descriptor_uses_closed_anchor_and_callback_schemas,
// popover.rs callback_schema_includes_the_script_context and dropdown_menu.rs
// item_callback_schema_includes_the_script_context, over the family's own
// catalog in mod.rs order.
void OverlayDescriptorsUseClosedSchemas() {
    FamilyCatalog catalog(&component_shell::RegisterOverlays);
    utassert(catalog.ok);
    const char* expected[] = {"HoverCard", "Popover", "DropdownMenu"};
    utassert(catalog.NamesAre(expected, 3));
    utassert(catalog.Documented());
    if (catalog.frozen.DescriptorCount() != 3) return;
    const ComponentDescriptor* card = catalog.frozen.Descriptor(0);
    utassert(card->methods[1].arguments[0].schema.kind ==
             shell::SchemaKind::Enum);
    utassert(card->methods[5].arguments[0].schema.kind ==
             shell::SchemaKind::Callback);
    utassert(StrEq(Str(card->methods[5].arguments[0].schema.text),
                   "(open: boolean, cx: Context) => void"));
    const ComponentDescriptor* popover = catalog.frozen.Descriptor(1);
    utassert(StrEq(Str(popover->methods[5].arguments[0].schema.text),
                   "(open: boolean, cx: Context) => void"));
    const ComponentDescriptor* dropdown = catalog.frozen.Descriptor(2);
    utassert(StrEq(Str(dropdown->methods[0].arguments[1].schema.text),
                   "(cx: Context) => void"));
}

// hover_card.rs delay_schema_rejects_values_that_duration_cannot_honestly_
// represent, whitespace_only_identity_is_rejected, incompatible_payload_
// reports_the_materializer_contract and materializer_requires_the_trigger_
// element_operation; overlay_host.rs hover_card_rejects_whitespace_identity;
// dropdown_menu.rs item_only_contract_rejects_every_child_lane; and the
// content requirement both lazy overlays state.
void OverlaysRefuseWhatRustRefuses() {
    const char* delays[] = {"-1", "60001"};
    for (const char* delay : delays) {
        TempStr call = fmt("new HoverCard('h').open_delay(%s)", Str(delay));
        utassert(StrContains(
            CallErrorTemp("HoverCard", call.s),
            StrL("HoverCard.open_delay(milliseconds) expects a finite value "
                 "from 0 through 60000")));
    }
    utassert(RendersCleanly(
        "HoverCard",
        "new HoverCard('h').trigger_element(div().child('Profile'))"
        ".content(div()).open_delay(250)",
        "Profile"));
    utassert(StrContains(CallErrorTemp("HoverCard", "new HoverCard('  \\t ')"),
                         StrL("HoverCard id must not be empty")));
    Str failure = MaterializeFailure("HoverCard", shell::ComponentPayload{}, 0);
    utassert(StrEq(failure, "HoverCard received an incompatible payload"));
    StrFree(failure);

    struct Case {
        const char* imports;
        const char* body;
        const char* message;
    };
    const Case cases[] = {
        {"HoverCard", "new HoverCard('h').content(div())",
         "HoverCard requires trigger_element(element)"},
        {"HoverCard", "new HoverCard('h').trigger_element(div())",
         "HoverCard requires content(element)"},
        {"Popover", "new Popover('p', 'Open')",
         "Popover requires content(element)"},
        {"DropdownMenu", "new DropdownMenu('d', 'Actions').child(div())",
         "DropdownMenu accepts item(label, callback) methods only; ordinary "
         "and typed children are unsupported"},
    };
    for (const Case& c : cases) {
        TempStr source =
            fmt("import { View, div } from 'gpui-kit';\n"
                "import { %s } from 'gpui-component';\n"
                "export default class App extends View { "
                "render() { return %s; } }\n",
                Str(c.imports), Str(c.body));
        Host host(source);
        utassert(StrContains(RenderRefusal(host), Str(c.message)));
    }
    utassert(StrContains(CallErrorTemp("Popover", "new Popover('p', ' ')"),
                         StrL("Popover id and label must not be empty")));
    utassert(StrContains(
        CallErrorTemp("DropdownMenu",
                      "new DropdownMenu('d', 'A').item(' ', () => {})"),
        StrL("DropdownMenu.item label must not be empty")));
}

// overlay_host.rs hover_card_materializes_real_trigger_content_style_and_
// closed_methods. The port's DebugTree spells a style `.p(2)` where Rust's
// prints `.p[Number(2.0)]`.
void HoverCardMaterializesRealTriggerContentStyleAndClosedMethods() {
    Host host(
        StrL("import { div, View } from 'gpui-kit';\n"
             "import { HoverCard } from 'gpui-component';\n"
             "export default class OverlayHost extends View {\n"
             "  render() {\n"
             "    return new HoverCard('profile')\n"
             "      .trigger_element(div().child('Profile'))\n"
             "      .content(div().child('Ada Lovelace'))\n"
             "      .p(2)\n"
             "      .card_anchor('bottom_center')\n"
             "      .open_delay(125)\n"
             "      .close_delay(250)\n"
             "      .appearance(true)\n"
             "      .on_open_change(open => { this.last_open = open; "
             "});\n"
             "  }\n"
             "}\n"));
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(!FindTextPrefix(root, StrL("Failed to render")));
    utassert(FindText(root, StrL("Profile")) != nullptr);
    Str tree = DebugTreeTemp(host);
    const char* contracts[] = {"HoverCard",
                               ":trigger_element(registered)",
                               "Ada Lovelace",
                               ".p(2)",
                               ":card_anchor(registered)",
                               ":open_delay(registered)",
                               ":close_delay(registered)",
                               ":appearance(registered)",
                               ":on_open_change(registered)"};
    for (const char* contract : contracts) {
        bool found = StrContains(tree, Str(contract));
        if (!found) printf("missing %s in %s\n", contract, tree.s);
        utassert(found);
    }
}

// The nearest element on the path to `text` whose `field` listener is set.
El* HandlerAbove(El* element, Str text, Listener El::* field,
                 El* best = nullptr) {
    if (!element) return nullptr;
    if ((element->*field).IsValid()) best = element;
    if (element->kind == ElKind::Text && StrEq(element->text, text))
        return best;
    for (El* child = element->first; child; child = child->next) {
        if (El* found = HandlerAbove(child, text, field, best)) return found;
    }
    return nullptr;
}

void MouseDown(Host& host, El* element) {
    MouseDownEvent event = {};
    ListenerCall(&host.app, &host.window, element->onMouseDown, &event);
}

// lazy_overlay_host.rs popover_content_is_lazy_and_open_changes_cross_the_
// registered_boundary: the content is built only while the popover is open,
// the trigger's press reaches on_open_change, and the lazy content's own
// click handler stays live. Closing goes through the trigger again rather
// than a press outside, which needs window hit-testing.
void PopoverContentIsLazyAndOpenChangesCrossTheRegisteredBoundary() {
    Host host(
        StrL("import { div, View } from 'gpui-kit';\n"
             "import { Popover } from 'gpui-component';\n"
             "export default class LazyPopover extends View {\n"
             "  init() { this.open = false; this.hits = 0; }\n"
             "  render() {\n"
             "    return div().pt(100).child(\n"
             "      new Popover('actions', 'Open actions')\n"
             "        .open(this.open)\n"
             "        .on_open_change((open, cx) => { this.open = open; "
             "cx.notify(); })\n"
             "        .content(div().w(200).h(120)\n"
             "          .on_click((_event, cx) => { this.hits += 1; "
             "cx.notify(); })\n"
             "          .child(`Lazy content ${this.hits}`))\n"
             "    ).child(`Open:${this.open}`);\n"
             "  }\n"
             "}\n"));
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(StrContains(DebugTreeTemp(host), StrL("Popover")));
    utassert(FindTextPrefix(root, StrL("Lazy content")) == nullptr);
    utassert(FindText(root, StrL("Open:false")) != nullptr);

    for (int round = 1; round <= 2; round++) {
        El* trigger =
            HandlerAbove(root, StrL("Open actions"), &El::onMouseDown);
        utassert(trigger != nullptr);
        if (!trigger) return;
        MouseDown(host, trigger);
        root = Rerender(host);
        utassert(FindText(root, StrL("Open:true")) != nullptr);
        TempStr before = fmt("Lazy content %d", round - 1);
        El* content = ListenerAbove(root, Str(before));
        utassert(content != nullptr);
        if (!content) return;
        Click(host, content);
        root = Rerender(host);
        TempStr after = fmt("Lazy content %d", round);
        utassert(FindText(root, Str(after)) != nullptr);

        // Closed again through the trigger: nothing of the content is
        // built.
        trigger = HandlerAbove(root, StrL("Open actions"), &El::onMouseDown);
        if (!trigger) return;
        MouseDown(host, trigger);
        root = Rerender(host);
        utassert(FindText(root, StrL("Open:false")) != nullptr);
        utassert(FindTextPrefix(root, StrL("Lazy content")) == nullptr);
    }
}

// lazy_overlay_host.rs hover_card_builds_lazy_content_only_after_hover_and_
// reports_lifecycle. The hover arms the open timer; the timer firing is
// driven directly, since a render has no clock.
void HoverCardBuildsLazyContentOnlyAfterHoverAndReportsLifecycle() {
    Host host(
        StrL("import { div, View } from 'gpui-kit';\n"
             "import { HoverCard } from 'gpui-component';\n"
             "export default class LazyHoverCard extends View {\n"
             "  init() { this.open = false; this.hits = 0; }\n"
             "  render() {\n"
             "    return div().v_flex().w(400).h(400)\n"
             "      .child(div().w(400).h(100))\n"
             "      .child(new HoverCard('profile')\n"
             "        .trigger_element(div().w(300).h(40).child('Profile'))\n"
             "        .content(div().w(200).h(120)\n"
             "          .on_click((_event, cx) => { this.hits += 1; "
             "cx.notify(); })\n"
             "          .child(`Lazy profile ${this.hits}`))\n"
             "        .card_anchor('top_left')\n"
             "        .open_delay(0).close_delay(0)\n"
             "        .on_open_change(open => { this.open = open; }))\n"
             "      .child(`Hover:${this.open}`);\n"
             "  }\n"
             "}\n"));
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(FindTextPrefix(root, StrL("Lazy profile")) == nullptr);

    El* trigger = HandlerAbove(root, StrL("Profile"), &El::onHover);
    utassert(trigger != nullptr);
    if (!trigger) return;
    HoverEvent hover = {};
    hover.hovered = true;
    ListenerCall(&host.app, &host.window, trigger->onHover, &hover);
    Entity<HoverCardState> state;
    state.id = trigger->onHover.view;
    HoverCardState* card = state.Get(&host.app);
    utassert(card && card->timer != 0);
    TickEvent tick = {};
    ListenerCall(&host.app, &host.window,
                 ListenTo(state, &HoverCardState::OnOpen), &tick);
    root = Rerender(host);
    El* content = ListenerAbove(root, StrL("Lazy profile 0"));
    utassert(content != nullptr);
    if (!content) return;
    Click(host, content);
    root = Rerender(host);
    utassert(FindText(root, StrL("Lazy profile 1")) != nullptr);
    utassert(FindText(root, StrL("Hover:true")) != nullptr);
    if (card && card->timer) WindowCancelTimer(&host.window, card->timer);
}

// lazy_overlay_host.rs dropdown_menu_opens_real_items_and_dispatches_the_
// selected_callback.
void DropdownMenuOpensRealItemsAndDispatchesTheSelectedCallback() {
    Host host(
        StrL("import { div, View } from 'gpui-kit';\n"
             "import { DropdownMenu } from 'gpui-component';\n"
             "export default class LazyMenu extends View {\n"
             "  init() { this.choice = 'none'; }\n"
             "  render() {\n"
             "    return div().pt(100)\n"
             "      .child(new DropdownMenu('actions', 'Actions').w(160)\n"
             "        .item('Rename', cx => { this.choice = 'rename'; "
             "cx.notify(); })\n"
             "        .item('Archive', cx => { this.choice = 'archive'; "
             "cx.notify(); }))\n"
             "      .child(`Choice:${this.choice}`);\n"
             "  }\n"
             "}\n"));
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(FindText(root, StrL("Rename")) == nullptr);
    El* trigger = ListenerAbove(root, StrL("Actions"));
    utassert(trigger != nullptr);
    if (!trigger) return;
    Click(host, trigger);
    root = Rerender(host);
    El* rename = ListenerAbove(root, StrL("Rename"));
    utassert(rename != nullptr);
    utassert(FindText(root, StrL("Archive")) != nullptr);
    if (!rename) return;
    Click(host, rename);
    root = Rerender(host);
    utassert(FindText(root, StrL("Choice:rename")) != nullptr);
}

// ─── delegate_collections/: mod.rs, list.rs ────────────────────────────────

// delegate_collections_host.rs delegate_collection_catalog_exposes_retained_
// list_contract.
void DelegateCollectionCatalogExposesRetainedListContract() {
    FamilyCatalog catalog(&component_shell::RegisterDelegateCollections);
    utassert(catalog.ok);
    const char* names[] = {"List"};
    utassert(catalog.NamesAre(names, 1));
    utassert(catalog.frozen.StateCount() == 0);
    utassert(catalog.Documented());
}

// list.rs test_probe: the ids of the rows the renderer built.
int gListRowsSeen = 0;
int gListRowsOther = 0;
const char* gListRowExpect = "";

void RecordListRow(Str id) {
    gListRowsSeen++;
    if (!StrEq(id, gListRowExpect)) gListRowsOther++;
}

void ExpectListRows(const char* id) {
    gListRowsSeen = 0;
    gListRowsOther = 0;
    gListRowExpect = id;
}

// delegate_collections_host.rs list_uses_a_fresh_immutable_snapshot_and_
// lazy_row_renderer: every render takes a fresh rows snapshot, and the rows
// the list lays out are built from it by the lazy renderer.
void ListUsesAFreshImmutableSnapshotAndLazyRowRenderer() {
    FamilyCatalog catalog(&component_shell::RegisterDelegateCollections);
    component_shell::SetListRowProbe(&RecordListRow);
    {
        Host host(StrL("import { View, div } from 'gpui-kit';\n"
                       "import { List } from 'gpui-component';\n"
                       "export default class App extends View {\n"
                       "  init() { this.updated = false; }\n"
                       "  render() {\n"
                       "    const rows = this.updated ? [{id: 'beta', label: "
                       "'Beta'}] : [{id: 'alpha', label: 'Alpha'}];\n"
                       "    this.updated = true;\n"
                       "    return new List('people', () => rows, row => "
                       "div().child(row.label));\n"
                       "  }\n"
                       "}\n"),
                  &catalog.frozen);
        ExpectListRows("alpha");
        El* root = RenderLaidOut(host);
        utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
        utassert(gListRowsSeen > 0 && gListRowsOther == 0);
        utassert(FindText(root, StrL("Alpha")) != nullptr);

        ExpectListRows("beta");
        root = RenderLaidOut(host);
        utassert(root && len(host.ViewError()) == 0);
        utassert(gListRowsSeen > 0 && gListRowsOther == 0);
        utassert(FindText(root, StrL("Beta")) != nullptr);
        utassert(FindText(root, StrL("Alpha")) == nullptr);
        utassert(len(host.runtime->LastComponentFailure()) == 0);
    }
    component_shell::SetListRowProbe(nullptr);
}

// list.rs: the rows callback must answer an array, and a List takes no
// children.
void ListRefusesWhatRustRefuses() {
    FamilyCatalog catalog(&component_shell::RegisterDelegateCollections);
    {
        Host host(StrL("import { View, div } from 'gpui-kit';\n"
                       "import { List } from 'gpui-component';\n"
                       "export default class App extends View { render() { "
                       "return new List('people', () => 3, row => div()); } "
                       "}\n"),
                  &catalog.frozen);
        El* root = host.Render();
        utassert(root != nullptr);
        utassert(FindTextPrefix(
                     root, StrL("Failed to snapshot List rows: component "
                                "delegate snapshot callback must return an "
                                "array of rows")) != nullptr);
    }
    {
        Host host(StrL("import { View, div } from 'gpui-kit';\n"
                       "import { List } from 'gpui-component';\n"
                       "export default class App extends View { render() { "
                       "return new List('people', () => [], row => div())"
                       ".child(div()); } }\n"),
                  &catalog.frozen);
        host.Render();
        utassert(StrContains(host.runtime->LastComponentFailure(),
                             StrL("List does not accept children")));
    }
    Str failure = CallErrorTemp("List", "new List(' ', () => [], r => null)");
    utassert(StrContains(failure, StrL("List expects a non-empty id, rows "
                                       "callback, and row renderer")));
}

// ─── delegate_combobox/mod.rs ──────────────────────────────────────────────

// delegate_combobox_host.rs combobox_catalog_exposes_native_single_select_
// contract.
void ComboboxCatalogExposesNativeSingleSelectContract() {
    FamilyCatalog catalog(&component_shell::RegisterDelegateCombobox);
    utassert(catalog.ok);
    const char* names[] = {"Combobox"};
    utassert(catalog.NamesAre(names, 1));
    utassert(catalog.Documented());
}

// mod.rs test_probe: the values of each Change and Confirm, in order.
char gComboboxEvents[8][32];
int gComboboxEventCount = 0;

void RecordComboboxEvent(bool confirm, const Str* values, int count) {
    if (gComboboxEventCount >= 8) return;
    char* out = gComboboxEvents[gComboboxEventCount++];
    snprintf(out, 32, "%s:%.*s", confirm ? "confirm" : "change",
             count > 0 ? (int)len(values[0]) : 0, count > 0 ? values[0].s : "");
}

// delegate_combobox_host.rs combobox_native_click_emits_change_and_confirm_
// for_stable_value: a click on the trigger opens the list, a click on the
// first row selects it, and the host hears one Change and one Confirm, both
// carrying the row's stable id. Rust clicks at window coordinates; here the
// clicks go to the trigger's and the row's own listeners.
void ComboboxNativeClickEmitsChangeAndConfirmForStableValue() {
    FamilyCatalog catalog(&component_shell::RegisterDelegateCombobox);
    component_shell::SetComboboxEventProbe(&RecordComboboxEvent);
    gComboboxEventCount = 0;
    {
        Host host(
            StrL("import { View, div } from 'gpui-kit';\n"
                 "import { Combobox } from 'gpui-component';\n"
                 "export default class App extends View { render() {\n"
                 "  return div().size_full().child(new Combobox('people', () "
                 "=> [\n"
                 "    {id:'alpha',label:'Alpha'}, {id:'beta',label:'Beta'}\n"
                 "  ], value => { this.change = value; }, (value, cx) => { "
                 "this.confirm = value; cx.notify(); })\n"
                 "    .searchable(false).placeholder('Choose'))\n"
                 "    .child(`Confirmed:${this.confirm}`);\n"
                 "} }\n"),
            &catalog.frozen);
        El* root = host.Render();
        utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
        El* trigger = ListenerAbove(root, StrL("Choose"));
        utassert(trigger != nullptr);
        if (!trigger) return;
        Click(host, trigger);
        root = Rerender(host);
        El* row = ListenerAbove(root, StrL("Alpha"));
        utassert(row != nullptr);
        if (!row) return;
        Click(host, row);
        utassert(gComboboxEventCount == 2);
        utassert(strcmp(gComboboxEvents[0], "change:alpha") == 0);
        utassert(strcmp(gComboboxEvents[1], "confirm:alpha") == 0);
        root = Rerender(host);
        utassert(FindText(root, StrL("Confirmed:alpha")) != nullptr);
        utassert(len(host.ViewError()) == 0);
        utassert(len(host.runtime->LastComponentFailure()) == 0);
    }
    component_shell::SetComboboxEventProbe(nullptr);
}

// mod.rs: rows need a string id and label, and a Combobox takes no children.
void ComboboxRefusesWhatRustRefuses() {
    FamilyCatalog catalog(&component_shell::RegisterDelegateCombobox);
    {
        Host host(StrL("import { View } from 'gpui-kit';\n"
                       "import { Combobox } from 'gpui-component';\n"
                       "export default class App extends View { render() { "
                       "return new Combobox('c', () => [{id:'a'}], v => {}, v "
                       "=> {}); } }\n"),
                  &catalog.frozen);
        El* root = host.Render();
        utassert(FindText(root, StrL("Invalid Combobox rows: Combobox row 0 "
                                     "requires a string `label`")) != nullptr);
    }
    {
        Host host(StrL("import { View, div } from 'gpui-kit';\n"
                       "import { Combobox } from 'gpui-component';\n"
                       "export default class App extends View { render() { "
                       "return new Combobox('c', () => [], v => {}, v => {})"
                       ".child(div()); } }\n"),
                  &catalog.frozen);
        host.Render();
        utassert(StrContains(host.runtime->LastComponentFailure(),
                             StrL("Combobox does not accept children")));
    }
    utassert(StrContains(
        CallErrorTemp("Combobox",
                      "new Combobox('c', () => [], v => {}, v => {})"
                      ".menu_width(0)"),
        StrL("Combobox.menu_width received an invalid value")));
    utassert(StrContains(
        CallErrorTemp("Combobox",
                      "new Combobox('', () => [], v => {}, v => {})"),
        StrL("Combobox expects id, rows, on_change, and on_confirm "
             "callbacks")));
}

// ─── delegate_select/mod.rs ────────────────────────────────────────────────

// delegate_select_host.rs select_catalog_exposes_native_retained_contract.
void SelectCatalogExposesNativeRetainedContract() {
    FamilyCatalog catalog(&component_shell::RegisterDelegateSelect);
    utassert(catalog.ok);
    const char* names[] = {"Select"};
    utassert(catalog.NamesAre(names, 1));
    utassert(catalog.Documented());
}

// mod.rs test_probe: the selected values, in order.
char gSelected[8][32];
int gSelectedCount = 0;

void RecordSelected(Str value) {
    if (gSelectedCount >= 8) return;
    snprintf(gSelected[gSelectedCount++], 32, "%.*s", (int)len(value), value.s);
}

// delegate_select_host.rs select_native_click_emits_selected_stable_value: a
// click on the trigger opens the list, whose rows the script renderer
// draws, and a click on the first row reports its stable id. Rust clicks at
// window coordinates; here the clicks go to the trigger's and the row's own
// listeners.
void SelectNativeClickEmitsSelectedStableValue() {
    FamilyCatalog catalog(&component_shell::RegisterDelegateSelect);
    component_shell::SetSelectProbe(&RecordSelected);
    gSelectedCount = 0;
    {
        Host host(StrL("import { View, div } from 'gpui-kit';\n"
                       "import { Select } from 'gpui-component';\n"
                       "export default class App extends View {\n"
                       "  render() {\n"
                       "    return div().size_full().child(new "
                       "Select('people', () => [\n"
                       "      {id: 'alpha', label: 'Alpha'}, {id: 'beta', "
                       "label: 'Beta'}\n"
                       "    ], row => div().child(`Row ${row.label}`), (value, "
                       "cx) => { this.selected = value; cx.notify(); })"
                       ".placeholder('Choose'))\n"
                       "      .child(`Selected:${this.selected}`);\n"
                       "  }\n"
                       "}\n"),
                  &catalog.frozen);
        El* root = host.Render();
        utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
        utassert(FindText(root, StrL("Row Alpha")) == nullptr);
        El* trigger = ListenerAbove(root, StrL("Choose"));
        utassert(trigger != nullptr);
        if (!trigger) return;
        Click(host, trigger);
        root = Rerender(host);
        // The rows are the script renderer's, inside the native rows.
        El* row = ListenerAbove(root, StrL("Row Alpha"));
        utassert(row != nullptr);
        utassert(FindText(root, StrL("Row Beta")) != nullptr);
        if (!row) return;
        Click(host, row);
        utassert(gSelectedCount == 1);
        utassert(strcmp(gSelected[0], "alpha") == 0);
        root = Rerender(host);
        utassert(FindText(root, StrL("Selected:alpha")) != nullptr);
        // The trigger names the retained selection across the re-render.
        utassert(FindText(root, StrL("Alpha")) != nullptr);
        utassert(len(host.ViewError()) == 0);
        utassert(len(host.runtime->LastComponentFailure()) == 0);
    }
    component_shell::SetSelectProbe(nullptr);
}

// mod.rs: rows need a string id and label, and a Select takes no children.
void SelectRefusesWhatRustRefuses() {
    FamilyCatalog catalog(&component_shell::RegisterDelegateSelect);
    {
        Host host(StrL("import { View, div } from 'gpui-kit';\n"
                       "import { Select } from 'gpui-component';\n"
                       "export default class App extends View { render() { "
                       "return new Select('s', () => [{label:'A'}], r => "
                       "div(), v => {}); } }\n"),
                  &catalog.frozen);
        El* root = host.Render();
        utassert(FindText(root, StrL("Invalid Select rows: Select row 0 "
                                     "requires a string `id`")) != nullptr);
    }
    {
        Host host(StrL("import { View, div } from 'gpui-kit';\n"
                       "import { Select } from 'gpui-component';\n"
                       "export default class App extends View { render() { "
                       "return new Select('s', () => ({}), r => div(), v => "
                       "{}); } }\n"),
                  &catalog.frozen);
        El* root = host.Render();
        utassert(FindTextPrefix(root, StrL("Failed to snapshot Select rows: "
                                           "component delegate snapshot")) !=
                 nullptr);
    }
    {
        Host host(StrL("import { View, div } from 'gpui-kit';\n"
                       "import { Select } from 'gpui-component';\n"
                       "export default class App extends View { render() { "
                       "return new Select('s', () => [], r => div(), v => {})"
                       ".child(div()); } }\n"),
                  &catalog.frozen);
        host.Render();
        utassert(StrContains(host.runtime->LastComponentFailure(),
                             StrL("Select does not accept children")));
    }
    utassert(StrContains(
        CallErrorTemp("Select",
                      "new Select('s', () => [], r => null, v => {})"
                      ".menu_width(-1)"),
        StrL("Select.menu_width received an invalid value")));
    utassert(StrContains(
        CallErrorTemp("Select",
                      "new Select(' ', () => [], r => null, v => {})"),
        StrL("Select expects id, rows callback, row renderer, and selection "
             "callback")));
}

// ─── data_table/mod.rs, and data_table_host.rs ─────────────────────────────

// mod.rs catalog_is_retained_data_table_only.
void DataTableCatalogIsRetainedDataTableOnly() {
    FamilyCatalog catalog(&component_shell::RegisterDataTable);
    utassert(catalog.ok);
    const char* names[] = {"DataTable"};
    utassert(catalog.NamesAre(names, 1));
    utassert(catalog.frozen.StateCount() == 1);
    utassert(catalog.Documented());
}

// mod.rs test_probe: cells built, and failures rendered in their place.
int gDataTableCellBuilds = 0;
int gDataTableErrors = 0;

void RecordDataTableProbe(bool built) {
    if (built)
        gDataTableCellBuilds++;
    else
        gDataTableErrors++;
}

void ResetDataTableProbe() {
    gDataTableCellBuilds = 0;
    gDataTableErrors = 0;
}

// data_table_host.rs retained_data_table_renders_lazy_cells_from_plain_rows.
void RetainedDataTableRendersLazyCellsFromPlainRows() {
    FamilyCatalog catalog(&component_shell::RegisterDataTable);
    component_shell::SetDataTableProbe(&RecordDataTableProbe);
    ResetDataTableProbe();
    {
        Host host(StrL("import { View, div } from 'gpui-kit';\n"
                       "import { DataTableState, DataTable } from "
                       "'gpui-component';\n"
                       "export default class App extends View { render() { "
                       "return new DataTable(\n"
                       "  DataTableState(['name', 'status']),\n"
                       "  () => [{name: 'Ada', status: 'Ready'}, {name: "
                       "'Lin', status: 'Busy'}],\n"
                       "  (row, column) => div().child(row[column])\n"
                       ").stripe(true).bordered(false).row_selectable(true)"
                       ".cell_selectable(true); } }\n"),
                  &catalog.frozen);
        El* root = RenderLaidOut(host);
        utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
        utassert(len(host.runtime->LastComponentFailure()) == 0);
        utassert(gDataTableCellBuilds >= 4);
        utassert(gDataTableErrors == 0);
        utassert(FindText(root, StrL("Ada")) != nullptr);
        utassert(FindText(root, StrL("Busy")) != nullptr);
    }
    component_shell::SetDataTableProbe(nullptr);
}

// data_table_host.rs data_table_rejects_non_array_snapshot_without_panicking.
void DataTableRejectsNonArraySnapshotWithoutPanicking() {
    FamilyCatalog catalog(&component_shell::RegisterDataTable);
    component_shell::SetDataTableProbe(&RecordDataTableProbe);
    ResetDataTableProbe();
    {
        Host host(StrL("import { View, div } from 'gpui-kit'; import { "
                       "DataTableState, DataTable } from 'gpui-component';\n"
                       "export default class App extends View { render() { "
                       "return new DataTable(DataTableState(['name']), () => "
                       "({name:'Ada'}), () => div()); } }\n"),
                  &catalog.frozen);
        El* root = RenderLaidOut(host);
        utassert(root && len(host.ViewError()) == 0);
        utassert(gDataTableCellBuilds == 0);
        utassert(gDataTableErrors == 1);
        utassert(FindTextPrefix(root, StrL("DataTable rows callback must "
                                           "return an array of rows: ")) !=
                 nullptr);
    }
    component_shell::SetDataTableProbe(nullptr);
}

// mod.rs: DataTableState's column refusals, and a DataTable takes no
// children.
void DataTableRefusesWhatRustRefuses() {
    struct Case {
        const char* call;
        const char* message;
    };
    const Case cases[] = {
        {"new DataTable(DataTableState([]), () => [], () => div())",
         "DataTableState requires at least one column"},
        {"new DataTable(DataTableState([' ']), () => [], () => div())",
         "DataTableState columns must be non-empty strings"},
        {"new DataTable(DataTableState(['a', 'a']), () => [], () => div())",
         "DataTableState column keys must be unique"},
    };
    for (const Case& c : cases) {
        Str failure = CallErrorTemp("DataTable, DataTableState", c.call);
        utassert(StrContains(failure, Str(c.message)));
    }
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { DataTable, DataTableState } from "
             "'gpui-component';\n"
             "export default class App extends View { render() { "
             "return new DataTable(DataTableState(['a']), () => [], "
             "() => div()).child(div()); } }\n"));
    host.Render();
    utassert(StrContains(host.runtime->LastComponentFailure(),
                         StrL("DataTable does not accept children")));
}

// ─── structured/: mod.rs, common.rs, description_list.rs, form.rs, table.rs ─

// mod.rs registers_structured_components_in_dependency_order
void RegistersStructuredComponentsInDependencyOrder() {
    FamilyCatalog catalog(&component_shell::RegisterStructured);
    utassert(catalog.ok);
    const char* names[] = {
        "DescriptionItem", "DescriptionList", "Field",        "Form",
        "TableHeader",     "TableBody",       "TableFooter",  "TableRow",
        "TableHead",       "TableCell",       "TableCaption", "Table"};
    utassert(catalog.NamesAre(names, 12));
    utassert(catalog.Documented());
}

// common.rs integer_conversions_reject_rounded_overflow_and_u16_overflow
void StructuredIntegerConversionsRejectRoundedOverflowAndU16Overflow() {
    using namespace component_shell::structured;
    uint64_t wide = 0;
    uint16_t narrow = 0;
    Str error;
    utassert(PositiveUsize(1.0, StrL("value"), &wide, &error) && wide == 1);
    utassert(
        !PositiveUsize(18446744073709551616.0, StrL("value"), &wide, &error));
    utassert(StrEq(error, StrL("value expects an exactly representable "
                               "positive integer")));
    utassert(PositiveU16(65535.0, StrL("span"), &narrow, &error) &&
             narrow == 65535);
    utassert(!PositiveU16(65536.0, StrL("span"), &narrow, &error));
    utassert(StrEq(error, StrL("span expects an integer no greater than "
                               "65535")));
}

// common.rs f32_conversion_rejects_values_that_overflow_to_infinity
void StructuredF32ConversionRejectsValuesThatOverflowToInfinity() {
    using namespace component_shell::structured;
    float out = 0;
    Str error;
    utassert(NonnegativeF32(42.5, StrL("width"), &out, &error) && out == 42.5f);
    utassert(
        !NonnegativeF32((double)FLT_MAX * 2.0, StrL("width"), &out, &error));
    utassert(!NonnegativeF32(-1.0, StrL("width"), &out, &error));
    utassert(!NonnegativeF32(NAN, StrL("width"), &out, &error));
    utassert(StrEq(error, StrL("width expects a nonnegative finite number "
                               "representable as f32")));
}

// Runs a structured recorder on one number argument.
bool RecordsNumber(bool (*record)(shell::PayloadBuild*,
                                  const shell::ComponentArgument*, int),
                   double value) {
    Arena* a = ArenaNew();
    shell::PayloadBuild build;
    build.a = a;
    shell::ComponentArgument argument = NumberArgument(value);
    bool ok = record(&build, &argument, 1);
    ArenaDelete(a);
    return ok;
}

// description_list.rs span_rejects_fractional_and_zero_values
void DescriptionSpanRejectsFractionalAndZeroValues() {
    using component_shell::structured::RecordItemSpan;
    utassert(!RecordsNumber(&RecordItemSpan, 0.0));
    utassert(!RecordsNumber(&RecordItemSpan, 1.5));
    utassert(RecordsNumber(&RecordItemSpan, 2.0));
    utassert(!RecordsNumber(&RecordItemSpan, 18446744073709551616.0));
}

// description_list.rs vertical_preserves_operations_recorded_before_and_
// after_it
void DescriptionVerticalPreservesOperationsRecordedBeforeAndAfterIt() {
    using namespace component_shell::structured;
    ListConfig config;
    ListOp ops[4];
    ops[0].kind = ListOp::Bordered;
    ops[0].bordered = false;
    ops[1].kind = ListOp::Columns;
    ops[1].columns = 7;
    ops[2].kind = ListOp::Vertical;
    ops[3].kind = ListOp::Size;
    ops[3].size = UiSize::Large;
    for (const ListOp& op : ops) config.Apply(op);
    utassert(config.vertical && !config.bordered && config.columns == 7 &&
             config.size == UiSize::Large);
}

// description_list.rs columns_rejects_values_the_component_would_otherwise_
// clamp
void DescriptionColumnsRejectsValuesTheComponentWouldOtherwiseClamp() {
    using component_shell::structured::RecordListColumns;
    utassert(RecordsNumber(&RecordListColumns, 10.0));
    utassert(!RecordsNumber(&RecordListColumns, 11.0));
    utassert(StrContains(
        CallErrorTemp("DescriptionList", "new DescriptionList().columns(11)"),
        StrL("DescriptionList.columns expects an integer from 1 through 10")));
}

// description_list.rs description_item_explicitly_rejects_children_and_style
void DescriptionItemExplicitlyRejectsChildrenAndStyle() {
    using component_shell::structured::EnsureItemSurface;
    Str error;
    utassert(EnsureItemSurface(0, false, &error));
    utassert(!EnsureItemSurface(1, false, &error));
    utassert(StrEq(error, StrL("DescriptionItem does not accept children; use "
                               "value(string)")));
    utassert(!EnsureItemSurface(0, true, &error));
    utassert(StrContains(error, StrL("does not accept style methods")));
}

// form.rs columns_accept_only_positive_integers
void FormColumnsAcceptOnlyPositiveIntegers() {
    using component_shell::structured::RecordFormColumns;
    utassert(RecordsNumber(&RecordFormColumns, 2.0));
    utassert(!RecordsNumber(&RecordFormColumns, 65536.0));
    utassert(!RecordsNumber(&RecordFormColumns, -1.0));
    utassert(!RecordsNumber(&RecordFormColumns, 1.5));
    utassert(!RecordsNumber(&RecordFormColumns, 18446744073709551616.0));
}

// form.rs field_span_and_label_width_reject_lossy_ranges
void FieldSpanAndLabelWidthRejectLossyRanges() {
    using namespace component_shell::structured;
    uint16_t span = 0;
    float width = 0;
    Str error;
    utassert(!PositiveU16(65536.0, StrL("Field.col_span"), &span, &error));
    utassert(!NonnegativeF32((double)FLT_MAX * 2.0, StrL("Form.label_width"),
                             &width, &error));
    utassert(StrContains(
        CallErrorTemp("Form, Field",
                      "new Form().child(new Field().col_span("
                      "65536))"),
        StrL("Field.col_span expects an integer no greater than 65535")));
    utassert(
        StrContains(CallErrorTemp("HForm", "new HForm().label_width(-1)"),
                    StrL("Form.label_width expects a nonnegative finite number "
                         "representable as f32")));
}

// table.rs table_span_rejects_rounded_usize_overflow
void TableSpanRejectsRoundedUsizeOverflow() {
    using component_shell::structured::RecordCellSpan;
    utassert(!RecordsNumber(&RecordCellSpan, 18446744073709551616.0));
    utassert(RecordsNumber(&RecordCellSpan, 2.0));
}

// table.rs typed_table_carriers_hold_real_gpui_component_parts
void TypedTableCarriersHoldRealGpuiComponentParts() {
    using namespace component_shell::typed_compound;
    using component_shell::structured::TableCellPart;
    RequestFixture f(0, nullptr, 0);
    Ctx* cx = &f.cx;
    El* row = TypedChildElementOf(cx, component::TableRow::New(cx));
    utassert(TakeElementAs<component::TableRow>(&f.request, row, "TableRow"));
    utassert(len(f.request.failure) == 0);
    utassert(!TakeElementAs<component::TableRow>(&f.request, row, "TableRow"));
    utassert(StrEq(f.request.failure,
                   StrL("registered TableRow child was already consumed")));

    RequestFixture g(0, nullptr, 0);
    component::TableCellEl* cell = component::TableCell::New(cx);
    El* wrong = TypedChildElement(cx, shell::PayloadTag<TableCellPart>(), cell,
                                  cell->IntoEl());
    utassert(
        !TakeElementAs<component::TableRow>(&g.request, wrong, "TableRow"));
}

// structured_host.rs structured_components_materialize_nested_children_in_
// script_order. The port's DebugTree spells a style `.p(2)` where Rust's
// prints `.p[Number(2.0)]`.
void StructuredComponentsMaterializeNestedChildrenInScriptOrder() {
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import {\n"
             "  DescriptionItem, DescriptionList, Field, HForm,\n"
             "  Table, TableBody, TableCaption, TableHeader,\n"
             "} from 'gpui-component';\n"
             "export default class StructuredApp extends View {\n"
             "  render() {\n"
             "    return div()\n"
             "      .child(new DescriptionList()\n"
             "        .bordered(false).columns(2).vertical().p(2)\n"
             "        .child(new DescriptionItem('First label').value('First "
             "value').span(1))\n"
             "        .child(new DescriptionItem('Second label').value('Second "
             "value')))\n"
             "      .child(new HForm()\n"
             "        .columns(2).label_width(120)\n"
             "        .child(new Field().label('Name').required(true)"
             ".child(div().child('Ada')))\n"
             "        .child(new Field().label('Role').child(div()"
             ".child('Admin'))))\n"
             "      .child(new Table()\n"
             "          .accessibility_label('People')\n"
             "          .size('small')\n"
             "          .child(new TableCaption().child('Current people'))\n"
             "          .child(new TableHeader())\n"
             "          .child(new TableBody()));\n"
             "  }\n"
             "}\n"));
    El* root = RenderLaidOut(host);
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(len(host.runtime->LastComponentFailure()) == 0);
    Str tree = DebugTreeTemp(host);
    const char* expected[] = {"DescriptionList",
                              ":bordered(registered)",
                              ":columns(registered)",
                              ":vertical(registered)",
                              ".p(2)",
                              "DescriptionItem",
                              "Form",
                              ":label_width(registered)",
                              "Field",
                              "Table",
                              ":accessibility_label(registered)",
                              "TableHeader",
                              "TableBody",
                              "TableCaption"};
    for (const char* contract : expected) {
        bool found = StrContains(tree, Str(contract));
        if (!found) printf("missing %s in %s\n", contract, tree.s);
        utassert(found);
    }
    auto at = [&](const char* needle) { return StrFind(tree, Str(needle)); };
    utassert(at(":bordered(registered)") < at(":columns(registered)"));
    utassert(at(":columns(registered)") < at(":vertical(registered)"));
    int items = 0;
    for (Str rest = tree; StrContains(rest, StrL("DescriptionItem")); items++) {
        int i = StrFind(rest, StrL("DescriptionItem"));
        rest = Str(rest.s + i + 1, rest.len - i - 1);
    }
    utassert(items == 2);
    utassert(at("Ada") < at("Admin"));
    utassert(at("TableHeader") < at("TableBody"));
    utassert(at("TableCaption") < at("TableHeader"));

    // And the built tree carries what the script said, in its order.
    utassert(FindText(root, StrL("First value")) != nullptr);
    utassert(FindText(root, StrL("Second label")) != nullptr);
    utassert(FindText(root, StrL("Name")) != nullptr);
    utassert(FindText(root, StrL("Ada")) != nullptr);
    utassert(FindText(root, StrL("Admin")) != nullptr);
    utassert(FindText(root, StrL("Current people")) != nullptr);
}

// Children the typed parents do not take are refused as Rust refuses them.
void StructuredParentsRefuseForeignChildren() {
    struct Case {
        const char* imports;
        const char* call;
        const char* message;
    };
    const Case cases[] = {
        {"DescriptionList", "new DescriptionList().child(div())",
         "DescriptionList"},
        {"Form", "new Form().child(div())", "Form"},
        {"Table, TableRow", "new Table().child(new TableRow())", "Table"},
        {"TableBody, TableCell", "new TableBody().child(new TableCell())",
         "TableBody"},
        {"TableRow, TableBody", "new TableRow().child(new TableBody())",
         "TableRow"},
    };
    for (const Case& c : cases) {
        TempStr source =
            fmt("import { View, div } from 'gpui-kit';\n"
                "import { %s } from 'gpui-component';\n"
                "export default class Main extends View {\n"
                "  render() { return div().child(%s); }\n"
                "}\n",
                Str(c.imports), Str(c.call));
        Host host(source);
        host.Render();
        Str failure = host.runtime->LastComponentFailure();
        bool ok = StrContains(failure, Str(c.message)) && len(failure) > 0;
        if (!ok) printf("structured refusal: %s -> %s\n", c.call, failure.s);
        utassert(ok);
    }
    utassert(RendersCleanly(
        "Table, TableHeader, TableBody, TableRow, TableHead, TableCell",
        "new Table().child(new TableHeader().child(new TableRow().child(new "
        "TableHead().child('Name')))).child(new TableBody().child(new "
        "TableRow().child(new TableCell().col_span(2).text_right().child("
        "'Ada'))))",
        "Ada"));
}

// ─── navigation/: mod.rs, icon.rs, sidebar.rs ──────────────────────────────

// mod.rs remaining_catalog_registers_real_icon_and_sidebar_exports
void RemainingCatalogRegistersRealIconAndSidebarExports() {
    FamilyCatalog catalog(&component_shell::RegisterNavigation);
    utassert(catalog.ok);
    const char* names[] = {"Icon",
                           "SidebarMenuItem",
                           "SidebarMenu",
                           "SidebarHeader",
                           "SidebarFooter",
                           "Sidebar",
                           "SidebarToggleButton"};
    utassert(catalog.NamesAre(names, 7));
    utassert(catalog.Documented());
}

// icon.rs icon_numeric_and_size_arguments_are_closed
void IconNumericAndSizeArgumentsAreClosed() {
    using namespace component_shell::navigation;
    UiSize size = UiSize::Medium;
    float radians = 0;
    Str error;
    utassert(IconSize(StrL("small"), &size, &error) && size == UiSize::Small);
    utassert(!IconSize(StrL("tiny"), &size, &error));
    utassert(StrEq(error, StrL("unsupported Icon size `tiny`")));
    utassert(IconRotation(0.5, &radians, &error) && radians == 0.5f);
    utassert(!IconRotation(NAN, &radians, &error));
    utassert(!IconRotation(INFINITY, &radians, &error));
    utassert(StrEq(error, StrL("Icon.rotate expects finite radians "
                               "representable as f32")));
}

// icon.rs icon_paths_are_relative_to_the_application_asset_root
void IconPathsAreRelativeToTheApplicationAssetRoot() {
    using component_shell::navigation::IconPath;
    Str error;
    utassert(IconPath(StrL("icons/check.svg"), &error));
    utassert(!IconPath(StrL("/tmp/check.svg"), &error));
    utassert(!IconPath(StrL("../check.svg"), &error));
    utassert(!IconPath(StrL("icons/../../check.svg"), &error));
    utassert(StrEq(error, StrL("Icon path must stay inside the application "
                               "asset root")));
    utassert(!IconPath(StrL("  "), &error));
    utassert(StrEq(error, StrL("Icon path must not be empty")));
}

// The method `method` of the navigation catalog's `component`.
const MethodDescriptor* NavigationMethod(const FamilyCatalog& catalog,
                                         const char* component,
                                         const char* method) {
    const ComponentDescriptor* d = catalog.frozen.Find(Str(component));
    if (!d) return nullptr;
    for (const MethodDescriptor& m : d->methods)
        if (strcmp(m.name, method) == 0) return &m;
    return nullptr;
}

// sidebar.rs sidebar_exports_use_closed_boolean_and_enum_schemas
void SidebarExportsUseClosedBooleanAndEnumSchemas() {
    FamilyCatalog catalog(&component_shell::RegisterNavigation);
    const MethodDescriptor* side = NavigationMethod(catalog, "Sidebar", "side");
    utassert(side && side->arguments.count == 1);
    if (side) {
        const ArgumentSchema& schema = side->arguments[0].schema;
        utassert(schema.kind == shell::SchemaKind::Enum &&
                 schema.values.count == 2 &&
                 strcmp(schema.values[0], "left") == 0 &&
                 strcmp(schema.values[1], "right") == 0);
    }
    const MethodDescriptor* collapsed =
        NavigationMethod(catalog, "Sidebar", "collapsed");
    utassert(collapsed && collapsed->arguments[0]
                                  .schema.kind == shell::SchemaKind::Boolean);
}

// sidebar.rs clickable_sidebar_parts_declare_the_exact_common_callback_schema
void ClickableSidebarPartsDeclareTheExactCommonCallbackSchema() {
    FamilyCatalog catalog(&component_shell::RegisterNavigation);
    for (const char* component : {"SidebarMenuItem", "SidebarToggleButton"}) {
        const MethodDescriptor* method =
            NavigationMethod(catalog, component, "on_click");
        utassert(method && method->arguments.count == 1);
        if (!method) continue;
        utassert(strcmp(method->arguments[0].name, "callback") == 0);
        utassert(method->arguments[0]
                         .schema.kind == shell::SchemaKind::Callback &&
                 strcmp(method->arguments[0].schema.text,
                        "(event: ClickEvent, cx: Context) => void") == 0);
    }
}

// sidebar.rs sidebar_menu_items_expose_a_closed_icon_vocabulary
void SidebarMenuItemsExposeAClosedIconVocabulary() {
    FamilyCatalog catalog(&component_shell::RegisterNavigation);
    const MethodDescriptor* icon =
        NavigationMethod(catalog, "SidebarMenuItem", "icon");
    utassert(icon != nullptr);
    if (!icon) return;
    const ArgumentSchema& schema = icon->arguments[0].schema;
    const char* expected[] = {"home", "components", "settings", "archive",
                              "account"};
    utassert(schema.kind == shell::SchemaKind::Enum && schema.values
                                                               .count == 5);
    for (int i = 0; i < 5 && i < schema.values.count; i++)
        utassert(strcmp(schema.values[i], expected[i]) == 0);
}

// sidebar.rs typed_carrier_rejects_double_consumption
void SidebarTypedCarrierRejectsDoubleConsumption() {
    RequestFixture f(0, nullptr, 0);
    El* element =
        component_shell::CarrierOf(&f.cx, component::SidebarMenu::New(&f.cx));
    utassert(component_shell::TakeCarriedAs<component::SidebarMenu>(
                 &f.request, element, "SidebarMenu") != nullptr);
    utassert(component_shell::TakeCarriedAs<component::SidebarMenu>(
                 &f.request, element, "SidebarMenu") == nullptr);
}

// sidebar.rs sidebar_typed_parents_reject_wrong_registered_and_ordinary_
// children
void SidebarTypedParentsRejectWrongRegisteredAndOrdinaryChildren() {
    using component_shell::navigation::RequireRegisteredChild;
    Str error;
    utassert(!RequireRegisteredChild("Sidebar", "SidebarMenu", "Icon", &error));
    utassert(
        !RequireRegisteredChild("Sidebar", "SidebarMenu", nullptr, &error));
    utassert(StrContains(error, StrL("ordinary element")));
    utassert(RequireRegisteredChild("SidebarMenu", "SidebarMenuItem",
                                    "SidebarMenuItem", &error));
}

// sidebar.rs sidebar_menu_item_rejects_style_instead_of_silently_dropping_it
void SidebarMenuItemRejectsStyleInsteadOfSilentlyDroppingIt() {
    using component_shell::navigation::RequireDefaultItemStyle;
    Str error;
    utassert(RequireDefaultItemStyle(false, &error));
    utassert(!RequireDefaultItemStyle(true, &error));
    utassert(StrContains(error, StrL("does not support shell style")));
    // And through a script, where the item's style is the one it was given.
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { SidebarMenu, SidebarMenuItem } from "
             "'gpui-component';\n"
             "export default class App extends View { render() { "
             "return new SidebarMenu().child(new SidebarMenuItem('A')"
             ".p(2)); } }\n"));
    host.Render();
    utassert(host.runtime->ComponentFailureCount() > 0);
}

// On the path to `text`: -1 when it is not under `element`, 1 when a box
// on the way is painted `accent`, 0 otherwise.
int AccentAbove(El* element, Str text, Rgba accent, bool seen = false) {
    if (!element) return -1;
    if (memcmp(&element->style.bg.color, &accent, sizeof(Rgba)) == 0)
        seen = true;
    if (element->kind == ElKind::Text && StrEq(element->text, text))
        return seen ? 1 : 0;
    for (El* child = element->first; child; child = child->next) {
        int found = AccentAbove(child, text, accent, seen);
        if (found >= 0) return found;
    }
    return -1;
}

// sidebar.rs common_selected_state_reaches_native_header_and_footer_selection
void CommonSelectedStateReachesNativeHeaderAndFooterSelection() {
    {
        RequestFixture f(0, nullptr, 0);
        component::SidebarHeader* header = component::SidebarHeader::New(&f.cx)
                                               ->Selected(true);
        component::SidebarFooter* footer = component::SidebarFooter::New(&f.cx)
                                               ->Selected(true);
        utassert(header->selected && footer->selected);
        header->Selected(false);
        footer->Selected(false);
        utassert(!header->selected && !footer->selected);
    }
    // The common selected state reaches them through the materializer.
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { SidebarHeader, SidebarFooter } from "
             "'gpui-component';\n"
             "export default class App extends View { render() { "
             "return div().child(new SidebarHeader().selected(true)"
             ".child('Head')).child(new SidebarFooter()"
             ".child('Foot')); } }\n"));
    El* root = host.Render();
    utassert(root && len(host.ViewError()) == 0);
    utassert(len(host.runtime->LastComponentFailure()) == 0);
    Rgba accent = ThemeNow(&host.app).tokens.sidebarAccent.color;
    utassert(AccentAbove(root, StrL("Head"), accent) == 1);
    utassert(AccentAbove(root, StrL("Foot"), accent) == 0);
}

// navigation_host.rs icon_accepts_an_application_relative_asset_and_
// rejects_traversal
void IconAcceptsAnApplicationRelativeAssetAndRejectsTraversal() {
    Host host(
        StrL("import { View } from 'gpui-kit';\n"
             "import { Icon } from 'gpui-component';\n"
             "export default class App extends View {\n"
             "  render() { return new Icon('icons/check.svg')"
             ".size('small'); }\n"
             "}\n"));
    El* root = host.Render();
    utassert(root && len(host.ViewError()) == 0);
    utassert(len(host.runtime->LastComponentFailure()) == 0);
    Str tree = DebugTreeTemp(host);
    utassert(StrContains(tree, StrL("Icon")));
    utassert(StrContains(tree, StrL(":size(registered)")));
    utassert(StrContains(CallErrorTemp("Icon", "new Icon('../outside.svg')"),
                         StrL("application asset root")));
}

// navigation_host.rs sidebar_materializes_typed_items_menu_header_and_
// wrapper_style_in_order. The port's DebugTree spells a style `.p(2)` and a
// behavior `:disabled(true)` where Rust's prints `.p[Number(2.0)]` and
// `:disabled[Bool(true)]`.
void SidebarMaterializesTypedItemsMenuHeaderAndWrapperStyleInOrder() {
    Host host(StrL(
        "import { View, div } from 'gpui-kit';\n"
        "import {\n"
        "  Sidebar, SidebarFooter, SidebarHeader, SidebarMenu, "
        "SidebarMenuItem, SidebarToggleButton,\n"
        "} from 'gpui-component';\n"
        "export default class App extends View {\n"
        "  render() {\n"
        "    return div()\n"
        "      .child(new SidebarToggleButton().side('left').collapsed(false)"
        ".p(2))\n"
        "      .child(\n"
        "        new Sidebar('nav')\n"
        "          .side('left')\n"
        "          .collapsible('icon')\n"
        "          .header(new SidebarHeader().selected(true)"
        ".child('Workspace'))\n"
        "          .footer(new SidebarFooter().child('Account'))\n"
        "          .child(new SidebarMenu()\n"
        "            .child(new SidebarMenuItem('First').selected(true)"
        ".disabled(true))\n"
        "            .child(new SidebarMenuItem('Second')))\n"
        "      );\n"
        "  }\n"
        "}\n"));
    El* root = RenderLaidOut(host);
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(len(host.runtime->LastComponentFailure()) == 0);
    Str tree = DebugTreeTemp(host);
    const char* expected[] = {
        "SidebarToggleButton", ".p(2)",          "Sidebar", "SidebarHeader",
        "Workspace",           "SidebarFooter",  "Account", "SidebarMenu",
        ":disabled(true)",     ":selected(true)"};
    for (const char* contract : expected) {
        bool found = StrContains(tree, Str(contract));
        if (!found) printf("missing %s in %s\n", contract, tree.s);
        utassert(found);
    }
    int first = StrFind(tree, StrL("SidebarMenuItem"));
    utassert(first >= 0);
    Str rest = Str(tree.s + first + 1, tree.len - first - 1);
    int second = StrFind(rest, StrL("SidebarMenuItem"));
    utassert(second >= 0);
    second += first + 1;
    Str after = Str(tree.s + second + 1, tree.len - second - 1);
    utassert(StrFind(after, StrL("SidebarMenuItem")) < 0);
    int disabled = StrFind(tree, StrL(":disabled(true)"));
    utassert(first < disabled && disabled < second);
    utassert(FindText(root, StrL("Workspace")) != nullptr);
    utassert(FindText(root, StrL("First")) != nullptr);
    utassert(FindText(root, StrL("Second")) != nullptr);
}

// The first element in `root`, depth first, whose click listener is set.
El* FirstListener(El* element) {
    if (!element) return nullptr;
    if (element->listener.IsValid()) return element;
    for (El* child = element->first; child; child = child->next) {
        if (El* found = FirstListener(child)) return found;
    }
    return nullptr;
}

// navigation_host.rs sidebar_toggle_invokes_the_registered_common_click_
// callback. Rust clicks at laid-out positions (the toggle once, then a grid
// over the menu row); here each click is the listener the element carries,
// and the disabled row is clicked again once it has been disabled.
void SidebarToggleInvokesTheRegisteredCommonClickCallback() {
    Host host(StrL(
        "import { View, div } from 'gpui-kit';\n"
        "import { Sidebar, SidebarMenu, SidebarMenuItem, SidebarToggleButton "
        "} from 'gpui-component';\n"
        "export default class App extends View {\n"
        "  init() { this.hits = 0; this.nav_disabled = false; }\n"
        "  render() {\n"
        "    return div().w(300).h(100)\n"
        "      .child(new SidebarToggleButton().w(120).h(40)"
        ".on_click((_event, cx) => {\n"
        "        this.hits += 1;\n"
        "        cx.notify();\n"
        "      }))\n"
        "      .child(new Sidebar('disabled-nav').w(240).child(\n"
        "        new SidebarMenu().child(\n"
        "          new SidebarMenuItem('Disabled destination')\n"
        "            .selected(true)\n"
        "            .disabled(this.nav_disabled)\n"
        "            .on_click((_event, cx) => {\n"
        "              this.hits += 100;\n"
        "              this.nav_disabled = true;\n"
        "              cx.notify();\n"
        "            })\n"
        "        )\n"
        "      ))\n"
        "      .child(`Hits: ${this.hits}`);\n"
        "  }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && len(host.ViewError()) == 0);
    El* toggle = FirstListener(root);
    utassert(toggle != nullptr);
    if (toggle) Click(host, toggle);
    for (int i = 0; i < 3; i++) {
        root = host.Render();
        El* row = ListenerAbove(root, StrL("Disabled destination"));
        if (row && row != FirstListener(root)) Click(host, row);
    }
    root = host.Render();
    utassert(FindText(root, StrL("Hits: 101")) != nullptr);
}

// ─── collections/: mod.rs, tree.rs ─────────────────────────────────────────

// mod.rs catalog_is_only_honest_tree_surface and collections_host.rs
// collection_catalog_stays_bounded: the family registers TreeItem and Tree
// and none of the deferred delegate collections.
void CollectionCatalogIsOnlyTheHonestTreeSurface() {
    FamilyCatalog catalog(&component_shell::RegisterCollections);
    utassert(catalog.ok);
    const char* names[] = {"TreeItem", "Tree"};
    utassert(catalog.NamesAre(names, 2));
    utassert(catalog.Documented());
    const char* deferred[] = {"List",     "DataTable",      "Select",
                              "Combobox", "SearchableList", "VirtualList"};
    for (const char* name : deferred)
        utassert(catalog.frozen.Find(Str(name)) == nullptr);
}

// tree.rs test_probe: the rows built, reduced the way distinct_rows does —
// one per id, in first-seen order, the latest build winning.
struct TreeRowSeen {
    char id[16];
    char label[32];
    bool selected;
};
TreeRowSeen gTreeRows[16];
int gTreeRowCount = 0;

void RecordTreeRow(Str id, Str label, bool selected) {
    TreeRowSeen* row = nullptr;
    for (int i = 0; i < gTreeRowCount; i++) {
        if (StrEq(Str(gTreeRows[i].id), id)) row = &gTreeRows[i];
    }
    if (!row) {
        if (gTreeRowCount >= 16) return;
        row = &gTreeRows[gTreeRowCount++];
    }
    snprintf(row->id, sizeof(row->id), "%.*s", (int)len(id), id.s);
    snprintf(row->label, sizeof(row->label), "%.*s", (int)len(label), label.s);
    row->selected = selected;
}

// Whether the distinct rows are exactly `expected`, each spelled
// `id|label|0` or `id|label|1`.
bool TreeRowsAre(const char* const* expected, int count) {
    if (gTreeRowCount != count) return false;
    for (int i = 0; i < count; i++) {
        char row[1024];
        snprintf(row, sizeof(row), "%s|%s|%d", gTreeRows[i].id,
                 gTreeRows[i].label, gTreeRows[i].selected ? 1 : 0);
        if (strcmp(row, expected[i]) != 0) return false;
    }
    return true;
}

// collections_host.rs tree_native_interaction_and_data_sync_survive_public_
// js_refresh: a press on the folder selects and collapses it natively; a
// refresh that renames it and adds a child syncs the data while the native
// selection and collapse persist; expanding again reveals the synchronized
// structure. This also covers tree.rs duplicate_ids_and_expansion_merge_are_
// defined's merge half: the incoming expanded(true) loses to the native
// collapse of the same id. Rust clicks at window coordinates; here the press
// goes to the row's own listener.
void TreeNativeInteractionAndDataSyncSurvivePublicJsRefresh() {
    FamilyCatalog catalog(&component_shell::RegisterCollections);
    component_shell::SetTreeRowProbe(&RecordTreeRow);
    {
        Host host(
            StrL("import { View } from 'gpui-kit';\n"
                 "import { Tree, TreeItem } from 'gpui-component';\n"
                 "export default class App extends View {\n"
                 "  init() { this.renders = 0; }\n"
                 "  render() { const updated = this.renders++ > 0;\n"
                 "    const folder = new TreeItem('src', updated ? 'Sources "
                 "renamed' : 'Source').expanded(true)\n"
                 "      .child(new TreeItem('main', 'main.rs'));\n"
                 "    if (updated) folder.child(new TreeItem('lib', "
                 "'lib.rs'));\n"
                 "    return new Tree('files').p(2).child(folder)\n"
                 "    .child(new TreeItem('readme', 'README').disabled(true)); "
                 "}\n"
                 "}\n"),
            &catalog.frozen);
        gTreeRowCount = 0;
        El* root = host.Render();
        utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
        utassert(len(host.runtime->LastComponentFailure()) == 0);
        const char* initial[] = {"src|Source|0", "main|main.rs|0",
                                 "readme|README|0"};
        utassert(TreeRowsAre(initial, 3));

        El* folder = ListenerAbove(root, StrL("Source"));
        utassert(folder != nullptr);
        if (!folder) return;
        Click(host, folder);
        gTreeRowCount = 0;
        root = host.Render();
        const char* collapsed[] = {"src|Source|1", "readme|README|0"};
        utassert(TreeRowsAre(collapsed, 2));

        gTreeRowCount = 0;
        root = Rerender(host);
        utassert(len(host.ViewError()) == 0);
        utassert(len(host.runtime->LastComponentFailure()) == 0);
        const char* synced[] = {"src|Sources renamed|1", "readme|README|0"};
        utassert(TreeRowsAre(synced, 2));

        folder = ListenerAbove(root, StrL("Sources renamed"));
        utassert(folder != nullptr);
        if (!folder) return;
        Click(host, folder);
        gTreeRowCount = 0;
        host.Render();
        const char* expanded[] = {"src|Sources renamed|1", "main|main.rs|0",
                                  "lib|lib.rs|0", "readme|README|0"};
        utassert(TreeRowsAre(expanded, 4));
    }
    component_shell::SetTreeRowProbe(nullptr);
}

// The failure a Tree script's render leaves behind.
Str TreeFailureTemp(const char* expression) {
    FamilyCatalog catalog(&component_shell::RegisterCollections);
    TempStr source =
        fmt("import { View, div } from 'gpui-kit';\n"
            "import { Tree, TreeItem } from 'gpui-component';\n"
            "export default class App extends View { render() { "
            "return %s; } }\n",
            Str(expression));
    Host host(source, &catalog.frozen);
    host.Render();
    return fmt("%s", host.runtime->LastComponentFailure());
}

// collections_host.rs typed_materializer_boundary_rejects_ordinary_and_
// registered_wrong_children, tree.rs wrong_children_are_rejected (a Tree
// takes TreeItems, a styled TreeItem is refused) and the duplicate-id half
// of duplicate_ids_and_expansion_merge_are_defined, through the scripts that
// reach them. A nested TreeItem's own refusal is drawn as its failure and
// the Tree then refuses the child it got, so the TreeItem cases render the
// item as the root to read its message.
void TreeTypedBoundaryRejectsWrongChildrenStyleAndDuplicates() {
    utassert(StrContains(TreeFailureTemp("new Tree('ordinary').child(div())"),
                         StrL("ordinary element")));
    utassert(StrContains(
        TreeFailureTemp("new Tree('outer').child(new Tree('wrong'))"),
        StrL("received Tree")));
    utassert(StrContains(
        TreeFailureTemp("new TreeItem('a', 'A').child(div())"),
        StrL("TreeItem accepts only registered TreeItem children")));
    utassert(
        StrContains(TreeFailureTemp("new TreeItem('a', 'A').p(2)"),
                    StrL("TreeItem is data and does not support shell style")));
    utassert(len(TreeFailureTemp("new Tree('t').child(new TreeItem('a', "
                                 "'A'))")) == 0);
    utassert(StrContains(
        TreeFailureTemp("new Tree('t').child(new TreeItem('same', 'A'))"
                        ".child(new TreeItem('same', 'B'))"),
        StrL("TreeItem id `same` is duplicated; ids must be unique within a "
             "Tree")));
    utassert(StrContains(CallErrorTemp("TreeItem", "new TreeItem('a', ' ')"),
                         StrL("TreeItem expects non-empty id and label")));
    utassert(StrContains(CallErrorTemp("Tree", "new Tree('')"),
                         StrL("Tree expects non-empty id")));
}

// ─── window_effects/mod.rs ─────────────────────────────────────────────────

// window_effects_host.rs catalog_exposes_only_closed_command_triggers.
void WindowEffectsCatalogExposesOnlyClosedCommandTriggers() {
    FamilyCatalog catalog(&component_shell::RegisterWindowEffects);
    utassert(catalog.ok);
    const char* names[] = {"Dialog", "AlertDialog", "Sheet", "Notification"};
    utassert(catalog.NamesAre(names, 4));
    utassert(catalog.Documented());
    for (int i = 0; i < catalog.frozen.DescriptorCount(); i++) {
        const ComponentDescriptor* d = catalog.frozen.Descriptor((uint32_t)i);
        const ArgumentSchema& schema = d->constructors[0].arguments[2].schema;
        utassert(schema.kind == shell::SchemaKind::Callback);
        utassert(
            StrEq(Str(schema.text), "(message: string, cx: Context) => void"));
    }
}

// The test-only lazy marker window_effects_host.rs registers beside the
// family: it records every build, and "fail" refuses.
char gEffectBuilds[16][24];
int gEffectBuildCount = 0;

bool ConstructEffectMarker(shell::PayloadBuild* build,
                           const shell::ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build->Fail(StrL("EffectMarker expects a label"));
    *build->New<Str>() = args[0].string;
    return true;
}

El* MaterializeEffectMarker(shell::MaterializeRequest* request) {
    const Str* label = request->PayloadAs<Str>();
    if (!label) return request->Fail(StrL("EffectMarker incompatible payload"));
    if (StrEq(*label, "fail"))
        return request->Fail(StrL("forced lazy factory failure"));
    if (gEffectBuildCount < 16) {
        snprintf(gEffectBuilds[gEffectBuildCount++], 24, "%.*s",
                 (int)len(*label), label->s);
    }
    return request
        ->Finish(Div(request->cx->a)->Child(TextEl(request->cx->a, *label)));
}

constexpr ArgumentDescriptor kEffectMarkerArgs[] = {
    {"label", shell::SchemaString()}};
constexpr ConstructorDescriptor kEffectMarkerConstructors[] = {
    {"EffectMarker", kEffectMarkerArgs, &ConstructEffectMarker}};
constexpr ComponentDescriptor kEffectMarker = {"EffectMarker",
                                               kEffectMarkerConstructors,
                                               {},
                                               "Test-only lazy marker.",
                                               &MaterializeEffectMarker};

bool RegisterWindowEffectsWithMarker(ComponentRegistry* registry,
                                     RegistryError* error) {
    return component_shell::RegisterWindowEffects(registry, error) &&
           registry->Register(&kEffectMarker, error);
}

bool EffectBuilt(const char* label) {
    for (int i = 0; i < gEffectBuildCount; i++) {
        if (strcmp(gEffectBuilds[i], label) == 0) return true;
    }
    return false;
}

// The window's layers, which a Root would draw over the page: each open
// dialog and the sheet, rendered into the host's frame the way the Root
// renders them after the page.
void RenderLayers(Host& host) {
    WindowLayers* layers = WindowLayersOf(&host.window);
    if (!layers) return;
    if (layers->hasSheet)
        EntityRender(&host.app, &host.window, host.frame, layers->sheet.view);
    for (int i = 0; i < layers->dialogs.len; i++) {
        EntityRender(&host.app, &host.window, host.frame,
                     layers->dialogs[i].view);
    }
}

// A frame of the whole window: the page, then the layers over it.
El* DrawWindow(Host& host) {
    El* root = host.Render();
    RenderLayers(host);
    return root;
}

Ctx HostCtx(Host& host) {
    Ctx cx;
    cx.app = &host.app;
    cx.win = &host.window;
    cx.a = host.frame;
    return cx;
}

// The element answering `action` in `element`'s tree, if any.
El* FindAction(El* element, uint32_t action) {
    if (!element) return nullptr;
    for (ActionSlot* slot = element->actions; slot; slot = slot->next) {
        if (slot->action == action && slot->fn.IsValid()) return element;
    }
    for (El* child = element->first; child; child = child->next) {
        if (El* found = FindAction(child, action)) return found;
    }
    return nullptr;
}

// Dispatches `action` to the topmost dialog, the way the window would
// dispatch it to the focused dialog's key context.
void DispatchToTopDialog(Host& host, uint32_t action) {
    WindowLayers* layers = WindowLayersOf(&host.window);
    if (!layers || layers->dialogs.len == 0) return;
    El* dialog = EntityRender(&host.app, &host.window, host.frame,
                              layers->dialogs[layers->dialogs.len - 1].view);
    El* target = FindAction(dialog, action);
    utassert(target != nullptr);
    if (!target) return;
    for (ActionSlot* slot = target->actions; slot; slot = slot->next) {
        if (slot->action != action) continue;
        ActionEvent event = {};
        event.action = action;
        ListenerCall(&host.app, &host.window, slot->fn, &event);
        return;
    }
}

void ClickLabel(Host& host, El* root, const char* label) {
    El* trigger = ListenerAbove(root, Str(label));
    utassert(trigger != nullptr);
    if (trigger) Click(host, trigger);
}

// window_effects_host.rs real_click_events_open_native_surfaces_and_build_
// lazy_content. Nothing is built by rendering the triggers; each real click
// opens its surface, whose lazy content is built only when the surface is
// drawn; cancelling and closing reach the script callbacks; a failing
// content factory is reported to on_effect_error once per opening. Rust
// clicks at window coordinates and dispatches dialog::Cancel to the focused
// dialog; here the clicks go to each trigger's listener, Cancel to the
// dialog's own action handler, and the sheet's outside press to its
// overlay's.
void RealClickEventsOpenNativeSurfacesAndBuildLazyContent() {
    FamilyCatalog catalog(&RegisterWindowEffectsWithMarker);
    gEffectBuildCount = 0;
    Host host(
        StrL(
            "import { View, div } from 'gpui-kit';\n"
            "import { Dialog, AlertDialog, Sheet, Notification, "
            "EffectMarker } from 'gpui-component';\n"
            "export default class App extends View {\n"
            " init() { this.errors = 0; this.closed = 0; }\n"
            " render() { const report = (_message, cx) => { this.errors += "
            "1; cx.notify(); };\n"
            "  return div().v_flex().gap(2)\n"
            "   .child(new Dialog('dialog', 'Open dialog', report).w(180)"
            ".title('Native dialog').on_cancel(cx => { this.closed += 1; "
            "cx.notify(); }).on_close(cx => { this.closed += 1; cx.notify(); "
            "}).content(new EffectMarker('dialog-lazy')))\n"
            "   .child(new Sheet('sheet', 'Open sheet', report).w(180)"
            ".title('Native sheet').placement('left').on_close(cx => { "
            "this.closed += 1; cx.notify(); }).content(new "
            "EffectMarker('sheet-lazy')))\n"
            "   .child(new AlertDialog('alert', 'Open alert', report).w(180)"
            ".title('Native alert').description('Closed contract')"
            ".show_cancel(true).on_cancel(cx => { this.closed += 1; "
            "cx.notify(); }).on_close(cx => { this.closed += 1; cx.notify(); "
            "}))\n"
            "   .child(new Notification('note', 'Notify', report).w(180)"
            ".title('Saved').message('Native notification').type('success')"
            ".autohide(false))\n"
            "   .child(new Dialog('fail-dialog', 'Open failing dialog', "
            "report).w(180).content(new EffectMarker('fail')))\n"
            "   .child(`Errors:${this.errors}`).child(`Closed:${this.closed}`);"
            "\n }\n"
            "}\n"),
        &catalog.frozen);
    Ctx cx = HostCtx(host);
    El* root = DrawWindow(host);
    root = DrawWindow(host);
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(gEffectBuildCount == 0);

    ClickLabel(host, root, "Open dialog");
    utassert(WindowHasActiveDialog(&cx));
    root = DrawWindow(host);
    root = DrawWindow(host);
    utassert(EffectBuilt("dialog-lazy"));
    utassert(FindText(root, StrL("Native dialog")) == nullptr);
    DispatchToTopDialog(host, action::Cancel());
    utassert(!WindowHasActiveDialog(&cx));

    utassert(!EffectBuilt("sheet-lazy"));
    root = DrawWindow(host);
    ClickLabel(host, root, "Open sheet");
    utassert(WindowHasActiveSheet(&cx));
    root = DrawWindow(host);
    root = DrawWindow(host);
    utassert(EffectBuilt("sheet-lazy"));
    // The press outside the sheet, on its overlay.
    WindowLayers* layers = WindowLayersOf(&host.window);
    El* sheet = layers && layers->hasSheet
                    ? EntityRender(&host.app, &host.window, host.frame,
                                   layers->sheet.view)
                    : nullptr;
    El* capture = nullptr;
    for (El* child = sheet ? sheet->first : nullptr; child;
         child = child->next) {
        if (child->onMouseDown.IsValid()) capture = child;
    }
    utassert(capture != nullptr);
    if (capture) MouseDown(host, capture);
    root = DrawWindow(host);
    utassert(!WindowHasActiveSheet(&cx));

    ClickLabel(host, root, "Open alert");
    utassert(WindowHasActiveDialog(&cx));
    DispatchToTopDialog(host, action::Cancel());
    utassert(!WindowHasActiveDialog(&cx));

    root = DrawWindow(host);
    ClickLabel(host, root, "Notify");
    utassert(WindowNotificationCount(&cx) == 1);

    root = DrawWindow(host);
    ClickLabel(host, root, "Open failing dialog");
    utassert(WindowHasActiveDialog(&cx));
    DrawWindow(host);
    DrawWindow(host);
    root = Rerender(host);
    utassert(FindText(root, StrL("Errors:1")) != nullptr);
    // Dialog: on_cancel + on_close; Sheet: on_close; AlertDialog: on_cancel
    // + on_close, the same pair a cancelled Dialog reports.
    utassert(FindText(root, StrL("Closed:5")) != nullptr);

    WindowCloseDialog(&cx);
    ClickLabel(host, root, "Open failing dialog");
    DrawWindow(host);
    DrawWindow(host);
    root = Rerender(host);
    utassert(FindText(root, StrL("Errors:2")) != nullptr);
    WindowCloseAllDialogs(&cx);
    WindowClearNotifications(&cx);
}

// window_effects_host.rs closed_alert_and_notification_reject_common_named_
// slots.
void ClosedAlertAndNotificationRejectCommonNamedSlots() {
    FamilyCatalog catalog(&component_shell::RegisterWindowEffects);
    const char* expressions[] = {
        "new AlertDialog('alert', 'Alert', (_message, _cx) => {})"
        ".content(div())",
        "new AlertDialog('alert', 'Alert', (_message, _cx) => {})"
        ".trigger(div())",
        "new AlertDialog('alert', 'Alert', (_message, _cx) => {})"
        ".header(div())",
        "new AlertDialog('alert', 'Alert', (_message, _cx) => {})"
        ".footer(div())",
        "new Notification('note', 'Notify', (_message, _cx) => {})"
        ".content(div())",
        "new Notification('note', 'Notify', (_message, _cx) => {})"
        ".trigger(div())",
        "new Notification('note', 'Notify', (_message, _cx) => {})"
        ".header(div())",
        "new Notification('note', 'Notify', (_message, _cx) => {})"
        ".footer(div())",
    };
    for (const char* expression : expressions) {
        TempStr source =
            fmt("import { View, div } from 'gpui-kit';\n"
                "import { AlertDialog, Notification } from 'gpui-component';\n"
                "export default class App extends View { render() { return "
                "%s; } }\n",
                Str(expression));
        Host host(source, &catalog.frozen);
        host.Render();
        host.Render();
        utassert(len(host.ViewError()) == 0);
        utassert(StrContains(host.runtime->LastComponentFailure(),
                             StrL("do not accept named slots")));
    }
    // Dialog and Sheet take only their content slot, and need it.
    const char* dialogs[][2] = {
        {"new Dialog('d', 'D', (_m, _cx) => {}).content(div()).header(div())",
         "Dialog and Sheet accept only the content named slot"},
        {"new Sheet('s', 'S', (_m, _cx) => {}).content(div()).footer(div())",
         "Dialog and Sheet accept only the content named slot"},
        {"new Dialog('d', 'D', (_m, _cx) => {})",
         "Dialog and Sheet require exactly one content(element) named slot"},
        {"new Sheet('s', 'S', (_m, _cx) => {}).content(div()).child(div())",
         "window effect triggers do not accept children"},
    };
    for (const auto& c : dialogs) {
        TempStr source =
            fmt("import { View, div } from 'gpui-kit';\n"
                "import { Dialog, Sheet } from 'gpui-component';\n"
                "export default class App extends View { render() { return "
                "%s; } }\n",
                Str(c[0]));
        Host host(source, &catalog.frozen);
        host.Render();
        utassert(StrContains(host.runtime->LastComponentFailure(), Str(c[1])));
    }
}

// window_effects_host.rs dialog_and_sheet_duplicate_content_is_last_call_
// wins.
void DialogAndSheetDuplicateContentIsLastCallWins() {
    FamilyCatalog catalog(&RegisterWindowEffectsWithMarker);
    const char* cases[][3] = {
        {"new Dialog('dialog', 'Dialog', (_message, _cx) => {}).content(new "
         "EffectMarker('dialog-first')).content(new "
         "EffectMarker('dialog-second'))",
         "dialog-second", "Dialog"},
        {"new Sheet('sheet', 'Sheet', (_message, _cx) => {}).content(new "
         "EffectMarker('sheet-first')).content(new "
         "EffectMarker('sheet-second'))",
         "sheet-second", "Sheet"},
    };
    for (const auto& c : cases) {
        gEffectBuildCount = 0;
        TempStr source =
            fmt("import { View } from 'gpui-kit';\n"
                "import { Dialog, Sheet, EffectMarker } from "
                "'gpui-component';\n"
                "export default class App extends View { render() { return "
                "%s; } }\n",
                Str(c[0]));
        Host host(source, &catalog.frozen);
        El* root = DrawWindow(host);
        root = DrawWindow(host);
        utassert(gEffectBuildCount == 0);
        ClickLabel(host, root, c[2]);
        DrawWindow(host);
        DrawWindow(host);
        utassert(gEffectBuildCount > 0);
        for (int i = 0; i < gEffectBuildCount; i++)
            utassert(strcmp(gEffectBuilds[i], c[1]) == 0);
        Ctx cx = HostCtx(host);
        WindowCloseAllDialogs(&cx);
        WindowCloseSheet(&cx);
    }
}

// mod.rs test_probe: the reporter failures diagnosed.
char gReporterFailure[256];
int gReporterFailureCount = 0;

void RecordReporterFailure(Str diagnosis) {
    gReporterFailureCount++;
    snprintf(gReporterFailure, sizeof(gReporterFailure), "%.*s",
             (int)len(diagnosis), diagnosis.s);
}

// window_effects_host.rs failed_factory_and_failed_reporter_are_both_
// diagnosed.
void FailedFactoryAndFailedReporterAreBothDiagnosed() {
    FamilyCatalog catalog(&RegisterWindowEffectsWithMarker);
    component_shell::SetWindowEffectsReporterFailureProbe(
        &RecordReporterFailure);
    {
        Host host(StrL("import { View } from 'gpui-kit';\n"
                       "import { Dialog, EffectMarker } from "
                       "'gpui-component';\n"
                       "export default class App extends View { render() { "
                       "return new Dialog('fail', 'Fail', () => { throw new "
                       "Error('reporter exploded'); }).content(new "
                       "EffectMarker('fail')); } }\n"),
                  &catalog.frozen);
        El* root = DrawWindow(host);
        gReporterFailureCount = 0;
        ClickLabel(host, root, "Fail");
        DrawWindow(host);
        utassert(gReporterFailureCount == 1);
        utassert(strstr(gReporterFailure, "forced lazy factory failure"));
        utassert(strstr(gReporterFailure, "reporter exploded"));
        Ctx cx = HostCtx(host);
        WindowCloseAllDialogs(&cx);
    }
    component_shell::SetWindowEffectsReporterFailureProbe(nullptr);
}

// mod.rs: the recorders refuse what Rust refuses.
void WindowEffectRecordersRefuseWhatRustRefuses() {
    utassert(StrContains(
        CallErrorTemp("Dialog", "new Dialog(' ', 'Open', (_m, _cx) => {})"),
        StrL("Dialog(id, label, on_effect_error) expects two non-empty "
             "strings and a callback")));
    utassert(StrContains(CallErrorTemp("Sheet",
                                       "new Sheet('s', 'Open', (_m, _cx) => {})"
                                       ".title(' ')"),
                         StrL("title(text) expects non-empty text")));
    utassert(
        StrContains(CallErrorTemp("Notification",
                                  "new Notification('n', 'N', (_m, _cx) => {})"
                                  ".message('')"),
                    StrL("message(text) expects non-empty text")));
}
// ─── chart/mod.rs, tests/chart_host.rs ─────────────────────────────────────

// mod.rs catalog_contains_only_concrete_constructible_charts
void CatalogContainsOnlyConcreteConstructibleCharts() {
    FamilyCatalog catalog(&component_shell::RegisterChart);
    utassert(catalog.ok);
    const char* expected[] = {"BarChart", "LineChart", "AreaChart", "PieChart",
                              "RadarChart"};
    utassert(catalog.NamesAre(expected, 5));
    utassert(catalog.Documented());
}

// The chart series in the tree, in tree order.
int CollectCharts(El* element, const ChartSeries** out, int count, int cap) {
    if (!element) return count;
    if (const ChartSeries* chart = element->Chart()) {
        if (count < cap) out[count++] = chart;
    }
    for (El* child = element->first; child; child = child->next)
        count = CollectCharts(child, out, count, cap);
    return count;
}

// A pie is painted by its own hook rather than carried as a ChartSeries.
int CountCustomPainted(El* element) {
    if (!element) return 0;
    int count = element->customPaint && !element->Chart() ? 1 : 0;
    for (El* child = element->first; child; child = child->next)
        count += CountCustomPainted(child);
    return count;
}

Str gChartError;
void RecordChartError(Str error) {
    StrFree(gChartError);
    gChartError = StrDup(error);
}

// chart_host.rs concrete_charts_consume_plain_immutable_rows. Rust reads the
// debug tree for the five chart names; the port has no named element per
// chart, so it finds the four series the charts carry and the painted pie,
// with the ops the script recorded.
void ConcreteChartsConsumePlainImmutableRows() {
    FamilyCatalog catalog(&component_shell::RegisterChart);
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { BarChart, LineChart, AreaChart, PieChart, RadarChart } "
             "from 'gpui-component';\n"
             "globalThis.calls = 0;\n"
             "const rows = () => { globalThis.calls++; return [{label: 'Jan', "
             "value: 2}, {label: 'Feb', value: 5}]; };\n"
             "export default class App extends View {\n"
             "  render() { return div()\n"
             "    .child(new BarChart(rows).grid(false).value_axis(true))\n"
             "    .child(new LineChart(rows).linear().dot().grid(false))\n"
             "    .child(new AreaChart(rows).step_after().grid(false))\n"
             "    .child(new PieChart(rows).inner_radius(8).pad_angle(0.05)"
             ".labels(true))\n"
             "    .child(new RadarChart(rows).dot().grid_levels(3)); }\n"
             "}\n"),
        &catalog.frozen);
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(len(host.runtime->LastComponentFailure()) == 0);
    utassert(FindTextPrefix(root, StrL("Failed to")) == nullptr);
    const ChartSeries* charts[8] = {};
    int count = CollectCharts(root, charts, 0, 8);
    utassert(count == 4);
    if (count == 4) {
        utassert(charts[0]->kind == ChartKind::Bar && !charts[0]->grid &&
                 charts[0]->valueAxis && charts[0]->barLabels &&
                 charts[0]->n == 2 && charts[0]->ys[1] == 5.f &&
                 strcmp(charts[0]->labels[0], "Jan") == 0);
        utassert(charts[1]->kind == ChartKind::Line && !charts[1]->grid &&
                 charts[1]->dot &&
                 charts[1]->strokeStyle == ChartStroke::Linear);
        utassert(charts[2]->kind == ChartKind::Area && !charts[2]->grid &&
                 charts[2]->strokeStyle == ChartStroke::StepAfter);
        utassert(charts[3]->kind == ChartKind::Radar && charts[3]->grid &&
                 charts[3]->dot && charts[3]->gridLevels == 3);
    }
    utassert(CountCustomPainted(root) == 1);
}

// chart_host.rs chart_rows_reject_missing_fields_without_panicking.
void ChartRowsRejectMissingFieldsWithoutPanicking() {
    FamilyCatalog catalog(&component_shell::RegisterChart);
    component_shell::SetChartErrorProbe(&RecordChartError);
    StrFree(gChartError);
    gChartError = {};
    {
        Host host(StrL("import { View } from 'gpui-kit';\n"
                       "import { BarChart } from 'gpui-component';\n"
                       "export default class App extends View { render() { "
                       "return new BarChart(() => [{label: 'Jan'}]); } }\n"),
                  &catalog.frozen);
        El* root = host.Render();
        utassert(root && len(host.ViewError()) == 0);
        utassert(StrContains(gChartError, StrL("finite number field `value`")));
        utassert(FindTextPrefix(root, StrL("Failed to build BarChart data: "
                                           "chart row 0 must have finite "
                                           "number field `value`")) != nullptr);
    }
    component_shell::SetChartErrorProbe(nullptr);
    StrFree(gChartError);
    gChartError = {};
}

// mod.rs: the recorders' and the materializer's refusals.
void ChartsRefuseWhatRustRefuses() {
    struct Case {
        const char* call;
        const char* message;
    };
    const Case cases[] = {
        {"new BarChart(() => []).tick_margin(0)",
         "tick_margin expects a positive integer"},
        {"new BarChart(() => []).tick_margin(1.5)",
         "tick_margin expects a positive integer"},
        {"new RadarChart(() => []).grid_levels(-1)",
         "grid_levels expects a positive integer"},
        {"new PieChart(() => []).inner_radius(-1)",
         "inner_radius expects a non-negative finite number"},
        {"new PieChart(() => []).pad_angle(1e39)",
         "pad_angle expects a non-negative finite number"},
    };
    for (const Case& c : cases) {
        Str failure = CallErrorTemp("BarChart, RadarChart, PieChart", c.call);
        utassert(StrContains(failure, Str(c.message)));
    }
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { LineChart } from 'gpui-component';\n"
             "export default class App extends View { render() { "
             "return new LineChart(() => []).child(div()); } }\n"));
    host.Render();
    utassert(StrContains(host.runtime->LastComponentFailure(),
                         StrL("charts do not accept children")));
    Str failure = MaterializeFailure("PieChart", shell::ComponentPayload{}, 0);
    utassert(StrEq(failure, "PieChart received an incompatible payload"));
    StrFree(failure);
}
// ─── carousel.rs ───────────────────────────────────────────────────────────

// registers_the_closed_carousel_family_and_state
void RegistersTheClosedCarouselFamilyAndState() {
    FamilyCatalog catalog(&component_shell::RegisterCarousel);
    utassert(catalog.ok);
    const char* expected[] = {"Carousel",
                              "CarouselContent",
                              "CarouselItem",
                              "CarouselPrevious",
                              "CarouselNext",
                              "CarouselPagination",
                              "CarouselPaginationItem"};
    utassert(catalog.NamesAre(expected, 7));
    utassert(catalog.frozen.StateCount() == 1);
    if (catalog.frozen.StateCount() == 1) {
        utassert(strcmp(catalog.frozen.State(0)->exportName, "CarouselState") ==
                 0);
        utassert(strcmp(catalog.frozen.State(0)->kind, "CarouselState") == 0);
    }
    utassert(catalog.Documented());
}

bool HasMethod(const ComponentDescriptor* d, const char* name) {
    for (const MethodDescriptor& m : d->methods)
        if (strcmp(m.name, name) == 0) return true;
    return false;
}

// every_part_exposes_its_scriptable_surface
void EveryCarouselPartExposesItsScriptableSurface() {
    FamilyCatalog catalog(&component_shell::RegisterCarousel);
    const ComponentDescriptor* root = catalog.frozen.Find(StrL("Carousel"));
    utassert(root && HasMethod(root, "item_count") &&
             HasMethod(root, "selected_index"));
    const char* parts[] = {"Carousel",           "CarouselItem",
                           "CarouselPrevious",   "CarouselNext",
                           "CarouselPagination", "CarouselPaginationItem"};
    for (const char* part : parts) {
        const ComponentDescriptor* d = catalog.frozen.Find(Str(part));
        utassert(d && HasMethod(d, "accessibility_label"));
    }
}

// identifiers_and_indices_are_closed
void CarouselIdentifiersAndIndicesAreClosed() {
    utassert(component_shell::CarouselNonemptyId(StringArgument("carousel")));
    utassert(!component_shell::CarouselNonemptyId(StringArgument("  ")));
    shell::ComponentArgument number;
    number.kind = shell::ComponentArgumentKind::Number;
    double out = 0;
    number.number = 3.;
    utassert(component_shell::CarouselNonnegativeUsize(number, &out) &&
             out == 3.);
    number.number = -1.;
    utassert(!component_shell::CarouselNonnegativeUsize(number, &out));
    number.number = 1.5;
    utassert(!component_shell::CarouselNonnegativeUsize(number, &out));
    number.number = 18446744073709551615.0; // usize::MAX as f64
    utassert(!component_shell::CarouselNonnegativeUsize(number, &out));
}

// Not in Rust (carousel has no host test): the parts compose through the
// public host, the root reasserts a script-owned selection, and a click on
// a control reaches on_change with the new index.
void CarouselPartsComposeAndReportChanges() {
    Host host(StrL(
        "import { View, div } from 'gpui-kit';\n"
        "import { Carousel, CarouselState, CarouselContent, CarouselItem, "
        "CarouselNext, CarouselPagination, CarouselPaginationItem } from "
        "'gpui-component';\n"
        "export default class Main extends View {\n"
        "  init() { this.index = 1; this.state = CarouselState(3, 0); }\n"
        "  render() {\n"
        "    const state = this.state;\n"
        "    return div().child(new Carousel('c', state).selected_index("
        "this.index)\n"
        "      .on_change((index, cx) => { this.index = index; cx.notify(); "
        "})\n"
        "      .child(new CarouselContent(state).h(40)\n"
        "        .child(new CarouselItem('a', 0, state).child('Slide A'))\n"
        "        .child(new CarouselItem('b', 1, state).child('Slide B'))\n"
        "        .child(new CarouselItem('c', 2, state).child('Slide C')))\n"
        "      .child(new CarouselNext(state).size('small'))\n"
        "      .child(new CarouselPagination()\n"
        "        .child(new CarouselPaginationItem('p0', 0, state))\n"
        "        .child(new CarouselPaginationItem('p1', 1, state))\n"
        "        .child(new CarouselPaginationItem('p2', 2, state))))\n"
        "      .child(`index: ${this.index}`);\n"
        "  }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(len(host.runtime->LastComponentFailure()) == 0);
    utassert(FindText(root, StrL("Slide B")) != nullptr);
    utassert(FindText(root, StrL("index: 1")) != nullptr);
    // The next control and the three pagination items take clicks.
    El* clickable[8] = {};
    int count = CollectListeners(root, clickable, 0, 8);
    utassert(count == 4);
    if (count == 4) Click(host, clickable[3]);
    root = Rerender(host);
    utassert(FindText(root, StrL("index: 2")) != nullptr);
    // On the last slide the next control is disabled and takes no click;
    // the first pagination item goes back to the start.
    count = CollectListeners(root, clickable, 0, 8);
    utassert(count == 3);
    if (count == 3) Click(host, clickable[0]);
    root = Rerender(host);
    utassert(FindText(root, StrL("index: 0")) != nullptr);
}

// carousel.rs: the recorders' and the state factory's refusals.
void CarouselRefusesWhatRustRefuses() {
    struct Case {
        const char* call;
        const char* message;
    };
    const Case cases[] = {
        {"new Carousel(' ', CarouselState(1))",
         "Carousel expects a nonempty string id"},
        {"new CarouselItem('i', 1.5, CarouselState(2))",
         "CarouselItem(id, index, state) expects a nonnegative integer"},
        {"new Carousel('c', CarouselState(1)).item_count(-1)",
         "Carousel.item_count(value) expects a nonnegative integer"},
        {"new Carousel('c', CarouselState(1, 1))",
         "CarouselState selected_index must be within item_count"},
        {"new Carousel('c', CarouselState(0.5))",
         "CarouselState(item_count) expects a nonnegative integer"},
    };
    for (const Case& c : cases) {
        Str failure =
            CallErrorTemp("Carousel, CarouselItem, CarouselState", c.call);
        utassert(StrContains(failure, Str(c.message)));
    }
    Host host(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { Carousel, CarouselState } from "
             "'gpui-component';\n"
             "export default class App extends View { render() { "
             "return new Carousel('c', CarouselState(1)).child(div()); "
             "} }\n"));
    host.Render();
    utassert(len(host.runtime->LastComponentFailure()) != 0);
    Str failure =
        MaterializeFailure("CarouselPagination", shell::ComponentPayload{}, 0);
    utassert(
        StrEq(failure, "Carousel component received an incompatible payload"));
    StrFree(failure);
}
// ─── questionnaire/mod.rs, tests/questionnaire_host.rs ─────────────────────

void QuestionnaireRegistersTypedPartsAndTheRoot() {
    FamilyCatalog catalog(&component_shell::RegisterQuestionnaire);
    utassert(catalog.ok);
    const char* expected[] = {"QuestionnaireChoice", "QuestionnaireItem",
                              "QuestionnaireInput", "Questionnaire"};
    utassert(catalog.NamesAre(expected, 4));
    utassert(catalog.Documented());
}

// Every element carrying a keyboard-shortcut badge's text: the letters the
// native flow handed the active question's choices.
bool HasShortcutBadge(El* root, const char* key) {
    return FindText(root, Str(key)) != nullptr;
}

// questionnaire_host.rs questionnaire_answers_and_advances_from_script_
// declared_questions. Rust reads the shortcut badges by their debug bounds
// (`kbd:a`), clicks the first choice and presses Enter. The port has no
// debug bounds or simulated keystrokes here, so it reads the badges' text,
// activates the first choice with its own click listener and confirms
// through QuestionnaireState::ConfirmCurrent, which is what Enter on a
// filled answer runs (QuestionnaireHandleKeyDown).
void QuestionnaireAnswersAndAdvancesFromScriptDeclaredQuestions() {
    Host host(StrL(
        "import { div, View } from 'gpui-kit';\n"
        "import { Questionnaire, QuestionnaireItem, QuestionnaireChoice } "
        "from 'gpui-component';\n"
        "export default class QuestionnaireHost extends View {\n"
        "  render() {\n"
        "    return div().w(500).h(400)\n"
        "      .child(new Questionnaire('host-questionnaire')"
        ".shortcuts('letters')\n"
        "        .child(new QuestionnaireItem('direction', 'Which "
        "direction?').required(true)\n"
        "          .child(new QuestionnaireChoice('delegation', "
        "'Delegation'))\n"
        "          .child(new QuestionnaireChoice('prompts', 'Question "
        "prompts')))\n"
        "        .child(new QuestionnaireItem('tools', 'Which "
        "tools?').multiple(true)\n"
        "          .child(new QuestionnaireChoice('editor', 'Editor'))\n"
        "          .child(new QuestionnaireChoice('terminal', 'Terminal'))\n"
        "          .child(new QuestionnaireChoice('browser', 'Browser'))));\n"
        "  }\n"
        "}\n"));
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(len(host.runtime->LastComponentFailure()) == 0);
    utassert(HasShortcutBadge(root, "A"));
    utassert(HasShortcutBadge(root, "B"));
    utassert(!HasShortcutBadge(root, "C"));
    utassert(FindText(root, StrL("Which direction?")) != nullptr);
    utassert(FindText(root, StrL("Which tools?")) == nullptr);

    El* choice = ListenerAbove(root, StrL("Delegation"));
    utassert(choice != nullptr);
    if (choice) Click(host, choice);
    root = Rerender(host);
    // Enter on the filled answer: the retained state the root keyed.
    Ctx cx = {};
    cx.app = &host.app;
    cx.win = &host.window;
    cx.a = host.frame;
    Entity<QuestionnaireState> state =
        component_shell::QuestionnaireStateFor(&cx, StrL("host-questionnaire"));
    QuestionnaireState* native = state.Get(&host.app);
    utassert(native != nullptr);
    if (native) {
        utassert(StrEq(native->CurrentItem(), "direction"));
        cx.self = state.id;
        utassert(native->ConfirmCurrent(&cx));
        utassert(StrEq(native->CurrentItem(), "tools"));
    }
    root = Rerender(host);
    utassert(HasShortcutBadge(root, "C"));
    utassert(FindText(root, StrL("Which tools?")) != nullptr);
}

// mod.rs: the constructors' and materializers' refusals.
void QuestionnaireRefusesWhatRustRefuses() {
    struct Case {
        const char* call;
        const char* message;
    };
    const Case cases[] = {
        {"new QuestionnaireChoice(' ', 'x')",
         "QuestionnaireChoice expects a non-empty value and label"},
        {"new QuestionnaireItem('n', '')",
         "QuestionnaireItem expects a non-empty name and label"},
        {"new Questionnaire(' ')", "Questionnaire expects a non-empty id"},
    };
    for (const Case& c : cases) {
        Str failure = CallErrorTemp(
            "Questionnaire, QuestionnaireItem, QuestionnaireChoice", c.call);
        utassert(StrContains(failure, Str(c.message)));
    }
    struct Render {
        const char* body;
        const char* message;
    };
    const Render renders[] = {
        {"new Questionnaire('q').child(div())",
         "Questionnaire accepts only registered QuestionnaireItem children"},
        // Materialized on its own, so the failure is the choice's rather
        // than its parent's report of it.
        {"div().child(new QuestionnaireChoice('x', 'X').p(4))",
         "QuestionnaireChoice carries data rather than a box, so it does not "
         "implement Styled"},
        {"new Questionnaire('q').child(new QuestionnaireItem('a', 'A'))"
         ".child(new QuestionnaireItem('a', 'B'))",
         "Questionnaire schema is invalid: "},
    };
    for (const Render& r : renders) {
        TempStr source =
            fmt("import { View, div } from 'gpui-kit';\n"
                "import { Questionnaire, QuestionnaireItem, "
                "QuestionnaireChoice } from 'gpui-component';\n"
                "export default class App extends View { render() { "
                "return %s; } }\n",
                Str(r.body));
        Host host(source);
        host.Render();
        Str failure = host.runtime->LastComponentFailure();
        utassert(StrContains(failure, Str(r.message)));
    }
}

// ─── the whole catalog ─────────────────────────────────────────────────────

// With every family registered, the catalog's whole `declare module
// "gpui-component"` block and its Element union are byte for byte what the
// Rust catalog writes: every export in the same order, nothing missing and
// nothing extra (130 element types and 14 state exports).
void WholeCatalogDeclarationsEqualRust() {
    const FrozenComponentRegistry* frozen = component_shell::Components();
    utassert(frozen->DescriptorCount() == 130);
    utassert(frozen->StateCount() == 14);

    StrBuilder rust;
    AppendRustComponentDeclarations(&rust);
    Str expected = Str(rust.els, rust.len);
    // The inline-token types sit between the module's import line and its
    // first state or descriptor, which is how typings.cpp finds them in the
    // runtime's own declarations too.
    Str header = StrL(
        "declare module \"gpui-component\" {\n"
        "  import { ClickEvent, Context, Element, NativeElement } from "
        "\"gpui-kit\";\n");
    utassert(StrStartsWith(expected, header));
    StrBuilder builtin;
    shell::AppendBuiltinTypeDeclarations(&builtin);
    Str text = Str(builtin.els, builtin.len);
    int moduleAt = StrFind(text, header);
    Str moduleEnd = StrL("}\n\ndeclare module \"gpui-shell\" {\n");
    int endAt =
        moduleAt >= 0
            ? StrFind(Str(text.s + moduleAt, len(text) - moduleAt), moduleEnd)
            : -1;
    utassert(moduleAt >= 0 && endAt >= 0);
    if (moduleAt < 0 || endAt < 0) return;
    int tokensAt = moduleAt + len(header);
    Str inlineTokenTypes = Str(text.s + tokensAt, moduleAt + endAt - tokensAt);

    StrBuilder ours;
    shell::AppendComponentDeclarations(&ours, frozen, inlineTokenTypes);
    Str actual = Str(ours.els, ours.len);
    bool same = StrEq(actual, expected);
    if (!same) {
        int at = 0;
        while (at < len(actual) && at < len(expected) &&
               actual.s[at] == expected.s[at])
            at++;
        int from = at > 80 ? at - 80 : 0;
        printf(
            "catalog declarations differ from Rust at %d:\n  ours: %.*s\n"
            "  rust: %.*s\n",
            at, std::min(160, len(actual) - from), actual.s + from,
            std::min(160, len(expected) - from), expected.s + from);
    }
    utassert(same);

    StrBuilder rustUnion;
    AppendRustComponentElementUnion(&rustUnion);
    StrBuilder ourUnion;
    shell::AppendComponentElementUnion(&ourUnion, frozen);
    utassert(StrEq(Str(ourUnion.els, ourUnion.len),
                   Str(rustUnion.els, rustUnion.len)));
}

// ─── tests/story_gallery_host.rs ───────────────────────────────────────────

// Where examples/js_story is from the test's working directory (the build's
// out/ directory, or the repository root). Null when the checkout has no
// such directory to read: the browser build has no file system to load it
// from.
const char* StoryRoot() {
#if GPUI_OS_WASM
    return nullptr;
#else
    static const char* const kCandidates[] = {
        "examples/js_story",
        "../examples/js_story",
        "../../examples/js_story",
        "../../../examples/js_story",
    };
    for (const char* candidate : kCandidates) {
        TempStr entry = fmt("%s/fixtures/all-examples.js", Str(candidate));
        if (PlatFileExists(entry.s)) return candidate;
    }
    return nullptr;
#endif
}

// The host of one js_story fixture, rendered.
struct StoryHost : Host {
    explicit StoryHost(const char* root, const char* fixture)
        : Host(AppDir{Str(root), Str(fixture)}) {}
    // A frame: the previous one's elements are gone, as after Rust's
    // `window.draw(cx).clear(cx)`.
    El* Draw() {
        frame->Reset();
        return Render();
    }
};

// The first element under `id` that takes a click: where Rust's simulated
// click at that element's position lands.
El* ClickTargetIn(El* root, const char* id) {
    El* element = FindById(root, Str(id));
    El* targets[1] = {};
    return CollectListeners(element, targets, 0, 1) == 1 ? targets[0] : nullptr;
}

// The text field under `id`: where Rust's click at that element focuses.
InputState* InputIn(El* root, const char* id) {
    InputState* inputs[1] = {};
    int count = 0;
    CollectInputs(FindById(root, Str(id)), inputs, &count, 1);
    return count == 1 ? inputs[0] : nullptr;
}

// input_group_comment_story_posts_once_and_cancels_the_next_draft. The
// clicks land on the elements Rust's simulated clicks hit, and the text
// arrives as one input event, the way `simulate_input` sends it.
void InputGroupCommentStoryPostsOnceAndCancelsTheNextDraft() {
    const char* storyRoot = StoryRoot();
    if (!storyRoot) return;
    StoryHost host(storyRoot, "fixtures/input-group.js");
    El* root = host.Draw();
    utassert(root && len(host.ViewError()) == 0);
    InputState* editor = InputIn(root, "ig-extra-comment");
    utassert(editor != nullptr);
    if (!editor) return;
    Str greeting = StrL("\xe4\xbd\xa0\xe5\xa5\xbd\xf0\x9f\x99\x82");
    InputReplaceTextInRange(editor, &host.app, &host.window, nullptr, greeting);
    ExecDrain();
    root = host.Draw();
    utassert(len(host.ViewError()) == 0);
    utassert(StrContains(DebugTreeTemp(host), fmt("Draft: %s", greeting)));

    El* post = ClickTargetIn(root, "ig-extra-comment-post");
    utassert(post != nullptr);
    if (post) Click(host, post);
    ExecDrain();
    root = host.Draw();
    Str posted = DebugTreeTemp(host);
    utassert(StrContains(posted, fmt("Posted: %s", greeting)));
    utassert(StrContains(posted, StrL("Draft: \xe2\x80\x94")));

    editor = InputIn(root, "ig-extra-comment");
    utassert(editor != nullptr);
    if (!editor) return;
    InputReplaceTextInRange(editor, &host.app, &host.window, nullptr,
                            StrL("New draft"));
    ExecDrain();
    root = host.Draw();
    El* cancel = ClickTargetIn(root, "ig-extra-comment-cancel");
    utassert(cancel != nullptr);
    if (cancel) Click(host, cancel);
    ExecDrain();
    root = host.Draw();
    Str cancelled = DebugTreeTemp(host);
    utassert(len(host.ViewError()) == 0);
    utassert(StrContains(cancelled, fmt("Posted: %s", greeting)));
    utassert(StrContains(cancelled, StrL("Draft: \xe2\x80\x94")));
    utassert(!StrContains(cancelled, StrL("New draft")));
}

// interactive_examples_keep_their_state_across_redraws: Rust clicks the
// compact Switch and the default Toggle by position; here the click lands on
// the control inside each fixture row.
void InteractiveExamplesKeepTheirStateAcrossRedraws() {
    const char* storyRoot = StoryRoot();
    if (!storyRoot) return;
    StoryHost host(storyRoot, "fixtures/interaction.js");
    El* root = host.Draw();
    utassert(root && len(host.ViewError()) == 0);
    Str initial = DebugTreeTemp(host);
    utassert(StrContains(initial, StrL("compact:false")));
    utassert(StrContains(initial, StrL("preview:false")));

    El* compact = ClickTargetIn(root, "switch-fixture");
    utassert(compact != nullptr);
    if (compact) Click(host, compact);
    ExecDrain();
    root = host.Draw();
    utassert(len(host.ViewError()) == 0);
    utassert(StrContains(DebugTreeTemp(host), StrL("compact:true")));

    El* preview = ClickTargetIn(root, "toggle-fixture");
    utassert(preview != nullptr);
    if (preview) Click(host, preview);
    ExecDrain();
    host.Draw();
    utassert(len(host.ViewError()) == 0);
    utassert(StrContains(DebugTreeTemp(host), StrL("preview:true")));
}

// input_story_accepts_text_and_keeps_it_across_redraws: Rust clicks the
// first Input example and types; the text survives two more frames.
void InputStoryAcceptsTextAndKeepsItAcrossRedraws() {
    const char* storyRoot = StoryRoot();
    if (!storyRoot) return;
    StoryHost host(storyRoot, "fixtures/input.js");
    El* root = host.Draw();
    utassert(root && len(host.ViewError()) == 0);
    InputState* input = InputIn(root, "input-target");
    utassert(input != nullptr);
    if (!input) return;
    TypeInto(host, input, "roadmap");
    ExecDrain();
    for (int frame = 0; frame < 2; frame++) {
        root = host.Draw();
        utassert(root && len(host.ViewError()) == 0);
        InputState* again = InputIn(root, "input-target");
        utassert(again == input &&
                 StrContains(InputValue(input), StrL("roadmap")));
    }
}

// dock_story_materializes_real_panels_dock_and_tabs
void DockStoryMaterializesRealPanelsDockAndTabs() {
    const char* storyRoot = StoryRoot();
    if (!storyRoot) return;
    StoryHost host(storyRoot, "fixtures/dock.js");
    host.Draw();
    ExecDrain();
    El* root = host.Draw();
    utassert(root && len(host.ViewError()) == 0);
    Str tree = DebugTreeTemp(host);
    utassert(StrContains(tree, StrL("dock_area")));
    utassert(StrContains(tree, StrL(":tab_bar(fn)")));
    utassert(StrContains(tree, StrL(":dock(fn)")));
}

Str gStorySurfaces[256];
int gStorySurfaceCount = 0;
Str gSelectedSurface;

void StoryRegisterSurfaces(HostCall* call) {
    const HostValue* list = nullptr;
    if (!call->arguments->Value(0, &list, &call->error)) return;
    if (list->kind != HostValueKind::Array) {
        call->Fail(StrL("fixture surface list"));
        return;
    }
    for (int i = 0; i < gStorySurfaceCount; i++) StrFree(gStorySurfaces[i]);
    gStorySurfaceCount = 0;
    for (HostValue* value : list->array) {
        if (value->kind != HostValueKind::String || gStorySurfaceCount >= 256) {
            call->Fail(StrL("surface name"));
            return;
        }
        gStorySurfaces[gStorySurfaceCount++] = StrDup(value->string);
    }
    call->result.SetNull();
}

void StorySelectedSurface(HostCall* call) {
    if (gSelectedSurface)
        call->result.SetString(gSelectedSurface);
    else
        call->result.SetNull();
}

// every_registered_story_example_materializes: each surface the gallery
// covers is selected in turn and rendered, and every one builds without a
// script error and without a registered component failing to materialize.
void EveryRegisteredStoryExampleMaterializes() {
    const char* storyRoot = StoryRoot();
    if (!storyRoot) return;
    ShellClearExportedModules();
    HostModule* module = HostModule::New(StrL("story-gallery-fixture"))
                             ->Function(StrL("register_surfaces"),
                                        MkFunc1Void(StoryRegisterSurfaces))
                             ->Function(StrL("selected_surface"),
                                        MkFunc1Void(StorySelectedSurface));
    HostError hostError;
    utassert(ShellExportModule(module, &hostError));
    module->Release();
    gSelectedSurface = {};
    {
        StoryHost host(storyRoot, "fixtures/all-examples.js");
        utassert(!host.error.IsSet() && host.view.IsValid());
        utassert(host.Draw() != nullptr && len(host.ViewError()) == 0);

        utassert(gStorySurfaceCount > 1);
        bool virtualList = false, tabBar = false, tab = false;
        for (int i = 0; i < gStorySurfaceCount; i++) {
            for (int j = i + 1; j < gStorySurfaceCount; j++)
                utassert(!StrEq(gStorySurfaces[i], gStorySurfaces[j]));
            virtualList |= StrEq(gStorySurfaces[i], StrL("VirtualList"));
            tabBar |= StrEq(gStorySurfaces[i], StrL("TabBar"));
            tab |= StrEq(gStorySurfaces[i], StrL("Tab"));
        }
        utassert(virtualList && tabBar && !tab);

        for (int i = 0; i < gStorySurfaceCount; i++) {
            Str surface = gStorySurfaces[i];
            gSelectedSurface = surface;
            uint64_t failures = host.runtime->ComponentFailureCount();
            host.Refresh();
            host.Draw();
            ExecDrain();
            El* root = host.Draw();
            bool clean = root && len(host.ViewError()) == 0 &&
                         host.runtime->ComponentFailureCount() == failures;
            Str tree = DebugTreeTemp(host);
            bool shown = StrEq(surface, StrL("VirtualList"))
                             ? StrContains(tree, StrL("v_virtual_list")) &&
                                   StrContains(tree, StrL("10,000 projects"))
                             : StrContains(tree, fmt("fixture-%s-", surface));
            if (!clean || !shown) {
                printf("story surface %.*s: %.*s%.*s\n", len(surface),
                       surface.s, len(host.ViewError()), host.ViewError().s,
                       host.runtime->ComponentFailureCount() != failures
                           ? len(host.runtime->LastComponentFailure())
                           : 0,
                       host.runtime->LastComponentFailure().s);
            }
            utassert(clean && shown);
        }
        gSelectedSurface = {};
    }
    for (int i = 0; i < gStorySurfaceCount; i++) StrFree(gStorySurfaces[i]);
    gStorySurfaceCount = 0;
    ShellClearExportedModules();
}

// ─── Temporary application directories ─────────────────────────────────────

// An application directory under the test's working directory, holding the
// files a test writes (main.js first) and removed with them: public_host.rs
// `TempApp` and check.rs `CheckApp`. Null `name` where there is no writable
// directory — the browser build.
struct TempApp {
    char name[64] = {};
    Str files[4] = {};
    int fileCount = 0;

    explicit TempApp(Str source) {
#if !GPUI_OS_WASM
        static int next = 0;
        snprintf(name, sizeof(name), "component_shell_app_%d", next++);
        // What a run that stopped half way may have left behind.
        const char* const stale[] = {"main.js", "gpui-shell.json",
                                     "gpui-kit.d.ts", "jsconfig.json"};
        for (const char* file : stale) Remove(Str(file));
        Fs(FsOperation::RemoveDirectory, StrL("."), Str(name));
        if (!Fs(FsOperation::MakeDirectory, StrL("."), Str(name)) ||
            !Write("main.js", source)) {
            name[0] = 0;
        }
#else
        (void)source;
#endif
    }
    ~TempApp() {
        if (!name[0]) return;
        for (int i = 0; i < fileCount; i++) {
            Remove(files[i]);
            StrFree(files[i]);
        }
        Fs(FsOperation::RemoveDirectory, StrL("."), Str(name));
    }
    bool Ok() const { return name[0] != 0; }
    Str Directory() const { return Str(name); }
    bool Write(const char* file, Str contents) {
        if (fileCount >= 4 ||
            !Fs(FsOperation::Write, Str(name), Str(file), contents))
            return false;
        files[fileCount++] = StrDup(Str(file));
        return true;
    }
    void Remove(Str file) { Fs(FsOperation::RemoveFile, Str(name), file); }
    static bool Fs(FsOperation operation, Str root, Str relative,
                   Str input = {}) {
        FsResult result;
        Str error;
        bool ok =
            FsRun(operation, root, relative, input, false, &result, &error);
        result.Free();
        StrFree(error);
        return ok;
    }
};

// Whether `dir` was written. Only the browser build has nowhere to write it,
// and skips; anywhere else a directory that could not be made fails.
bool Ready(const TempApp& dir) {
#if !GPUI_OS_WASM
    utassert(dir.Ok());
#endif
    return dir.Ok();
}

// ─── tests/public_host.rs ──────────────────────────────────────────────────

// public_host_api_mounts_and_materializes_registered_component_js: the
// application is read from its directory, as `load_application` reads it,
// mounted as a ScriptView and drawn.
void PublicHostApiMountsAndMaterializesRegisteredComponentJs() {
    TempApp dir(
        StrL("import { div, View } from 'gpui-kit';\n"
             "import { Spinner } from 'gpui-component';\n"
             "export default class ComponentApp extends View {\n"
             "  render() {\n"
             "    return div().p(2).child('loading')"
             ".child(new Spinner().size('small'));\n"
             "  }\n"
             "}\n"));
    if (!Ready(dir)) return;
    Host host(AppDir{dir.Directory(), StrL("main.js")});
    El* root = host.Render();
    utassert(root && !host.error.IsSet() && len(host.ViewError()) == 0);
    utassert(!FindTextPrefix(root, StrL("Failed to render")));
    Str tree = DebugTreeTemp(host);
    utassert(StrContains(tree, StrL("div")));
    utassert(StrContains(tree, StrL("loading")));
    utassert(StrContains(tree, StrL("Spinner")));
    utassert(StrContains(tree, StrL(":size(registered)")));
}

// registered_component_argument_errors_are_reported_during_render
void RegisteredComponentArgumentErrorsAreReportedDuringRender() {
    TempApp dir(
        StrL("import { View } from 'gpui-kit';\n"
             "import { Spinner } from 'gpui-component';\n"
             "export default class InvalidComponentApp extends View {\n"
             "  render() {\n"
             "    return new Spinner().size('enormous');\n"
             "  }\n"
             "}\n"));
    if (!Ready(dir)) return;
    Host host(AppDir{dir.Directory(), StrL("main.js")});
    host.Render();
    Str error = host.ViewError();
    utassert(StrContains(
        error,
        StrL("size(size) expects `xsmall`, `small`, `medium`, `large`")));
    utassert(StrContains(error, StrL("at render")));
}

// failed_owner_mount_consumes_the_loaded_application, its first half: an
// application whose init throws fails its mount with that error. The second
// half — a later mount of the same loaded application is refused as
// "already been mounted" — has no C++ counterpart: a ViewType is a
// refcounted class handle that any number of ScriptViews instantiate, with
// no LoadedApplication wrapper to consume.
void FailedMountReportsTheInitError() {
    TempApp dir(
        StrL("import { View } from 'gpui-kit';\n"
             "export default class Broken extends View {\n"
             "  init() { throw new Error('init failed'); }\n"
             "  render() { return 'unreachable'; }\n"
             "}\n"));
    if (!Ready(dir)) return;
    App app;
    Window window;
    window.app = &app;
    component_shell::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime =
        ShellRuntime::New(&app, &error, component_shell::Components());
    ViewType* type =
        runtime ? runtime->LoadApp(dir.Directory(), StrL("main.js"), &error)
                : nullptr;
    utassert(type != nullptr && !error.IsSet());
    ViewObject* object =
        type ? runtime->Instantiate(type, &window, &app, nullptr, &error)
             : nullptr;
    utassert(object == nullptr);
    utassert(StrContains(error.message, StrL("init failed")));
    ViewObjectRelease(object);
    ViewTypeRelease(type);
    EntityDropAll(&app);
    if (runtime) runtime->Release();
    ShellErrorClear(&error);
    AppGlobalClear(&app);
}

// ─── tests/check.rs ────────────────────────────────────────────────────────

// What `gpui_shell check <directory> --print-spec` does with one
// application, in process: ShellCheckApplication with the component catalog
// and a hidden window. Answers the printed description, or empty with
// `error` set. The binary's exit status and its "check passed:"/"check
// failed:" lines are gpui_shell/main.cpp's printing around exactly this.
struct Checker {
    App app;
    Window window;
    ShellRuntime* runtime = nullptr;
    Arena* arena = nullptr;
    ShellError error = {};

    Checker() {
        window.app = &app;
        ShellInitWithComponents(&app, component_shell::Components());
        runtime =
            ShellRuntime::New(&app, &error, component_shell::Components());
        arena = ArenaNew();
    }
    ~Checker() {
        EntityDropAll(&app);
        if (runtime) runtime->Release();
        AppGlobalClear(&app);
        ArenaDelete(arena);
        ShellErrorClear(&error);
    }
    Str Check(const TempApp& dir) {
        ShellErrorClear(&error);
        if (!runtime) return {};
        return ShellCheckApplication(arena, runtime, dir.Directory(), &window,
                                     &app, nullptr, &error);
    }
};

// check_materializes_valid_typed_children_and_preserves_print_spec
void CheckMaterializesValidTypedChildrenAndPreservesPrintSpec() {
    TempApp dir(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { HForm, Field } from 'gpui-component';\n"
             "export default class App extends View {\n"
             "  render() {\n"
             "    return new HForm().child(new Field().label('Name')"
             ".child(div().child('Ada')));\n"
             "  }\n"
             "}\n"));
    if (!Ready(dir)) return;
    Checker checker;
    Str spec = checker.Check(dir);
    utassert(!checker.error.IsSet());
    utassert(StrContains(spec, StrL("Field")) &&
             StrContains(spec, StrL("Ada")));
}

// check_rejects_an_ordinary_child_in_a_typed_form
void CheckRejectsAnOrdinaryChildInATypedForm() {
    TempApp dir(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { HForm } from 'gpui-component';\n"
             "export default class App extends View {\n"
             "  render() { return div().child(new HForm()"
             ".child(div())); }\n"
             "}\n"));
    if (!Ready(dir)) return;
    Checker checker;
    Str spec = checker.Check(dir);
    utassert(len(spec) == 0);
    utassert(StrContains(checker.error.message,
                         StrL("failed to materialize `Form`")));
    utassert(StrContains(checker.error.message,
                         StrL("Field children; received an ordinary element")));
}

// check_rejects_style_on_a_data_only_component
void CheckRejectsStyleOnADataOnlyComponent() {
    TempApp dir(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { MenuItem } from 'gpui-component';\n"
             "export default class App extends View {\n"
             "  render() { return div().child(new MenuItem('Open', "
             "'open').p(2)); }\n"
             "}\n"));
    if (!Ready(dir)) return;
    Checker checker;
    Str spec = checker.Check(dir);
    utassert(len(spec) == 0);
    utassert(StrContains(checker.error.message,
                         StrL("failed to materialize `MenuItem`")));
    utassert(
        StrContains(checker.error.message, StrL("does not implement Styled")));
}

// check_reports_load_and_render_failures_without_hanging
void CheckReportsLoadAndRenderFailuresWithoutHanging() {
    struct Case {
        const char* source;
        const char* expected;
    };
    const Case cases[] = {
        {"this is not javascript", "main.js"},
        {"import { View } from 'gpui-kit';\n"
         "export default class App extends View {\n"
         "  render() { throw new Error('render failed deliberately'); }\n"
         "}\n",
         "render failed deliberately"},
        {"import { View } from 'gpui-kit';\n"
         "export default class App extends View {\n"
         "  render() { while (true) {} }\n"
         "}\n",
         "interrupted"},
    };
    for (const Case& c : cases) {
        TempApp dir(Str(c.source));
        if (!Ready(dir)) return;
        Checker checker;
        Str spec = checker.Check(dir);
        utassert(len(spec) == 0);
        utassert(StrContains(checker.error.message, Str(c.expected)));
    }
}

// check_reports_invalid_metadata_before_opening_a_window
void CheckReportsInvalidMetadataBeforeOpeningAWindow() {
    TempApp dir(StrL("export default 1;"));
    if (!Ready(dir)) return;
    utassert(dir.Write("gpui-shell.json", StrL("{}")));
    Checker checker;
    Str spec = checker.Check(dir);
    utassert(len(spec) == 0);
    utassert(StrContains(checker.error.message, StrL("gpui-shell.json")));
}

// runtime_check_preserves_errors_and_clears_them_before_the_next_check: one
// runtime checks a broken application, then a valid one, which must neither
// inherit the first failure nor render twice.
void RuntimeCheckPreservesErrorsAndClearsThemBeforeTheNextCheck() {
    TempApp invalid(
        StrL("import { View, div } from 'gpui-kit';\n"
             "import { HForm } from 'gpui-component';\n"
             "export default class App extends View {\n"
             "  render() { return new HForm().child(div()); }\n"
             "}\n"));
    TempApp valid(StrL(
        "import { View } from 'gpui-kit';\n"
        "import { HForm, Field } from 'gpui-component';\n"
        "export default class App extends View {\n"
        "  render() {\n"
        "    if (this.rendered) throw new Error('check rendered twice');\n"
        "    this.rendered = true;\n"
        "    return new HForm().child(new Field().child('checked once'));\n"
        "  }\n"
        "}\n"));
    if (!Ready(invalid) || !Ready(valid)) return;
    Checker checker;
    checker.Check(invalid);
    utassert(StrContains(checker.error.message,
                         StrL("Form accepts only registered Field children")));
    Str description = checker.Check(valid);
    utassert(!checker.error.IsSet());
    utassert(StrContains(description, StrL("checked once")));
}

// ─── tests/cli.rs and src/bin/gpui-component-shell.rs ──────────────────────

// component_shell_accepts_the_same_run_check_and_types_commands_as_shell
void ComponentShellAcceptsTheSameRunCheckAndTypesCommandsAsShell() {
    struct Case {
        const char* arguments[2];
        int count;
        shell::InvocationKind kind;
    };
    const Case cases[] = {
        {{"examples/js_story", nullptr}, 1, shell::InvocationKind::Run},
        {{"check", "examples/js_story"}, 2, shell::InvocationKind::Check},
        {{"types", "examples/js_story"}, 2, shell::InvocationKind::Types},
    };
    for (const Case& c : cases) {
        shell::Invocation invocation;
        Str error;
        utassert(shell::ShellParseInvocation(c.arguments, c.count, &invocation,
                                             &error));
        utassert(invocation.kind == c.kind);
        utassert(StrEq(invocation.directory, StrL("examples/js_story")));
        StrFree(error);
    }
}

// help_uses_the_adapter_program_name: --help answers before anything else
// can fail, even beside an unknown flag. The program is named gpui-shell by
// design — the C++ host carries the catalog itself, so there is no separate
// adapter name to brand the help with.
void HelpIsAnsweredBeforeAnyOtherArgument() {
    const char* const arguments[] = {"--unknown", "--help", "--version"};
    shell::Invocation invocation;
    Str error;
    utassert(shell::ShellParseInvocation(arguments, 3, &invocation, &error));
    utassert(invocation.kind == shell::InvocationKind::Help);
    StrFree(error);
}

// invalid_arguments_exit_two_with_adapter_branding: the sentence the host
// prints, followed by the pointer to --help, before it exits 2.
void InvalidArgumentsAreRefusedWithTheSentenceToPrint() {
    const char* const unknown[] = {"--unknown"};
    shell::Invocation invocation;
    Str error;
    utassert(!shell::ShellParseInvocation(unknown, 1, &invocation, &error));
    utassert(StrEq(error, StrL("unknown flag `--unknown`")));
    const char* const two[] = {"one", "two"};
    utassert(!shell::ShellParseInvocation(two, 2, &invocation, &error));
    utassert(StrEq(error, StrL("unexpected argument `two`; gpui-shell runs "
                               "one application directory")));
    utassert(!shell::ShellParseInvocation(nullptr, 0, &invocation, &error));
    utassert(StrEq(error, StrL("expected an application directory")));
    StrFree(error);
}

// types_uses_the_adapter_component_registry: `types` writes the declarations
// of the catalog the host carries.
void TypesUsesTheComponentCatalog() {
    TempApp dir(StrL("export default 1;"));
    if (!Ready(dir)) return;
    ShellError error = {};
    int written = 0;
    utassert(ShellWriteTypeDeclarations(dir.Directory(), nullptr, &written,
                                        &error, component_shell::Components()));
    utassert(!error.IsSet() && written > 0);
    FsResult result;
    Str fsError;
    utassert(FsRun(FsOperation::Read, dir.Directory(), StrL("gpui-kit.d.ts"),
                   {}, false, &result, &fsError));
    utassert(StrContains(result.bytes, StrL("export const Spinner:")));
    utassert(StrContains(result.bytes, StrL("export const Accordion:")));
    result.Free();
    StrFree(fsError);
    dir.Remove(StrL("gpui-kit.d.ts"));
    dir.Remove(StrL("jsconfig.json"));
    ShellErrorClear(&error);
}

// ─── src/lib.rs ────────────────────────────────────────────────────────────

// init_installs_the_component_catalog_globals: the component library's
// theme registry is the global its init installs.
void InitInstallsTheComponentCatalogGlobals() {
    App app;
    utassert(AppGlobalGet<ThemeRegistry>(&app) == nullptr);
    component_shell::Init(&app);
    utassert(AppGlobalGet<ThemeRegistry>(&app) != nullptr);
    AppGlobalClear(&app);
}

// the_frozen_catalog_carries_its_own_startup: a host holding only the
// frozen catalog, as gpui_shell does, starts the components through it.
void TheFrozenCatalogCarriesItsOwnStartup() {
    const FrozenComponentRegistry* components = component_shell::Components();
    utassert(components->Initializer() != nullptr);
    App app;
    ShellInitWithComponents(&app, components);
    utassert(AppGlobalGet<ThemeRegistry>(&app) != nullptr);
    AppGlobalClear(&app);

    // init_with_components_matches_init_for_a_catalog_without_one
    FrozenComponentRegistry bare;
    utassert(bare.Initializer() == nullptr);
    App plain;
    ShellInitWithComponents(&plain, &bare);
    utassert(AppGlobalGet<ThemeRegistry>(&plain) == nullptr);
    AppGlobalClear(&plain);
}

// Where src/shell is from the test's working directory, found the way
// StoryRoot finds examples/js_story.
const char* ShellSourceRoot() {
#if GPUI_OS_WASM
    return nullptr;
#else
    static const char* const kCandidates[] = {
        "src/shell",
        "../src/shell",
        "../../src/shell",
        "../../../src/shell",
    };
    for (const char* candidate : kCandidates) {
        TempStr probe = fmt("%s/component_registry.h", Str(candidate));
        if (PlatFileExists(probe.s)) return candidate;
    }
    return nullptr;
#endif
}

// the_runtime_does_not_depend_on_the_component_library. Rust reads
// crates/shell's Cargo.toml; here the edge is an #include. The runtime names
// no adapter: no file under src/shell includes a component_shell/ header.
// (It does include some ui/ headers — ShellRoot hosts the themed dialog,
// sheet and notification layers, and the inline-token Input and the theme
// tokens read ui/ — so that half of Rust's rule does not hold here; see
// port-status.md.)
void TheRuntimeDoesNotDependOnTheComponentCatalog() {
    const char* source = ShellSourceRoot();
    if (!source) return;
    FsResult listing;
    Str error;
    utassert(FsRun(FsOperation::ReadDirectory, Str(source), StrL("."), {},
                   false, &listing, &error));
    int files = 0;
    for (const FsEntry& entry : listing.entries) {
        if (entry.isDirectory) continue;
        FsResult file;
        Str readError;
        if (!FsRun(FsOperation::Read, Str(source), entry.name, {}, false, &file,
                   &readError)) {
            utassert(false);
            StrFree(readError);
            continue;
        }
        files++;
        bool includesAdapter =
            StrContains(file.bytes, StrL("#include \"component_shell/"));
        if (includesAdapter)
            printf("src/shell/%.*s includes a component_shell/ header\n",
                   len(entry.name), entry.name.s);
        utassert(!includesAdapter);
        file.Free();
        StrFree(readError);
    }
    utassert(files > 10);
    listing.Free();
    StrFree(error);
}

// ─── tests/inventory.rs ────────────────────────────────────────────────────

// Where the Rust tree's crates are from the test's working directory, or null
// without a .work checkout (CI builds with GPUI_NO_RUST_TREE=1 and has none):
// the inventory is Rust's, read where Rust keeps it rather than copied.
const char* RustCratesRoot() {
#if GPUI_OS_WASM
    return nullptr;
#else
    static const char* const kCandidates[] = {
        ".work/gpui-component/crates",
        "../.work/gpui-component/crates",
        "../../.work/gpui-component/crates",
        "../../../.work/gpui-component/crates",
    };
    for (const char* candidate : kCandidates) {
        TempStr probe =
            fmt("%s/component-shell/component-inventory.json", Str(candidate));
        if (PlatFileExists(probe.s)) return candidate;
    }
    return nullptr;
#endif
}

// A file of the Rust tree, copied into `a`; empty when it cannot be read.
Str ReadRustFile(Arena* a, const char* crates, const char* relative) {
    FsResult result;
    Str error;
    Str text = {};
    if (FsRun(FsOperation::Read, Str(crates), Str(relative), {}, false, &result,
              &error))
        text = StrDup(a, result.bytes);
    result.Free();
    StrFree(error);
    return text;
}

// A set of strings, as the BTreeSets inventory.rs compares.
struct NameSet {
    Vec<Str> items;
    bool Has(Str name) const {
        for (Str item : items)
            if (StrEq(item, name)) return true;
        return false;
    }
    // False when `name` was already there.
    bool Insert(Str name) {
        if (Has(name)) return false;
        VecAppend(items, name);
        return true;
    }
    bool Equals(const NameSet& other) const {
        if (len(items) != len(other.items)) return false;
        for (Str item : items)
            if (!other.Has(item)) return false;
        return true;
    }
};

// Every string of a JSON array, as a set.
NameSet JsonNames(const JsonValue* array) {
    NameSet set;
    for (const JsonValue* v = array ? array->first : nullptr; v; v = v->next)
        set.Insert(v->str);
    return set;
}

// The C++ catalog's constructor exports of `descriptor`, as a set.
NameSet CatalogExports(const ComponentDescriptor* descriptor) {
    NameSet set;
    for (const ConstructorDescriptor& c : descriptor->constructors)
        set.Insert(Str(c.exportName));
    return set;
}

// The next line of `text` at `*at`, trimmed; false at the end.
bool NextTrimmedLine(Str text, int* at, Str* line) {
    if (*at >= len(text)) return false;
    int start = *at;
    int end = start;
    while (end < len(text) && text.s[end] != '\n') end++;
    *at = end + 1;
    while (start < end && (text.s[start] == ' ' || text.s[start] == '\t'))
        start++;
    while (end > start && (text.s[end - 1] == ' ' || text.s[end - 1] == '\t' ||
                           text.s[end - 1] == '\r'))
        end--;
    *line = Str(text.s + start, end - start);
    return true;
}

// public_ui_modules and public_story_modules, into `sources` as
// "ui:name"/"story:name".
void PublicModules(Arena* a, Str uiLib, Str storiesMod, NameSet* sources) {
    int at = 0;
    Str line;
    while (NextTrimmedLine(uiLib, &at, &line)) {
        Str prefix = StrL("pub mod ");
        if (!StrStartsWith(line, prefix)) continue;
        int start = len(prefix), end = start;
        while (end < len(line) && line.s[end] != ' ' && line.s[end] != '{' &&
               line.s[end] != ';')
            end++;
        if (end > start)
            sources->Insert(
                StrDup(a, fmt("ui:%s", Str(line.s + start, end - start))));
    }
    at = 0;
    while (NextTrimmedLine(storiesMod, &at, &line)) {
        Str prefix = StrL("pub use ");
        if (!StrStartsWith(line, prefix)) continue;
        Str rest = Str(line.s + len(prefix), len(line) - len(prefix));
        int split = StrFind(rest, StrL("::"));
        if (split < 0) continue;
        Str module = Str(rest.s, split);
        while (StrEndsWith(module, StrL("_story")))
            module = Str(module.s, len(module) - len(StrL("_story")));
        sources->Insert(StrDup(a, fmt("story:%s", module)));
    }
}

// every_public_component_and_story_is_accounted_for
void EveryPublicComponentAndStoryIsAccountedFor() {
    const char* crates = RustCratesRoot();
    if (!crates) return;
    Arena* a = ArenaNew();
    const JsonValue* document = JsonParse(
        a, ReadRustFile(a, crates, "component-shell/component-inventory.json"));
    const JsonValue* items = JsonGet(document, "items");
    utassert(items && items->kind == JsonKind::Array);
    NameSet expected;
    PublicModules(a, ReadRustFile(a, crates, "component/src/lib.rs"),
                  ReadRustFile(a, crates, "story/src/stories/mod.rs"),
                  &expected);
    NameSet sources;
    int entries = 0;
    for (const JsonValue* item = items ? items->first : nullptr; item;
         item = item->next) {
        entries++;
        sources
            .Insert(StrDup(a, fmt("%s:%s", JsonString(JsonGet(item, "source")),
                                  JsonString(JsonGet(item, "name")))));
    }
    // No duplicate inventory item, and no drift from the public exports.
    utassert(entries == len(sources.items));
    utassert(len(expected.items) > 0 && sources.Equals(expected));
    ArenaDelete(a);
}

// inventory_entries_have_a_registration_or_a_reason
void InventoryEntriesHaveARegistrationOrAReason() {
    const char* crates = RustCratesRoot();
    if (!crates) return;
    Arena* a = ArenaNew();
    const JsonValue* items = JsonGet(
        JsonParse(a, ReadRustFile(a, crates,
                                  "component-shell/component-inventory.json")),
        "items");
    utassert(items != nullptr);
    for (const JsonValue* item = items ? items->first : nullptr; item;
         item = item->next) {
        Str classification = JsonString(JsonGet(item, "classification"));
        if (StrEq(classification, StrL("infrastructure"))) {
            utassert(len(JsonString(JsonGet(item, "explanation"))) > 0);
            continue;
        }
        utassert(StrEq(classification, StrL("component")) ||
                 StrEq(classification, StrL("platform")));
        const JsonValue* registration = JsonGet(item, "registration");
        utassert(registration != nullptr);
        if (!registration) continue;
        utassert(StrEq(JsonString(JsonGet(registration, "status")),
                       StrL("registered")));
        utassert(len(JsonString(JsonGet(registration, "descriptor"))) > 0);
        utassert(JsonLen(JsonGet(registration, "exports")) > 0);
        const JsonValue* related = JsonGet(registration, "related");
        for (const JsonValue* r = related ? related->first : nullptr; r;
             r = r->next) {
            utassert(len(JsonString(JsonGet(r, "descriptor"))) > 0);
            utassert(JsonLen(JsonGet(r, "exports")) > 0);
            utassert(len(JsonString(JsonGet(r, "role"))) > 0);
        }
        const JsonValue* states = JsonGet(registration, "states");
        for (const JsonValue* s = states ? states->first : nullptr; s;
             s = s->next) {
            utassert(len(JsonString(JsonGet(s, "export"))) > 0);
            utassert(len(JsonString(JsonGet(s, "kind"))) > 0);
            utassert(len(JsonString(JsonGet(s, "role"))) > 0);
        }
    }
    ArenaDelete(a);
}

// registered_inventory_matches_the_frozen_component_catalog: what Rust's
// inventory says is registered — descriptors, their exports, and the state
// exports with their kinds — is exactly the C++ catalog.
void RegisteredInventoryMatchesTheFrozenComponentCatalog() {
    const char* crates = RustCratesRoot();
    if (!crates) return;
    const FrozenComponentRegistry* frozen = component_shell::Components();
    Arena* a = ArenaNew();
    const JsonValue* items = JsonGet(
        JsonParse(a, ReadRustFile(a, crates,
                                  "component-shell/component-inventory.json")),
        "items");
    utassert(items != nullptr);
    NameSet descriptors, exports, states;
    // One descriptor's claimed exports against the catalog's.
    auto claim = [&](Str descriptor, const JsonValue* claimed) {
        const ComponentDescriptor* actual = frozen->Find(descriptor);
        if (!actual)
            printf("inventory claims missing descriptor `%.*s`\n",
                   len(descriptor), descriptor.s);
        utassert(actual != nullptr);
        if (!actual) return;
        bool same = JsonNames(claimed).Equals(CatalogExports(actual));
        if (!same)
            printf("inventory has stale exports for `%.*s`\n", len(descriptor),
                   descriptor.s);
        utassert(same);
        descriptors.Insert(descriptor);
        for (const JsonValue* e = claimed ? claimed->first : nullptr; e;
             e = e->next)
            exports.Insert(e->str);
    };
    for (const JsonValue* item = items ? items->first : nullptr; item;
         item = item->next) {
        const JsonValue* registration = JsonGet(item, "registration");
        if (!registration) continue;
        claim(JsonString(JsonGet(registration, "descriptor")),
              JsonGet(registration, "exports"));
        const JsonValue* related = JsonGet(registration, "related");
        for (const JsonValue* r = related ? related->first : nullptr; r;
             r = r->next)
            claim(JsonString(JsonGet(r, "descriptor")), JsonGet(r, "exports"));
        const JsonValue* stateList = JsonGet(registration, "states");
        for (const JsonValue* s = stateList ? stateList->first : nullptr; s;
             s = s->next) {
            Str name = JsonString(JsonGet(s, "export"));
            const StateDescriptor* actual = nullptr;
            for (int i = 0; i < frozen->StateCount(); i++) {
                if (StrEq(Str(frozen->State(i)->exportName), name))
                    actual = frozen->State(i);
            }
            if (!actual)
                printf("inventory claims missing retained state `%.*s`\n",
                       len(name), name.s);
            utassert(actual != nullptr);
            if (actual)
                utassert(
                    StrEq(Str(actual->kind), JsonString(JsonGet(s, "kind"))));
            states.Insert(name);
        }
    }

    NameSet actualDescriptors, actualExports, actualStates;
    for (int i = 0; i < frozen->DescriptorCount(); i++) {
        const ComponentDescriptor* d = frozen->Descriptor((uint32_t)i);
        actualDescriptors.Insert(Str(d->name));
        for (const ConstructorDescriptor& c : d->constructors)
            actualExports.Insert(Str(c.exportName));
    }
    for (int i = 0; i < frozen->StateCount(); i++)
        actualStates.Insert(Str(frozen->State(i)->exportName));
    utassert(descriptors.Equals(actualDescriptors));
    utassert(exports.Equals(actualExports));
    utassert(states.Equals(actualStates));
    ArenaDelete(a);
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

    TestSuite("controls");
    ControlsRegisterTheSupportedPublicExports();
    EveryControlDocumentsItsSurface();
    IdentityControlsRejectEmptyIds();
    ADisabledLinkStillTakesItsClick();
    ButtonOperationsReplayInRecordedOrder();
    BadgeNumbersRejectFractionalNegativeAndOverflow();
    LaterTagVariantPreservesEarlierSizeOutlineAndRounding();
    BadgeOperationsMaterializeInRecordedOrder();
    KbdParsesItsKeystroke();
    ClickingATwoStateControlReportsItsNewState();
    ClickingAButtonReachesTheScript();

    TestSuite("delegate_collections");
    DelegateCollectionCatalogExposesRetainedListContract();
    ListUsesAFreshImmutableSnapshotAndLazyRowRenderer();
    ListRefusesWhatRustRefuses();

    TestSuite("delegate_combobox");
    ComboboxCatalogExposesNativeSingleSelectContract();
    ComboboxNativeClickEmitsChangeAndConfirmForStableValue();
    ComboboxRefusesWhatRustRefuses();
    TestSuite("delegate_select");
    SelectCatalogExposesNativeRetainedContract();
    SelectNativeClickEmitsSelectedStableValue();
    SelectRefusesWhatRustRefuses();

    TestSuite("data_table");
    DataTableCatalogIsRetainedDataTableOnly();
    RetainedDataTableRendersLazyCellsFromPlainRows();
    DataTableRejectsNonArraySnapshotWithoutPanicking();
    DataTableRefusesWhatRustRefuses();

    TestSuite("structured");
    RegistersStructuredComponentsInDependencyOrder();
    StructuredIntegerConversionsRejectRoundedOverflowAndU16Overflow();
    StructuredF32ConversionRejectsValuesThatOverflowToInfinity();
    DescriptionSpanRejectsFractionalAndZeroValues();
    DescriptionVerticalPreservesOperationsRecordedBeforeAndAfterIt();
    DescriptionColumnsRejectsValuesTheComponentWouldOtherwiseClamp();
    DescriptionItemExplicitlyRejectsChildrenAndStyle();
    FormColumnsAcceptOnlyPositiveIntegers();
    FieldSpanAndLabelWidthRejectLossyRanges();
    TableSpanRejectsRoundedUsizeOverflow();
    TypedTableCarriersHoldRealGpuiComponentParts();
    StructuredComponentsMaterializeNestedChildrenInScriptOrder();
    StructuredParentsRefuseForeignChildren();

    TestSuite("navigation");
    RemainingCatalogRegistersRealIconAndSidebarExports();
    IconNumericAndSizeArgumentsAreClosed();
    IconPathsAreRelativeToTheApplicationAssetRoot();
    SidebarExportsUseClosedBooleanAndEnumSchemas();
    ClickableSidebarPartsDeclareTheExactCommonCallbackSchema();
    SidebarMenuItemsExposeAClosedIconVocabulary();
    SidebarTypedCarrierRejectsDoubleConsumption();
    SidebarTypedParentsRejectWrongRegisteredAndOrdinaryChildren();
    SidebarMenuItemRejectsStyleInsteadOfSilentlyDroppingIt();
    CommonSelectedStateReachesNativeHeaderAndFooterSelection();
    IconAcceptsAnApplicationRelativeAssetAndRejectsTraversal();
    SidebarMaterializesTypedItemsMenuHeaderAndWrapperStyleInOrder();
    SidebarToggleInvokesTheRegisteredCommonClickCallback();

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

    TestSuite("chat");
    MainChatComponentsAreRegistered();
    ChatComponentsMaterializeThroughThePublicHost();
    ChatRecordersRefuseWhatRustRefuses();
    MessageScrollerRendersScriptRows();

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
    AccordionReportsEveryClickInIt();
    TypedContainersRefuseForeignChildren();

    TestSuite("input_group");
    InputGroupRegistersItsSixDocumentedParts();
    TextareaRowsArePositiveWholeCounts();
    InputGroupRetainsTextCallbacksAndRoutesAddonActions();
    TwoComponentsOnOneStateBothHearItsChange();
    InputGroupRejectsWrongPartTypesAndInvalidLayoutOptions();
    InlineTokensScriptOperationsAndClickReentry();

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

    TestSuite("layout");
    RegistersOnlyRealConstructibleLayoutSurfaces();
    ResizableNumericContractsAreClosed();
    ResizableGroupRejectsStyleAndWrongChildren();
    TextareaIsAnExactLeaf();
    LayoutCatalogHasClosedStateAndTypedLayoutContracts();
    TextareaStateSurvivesTwoNativeDraws();
    ResizableConsumesTwoTypedPanels();

    TestSuite("media");
    ImageSourcesAreConfinedToTheAssetRoot();
    EditorIsAnExactLeaf();
    CatalogExposesOnlyRenderableMediaSurfaces();
    LocalImageAndRetainedEditorCrossThePublicHost();
    MediaRecordersRefuseWhatRustRefuses();

    TestSuite("scroll");
    ScrollCatalogIsBoundedToCapabilityAndRealSurfaces();
    ScrollbarLeafContractIsExact();
    RepeatedScrollConfigurationIsLastCallWins();
    SharedNativeHandleScrollsAndPreservesOffset();
    NativeScrollbarRejectsChildrenAndShellStyle();

    TestSuite("settings");
    SettingsNumericAndStructuralContractsAreClosed();
    SettingsCatalogNamesTheRealNativeHierarchy();
    FullSettingsHierarchyRebuildsLazySlotsAcrossDraws();
    SettingsRejectStyleAndForeignChildren();

    TestSuite("lifecycle");
    TooltipBuildsARealButtonWithAManagedTooltip();
    MenuBarInstallsNativeAndComponentMenuModelsAfterRender();
    ReturningToTheInstalledAppEffectCancelsAPendingReplacement();
    RetiringAnApplicationGenerationRunsItsAppEffectCleanups();
    LifecycleMenuRefusesWhatRustRefuses();
    TestSuite("collections");
    CollectionCatalogIsOnlyTheHonestTreeSurface();
    TreeNativeInteractionAndDataSyncSurvivePublicJsRefresh();
    TreeTypedBoundaryRejectsWrongChildrenStyleAndDuplicates();

    TestSuite("command");
    CommandCatalogIsClosed();
    CommandTypedBoundariesAreClosed();
    RetainedCommandTypedEntriesQueryAndConfirmCallbacksAreNative();
    NativeMenuTriggerRunsOneKeyedShowEffectPerClick();
    NativeMenuItemOperationsAreLastCallWins();
    TestSuite("window_effects");
    WindowEffectsCatalogExposesOnlyClosedCommandTriggers();
    RealClickEventsOpenNativeSurfacesAndBuildLazyContent();
    ClosedAlertAndNotificationRejectCommonNamedSlots();
    DialogAndSheetDuplicateContentIsLastCallWins();
    FailedFactoryAndFailedReporterAreBothDiagnosed();
    WindowEffectRecordersRefuseWhatRustRefuses();

    TestSuite("overlays");
    OverlayDescriptorsUseClosedSchemas();
    OverlaysRefuseWhatRustRefuses();
    HoverCardMaterializesRealTriggerContentStyleAndClosedMethods();
    PopoverContentIsLazyAndOpenChangesCrossTheRegisteredBoundary();
    HoverCardBuildsLazyContentOnlyAfterHoverAndReportsLifecycle();
    DropdownMenuOpensRealItemsAndDispatchesTheSelectedCallback();

    TestSuite("chart");
    CatalogContainsOnlyConcreteConstructibleCharts();
    ConcreteChartsConsumePlainImmutableRows();
    ChartRowsRejectMissingFieldsWithoutPanicking();
    ChartsRefuseWhatRustRefuses();

    TestSuite("carousel");
    RegistersTheClosedCarouselFamilyAndState();
    EveryCarouselPartExposesItsScriptableSurface();
    CarouselIdentifiersAndIndicesAreClosed();
    CarouselPartsComposeAndReportChanges();
    CarouselRefusesWhatRustRefuses();

    TestSuite("questionnaire");
    QuestionnaireRegistersTypedPartsAndTheRoot();
    QuestionnaireAnswersAndAdvancesFromScriptDeclaredQuestions();
    QuestionnaireRefusesWhatRustRefuses();

    TestSuite("component-shell catalog");
    WholeCatalogDeclarationsEqualRust();

    TestSuite("component-shell lib");
    InitInstallsTheComponentCatalogGlobals();
    TheFrozenCatalogCarriesItsOwnStartup();
    TheRuntimeDoesNotDependOnTheComponentCatalog();

    TestSuite("public_host");
    PublicHostApiMountsAndMaterializesRegisteredComponentJs();
    RegisteredComponentArgumentErrorsAreReportedDuringRender();
    FailedMountReportsTheInitError();

    TestSuite("check");
    CheckMaterializesValidTypedChildrenAndPreservesPrintSpec();
    CheckRejectsAnOrdinaryChildInATypedForm();
    CheckRejectsStyleOnADataOnlyComponent();
    CheckReportsLoadAndRenderFailuresWithoutHanging();
    CheckReportsInvalidMetadataBeforeOpeningAWindow();
    RuntimeCheckPreservesErrorsAndClearsThemBeforeTheNextCheck();

    TestSuite("cli");
    ComponentShellAcceptsTheSameRunCheckAndTypesCommandsAsShell();
    HelpIsAnsweredBeforeAnyOtherArgument();
    InvalidArgumentsAreRefusedWithTheSentenceToPrint();
    TypesUsesTheComponentCatalog();

    TestSuite("inventory");
    EveryPublicComponentAndStoryIsAccountedFor();
    InventoryEntriesHaveARegistrationOrAReason();
    RegisteredInventoryMatchesTheFrozenComponentCatalog();

    TestSuite("story_gallery_host");
    InputGroupCommentStoryPostsOnceAndCancelsTheNextDraft();
    InteractiveExamplesKeepTheirStateAcrossRedraws();
    InputStoryAcceptsTextAndKeepsItAcrossRedraws();
    DockStoryMaterializesRealPanelsDockAndTabs();
    EveryRegisteredStoryExampleMaterializes();
}
