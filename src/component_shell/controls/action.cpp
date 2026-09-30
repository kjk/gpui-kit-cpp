// crates/component-shell/src/shell/controls/action.rs

#include "component_shell/families.h"
#include "component_shell/controls/support.h"
#include "shell/view.h"
#include "ui/button.h"
#include "ui/checkbox.h"
#include "ui/switch.h"

namespace gpui::component_shell::controls::action {

using support::CommonOp;

struct IdPayload {
    Str id;
};

struct ButtonOp {
    enum Kind : uint8_t {
        Size,
        Label,
        Tooltip,
        Loading,
        Outline,
        Primary,
        Secondary,
        Danger,
        Success,
        Warning,
        Ghost,
        Link,
        Compact,
    } kind = Size;
    UiSize size = UiSize::Medium;
    Str text;
    bool loading = false;
};

// id_constructor / validate_id: an identity control's id must not be empty.
static bool ConstructId(PayloadBuild* build, const ComponentArgument* args,
                        const char* exportName) {
    if (len(args[0].string) == 0)
        return build->Fail(fmt("%s id must not be empty", Str(exportName)));
    build->New<IdPayload>()->id = args[0].string;
    return true;
}

static bool ConstructButton(PayloadBuild* build, const ComponentArgument* args,
                            int) {
    return ConstructId(build, args, "Button");
}
static bool ConstructCheckbox(PayloadBuild* build,
                              const ComponentArgument* args, int) {
    return ConstructId(build, args, "Checkbox");
}
static bool ConstructSwitch(PayloadBuild* build, const ComponentArgument* args,
                            int) {
    return ConstructId(build, args, "Switch");
}
static bool ConstructToggle(PayloadBuild* build, const ComponentArgument* args,
                            int) {
    return ConstructId(build, args, "Toggle");
}

template <ButtonOp::Kind K>
static bool RecordVariant(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<ButtonOp>()->kind = K;
    return true;
}

template <ButtonOp::Kind K>
static bool RecordButtonString(PayloadBuild* build,
                               const ComponentArgument* args, int) {
    ButtonOp* op = build->New<ButtonOp>();
    op->kind = K;
    op->text = args[0].string;
    return true;
}

static bool RecordButtonLoading(PayloadBuild* build,
                                const ComponentArgument* args, int) {
    ButtonOp* op = build->New<ButtonOp>();
    op->kind = ButtonOp::Loading;
    op->loading = args[0].boolean;
    return true;
}

static bool RecordButtonSize(PayloadBuild* build, const ComponentArgument* args,
                             int) {
    ButtonOp* op = build->New<ButtonOp>();
    op->kind = ButtonOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

static El* MaterializeButton(MaterializeRequest* request) {
    const IdPayload* id = request->PayloadAs<IdPayload>();
    if (!id)
        return request->Fail(StrL("Button received an incompatible payload"));
    Ctx* cx = request->cx;
    component::Button* button = component::Button::New(cx, id->id)
                                    ->Disabled(request->disabled)
                                    ->Selected(request->selected);
    EachMethod<ButtonOp>(request, [&](const ButtonOp& op) {
        switch (op.kind) {
            case ButtonOp::Size:
                button->WithSize(op.size);
                break;
            case ButtonOp::Label:
                button->Label(op.text);
                break;
            case ButtonOp::Tooltip:
                button->Tooltip(op.text);
                break;
            case ButtonOp::Loading:
                button->Loading(op.loading);
                break;
            case ButtonOp::Outline:
                button->Outline();
                break;
            case ButtonOp::Primary:
                button->Primary();
                break;
            case ButtonOp::Secondary:
                button->Secondary();
                break;
            case ButtonOp::Danger:
                button->Danger();
                break;
            case ButtonOp::Success:
                button->Success();
                break;
            case ButtonOp::Warning:
                button->Warning();
                break;
            case ButtonOp::Ghost:
                button->Ghost();
                break;
            case ButtonOp::Link:
                button->Link();
                break;
            case ButtonOp::Compact:
                button->Compact();
                break;
        }
    });
    if (request->onClick)
        button->OnClick(
            Listen(cx, &ScriptView::OnClick, (intptr_t)request->onClick));
    // request.finish(component): the children go into the button, the style
    // onto the element it renders.
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    button->Children(children, count);
    return request->ApplyStyle(button->IntoEl());
}

// The on_change closure: `invoke_and_report_with(context, [checked])`. The
// controls are controlled, so the state a click produces is `!checked` as
// this frame rendered it — which is what the Rust handler is handed — and it
// is bound here rather than filled in by the control.
static void RunChange(const shell::ComponentEventBinding* binding,
                      ScriptView* view, Ctx* cx, const void*) {
    shell::ComponentDataValue checked =
        shell::ComponentDataValue::Boolean(binding->value != 0);
    binding->callback.InvokeAndReport(view->runtime, (const char*)binding->user,
                                      &checked, 1, cx->win, cx->app);
}

static Listener ChangeListener(MaterializeRequest* request,
                               const shell::ComponentArgument* change,
                               const char* context, bool checked) {
    return shell::ComponentListener(request->cx, &RunChange,
                                    request->ResolveCallback(*change),
                                    (void*)context, checked ? 0 : 1);
}

static El* MaterializeCheckbox(MaterializeRequest* request) {
    const IdPayload* id = request->PayloadAs<IdPayload>();
    if (!id)
        return request->Fail(StrL("Checkbox received an incompatible payload"));
    Ctx* cx = request->cx;
    const shell::ComponentArgument* change = nullptr;
    component::Checkbox* checkbox = component::Checkbox::New(cx, id->id)
                                        ->Disabled(request->disabled)
                                        ->Checked(request->selected);
    EachMethod<CommonOp>(request, [&](const CommonOp& op) {
        switch (op.kind) {
            case CommonOp::Size:
                checkbox->WithSize(op.size);
                break;
            case CommonOp::Label:
                checkbox->Label(op.text);
                break;
            case CommonOp::Tooltip:
                checkbox->Tooltip(op.text);
                break;
            case CommonOp::Checked:
                checkbox->Checked(op.checked);
                break;
            case CommonOp::Change:
                change = &op.change;
                break;
            case CommonOp::Outline:
                break;
        }
    });
    if (change)
        checkbox->OnChange(ChangeListener(request, change, "Checkbox.on_change",
                                          checkbox->checked));
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    if (count == 1) {
        checkbox->Child(children[0]);
    } else if (count > 1) {
        // component::Checkbox holds one child under its label where Rust's
        // holds a Vec in the same `v_flex().gap_1()` column; a column with
        // the same gap around them lays them out the same.
        El* column = Div(cx->a)->FlexCol()->Gap(4);
        for (int i = 0; i < count; i++) column->Child(children[i]);
        checkbox->Child(column);
    }
    return request->ApplyStyle(checkbox->IntoEl());
}

static El* MaterializeSwitch(MaterializeRequest* request) {
    const IdPayload* id = request->PayloadAs<IdPayload>();
    if (!id)
        return request->Fail(StrL("Switch received an incompatible payload"));
    Ctx* cx = request->cx;
    const shell::ComponentArgument* change = nullptr;
    component::Switch* control = component::Switch::New(cx, id->id)
                                     ->Disabled(request->disabled)
                                     ->Checked(request->selected);
    EachMethod<CommonOp>(request, [&](const CommonOp& op) {
        switch (op.kind) {
            case CommonOp::Size:
                control->WithSize(op.size);
                break;
            case CommonOp::Label:
                control->Label(op.text);
                break;
            case CommonOp::Tooltip:
                control->Tooltip(op.text);
                break;
            case CommonOp::Checked:
                control->Checked(op.checked);
                break;
            case CommonOp::Change:
                change = &op.change;
                break;
            case CommonOp::Outline:
                break;
        }
    });
    if (change)
        control->OnChange(ChangeListener(request, change, "Switch.on_change",
                                         control->checked));
    // request.finish(div().child(component)): Switch is not a ParentElement,
    // so the wrapper takes the style and the children.
    return request->Finish(Div(cx->a)->Child(control->IntoEl()));
}

static El* MaterializeToggle(MaterializeRequest* request) {
    const IdPayload* id = request->PayloadAs<IdPayload>();
    if (!id)
        return request->Fail(StrL("Toggle received an incompatible payload"));
    Ctx* cx = request->cx;
    const shell::ComponentArgument* change = nullptr;
    component::Toggle* toggle = component::Toggle::New(cx, id->id)
                                    ->Disabled(request->disabled)
                                    ->Checked(request->selected);
    EachMethod<CommonOp>(request, [&](const CommonOp& op) {
        switch (op.kind) {
            case CommonOp::Size:
                toggle->WithSize(op.size);
                break;
            case CommonOp::Label:
                toggle->Label(op.text);
                break;
            case CommonOp::Tooltip:
                toggle->Tooltip(op.text);
                break;
            case CommonOp::Checked:
                toggle->Checked(op.checked);
                break;
            case CommonOp::Outline:
                toggle->Outline();
                break;
            case CommonOp::Change:
                change = &op.change;
                break;
        }
    });
    if (change)
        toggle->OnClick(ChangeListener(request, change, "Toggle.on_change",
                                       toggle->checked));
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) toggle->Child(children[i]);
    return request->ApplyStyle(toggle->IntoEl());
}

static constexpr ArgumentDescriptor kIdArgs[] = {{"id", SchemaString()}};
static constexpr ArgumentDescriptor kLabelArgs[] = {{"label", SchemaString()}};
static constexpr ArgumentDescriptor kTooltipArgs[] = {
    {"tooltip", SchemaString()}};
static constexpr ArgumentDescriptor kLoadingArgs[] = {
    {"loading", SchemaBoolean()}};
static constexpr ArgumentDescriptor kCheckedArgs[] = {
    {"checked", SchemaBoolean()}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};

static constexpr ConstructorDescriptor kButtonConstructors[] = {
    {"Button", kIdArgs, &ConstructButton}};
static constexpr MethodDescriptor kButtonMethods[] = {
    kOnClickMethod,
    kDisabledMethod,
    {"label", kLabelArgs, "Sets the visible button label.",
     &RecordButtonString<ButtonOp::Label>},
    {"tooltip", kTooltipArgs, "Sets concise hover help.",
     &RecordButtonString<ButtonOp::Tooltip>},
    {"loading", kLoadingArgs, "Sets the loading presentation.",
     &RecordButtonLoading},
    {"size", kSizeArgs, "Sets the semantic control size.", &RecordButtonSize},
    {"outline",
     {},
     "Uses the outline presentation.",
     &RecordVariant<ButtonOp::Outline>},
    {"primary",
     {},
     "Uses the primary action variant.",
     &RecordVariant<ButtonOp::Primary>},
    {"secondary",
     {},
     "Uses the secondary variant.",
     &RecordVariant<ButtonOp::Secondary>},
    {"danger",
     {},
     "Uses the destructive-action variant.",
     &RecordVariant<ButtonOp::Danger>},
    {"success",
     {},
     "Uses the success variant.",
     &RecordVariant<ButtonOp::Success>},
    {"warning",
     {},
     "Uses the warning variant.",
     &RecordVariant<ButtonOp::Warning>},
    {"ghost",
     {},
     "Uses the quiet ghost variant.",
     &RecordVariant<ButtonOp::Ghost>},
    {"link",
     {},
     "Uses the link-like visual variant.",
     &RecordVariant<ButtonOp::Link>},
    {"compact",
     {},
     "Uses compact internal spacing.",
     &RecordVariant<ButtonOp::Compact>},
};
static constexpr ComponentDescriptor kButton = {
    "Button", kButtonConstructors, kButtonMethods,
    "A stateless command button. Shell disabled, selected, children, style, "
    "and on_click operations are honored.",
    &MaterializeButton};

// state_methods(component), in its order.
static constexpr MethodDescriptor kLabelMethod = {
    "label", kLabelArgs, "Sets the visible control label.",
    &support::RecordLabel};
static constexpr MethodDescriptor kTooltipMethod = {"tooltip", kTooltipArgs,
                                                    "Sets concise hover help.",
                                                    &support::RecordTooltip};
static constexpr MethodDescriptor kCheckedMethod = {
    "checked", kCheckedArgs, "Sets the controlled checked state.",
    &support::RecordChecked};
static constexpr MethodDescriptor kStateMethods[] = {
    kLabelMethod,         kTooltipMethod,         kCheckedMethod,
    support::kSizeMethod, support::kChangeMethod, kDisabledMethod};
// Toggle alone adds outline_method.
static constexpr MethodDescriptor kToggleMethods[] = {
    kLabelMethod,           kTooltipMethod,         kCheckedMethod,
    support::kSizeMethod,   support::kChangeMethod, kDisabledMethod,
    support::kOutlineMethod};

static constexpr const char* kStateDocumentation =
    "A controlled stateless boolean control. Provide checked explicitly; "
    "boolean change callbacks are not exposed until the shell callback facade "
    "can carry values.";

static constexpr ConstructorDescriptor kCheckboxConstructors[] = {
    {"Checkbox", kIdArgs, &ConstructCheckbox}};
static constexpr ConstructorDescriptor kSwitchConstructors[] = {
    {"Switch", kIdArgs, &ConstructSwitch}};
static constexpr ConstructorDescriptor kToggleConstructors[] = {
    {"Toggle", kIdArgs, &ConstructToggle}};

static constexpr ComponentDescriptor kCheckbox = {
    "Checkbox", kCheckboxConstructors, kStateMethods, kStateDocumentation,
    &MaterializeCheckbox};
static constexpr ComponentDescriptor kSwitch = {
    "Switch", kSwitchConstructors, kStateMethods, kStateDocumentation,
    &MaterializeSwitch};
static constexpr ComponentDescriptor kToggle = {
    "Toggle", kToggleConstructors, kToggleMethods, kStateDocumentation,
    &MaterializeToggle};

} // namespace gpui::component_shell::controls::action

namespace gpui::component_shell {

bool RegisterControlsAction(shell::ComponentRegistry* registry,
                            shell::RegistryError* error) {
    return registry->Register(&controls::action::kButton, error) &&
           registry->Register(&controls::action::kCheckbox, error) &&
           registry->Register(&controls::action::kSwitch, error) &&
           registry->Register(&controls::action::kToggle, error);
}

} // namespace gpui::component_shell
