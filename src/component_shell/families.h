#ifndef GPUI_COMPONENT_SHELL_FAMILIES_H_
#define GPUI_COMPONENT_SHELL_FAMILIES_H_

// crates/component-shell/src/shell/mod.rs: one registration per family, in
// the order `register` calls them.

#include "shell/component_registry.h"

namespace gpui::component_shell {

using RegisterFamily = bool (*)(shell::ComponentRegistry* registry,
                                shell::RegistryError* error);

bool RegisterSpinner(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterSeparator(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterSkeleton(shell::ComponentRegistry*, shell::RegistryError*);

} // namespace gpui::component_shell
#endif // GPUI_COMPONENT_SHELL_FAMILIES_H_
