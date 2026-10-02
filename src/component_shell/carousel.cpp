// crates/component-shell/src/shell/carousel.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "component_shell/typed_compound/mod.h"
#include "ui/carousel.h"

#include <math.h>

namespace gpui::component_shell::carousel {

using component::CarouselEvent;
using component::CarouselState;
using typed_compound::FinishPart;
using typed_compound::FinishTypedChildren;

// Payload: one tagged struct for Rust's seven-variant enum.
struct Payload {
    enum Kind : uint8_t {
        Root,
        Content,
        Item,
        Previous,
        Next,
        Pagination,
        PaginationItem,
    } kind = Root;
    Str id = {};
    int index = 0;
    ComponentArgument state = {};
};

struct Op {
    enum Kind : uint8_t {
        AccessibilityLabel,
        FocusRing,
        Size,
        SelectedIndex,
        ItemCount,
        OnChange,
    } kind = AccessibilityLabel;
    Str text = {};
    bool flag = false;
    UiSize size = UiSize::Medium;
    // Rust's usize; the C++ state counts in int, so a larger value is
    // clamped to the largest int, which no item count reaches either.
    int count = 0;
    ComponentArgument callback = {};
};

// ChangeHost: the keyed entity that holds the subscription to the state's
// CarouselEvent and the script's latest on_change callback.
struct ChangeHost {
    ShellRuntime* runtime = nullptr;
    shell::ComponentCallback callback = {};
    bool subscribed = false;

