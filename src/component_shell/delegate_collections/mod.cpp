// crates/component-shell/src/shell/delegate_collections/mod.rs: the retained
// List bound to an immutable rows snapshot and a lazy row renderer.

#include "component_shell/families.h"

namespace gpui::component_shell {

bool RegisterDelegateCollections(shell::ComponentRegistry* registry,
                                 shell::RegistryError* error) {
    return RegisterDelegateCollectionsList(registry, error);
}

} // namespace gpui::component_shell
