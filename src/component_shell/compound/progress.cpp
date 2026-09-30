// crates/component-shell/src/shell/compound/progress.rs

#include "component_shell/families.h"
#include "component_shell/compound/common.h"
#include "ui/progress.h"

namespace gpui::component_shell::compound::progress {

struct ProgressPayload {
    Str id;
};

struct ProgressOp {
    enum Kind : uint8_t {
        Value,
        Loading,
        Label,
        Size,
    } kind = Value;
    float value = 0;
    bool loading = false;
    Str label;
    UiSize size = UiSize::Medium;
};

static bool Construct(PayloadBuild* build, const ComponentArgument* args, int) {
    Str error;
    if (!common::NonemptyId(args[0].string, "Progress", &error))
        return build->Fail(error);
    build->New<ProgressPayload>()->id = args[0].string;
    return true;
}

static bool RecordValue(PayloadBuild* build, const ComponentArgument* args,
                        int) {
    float value = 0;
    Str error;
    if (!common::FiniteF32(args[0].number, StrL("Progress.value(value)"),
                           &value, &error))
        return build->Fail(error);
    ProgressOp* op = build->New<ProgressOp>();
    op->kind = ProgressOp::Value;
    op->value = value;
    return true;
}

static bool RecordLoading(PayloadBuild* build, const ComponentArgument* args,
                          int) {
    ProgressOp* op = build->New<ProgressOp>();
    op->kind = ProgressOp::Loading;
    op->loading = args[0].boolean;
    return true;
}

static bool RecordLabel(PayloadBuild* build, const ComponentArgument* args,
                        int) {
    ProgressOp* op = build->New<ProgressOp>();
    op->kind = ProgressOp::Label;
    op->label = args[0].string;
    return true;
}

static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int) {
    ProgressOp* op = build->New<ProgressOp>();
    op->kind = ProgressOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

// ProgressMaterializer::component: the payload check, then every recorded
// operation folded into the builder in script order.
static component::Progress* Component(MaterializeRequest* request) {
    const ProgressPayload* payload = request->PayloadAs<ProgressPayload>();
    if (!payload) {
        request->Fail(StrL("Progress received an incompatible payload"));
        return nullptr;
    }
    component::Progress* progress = component::Progress::New(request->cx)
                                        ->Id(payload->id);
    EachMethod<ProgressOp>(request, [&](const ProgressOp& op) {
        switch (op.kind) {
            case ProgressOp::Value:
                progress->Value(op.value);
                break;
            case ProgressOp::Loading:
                progress->Loading(op.loading);
                break;
            case ProgressOp::Label:
                progress->AccessibilityLabel(op.label);
                break;
            case ProgressOp::Size:
                progress->WithSize(op.size);
                break;
        }
    });
    return progress;
}

static El* Materialize(MaterializeRequest* request) {
    if (request->ChildrenLen() != 0)
        return request->Fail(StrL("Progress does not accept children"));
    component::Progress* progress = Component(request);
    if (!progress) return nullptr;
    El* wrapper = Div(request->cx->a)->Child(progress->IntoEl());
    return request->ApplyStyle(wrapper);
}

static constexpr ArgumentDescriptor kIdArgs[] = {{"id", SchemaString()}};
static constexpr ArgumentDescriptor kValueArgs[] = {{"value", SchemaNumber()}};
static constexpr ArgumentDescriptor kLoadingArgs[] = {
    {"loading", SchemaBoolean()}};
static constexpr ArgumentDescriptor kLabelArgs[] = {
    {"accessibility_label", SchemaString()}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Progress", kIdArgs, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"value", kValueArgs,
     "Sets percentage progress; the component clamps it to 0–100.",
     &RecordValue},
    {"loading", kLoadingArgs, "Enables indeterminate loading animation.",
     &RecordLoading},
    {"accessibility_label", kLabelArgs, "Sets the accessible name.",
     &RecordLabel},
    {"size", kSizeArgs, "Sets the semantic size.", &RecordSize},
};
static constexpr ComponentDescriptor kProgress = {
    "Progress", kConstructors, kMethods,
    "A linear determinate or indeterminate progress indicator.", &Materialize};

} // namespace gpui::component_shell::compound::progress

namespace gpui::component_shell {

bool RegisterCompoundProgress(shell::ComponentRegistry* registry,
                              shell::RegistryError* error) {
    return registry->Register(&compound::progress::kProgress, error);
}

} // namespace gpui::component_shell
