// crates/component-shell/src/shell/window_effects/mod.rs: event-triggered
// native window effects.
//
// These registrations deliberately render real buttons. Dialogs, sheets, and
// notifications are opened only by the button's click event, never while the
// script tree is materialized. Custom footer/action IR is intentionally
// omitted; the public surface is limited to native contracts we can close.

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "shell/runtime.h"
#include "ui/button.h"
#include "ui/dialog.h"
#include "ui/notification.h"
#include "ui/sheet.h"
#include "ui/window_ext.h"

namespace gpui::component_shell::window_effects {

using component::NotificationType;
using component::SheetPlacement;

struct Trigger {
    Str id;
    Str label;
    ComponentArgument reporter = {};
};

struct Op {
    enum Kind : uint8_t {
        Title,
        Description,
        Placement,
        Type,
        Autohide,
        ShowCancel,
        OnOk,
        OnCancel,
        OnClose,
        OnClick,
    } kind = Title;
    Str text;
    SheetPlacement placement = SheetPlacement::Right;
    NotificationType type = NotificationType::Info;
    bool flag = false;
    ComponentArgument callback = {};
};

// Kind: which of the four surfaces a descriptor opens.
enum class Surface : uint8_t {
    Dialog,
    AlertDialog,
    Sheet,
    Notification,
};

// mod.rs test_probe, widened into a seam: see families.h.
static WindowEffectsReporterFailureProbe gReporterProbe = nullptr;

// ─── The trigger's latest frame ────────────────────────────────────────────
//
// Rust's click closure captures the content factory and the resolved
// callbacks, and the factory holds a lease on the snapshot it was recorded
// in, so a surface keeps building the content of the render that opened it.
// A snapshot here lives only until the script renders again, and its
// callbacks retire with it. So a trigger republishes, every frame it is
// materialized, what an open surface needs — the factory with the
// description it belongs to, and the callbacks — into a relay keyed by its
// id, and the surface reads the relay: the content is the latest render's,
// and a surface whose trigger was not materialized in the current frame
// shows a failure instead of a stale description.
struct Relay {
    ShellRuntime* runtime = nullptr;
    // The ScriptView the trigger was materialized in: the content's own
    // listeners are the view's, so it is built as the view.
    EntityId view = {};
    uint64_t frame = 0;
    const shell::SpecArena* specs = nullptr;
    ShellError* error = nullptr;
    shell::ComponentElementFactory content = {};
    shell::ComponentCallback reporter = {};
    shell::ComponentCallback onOk = {};
    shell::ComponentCallback onCancel = {};
    shell::ComponentCallback onClose = {};
    shell::ComponentCallback onClick = {};

    bool Fresh(Ctx* cx) const {
        return runtime && specs && cx->win && frame == cx->win->frameSeq;
    }

    // invoke(): the callback, when there is one, reporting a failure.
    void Invoke(Ctx* cx, shell::ComponentCallback callback,
                const char* label) const {
        if (runtime && callback.IsSet())
            callback
                .InvokeAndReport(runtime, label, nullptr, 0, cx->win, cx->app);
    }

    static void OnNotificationClick(Relay* self, Ctx* cx, const void*) {
        if (self)
            self->Invoke(cx, self->onClick,
                         "Notification.on_click callback failed");
    }
    static void OnNotificationClose(Relay* self, Ctx* cx, const void*) {
        if (self)
            self->Invoke(cx, self->onClose,
                         "Notification.on_close callback failed");
    }
};

// The notification id's type: Rust's `id1::<Materializer>(id)`.
struct NotificationTag {};

// report_factory_error: the reporter hears the message; a reporter that
// fails too is diagnosed with both.
static void ReportFactoryError(const Relay* relay, Ctx* cx, Str message) {
    shell::ComponentDataValue value =
        shell::ComponentDataValue::String(message);
    Str error;
    if (!relay->reporter.InvokeWith(relay->runtime, &value, 1, cx->win, cx->app,
                                    nullptr, &error, cx->a)) {
        TempStr diagnosis =
            fmt("%s; effect error reporter also failed: %s", message, error);
        logf("%s\n", diagnosis);
        if (gReporterProbe) gReporterProbe(Str(diagnosis));
    }
}

// One open dialog, alert or sheet: the layer entity the window's Root
// renders, holding what the click captured.
struct Layer {
    Surface surface = Surface::Dialog;
    Entity<Relay> relay = {};
    Arena* arena = nullptr;
    Str title;
    Str description;
    bool showCancel = false;
    SheetPlacement placement = SheetPlacement::Right;
    // factory_error_reported: once per opened surface.
    bool factoryErrorReported = false;

