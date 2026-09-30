// crates/component-shell/src/shell/questionnaire/mod.rs
//
// Questionnaire: typed question and choice data plus the retained flow.
//
// The schema a `QuestionnaireState` is built from is a tree of plain values,
// and a state factory cannot take elements, so the questions arrive as typed
// children the way `Tree`'s items do. The root builds the state, keeps it
// across frames, and renders the default composition: progress, the active
// question with its title, description, answers and error, then the
// navigation actions.

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "shell/input_tokens.h"
#include "ui/questionnaire.h"

namespace gpui::component_shell::questionnaire {

struct ChoicePayload {
    Str value = {};
    Str label = {};
};

struct ChoiceOp {
    enum Kind : uint8_t {
        Description,
        Disabled,
        DefaultSelected,
    } kind = Description;
    Str text = {};
    bool flag = false;
};

struct ItemPayload {
    Str name = {};
    Str label = {};
};

struct ItemOp {
    enum Kind : uint8_t {
        Description,
        Required,
        Multiple,
        Disabled,
    } kind = Description;
    Str text = {};
    bool flag = false;
};

struct InputPayload {
    ComponentArgument state = {};
    Str label = {};
};

// QuestionnaireInput's one method records a bare bool, as Rust's does.
struct InputDisabled {
    bool value = false;
};

struct RootPayload {
    Str id = {};
};

struct RootOp {
    QuestionnaireShortcutMode mode = QuestionnaireShortcutMode::Letters;
};

// What a QuestionnaireItem carries to its root. Rust's carries a
// QuestionnaireItemDefinition; the C++ one owns a heap Vec of choices, which
// a frame-arena carrier would never free, so the frame carries the choices
// in an ArenaVec and the root builds the definitions only to hand them to
// QuestionnaireStateNew.
struct CarriedItem {
    Str name = {};
    Str label = {};
    Str description = {};
    bool required = false;
    bool multiple = false;
    bool disabled = false;
    ArenaVec<QuestionnaireChoiceDefinition> choices;
    QuestionnaireInputDefinition input = {};
    bool hasInput = false;
};

struct CarriedChoice {
    QuestionnaireChoiceDefinition definition = {};
};
struct CarriedInput {
    QuestionnaireInputDefinition definition = {};
};

// RetainedQuestionnaire: the state, the fingerprint of the schema it was
// built from, and a schema the state refused.
struct Retained {
    App* app = nullptr;
    Entity<QuestionnaireState> native = {};
    uint64_t fingerprint = 0;
    bool built = false;
    // A schema the state refused. The flow renders nothing and the script
    // hears why, instead of a questionnaire that silently lost a question.
    Str error = {};

