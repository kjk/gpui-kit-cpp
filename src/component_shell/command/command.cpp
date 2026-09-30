// crates/component-shell/src/shell/command/command.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "shell/action.h"
#include "shell/view.h"
#include "ui/command.h"

#include <float.h>
#include <math.h>

namespace gpui::component_shell::command::command {

using component::CommandEntry;
using component::CommandGroup;
using component::CommandItem;

struct ItemPayload {
    Str label;
};
struct GroupPayload {
    Str label;
};
struct CommandPayload {
    ComponentArgument state;
};
struct Separator {};

struct ItemOp {
    enum Kind : uint8_t {
        Keyword,
        Checked,
        Action,
    } kind = Keyword;
    Str text;
    bool value = false;
};

struct CommandOp {
    enum Kind : uint8_t {
        Searchable,
        Filterable,
        Bordered,
        Placeholder,
        MaxHeight,
        OnQuery,
        OnSelect,
        OnConfirm,
        OnCancel,
    } kind = Searchable;
    bool value = false;
    Str text;
    float pixels = 0;
    ComponentArgument callback = {};
};

// n value-initialized T from `a`.
template <class T>
static T* NewArray(Arena* a, int n) {
    if (n <= 0) return nullptr;
    T* items = (T*)Alloc(a, (int)sizeof(T) * n);
    for (int i = 0; i < n; i++) new (&items[i]) T();
    return items;
}

// CommandItem::child's closure: the lazy row, rebuilt whenever the palette
// draws it. The DeferredSlot rides in the item's `data`, which nothing in
// the shell reads back: Rust's callbacks report an IndexPath only.
static El* BuildItemContent(Ctx* cx, const CommandItem* item) {
    return BuildDeferredSlot((const DeferredSlot*)item->data, cx);
}

static El* MaterializeItem(MaterializeRequest* request) {
    const ItemPayload* payload = request->PayloadAs<ItemPayload>();
    if (!payload)
        return request->Fail(StrL("CommandItem incompatible payload"));
    Ctx* cx = request->cx;
    CommandItem* item = ArenaNew<CommandItem>(cx->a);
    item->label = payload->label;
    item->disabled = request->disabled;
    int keywords = 0;
    EachMethod<ItemOp>(request, [&](const ItemOp& op) {
        if (op.kind == ItemOp::Keyword) keywords++;
    });
    Str* words = NewArray<Str>(cx->a, keywords);
    int at = 0;
    EachMethod<ItemOp>(request, [&](const ItemOp& op) {
        switch (op.kind) {
            case ItemOp::Keyword:
                words[at++] = op.text;
                break;
            case ItemOp::Checked:
                item->checked = op.value;
                break;
            case ItemOp::Action:
                item->action = shell::ShellActionOf(op.text);
                break;
        }
    });
    item->keywords = words;
    item->nKeywords = keywords;
    if (!RejectStyle(request, "CommandItem")) return nullptr;
    shell::ComponentElementFactory content = request
                                                 ->TakeSlotFactory("content");
    if (content.IsSet()) {
        // Rust measures a custom row with layout_as_root; the port's
        // CommandItem takes a declared height instead, and a shell row is
        // given none, so it is laid out at the standard row height.
        item->content = &BuildItemContent;
        item->data = (intptr_t)NewDeferredSlot(request, content,
                                               "Failed to render CommandItem "
                                               "content");
    }
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "CommandItem", children[i].componentName,
                          {}))
            return nullptr;
    }
    return CarrierOf(cx, item);
}

static El* MaterializeGroup(MaterializeRequest* request) {
    const GroupPayload* payload = request->PayloadAs<GroupPayload>();
    if (!payload)
        return request->Fail(StrL("CommandGroup incompatible payload"));
    Ctx* cx = request->cx;
    if (!RejectStyle(request, "CommandGroup")) return nullptr;
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    CommandGroup* group = ArenaNew<CommandGroup>(cx->a);
    group->heading = payload->label;
    CommandItem* items = NewArray<CommandItem>(cx->a, count);
    static constexpr const char* kAllowed[] = {"CommandItem"};
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "CommandGroup", children[i].componentName,
                          kAllowed))
            return nullptr;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        CommandItem* item =
            TakeCarriedAs<CommandItem>(request, element, "CommandItem");
        if (!item) return nullptr;
        items[i] = *item;
    }
    group->items = items;
    group->nItems = count;
    return CarrierOf(cx, group);
}

