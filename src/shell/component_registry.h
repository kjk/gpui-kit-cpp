#ifndef GPUI_SHELL_COMPONENT_REGISTRY_H_
#define GPUI_SHELL_COMPONENT_REGISTRY_H_

// crates/shell/src/component_registry.rs: the catalog a component library
// registers into, so a script can import it from one module.
//
// The runtime knows no concrete component. An adapter — src/component_shell
// for gpui-component — describes each one as a descriptor: the exports that
// construct it, the builder methods a script may call on it, and a
// materializer that turns the recording into an element. The runtime installs
// the exports, validates every call against the declared argument schemas,
// records the adapter's payloads into the description, and hands the node back
// to the materializer at paint time.
//
// Rust's trait objects and closures become the recurring projections: a
// descriptor is a static table, a payload factory and a materializer are
// function pointers, and a payload is an arena value tagged with its type. The
// table rows are PODs so a family is a page of `static const` data, the shape
// Rust's `ComponentDescriptor::new(..).with_constructors(vec![..])` builds.

#include "shell/spec.h"

namespace gpui {
class ShellRuntime;
struct ScriptView;
} // namespace gpui

namespace gpui::shell {

constexpr uint32_t kComponentRegistryApiVersion = 1;

// The specifier the shipped gpui-component adapter imports under.
extern const char kDefaultComponentModule[];

// A read-only view of a static array, so a descriptor table names its rows
// without a separate count to keep in step.
template <class T>
struct Slice {
    const T* items = nullptr;
    int count = 0;

    constexpr Slice() = default;
    constexpr Slice(const T* items, int count) : items(items), count(count) {}
    template <int N>
    constexpr Slice(const T (&array)[N]) : items(array), count(N) {}

    const T* begin() const { return items; }
    const T* end() const { return items + count; }
    const T& operator[](int at) const { return items[at]; }
};

enum class SchemaKind : uint8_t {
    String,
    Number,
    Boolean,
    Element,
    Entity,
    Callback,
    Enum,
    Array,
    Optional,
};

// ArgumentSchema. `text` is an Entity's kind or a Callback's TypeScript
// signature; `values` an Enum's literals; `item` what an Array holds or an
// Optional wraps.
struct ArgumentSchema {
    SchemaKind kind = SchemaKind::String;
    const char* text = nullptr;
    Slice<const char*> values = {};
    const ArgumentSchema* item = nullptr;
};

constexpr ArgumentSchema SchemaString() {
    return {SchemaKind::String, nullptr, {}, nullptr};
}
constexpr ArgumentSchema SchemaNumber() {
    return {SchemaKind::Number, nullptr, {}, nullptr};
}
constexpr ArgumentSchema SchemaBoolean() {
    return {SchemaKind::Boolean, nullptr, {}, nullptr};
}
constexpr ArgumentSchema SchemaElement() {
    return {SchemaKind::Element, nullptr, {}, nullptr};
}
constexpr ArgumentSchema SchemaEntity(const char* kind) {
    return {SchemaKind::Entity, kind, {}, nullptr};
}
constexpr ArgumentSchema SchemaCallback(const char* signature) {
    return {SchemaKind::Callback, signature, {}, nullptr};
}
constexpr ArgumentSchema SchemaEnum(Slice<const char*> values) {
    return {SchemaKind::Enum, nullptr, values, nullptr};
}
constexpr ArgumentSchema SchemaArray(const ArgumentSchema* item) {
    return {SchemaKind::Array, nullptr, {}, item};
}
constexpr ArgumentSchema SchemaOptional(const ArgumentSchema* item) {
    return {SchemaKind::Optional, nullptr, {}, item};
}

// One argument of a registered constructor, method, or state factory.
struct ArgumentDescriptor {
    const char* name = nullptr;
    ArgumentSchema schema = {};
};

enum class ComponentArgumentKind : uint8_t {
    String,
    Number,
    Boolean,
    Element,
    Entity,
    Callback,
    Enum,
    Array,
    Optional,
};

// ComponentArgument: one call argument after the runtime validated it against
// its schema. Arena-allocated with the description it was recorded into.
// `items` is an Array's elements, or an Optional's one value (count 1) or
// absence (count 0).
struct ComponentArgument {
    ComponentArgumentKind kind = ComponentArgumentKind::String;
    Str string;
    double number = 0;
    bool boolean = false;
    SpecId element = 0;
    const char* entityKind = nullptr;
    uint64_t handle = 0;
    CallbackId callback = 0;
    const ComponentArgument* items = nullptr;
    int count = 0;

