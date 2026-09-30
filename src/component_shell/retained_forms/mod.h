#ifndef GPUI_COMPONENT_SHELL_RETAINED_FORMS_MOD_H_
#define GPUI_COMPONENT_SHELL_RETAINED_FORMS_MOD_H_

// crates/component-shell/src/shell/retained_forms/mod.rs: the helpers its
// own tests reach.

#include "component_shell/support.h"

namespace gpui::component_shell::retained_forms {

// positive_usize: one exact positive integer below 2^64, or "<callable>
// expects a positive integer" in `error`.
bool PositiveUsize(const ComponentArgument* args, int count,
                   const char* callable, uint64_t* out, Str* error);

// ensure_leaf: "<component> does not accept children" when there are any.
bool EnsureLeaf(int childrenLen, const char* component, Str* error);

} // namespace gpui::component_shell::retained_forms
#endif // GPUI_COMPONENT_SHELL_RETAINED_FORMS_MOD_H_
