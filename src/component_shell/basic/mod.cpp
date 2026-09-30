// crates/component-shell/src/shell/basic/mod.rs: text and dropdown-button
// surfaces.

#include "component_shell/families.h"

namespace gpui::component_shell {

bool RegisterBasic(shell::ComponentRegistry* registry,
                   shell::RegistryError* error) {
    return RegisterBasicText(registry, error) &&
           RegisterBasicDropdownButton(registry, error);
}

} // namespace gpui::component_shell