    void Clear() {
        if (app && native.IsValid()) EntityDrop(app, native.id);
        native = {};
        StrFree(error);
        error = {};
    }
    ~Retained() { Clear(); }
};

// ─── Materializers ────────────────────────────────────────────────────────

static El* MaterializeChoice(MaterializeRequest* request) {
    const ChoicePayload* payload = request->PayloadAs<ChoicePayload>();
    if (!payload)
        return request->Fail(
            StrL("QuestionnaireChoice received an incompatible payload"));
    QuestionnaireChoiceDefinition choice =
        QuestionnaireChoiceDefinition::New(payload->value, payload->label);
    EachMethod<ChoiceOp>(request, [&](const ChoiceOp& op) {
        switch (op.kind) {
            case ChoiceOp::Description:
                choice = choice.WithDescription(op.text);
                break;
            case ChoiceOp::Disabled:
                choice = choice.WithDisabled(op.flag);
                break;
            case ChoiceOp::DefaultSelected:
                choice = choice.WithDefaultSelected(op.flag);
                break;
        }
    });
    if (!RejectStyle(request, "QuestionnaireChoice")) return nullptr;
    CarriedChoice* carried = ArenaNew<CarriedChoice>(request->cx->a);
    carried->definition = choice;
    return CarrierOf(request->cx, carried);
}

static El* MaterializeInput(MaterializeRequest* request) {
    const InputPayload* payload = request->PayloadAs<InputPayload>();
    if (!payload)
        return request
            ->Fail(StrL("QuestionnaireInput received an incompatible payload"));
    shell::TextStateEntity* state =
        request->StateAs<shell::TextStateEntity>(payload->state, "InputState");
    if (!state) return nullptr;
    QuestionnaireInputDefinition input =
        QuestionnaireInputDefinition::New(&state->input, payload->label);
    EachMethod<InputDisabled>(request, [&](const InputDisabled& op) {
        input = input.WithDisabled(op.value);
    });
    if (!RejectStyle(request, "QuestionnaireInput")) return nullptr;
    CarriedInput* carried = ArenaNew<CarriedInput>(request->cx->a);
    carried->definition = input;
    return CarrierOf(request->cx, carried);
}

static El* MaterializeItem(MaterializeRequest* request) {
    const ItemPayload* payload = request->PayloadAs<ItemPayload>();
    if (!payload)
        return request
            ->Fail(StrL("QuestionnaireItem received an incompatible payload"));
    Arena* a = request->cx->a;
    CarriedItem* item = ArenaNew<CarriedItem>(a);
    item->name = payload->name;
    item->label = payload->label;
    EachMethod<ItemOp>(request, [&](const ItemOp& op) {
        switch (op.kind) {
            case ItemOp::Description:
                item->description = op.text;
                break;
            case ItemOp::Required:
                item->required = op.flag;
                break;
            case ItemOp::Multiple:
                item->multiple = op.flag;
                break;
            case ItemOp::Disabled:
                item->disabled = op.flag;
                break;
        }
    });
    if (!RejectStyle(request, "QuestionnaireItem")) return nullptr;
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    static constexpr const char* kAllowed[] = {"QuestionnaireChoice",
                                               "QuestionnaireInput"};
    for (int i = 0; i < count; i++) {
        const char* name = children[i].componentName;
        if (!RequireChild(request, "QuestionnaireItem", name, kAllowed))
            return nullptr;
        bool freeform = name && strcmp(name, "QuestionnaireInput") == 0;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        if (freeform) {
            CarriedInput* input = TakeCarriedAs<CarriedInput>(
                request, element, "QuestionnaireInput");
            if (!input) return nullptr;
            // with_input: a later input replaces an earlier one.
            item->input = input->definition;
            item->hasInput = true;
        } else {
            CarriedChoice* choice = TakeCarriedAs<CarriedChoice>(
                request, element, "QuestionnaireChoice");
            if (!choice) return nullptr;
            item->choices.Append(a, choice->definition);
        }
    }
    return CarrierOf(request->cx, item);
}

// fingerprint: FNV-1a over the schema in place, each string length-prefixed
// so neighbouring fields cannot run into each other. Rust hashes the same
// fields with the std DefaultHasher; the value is only ever compared with
// the last one this process computed.
struct Hasher {
    uint64_t h = 1469598103934665603ull;

