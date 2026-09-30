// crates/component-shell/src/shell/chart/mod.rs
//
// Concrete chart bindings backed by immutable script data snapshots.
//
// `gpui_component::plot::Plot` is deliberately not registered: it is a Rust
// painting trait implemented by concrete chart elements, not a constructible
// component surface.

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "ui/chart.h"
#include "ui/theme.h"

#include <float.h>
#include <math.h>

namespace gpui::component_shell::chart {

// mod.rs test_probe, widened into a seam: see families.h.
static ChartErrorProbe gProbe = nullptr;

// Payload(ComponentArgument): the row snapshot callback.
struct Payload {
    ComponentArgument rows = {};
};

// One chart row. The label lives in the frame arena, NUL-terminated, so the
// charts' `const char*` label arrays can point at it.
struct Row {
    Str label = {};
    double value = 0;
};

struct Op {
    enum Kind : uint8_t {
        Grid,
        Dot,
        Natural,
        Linear,
        StepAfter,
        TickMargin,
        Axis,
        ValueAxis,
        InnerRadius,
        PadAngle,
        Labels,
        GridLevels,
    } kind = Grid;
    bool flag = false;
    // TickMargin and GridLevels: Rust's usize, clamped to what the C++
    // charts count in.
    int count = 0;
    float number = 0;
};

// rows(): the snapshot, each row a plain object with a string `label` and a
// number `value`. False with `error` set otherwise.
static bool Rows(MaterializeRequest* request, shell::ComponentCallback callback,
                 Row** out, int* count, Str* error) {
    Ctx* cx = request->cx;
    Arena* a = cx->a;
    const shell::ComponentDataValue* rows = nullptr;
    int n = 0;
    if (!callback.SnapshotRowsWith(request->runtime, nullptr, 0, cx, &rows, &n,
                                   a, error)) {
        return false;
    }
    Row* data = n > 0 ? (Row*)Alloc(a, (int)sizeof(Row) * n) : nullptr;
    for (int index = 0; index < n; index++) {
        const shell::ComponentDataValue& row = rows[index];
        if (row.kind != shell::DataKind::Object) {
            *error =
                StrDup(a, fmt("chart row %d must be a plain object", index));
            return false;
        }
        const shell::ComponentDataValue* label = row.Get(StrL("label"));
        if (!label || label->kind != shell::DataKind::String) {
            *error = StrDup(
                a, fmt("chart row %d must have string field `label`", index));
            return false;
        }
        const shell::ComponentDataValue* value = row.Get(StrL("value"));
        if (!value || value->kind != shell::DataKind::Number) {
            *error = StrDup(
                a, fmt("chart row %d must have finite number field `value`",
                       index));
            return false;
        }
        data[index].label = StrDup(a, label->string);
        data[index].value = value->number;
    }
    *out = data;
    *count = n;
    return true;
}

// The charts here hold arrays: the rows' values as floats and their labels
// as a `const char*` array, where Rust's take the rows and closures over
// them. The values narrow from f64 to f32, as the C++ charts paint floats.
static const float* Values(Arena* a, const Row* rows, int n) {
    float* values = (float*)Alloc(a, (int)sizeof(float) * (n > 0 ? n : 1));
    for (int i = 0; i < n; i++) values[i] = (float)rows[i].value;
    return values;
}
static const char* const* Labels(Arena* a, const Row* rows, int n) {
    const char** labels =
        (const char**)Alloc(a, (int)sizeof(const char*) * (n > 0 ? n : 1));
    for (int i = 0; i < n; i++)
        labels[i] = rows[i].label.s ? rows[i].label.s : "";
    return labels;
}

// ChartHost::render: the chart `kind` names over the snapshot, with every
// recorded op folded in in script order.
//
// Rust keys the chart's hover state and path caches on its construction
// site under the wrapper's id (the spec node's own). The C++ chart ids fold
// onto the id stack at construction, which the wrapper cannot push, so the
// chart is renamed to the node's id instead: one per spec node either way.
static El* Render(MaterializeRequest* request, const char* kind,
                  shell::ComponentCallback callback) {
    Ctx* cx = request->cx;
    Arena* a = cx->a;
    Row* data = nullptr;
    int n = 0;
    Str error;
    if (!Rows(request, callback, &data, &n, &error)) {
        if (gProbe) gProbe(error);
        return Div(a)
            ->Child(TextEl(a, StrDup(a, fmt("Failed to build %s data: %s",
                                            Str(kind), error))));
    }
    const float* values = Values(a, data, n);
    Str id = request->elementId;
    if (strcmp(kind, "BarChart") == 0) {
        // .band(label).value(value).label(value.to_string()): the value
        // written at each bar's end, in the chart's own float Display.
        component::BarChart* chart = component::BarChart::New(cx, values, n)
                                         ->Id(id)
                                         ->Labels(Labels(a, data, n))
                                         ->LabelValues(true);
        EachMethod<Op>(request, [&](const Op& op) {
            switch (op.kind) {
                case Op::Grid:
                    chart->Grid(op.flag);
                    break;
                case Op::TickMargin:
                    chart->TickMargin(op.count);
                    break;
                case Op::Axis:
                    chart->LabelAxis(op.flag);
                    break;
                case Op::ValueAxis:
                    chart->ValueAxis(op.flag);
                    break;
                default:
                    break;
            }
        });
        return chart->IntoEl();
    }
    if (strcmp(kind, "LineChart") == 0) {
        component::LineChart* chart = component::LineChart::New(cx, values, n)
                                          ->Id(id)
                                          ->Labels(Labels(a, data, n));
        EachMethod<Op>(request, [&](const Op& op) {
            switch (op.kind) {
                case Op::Grid:
                    chart->Grid(op.flag);
                    break;
                case Op::Dot:
                    chart->Dot();
                    break;
                case Op::Natural:
                    chart->Natural();
                    break;
                case Op::Linear:
                    chart->Linear();
                    break;
                case Op::StepAfter:
                    chart->StepAfter();
                    break;
                case Op::TickMargin:
                    chart->TickMargin(op.count);
                    break;
                case Op::Axis:
                    chart->XAxis(op.flag);
                    break;
                default:
                    break;
            }
        });
        return chart->IntoEl();
    }
    if (strcmp(kind, "AreaChart") == 0) {
        component::AreaChart* chart = component::AreaChart::New(cx, values, n)
                                          ->Id(id)
                                          ->Labels(Labels(a, data, n));
        EachMethod<Op>(request, [&](const Op& op) {
            switch (op.kind) {
                case Op::Grid:
                    chart->Grid(op.flag);
                    break;
                case Op::Natural:
                    chart->Natural();
                    break;
                case Op::Linear:
                    chart->Linear();
                    break;
                case Op::StepAfter:
                    chart->StepAfter();
                    break;
                case Op::TickMargin:
                    chart->TickMargin(op.count);
                    break;
                case Op::Axis:
                    chart->XAxis(op.flag);
                    break;
                default:
                    break;
            }
        });
        return chart->IntoEl();
    }
    if (strcmp(kind, "PieChart") == 0) {
        // PieChart::new(data).value(value as f32): every slice in the
        // theme's chart_2, which is Rust's slice_color with no color set.
        component::PieChart* chart = component::PieChart::New(cx)->Id(id);
        Rgba color = ThemeNow(cx->app).chart2;
        for (int i = 0; i < n; i++) chart->Slice(values[i], color);
        bool labels = false;
        EachMethod<Op>(request, [&](const Op& op) {
            switch (op.kind) {
                case Op::InnerRadius:
                    chart->InnerRadius(op.number);
                    break;
                case Op::PadAngle:
                    chart->PadAngle(op.number);
                    break;
                case Op::Labels:
                    // Op::Labels(true) => chart.label(label); false leaves
                    // an earlier true in place, as Rust's match does.
                    labels = labels || op.flag;
                    break;
                default:
                    break;
            }
        });
        if (labels) {
            for (int i = 0; i < n; i++) {
                chart->slices[i].label = data[i].label;
                chart->hasLabels = true;
            }
        }
        return chart->IntoEl();
    }
    // RadarChart::new(data).value(value).label(label).
    component::RadarChart* chart = component::RadarChart::New(cx, values, n)
                                       ->Id(id)
                                       ->Labels(Labels(a, data, n));
    EachMethod<Op>(request, [&](const Op& op) {
        switch (op.kind) {
            case Op::Grid:
                chart->Grid(op.flag);
                break;
            case Op::Dot:
                chart->Dot();
                break;
            case Op::GridLevels:
                chart->GridLevels(op.count);
                break;
            default:
                break;
        }
    });
    return chart->IntoEl();
}

// ChartMaterializer + wrap: the payload, the callback, no children of either
// lane, then `div().size_full().child(chart)` refined by the node's style.
static El* MaterializeKind(MaterializeRequest* request, const char* kind) {
    const Payload* payload = request->PayloadAs<Payload>();
    if (!payload)
        return request->Fail(
            StrDup(request->cx->a,
                   fmt("%s received an incompatible payload", Str(kind))));
    shell::ComponentCallback callback = request->ResolveCallback(payload->rows);
    if (!callback.IsSet())
        return request->Fail(StrL("component argument is not a callback"));
    if (request->ChildrenLen() != 0)
        return request->Fail(StrL("charts do not accept children"));
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    if (count != 0)
        return request->Fail(StrL("charts do not accept ordinary children"));
    El* wrapper = Div(request->cx->a)->W(kFill)->H(kFill);
    request->ApplyStyle(wrapper);
    wrapper->Child(Render(request, kind, callback));
    return wrapper;
}

template <const char* const* Kind>
static El* Materialize(MaterializeRequest* request) {
    return MaterializeKind(request, *Kind);
}

static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Callback)
        return build->Fail(StrL("chart expects a row snapshot callback"));
    build->New<Payload>()->rows = args[0];
    return true;
}