    ~Layer() {
        if (arena) ArenaDelete(arena);
    }

    const char* Name() const {
        return surface == Surface::Dialog        ? "Dialog"
               : surface == Surface::AlertDialog ? "AlertDialog"
                                                 : "Sheet";
    }

    // factory.build(window, cx), or the failure it answers — reported to the
    // effect's reporter the first time.
    El* Content(Ctx* cx) {
        Relay* live = relay.Get(cx);
        Arena* a = cx->a;
        if (!live || !live->Fresh(cx) || !live->content.IsSet()) {
            return Div(a)->Child(TextEl(
                a, StrDup(a, fmt("Failed to render %s content: the trigger "
                                 "that opened it is not rendered",
                                 Str(Name())))));
        }
        Ctx view = *cx;
        view.self = live->view;
        shell::MaterializeRequest request;
        request.cx = &view;
        request.runtime = live->runtime;
        request.specs = live->specs;
        request.error = live->error;
        uint64_t failures = live->runtime->ComponentFailureCount();
        El* element = request.BuildFactory(live->content);
        if (element && live->runtime->ComponentFailureCount() == failures)
            return element;
        Str why = live->runtime->ComponentFailureCount() != failures
                      ? live->runtime->LastComponentFailure()
                      : StrL("no element");
        Str message =
            StrDup(a, fmt("Failed to render %s content: %s", Str(Name()), why));
        if (!factoryErrorReported) {
            factoryErrorReported = true;
            ReportFactoryError(live, cx, message);
        }
        return Div(a)->Child(TextEl(a, message));
    }

    // The callbacks an action runs, read before the layer closes: closing
    // drops this entity.
    struct Close {
        Relay relay;
        const char* closeLabel = nullptr;
    };
    Close Closing(Ctx* cx) const {
        Close close;
        if (const Relay* r = relay.Get(cx)) close.relay = *r;
        close.closeLabel = surface == Surface::Dialog
                               ? "Dialog.on_close callback failed"
                           : surface == Surface::AlertDialog
                               ? "AlertDialog.on_close callback failed"
                               : "Sheet.on_close callback failed";
        return close;
    }

    // on_ok / on_cancel answer true, so the dialog closes, and on_close
    // follows.
    static void OnOk(Layer* self, Ctx* cx, const ClickEvent*) {
        if (!self) return;
        Close close = self->Closing(cx);
        close.relay.Invoke(cx, close.relay.onOk,
                           self->surface == Surface::Dialog
                               ? "Dialog.on_ok callback failed"
                               : "AlertDialog.on_ok callback failed");
        WindowCloseDialog(cx);
        close.relay.Invoke(cx, close.relay.onClose, close.closeLabel);
    }
    static void OnCancel(Layer* self, Ctx* cx, const ClickEvent*) {
        if (!self) return;
        Close close = self->Closing(cx);
        close.relay.Invoke(cx, close.relay.onCancel,
                           self->surface == Surface::Dialog
                               ? "Dialog.on_cancel callback failed"
                               : "AlertDialog.on_cancel callback failed");
        WindowCloseDialog(cx);
        close.relay.Invoke(cx, close.relay.onClose, close.closeLabel);
    }
    // A dialog's close without an answer, and a sheet's close.
    static void OnClose(Layer* self, Ctx* cx, const ClickEvent*) {
        if (!self) return;
        Close close = self->Closing(cx);
        if (self->surface == Surface::Sheet)
            WindowCloseSheet(cx);
        else
            WindowCloseDialog(cx);
        close.relay.Invoke(cx, close.relay.onClose, close.closeLabel);
    }

