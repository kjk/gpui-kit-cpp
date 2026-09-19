#ifndef GPUI_UI_TOUCH_SELECTION_H_
#define GPUI_UI_TOUCH_SELECTION_H_
/* Themed touch-selection overlay — crates/ui/src/touch_selection.

   Base owns the gesture and the drag. This module draws the edit menu a long
   press leaves over the window text selection. Root mounts one per window
   after the content, so the menu floats above whatever was selected. Handles
   are painted by the text that owns them. */

#include "ui/sizing.h"
#include "base/touch_selection.h"

namespace gpui {

namespace component {

struct EditMenuItem {
    Str label = {};
    Listener onClick = {};
};

// The row of commands a touch selection offers: Copy, Select All —
// whichever apply. It floats above the selection, or below it when there
// is no room above, and stays out of the way of the handles' knobs.
struct EditMenu {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Bounds anchor = {};
    ArenaVec<EditMenuItem> items;

    static EditMenu* New(Ctx* cx, Str id, Bounds anchor);
    EditMenu* Item(Str label, Listener onClick);
    El* IntoEl();
};

// Draws one touch selection: when open, its edit menu. An owner whose text
// paints its own handles in place leaves those unset, which is what the
// window overlay does.
struct TouchSelectionOverlay {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    TouchSelectionSnapshot snapshot = {};
    bool hasSnapshot = false;
    ArenaVec<EditMenuItem> items;

    static TouchSelectionOverlay* New(Ctx* cx, Str id);
    TouchSelectionOverlay* Snapshot(const TouchSelectionSnapshot& snap);
    TouchSelectionOverlay* Item(Str label, Listener onClick);
    El* IntoEl();
};

// Root mounts this after the page: Copy and Select All over the window
// text selection. Null when there is no touch selection.
El* WindowTouchSelectionOverlay(Ctx* cx);

} // namespace component
} // namespace gpui
#endif // GPUI_UI_TOUCH_SELECTION_H_