    static void OnChange(ChangeHost* self, Ctx* cx,
                         const CarouselEvent* event) {
        if (!self || !event || !self->callback.IsSet()) return;
        shell::ComponentDataValue index =
            shell::ComponentDataValue::Number((double)event->index);
        self->callback.InvokeAndReport(self->runtime,
                                       "Carousel.on_change callback failed",
                                       &index, 1, cx->win, cx->app);
    }
};

static int ClampCount(double value) {
    return value > 2147483647.0 ? 2147483647 : (int)value;
}

// nonnegative_usize: a finite whole number from zero to below 2^64 (Rust's
// `usize::MAX as f64` rounds up to 2^64 on 64-bit targets).
static bool NonnegativeUsize(const ComponentArgument& argument, double* out) {
    if (argument.kind != shell::ComponentArgumentKind::Number) return false;
    double value = argument.number;
    if (!isfinite(value) || value < 0. || value != floor(value) ||
        value >= 18446744073709551616.0)
        return false;
    *out = value;
    return true;
}

static bool NonemptyId(const ComponentArgument& argument) {
    return argument.kind == shell::ComponentArgumentKind::String &&
           len(StrTrim(argument.string)) != 0;
}

// ─── Materializer ─────────────────────────────────────────────────────────

static Entity<CarouselState> StateOf(MaterializeRequest* request,
                                     const ComponentArgument& argument) {
    EntityState<CarouselState>* state =
        request->StateAs<EntityState<CarouselState>>(argument, "CarouselState");
    return state ? state->entity : Entity<CarouselState>{};
}

// BoundCarousel::render: a script that passes `item_count` or
// `selected_index` owns that value, so every frame reasserts it. Both
// setters run only when the value differs, which keeps a gesture in progress
// from being cancelled and cannot loop through `on_change`. Here this runs
// before the parts are built, where Rust's runs before they render.
static void Reassert(MaterializeRequest* request, Entity<CarouselState> handle,
                     bool hasCount, int count, bool hasIndex, int index) {
    Ctx stateCx = *request->cx;
    stateCx.self = handle.id;
    CarouselState* state = handle.Get(request->cx);
    if (!state) return;
    if (hasCount && state->ItemCount() != count)
        state->SetItemCount(count, &stateCx);
    if (hasIndex && state->ItemCount() > 0 && state->SelectedIndex() != index)
        state->SetSelectedIndex(index, &stateCx);
}

static El* MaterializeRoot(MaterializeRequest* request, const Payload* payload,
                           Entity<CarouselState> state) {
    Ctx* cx = request->cx;
    // The last on_change, selected_index and item_count win.
    const ComponentArgument* onChange = nullptr;
    bool hasIndex = false, hasCount = false;
    int index = 0, count = 0;
    EachMethod<Op>(request, [&](const Op& op) {
        if (op.kind == Op::OnChange) onChange = &op.callback;
        if (op.kind == Op::SelectedIndex) {
            hasIndex = true;
            index = op.count;
        }
        if (op.kind == Op::ItemCount) {
            hasCount = true;
            count = op.count;
        }
    });
    shell::ComponentCallback callback = {};
    if (onChange) {
        callback = request->ResolveCallback(*onChange);
        if (!callback.IsSet())
            return request->Fail(StrL("component argument is not a callback"));
    }
    component::Carousel* carousel =
        component::Carousel::New(cx, payload->id, state);
    EachMethod<Op>(request, [&](const Op& op) {
        if (op.kind == Op::AccessibilityLabel)
            carousel->AccessibilityLabel(op.text);
        else if (op.kind == Op::FocusRing)
            carousel->FocusRing(op.flag);
    });
    Reassert(request, state, hasCount, count, hasIndex, index);

    // window.use_keyed_state("shell-carousel:{id}:{entity}", ..): the host
    // subscribes once and takes the latest callback every render.
    Str key = StrDup(cx->a, fmt("shell-carousel:%s:%d:%u", payload->id,
                                state.id.index, state.id.gen));
    Entity<ChangeHost> handle =
        UseKeyedState<ChangeHost>(cx, key, StrL("shell-carousel"));
    if (ChangeHost* host = handle.Get(cx)) {
        if (!host->subscribed) {
            host->subscribed = true;
            SubscribeTo(cx->app, state, handle, &ChangeHost::OnChange);
        }
        host->runtime = request->runtime;
        host->callback = callback;
    }

    static constexpr const char* kAllowed[] = {
        "CarouselContent", "CarouselPrevious", "CarouselNext",
        "CarouselPagination"};
    return FinishTypedChildren(request, carousel, "Carousel", kAllowed);
}

static El* Materialize(MaterializeRequest* request) {
    const Payload* payload = request->PayloadAs<Payload>();
    if (!payload)
        return request
            ->Fail(StrL("Carousel component received an incompatible payload"));
    Ctx* cx = request->cx;
    if (payload->kind == Payload::Pagination) {
        component::CarouselPagination* pagination =
            component::CarouselPagination::New(cx);
        EachMethod<Op>(request, [&](const Op& op) {
            if (op.kind == Op::AccessibilityLabel)
                pagination->AccessibilityLabel(op.text);
        });
        static constexpr const char* kAllowed[] = {"CarouselPaginationItem"};
        return FinishTypedChildren(request, pagination, "CarouselPagination",
                                   kAllowed);
    }
    Entity<CarouselState> state = StateOf(request, payload->state);
    if (!state.IsValid()) return nullptr;
    switch (payload->kind) {
        case Payload::Root:
            return MaterializeRoot(request, payload, state);
        case Payload::Content: {
            static constexpr const char* kAllowed[] = {"CarouselItem"};
            return FinishTypedChildren(
                request, component::CarouselContent::New(cx, state),
                "CarouselContent", kAllowed);
        }
        case Payload::Item: {
            component::CarouselItem* item = component::CarouselItem::New(
                cx, payload->id, payload->index, state);
            EachMethod<Op>(request, [&](const Op& op) {
                if (op.kind == Op::AccessibilityLabel)
                    item->AccessibilityLabel(op.text);
            });
            return FinishPart(request, item);
        }
        case Payload::Previous:
        case Payload::Next: {
            component::CarouselControl* control =
                payload->kind == Payload::Next
                    ? (component::CarouselControl*)component::CarouselNext::New(
                          cx, state)
                    : component::CarouselPrevious::New(cx, state);
            EachMethod<Op>(request, [&](const Op& op) {
                if (op.kind == Op::AccessibilityLabel)
                    control->AccessibilityLabel(op.text);
                else if (op.kind == Op::Size)
                    control->WithSize(op.size);
            });
            if (payload->kind == Payload::Next)
                return FinishPart(request, (component::CarouselNext*)control);
            return FinishPart(request, (component::CarouselPrevious*)control);
        }
        default: {
            component::CarouselPaginationItem* item =
                component::CarouselPaginationItem::New(cx, payload->id,
                                                       payload->index, state);
            EachMethod<Op>(request, [&](const Op& op) {
                if (op.kind == Op::AccessibilityLabel)
                    item->AccessibilityLabel(op.text);
                else if (op.kind == Op::Size)
                    item->WithSize(op.size);
            });
            return FinishPart(request, item);
        }
    }
}

// ─── Constructors ─────────────────────────────────────────────────────────

// entity(argument, component): "{component} expects a CarouselState entity".
static bool RequireEntity(PayloadBuild* build,
                          const ComponentArgument& argument,
                          const char* component) {
    if (argument.kind == shell::ComponentArgumentKind::Entity) return true;
    return build
        ->Fail(fmt("%s expects a CarouselState entity", Str(component)));
}

// state_constructor(component, payload).
template <Payload::Kind K>
static bool StateConstruct(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    const char* component = K == Payload::Content    ? "CarouselContent"
                            : K == Payload::Previous ? "CarouselPrevious"
                                                     : "CarouselNext";
    if (count != 1)
        return build->Fail(
            fmt("%s(state) expects one CarouselState entity", Str(component)));
    if (!RequireEntity(build, args[0], component)) return false;
    Payload* payload = build->New<Payload>();
    payload->kind = K;
    payload->state = args[0];
    return true;
}

// id_state_constructor("Carousel", ..).
static bool RootConstruct(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 2)
        return build
            ->Fail(StrL("Carousel(id, state) expects a nonempty id and "
                        "CarouselState entity"));
    if (!NonemptyId(args[0]))
        return build->Fail(StrL("Carousel expects a nonempty string id"));
    if (!RequireEntity(build, args[1], "Carousel")) return false;
    Payload* payload = build->New<Payload>();
    payload->kind = Payload::Root;
    payload->id = args[0].string;
    payload->state = args[1];
    return true;
}

