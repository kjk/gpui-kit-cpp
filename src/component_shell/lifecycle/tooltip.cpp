// crates/component-shell/src/shell/lifecycle/tooltip.rs

#include "component_shell/families.h"
#include "component_shell/support.h"
#include "ui/button.h"

namespace gpui::component_shell::lifecycle::tooltip {

struct TooltipPayload {
    Str id;
    Str label;
    Str text;
};

static bool Construct(PayloadBuild* build, const ComponentArgument* args,
                      int count) {
    bool strings = count == 3;
    for (int i = 0; strings && i < 3; i++) {
        if (args[i].kind != shell::ComponentArgumentKind::String)
            strings = false;
    }
    if (!strings)
        return build
            ->Fail(StrL("Tooltip(id, label, text) expects three "
                        "strings"));
    for (int i = 0; i < 3; i++) {
        if (len(StrTrim(args[i].string)) == 0)
            return build
                ->Fail(StrL("Tooltip id, label, and text must not be "
                            "empty"));
    }
    TooltipPayload* payload = build->New<TooltipPayload>();
    payload->id = args[0].string;
    payload->label = args[1].string;
    payload->text = args[2].string;
    return true;
}

// Rust's #[cfg(test)] test_probe records every payload it materializes; the
// C++ test reads the materialized Button's tooltip off the element instead.
static El* Materialize(MaterializeRequest* request) {
    const TooltipPayload* payload = request->PayloadAs<TooltipPayload>();
    if (!payload)
        return request->Fail(StrL("Tooltip received an incompatible payload"));
    Ctx* cx = request->cx;
    return request->Finish(component::Button::New(cx, payload->id)
                               ->Label(payload->label)
                               ->Tooltip(payload->text)
                               ->IntoEl());
}

static constexpr ArgumentDescriptor kArgs[] = {{"id", SchemaString()},
                                               {"label", SchemaString()},
                                               {"text", SchemaString()}};
static constexpr ConstructorDescriptor kConstructors[] = {
    {"Tooltip", kArgs, &Construct}};
static constexpr ComponentDescriptor kTooltip = {
    "Tooltip",
    kConstructors,
    {},
    "A real gpui-component Button trigger with a managed text tooltip.",
    &Materialize};

} // namespace gpui::component_shell::lifecycle::tooltip

namespace gpui::component_shell {

bool RegisterLifecycleTooltip(shell::ComponentRegistry* registry,
                              shell::RegistryError* error) {
    return registry->Register(&lifecycle::tooltip::kTooltip, error);
}

} // namespace gpui::component_shell