static El* MaterializeSeparator(MaterializeRequest* request) {
    if (!shell::PayloadIs<Separator>(request->payload))
        return request->Fail(StrL("CommandSeparator incompatible payload"));
    if (!RejectStyle(request, "CommandSeparator")) return nullptr;
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "CommandSeparator",
                          children[i].componentName, {}))
            return nullptr;
    }
    return CarrierOf(request->cx, ArenaNew<Separator>(request->cx->a));
}

// The callbacks, each `invoke_and_report_with` its context.
static void RunQuery(const shell::ComponentEventBinding* binding,
                     ScriptView* view, Ctx* cx, const void* event) {
    const component::CommandEvent* ev = (const component::CommandEvent*)event;
    shell::ComponentDataValue query =
        shell::ComponentDataValue::String(ev ? ev->query : Str{});
    binding->callback.InvokeAndReport(view->runtime, "Command.on_query", &query,
                                      1, cx->win, cx->app);
}

template <int Which>
static void RunPath(const shell::ComponentEventBinding* binding,
                    ScriptView* view, Ctx* cx, const void* event) {
    const component::CommandEvent* ev = (const component::CommandEvent*)event;
    shell::ComponentDataValue path[2] = {
        shell::ComponentDataValue::Number(ev ? (double)ev->path.section : 0),
        shell::ComponentDataValue::Number(ev ? (double)ev->path.row : 0)};
    binding->callback.InvokeAndReport(
        view->runtime, Which == 0 ? "Command.on_select" : "Command.on_confirm",
        path, 2, cx->win, cx->app);
}

static void RunCancel(const shell::ComponentEventBinding* binding,
                      ScriptView* view, Ctx* cx, const void*) {
    binding->callback.InvokeAndReport(view->runtime, "Command.on_cancel",
                                      nullptr, 0, cx->win, cx->app);
}

