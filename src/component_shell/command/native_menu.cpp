// crates/component-shell/src/shell/command/native_menu.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "shell/action.h"
#include "shell/view.h"
#include "ui/button.h"
#include "ui/native_menu.h"

namespace gpui::component_shell::command::native_menu {

struct Item {
    Str label;
    Str action;
    bool disabled = false;
    bool checked = false;
};
struct Separator {};
struct Trigger {
    Str id;
    Str label;
};
struct ItemOp {
    enum Kind : uint8_t {
        Checked,
        Action,
    } kind = Checked;
    bool checked = false;
    Str action;
};
struct ErrorCallback {
    ComponentArgument callback = {};
};
struct Entry {
    enum Kind : uint8_t {
        Item,
        Separator,
    } kind = Item;
    native_menu::Item item;
};

// native_menu.rs test_probe, widened into a seam: see families.h.
static NativeMenuShowProbe gShowProbe = nullptr;

// resolve_item: last call wins, and the combination the native API cannot
// express is refused.
static bool ResolveItem(MaterializeRequest* request, const Item& base,
                        bool disabled, Item* out) {
    *out = base;
    out->disabled = disabled;
    EachMethod<ItemOp>(request, [&](const ItemOp& op) {
        if (op.kind == ItemOp::Checked)
            out->checked = op.checked;
        else
            out->action = op.action;
    });
    if (out->disabled && out->checked) {
        request
            ->Fail(StrL("NativeMenuItem cannot be both disabled and checked "
                        "because the native API has no combined "
                        "constructor"));
        return false;
    }
    return true;
}

static El* MaterializeItem(MaterializeRequest* request) {
    const Item* base = request->PayloadAs<Item>();
    if (!base)
        return request->Fail(StrL("NativeMenuItem incompatible payload"));
    Ctx* cx = request->cx;
    Entry* entry = ArenaNew<Entry>(cx->a);
    entry->kind = Entry::Item;
    if (!ResolveItem(request, *base, request->disabled, &entry->item))
        return nullptr;
    if (!RejectStyle(request, "NativeMenuItem")) return nullptr;
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "NativeMenuItem", children[i].componentName,
                          {}))
            return nullptr;
    }
    return CarrierOf(cx, entry);
}

static El* MaterializeSeparator(MaterializeRequest* request) {
    if (!RejectStyle(request, "NativeMenuSeparator")) return nullptr;
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "NativeMenuSeparator",
                          children[i].componentName, {}))
            return nullptr;
    }
    Entry* entry = ArenaNew<Entry>(request->cx->a);
    entry->kind = Entry::Separator;
    return CarrierOf(request->cx, entry);
}

// What the Button's on_click closure captured, for this frame.
struct TriggerClick {
    shell::ComponentWindowEffects effects;
    const Entry* entries = nullptr;
    int count = 0;
    Str key;
    // Filled at click time: the event's context and position.
    Ctx* cx = nullptr;
    float x = 0;
    float y = 0;
};

// The keyed effect body: build the NativeMenu, whose chosen row's action id
// is its ShellAction, and show it where the click was.
static bool ShowMenu(void* user, Window*, App*, Str* error, Arena* a) {
    TriggerClick* click = (TriggerClick*)user;
    Ctx* cx = click->cx;
    component::NativeMenu* menu = component::NativeMenu::New(cx);
    for (int i = 0; i < click->count; i++) {
        const Entry& entry = click->entries[i];
        if (entry.kind == Entry::Separator) {
            menu->Separator();
            continue;
        }
        intptr_t action = (intptr_t)shell::ShellActionOf(entry.item.action);
        if (entry.item.disabled)
            menu->MenuWithDisabled(entry.item.label, true, action);
        else if (entry.item.checked)
            menu->MenuWithCheck(entry.item.label, true, action);
        else
            menu->Menu(entry.item.label, action);
    }
    // Rust dispatches each row's ShellAction; the port's NativeMenu reports
    // the chosen row's id instead, which is the action, dispatched here from
    // the focused element.
    menu->OnSelect(Listen(cx, &ScriptView::OnDispatchAction));
    if (gShowProbe) return gShowProbe(menu, error, a);
    menu->Show(click->x, click->y);
    return true;
}

