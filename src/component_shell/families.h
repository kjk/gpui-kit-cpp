#ifndef GPUI_COMPONENT_SHELL_FAMILIES_H_
#define GPUI_COMPONENT_SHELL_FAMILIES_H_

// crates/component-shell/src/shell/mod.rs: one registration per family, in
// the order `register` calls them.

#include "shell/component_registry.h"

namespace gpui::component {
struct NativeMenu;
}

namespace gpui::component_shell {

using RegisterFamily = bool (*)(shell::ComponentRegistry* registry,
                                shell::RegistryError* error);

bool RegisterSpinner(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterSeparator(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterSkeleton(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterChat(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterEmpty(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterInputGroup(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterControls(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterDelegateCollections(shell::ComponentRegistry*,
                                 shell::RegistryError*);
bool RegisterDelegateCombobox(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterDelegateSelect(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterDataTable(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterDisplay(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterCompound(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterTypedCompound(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterLifecycle(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterCollections(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterCommand(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterWindowEffects(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterOverlays(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterRetainedForms(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterLayout(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterMedia(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterScroll(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterSettings(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterStructured(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterNavigation(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterBasic(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterChart(shell::ComponentRegistry*, shell::RegistryError*);

// A family's own modules, in the order its mod.rs registers them.

// controls/mod.rs
bool RegisterControlsAction(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterControlsDisplay(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterControlsText(shell::ComponentRegistry*, shell::RegistryError*);

// delegate_collections/mod.rs
bool RegisterDelegateCollectionsList(shell::ComponentRegistry*,
                                     shell::RegistryError*);

// list.rs test_probe, widened into a seam: when set, it hears the id of every
// List row whose renderer built an element, in the order they were built.
using ListRowProbe = void (*)(Str id);
void SetListRowProbe(ListRowProbe probe);

// delegate_combobox/mod.rs test_probe, widened into a seam: when set, it
// hears every ComboboxEvent a retained Combobox host receives, before the
// script callback — `confirm` false for Change — with the event's values.
using ComboboxEventProbe = void (*)(bool confirm, const Str* values, int count);
void SetComboboxEventProbe(ComboboxEventProbe probe);

// delegate_select/mod.rs test_probe, widened into a seam: when set, it hears
// the value of every SelectEvent::Confirm a retained Select host receives,
// before the script callback.
using SelectProbe = void (*)(Str value);
void SetSelectProbe(SelectProbe probe);

// data_table/mod.rs test_probe, widened into a seam: when set, it hears
// every DataTable cell the renderer built (true) and every rows-snapshot or
// cell failure the table rendered in its place (false).
using DataTableProbe = void (*)(bool built);
void SetDataTableProbe(DataTableProbe probe);

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

// lifecycle/mod.rs
bool RegisterLifecycleTooltip(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterLifecycleMenu(shell::ComponentRegistry*, shell::RegistryError*);

// collections/mod.rs
bool RegisterCollectionsTree(shell::ComponentRegistry*, shell::RegistryError*);

// tree.rs test_probe, widened into a seam: when set, it hears every Tree row
// the native tree builds, with its item's id and label and whether it is
// selected.
using TreeRowProbe = void (*)(Str id, Str label, bool selected);
void SetTreeRowProbe(TreeRowProbe probe);

// command/mod.rs
bool RegisterCommandCommand(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterCommandNativeMenu(shell::ComponentRegistry*,
                               shell::RegistryError*);

// native_menu.rs test_probe, widened into a seam: when set, a
// NativeMenuTrigger's keyed show effect hands the menu it built here instead
// of showing it, and the effect fails with `*error` when this answers false.
// A menu the OS takes over has nothing a test can reach, which is why Rust
// counts shows behind #[cfg(test)]; this is that count plus the menu.
using NativeMenuShowProbe = bool (*)(const component::NativeMenu* menu,
                                     Str* error, Arena* a);
void SetNativeMenuShowProbe(NativeMenuShowProbe probe);

// window_effects/mod.rs test_probe, widened into a seam: when set, it hears
// the diagnosis logged when a surface's content factory failed and the
// effect's error reporter failed as well.
using WindowEffectsReporterFailureProbe = void (*)(Str diagnosis);
void SetWindowEffectsReporterFailureProbe(
    WindowEffectsReporterFailureProbe probe);

// overlays/mod.rs
bool RegisterOverlaysHoverCard(shell::ComponentRegistry*,
                               shell::RegistryError*);
bool RegisterOverlaysPopover(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterOverlaysDropdownMenu(shell::ComponentRegistry*,
                                  shell::RegistryError*);

// layout/mod.rs
bool RegisterLayoutTextarea(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterLayoutResizable(shell::ComponentRegistry*, shell::RegistryError*);

// media/mod.rs
bool RegisterMediaImage(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterMediaEditor(shell::ComponentRegistry*, shell::RegistryError*);

// scroll/mod.rs
bool RegisterScrollScroll(shell::ComponentRegistry*, shell::RegistryError*);

// structured/mod.rs
bool RegisterStructuredDescriptionList(shell::ComponentRegistry*,
                                       shell::RegistryError*);
bool RegisterStructuredForm(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterStructuredTable(shell::ComponentRegistry*, shell::RegistryError*);

// navigation/mod.rs
bool RegisterNavigationIcon(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterNavigationSidebar(shell::ComponentRegistry*,
                               shell::RegistryError*);

// basic/mod.rs
bool RegisterBasicText(shell::ComponentRegistry*, shell::RegistryError*);
bool RegisterBasicDropdownButton(shell::ComponentRegistry*,
                                 shell::RegistryError*);

// chart/mod.rs test_probe, widened into a seam: when set, it hears the
// error a chart rendered in place of its data ("Failed to build X data: ..").
using ChartErrorProbe = void (*)(Str error);
void SetChartErrorProbe(ChartErrorProbe probe);

} // namespace gpui::component_shell
#endif // GPUI_COMPONENT_SHELL_FAMILIES_H_
