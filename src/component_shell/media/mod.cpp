// crates/component-shell/src/shell/media/mod.rs
//
// Renderable media bindings with closed resource and retained-state
// contracts.
//
// `Chart` is intentionally deferred: the native surface is a family of
// generic chart types whose accessor closures require a typed row carrier
// that the shell does not expose. `Plot` is a Rust paint/prepaint trait, not a
// constructor.

#include "component_shell/families.h"

namespace gpui::component_shell {

bool RegisterMedia(shell::ComponentRegistry* registry,
                   shell::RegistryError* error) {
    return RegisterMediaImage(registry, error) &&
           RegisterMediaEditor(registry, error);
}

} // namespace gpui::component_shell