// indexed_state_constructor(component, payload).
template <Payload::Kind K>
static bool IndexedConstruct(PayloadBuild* build, const ComponentArgument* args,
                             int count) {
    const char* component =
        K == Payload::Item ? "CarouselItem" : "CarouselPaginationItem";
    if (count != 3)
        return build
            ->Fail(fmt("%s(id, index, state) expects an id, "
                       "nonnegative index, and CarouselState",
                       Str(component)));
    if (!NonemptyId(args[0]))
        return build
            ->Fail(fmt("%s expects a nonempty string id", Str(component)));
    double index = 0;
    if (!NonnegativeUsize(args[1], &index))
        return build
            ->Fail(fmt("%s(id, index, state) expects a nonnegative "
                       "integer",
                       Str(component)));
    if (!RequireEntity(build, args[2], component)) return false;
    Payload* payload = build->New<Payload>();
    payload->kind = K;
    payload->id = args[0].string;
    payload->index = ClampCount(index);
    payload->state = args[2];
    return true;
}

static bool PaginationConstruct(PayloadBuild* build, const ComponentArgument*,
                                int) {
    build->New<Payload>()->kind = Payload::Pagination;
    return true;
}

// ─── Methods ──────────────────────────────────────────────────────────────

// The component a method's error message names. The method tables are
// shared by name; each gets its own recorder instance per component.
enum class Part : uint8_t {
    Carousel,
    Item,
    Previous,
    Next,
    Pagination,
    PaginationItem,
};

