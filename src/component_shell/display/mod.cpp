// crates/component-shell/src/shell/display/mod.rs: stateless display and
// content component registrations.

#include "component_shell/families.h"

namespace gpui::component_shell {

bool RegisterDisplay(shell::ComponentRegistry* registry,
                     shell::RegistryError* error) {
    return RegisterDisplayAlert(registry, error) &&
           RegisterDisplayBreadcrumb(registry, error) &&
           RegisterDisplayClipboard(registry, error) &&
           RegisterDisplayGroupBox(registry, error) &&
           RegisterDisplayRating(registry, error) &&
           RegisterDisplayStatusBar(registry, error) &&
           RegisterDisplayToolbar(registry, error);
}

} // namespace gpui::component_shell
