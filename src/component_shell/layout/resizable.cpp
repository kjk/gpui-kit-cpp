// crates/component-shell/src/shell/layout/resizable.rs

#include "component_shell/families.h"
#include "component_shell/layout/mod.h"
#include "ui/resizable.h"

#include <float.h>
#include <math.h>

namespace gpui::component_shell::layout::resizable {

struct Id {
    Str value;
};

struct PanelOp {
    enum Kind : uint8_t {
        Visible,
        Size,
        Range,
    } kind = Visible;
    bool visible = false;
    float a = 0;
    float b = 0;
};

struct GroupOp {
    enum Kind : uint8_t {
        Axis,
        CrossSize,
    } kind = Axis;
    bool vertical = false;
    float size = 0;
};

bool FinitePositive(double value, const char* label, float* out, Str* error) {
    if (isfinite(value) && value > 0. && value <= (double)FLT_MAX) {
        *out = (float)value;
        return true;
    }
    *error = fmt("%s expects a positive finite pixel value", Str(label));
    return false;
}

bool RequireGroupStyle(bool styled, Str* error) {
    if (!styled) return true;
    *error =
        StrL("Resizable does not implement Styled; style its parent or panels");
    return false;
}

bool RequirePanelChild(const char* actual, Str* error) {
    if (actual && strcmp(actual, "ResizablePanel") == 0) return true;
    *error = fmt("Resizable accepts only ResizablePanel children; received %s",
                 Str(actual ? actual : "an ordinary element"));
    return false;
}

static El* MaterializePanel(MaterializeRequest* request) {
    if (!shell::PayloadIs<Empty>(request->payload))
        return request
            ->Fail(StrL("ResizablePanel received an incompatible payload"));
    Ctx* cx = request->cx;
    ResizablePanel* panel = resizable_panel(cx);
    EachMethod<PanelOp>(request, [&](const PanelOp& op) {
        switch (op.kind) {
            case PanelOp::Visible:
                panel->Visible(op.visible);
                break;
            case PanelOp::Size:
                panel->Size(op.a);
                break;
            case PanelOp::Range:
                panel->SizeRange(op.a, op.b);
                break;
        }
    });
    panel->refiner = request->TakeStyle();
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) panel->Child(children[i]);
    return CarrierOf(cx, panel);
}

static El* MaterializeGroup(MaterializeRequest* request) {
    const Id* id = request->PayloadAs<Id>();
    if (!id)
        return request
            ->Fail(StrL("Resizable received an incompatible payload"));
    bool vertical = false;
    bool hasSize = false;
    float size = 0;
    EachMethod<GroupOp>(request, [&](const GroupOp& op) {
        switch (op.kind) {
            case GroupOp::Axis:
                vertical = op.vertical;
                break;
            case GroupOp::CrossSize:
                hasSize = true;
                size = op.size;
                break;
        }
    });
    Ctx* cx = request->cx;
    ResizablePanelGroup* group = component::Resizable::New(
        cx, id->value, {}, vertical ? Axis::Vertical : Axis::Horizontal);
    if (hasSize) group->Size(size);
    bool styled = request->HasStyle();
    request->TakeStyle();
    Str error;
    if (!RequireGroupStyle(styled, &error)) return request->Fail(error);
    shell::ComponentChild* children = nullptr;
    int count = 0;
    if (!request->TakeTypedChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) {
        if (!RequirePanelChild(children[i].componentName, &error))
            return request->Fail(error);
        El* element = request->MaterializeChild(&children[i]);
        if (!element) return nullptr;
        ResizablePanel* panel =
            TakeCarriedAs<ResizablePanel>(request, element, "ResizablePanel");
        if (!panel) return nullptr;
        group->Child(panel);
    }
    return group->IntoEl();
}

// ─── Recorders ─────────────────────────────────────────────────────────────

static bool ConstructGroup(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String ||
        len(StrTrimAscii(args[0].string)) == 0) {
        return build->Fail(StrL("Resizable expects a non-empty id"));
    }
    build->New<Id>()->value = args[0].string;
    return true;
}

static bool RecordVisible(PayloadBuild* build, const ComponentArgument* args,
                          int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(StrL("ResizablePanel.visible expects boolean"));
    PanelOp* op = build->New<PanelOp>();
    op->kind = PanelOp::Visible;
    op->visible = args[0].boolean;
    return true;
}