static const char* OpName(Op::Kind kind) {
    switch (kind) {
        case Op::Grid:
            return "grid";
        case Op::ValueAxis:
            return "value_axis";
        case Op::Labels:
            return "labels";
        case Op::TickMargin:
            return "tick_margin";
        case Op::GridLevels:
            return "grid_levels";
        case Op::InnerRadius:
            return "inner_radius";
        case Op::PadAngle:
            return "pad_angle";
        default:
            return "";
    }
}

// bool_method("Chart", name, "Configures this chart option.", op). Axis is
// `label_axis` on BarChart and `x_axis` on the point charts.
template <Op::Kind K>
static bool RecordBool(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(fmt("Chart.%s expects one boolean", Str(OpName(K))));
    Op* op = build->New<Op>();
    op->kind = K;
    op->flag = args[0].boolean;
    return true;
}
static bool RecordLabelAxis(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(StrL("Chart.label_axis expects one boolean"));
    Op* op = build->New<Op>();
    op->kind = Op::Axis;
    op->flag = args[0].boolean;
    return true;
}
static bool RecordXAxis(PayloadBuild* build, const ComponentArgument* args,
                        int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(StrL("Chart.x_axis expects one boolean"));
    Op* op = build->New<Op>();
    op->kind = Op::Axis;
    op->flag = args[0].boolean;
    return true;
}

