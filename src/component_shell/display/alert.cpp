// crates/component-shell/src/shell/display/alert.rs

#include "component_shell/families.h"
#include "component_shell/display/common.h"
#include "ui/alert.h"

namespace gpui::component_shell::display::alert {

using component::AlertVariant;

struct AlertPayload {
    Str id;
    Str message;
    AlertVariant variant = AlertVariant::Default;
};

struct AlertOp {
    enum Kind : uint8_t {
        Title,
        Banner,
        Visible,
        Size,
    } kind = Title;
    Str title;
    bool visible = true;
    UiSize size = UiSize::Medium;
};

template <AlertVariant V>
static bool Construct(PayloadBuild* build, const ComponentArgument* args, int) {
    if (!common::NonEmptyId(build, "Alert", args[0].string)) return false;
    AlertPayload* payload = build->New<AlertPayload>();
    payload->id = args[0].string;
    payload->message = args[1].string;
    payload->variant = V;
    return true;
}

static bool RecordTitle(PayloadBuild* build, const ComponentArgument* args,
                        int) {
    AlertOp* op = build->New<AlertOp>();
    op->kind = AlertOp::Title;
    op->title = args[0].string;
    return true;
}

static bool RecordBanner(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<AlertOp>()->kind = AlertOp::Banner;
    return true;
}

static bool RecordVisible(PayloadBuild* build, const ComponentArgument* args,
                          int) {
    AlertOp* op = build->New<AlertOp>();
    op->kind = AlertOp::Visible;
    op->visible = args[0].boolean;
    return true;
}

static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int) {
    AlertOp* op = build->New<AlertOp>();
    op->kind = AlertOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

// AlertMaterializer::component: the variant's constructor, then every
// operation in script order.
static component::Alert* Component(MaterializeRequest* request) {
    const AlertPayload* payload = request->PayloadAs<AlertPayload>();
    if (!payload) {
        request->Fail(StrL("Alert received an incompatible payload"));
        return nullptr;
    }
    Ctx* cx = request->cx;
    component::Alert* alert = nullptr;
    switch (payload->variant) {
        case AlertVariant::Default:
            alert = component::Alert::New(cx, payload->id, payload->message);
            break;
        case AlertVariant::Info:
            alert = component::Alert::Info(cx, payload->id, payload->message);
            break;
        case AlertVariant::Success:
            alert =
                component::Alert::Success(cx, payload->id, payload->message);
            break;
        case AlertVariant::Warning:
            alert =
                component::Alert::Warning(cx, payload->id, payload->message);
            break;
        case AlertVariant::Error:
            alert = component::Alert::Error(cx, payload->id, payload->message);
            break;
    }
    EachMethod<AlertOp>(request, [&](const AlertOp& op) {
        switch (op.kind) {
            case AlertOp::Title:
                alert->Title(op.title);
                break;
            case AlertOp::Banner:
                alert->Banner();
                break;
            case AlertOp::Visible:
                alert->Visible(op.visible);
                break;
            case AlertOp::Size:
                alert->WithSize(op.size);
                break;
        }
    });
    return alert;
}

static El* Materialize(MaterializeRequest* request) {
    if (!common::EnsureNoChildren(request, "Alert")) return nullptr;
    component::Alert* alert = Component(request);
    if (!alert) return nullptr;
    return request->ApplyStyle(alert->IntoEl());
}

static constexpr ArgumentDescriptor kConstructorArgs[] = {
    {"id", SchemaString()},
    {"message", SchemaString()}};
static constexpr ArgumentDescriptor kTitleArgs[] = {{"title", SchemaString()}};
static constexpr ArgumentDescriptor kVisibleArgs[] = {
    {"visible", SchemaBoolean()}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Alert", kConstructorArgs, &Construct<AlertVariant::Default>},
    {"InfoAlert", kConstructorArgs, &Construct<AlertVariant::Info>},
    {"SuccessAlert", kConstructorArgs, &Construct<AlertVariant::Success>},
    {"WarningAlert", kConstructorArgs, &Construct<AlertVariant::Warning>},
    {"ErrorAlert", kConstructorArgs, &Construct<AlertVariant::Error>},
};
static constexpr MethodDescriptor kMethods[] = {
    {"title", kTitleArgs, "Sets the alert title.", &RecordTitle},
    {"banner", {}, "Uses the full-width banner presentation.", &RecordBanner},
    {"visible", kVisibleArgs, "Controls whether the alert is rendered.",
     &RecordVisible},
    {"size", kSizeArgs, "Sets the alert's semantic size.", &RecordSize},
};
static constexpr ComponentDescriptor kAlert = {
    "Alert", kConstructors, kMethods,
    "A message banner with semantic default, info, success, warning, and "
    "error constructors.",
    &Materialize};

} // namespace gpui::component_shell::display::alert

namespace gpui::component_shell {

bool RegisterDisplayAlert(shell::ComponentRegistry* registry,
                          shell::RegistryError* error) {
    return registry->Register(&display::alert::kAlert, error);
}

} // namespace gpui::component_shell