    static El* Render(Layer* self, Ctx* cx) {
        Arena* a = cx->a;
        Listener ok = Listen(cx, &Layer::OnOk);
        Listener cancel = Listen(cx, &Layer::OnCancel);
        Listener close = Listen(cx, &Layer::OnClose);
        WinSize size = WindowSize(cx->win);
        switch (self->surface) {
            case Surface::Dialog: {
                // No footer: the Rust Dialog shows no action row unless one
                // is given, and keeps its corner close button.
                El* body = Div(a)->FlexCol()->W(kFill)->PadY(16)->Gap(16);
                if (self->title.s) {
                    body->Child(component::DialogTitle::New(cx)
                                    ->Child(TextEl(a, self->title))
                                    ->IntoEl()
                                    ->PadX(16));
                }
                body->Child(component::DialogContent::New(cx)
                                ->Child(self->Content(cx))
                                ->IntoEl()
                                ->PadX(16));
                return component::Dialog::New(cx)
                    ->Open(true)
                    ->Surface(body)
                    ->CloseButton(true)
                    ->OnOk(ok)
                    ->OnCancel(cancel)
                    ->OnClose(close)
                    ->IntoEl(size);
            }
            case Surface::AlertDialog: {
                component::AlertDialog* alert = component::AlertDialog::New(cx)
                                                    ->Open(true);
                if (self->title.s) alert->Title(self->title);
                if (self->description.s) alert->Description(self->description);
                component::DialogButtonProps props;
                props.ShowCancel(self->showCancel)->OnOk(ok)->OnCancel(cancel);
                return alert->ButtonProps(props)->OnClose(close)->IntoEl(size);
            }
            case Surface::Sheet:
            case Surface::Notification: {
                component::Sheet* sheet = component::Sheet::New(cx)
                                              ->Open(true)
                                              ->Placement(self->placement);
                if (self->title.s) sheet->Title(self->title);
                return sheet->Child(self->Content(cx))
                    ->OnClose(close)
                    ->IntoEl(size);
            }
        }
        return Div(a);
    }
};

// What the Button's on_click closure captured, for this frame.
struct TriggerClick {
    Surface surface = Surface::Dialog;
    shell::ComponentWindowEffects effects;
    Entity<Relay> relay = {};
    Str key;
    Str id;
    // The last of each op, as the closure folds them.
    Str title;
    Str description;
    bool showCancel = false;
    SheetPlacement placement = SheetPlacement::Right;
    NotificationType type = NotificationType::Info;
    bool autohide = true;
    // Filled at click time.
    Ctx* cx = nullptr;
};

static Entity<Layer> NewLayer(Ctx* cx, const TriggerClick* click) {
    Entity<Layer> handle = EntityNew<Layer>(cx->app);
    Layer* layer = handle.Get(cx);
    if (!layer) return handle;
    layer->surface = click->surface;
    layer->relay = click->relay;
    layer->arena = ArenaNew();
    if (click->title.s) layer->title = StrDup(layer->arena, click->title);
    if (click->description.s)
        layer->description = StrDup(layer->arena, click->description);
    layer->showCancel = click->showCancel;
    layer->placement = click->placement;
    return handle;
}

// The keyed effect body: open the surface.
static bool Open(void* user, Window*, App*, Str*, Arena*) {
    TriggerClick* click = (TriggerClick*)user;
    Ctx* cx = click->cx;
    switch (click->surface) {
        case Surface::Dialog:
        case Surface::AlertDialog:
            WindowOpenDialog(cx, NewLayer(cx, click), true);
            break;
        case Surface::Sheet:
            // open_sheet_at, at the sheet's own 350px along the placement
            // (component::Sheet::size's default).
            WindowOpenSheetAt(cx, NewLayer(cx, click), click->placement, 350);
            break;
        case Surface::Notification: {
            component::Notification notification =
                component::Notification::New();
            notification.Id1<NotificationTag>(click->id)
                .WithType(click->type)
                .Autohide(click->autohide);
            if (click->title.s) notification.Title(click->title);
            if (click->description.s) notification.Message(click->description);
            notification
                .OnClick(ListenTo(click->relay, &Relay::OnNotificationClick))
                .OnClose(ListenTo(click->relay, &Relay::OnNotificationClose));
            WindowPushNotification(cx, notification);
            break;
        }
    }
    return true;
}

static bool RunEvent(shell::ComponentEventEffects* effects, void* user,
                     Str* error) {
    TriggerClick* click = (TriggerClick*)user;
    bool executed = false;
    return effects->RunOnce(click->key, &Open, user, &executed, error);
}

static void RunClick(const shell::ComponentEventBinding* binding, ScriptView*,
                     Ctx* cx, const void*) {
    TriggerClick* click = (TriggerClick*)binding->user;
    click->cx = cx;
    Arena* a = ArenaNew();
    Str error;
    // `let _ = effects.event(..)`: a failure has been reported to the script.
    click->effects.Event(cx->win, cx->app, &RunEvent, click, &error, a);
    ArenaDelete(a);
}

// reject_named_slots: a named slot counts whether it was given as an element
// or as a factory.
static bool RejectNamedSlots(MaterializeRequest* request,
                             Slice<const char*> names, const char* message) {
    int count = 0;
    for (const char* name : names) {
        while (request->TakeSlotFactory(name).IsSet()) count++;
    }
    if (count != 0) {
        request->Fail(Str(message));
        return false;
    }
    return true;
}

static El* Materialize(MaterializeRequest* request, Surface surface) {
    const Trigger* trigger = request->PayloadAs<Trigger>();
    if (!trigger)
        return request
            ->Fail(StrL("window effect received an incompatible payload"));
    if (request->ChildrenLen() != 0)
        return request
            ->Fail(StrL("window effect triggers do not accept children"));
    Ctx* cx = request->cx;
    shell::ComponentCallback reporter =
        request->ResolveCallback(trigger->reporter);
    if (!reporter.IsSet())
        return request->Fail(StrL("component argument is not a callback"));
    TriggerClick* click = ArenaNew<TriggerClick>(cx->a);
    click->surface = surface;
    click->effects = reporter.WindowEffects(request->runtime, cx->a);
    // The operations, folded last-call-wins.
    const ComponentArgument* onOk = nullptr;
    const ComponentArgument* onCancel = nullptr;
    const ComponentArgument* onClose = nullptr;
    const ComponentArgument* onClick = nullptr;
    EachMethod<Op>(request, [&](const Op& op) {
        switch (op.kind) {
            case Op::Title:
                click->title = op.text;
                break;
            case Op::Description:
                click->description = op.text;
                break;
            case Op::Placement:
                click->placement = op.placement;
                break;
            case Op::Type:
                click->type = op.type;
                break;
            case Op::Autohide:
                click->autohide = op.flag;
                break;
            case Op::ShowCancel:
                click->showCancel = op.flag;
                break;
            case Op::OnOk:
                onOk = &op.callback;
                break;
            case Op::OnCancel:
                onCancel = &op.callback;
                break;
            case Op::OnClose:
                onClose = &op.callback;
                break;
            case Op::OnClick:
                onClick = &op.callback;
                break;
        }
    });
    shell::ComponentElementFactory content = {};
    if (surface == Surface::Dialog || surface == Surface::Sheet) {
        static constexpr const char* kRejected[] = {"trigger", "header",
                                                    "footer"};
        if (!RejectNamedSlots(
                request, kRejected,
                "Dialog and Sheet accept only the content named slot"))
            return nullptr;
        // take_content_factory. Repeated content(..) calls are folded by the
        // shell, the last one winning.
        content = request->TakeSlotFactory("content");
        if (!content.IsSet())
            return request
                ->Fail(StrL("Dialog and Sheet require exactly one "
                            "content(element) named slot"));
    } else {
        static constexpr const char* kRejected[] = {"content", "trigger",
                                                    "header", "footer"};
        if (!RejectNamedSlots(
                request, kRejected,
                "AlertDialog and Notification do not accept named slots"))
            return nullptr;
    }

    click->id = trigger->id;
    click->key = StrDup(cx->a, fmt("window-effect:%s", trigger->id));
    click->relay =
        UseKeyedState<Relay>(cx, click->key, StrL("shell-window-effect"));
    if (Relay* relay = click->relay.Get(cx)) {
        relay->runtime = request->runtime;
        relay->view = cx->self;
        relay->frame = cx->win ? cx->win->frameSeq : 0;
        relay->specs = request->specs;
        relay->error = request->error;
        relay->content = content;
        relay->reporter = reporter;
        relay->onOk =
            onOk ? request->ResolveCallback(*onOk) : shell::ComponentCallback{};
        relay->onCancel = onCancel ? request->ResolveCallback(*onCancel)
                                   : shell::ComponentCallback{};
        relay->onClose = onClose ? request->ResolveCallback(*onClose)
                                 : shell::ComponentCallback{};
        relay->onClick = onClick ? request->ResolveCallback(*onClick)
                                 : shell::ComponentCallback{};
    }
    component::Button* button =
        component::Button::New(cx, trigger->id)
            ->Label(trigger->label)
            ->OnClick(shell::ComponentListener(cx, &RunClick, reporter, click));
    return request->ApplyStyle(button->IntoEl());
}

static El* MaterializeDialog(MaterializeRequest* request) {
    return Materialize(request, Surface::Dialog);
}
static El* MaterializeAlertDialog(MaterializeRequest* request) {
    return Materialize(request, Surface::AlertDialog);
}
static El* MaterializeSheet(MaterializeRequest* request) {
    return Materialize(request, Surface::Sheet);
}
static El* MaterializeNotification(MaterializeRequest* request) {
    return Materialize(request, Surface::Notification);
}

// ─── Recorders ─────────────────────────────────────────────────────────────

static bool IsNonEmptyText(const ComponentArgument& arg) {
    return arg.kind == shell::ComponentArgumentKind::String &&
           len(StrTrim(arg.string)) != 0;
}

// descriptor(name, ..)'s constructor.
template <int N>
static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    static constexpr const char* kNames[] = {"Dialog", "AlertDialog", "Sheet",
                                             "Notification"};
    if (count != 3 || !IsNonEmptyText(args[0]) || !IsNonEmptyText(args[1]) ||
        args[2].kind != shell::ComponentArgumentKind::Callback) {
        return build
            ->Fail(fmt("%s(id, label, on_effect_error) expects two "
                       "non-empty strings and a callback",
                       Str(kNames[N])));
    }
    Trigger* trigger = build->New<Trigger>();
    trigger->id = args[0].string;
    trigger->label = args[1].string;
    trigger->reporter = args[2];
    return true;
}