// positive_usize_method(name, op): a finite whole number of at least one.
template <Op::Kind K>
static bool RecordPositive(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    double value =
        count == 1 && args[0].kind == shell::ComponentArgumentKind::Number
            ? args[0].number
            : NAN;
    if (!isfinite(value) || value < 1. || value != floor(value) ||
        value > 18446744073709551615.0)
        return build
            ->Fail(fmt("%s expects a positive integer", Str(OpName(K))));
    Op* op = build->New<Op>();
    op->kind = K;
    // The C++ charts count in int; a larger count means the same thing.
    op->count = value > 2147483647.0 ? 2147483647 : (int)value;
    return true;
}

// number_method(name, op): a finite number from zero to f32::MAX.
template <Op::Kind K>
static bool RecordNumber(PayloadBuild* build, const ComponentArgument* args,
                         int count) {
    double value =
        count == 1 && args[0].kind == shell::ComponentArgumentKind::Number
            ? args[0].number
            : NAN;
    if (!isfinite(value) || value < 0. || value > (double)FLT_MAX)
        return build->Fail(
            fmt("%s expects a non-negative finite number", Str(OpName(K))));
    Op* op = build->New<Op>();
    op->kind = K;
    op->number = (float)value;
    return true;
}

// flag(name, op).
template <Op::Kind K>
static bool RecordFlag(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<Op>()->kind = K;
    return true;
}

static constexpr const char* kOptionDoc = "Configures this chart option.";
static constexpr const char* kPositiveDoc =
    "Configures this chart option with a positive integer.";
static constexpr const char* kNumberDoc =
    "Configures this chart option with a non-negative number.";
static constexpr const char* kFlagDoc = "Selects this chart rendering mode.";
static constexpr const char* kChartDoc =
    "A concrete native chart backed by one immutable plain-data snapshot per "
    "materialization. Rows require { label, value }; style applies to its "
    "full-size host.";

static constexpr ArgumentDescriptor kRowsArgs[] = {
    {"rows",
     SchemaCallback(
         "(cx: Context) => readonly { label: string; value: number }[]")}};
static constexpr ArgumentDescriptor kGridArgs[] = {{"grid", SchemaBoolean()}};
static constexpr ArgumentDescriptor kLabelAxisArgs[] = {
    {"label_axis", SchemaBoolean()}};
static constexpr ArgumentDescriptor kValueAxisArgs[] = {
    {"value_axis", SchemaBoolean()}};
static constexpr ArgumentDescriptor kXAxisArgs[] = {
    {"x_axis", SchemaBoolean()}};
static constexpr ArgumentDescriptor kTickMarginArgs[] = {
    {"tick_margin", SchemaNumber()}};
static constexpr ArgumentDescriptor kInnerRadiusArgs[] = {
    {"inner_radius", SchemaNumber()}};
static constexpr ArgumentDescriptor kPadAngleArgs[] = {
    {"pad_angle", SchemaNumber()}};
static constexpr ArgumentDescriptor kLabelsArgs[] = {
    {"labels", SchemaBoolean()}};
static constexpr ArgumentDescriptor kGridLevelsArgs[] = {
    {"grid_levels", SchemaNumber()}};