    // An Optional's value, or null when it was absent. Any other kind is its
    // own value, so a factory can unwrap without asking which one it has.
    const ComponentArgument* Some() const;
};

// Values a component hands back to a script callback, and the plain data a
// delegate snapshot or state operation returns: ComponentCallbackArgument,
// ComponentCallbackValue and ComponentDataValue are one closed type here,
// since none of them may carry a handle or an element across the boundary.
enum class DataKind : uint8_t {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
};

struct ComponentDataValue {
    DataKind kind = DataKind::Null;
    bool boolean = false;
    double number = 0;
    Str string;
    const ComponentDataValue* items = nullptr;
    // An Object's keys, one per item.
    const Str* keys = nullptr;
    int count = 0;

    static ComponentDataValue Null();
    static ComponentDataValue Boolean(bool value);
    static ComponentDataValue Number(double value);
    static ComponentDataValue String(Str value);
    static ComponentDataValue Array(const ComponentDataValue* items, int count);

    // An Object's member, or null.
    const ComponentDataValue* Get(Str key) const;
};

using ComponentCallbackArgument = ComponentDataValue;
using ComponentCallbackValue = ComponentDataValue;

// A typed tag for a payload. The address of a per-type static is the type's
// identity, which is what Rust's `Any` downcast asks and RTTI would otherwise
// answer.
template <class T>
const void* PayloadTag() {
    static const char tag = 0;
    return &tag;
}

// Built-in payloads the helpers record.
struct EmptyPayload {};

// The payload a constructor or method recorded. Borrowed from the description
// that holds it.
struct PayloadBuild;

// Makes a payload from validated arguments, or fails with a message the
// script sees as a TypeError. `build->a` is the recording arena.
using PayloadFactory = bool (*)(PayloadBuild* build,
                                const ComponentArgument* arguments, int count);

struct PayloadBuild {
    Arena* a = nullptr;
    ComponentPayload out = {};
    Str error;

    template <class T>
    T* New() {
        T* value = ArenaNew<T>(a);
        out.data = value;
        out.type = PayloadTag<T>();
        return value;
    }
    // A payload that carries nothing but its type.
    template <class T>
    bool Mark() {
        out.data = nullptr;
        out.type = PayloadTag<T>();
        return true;
    }
    bool Fail(Str message);
    Str Dup(Str value) const { return StrDup(a, value); }
};

template <class T>
const T* PayloadAs(const ComponentPayload& payload) {
    return payload.type == PayloadTag<T>() ? (const T*)payload.data : nullptr;
}
template <class T>
bool PayloadIs(const ComponentPayload& payload) {
    return payload.type == PayloadTag<T>();
}

struct ConstructorDescriptor {
    const char* exportName = nullptr;
    Slice<ArgumentDescriptor> arguments = {};
    PayloadFactory factory = nullptr;
    // A deprecated export names another export of the same descriptor.
    const char* deprecationReplacement = nullptr;
    const char* deprecationMessage = nullptr;
};

struct MethodDescriptor {
    const char* name = nullptr;
    Slice<ArgumentDescriptor> arguments = {};
    const char* documentation = nullptr;
    PayloadFactory recorder = nullptr;
};

struct MaterializeRequest;

// Turns one recorded node into its element. Returns null after
// `request->Fail(...)` when it cannot.
using ComponentMaterializer = El* (*)(MaterializeRequest * request);

struct ComponentDescriptor {
    const char* name = nullptr;
    Slice<ConstructorDescriptor> constructors = {};
    Slice<MethodDescriptor> methods = {};
    const char* documentation = nullptr;
    ComponentMaterializer materialize = nullptr;
};

// A retained, adapter-owned value created on the app thread. `value` is the
// adapter's, freed by `destroy`.
struct StateBuild {
    Window* window = nullptr;
    App* app = nullptr;
    // Scratch for the factory's own strings; the value must not point into it.
    Arena* a = nullptr;
    void* value = nullptr;
    void (*destroy)(void* value) = nullptr;
    Str error;

    template <class T>
    T* New() {
        T* state = new T();
        value = state;
        destroy = [](void* held) { delete (T*)held; };
        return state;
    }
    bool Fail(Str message);
};

using StateFactory = bool (*)(StateBuild* build,
                              const ComponentArgument* arguments, int count);

// One operation on retained state. `out` is allocated from `a`.
struct StateCall {
    Window* window = nullptr;
    App* app = nullptr;
    Arena* a = nullptr;
    ComponentDataValue out = {};
    Str error;
    // A JavaScript error's `code` property, for the operations that name why
    // they refused (inline token errors).
    Str code;

