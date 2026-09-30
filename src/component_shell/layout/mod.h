#ifndef GPUI_COMPONENT_SHELL_LAYOUT_MOD_H_
#define GPUI_COMPONENT_SHELL_LAYOUT_MOD_H_

// crates/component-shell/src/shell/layout/: retained textarea and typed
// resizable-layout bindings. The helpers the modules' own tests reach.

#include "component_shell/support.h"

namespace gpui::component_shell::layout::textarea {

// require_leaf: "Textarea does not accept children".
bool RequireLeaf(int children, Str* error);

} // namespace gpui::component_shell::layout::textarea

namespace gpui::component_shell::layout::resizable {

// finite_positive: "<label> expects a positive finite pixel value".
bool FinitePositive(double value, const char* label, float* out, Str* error);
// require_group_style: a Resizable takes no style of its own.
bool RequireGroupStyle(bool styled, Str* error);
// require_panel_child: `actual` is the registered child's name, or null for
// an ordinary element.
bool RequirePanelChild(const char* actual, Str* error);

} // namespace gpui::component_shell::layout::resizable
#endif // GPUI_COMPONENT_SHELL_LAYOUT_MOD_H_
