// crates/component-shell/src/shell/command/mod.rs: retained command palette
// bindings and the native menu trigger.

#include "component_shell/families.h"

namespace gpui::component_shell {

bool RegisterCommand(shell::ComponentRegistry* registry,
                     shell::RegistryError* error) {
    return RegisterCommandCommand(registry, error) &&
           RegisterCommandNativeMenu(registry, error);
}

} // namespace gpui::component_shell
