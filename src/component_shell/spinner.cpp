// crates/component-shell/src/shell/spinner.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "base/animation.h"
#include "ui/spinner.h"

namespace gpui::component_shell::spinner {

struct SpinnerPayload {};

enum class SpinnerEase : uint8_t {
    Linear,
    EaseInOut,
    EaseOutQuint,
};

struct SpinnerOp {
    enum Kind : uint8_t {
        Size,
        Icon,
        Color,
        Ease,
    } kind = Size;
    UiSize size = UiSize::Medium;
    IconName icon = IconName::Loader;
    Rgba color = {};
    SpinnerEase ease = SpinnerEase::Linear;
};

static bool Construct(PayloadBuild* build, const ComponentArgument*, int) {
    return build->Mark<SpinnerPayload>();
}

static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int) {
    SpinnerOp* op = build->New<SpinnerOp>();
    op->kind = SpinnerOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

static bool RecordIcon(PayloadBuild* build, const ComponentArgument* args,
                       int) {
    SpinnerOp* op = build->New<SpinnerOp>();
    op->kind = SpinnerOp::Icon;
    op->icon = StrEq(args[0].string, "loader_circle") ? IconName::LoaderCircle
                                                      : IconName::Loader;
    return true;
}

static bool RecordColor(PayloadBuild* build, const ComponentArgument* args,
                        int) {
    Rgba color = {};
    if (!ParseColorArgument(build, "Spinner", args[0].string, &color))
        return false;
    SpinnerOp* op = build->New<SpinnerOp>();
    op->kind = SpinnerOp::Color;
    op->color = color;
    return true;
}

static bool RecordEase(PayloadBuild* build, const ComponentArgument* args,
                       int) {
    SpinnerOp* op = build->New<SpinnerOp>();
    op->kind = SpinnerOp::Ease;
    Str ease = args[0].string;
    op->ease = StrEq(ease, "ease_in_out")      ? SpinnerEase::EaseInOut
               : StrEq(ease, "ease_out_quint") ? SpinnerEase::EaseOutQuint
                                               : SpinnerEase::Linear;
    return true;
}

// SpinnerMaterializer::component: the payload check, then every recorded
// operation folded into the builder in script order.
static component::Spinner* Component(MaterializeRequest* request) {
    if (!shell::PayloadIs<SpinnerPayload>(request->payload)) {
        request->Fail(StrL("Spinner received an incompatible payload"));
        return nullptr;
    }
    component::Spinner* spinner = component::Spinner::New(request->cx)
                                      ->Id(request->elementId);
    EachMethod<SpinnerOp>(request, [&](const SpinnerOp& op) {
        switch (op.kind) {
            case SpinnerOp::Size:
                spinner->WithSize(op.size);
                break;
            case SpinnerOp::Icon:
                spinner->Icon(op.icon);
                break;
            case SpinnerOp::Color:
                spinner->Color(op.color);
                break;
            case SpinnerOp::Ease:
                spinner->Ease(op.ease == SpinnerEase::EaseInOut ? &EaseInOutQuad
                              : op.ease == SpinnerEase::EaseOutQuint
                                  ? &EaseOutQuint
                                  : &EaseLinear);
                break;
        }
    });
    return spinner;
}

static El* Materialize(MaterializeRequest* request) {
    component::Spinner* spinner = Component(request);
    if (!spinner) return nullptr;
    El* element = Div(request->cx->a)->Child(spinner->IntoEl());
    return request->ApplyStyle(element);
}

static constexpr const char* kIconLiterals[] = {"loader", "loader_circle"};
static constexpr const char* kEaseLiterals[] = {"linear", "ease_in_out",
                                                "ease_out_quint"};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};
static constexpr ArgumentDescriptor kIconArgs[] = {
    {"icon", SchemaEnum(kIconLiterals)}};
static constexpr ArgumentDescriptor kColorArgs[] = {{"color", SchemaString()}};
static constexpr ArgumentDescriptor kEaseArgs[] = {
    {"ease", SchemaEnum(kEaseLiterals)}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Spinner", {}, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"size", kSizeArgs, "Sets the spinner's semantic size.", &RecordSize},
    {"icon", kIconArgs, "Selects the icon rotated by the spinner.",
     &RecordIcon},
    {"color", kColorArgs, "Sets the spinner icon color.", &RecordColor},
    {"ease", kEaseArgs, "Sets the spinner rotation easing curve.", &RecordEase},
};
static constexpr ComponentDescriptor kSpinner = {
    "Spinner", kConstructors, kMethods, "A cycling loading spinner.",
    &Materialize};

} // namespace gpui::component_shell::spinner

namespace gpui::component_shell {

bool RegisterSpinner(shell::ComponentRegistry* registry,
                     shell::RegistryError* error) {
    return registry->Register(&spinner::kSpinner, error);
}

} // namespace gpui::component_shell
