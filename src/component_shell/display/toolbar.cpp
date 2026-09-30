// crates/component-shell/src/shell/display/toolbar.rs

#include "component_shell/families.h"
#include "component_shell/display/common.h"
#include "ui/toolbar.h"

namespace gpui::component_shell::display::toolbar {

struct ToolbarPayload {
    Str id;
};

static bool Construct(PayloadBuild* build, const ComponentArgument* args, int) {
    if (!common::NonEmptyId(build, "Toolbar", args[0].string)) return false;
    build->New<ToolbarPayload>()->id = args[0].string;
    return true;
}

static El* Materialize(MaterializeRequest* request) {
    const ToolbarPayload* payload = request->PayloadAs<ToolbarPayload>();
    if (!payload)
        return request->Fail(StrL("Toolbar received an incompatible payload"));
    component::Toolbar* toolbar =
        component::Toolbar::New(request->cx, payload->id);
    // Toolbar's ParentElement: ordinary children are non-sized content, in
    // source order.
    El** children = nullptr;
    int count = 0;
    if (!request->TakeChildren(&children, &count)) return nullptr;
    toolbar->Contents(children, count);
    return request->ApplyStyle(toolbar->IntoEl());
}

static constexpr ArgumentDescriptor kIdArgs[] = {{"id", SchemaString()}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Toolbar", kIdArgs, &Construct}};
static constexpr ComponentDescriptor kToolbar = {
    "Toolbar",
    kConstructors,
    {},
    "A transparent, sizable toolbar container whose controls render in source "
    "order.",
    &Materialize};

} // namespace gpui::component_shell::display::toolbar

namespace gpui::component_shell {

bool RegisterDisplayToolbar(shell::ComponentRegistry* registry,
                            shell::RegistryError* error) {
    return registry->Register(&display::toolbar::kToolbar, error);
}

} // namespace gpui::component_shell