    void Bytes(const void* data, size_t n) {
        const uint8_t* p = (const uint8_t*)data;
        for (size_t i = 0; i < n; i++) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
    }
    void U64(uint64_t v) { Bytes(&v, sizeof(v)); }
    void Text(Str s) {
        U64((uint64_t)len(s));
        if (len(s) > 0) Bytes(s.s, (size_t)len(s));
    }
    // An Option<&str>: the description is None when empty.
    void OptionalText(Str s) {
        U64(len(s) > 0 ? 1 : 0);
        if (len(s) > 0) Text(s);
    }
};

static uint64_t Fingerprint(CarriedItem* const* items, int n, bool hasMode,
                            QuestionnaireShortcutMode mode) {
    Hasher hasher;
    hasher.U64(!hasMode                                     ? 0
               : mode == QuestionnaireShortcutMode::Letters ? 1
                                                            : 2);
    for (int i = 0; i < n; i++) {
        const CarriedItem* item = items[i];
        hasher.Text(item->name);
        hasher.Text(item->label);
        hasher.OptionalText(item->description);
        hasher.U64((uint64_t)item->required | (uint64_t)item->multiple << 1 |
                   (uint64_t)item->disabled << 2);
        hasher.U64((uint64_t)len(item->choices));
        if (item->hasInput) {
            // The InputState's identity, where Rust hashes its entity id.
            hasher.U64((uint64_t)(uintptr_t)item->input.state);
            hasher.Text(item->input.accessibilityLabel);
            hasher.U64(item->input.disabled);
        }
        for (const QuestionnaireChoiceDefinition& choice : item->choices) {
            hasher.Text(choice.value);
            hasher.Text(choice.accessibilityLabel);
            hasher.OptionalText(choice.description);
            hasher.U64((uint64_t)choice.disabled |
                       (uint64_t)choice.defaultSelected << 1);
        }
    }
    return hasher.h;
}

// build_retained: a fresh QuestionnaireState from the carried schema, or the
// reason it refused it.
static void BuildRetained(App* app, Retained* retained,
                          CarriedItem* const* items, int n, bool hasMode,
                          QuestionnaireShortcutMode mode,
                          uint64_t fingerprint) {
    retained->Clear();
    retained->app = app;
    retained->fingerprint = fingerprint;
    retained->built = true;
    QuestionnaireItemDefinition* definitions =
        n > 0 ? new QuestionnaireItemDefinition[n] : nullptr;
    for (int i = 0; i < n; i++) {
        const CarriedItem* item = items[i];
        QuestionnaireItemDefinition& d = definitions[i];
        d.name = item->name;
        d.accessibilityLabel = item->label;
        d.description = item->description;
        d.required = item->required;
        d.multiple = item->multiple;
        d.disabled = item->disabled;
        for (const QuestionnaireChoiceDefinition& choice : item->choices)
            VecAppend(d.choices, choice);
        d.input = item->input;
        d.hasInput = item->hasInput;
    }
    Entity<QuestionnaireState> native = {};
    QuestionnaireSchemaError error =
        QuestionnaireStateNew(app, definitions, n, &native);
    delete[] definitions;
    if (error.IsError()) {
        // Rust keeps an empty state beside the error; nothing reads it, so
        // the port keeps none.
        retained
            ->error = StrDup(fmt("Questionnaire schema is invalid: %s",
                                 QuestionnaireSchemaErrorMessageTemp(error)));
        return;
    }
    retained->native = native;
    if (hasMode) {
        if (QuestionnaireState* state = native.Get(app))
            state->WithShortcuts(mode);
    }
}

static El* MaterializeRoot(MaterializeRequest* request) {
    const RootPayload* payload = request->PayloadAs<RootPayload>();
    if (!payload)
        return request
            ->Fail(StrL("Questionnaire received an incompatible payload"));
    Ctx* cx = request->cx;
    Arena* a = cx->a;
    bool hasMode = false;
    QuestionnaireShortcutMode mode = QuestionnaireShortcutMode::Letters;
    EachMethod<RootOp>(request, [&](const RootOp& op) {
        hasMode = true;
        mode = op.mode;
    });

    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    CarriedItem** items = (CarriedItem**)Alloc(
        a, (int)sizeof(CarriedItem*) * (count ? count : 1));
    static constexpr const char* kAllowed[] = {"QuestionnaireItem"};
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "Questionnaire", children[i].componentName,
                          kAllowed))
            return nullptr;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        items[i] =
            TakeCarriedAs<CarriedItem>(request, element, "QuestionnaireItem");
        if (!items[i]) return nullptr;
    }

    uint64_t fingerprint = Fingerprint(items, count, hasMode, mode);
    Str key = StrDup(a, fmt("shell-questionnaire:%s", payload->id));
    Retained* retained =
        UseKeyedState<Retained>(cx, key, StrL("shell-questionnaire")).Get(cx);
    if (!retained) return Div(a);
    if (!retained->built || retained->fingerprint != fingerprint)
        BuildRetained(cx->app, retained, items, count, hasMode, mode,
                      fingerprint);
    if (len(retained->error)) return request->Fail(StrDup(a, retained->error));
    Entity<QuestionnaireState> state = retained->native;

    component::Questionnaire* questionnaire =
        component::Questionnaire::New(cx, state);
    questionnaire
        ->Child(component::QuestionnaireProgress::New(cx, state)->IntoEl());
    for (int i = 0; i < count; i++) {
        Str name = items[i]->name;
        component::QuestionnaireChoices* answers =
            component::QuestionnaireChoices::New(cx, state, name);
        for (const QuestionnaireChoiceDefinition& choice : items[i]->choices) {
            answers->Child(component::QuestionnaireChoice::New(cx, state, name,
                                                               choice.value)
                               ->IntoEl());
        }
        answers->Child(component::QuestionnaireInput::New(cx, state, name)
                           ->IntoEl());
        questionnaire->Child(
            component::QuestionnaireItem::New(cx, state, name)
                ->Child(component::QuestionnaireTitle::New(cx, state, name)
                            ->IntoEl())
                ->Child(
                    component::QuestionnaireDescription::New(cx, state, name)
                        ->IntoEl())
                ->Child(answers->IntoEl())
                ->Child(component::QuestionnaireError::New(cx, state, name)
                            ->IntoEl())
                ->IntoEl());
    }
    questionnaire->Child(
        component::QuestionnaireActions::New(cx, state)
            ->Child(component::QuestionnairePrevious::New(cx, state)->IntoEl())
            ->Child(component::QuestionnaireSkip::New(cx, state)->IntoEl())
            ->Child(component::QuestionnaireNext::New(cx, state)->IntoEl())
            ->Child(component::QuestionnaireSubmit::New(cx, state)->IntoEl())
            ->IntoEl());
    questionnaire->refiner = request->TakeStyle();
    return questionnaire->IntoEl();
}