static constexpr MethodDescriptor kGrid = {"grid", kGridArgs, kOptionDoc,
                                           &RecordBool<Op::Grid>};
static constexpr MethodDescriptor kXAxis = {"x_axis", kXAxisArgs, kOptionDoc,
                                            &RecordXAxis};
static constexpr MethodDescriptor kTickMargin = {
    "tick_margin", kTickMarginArgs, kPositiveDoc,
    &RecordPositive<Op::TickMargin>};
static constexpr MethodDescriptor kDot = {"dot",
                                          {},
                                          kFlagDoc,
                                          &RecordFlag<Op::Dot>};
static constexpr MethodDescriptor kNatural = {"natural",
                                              {},
                                              kFlagDoc,
                                              &RecordFlag<Op::Natural>};
static constexpr MethodDescriptor kLinear = {"linear",
                                             {},
                                             kFlagDoc,
                                             &RecordFlag<Op::Linear>};
static constexpr MethodDescriptor kStepAfter = {"step_after",
                                                {},
                                                kFlagDoc,
                                                &RecordFlag<Op::StepAfter>};

static constexpr MethodDescriptor kBarMethods[] = {
    kGrid,
    {"label_axis", kLabelAxisArgs, kOptionDoc, &RecordLabelAxis},
    {"value_axis", kValueAxisArgs, kOptionDoc, &RecordBool<Op::ValueAxis>},
    kTickMargin,
};
static constexpr MethodDescriptor kLineMethods[] = {
    kGrid, kXAxis, kTickMargin, kDot, kNatural, kLinear, kStepAfter,
};
static constexpr MethodDescriptor kAreaMethods[] = {
    kGrid, kXAxis, kTickMargin, kNatural, kLinear, kStepAfter,
};
static constexpr MethodDescriptor kPieMethods[] = {
    {"inner_radius", kInnerRadiusArgs, kNumberDoc,
     &RecordNumber<Op::InnerRadius>},
    {"pad_angle", kPadAngleArgs, kNumberDoc, &RecordNumber<Op::PadAngle>},
    {"labels", kLabelsArgs, kOptionDoc, &RecordBool<Op::Labels>},
};
static constexpr MethodDescriptor kRadarMethods[] = {
    kGrid,
    {"grid_levels", kGridLevelsArgs, kPositiveDoc,
     &RecordPositive<Op::GridLevels>},
    kDot,
};

static constexpr const char* kBarName = "BarChart";
static constexpr const char* kLineName = "LineChart";
static constexpr const char* kAreaName = "AreaChart";
static constexpr const char* kPieName = "PieChart";
static constexpr const char* kRadarName = "RadarChart";

static constexpr ConstructorDescriptor kBarConstructors[] = {
    {"BarChart", kRowsArgs, &Construct}};
static constexpr ConstructorDescriptor kLineConstructors[] = {
    {"LineChart", kRowsArgs, &Construct}};
static constexpr ConstructorDescriptor kAreaConstructors[] = {
    {"AreaChart", kRowsArgs, &Construct}};
static constexpr ConstructorDescriptor kPieConstructors[] = {
    {"PieChart", kRowsArgs, &Construct}};
static constexpr ConstructorDescriptor kRadarConstructors[] = {
    {"RadarChart", kRowsArgs, &Construct}};

static constexpr ComponentDescriptor kBarChart = {"BarChart", kBarConstructors,
                                                  kBarMethods, kChartDoc,
                                                  &Materialize<&kBarName>};
static constexpr ComponentDescriptor kLineChart = {
    "LineChart", kLineConstructors, kLineMethods, kChartDoc,
    &Materialize<&kLineName>};
static constexpr ComponentDescriptor kAreaChart = {
    "AreaChart", kAreaConstructors, kAreaMethods, kChartDoc,
    &Materialize<&kAreaName>};
static constexpr ComponentDescriptor kPieChart = {"PieChart", kPieConstructors,
                                                  kPieMethods, kChartDoc,
                                                  &Materialize<&kPieName>};
static constexpr ComponentDescriptor kRadarChart = {
    "RadarChart", kRadarConstructors, kRadarMethods, kChartDoc,
    &Materialize<&kRadarName>};

} // namespace gpui::component_shell::chart

namespace gpui::component_shell {

void SetChartErrorProbe(ChartErrorProbe probe) {
    chart::gProbe = probe;
}

bool RegisterChart(shell::ComponentRegistry* registry,
                   shell::RegistryError* error) {
    return registry->Register(&chart::kBarChart, error) &&
           registry->Register(&chart::kLineChart, error) &&
           registry->Register(&chart::kAreaChart, error) &&
           registry->Register(&chart::kPieChart, error) &&
           registry->Register(&chart::kRadarChart, error);
}

} // namespace gpui::component_shell
