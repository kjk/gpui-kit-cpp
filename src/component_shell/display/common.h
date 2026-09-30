#ifndef GPUI_COMPONENT_SHELL_DISPLAY_COMMON_H_
#define GPUI_COMPONENT_SHELL_DISPLAY_COMMON_H_

// crates/component-shell/src/shell/display/common.rs: the checks the display
// family shares. size_operation is SizeOfLiteral (support.h): the schema has
// already refused anything but the four literals.

#include "component_shell/support.h"

namespace gpui::component_shell::display::common {

// non_empty_id: fails with "<component> id must not be empty".
bool NonEmptyId(PayloadBuild* build, const char* component, Str id);

// ensure_no_children: fails with "<component> does not accept child
// elements".
bool EnsureNoChildren(MaterializeRequest* request, const char* component);

} // namespace gpui::component_shell::display::common
#endif // GPUI_COMPONENT_SHELL_DISPLAY_COMMON_H_