// ─── Recorders ────────────────────────────────────────────────────────────

static bool NonBlank(const ComponentArgument& argument) {
    return argument.kind == shell::ComponentArgumentKind::String &&
           len(StrTrimAscii(argument.string)) != 0;
}

static bool ConstructChoice(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count != 2 || !NonBlank(args[0]) || !NonBlank(args[1]))
        return build->Fail(
            StrL("QuestionnaireChoice expects a non-empty value and label"));
    ChoicePayload* payload = build->New<ChoicePayload>();
    payload->value = args[0].string;
    payload->label = args[1].string;
    return true;
}

static bool ConstructItem(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 2 || !NonBlank(args[0]) || !NonBlank(args[1]))
        return build->Fail(
            StrL("QuestionnaireItem expects a non-empty name and label"));
    ItemPayload* payload = build->New<ItemPayload>();
    payload->name = args[0].string;
    payload->label = args[1].string;
    return true;
}

static bool ConstructInput(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    if (count != 2 || args[0].kind != shell::ComponentArgumentKind::Entity ||
        !NonBlank(args[1]))
        return build
            ->Fail(StrL("QuestionnaireInput expects an InputState entity and a "
                        "non-empty label"));
    InputPayload* payload = build->New<InputPayload>();
    payload->state = args[0];
    payload->label = args[1].string;
    return true;
}

static bool ConstructRoot(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 1 || !NonBlank(args[0]))
        return build->Fail(StrL("Questionnaire expects a non-empty id"));
    build->New<RootPayload>()->id = args[0].string;
    return true;
}