static const char* PartName(Part part) {
    switch (part) {
        case Part::Carousel:
            return "Carousel";
        case Part::Item:
            return "CarouselItem";
        case Part::Previous:
            return "CarouselPrevious";
        case Part::Next:
            return "CarouselNext";
        case Part::Pagination:
            return "CarouselPagination";
        default:
            return "CarouselPaginationItem";
    }
}

// accessibility_label_method(component): string_method(..).
template <Part P>
static bool RecordAccessibilityLabel(PayloadBuild* build,
                                     const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build->Fail(
            fmt("%s.accessibility_label expects one string", Str(PartName(P))));
    Op* op = build->New<Op>();
    op->kind = Op::AccessibilityLabel;
    op->text = args[0].string;
    return true;
}

// bool_method("Carousel", "focus_ring", ..).
static bool RecordFocusRing(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(StrL("Carousel.focus_ring expects one boolean"));
    Op* op = build->New<Op>();
    op->kind = Op::FocusRing;
    op->flag = args[0].boolean;
    return true;
}

// usize_method("Carousel", name, ..).
template <Op::Kind K>
static bool RecordUsize(PayloadBuild* build, const ComponentArgument* args,
                        int count) {
    const char* name = K == Op::ItemCount ? "item_count" : "selected_index";
    if (count != 1)
        return build
            ->Fail(fmt("Carousel.%s(value) expects one number", Str(name)));
    double value = 0;
    if (!NonnegativeUsize(args[0], &value))
        return build
            ->Fail(fmt("Carousel.%s(value) expects a nonnegative "
                       "integer",
                       Str(name)));
    Op* op = build->New<Op>();
    op->kind = K;
    op->count = ClampCount(value);
    return true;
}

static bool RecordOnChange(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Callback)
        return build
            ->Fail(StrL("Carousel.on_change(callback) expects one callback"));
    Op* op = build->New<Op>();
    op->kind = Op::OnChange;
    op->callback = args[0];
    return true;
}

// size_method(component).
template <Part P>
static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build->Fail(
            fmt("%s.size(size) expects a semantic size", Str(PartName(P))));
    Op* op = build->New<Op>();
    op->kind = Op::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

// ─── CarouselState ────────────────────────────────────────────────────────

static bool NewState(shell::StateBuild* build, const ComponentArgument* args,
                     int count) {
    if (count != 4)
        return build->Fail(
            StrL("CarouselState expects item_count, selected_index, axis, and "
                 "looping"));
    double itemCount = 0;
    if (!NonnegativeUsize(args[0], &itemCount))
        return build->Fail(
            StrL("CarouselState(item_count) expects a nonnegative integer"));
    if (args[1].kind != shell::ComponentArgumentKind::Optional)
        return build
            ->Fail(StrL("CarouselState selected_index must be optional"));
    bool hasIndex = false;
    double index = 0;
    if (const ComponentArgument* some = args[1].Some()) {
        if (!NonnegativeUsize(*some, &index))
            return build->Fail(
                StrL("CarouselState selected_index expects a nonnegative "
                     "integer"));
        hasIndex = true;
    }
    if (hasIndex && index >= itemCount)
        return build->Fail(
            StrL("CarouselState selected_index must be within item_count"));
    if (args[2].kind != shell::ComponentArgumentKind::Optional)
        return build->Fail(StrL("CarouselState axis must be optional"));
    Axis axis = Axis::Horizontal;
    if (const ComponentArgument* some = args[2].Some()) {
        if (some->kind == shell::ComponentArgumentKind::Enum &&
            StrEq(some->string, "horizontal"))
            axis = Axis::Horizontal;
        else if (some->kind == shell::ComponentArgumentKind::Enum &&
                 StrEq(some->string, "vertical"))
            axis = Axis::Vertical;
        else
            return build->Fail(
                StrL("CarouselState axis expects horizontal or vertical"));
    }
    if (args[3].kind != shell::ComponentArgumentKind::Optional)
        return build->Fail(StrL("CarouselState looping must be optional"));
    bool looping = false;
    if (const ComponentArgument* some = args[3].Some()) {
        if (some->kind != shell::ComponentArgumentKind::Boolean)
            return build->Fail(StrL("CarouselState looping expects a boolean"));
        looping = some->boolean;
    }
    EntityState<CarouselState>* state = build
                                            ->New<EntityState<CarouselState>>();
    state->app = build->app;
    state->entity =
        component::CarouselStateNew(build->app, ClampCount(itemCount));
    if (CarouselState* native = state->entity.Get(build->app)) {
        native->WithAxis(axis).WithLooping(looping);
        if (hasIndex) native->WithSelectedIndex(ClampCount(index));
    }
    return true;
}

