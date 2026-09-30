// crates/component-shell/src/shell/compound/collapsible.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "ui/collapsible.h"

namespace gpui::component_shell::compound::collapsible {

struct CollapsiblePayload {};

struct CollapsibleOp {
    enum Kind : uint8_t {
        Open,
        MotionId,
    } kind = Open;
    bool open = false;
    Str motionId;
};

static bool Construct(PayloadBuild* build, const ComponentArgument*, int) {
    return build->Mark<CollapsiblePayload>();
}

static bool RecordOpen(PayloadBuild* build, const ComponentArgument* args,
                       int) {
    CollapsibleOp* op = build->New<CollapsibleOp>();
    op->kind = CollapsibleOp::Open;
    op->open = args[0].boolean;
    return true;
}

static bool RecordMotionId(PayloadBuild* build, const ComponentArgument* args,
                           int) {
    CollapsibleOp* op = build->New<CollapsibleOp>();
    op->kind = CollapsibleOp::MotionId;
    op->motionId = args[0].string;
    return true;
}

static El* Materialize(MaterializeRequest* request) {
    if (!shell::PayloadIs<CollapsiblePayload>(request->payload))
        return request
            ->Fail(StrL("Collapsible received an incompatible payload"));
    component::Collapsible* component =
        component::Collapsible::New(request->cx);
    EachMethod<CollapsibleOp>(request, [&](const CollapsibleOp& op) {
        switch (op.kind) {
            case CollapsibleOp::Open:
                component->Open(op.open);
                break;
            case CollapsibleOp::MotionId:
                component->MotionId(op.motionId);
                break;
        }
    });
    if (El* content = request->TakeSlot("content")) component->Content(content);
    // request.finish: the ordinary children are the collapsible's own
    // (ParentElement), ahead of its content, and the style refines its root.
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) component->Child(children[i]);
    return request->ApplyStyle(component->IntoEl());
}

static constexpr ArgumentDescriptor kOpenArgs[] = {{"open", SchemaBoolean()}};
static constexpr ArgumentDescriptor kMotionIdArgs[] = {{"id", SchemaString()}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Collapsible", {}, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"open", kOpenArgs, "Controls whether the content slot is revealed.",
     &RecordOpen},
    {"motion_id", kMotionIdArgs,
     "Adds stable identity for a reversible measured reveal.", &RecordMotionId},
};
static constexpr ComponentDescriptor kCollapsible = {
    "Collapsible", kConstructors, kMethods,
    "A trigger container with optional named `content` reveal content.",
    &Materialize};

} // namespace gpui::component_shell::compound::collapsible

namespace gpui::component_shell {

bool RegisterCompoundCollapsible(shell::ComponentRegistry* registry,
                                 shell::RegistryError* error) {
    return registry->Register(&compound::collapsible::kCollapsible, error);
}

} // namespace gpui::component_shell