// string_method("QuestionnaireChoice" / "QuestionnaireItem", "description").
template <bool Item>
static bool RecordDescription(PayloadBuild* build,
                              const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build->Fail(
            Item ? StrL("QuestionnaireItem.description expects one string")
                 : StrL("QuestionnaireChoice.description expects one string"));
    if (Item) {
        ItemOp* op = build->New<ItemOp>();
        op->kind = ItemOp::Description;
        op->text = args[0].string;
    } else {
        ChoiceOp* op = build->New<ChoiceOp>();
        op->kind = ChoiceOp::Description;
        op->text = args[0].string;
    }
    return true;
}

// bool_method("QuestionnaireChoice", name, ..).
template <ChoiceOp::Kind K>
static bool RecordChoiceBool(PayloadBuild* build, const ComponentArgument* args,
                             int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(fmt("QuestionnaireChoice.%s expects one boolean",
                               K == ChoiceOp::Disabled ? StrL("disabled")
                                                       : StrL("default_"
                                                              "selected")));
    ChoiceOp* op = build->New<ChoiceOp>();
    op->kind = K;
    op->flag = args[0].boolean;
    return true;
}

// bool_method("QuestionnaireItem", name, ..).
template <ItemOp::Kind K>
static bool RecordItemBool(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean) {
        Str name = K == ItemOp::Required   ? StrL("required")
                   : K == ItemOp::Multiple ? StrL("multiple")
                                           : StrL("disabled");
        return build
            ->Fail(fmt("QuestionnaireItem.%s expects one boolean", name));
    }
    ItemOp* op = build->New<ItemOp>();
    op->kind = K;
    op->flag = args[0].boolean;
    return true;
}

// bool_method("QuestionnaireInput", "disabled", .., |value| value).
static bool RecordInputDisabled(PayloadBuild* build,
                                const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build
            ->Fail(StrL("QuestionnaireInput.disabled expects one boolean"));
    build->New<InputDisabled>()->value = args[0].boolean;
    return true;
}

static bool RecordShortcuts(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count == 1 && args[0].kind == shell::ComponentArgumentKind::Enum) {
        if (StrEq(args[0].string, "letters")) {
            build->New<RootOp>()->mode = QuestionnaireShortcutMode::Letters;
            return true;
        }
        if (StrEq(args[0].string, "numbers")) {
            build->New<RootOp>()->mode = QuestionnaireShortcutMode::Numbers;
            return true;
        }
    }
    return build
        ->Fail(StrL("Questionnaire.shortcuts expects letters or numbers"));
}

// ─── Tables ───────────────────────────────────────────────────────────────

static constexpr ArgumentDescriptor kChoiceArgs[] = {{"value", SchemaString()},
                                                     {"label", SchemaString()}};
static constexpr ArgumentDescriptor kItemArgs[] = {{"name", SchemaString()},
                                                   {"label", SchemaString()}};
static constexpr ArgumentDescriptor kInputArgs[] = {
    {"state", SchemaEntity("InputState")},
    {"label", SchemaString()}};
static constexpr ArgumentDescriptor kRootArgs[] = {{"id", SchemaString()}};

static constexpr ArgumentDescriptor kDescriptionArgs[] = {
    {"description", SchemaString()}};
static constexpr ArgumentDescriptor kDisabledArgs[] = {
    {"disabled", SchemaBoolean()}};
static constexpr ArgumentDescriptor kDefaultSelectedArgs[] = {
    {"default_selected", SchemaBoolean()}};
static constexpr ArgumentDescriptor kRequiredArgs[] = {
    {"required", SchemaBoolean()}};
static constexpr ArgumentDescriptor kMultipleArgs[] = {
    {"multiple", SchemaBoolean()}};
static constexpr const char* kModeLiterals[] = {"letters", "numbers"};
static constexpr ArgumentDescriptor kShortcutsArgs[] = {
    {"mode", SchemaEnum(kModeLiterals)}};

