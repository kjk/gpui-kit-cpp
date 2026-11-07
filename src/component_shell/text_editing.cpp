#include "component_shell/text_editing.h"

namespace gpui::component_shell::text_editing {

bool RecordState(PayloadBuild* build, const char* component,
                 const char* stateName, const ComponentArgument* args,
                 int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Entity) {
        return build->Fail(
            fmt("%s expects one %s entity", Str(component), Str(stateName)));
    }
    *build->New<ComponentArgument>() = args[0];
    return true;
}

bool RecordBool(PayloadBuild* build, const char* component, Op::Kind kind,
                const ComponentArgument* args, int count) {
    const char* name = kind == Op::Appearance ? "appearance"
                       : kind == Op::Bordered ? "bordered"
                                              : "readonly";
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean) {
        return build
            ->Fail(fmt("%s.%s expects one boolean", Str(component), Str(name)));
    }
    Op* op = build->New<Op>();
    op->kind = kind;
    op->flag = args[0].boolean;
    return true;
}

bool RecordAriaLabel(PayloadBuild* build, const char* component,
                     const ComponentArgument* args, int count) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String ||
        len(StrTrim(args[0].string)) == 0) {
        return build
            ->Fail(fmt("%s.aria_label expects non-empty text", Str(component)));
    }
    Op* op = build->New<Op>();
    op->kind = Op::AriaLabel;
    op->text = args[0].string;
    return true;
}

} // namespace gpui::component_shell::text_editing
