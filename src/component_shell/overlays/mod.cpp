// crates/component-shell/src/shell/overlays/mod.rs: overlay components that
// can be expressed as elements.
//
// Popover and HoverCard consume repeatable slot factories because their
// native content builders run after the registered materialization request.
// DropdownMenu is intentionally narrower: closed label/callback item specs
// build the real native popup menu without exposing a delegate or menu IR.
// Dialog, AlertDialog, Sheet and a standalone Tooltip remain absent, as in
// Rust (see mod.rs there for why).

#include "component_shell/families.h"

namespace gpui::component_shell {

bool RegisterOverlays(shell::ComponentRegistry* registry,
                      shell::RegistryError* error) {
    return RegisterOverlaysHoverCard(registry, error) &&
           RegisterOverlaysPopover(registry, error) &&
           RegisterOverlaysDropdownMenu(registry, error);
}

} // namespace gpui::component_shell
