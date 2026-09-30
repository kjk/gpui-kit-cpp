// crates/component-shell/src/shell/compound/mod.rs: compound components that
// can be represented without retaining typed child state.

#include "component_shell/families.h"

namespace gpui::component_shell {

bool RegisterCompound(shell::ComponentRegistry* registry,
                      shell::RegistryError* error) {
    return RegisterCompoundAvatar(registry, error) &&
           RegisterCompoundCollapsible(registry, error) &&
           RegisterCompoundPagination(registry, error) &&
           RegisterCompoundProgress(registry, error) &&
           RegisterCompoundRadio(registry, error);
}

} // namespace gpui::component_shell
