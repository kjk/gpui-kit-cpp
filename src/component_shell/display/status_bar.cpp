// crates/component-shell/src/shell/display/status_bar.rs

#include "component_shell/families.h"
#include "component_shell/display/common.h"
#include "ui/status_bar.h"

namespace gpui::component_shell::display::status_bar {

struct StatusBarPayload {};

struct StatusBarOp {
    enum Kind : uint8_t {
        Left,
        Right,
    } kind = Left;
    shell::ComponentArgument element = {};
};

static bool Construct(PayloadBuild* build, const ComponentArgument*, int) {
    return build->Mark<StatusBarPayload>();
}

template <StatusBarOp::Kind K>
static bool RecordContent(PayloadBuild* build, const ComponentArgument* args,
                          int) {
    StatusBarOp* op = build->New<StatusBarOp>();
    op->kind = K;
    op->element = args[0];
    return true;
}

static El* Materialize(MaterializeRequest* request) {
    if (!shell::PayloadIs<StatusBarPayload>(request->payload))
        return request
            ->Fail(StrL("StatusBar received an incompatible payload"));
    component::StatusBar* bar = component::StatusBar::New(request->cx);
    bool failed = false;
    EachMethod<StatusBarOp>(request, [&](const StatusBarOp& op) {
        if (failed) return;
        El* element = request->ResolveElement(op.element);
        if (!element) {
            failed = true;
            return;
        }
        if (op.kind == StatusBarOp::Left)
            bar->Left(element);
        else
            bar->Right(element);
    });
    if (failed) return nullptr;
    // Ordinary children fill the center.
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    for (int i = 0; i < count; i++) bar->Center(children[i]);
    return request->ApplyStyle(bar->IntoEl());
}

static constexpr ArgumentDescriptor kElementArgs[] = {
    {"element", SchemaElement()}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"StatusBar", {}, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"left_content", kElementArgs, "Appends content to the leading region.",
     &RecordContent<StatusBarOp::Left>},
    {"right_content", kElementArgs, "Appends content to the trailing region.",
     &RecordContent<StatusBarOp::Right>},
};
static constexpr ComponentDescriptor kStatusBar = {
    "StatusBar", kConstructors, kMethods,
    "A three-region status bar; ordinary children fill the center and named "
    "left/right slots pin content to each edge.",
    &Materialize};

} // namespace gpui::component_shell::display::status_bar

namespace gpui::component_shell {

bool RegisterDisplayStatusBar(shell::ComponentRegistry* registry,
                              shell::RegistryError* error) {
    return registry->Register(&display::status_bar::kStatusBar, error);
}

} // namespace gpui::component_shell
