// crates/component-shell/src/shell/structured/mod.rs

#include "component_shell/families.h"

namespace gpui::component_shell {

bool RegisterStructured(shell::ComponentRegistry* registry,
                        shell::RegistryError* error) {
    return RegisterStructuredDescriptionList(registry, error) &&
           RegisterStructuredForm(registry, error) &&
           RegisterStructuredTable(registry, error);
}

} // namespace gpui::component_shell
