// crates/component-shell/src/shell/display/clipboard.rs

#include "component_shell/families.h"
#include "component_shell/display/common.h"
#include "ui/clipboard.h"

namespace gpui::component_shell::display::clipboard {

struct ClipboardPayload {
    Str id;
};

struct ClipboardOp {
    enum Kind : uint8_t {
        Value,
        Tooltip,
    } kind = Value;
    Str text;
};

static bool Construct(PayloadBuild* build, const ComponentArgument* args, int) {
    if (!common::NonEmptyId(build, "Clipboard", args[0].string)) return false;
    build->New<ClipboardPayload>()->id = args[0].string;
    return true;
}

template <ClipboardOp::Kind K>
static bool RecordString(PayloadBuild* build, const ComponentArgument* args,
                         int) {
    ClipboardOp* op = build->New<ClipboardOp>();
    op->kind = K;
    op->text = args[0].string;
    return true;
}

static component::Clipboard* Component(MaterializeRequest* request) {
    const ClipboardPayload* payload = request->PayloadAs<ClipboardPayload>();
    if (!payload) {
        request->Fail(StrL("Clipboard received an incompatible payload"));
        return nullptr;
    }
    component::Clipboard* clipboard =
        component::Clipboard::New(request->cx, payload->id);
    EachMethod<ClipboardOp>(request, [&](const ClipboardOp& op) {
        switch (op.kind) {
            case ClipboardOp::Value:
                clipboard->Value(op.text);
                break;
            case ClipboardOp::Tooltip:
                clipboard->Tooltip(op.text);
                break;
        }
    });
    return clipboard;
}

static El* Materialize(MaterializeRequest* request) {
    if (!common::EnsureNoChildren(request, "Clipboard")) return nullptr;
    component::Clipboard* clipboard = Component(request);
    if (!clipboard) return nullptr;
    return request->Finish(Div(request->cx->a)->Child(clipboard->IntoEl()));
}

static constexpr ArgumentDescriptor kIdArgs[] = {{"id", SchemaString()}};
static constexpr ArgumentDescriptor kValueArgs[] = {{"value", SchemaString()}};
static constexpr ArgumentDescriptor kTooltipArgs[] = {
    {"tooltip", SchemaString()}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Clipboard", kIdArgs, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"value", kValueArgs, "Sets the text copied when the button is pressed.",
     &RecordString<ClipboardOp::Value>},
    {"tooltip", kTooltipArgs, "Sets the copy button tooltip.",
     &RecordString<ClipboardOp::Tooltip>},
};
static constexpr ComponentDescriptor kClipboard = {
    "Clipboard", kConstructors, kMethods,
    "A button that copies a configured string to the system clipboard.",
    &Materialize};

} // namespace gpui::component_shell::display::clipboard

namespace gpui::component_shell {

bool RegisterDisplayClipboard(shell::ComponentRegistry* registry,
                              shell::RegistryError* error) {
    return registry->Register(&display::clipboard::kClipboard, error);
}

} // namespace gpui::component_shell
