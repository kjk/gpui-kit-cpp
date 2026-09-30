// crates/component-shell/src/shell/display/breadcrumb.rs

#include "component_shell/families.h"
#include "component_shell/display/common.h"
#include "ui/breadcrumb.h"

namespace gpui::component_shell::display::breadcrumb {

// BreadcrumbPayload: the labels, in order. They are the validated argument's
// own items, which live in the recording arena already.
struct BreadcrumbPayload {
    const ComponentArgument* labels = nullptr;
    int count = 0;
};

static bool Construct(PayloadBuild* build, const ComponentArgument* args, int) {
    BreadcrumbPayload* payload = build->New<BreadcrumbPayload>();
    payload->labels = args[0].items;
    payload->count = args[0].count;
    return true;
}

static El* Materialize(MaterializeRequest* request) {
    if (!common::EnsureNoChildren(request, "Breadcrumb")) return nullptr;
    const BreadcrumbPayload* payload = request->PayloadAs<BreadcrumbPayload>();
    if (!payload)
        return request
            ->Fail(StrL("Breadcrumb received an incompatible payload"));
    component::Breadcrumb* trail = component::Breadcrumb::New(request->cx);
    for (int i = 0; i < payload->count; i++) {
        trail->Child(payload->labels[i].string);
    }
    return request->ApplyStyle(trail->IntoEl());
}

static constexpr ArgumentSchema kLabel = SchemaString();
static constexpr ArgumentDescriptor kLabelsArgs[] = {
    {"labels", SchemaArray(&kLabel)}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Breadcrumb", kLabelsArgs, &Construct}};
static constexpr ComponentDescriptor kBreadcrumb = {
    "Breadcrumb",
    kConstructors,
    {},
    "A navigation trail built from an ordered array of labels.",
    &Materialize};

} // namespace gpui::component_shell::display::breadcrumb

namespace gpui::component_shell {

bool RegisterDisplayBreadcrumb(shell::ComponentRegistry* registry,
                               shell::RegistryError* error) {
    return registry->Register(&display::breadcrumb::kBreadcrumb, error);
}

} // namespace gpui::component_shell