// ─── Tables ───────────────────────────────────────────────────────────────

static constexpr ArgumentSchema kNumberSchema = SchemaNumber();
static constexpr const char* kAxisLiterals[] = {"horizontal", "vertical"};
static constexpr ArgumentSchema kAxisSchema = SchemaEnum(kAxisLiterals);
static constexpr ArgumentSchema kBooleanSchema = SchemaBoolean();
static constexpr ArgumentDescriptor kStateArgs[] = {
    {"item_count", SchemaNumber()},
    {"selected_index", SchemaOptional(&kNumberSchema)},
    {"axis", SchemaOptional(&kAxisSchema)},
    {"looping", SchemaOptional(&kBooleanSchema)},
};
static constexpr shell::StateDescriptor kCarouselState = {
    "CarouselState",
    "CarouselState",
    kStateArgs,
    "Retained Carousel selection, axis, looping, and interaction state.",
    &NewState,
    {}};

static constexpr ArgumentDescriptor kStateOnlyArgs[] = {
    {"state", SchemaEntity("CarouselState")}};
static constexpr ArgumentDescriptor kIdStateArgs[] = {
    {"id", SchemaString()},
    {"state", SchemaEntity("CarouselState")}};
static constexpr ArgumentDescriptor kIndexedArgs[] = {
    {"id", SchemaString()},
    {"index", SchemaNumber()},
    {"state", SchemaEntity("CarouselState")}};

static constexpr ArgumentDescriptor kLabelArgs[] = {
    {"accessibility_label", SchemaString()}};
static constexpr ArgumentDescriptor kFocusRingArgs[] = {
    {"focus_ring", SchemaBoolean()}};
static constexpr ArgumentDescriptor kValueArgs[] = {{"value", SchemaNumber()}};
static constexpr ArgumentDescriptor kOnChangeArgs[] = {
    {"callback", SchemaCallback("(index: number, cx: Context) => void")}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};

static constexpr const char* kLabelDoc =
    "Sets the name announced by accessibility clients.";
static constexpr const char* kSizeDoc = "Sets the semantic control size.";

static constexpr MethodDescriptor kCarouselMethods[] = {
    {"accessibility_label", kLabelArgs, kLabelDoc,
     &RecordAccessibilityLabel<Part::Carousel>},
    {"focus_ring", kFocusRingArgs,
     "Controls whether keyboard focus draws a focus ring.", &RecordFocusRing},
    {"item_count", kValueArgs,
     "Sets the number of logical items the state tracks.",
     &RecordUsize<Op::ItemCount>},
    {"selected_index", kValueArgs,
     "Selects an item without emitting a change event, for a script that "
     "owns the selection.",
     &RecordUsize<Op::SelectedIndex>},
    {"on_change", kOnChangeArgs,
     "Reports the newly selected zero-based item index.", &RecordOnChange},
};
static constexpr MethodDescriptor kItemMethods[] = {
    {"accessibility_label", kLabelArgs, kLabelDoc,
     &RecordAccessibilityLabel<Part::Item>},
};
static constexpr MethodDescriptor kPreviousMethods[] = {
    {"accessibility_label", kLabelArgs, kLabelDoc,
     &RecordAccessibilityLabel<Part::Previous>},
    {"size", kSizeArgs, kSizeDoc, &RecordSize<Part::Previous>},
};
static constexpr MethodDescriptor kNextMethods[] = {
    {"accessibility_label", kLabelArgs, kLabelDoc,
     &RecordAccessibilityLabel<Part::Next>},
    {"size", kSizeArgs, kSizeDoc, &RecordSize<Part::Next>},
};
static constexpr MethodDescriptor kPaginationMethods[] = {
    {"accessibility_label", kLabelArgs, kLabelDoc,
     &RecordAccessibilityLabel<Part::Pagination>},
};
static constexpr MethodDescriptor kPaginationItemMethods[] = {
    {"accessibility_label", kLabelArgs, kLabelDoc,
     &RecordAccessibilityLabel<Part::PaginationItem>},
    {"size", kSizeArgs, kSizeDoc, &RecordSize<Part::PaginationItem>},
};

