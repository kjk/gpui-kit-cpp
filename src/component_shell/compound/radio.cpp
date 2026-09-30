// crates/component-shell/src/shell/compound/radio.rs

#include "component_shell/families.h"
#include "component_shell/compound/common.h"
#include "component_shell/typed_compound/mod.h"
#include "shell/view.h"
#include "ui/radio.h"

namespace gpui::component_shell::compound::radio {

struct RadioPayload {
    Str id;
};

struct RadioOp {
    enum Kind : uint8_t {
        OnChange,
        Label,
        A11y,
        Checked,
        TabStop,
        Size,
    } kind = OnChange;
    ComponentArgument change = {};
    Str text;
    bool flag = false;
    UiSize size = UiSize::Medium;
};

static bool Construct(PayloadBuild* build, const ComponentArgument* args, int) {
    Str error;
    if (!common::NonemptyId(args[0].string, "Radio", &error))
        return build->Fail(error);
    build->New<RadioPayload>()->id = args[0].string;
    return true;
}

template <RadioOp::Kind K>
static bool RecordString(PayloadBuild* build, const ComponentArgument* args,
                         int) {
    RadioOp* op = build->New<RadioOp>();
    op->kind = K;
    op->text = args[0].string;
    return true;
}

template <RadioOp::Kind K>
static bool RecordBool(PayloadBuild* build, const ComponentArgument* args,
                       int) {
    RadioOp* op = build->New<RadioOp>();
    op->kind = K;
    op->flag = args[0].boolean;
    return true;
}

static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int) {
    RadioOp* op = build->New<RadioOp>();
    op->kind = RadioOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

static bool RecordChange(PayloadBuild* build, const ComponentArgument* args,
                         int) {
    RadioOp* op = build->New<RadioOp>();
    op->kind = RadioOp::OnChange;
    op->change = args[0];
    return true;
}

// The on_change closure: the state the click leaves the radio in, which the
// component fills in.
static void RunChange(const shell::ComponentEventBinding* binding,
                      ScriptView* view, Ctx* cx, const void*) {
    shell::ComponentDataValue checked =
        shell::ComponentDataValue::Boolean(binding->value != 0);
    binding->callback
        .InvokeAndReport(view->runtime, "Radio.on_change callback failed",
                         &checked, 1, cx->win, cx->app);
}

static El* Materialize(MaterializeRequest* request) {
    const RadioPayload* payload = request->PayloadAs<RadioPayload>();
    if (!payload)
        return request->Fail(StrL("Radio received an incompatible payload"));
    // A `Radio` inside a `RadioGroup` is driven by the group, which reports
    // the selected index itself. One standing on its own has nothing above
    // it, so it reports its own click.
    const ComponentArgument* change = nullptr;
    EachMethod<RadioOp>(request, [&](const RadioOp& op) {
        if (op.kind == RadioOp::OnChange) change = &op.change;
    });
    component::Radio* radio = component::Radio::New(request->cx, payload->id)
                                  ->Disabled(request->disabled)
                                  ->Checked(request->selected);
    EachMethod<RadioOp>(request, [&](const RadioOp& op) {
        switch (op.kind) {
            case RadioOp::Label:
                radio->Label(op.text);
                break;
            case RadioOp::A11y:
                radio->AccessibilityLabel(op.text);
                break;
            case RadioOp::Checked:
                radio->Checked(op.flag);
                break;
            case RadioOp::TabStop:
                radio->TabStop(op.flag);
                break;
            case RadioOp::Size:
                radio->WithSize(op.size);
                break;
            case RadioOp::OnChange:
                break;
        }
    });
    if (change) {
        radio->OnChange(shell::ComponentValueListener(
            request->cx, request->elementId, &RunChange,
            request->ResolveCallback(*change)));
    }
    return typed_compound::FinishPart(request, radio);
}

static constexpr ArgumentDescriptor kIdArgs[] = {{"id", SchemaString()}};
static constexpr ArgumentDescriptor kLabelArgs[] = {{"label", SchemaString()}};
static constexpr ArgumentDescriptor kA11yArgs[] = {
    {"accessibility_label", SchemaString()}};
static constexpr ArgumentDescriptor kCheckedArgs[] = {
    {"checked", SchemaBoolean()}};
static constexpr ArgumentDescriptor kTabStopArgs[] = {
    {"tab_stop", SchemaBoolean()}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};
static constexpr ArgumentDescriptor kChangeArgs[] = {
    {"on_change", SchemaCallback("(checked: boolean, cx: Context) => void")}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Radio", kIdArgs, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"label", kLabelArgs, "Sets the visible label.",
     &RecordString<RadioOp::Label>},
    {"accessibility_label", kA11yArgs, "Overrides the announced name.",
     &RecordString<RadioOp::A11y>},
    {"checked", kCheckedArgs, "Controls checked state.",
     &RecordBool<RadioOp::Checked>},
    {"tab_stop", kTabStopArgs, "Controls keyboard tab-stop participation.",
     &RecordBool<RadioOp::TabStop>},
    {"size", kSizeArgs, "Sets semantic size.", &RecordSize},
    {"on_change", kChangeArgs,
     "Reports a click on a radio used on its own. Inside a `RadioGroup` the "
     "group reports the selected index instead.",
     &RecordChange},
};
static constexpr ComponentDescriptor kRadio = {
    "Radio", kConstructors, kMethods,
    "A controlled radio control; selected and disabled common behavior is "
    "supported.",
    &Materialize};

} // namespace gpui::component_shell::compound::radio

namespace gpui::component_shell {

bool RegisterCompoundRadio(shell::ComponentRegistry* registry,
                           shell::RegistryError* error) {
    return registry->Register(&compound::radio::kRadio, error);
}

} // namespace gpui::component_shell
