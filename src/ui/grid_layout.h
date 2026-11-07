#ifndef GPUI_SRC_UI_GRID_LAYOUT_H_
#define GPUI_SRC_UI_GRID_LAYOUT_H_
/* Themed grid — crates/component/src/grid_layout.rs.

   Base owns placement, dragging and the keyboard. This paints the theme
   gap, the settle motion, the drop placeholder, the resize grip and the
   rings on the dragged, selected and focused items. The base names come
   through with it: GridLayout, GridLayoutState, GridArea, GridItem,
   GridDragHandle, GridLayoutEvent. */

#include "base/grid_layout.h"

namespace gpui {
namespace component {

// grid_layout(id, state, cx).
GridLayout* grid_layout(Ctx* cx, Str id, Entity<GridLayoutState> state);

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_GRID_LAYOUT_H_
