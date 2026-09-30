// crates/component-shell/src/shell/separator.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "ui/separator.h"

namespace gpui::component_shell::separator {

enum class SeparatorPayload : uint8_t {
    Horizontal,
    Vertical,
    HorizontalDashed,
    VerticalDashed,
};

struct SeparatorOp {
    enum Kind : uint8_t {
        Label,
        Color,
        Dashed,
    } kind = Label;
    Str label;
    Rgba color = {};
};

template <SeparatorPayload P>
static bool Construct(PayloadBuild* build, const ComponentArgument*, int) {
    *build->New<SeparatorPayload>() = P;
    return true;
}

static bool RecordLabel(PayloadBuild* build, const ComponentArgument* args,
                        int) {
    SeparatorOp* op = build->New<SeparatorOp>();
    op->kind = SeparatorOp::Label;
    op->label = args[0].string;
    return true;
}

static bool RecordColor(PayloadBuild* build, const ComponentArgument* args,
                        int) {
    Rgba color = {};
    if (!ParseColorArgument(build, "Separator", args[0].string, &color))
        return false;
    SeparatorOp* op = build->New<SeparatorOp>();
    op->kind = SeparatorOp::Color;
    op->color = color;
    return true;
}

static bool RecordDashed(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<SeparatorOp>()->kind = SeparatorOp::Dashed;
    return true;
}

static El* Materialize(MaterializeRequest* request) {
    const SeparatorPayload* payload = request->PayloadAs<SeparatorPayload>();
    if (!payload)
        return request
            ->Fail(StrL("Separator received an incompatible "
                        "payload"));
    Ctx* cx = request->cx;
    bool vertical = *payload == SeparatorPayload::Vertical ||
                    *payload == SeparatorPayload::VerticalDashed;
    component::Separator* separator =
        vertical ? component::Separator::Vertical(cx)
                 : component::Separator::Horizontal(cx);
    if (*payload == SeparatorPayload::HorizontalDashed ||
        *payload == SeparatorPayload::VerticalDashed)
        separator->Dashed();
    EachMethod<SeparatorOp>(request, [&](const SeparatorOp& op) {
        switch (op.kind) {
            case SeparatorOp::Label:
                separator->Label(op.label);
                break;
            case SeparatorOp::Color:
                separator->Color(op.color);
                break;
            case SeparatorOp::Dashed:
                separator->Dashed();
                break;
        }
    });
    return request->ApplyStyle(separator->IntoEl());
}

static constexpr ArgumentDescriptor kLabelArgs[] = {{"label", SchemaString()}};
static constexpr ArgumentDescriptor kColorArgs[] = {{"color", SchemaString()}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Separator", {}, &Construct<SeparatorPayload::Horizontal>},
    {"VerticalSeparator", {}, &Construct<SeparatorPayload::Vertical>},
    {"DashedSeparator", {}, &Construct<SeparatorPayload::HorizontalDashed>},
    {"VerticalDashedSeparator",
     {},
     &Construct<SeparatorPayload::VerticalDashed>},
};
static constexpr MethodDescriptor kMethods[] = {
    {"label", kLabelArgs, "Displays text centered over the separator line.",
     &RecordLabel},
    {"color", kColorArgs, "Sets the separator line color.", &RecordColor},
    {"dashed", {}, "Uses a dashed separator line.", &RecordDashed},
};
static constexpr ComponentDescriptor kSeparator = {
    "Separator", kConstructors, kMethods,
    "A horizontal or vertical, solid or dashed separator.", &Materialize};

} // namespace gpui::component_shell::separator

namespace gpui::component_shell {

bool RegisterSeparator(shell::ComponentRegistry* registry,
                       shell::RegistryError* error) {
    return registry->Register(&separator::kSeparator, error);
}

} // namespace gpui::component_shell
