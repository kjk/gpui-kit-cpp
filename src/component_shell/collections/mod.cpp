// crates/component-shell/src/shell/collections/mod.rs: the typed Tree
// binding. List, Select and Combobox are their own delegate families
// (delegate_collections, delegate_select, delegate_combobox); DataTable,
// SearchableList and VirtualList stay deferred, as the Rust module says.

#include "component_shell/families.h"

namespace gpui::component_shell {

bool RegisterCollections(shell::ComponentRegistry* registry,
                         shell::RegistryError* error) {
    return RegisterCollectionsTree(registry, error);
}

} // namespace gpui::component_shell
