// crates/component-shell/src/shell/lifecycle/mod.rs: concrete
// lifecycle-adjacent surfaces that can be mounted as ordinary elements.

#include "component_shell/families.h"

namespace gpui::component_shell {

bool RegisterLifecycle(shell::ComponentRegistry* registry,
                       shell::RegistryError* error) {
    return RegisterLifecycleTooltip(registry, error) &&
           RegisterLifecycleMenu(registry, error);
}

} // namespace gpui::component_shell
