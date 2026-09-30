// crates/component-shell/src/shell/basic/text.rs

#include "component_shell/families.h"
#include "component_shell/support.h"

namespace gpui::component_shell::basic::text {

struct TextPayload {
    Str value;
};

// text_payload.
static bool Construct(PayloadBuild* build, const ComponentArgument* args, int) {
    build->New<TextPayload>()->value = args[0].string;
    return true;
}

static El* Materialize(MaterializeRequest* request) {
    const TextPayload* payload = request->PayloadAs<TextPayload>();
    if (!payload)
        return request->Fail(StrL("Text received an incompatible payload"));
    if (request->ChildrenLen() != 0)
        return request
            ->Fail(StrL("Text does not accept children; pass its content to "
                        "Text(value)"));
    // `div().child(Text::from(value))`: a plain string is Text::String, which
    // renders as the text itself.
    Ctx* cx = request->cx;
    return request->Finish(Div(cx->a)->Child(TextEl(cx->a, payload->value)));
}

static constexpr ArgumentDescriptor kValueArgs[] = {{"value", SchemaString()}};

static constexpr ConstructorDescriptor kConstructors[] = {
    {"Text", kValueArgs, &Construct}};
static constexpr ComponentDescriptor kText = {
    "Text",
    kConstructors,
    {},
    "Plain gpui-component Text content in a styleable shell wrapper. Text "
    "accepts no children.",
    &Materialize};

} // namespace gpui::component_shell::basic::text

namespace gpui::component_shell {

bool RegisterBasicText(shell::ComponentRegistry* registry,
                       shell::RegistryError* error) {
    return registry->Register(&basic::text::kText, error);
}

} // namespace gpui::component_shell