static bool RunEvent(shell::ComponentEventEffects* effects, void* user,
                     Str* error) {
    TriggerClick* click = (TriggerClick*)user;
    bool executed = false;
    return effects->RunOnce(click->key, &ShowMenu, user, &executed, error);
}

static void RunClick(const shell::ComponentEventBinding* binding, ScriptView*,
                     Ctx* cx, const void* event) {
    TriggerClick* click = (TriggerClick*)binding->user;
    const ClickEvent* ev = (const ClickEvent*)event;
    click->cx = cx;
    click->x = ev ? ev->x : 0;
    click->y = ev ? ev->y : 0;
    Arena* a = ArenaNew();
    Str error;
    // `let _ = effects.event(..)`: a failure has been reported to the script.
    click->effects.Event(cx->win, cx->app, &RunEvent, click, &error, a);
    ArenaDelete(a);
}

static El* MaterializeTrigger(MaterializeRequest* request) {
    const Trigger* trigger = request->PayloadAs<Trigger>();
    if (!trigger)
        return request->Fail(StrL("NativeMenuTrigger incompatible payload"));
    // last_reporter.
    const ErrorCallback* reporter = nullptr;
    EachMethod<ErrorCallback>(request,
                              [&](const ErrorCallback& op) { reporter = &op; });
    if (!reporter)
        return request
            ->Fail(StrL("NativeMenuTrigger requires "
                        "on_effect_error(callback)"));
    Ctx* cx = request->cx;
    TriggerClick* click = ArenaNew<TriggerClick>(cx->a);
    click->effects = request->ResolveCallback(reporter->callback)
                         .WindowEffects(request->runtime, cx->a);
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    Entry* entries =
        count ? (Entry*)Alloc(cx->a, (int)sizeof(Entry) * count) : nullptr;
    static constexpr const char* kAllowed[] = {"NativeMenuItem",
                                               "NativeMenuSeparator"};
    for (int i = 0; i < count; i++) {
        if (!RequireChild(request, "NativeMenuTrigger",
                          children[i].componentName, kAllowed))
            return nullptr;
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        Entry* entry =
            TakeCarriedAs<Entry>(request, element, "NativeMenu entry");
        if (!entry) return nullptr;
        entries[i] = *entry;
    }
    click->entries = entries;
    click->count = count;
    click->key = StrDup(cx->a, fmt("native-menu:%s", trigger->id));
    component::Button* button =
        component::Button::New(cx, trigger->id)
            ->Label(trigger->label)
            ->Disabled(request->disabled)
            ->OnClick(shell::ComponentListener(cx, &RunClick,
                                               click->effects.reporter, click));
    return request->ApplyStyle(button->IntoEl());
}

// ─── Recorders ─────────────────────────────────────────────────────────────

static bool IsNonEmptyText(const ComponentArgument& arg) {
    return arg.kind == shell::ComponentArgumentKind::String &&
           len(StrTrimAscii(arg.string)) != 0;
}

static bool ConstructItem(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 2 || !IsNonEmptyText(args[0]) || !IsNonEmptyText(args[1]))
        return build
            ->Fail(StrL("NativeMenuItem expects non-empty label and "
                        "action id"));
    Item* item = build->New<Item>();
    item->label = args[0].string;
    item->action = args[1].string;
    return true;
}

static bool ConstructSeparator(PayloadBuild* build, const ComponentArgument*,
                               int) {
    return build->Mark<Separator>();
}

static bool ConstructTrigger(PayloadBuild* build, const ComponentArgument* args,
                             int count) {
    if (count != 2 || !IsNonEmptyText(args[0]) || !IsNonEmptyText(args[1]))
        return build
            ->Fail(StrL("NativeMenuTrigger expects non-empty id and "
                        "label"));
    Trigger* trigger = build->New<Trigger>();
    trigger->id = args[0].string;
    trigger->label = args[1].string;
    return true;
}

static bool RecordChecked(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(StrL("NativeMenuItem.checked expects boolean"));
    ItemOp* op = build->New<ItemOp>();
    op->kind = ItemOp::Checked;
    op->checked = args[0].boolean;
    return true;
}