static bool RecordSize(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number)
        return build->Fail(StrL("ResizablePanel.size expects pixels"));
    float value = 0;
    Str error;
    if (!FinitePositive(args[0].number, "ResizablePanel.size", &value, &error))
        return build->Fail(error);
    PanelOp* op = build->New<PanelOp>();
    op->kind = PanelOp::Size;
    op->a = value;
    return true;
}

static bool RecordSizeRange(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count != 2 || args[0].kind != shell::ComponentArgumentKind::Number ||
        args[1].kind != shell::ComponentArgumentKind::Number) {
        return build
            ->Fail(StrL("ResizablePanel.size_range expects two numbers"));
    }
    float min = 0, max = 0;
    Str error;
    if (!FinitePositive(args[0].number, "minimum", &min, &error) ||
        !FinitePositive(args[1].number, "maximum", &max, &error)) {
        return build->Fail(error);
    }
    if (min > max)
        return build->Fail(StrL("size_range minimum must not exceed maximum"));
    PanelOp* op = build->New<PanelOp>();
    op->kind = PanelOp::Range;
    op->a = min;
    op->b = max;
    return true;
}

static bool RecordAxis(PayloadBuild* build, const ComponentArgument* args,
                       int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Enum)
        return build->Fail(StrL("Resizable.axis expects an axis"));
    bool vertical = false;
    if (StrEq(args[0].string, "horizontal")) {
        vertical = false;
    } else if (StrEq(args[0].string, "vertical")) {
        vertical = true;
    } else {
        return build->Fail(StrL("unsupported axis"));
    }
    GroupOp* op = build->New<GroupOp>();
    op->kind = GroupOp::Axis;
    op->vertical = vertical;
    return true;
}

static bool RecordCrossSize(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Number)
        return build->Fail(StrL("Resizable.cross_size expects pixels"));
    float value = 0;
    Str error;
    if (!FinitePositive(args[0].number, "Resizable.cross_size", &value, &error))
        return build->Fail(error);
    GroupOp* op = build->New<GroupOp>();
    op->kind = GroupOp::CrossSize;
    op->size = value;
    return true;
}

// ─── Descriptors ───────────────────────────────────────────────────────────

static constexpr ConstructorDescriptor kPanelConstructors[] = {
    {"ResizablePanel", {}, &RecordEmpty}};
static constexpr ArgumentDescriptor kIdArgs[] = {{"id", SchemaString()}};
static constexpr ConstructorDescriptor kGroupConstructors[] = {
    {"Resizable", kIdArgs, &ConstructGroup}};

static constexpr ArgumentDescriptor kVisibleArgs[] = {
    {"visible", SchemaBoolean()}};
static constexpr ArgumentDescriptor kPixelsArgs[] = {
    {"pixels", SchemaNumber()}};
static constexpr ArgumentDescriptor kRangeArgs[] = {
    {"minimum", SchemaNumber()},
    {"maximum", SchemaNumber()}};
static constexpr const char* kAxisLiterals[] = {"horizontal", "vertical"};
static constexpr ArgumentDescriptor kAxisArgs[] = {
    {"axis", SchemaEnum(kAxisLiterals)}};

static constexpr MethodDescriptor kPanelMethods[] = {
    {"visible", kVisibleArgs, "Sets panel visibility.", &RecordVisible},
    {"size", kPixelsArgs, "Sets initial panel size in pixels.", &RecordSize},
    {"size_range", kRangeArgs, "Sets the inclusive resize range in pixels.",
     &RecordSizeRange},
};
static constexpr MethodDescriptor kGroupMethods[] = {
    {"axis", kAxisArgs, "Sets the resize axis.", &RecordAxis},
    {"cross_size", kPixelsArgs, "Sets the cross-axis size in pixels.",
     &RecordCrossSize},
};

static constexpr ComponentDescriptor kDescriptors[] = {
    {"ResizablePanel", kPanelConstructors, kPanelMethods,
     "A typed resizable panel accepting ordinary children and shell style.",
     &MaterializePanel},
    {"Resizable", kGroupConstructors, kGroupMethods,
     "A typed native resizable group accepting only ResizablePanel children. "
     "It owns keyed state internally.",
     &MaterializeGroup},
};

} // namespace gpui::component_shell::layout::resizable

namespace gpui::component_shell {

bool RegisterLayoutResizable(shell::ComponentRegistry* registry,
                             shell::RegistryError* error) {
    for (const ComponentDescriptor& d : layout::resizable::kDescriptors) {
        if (!registry->Register(&d, error)) return false;
    }
    return true;
}

} // namespace gpui::component_shell