// text(name, op): false, with nothing recorded, when the text is empty; the
// caller names the method in the failure.
template <Op::Kind K>
static bool RecordText(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || !IsNonEmptyText(args[0])) return false;
    Op* op = build->New<Op>();
    op->kind = K;
    op->text = args[0].string;
    return true;
}

static bool TextFailure(PayloadBuild* build, const char* name) {
    return build->Fail(fmt("%s(text) expects non-empty text", Str(name)));
}
static bool RecordTitle(PayloadBuild* build, const ComponentArgument* args,
                        int count) {
    return RecordText<Op::Title>(build, args, count) ||
           TextFailure(build, "title");
}
static bool RecordDescription(PayloadBuild* build,
                              const ComponentArgument* args, int count) {
    return RecordText<Op::Description>(build, args, count) ||
           TextFailure(build, "description");
}
static bool RecordMessage(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    return RecordText<Op::Description>(build, args, count) ||
           TextFailure(build, "message");
}

// callback_method(name, op).
template <Op::Kind K>
static bool RecordCallback(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Callback) {
        const char* name = K == Op::OnOk       ? "on_ok"
                           : K == Op::OnCancel ? "on_cancel"
                           : K == Op::OnClose  ? "on_close"
                                               : "on_click";
        return build->Fail(fmt("%s(callback) expects a callback", Str(name)));
    }
    Op* op = build->New<Op>();
    op->kind = K;
    op->callback = args[0];
    return true;
}

