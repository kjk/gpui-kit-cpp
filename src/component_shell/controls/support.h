#ifndef GPUI_COMPONENT_SHELL_CONTROLS_SUPPORT_H_
#define GPUI_COMPONENT_SHELL_CONTROLS_SUPPORT_H_

// crates/component-shell/src/shell/controls/support.rs: the operations the
// two-state controls (Checkbox, Switch, Toggle) share, and the descriptors
// that record them. `bool_method`, `string_method`, `on_click_method` and
// `disabled_method` are the shell-wide ones in component_shell/support.h.

#include "component_shell/support.h"

namespace gpui::component_shell::controls::support {

// CommonOp.
struct CommonOp {
    enum Kind : uint8_t {
        Size,
        Label,
        Tooltip,
        Checked,
        Outline,
        Change,
    } kind = Size;
    UiSize size = UiSize::Medium;
    Str text;
    bool checked = false;
    // Change: the callback argument, resolved when the control is built.
    shell::ComponentArgument change = {};
};

bool RecordLabel(PayloadBuild* build, const ComponentArgument* args, int);
bool RecordTooltip(PayloadBuild* build, const ComponentArgument* args, int);
bool RecordChecked(PayloadBuild* build, const ComponentArgument* args, int);
bool RecordSize(PayloadBuild* build, const ComponentArgument* args, int);
bool RecordOutline(PayloadBuild* build, const ComponentArgument*, int);
bool RecordChange(PayloadBuild* build, const ComponentArgument* args, int);

inline constexpr ArgumentDescriptor kChangeArguments[] = {
    {"on_change", SchemaCallback("(checked: boolean, cx: Context) => void")}};
inline constexpr ArgumentDescriptor kSizeArguments[] = {
    {"size", SchemaEnum(kSizeLiterals)}};

// change_method: without it a two-state control is set-only — the script
// owns `checked` and a click has nowhere to report to, so the control looks
// interactive and never changes.
inline constexpr MethodDescriptor kChangeMethod = {
    "on_change", kChangeArguments,
    "Reports the new checked state after a click.", &RecordChange};
inline constexpr MethodDescriptor kSizeMethod = {
    "size", kSizeArguments, "Sets the semantic control size.", &RecordSize};
inline constexpr MethodDescriptor kOutlineMethod = {
    "outline",
    {},
    "Uses the component's outline presentation.",
    &RecordOutline};

} // namespace gpui::component_shell::controls::support
#endif // GPUI_COMPONENT_SHELL_CONTROLS_SUPPORT_H_