static constexpr ConstructorDescriptor kCarouselConstructors[] = {
    {"Carousel", kIdStateArgs, &RootConstruct}};
static constexpr ConstructorDescriptor kContentConstructors[] = {
    {"CarouselContent", kStateOnlyArgs, &StateConstruct<Payload::Content>}};
static constexpr ConstructorDescriptor kItemConstructors[] = {
    {"CarouselItem", kIndexedArgs, &IndexedConstruct<Payload::Item>}};
static constexpr ConstructorDescriptor kPreviousConstructors[] = {
    {"CarouselPrevious", kStateOnlyArgs, &StateConstruct<Payload::Previous>}};
static constexpr ConstructorDescriptor kNextConstructors[] = {
    {"CarouselNext", kStateOnlyArgs, &StateConstruct<Payload::Next>}};
static constexpr ConstructorDescriptor kPaginationConstructors[] = {
    {"CarouselPagination", {}, &PaginationConstruct}};
static constexpr ConstructorDescriptor kPaginationItemConstructors[] = {
    {"CarouselPaginationItem", kIndexedArgs,
     &IndexedConstruct<Payload::PaginationItem>}};

static constexpr ComponentDescriptor kDescriptors[] = {
    {"Carousel", kCarouselConstructors, kCarouselMethods,
     "A retained snapping viewport composed from Carousel parts.",
     &Materialize},
    {"CarouselContent",
     kContentConstructors,
     {},
     "The clipped Carousel viewport; accepts only CarouselItem children.",
     &Materialize},
    {"CarouselItem", kItemConstructors, kItemMethods,
     "One indexed Carousel slide that accepts ordinary content children.",
     &Materialize},
    {"CarouselPrevious", kPreviousConstructors, kPreviousMethods,
     "The previous-item control for a CarouselState.", &Materialize},
    {"CarouselNext", kNextConstructors, kNextMethods,
     "The next-item control for a CarouselState.", &Materialize},
    {"CarouselPagination", kPaginationConstructors, kPaginationMethods,
     "A container that accepts only CarouselPaginationItem children.",
     &Materialize},
    {"CarouselPaginationItem", kPaginationItemConstructors,
     kPaginationItemMethods, "One indexed Carousel pagination control.",
     &Materialize},
};

} // namespace gpui::component_shell::carousel

namespace gpui::component_shell {

bool CarouselNonnegativeUsize(const shell::ComponentArgument& argument,
                              double* out) {
    return carousel::NonnegativeUsize(argument, out);
}
bool CarouselNonemptyId(const shell::ComponentArgument& argument) {
    return carousel::NonemptyId(argument);
}

bool RegisterCarousel(shell::ComponentRegistry* registry,
                      shell::RegistryError* error) {
    if (!registry->RegisterState(&carousel::kCarouselState, error))
        return false;
    for (const shell::ComponentDescriptor& descriptor :
         carousel::kDescriptors) {
        if (!registry->Register(&descriptor, error)) return false;
    }
    return true;
}

} // namespace gpui::component_shell
