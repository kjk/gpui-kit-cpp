// crates/component-shell/src/shell/layout/mod.rs
//
// Retained textarea and typed resizable-layout bindings.
//
// `Scrollbar` is intentionally not registered: its public constructor requires
// a concrete `ScrollbarHandle`, and the shell does not expose scroll handles as
// retained entities. `ScrollableElement` is a Rust extension trait rather than
// a component constructor, so registering `Scroll` would fabricate an API.

#include "component_shell/families.h"

namespace gpui::component_shell {

bool RegisterLayout(shell::ComponentRegistry* registry,
                    shell::RegistryError* error) {
    return RegisterLayoutTextarea(registry, error) &&
           RegisterLayoutResizable(registry, error);
}

} // namespace gpui::component_shell