static bool RecordAction(PayloadBuild* build, const ComponentArgument* args,
                         int count) {
    if (count != 1 || !IsNonEmptyText(args[0]))
        return build
            ->Fail(StrL("NativeMenuItem.action expects non-empty "
                        "action id"));
    ItemOp* op = build->New<ItemOp>();
    op->kind = ItemOp::Action;
    op->action = args[0].string;
    return true;
}

static bool RecordErrorCallback(PayloadBuild* build,
                                const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Callback)
        return build
            ->Fail(StrL("NativeMenuTrigger.on_effect_error expects "
                        "callback"));
    build->New<ErrorCallback>()->callback = args[0];
    return true;
}

static constexpr ArgumentDescriptor kItemArgs[] = {{"label", SchemaString()},
                                                   {"action", SchemaString()}};
static constexpr ArgumentDescriptor kTriggerArgs[] = {
    {"id", SchemaString()},
    {"label", SchemaString()}};
static constexpr ArgumentDescriptor kDisabledArgs[] = {
    {"disabled", SchemaBoolean()}};
static constexpr ArgumentDescriptor kCheckedArgs[] = {
    {"checked", SchemaBoolean()}};
static constexpr ArgumentDescriptor kActionArgs[] = {
    {"action", SchemaString()}};
static constexpr ArgumentDescriptor kErrorArgs[] = {
    {"callback", SchemaCallback("(message: string, cx: Context) => void")}};

static constexpr ConstructorDescriptor kItemConstructors[] = {
    {"NativeMenuItem", kItemArgs, &ConstructItem}};
static constexpr MethodDescriptor kItemMethods[] = {
    {"disabled", kDisabledArgs, "Disables the native menu item.",
     &RecordCommonBehavior},
    {"checked", kCheckedArgs, "Sets the native menu item checked state.",
     &RecordChecked},
    {"action", kActionArgs, "Dispatches the named shell action.",
     &RecordAction},
};
static constexpr ComponentDescriptor kNativeMenuItem = {
    "NativeMenuItem", kItemConstructors, kItemMethods,
    "Typed native-menu item data with last-call-wins checked/action; "
    "disabled+checked is rejected because the native API cannot express it. "
    "ShellAction dispatches selection.",
    &MaterializeItem};

static constexpr ConstructorDescriptor kSeparatorConstructors[] = {
    {"NativeMenuSeparator", {}, &ConstructSeparator}};
static constexpr ComponentDescriptor kNativeMenuSeparator = {
    "NativeMenuSeparator",
    kSeparatorConstructors,
    {},
    "Typed native-menu separator data.",
    &MaterializeSeparator};

static constexpr ConstructorDescriptor kTriggerConstructors[] = {
    {"NativeMenuTrigger", kTriggerArgs, &ConstructTrigger}};
static constexpr MethodDescriptor kTriggerMethods[] = {
    {"disabled", kDisabledArgs, "Disables the native menu trigger.",
     &RecordCommonBehavior},
    {"on_effect_error", kErrorArgs,
     "Reports asynchronous native menu failures.", &RecordErrorCallback},
};
static constexpr ComponentDescriptor kNativeMenuTrigger = {
    "NativeMenuTrigger", kTriggerConstructors, kTriggerMethods,
    "A real Button trigger that shows an OS NativeMenu (or fallback) in a "
    "keyed event effect. Last on_effect_error wins; typed item selection "
    "dispatches ShellAction.",
    &MaterializeTrigger};

} // namespace gpui::component_shell::command::native_menu

namespace gpui::component_shell {

void SetNativeMenuShowProbe(NativeMenuShowProbe probe) {
    command::native_menu::gShowProbe = probe;
}

bool RegisterCommandNativeMenu(shell::ComponentRegistry* registry,
                               shell::RegistryError* error) {
    using namespace command::native_menu;
    return registry->Register(&kNativeMenuItem, error) &&
           registry->Register(&kNativeMenuSeparator, error) &&
           registry->Register(&kNativeMenuTrigger, error);
}

} // namespace gpui::component_shell