static bool RecordShowCancel(PayloadBuild* build, const ComponentArgument* args,
                             int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(StrL("show_cancel(value) expects boolean"));
    Op* op = build->New<Op>();
    op->kind = Op::ShowCancel;
    op->flag = args[0].boolean;
    return true;
}

static bool RecordAutohide(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(StrL("autohide(value) expects boolean"));
    Op* op = build->New<Op>();
    op->kind = Op::Autohide;
    op->flag = args[0].boolean;
    return true;
}

static constexpr const char* kPlacementLiterals[] = {"top", "right", "bottom",
                                                     "left"};
static bool RecordPlacement(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build
            ->Fail(StrL("placement expects top, right, bottom, or left"));
    static constexpr SheetPlacement kPlacements[] = {
        SheetPlacement::Top, SheetPlacement::Right, SheetPlacement::Bottom,
        SheetPlacement::Left};
    for (int i = 0; i < 4; i++) {
        if (StrEq(args[0].string, kPlacementLiterals[i])) {
            Op* op = build->New<Op>();
            op->kind = Op::Placement;
            op->placement = kPlacements[i];
            return true;
        }
    }
    return build->Fail(StrL("unsupported placement"));
}

static constexpr const char* kTypeLiterals[] = {"info", "success", "warning",
                                                "error"};
static bool RecordType(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build
            ->Fail(StrL("type expects info, success, warning, or error"));
    static constexpr NotificationType kTypes[] = {
        NotificationType::Info, NotificationType::Success,
        NotificationType::Warning, NotificationType::Error};
    for (int i = 0; i < 4; i++) {
        if (StrEq(args[0].string, kTypeLiterals[i])) {
            Op* op = build->New<Op>();
            op->kind = Op::Type;
            op->type = kTypes[i];
            return true;
        }
    }
    return build->Fail(StrL("unsupported notification type"));
}

// descriptor(): the shared sentence every method without its own takes.
static constexpr const char* kMethodDocumentation =
    "Configures this native window effect.";
