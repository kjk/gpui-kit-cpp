#ifndef GPUI_SRC_UI_RESIZABLE_H_
#define GPUI_SRC_UI_RESIZABLE_H_
/* Themed resizable panels — crates/component/src/resizable.rs

   What this design system paints inside a resize handle. Base owns the band,
   the cursor and the drag (`base/resizable.h`); everything here is
   appearance. A divider rests as the same hairline it has always been, and
   answers the pointer with a pill that grows and solidifies as the pointer
   engages it: available, held, being dragged. */

#include "base/resizable.h"
#include "ui/sizing.h"

namespace gpui {

namespace component {

using ResizableState = gpui::ResizableState;

// INDICATOR_THICKNESS: how thick the indicator is across its divider.
const float kResizeIndicatorThickness = 3.f;

// indicator: how long the pill is at each level of engagement, and how
// solid. Idle draws nothing -- the hairline is a divider's resting look.
struct ResizeIndicator {
    float length = 0;
    float opacity = 0;
};
ResizeIndicator ResizeHandleIndicator(ResizeHandleState state);

// render_resize_handle: the hairline, and the indicator riding on it.
El* RenderResizeHandle(void* user, const ResizeHandleContext* handle, Ctx* cx);

// resize_handle_appearance: this design system's divider appearance, for a
// handle base does not already hand it — a dock split, or a hand-rolled
// handle in an application. Pass it to WithAppearance /
// WithHandleAppearance with a null user.
inline ResizeHandleRenderer ResizeHandleAppearance() {
    return &RenderResizeHandle;
}

// resize_handle: the base band with this design system's divider on it,
// for an edge no panel group owns.
inline gpui::ResizeHandle* resize_handle(Ctx* cx, Str id, Axis axis) {
    return gpui::resize_handle(cx, id, axis)
        ->WithAppearance(nullptr, &RenderResizeHandle);
}

struct Resizable {
    // h_resizable / v_resizable: the base group with this design system's
    // handle appearance. The chain that follows — `W`, `Panel`, `Grow`,
    // `Flex`, `Visible`, `IntoEl` — is the base group's own.
    static gpui::Resizable* New(Ctx* cx, Str id,
                                Entity<ResizableState> state = {},
                                Axis axis = Axis::Horizontal);
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_RESIZABLE_H_