    bool Fail(Str message);
};

using StateMethod = bool (*)(StateCall* call, void* state,
                             const ComponentDataValue* arguments, int count);

struct StateMethodDescriptor {
    const char* name = nullptr;
    // TypeScript parameter list and result, e.g. `(): string`.
    const char* signature = nullptr;
    bool readonly = false;
    StateMethod call = nullptr;
};

struct StateDescriptor {
    const char* exportName = nullptr;
    const char* kind = nullptr;
    Slice<ArgumentDescriptor> arguments = {};
    const char* documentation = nullptr;
    StateFactory factory = nullptr;
    Slice<StateMethodDescriptor> methods = {};
};

enum class RegistryErrorKind : uint8_t {
    None,
    IncompatibleApiVersion,
    DuplicateComponent,
    InvalidComponent,
    DuplicateExport,
    InvalidExport,
    InvalidMethod,
    InvalidArgument,
    DuplicateArgument,
    InvalidArgumentSchema,
    RequiredArgumentAfterOptional,
    DuplicateMethod,
    UndocumentedMethod,
    UnreachableMethodVocabulary,
    InvalidDeprecationReplacement,
    EmptyConstructorList,
    InvalidStateKind,
    DuplicateStateKind,
    InvalidModuleSpecifier,
};

struct RegistryError {
    RegistryErrorKind kind = RegistryErrorKind::None;
    uint32_t expected = 0;
    uint32_t actual = 0;
    const char* component = nullptr;
    const char* callable = nullptr;
    const char* argument = nullptr;
    const char* reason = nullptr;
    const char* literal = nullptr;

    bool IsSet() const { return kind != RegistryErrorKind::None; }
    bool operator==(const RegistryError& other) const;
};

// The Display text Rust gives the error, into `into`.
Str RegistryErrorMessage(Arena* into, const RegistryError& error);

// A catalog's startup, run once with the App: Rust's `with_initializer`.
using ComponentInitializer = void (*)(App* app);

class FrozenComponentRegistry;

class ComponentRegistry {
  public:
    ComponentRegistry() = default;
    ComponentRegistry(const ComponentRegistry&) = delete;
    ComponentRegistry& operator=(const ComponentRegistry&) = delete;
    ~ComponentRegistry();

    // Rust's `ComponentRegistry::new`. False with `error` set when the
    // version or the module name is refused.
    bool Open(uint32_t apiVersion, const char* moduleSpecifier,
              RegistryError* error);
    void WithInitializer(ComponentInitializer initializer);

    // The descriptor is borrowed and must outlive the frozen catalog — a
    // static table, in every adapter. Answers the component's id.
    bool Register(const ComponentDescriptor* descriptor, RegistryError* error,
                  uint32_t* id = nullptr);
    bool RegisterState(const StateDescriptor* descriptor, RegistryError* error);

    // Publishes the catalog. The builder is left empty.
    void Freeze(FrozenComponentRegistry* out);

  private:
    const char* moduleSpecifier = nullptr;
    ComponentInitializer initializer = nullptr;
    Vec<const ComponentDescriptor*> descriptors;
    Vec<const StateDescriptor*> states;
    Vec<const char*> exports;
    Vec<const char*> stateKinds;

    bool HasExport(const char* name) const;
};

class FrozenComponentRegistry {
  public:
    FrozenComponentRegistry() = default;
    FrozenComponentRegistry(const FrozenComponentRegistry&) = delete;
    FrozenComponentRegistry& operator=(const FrozenComponentRegistry&) = delete;
    ~FrozenComponentRegistry();

    // Null for the empty catalog the bare runtime ships, which declares no
    // module at all rather than an empty one.
    const char* ModuleSpecifier() const { return moduleSpecifier; }
    ComponentInitializer Initializer() const { return initializer; }
    int DescriptorCount() const { return len(descriptors); }
    const ComponentDescriptor* Descriptor(uint32_t id) const;
    int StateCount() const { return len(states); }
    const StateDescriptor* State(int at) const;
    const StateDescriptor* StateOfKind(Str kind) const;
    // The descriptor a node was recorded from, by the name the registry
    // holds; null for a built-in.
    const ComponentDescriptor* Find(Str name, uint32_t* id = nullptr) const;
    const MethodDescriptor* Method(uint32_t component, Str name) const;

    // The ES module a script imports the catalog as. `stateProof` binds the
    // state wrappers to the runtime that minted them.
    Str JavaScriptModuleSource(Str stateProof) const;

