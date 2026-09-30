// crates/component-shell/src/shell/scroll/mod.rs
//
// Retained scrolling capability and its two honest render surfaces.
//
// `Scroll` is explicitly an adapter wrapper for `ScrollableElement` behavior;
// that extension-only trait is not registered as a constructor. `Scrollbar`
// materializes the real native base element and shares the same retained
// scroll handle with `Scroll`.

#include "component_shell/families.h"

namespace gpui::component_shell {

bool RegisterScroll(shell::ComponentRegistry* registry,
                    shell::RegistryError* error) {
    return RegisterScrollScroll(registry, error);
}

} // namespace gpui::component_shell
