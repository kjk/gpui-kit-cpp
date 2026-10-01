#ifndef GPUI_COMPONENT_SHELL_LIB_H_
#define GPUI_COMPONENT_SHELL_LIB_H_

// crates/component-shell: the gpui-component catalog for the shell runtime.
//
// This is the only place concrete component knowledge meets the shell. The
// dependency runs one way: the runtime (src/shell) knows neither this nor
// src/ui, so it stays usable without a component catalog, and this adapter
// registers the themed components (src/ui) into the registry the runtime
// installs as the `gpui-component` module.

#include "shell/component_registry.h"

namespace gpui::component_shell {

// lib.rs `init`: the component library's globals and the runtime's, once at
// startup, before any script runs.
void Init(App* app);

// lib.rs `components`: builds and freezes the catalog, once per process. The
// catalog is static data and outlives every runtime built over it.
const shell::FrozenComponentRegistry* Components();

// lib.rs `open_window_with_root`: the catalog's ComponentWindowOpener. Opens
// the window with the component library's Root as its root view, around
// what `build` answers.
//
// Every gpui-component overlay — dialog, alert dialog, sheet, notification —
// finds its host through the window's Root and draws nowhere when the window
// is rooted at anything else. The runtime installs its own ShellRoot and
// cannot name Root, so the catalog that needs one supplies it here. ShellRoot
// keeps rendering inside it, and its own overlays with it.
Window* OpenWindowWithRoot(App* app,
                           const shell::ComponentWindowOptions& options,
                           shell::ComponentWindowBuild build, void* data);

// The half of OpenWindowWithRoot after the window exists: builds the content
// and mounts the Root around it. Split out so the root can be checked without
// an OS window.
void MountWindowRoot(App* app, Window* window,
                     shell::ComponentWindowBuild build, void* data);

// lib.rs `register`: every family, in the order Rust registers them. False
// with `error` set if a descriptor is refused.
bool Register(shell::ComponentRegistry* registry, shell::RegistryError* error);

} // namespace gpui::component_shell
#endif // GPUI_COMPONENT_SHELL_LIB_H_