static El* MaterializeCommand(MaterializeRequest* request) {
    const CommandPayload* payload = request->PayloadAs<CommandPayload>();
    if (!payload) return request->Fail(StrL("Command incompatible payload"));
    Ctx* cx = request->cx;
    EntityState<component::CommandState>* state =
        request->StateAs<EntityState<component::CommandState>>(payload->state,
                                                               "CommandState");
    if (!state) return nullptr;
    component::Command* command =
        component::Command::New(cx, request->elementId, state->entity);
    // Rust's header and footer are lazy factories the palette builds when it
    // renders; the port's Command takes this frame's elements, which are
    // built here for the same render.
    shell::ComponentElementFactory header = request->TakeSlotFactory("header");
    if (header.IsSet())
        command->Header(BuildDeferredSlot(
            NewDeferredSlot(request, header, "Failed to render Command header"),
            cx));
    shell::ComponentElementFactory footer = request->TakeSlotFactory("footer");
    if (footer.IsSet())
        command->Footer(BuildDeferredSlot(
            NewDeferredSlot(request, footer, "Failed to render Command footer"),
            cx));
    EachMethod<CommandOp>(request, [&](const CommandOp& op) {
        switch (op.kind) {
            case CommandOp::Searchable:
                command->Searchable(op.value);
                break;
            case CommandOp::Filterable:
                command->Filterable(op.value);
                break;
            case CommandOp::Bordered:
                command->Bordered(op.value);
                break;
            case CommandOp::Placeholder:
                command->Placeholder(op.text);
                break;
            case CommandOp::MaxHeight:
                command->MaxH(op.pixels);
                break;
            case CommandOp::OnQuery:
                command->OnQuery(shell::ComponentListener(
                    cx, &RunQuery, request->ResolveCallback(op.callback)));
                break;
            case CommandOp::OnSelect:
                command->OnSelect(shell::ComponentListener(
                    cx, &RunPath<0>, request->ResolveCallback(op.callback)));
                break;
            case CommandOp::OnConfirm:
                command->OnConfirm(shell::ComponentListener(
                    cx, &RunPath<1>, request->ResolveCallback(op.callback)));
                break;
            case CommandOp::OnCancel:
                command->OnCancel(shell::ComponentListener(
                    cx, &RunCancel, request->ResolveCallback(op.callback)));
                break;
        }
    });
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    CommandEntry* entries = NewArray<CommandEntry>(cx->a, count);
    static constexpr const char* kAllowed[] = {"CommandItem", "CommandGroup",
                                               "CommandSeparator"};
    for (int i = 0; i < count; i++) {
        const char* name = children[i].componentName;
        if (!RequireChild(request, "Command", name, kAllowed)) return nullptr;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        if (strcmp(name, "CommandItem") == 0) {
            CommandItem* item =
                TakeCarriedAs<CommandItem>(request, element, "CommandItem");
            if (!item) return nullptr;
            entries[i] = component::CommandEntryOf(*item);
        } else if (strcmp(name, "CommandGroup") == 0) {
            CommandGroup* group =
                TakeCarriedAs<CommandGroup>(request, element, "CommandGroup");
            if (!group) return nullptr;
            entries[i] = component::CommandEntryOf(*group);
        } else {
            if (!TakeCarriedAs<Separator>(request, element, "CommandSeparator"))
                return nullptr;
            entries[i] = component::CommandSeparatorEntry();
        }
    }
    command->Entries(entries, count);
    return request->ApplyStyle(command->IntoEl());
}

// ─── Recorders ─────────────────────────────────────────────────────────────

static bool IsText(const ComponentArgument* args, int count) {
    return count == 1 && args[0].kind == shell::ComponentArgumentKind::String;
}
static bool IsNonEmptyText(const ComponentArgument* args, int count) {
    return IsText(args, count) && len(StrTrimAscii(args[0].string)) != 0;
}

static bool ConstructItem(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (!IsNonEmptyText(args, count))
        return build->Fail(StrL("CommandItem expects non-empty label"));
    build->New<ItemPayload>()->label = args[0].string;
    return true;
}

static bool ConstructGroup(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    if (!IsNonEmptyText(args, count))
        return build->Fail(StrL("CommandGroup expects non-empty label"));
    build->New<GroupPayload>()->label = args[0].string;
    return true;
}

static bool ConstructSeparator(PayloadBuild* build, const ComponentArgument*,
                               int) {
    return build->Mark<Separator>();
}

static bool ConstructCommand(PayloadBuild* build, const ComponentArgument* args,
                             int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Entity)
        return build->Fail(StrL("Command expects CommandState"));
    build->New<CommandPayload>()->state = args[0];
    return true;
}

static bool RecordKeyword(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (!IsNonEmptyText(args, count))
        return build->Fail(StrL("CommandItem.keyword expects non-empty text"));
    ItemOp* op = build->New<ItemOp>();
    op->kind = ItemOp::Keyword;
    op->text = args[0].string;
    return true;
}

static bool RecordChecked(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(StrL("CommandItem.checked expects boolean"));
    ItemOp* op = build->New<ItemOp>();
    op->kind = ItemOp::Checked;
    op->value = args[0].boolean;
    return true;
}

static bool RecordAction(PayloadBuild* build, const ComponentArgument* args,
                         int count) {
    if (!IsNonEmptyText(args, count))
        return build
            ->Fail(StrL("CommandItem.action expects non-empty action "
                        "id"));
    ItemOp* op = build->New<ItemOp>();
    op->kind = ItemOp::Action;
    op->text = args[0].string;
    return true;
}