static constexpr const char* kDocumentation =
    "A real button-triggered native window effect; on_effect_error receives "
    "asynchronous effect failures.";

static constexpr ArgumentDescriptor kArguments[] = {
    {"id", SchemaString()},
    {"label", SchemaString()},
    {"on_effect_error",
     SchemaCallback("(message: string, cx: Context) => void")},
};
static constexpr ArgumentDescriptor kTextArgs[] = {{"text", SchemaString()}};
static constexpr ArgumentDescriptor kCallbackArgs[] = {
    {"callback", SchemaCallback("(cx: Context) => void")}};
static constexpr ArgumentDescriptor kValueArgs[] = {{"value", SchemaBoolean()}};
static constexpr ArgumentDescriptor kPlacementArgs[] = {
    {"placement", SchemaEnum(kPlacementLiterals)}};
static constexpr ArgumentDescriptor kTypeArgs[] = {
    {"type", SchemaEnum(kTypeLiterals)}};

static constexpr MethodDescriptor kTitle = {"title", kTextArgs,
                                            kMethodDocumentation, &RecordTitle};
static constexpr MethodDescriptor kOnOk = {
    "on_ok", kCallbackArgs, kMethodDocumentation, &RecordCallback<Op::OnOk>};
static constexpr MethodDescriptor kOnCancel = {"on_cancel", kCallbackArgs,
                                               kMethodDocumentation,
                                               &RecordCallback<Op::OnCancel>};
static constexpr MethodDescriptor kOnClose = {"on_close", kCallbackArgs,
                                              kMethodDocumentation,
                                              &RecordCallback<Op::OnClose>};

static constexpr ConstructorDescriptor kDialogConstructors[] = {
    {"Dialog", kArguments, &Construct<0>}};
static constexpr MethodDescriptor kDialogMethods[] = {kTitle, kOnOk, kOnCancel,
                                                      kOnClose};
static constexpr ComponentDescriptor kDialog = {"Dialog", kDialogConstructors,
                                                kDialogMethods, kDocumentation,
                                                &MaterializeDialog};

static constexpr ConstructorDescriptor kAlertConstructors[] = {
    {"AlertDialog", kArguments, &Construct<1>}};
static constexpr MethodDescriptor kAlertMethods[] = {
    kTitle,
    {"description", kTextArgs, kMethodDocumentation, &RecordDescription},
    {"show_cancel", kValueArgs, kMethodDocumentation, &RecordShowCancel},
    kOnOk,
    kOnCancel,
    kOnClose,
};
static constexpr ComponentDescriptor kAlertDialog = {
    "AlertDialog", kAlertConstructors, kAlertMethods, kDocumentation,
    &MaterializeAlertDialog};

static constexpr ConstructorDescriptor kSheetConstructors[] = {
    {"Sheet", kArguments, &Construct<2>}};
static constexpr MethodDescriptor kSheetMethods[] = {
    kTitle,
    {"placement", kPlacementArgs, kMethodDocumentation, &RecordPlacement},
    kOnClose,
};
static constexpr ComponentDescriptor kSheet = {"Sheet", kSheetConstructors,
                                               kSheetMethods, kDocumentation,
                                               &MaterializeSheet};

static constexpr ConstructorDescriptor kNotificationConstructors[] = {
    {"Notification", kArguments, &Construct<3>}};
static constexpr MethodDescriptor kNotificationMethods[] = {
    kTitle,
    {"message", kTextArgs, kMethodDocumentation, &RecordMessage},
    {"type", kTypeArgs, kMethodDocumentation, &RecordType},
    {"on_click", kCallbackArgs, kMethodDocumentation,
     &RecordCallback<Op::OnClick>},
    kOnClose,
    {"autohide", kValueArgs, kMethodDocumentation, &RecordAutohide},
};
static constexpr ComponentDescriptor kNotification = {
    "Notification", kNotificationConstructors, kNotificationMethods,
    kDocumentation, &MaterializeNotification};

} // namespace gpui::component_shell::window_effects

namespace gpui::component_shell {

void SetWindowEffectsReporterFailureProbe(
    WindowEffectsReporterFailureProbe probe) {
    window_effects::gReporterProbe = probe;
}

bool RegisterWindowEffects(shell::ComponentRegistry* registry,
                           shell::RegistryError* error) {
    using namespace window_effects;
    return registry->Register(&kDialog, error) &&
           registry->Register(&kAlertDialog, error) &&
           registry->Register(&kSheet, error) &&
           registry->Register(&kNotification, error);
}

} // namespace gpui::component_shell
