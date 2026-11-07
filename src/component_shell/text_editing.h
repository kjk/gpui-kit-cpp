#ifndef GPUI_COMPONENT_SHELL_TEXT_EDITING_H_
#define GPUI_COMPONENT_SHELL_TEXT_EDITING_H_

// What media's Editor and layout's Textarea record the same way: one state
// entity as the payload, and the appearance / bordered / readonly /
// aria_label methods support.rs and media write with bool_method. Only the
// component's name in the error messages differs.

#include "component_shell/support.h"

namespace gpui::component_shell::text_editing {

struct Op {
    enum Kind : uint8_t {
        Appearance,
        Bordered,
        Readonly,
        AriaLabel,
    } kind = Appearance;
    bool flag = false;
    Str text;
};

// `Editor(state)`: one `stateName` entity, kept whole as the payload.
bool RecordState(PayloadBuild* build, const char* component,
                 const char* stateName, const ComponentArgument* args,
                 int count);
// bool_method(component, name, ...) for the three boolean policies.
bool RecordBool(PayloadBuild* build, const char* component, Op::Kind kind,
                const ComponentArgument* args, int count);
bool RecordAriaLabel(PayloadBuild* build, const char* component,
                     const ComponentArgument* args, int count);

// The recorders in a method table's shape, bound to the component's name.
template <const char* Component, Op::Kind K>
bool RecordBoolFor(PayloadBuild* build, const ComponentArgument* args,
                   int count) {
    return RecordBool(build, Component, K, args, count);
}
template <const char* Component>
bool RecordAriaLabelFor(PayloadBuild* build, const ComponentArgument* args,
                        int count) {
    return RecordAriaLabel(build, Component, args, count);
}

// Replays the recorded methods onto an Editor or a Textarea.
template <class W>
void ApplyOps(const MaterializeRequest* request, W* widget) {
    EachMethod<Op>(request, [&](const Op& op) {
        switch (op.kind) {
            case Op::Appearance:
                widget->Appearance(op.flag);
                break;
            case Op::Bordered:
                widget->Bordered(op.flag);
                break;
            case Op::Readonly:
                widget->Readonly(op.flag);
                break;
            case Op::AriaLabel:
                widget->AriaLabel(op.text);
                break;
        }
    });
}

inline constexpr ArgumentDescriptor kAppearanceArgs[] = {
    {"appearance", SchemaBoolean()}};
inline constexpr ArgumentDescriptor kBorderedArgs[] = {
    {"bordered", SchemaBoolean()}};
inline constexpr ArgumentDescriptor kReadonlyArgs[] = {
    {"readonly", SchemaBoolean()}};
inline constexpr ArgumentDescriptor kAriaLabelArgs[] = {
    {"label", SchemaString()}};

} // namespace gpui::component_shell::text_editing

#endif // GPUI_COMPONENT_SHELL_TEXT_EDITING_H_