// bool_method("Command", name, "Sets native Command behavior.", ..).
template <CommandOp::Kind Kind>
static bool RecordBool(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean) {
        const char* name = Kind == CommandOp::Searchable   ? "searchable"
                           : Kind == CommandOp::Filterable ? "filterable"
                                                           : "bordered";
        return build->Fail(fmt("Command.%s expects one boolean", Str(name)));
    }
    CommandOp* op = build->New<CommandOp>();
    op->kind = Kind;
    op->value = args[0].boolean;
    return true;
}

static bool RecordPlaceholder(PayloadBuild* build,
                              const ComponentArgument* args, int count) {
    if (!IsText(args, count))
        return build->Fail(StrL("Command.placeholder expects text"));
    CommandOp* op = build->New<CommandOp>();
    op->kind = CommandOp::Placeholder;
    op->text = args[0].string;
    return true;
}

static bool RecordMaxHeight(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    double value =
        count == 1 && args[0].kind == shell::ComponentArgumentKind::Number
            ? args[0].number
            : NAN;
    if (!isfinite(value) || !(value > 0.0) || value > (double)FLT_MAX)
        return build
            ->Fail(StrL("Command.max_height expects positive finite "
                        "pixels"));
    CommandOp* op = build->New<CommandOp>();
    op->kind = CommandOp::MaxHeight;
    op->pixels = (float)value;
    return true;
}

// callback_method(name, signature, make).
template <CommandOp::Kind Kind>
static bool RecordCallback(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Callback) {
        const char* name = Kind == CommandOp::OnQuery     ? "on_query"
                           : Kind == CommandOp::OnSelect  ? "on_select"
                           : Kind == CommandOp::OnConfirm ? "on_confirm"
                                                          : "on_cancel";
        return build->Fail(fmt("Command.%s expects callback", Str(name)));
    }
    CommandOp* op = build->New<CommandOp>();
    op->kind = Kind;
    op->callback = args[0];
    return true;
}

static bool NewCommandState(shell::StateBuild* build, const ComponentArgument*,
                            int) {
    EntityState<component::CommandState>* state =
        build->New<EntityState<component::CommandState>>();
    state->app = build->app;
    state->entity = EntityNewState<component::CommandState>(build->app);
    return true;
}

static constexpr ArgumentDescriptor kLabelArgs[] = {{"label", SchemaString()}};
static constexpr ArgumentDescriptor kDisabledArgs[] = {
    {"disabled", SchemaBoolean()}};
static constexpr ArgumentDescriptor kKeywordArgs[] = {
    {"keyword", SchemaString()}};
static constexpr ArgumentDescriptor kCheckedArgs[] = {
    {"checked", SchemaBoolean()}};
static constexpr ArgumentDescriptor kActionArgs[] = {
    {"action", SchemaString()}};
static constexpr ArgumentDescriptor kStateArgs[] = {
    {"state", SchemaEntity("CommandState")}};
static constexpr ArgumentDescriptor kSearchableArgs[] = {
    {"searchable", SchemaBoolean()}};
static constexpr ArgumentDescriptor kFilterableArgs[] = {
    {"filterable", SchemaBoolean()}};
static constexpr ArgumentDescriptor kBorderedArgs[] = {
    {"bordered", SchemaBoolean()}};
static constexpr ArgumentDescriptor kPlaceholderArgs[] = {
    {"placeholder", SchemaString()}};
static constexpr ArgumentDescriptor kPixelsArgs[] = {
    {"pixels", SchemaNumber()}};
static constexpr ArgumentDescriptor kQueryArgs[] = {
    {"callback", SchemaCallback("(query: string, cx: Context) => void")}};
static constexpr ArgumentDescriptor kPathArgs[] = {
    {"callback",
     SchemaCallback("(section: number, row: number, cx: Context) => void")}};
static constexpr ArgumentDescriptor kCancelArgs[] = {
    {"callback", SchemaCallback("(cx: Context) => void")}};

static constexpr const char* kCallbackDoc =
    "Runs after the native Command state releases its update lease.";

