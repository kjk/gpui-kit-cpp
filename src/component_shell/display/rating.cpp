// crates/component-shell/src/shell/display/rating.rs

#include "component_shell/families.h"
#include "component_shell/display/common.h"
#include "shell/view.h"
#include "ui/rating.h"

#include <limits.h>

namespace gpui::component_shell::display::rating {

struct RatingPayload {
    Str id;
};

struct RatingOp {
    enum Kind : uint8_t {
        Value,
        Max,
        Color,
        Size,
        OnChange,
    } kind = Value;
    // Value and Max: Rust's usize.
    double count = 0;
    Rgba color = {};
    UiSize size = UiSize::Medium;
    shell::ComponentArgument change = {};
};

static bool Construct(PayloadBuild* build, const ComponentArgument* args, int) {
    if (!common::NonEmptyId(build, "Rating", args[0].string)) return false;
    build->New<RatingPayload>()->id = args[0].string;
    return true;
}

// rating_count: `usize::MAX as f64` rounds to 2^64 on 64-bit targets, so
// the upper boundary is exclusive.
static bool RatingCount(PayloadBuild* build, double value, const char* name) {
    bool whole = value == value && value >= 0.0 &&
                 value < 18446744073709551616.0 &&
                 value == (double)(uint64_t)value;
    if (whole) return true;
    return build
        ->Fail(fmt("Rating.%s(%s) expects a non-negative integer, got %s",
                   Str(name), Str(name), Str(F64DisplayTemp(value))));
}

template <RatingOp::Kind K>
static bool RecordCount(PayloadBuild* build, const ComponentArgument* args,
                        int) {
    const char* name = K == RatingOp::Value ? "value" : "max";
    if (!RatingCount(build, args[0].number, name)) return false;
    RatingOp* op = build->New<RatingOp>();
    op->kind = K;
    op->count = args[0].number;
    return true;
}

static bool RecordChange(PayloadBuild* build, const ComponentArgument* args,
                         int) {
    RatingOp* op = build->New<RatingOp>();
    op->kind = RatingOp::OnChange;
    op->change = args[0];
    return true;
}

static bool RecordColor(PayloadBuild* build, const ComponentArgument* args,
                        int) {
    Rgba color = {};
    if (!ParseColorArgument(build, "Rating", args[0].string, &color))
        return false;
    RatingOp* op = build->New<RatingOp>();
    op->kind = RatingOp::Color;
    op->color = color;
    return true;
}

static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int) {
    RatingOp* op = build->New<RatingOp>();
    op->kind = RatingOp::Size;
    op->size = SizeOfLiteral(args[0].string);
    return true;
}

// rating_settings: the later of value and max wins, and value never passes
// the max in force when it was set, or any max set after it.
static void Settings(const MaterializeRequest* request, double* value,
                     double* max) {
    *value = 0;
    *max = 5;
    EachMethod<RatingOp>(request, [&](const RatingOp& op) {
        if (op.kind == RatingOp::Value) {
            *value = op.count < *max ? op.count : *max;
        } else if (op.kind == RatingOp::Max) {
            *max = op.count;
            if (*value > *max) *value = *max;
        }
    });
}

// component::Rating counts stars in an int.
static int StarCount(double value) {
    return value > (double)INT_MAX ? INT_MAX : (int)value;
}

// The on_change closure. The star the reader clicked arrives as the value
// Rating fills in.
static void RunChange(const shell::ComponentEventBinding* binding,
                      ScriptView* view, Ctx* cx, const void*) {
    shell::ComponentDataValue value =
        shell::ComponentDataValue::Number((double)binding->value);
    binding->callback
        .InvokeAndReport(view->runtime, "Rating.on_change callback failed",
                         &value, 1, cx->win, cx->app);
}

static component::Rating* Component(MaterializeRequest* request) {
    const RatingPayload* payload = request->PayloadAs<RatingPayload>();
    if (!payload) {
        request->Fail(StrL("Rating received an incompatible payload"));
        return nullptr;
    }
    double value = 0;
    double max = 0;
    Settings(request, &value, &max);
    component::Rating* rating = component::Rating::New(request->cx, payload->id)
                                    ->Disabled(request->disabled)
                                    ->Max(StarCount(max))
                                    ->Value(StarCount(value));
    EachMethod<RatingOp>(request, [&](const RatingOp& op) {
        switch (op.kind) {
            case RatingOp::Color:
                rating->Color(op.color);
                break;
            case RatingOp::Size:
                rating->WithSize(op.size);
                break;
            case RatingOp::Value:
            case RatingOp::Max:
            case RatingOp::OnChange:
                break;
        }
    });
    return rating;
}

static El* Materialize(MaterializeRequest* request) {
    if (!common::EnsureNoChildren(request, "Rating")) return nullptr;
    // The last on_change wins.
    const shell::ComponentArgument* change = nullptr;
    EachMethod<RatingOp>(request, [&](const RatingOp& op) {
        if (op.kind == RatingOp::OnChange) change = &op.change;
    });
    component::Rating* rating = Component(request);
    if (!rating) return nullptr;
    if (change) {
        rating->OnClick(shell::ComponentValueListener(
            request->cx, request->elementId, &RunChange,
            request->ResolveCallback(*change)));
    }
    return request->ApplyStyle(rating->IntoEl());
}

static constexpr ArgumentDescriptor kIdArgs[] = {{"id", SchemaString()}};
static constexpr ArgumentDescriptor kValueArgs[] = {{"value", SchemaNumber()}};
static constexpr ArgumentDescriptor kMaxArgs[] = {{"max", SchemaNumber()}};
static constexpr ArgumentDescriptor kChangeArgs[] = {
    {"on_change", SchemaCallback("(value: number, cx: Context) => void")}};
static constexpr ArgumentDescriptor kColorArgs[] = {{"color", SchemaString()}};
static constexpr ArgumentDescriptor kSizeArgs[] = {
    {"size", SchemaEnum(kSizeLiterals)}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Rating", kIdArgs, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    kDisabledMethod,
    {"value", kValueArgs, "Sets the current number of active stars.",
     &RecordCount<RatingOp::Value>},
    {"max", kMaxArgs, "Sets the maximum number of stars.",
     &RecordCount<RatingOp::Max>},
    {"on_change", kChangeArgs,
     "Reports the star the reader clicked, so the script can drive `value`.",
     &RecordChange},
    {"color", kColorArgs, "Sets the active star color.", &RecordColor},
    {"size", kSizeArgs, "Sets the rating's semantic size.", &RecordSize},
};
static constexpr ComponentDescriptor kRating = {
    "Rating", kConstructors, kMethods,
    "An interactive star rating with configurable value and maximum.",
    &Materialize};

} // namespace gpui::component_shell::display::rating

namespace gpui::component_shell {

bool RegisterDisplayRating(shell::ComponentRegistry* registry,
                           shell::RegistryError* error) {
    return registry->Register(&display::rating::kRating, error);
}

} // namespace gpui::component_shell
