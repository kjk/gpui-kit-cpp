// crates/component-shell/src/shell/skeleton.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "ui/skeleton.h"

namespace gpui::component_shell::skeleton {

struct SkeletonPayload {};
struct Secondary {};

static bool Construct(PayloadBuild* build, const ComponentArgument*, int) {
    return build->Mark<SkeletonPayload>();
}

static bool RecordSecondary(PayloadBuild* build, const ComponentArgument*,
                            int) {
    return build->Mark<Secondary>();
}

static El* Materialize(MaterializeRequest* request) {
    if (!shell::PayloadIs<SkeletonPayload>(request->payload))
        return request
            ->Fail(StrL("Skeleton received an incompatible "
                        "payload"));
    component::Skeleton* skeleton = component::Skeleton::New(request->cx);
    EachMethod<Secondary>(request,
                          [&](const Secondary&) { skeleton->Secondary(); });
    return request->ApplyStyle(skeleton->IntoEl());
}

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Skeleton", {}, &Construct}};
static constexpr MethodDescriptor kMethods[] = {
    {"secondary", {}, "Uses the secondary skeleton color.", &RecordSecondary},
};
static constexpr ComponentDescriptor kSkeleton = {
    "Skeleton", kConstructors, kMethods, "An animated loading placeholder.",
    &Materialize};

} // namespace gpui::component_shell::skeleton

namespace gpui::component_shell {

bool RegisterSkeleton(shell::ComponentRegistry* registry,
                      shell::RegistryError* error) {
    return registry->Register(&skeleton::kSkeleton, error);
}

} // namespace gpui::component_shell