static constexpr shell::StateDescriptor kCommandState = {
    "CommandState",
    "CommandState",
    {},
    "Retained native command query, focus, selection, measurement and scroll "
    "state.",
    &NewCommandState,
    {}};

static constexpr ConstructorDescriptor kItemConstructors[] = {
    {"CommandItem", kLabelArgs, &ConstructItem}};
static constexpr MethodDescriptor kItemMethods[] = {
    {"disabled", kDisabledArgs, "Sets common disabled state.",
     &RecordCommonBehavior},
    {"keyword", kKeywordArgs, "Sets the item search keyword.", &RecordKeyword},
    {"checked", kCheckedArgs, "Sets the item checked state.", &RecordChecked},
    {"action", kActionArgs, "Dispatches the named shell action when selected.",
     &RecordAction},
};
static constexpr ComponentDescriptor kCommandItem = {
    "CommandItem", kItemConstructors, kItemMethods,
    "Typed native CommandItem data. Action strings map to ShellAction; style "
    "and ordinary/typed children are rejected. Named content(element) is a "
    "repeatable lazy row factory.",
    &MaterializeItem};

static constexpr ConstructorDescriptor kGroupConstructors[] = {
    {"CommandGroup", kLabelArgs, &ConstructGroup}};
static constexpr ComponentDescriptor kCommandGroup = {
    "CommandGroup",
    kGroupConstructors,
    {},
    "Typed native CommandGroup data accepting only CommandItem children; style "
    "is rejected.",
    &MaterializeGroup};

static constexpr ConstructorDescriptor kSeparatorConstructors[] = {
    {"CommandSeparator", {}, &ConstructSeparator}};
static constexpr ComponentDescriptor kCommandSeparator = {
    "CommandSeparator",
    kSeparatorConstructors,
    {},
    "Typed Command separator data; style and children are rejected.",
    &MaterializeSeparator};

static constexpr ConstructorDescriptor kCommandConstructors[] = {
    {"Command", kStateArgs, &ConstructCommand}};
static constexpr MethodDescriptor kCommandMethods[] = {
    {"searchable", kSearchableArgs, "Sets native Command behavior.",
     &RecordBool<CommandOp::Searchable>},
    {"filterable", kFilterableArgs, "Sets native Command behavior.",
     &RecordBool<CommandOp::Filterable>},
    {"bordered", kBorderedArgs, "Sets native Command behavior.",
     &RecordBool<CommandOp::Bordered>},
    {"placeholder", kPlaceholderArgs, "Sets the command search placeholder.",
     &RecordPlaceholder},
    {"max_height", kPixelsArgs, "Sets the command results maximum height.",
     &RecordMaxHeight},
    {"on_query", kQueryArgs, kCallbackDoc, &RecordCallback<CommandOp::OnQuery>},
    {"on_select", kPathArgs, kCallbackDoc,
     &RecordCallback<CommandOp::OnSelect>},
    {"on_confirm", kPathArgs, kCallbackDoc,
     &RecordCallback<CommandOp::OnConfirm>},
    {"on_cancel", kCancelArgs, kCallbackDoc,
     &RecordCallback<CommandOp::OnCancel>},
};
static constexpr ComponentDescriptor kCommand = {
    "Command", kCommandConstructors, kCommandMethods,
    "Styled retained native Command palette consuming CommandItem, "
    "CommandGroup and CommandSeparator in exact order. Named header/footer "
    "elements are repeatable lazy factories; native empty content remains "
    "unavailable because the shell has no common empty(element) named-slot "
    "route.",
    &MaterializeCommand};

} // namespace gpui::component_shell::command::command

namespace gpui::component_shell {

bool RegisterCommandCommand(shell::ComponentRegistry* registry,
                            shell::RegistryError* error) {
    using namespace command::command;
    return registry->RegisterState(&kCommandState, error) &&
           registry->Register(&kCommandItem, error) &&
           registry->Register(&kCommandGroup, error) &&
           registry->Register(&kCommandSeparator, error) &&
           registry->Register(&kCommand, error);
}

} // namespace gpui::component_shell