  private:
    friend class ComponentRegistry;
    const char* moduleSpecifier = nullptr;
    ComponentInitializer initializer = nullptr;
    Vec<const ComponentDescriptor*> descriptors;
    Vec<const StateDescriptor*> states;
};

// Whether `name` is a JavaScript identifier that is not a reserved word.
bool IsJavaScriptIdentifier(Str name);

// The `declare module "gpui-component"` block of gpui-kit.d.ts, generated
// from the catalog the way typings.rs writes it, and the `Element` union's
// registered half (` | import("gpui-component").XElement` per descriptor).
void AppendComponentDeclarations(StrBuilder* out,
                                 const FrozenComponentRegistry* components,
                                 Str inlineTokenTypes);
void AppendComponentElementUnion(StrBuilder* out,
                                 const FrozenComponentRegistry* components);

// The behaviors a registered component answers only when its descriptor
// declares them. Mirrors the runtime's own gate.
extern const char* const kRegisteredCommonBehaviors[3];

// The slots every element carries, which a registered component reads with
// TakeSlot rather than declaring.
bool IsRegisteredCommonSlot(Str name);

// Element methods whose argument the prelude checks against a fixed
// vocabulary, before the call can reach a registered component.
struct CheckedVocabulary {
    const char* method;
    Slice<const char*> accepted;
};
Slice<CheckedVocabulary> PreludeCheckedVocabularies();

// The retained component states of one runtime: Rust's RetainedStateStore.
constexpr int kMaxRetainedComponentStates = 4096;

class ComponentStateStore {
  public:
    ComponentStateStore() = default;
    ComponentStateStore(const ComponentStateStore&) = delete;
    ComponentStateStore& operator=(const ComponentStateStore&) = delete;
    ~ComponentStateStore();

    int Len() const { return len(entries); }
    // Takes the value; on failure it is destroyed and `error` says why.
    bool Insert(const char* kind, void* owner, void* value,
                void (*destroy)(void*), uint64_t* handle, Str* error, Arena* a);
    const char* Kind(uint64_t handle) const;
    // The value when `handle` is live and of `kind`, else null with `error`.
    void* Get(uint64_t handle, const char* kind, Str* error, Arena* a) const;
    void ReleaseApplication(void* application);
    void Clear();

  private:
    struct Entry {
        uint64_t handle = 0;
        const char* kind = nullptr;
        void* owner = nullptr;
        void* value = nullptr;
        void (*destroy)(void*) = nullptr;
    };
    uint64_t nextHandle = 0;
    Vec<Entry> entries;
};

// ─── What a materializer is handed ────────────────────────────────────────

// ComponentCallback: a script handler an adapter may invoke from a native
// event. The runtime is the ScriptView's; an invocation outside one (a
// released view, a retired render) fails rather than running.
struct ComponentCallback {
    CallbackId id = 0;

