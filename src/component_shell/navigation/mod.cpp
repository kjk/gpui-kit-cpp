// crates/component-shell/src/shell/navigation/mod.rs

#include "component_shell/families.h"

namespace gpui::component_shell {

bool RegisterNavigation(shell::ComponentRegistry* registry,
                        shell::RegistryError* error) {
    return RegisterNavigationIcon(registry, error) &&
           RegisterNavigationSidebar(registry, error);
}

} // namespace gpui::component_shell
