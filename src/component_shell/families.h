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
bool RegisterEmpty(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterInputGroup(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterControls(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterDisplay(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterCompound(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterTypedCompound(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterRetainedForms(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterLayout(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterBasic(shell::ComponentRegistry*, shell::RegistryError*);

// A family's own modules, in the order its mod.rs registers them.

// controls/mod.rs
bool RegisterControlsAction(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterControlsDisplay(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterControlsText(shell::ComponentRegistry*, shell::RegistryError*);

// display/mod.rs
bool RegisterDisplayAlert(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterDisplayBreadcrumb(shell::ComponentRegistry*,
                               shell::RegistryError*);
bool RegisterDisplayClipboard(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterDisplayGroupBox(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterDisplayRating(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterDisplayStatusBar(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterDisplayToolbar(shell::ComponentRegistry*, shell::RegistryError*);

// compound/mod.rs
bool RegisterCompoundAvatar(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterCompoundCollapsible(shell::ComponentRegistry*,
                                 shell::RegistryError*);
bool RegisterCompoundPagination(shell::ComponentRegistry*,
                                shell::RegistryError*);
bool RegisterCompoundProgress(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterCompoundRadio(shell::ComponentRegistry*, shell::RegistryError*);

// layout/mod.rs
bool RegisterLayoutTextarea(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterLayoutResizable(shell::ComponentRegistry*, shell::RegistryError*);

// basic/mod.rs
bool RegisterBasicText(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterBasicDropdownButton(shell::ComponentRegistry*,
                                 shell::RegistryError*);

} // namespace gpui::component_shell
#endif // GPUI_COMPONENT_SHELL_FAMILIES_H_
