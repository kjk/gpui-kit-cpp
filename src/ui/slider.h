#ifndef GPUI_SRC_UI_SLIDER_H_
#define GPUI_SRC_UI_SLIDER_H_
/* Themed slider — crates/ui/src/slider.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

struct Slider {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    // Each slider on a page needs its own click id, or they share a hover
    // and the hit test cannot tell them apart.
    Str id = {};
    // The value, its limits and its scale, owned by the view — Rust's
    // `Entity<SliderState>`. The widget reads the percentages it has already
    // worked out and the window writes to it on a press or a drag.
    SliderState* state = nullptr;
    // reverse(): the fill runs from the far end back toward the thumb, for
    // "remaining capacity" readings.
    bool reverse = false;
    bool disabled = false;
    // vertical(): the track runs bottom-to-top, and `width` is its length.
    Axis axis = Axis::Horizontal;
    // The track's length along its axis. kFill is Rust's `w_full()` inside a
    // `flex_1` root: the parts are then placed by `left(relative(..))` rather
    // than by pixels, so the slider can share a row with anything.
    // kAuto is Rust's own: w_full() across, 120px upright.
    float width = kAuto;
    // Styled::bg on the slider — `bar_color` in slider.rs. The rail takes it
    // at 20%, the filled part whole and the thumb's ring at 50%. Unset is the
    // theme's own pair. The colour picker passes a transparent one so only
    // the gradient behind the slider shows.
    Rgba bar = {};
    bool hasBar = false;
    Listener onChange;

    static Slider* New(Ctx* cx, Str id, SliderState* state);
    Slider* Reverse(bool v = true);
    Slider* Vertical(bool v = true);
    Slider* WithAxis(Axis v);
    Slider* Disabled(bool v = true);
    Slider* W(float px);
    // w_full(): the track fills the box it is given.
    Slider* WFill();
    Slider* Bg(Rgba c);
    // cx.subscribe(&state, ..): the view hears SliderEvent::Change while the
    // slider moves and Release when the button comes back up.
    Slider* OnChange(Listener fn);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_SLIDER_H_
