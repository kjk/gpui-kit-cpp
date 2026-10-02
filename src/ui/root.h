#ifndef GPUI_SRC_UI_ROOT_H_
#define GPUI_SRC_UI_ROOT_H_
/* Component window state — crates/component/src/root.rs

   The window's root is Base's (`base/root.h`); `component::Root` is that
   type re-exported. What stays here is Component's per-window presentation,
   registered by component::Init as a Root plugin: the notifications, the
   sheet and the stack of dialogs over the page, the touch-selection menu,
   the theme's defaults for the root surface, and the window border around
   it. The layers are WindowState's, the plugin instance the window's Root
   holds (`WindowLayersOf`), which is where WindowExt's operations put them. */

#include "base/root.h"
#include "ui/sizing.h"
#include "ui/sheet.h"
#include "ui/window_border.h"
#include "ui/window_ext.h"

namespace gpui {

namespace component {

// pub use gpui_base::Root.
using Root = gpui::Root;

// Which dialog shows the overlay: the last one that asked for one, so a stack
// of dialogs tints the page once, under the topmost of them. -1 when none of
// them wants one.
int RootDialogOverlayIndex(const bool* wantsOverlay, int n);

// notification_layer: the notifications fill the window, less the room an
// open sheet takes on its own edge — so a sheet on the right pushes them
// left rather than covering them.
Edges RootNotificationInsets(bool hasSheet, SheetPlacement placement,
                             float size);

// WindowState's RootPlugin table. Its per-window state is the window's
// WindowLayers, made with the window's Root and dropped with it.
extern const RootPlugin kWindowStatePlugin;

// root::init: register WindowState as a Root plugin.
void RootInit(App* app);

// WindowStateLayers: the sheet, dialog and notification layers the window
// holds, for a host that renders its own root surface. Null when none is
// open.
El* WindowStateLayers(Ctx* cx);

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_ROOT_H_