static constexpr MethodDescriptor kChoiceMethods[] = {
    {"description", kDescriptionArgs,
     "Adds secondary text under the choice label.", &RecordDescription<false>},
    {"disabled", kDisabledArgs, "Keeps the choice visible but unselectable.",
     &RecordChoiceBool<ChoiceOp::Disabled>},
    {"default_selected", kDefaultSelectedArgs,
     "Selects the choice in the questionnaire's initial snapshot.",
     &RecordChoiceBool<ChoiceOp::DefaultSelected>},
};
static constexpr MethodDescriptor kItemMethods[] = {
    {"description", kDescriptionArgs,
     "Adds supporting text under the question title.",
     &RecordDescription<true>},
    {"required", kRequiredArgs,
     "Requires an answer and hides Skip for this question.",
     &RecordItemBool<ItemOp::Required>},
    {"multiple", kMultipleArgs,
     "Accepts more than one choice for this question.",
     &RecordItemBool<ItemOp::Multiple>},
    {"disabled", kDisabledArgs,
     "Removes the question from progress, navigation and submission.",
     &RecordItemBool<ItemOp::Disabled>},
};
static constexpr MethodDescriptor kInputMethods[] = {
    {"disabled", kDisabledArgs,
     "Keeps the freeform answer visible but not editable.",
     &RecordInputDisabled},
};
static constexpr MethodDescriptor kRootMethods[] = {
    {"shortcuts", kShortcutsArgs,
     "Hands each enabled choice of the active question a letter or number "
     "shortcut.",
     &RecordShortcuts},
};

static constexpr ConstructorDescriptor kChoiceConstructors[] = {
    {"QuestionnaireChoice", kChoiceArgs, &ConstructChoice}};
static constexpr ConstructorDescriptor kItemConstructors[] = {
    {"QuestionnaireItem", kItemArgs, &ConstructItem}};
static constexpr ConstructorDescriptor kInputConstructors[] = {
    {"QuestionnaireInput", kInputArgs, &ConstructInput}};
static constexpr ConstructorDescriptor kRootConstructors[] = {
    {"Questionnaire", kRootArgs, &ConstructRoot}};

static constexpr ComponentDescriptor kDescriptors[] = {
    {"QuestionnaireChoice", kChoiceConstructors, kChoiceMethods,
     "Typed answer data for one questionnaire choice; style is rejected.",
     &MaterializeChoice},
    {"QuestionnaireItem", kItemConstructors, kItemMethods,
     "Typed data for one question, holding QuestionnaireChoice and "
     "QuestionnaireInput children; style is rejected.",
     &MaterializeItem},
    {"QuestionnaireInput", kInputConstructors, kInputMethods,
     "Typed freeform-answer data for a question, backed by a retained "
     "InputState; style is rejected.",
     &MaterializeInput},
    {"Questionnaire", kRootConstructors, kRootMethods,
     "Native retained questionnaire keyed by a stable id: it owns answers, "
     "validation, navigation, focus and shortcuts, and renders progress, the "
     "active question and the navigation actions. Changing the questions "
     "rebuilds the flow.",
     &MaterializeRoot},
};

} // namespace gpui::component_shell::questionnaire

namespace gpui::component_shell {

Entity<QuestionnaireState> QuestionnaireStateFor(Ctx* cx, Str id) {
    Str key = StrDup(cx->a, fmt("shell-questionnaire:%s", id));
    questionnaire::Retained* retained =
        UseKeyedState<questionnaire::Retained>(cx, key,
                                               StrL("shell-questionnaire"))
            .Get(cx);
    return retained ? retained->native : Entity<QuestionnaireState>{};
}

bool RegisterQuestionnaire(shell::ComponentRegistry* registry,
                           shell::RegistryError* error) {
    for (const shell::ComponentDescriptor& descriptor :
         questionnaire::kDescriptors) {
        if (!registry->Register(&descriptor, error)) return false;
    }
    return true;
}

} // namespace gpui::component_shell
