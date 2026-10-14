#ifndef GPUI_BASE_SLIDER_H_
#define GPUI_BASE_SLIDER_H_
/* Unstyled slider — crates/base/src/slider.rs */

#include "gpui/gpui.h"

namespace gpui {

struct Slider {
    // `accessibilityLabel` is the name a screen reader announces. Empty
    // leaves the node unnamed, which is Rust's `accessibility_label: None`.
    static El* New(Ctx* cx, SliderState* state = nullptr,
                   Axis axis = Axis::Horizontal, int clickId = 0,
                   Str accessibilityLabel = {});
};
// The box that catches a press. Binding the state here is what Rust's
// `SliderTrack::on_mouse_down` + `on_drag_move` do.
struct SliderTrack {
    static El* New(Ctx* cx, SliderState* state = nullptr,
                   Axis axis = Axis::Horizontal);
};
// The rail a value maps against. Rust's records its box in on_prepaint and
// wraps the filled part and the thumbs; here it is their sibling, which draws
// the same and keeps the absolute offsets against the track.
struct SliderIndicator {
    static El* New(Ctx* cx, SliderState* state = nullptr);
};
struct SliderThumb {
    static El* New(Ctx* cx);
};
} // namespace gpui
#endif // GPUI_BASE_SLIDER_H_
