// crates/component-shell/src/shell/controls/mod.rs: stateless visual-control
// bindings.

#include "component_shell/families.h"

namespace gpui::component_shell {

bool RegisterControls(shell::ComponentRegistry* registry,
                      shell::RegistryError* error) {
    return RegisterControlsAction(registry, error) &&
           RegisterControlsDisplay(registry, error) &&
           RegisterControlsText(registry, error);
}

} // namespace gpui::component_shell