    bool IsSet() const { return id != 0; }
    // Invokes with `arguments` then the script's `cx`. `result`, when given,
    // receives what the handler returned (null, boolean, finite number or
    // string). False with `error` when the handler could not run.
    bool InvokeWith(ShellRuntime* runtime, const ComponentDataValue* arguments,
                    int count, Window* window, App* app,
                    ComponentDataValue* result = nullptr, Str* error = nullptr,
                    Arena* a = nullptr) const;
    // Invokes and logs any failure, for event closures that cannot return
    // one.
    void InvokeAndReport(ShellRuntime* runtime, const char* context,
                         const ComponentDataValue* arguments, int count,
                         Window* window, App* app) const;
    // A delegate snapshot: the handler's return as plain data.
    bool SnapshotWith(ShellRuntime* runtime,
                      const ComponentDataValue* arguments, int count, Ctx* cx,
                      ComponentDataValue* out, Arena* a,
                      Str* error = nullptr) const;
    // An element renderer: the element the handler described, materialized
    // into `cx`'s frame. Null when it returned nothing or failed (`error`).
    El* BuildWith(ShellRuntime* runtime, const ComponentDataValue* arguments,
                  int count, Ctx* cx, Str* error = nullptr) const;
    // Like BuildWith, for a subtree whose own handlers (a nested button's
    // on_click) stay live until the next frame replaces it.
    El* BuildInteractiveWith(ShellRuntime* runtime,
                             const ComponentDataValue* arguments, int count,
                             Ctx* cx, Str* error = nullptr) const;
};

// A native event bound to a script callback: what an El listener carries into
// ScriptView::OnComponentEvent. `run` reads the event and invokes
// `callback`, and `user` is whatever it needs, arena-allocated with the frame.
struct ComponentEventBinding;
using ComponentEventRun = void (*)(const ComponentEventBinding* binding,
                                   ScriptView* view, Ctx* cx,
                                   const void* event);
struct ComponentEventBinding {
    ComponentEventRun run = nullptr;
    ComponentCallback callback = {};
    void* user = nullptr;
    intptr_t value = 0;
};

// Listen(cx, &ScriptView::OnComponentEvent, binding) for `cx`'s ScriptView.
Listener ComponentListener(Ctx* cx, ComponentEventRun run,
                           ComponentCallback callback, void* user = nullptr,
                           intptr_t value = 0);

// The same, for a component that supplies the value itself when the event
// happens — the star a rating click lands on — and hands it over with
// ListenerFill. Rust's closure captures the callback and receives the value
// beside it; a Listener carries one intptr_t, so the callback waits in a
// keyed relay named `key` (unique among its siblings, like an element id)
// and the listener is left for the component to fill. `run` sees the filled
// value as `binding->value`.
Listener ComponentValueListener(Ctx* cx, Str key, ComponentEventRun run,
                                ComponentCallback callback,
                                void* user = nullptr);

// A typed child description a parent may materialize exactly once.
struct ComponentChild {
    SpecId id = 0;
    // The registered component it was recorded as, or null for a built-in.
    const char* componentName = nullptr;
    const ComponentPayload* payload = nullptr;
    bool consumed = false;
};

// A deferred slot: built only when the adapter asks for it, within the frame.
struct ComponentElementFactory {
    SpecId id = 0;
    bool IsSet() const { return id != 0 || set; }
    bool set = false;
};

// One recorded method, in script order.
struct RecordedComponentMethod {
    Str name;
    ComponentPayload payload;
};

struct MaterializeRequest {
    Ctx* cx = nullptr;
    ShellRuntime* runtime = nullptr;
    const SpecArena* specs = nullptr;
    const SpecNode* node = nullptr;
    SpecId id = 0;
    const ComponentDescriptor* descriptor = nullptr;
    ComponentPayload payload = {};
    // The script's `key` for this node, or its address in the description.
    Str elementId;
    bool disabled = false;
    bool selected = false;
    CallbackId onClick = 0;
    ShellError* error = nullptr;

    const char* ComponentName() const {
        return descriptor ? descriptor->name : "";
    }
    template <class T>
    const T* PayloadAs() const {
        return shell::PayloadAs<T>(payload);
    }

    // The methods recorded on this node, in script order.
    int MethodCount() const;
    RecordedComponentMethod Method(int at) const;

    // Whether the script styled this node at all.
    bool HasStyle() const;
    // Applies this node's style to `target` exactly once: Rust's
    // `take_style` refined into the component's own style.
    El* ApplyStyle(El* target);
    // Rust's `take_style` handed to a component rather than applied here:
    // this node's style as a refinement the component applies to the element
    // it refines, whenever it renders — including when a typed parent renders
    // it. Counts as taking the style. Valid for the frame.
    ElRefiner TakeStyle();

    int ChildrenLen() const;
    // The ordinary children, materialized in order. Exclusive with
    // TakeTypedChildren.
    bool TakeChildren(El*** out, int* count);
    // Appends every ordinary child to `parent`.
    bool AppendChildren(El* parent);
    bool TakeTypedChildren(ComponentChild** out, int* count);
    El* MaterializeChild(ComponentChild* child);

    // A named slot, materialized, or null when the script wrote none.
    El* TakeSlot(const char* name);
    // Every slot named `name`, in script order.
    int TakeSlots(const char* name, El*** out);
    ComponentElementFactory TakeSlotFactory(const char* name);
    El* BuildFactory(ComponentElementFactory factory);

    // Applies style and ordinary children to `element` and finishes.
    El* Finish(El* element);

    El* ResolveElement(const ComponentArgument& argument);
    ComponentCallback ResolveCallback(const ComponentArgument& argument) const;
    // Adapter-owned retained state named by an Entity argument.
    void* State(const ComponentArgument& argument, const char* kind);
    template <class T>
    T* StateAs(const ComponentArgument& argument, const char* kind) {
        return (T*)State(argument, kind);
    }
    ComponentCallback OnClick() const { return {onClick}; }

    // Records a failure the dispatcher reports; always null so a
    // materializer can `return request->Fail(...)`.
    El* Fail(Str message);

    // Bookkeeping the dispatcher reads for its unread-parts warnings.
    bool styleTaken = false;
    uint8_t lane = 0;
    int childrenTaken = 0;
    ComponentChild* typed = nullptr;
    int typedCount = 0;
    // One flag per op of `node`, set when that Slot op was taken.
    bool* slotTaken = nullptr;
    Str failure;
};

} // namespace gpui::shell
#endif // GPUI_SHELL_COMPONENT_REGISTRY_H_
