#ifndef GPUI_BASE_BUTTON_H_
#define GPUI_BASE_BUTTON_H_
/* Unstyled button — crates/base/src/button.rs */

#include "base/state_style.h"

namespace gpui {

// Rust's `Button::new(id).disabled(..).on_click(..).focusable(..)`. The button
// owns identity, focus and the click; layout, color and typography stay with
// the caller.
//
// `focusable` is Rust's own escape hatch, and its reasoning carries over
// exactly: a non-focusable button leaves focus where it already is, so a
// composed control does not flicker its ring on every press — and it gives up
// Enter and Space with it, so use it only where a focused sibling already
// exposes the action to the keyboard, the way a number input's step buttons
// do. A disabled button keeps its element id and does not activate. A
// focusable one stays in the focus list as a non-stop, so a button that
// disables itself while focused does not fall out of Tab in either
// direction, and Tab does not land on it again until it is enabled.
// ButtonStyles, which is what `Button::styles(|s| s.selected(..).disabled(..))`
// builds. `resolve_style` puts them in the one fixed order — the value state,
// then disabled — so no two buttons drift apart, and both land at layout, so
// they win over the look the caller chains onto the element afterwards.
// The neutral control geometry Button, Tab and Toggle share, named `id`
// among its siblings. Centering is control geometry: an ordinary child of a
// fixed-size control sits in the middle on both axes, independent of the
// caller's padding. relative(1): the inherited text leading must not make
// the control taller than the geometry its caller requested.
El* ControlBox(Arena* a, Str id);
// The focus an enabled checkbox or switch takes: the caller's FocusHandle
// when it gave one, its own path otherwise, then its place in the Tab order.
El* ControlFocus(El* e, FocusHandle focus, Str id, int tabIndex, bool tabStop);

struct ButtonStyles {
    StateStyle selected = {};
    StateStyle disabled = {};
};

struct Button {
    static El* New(Ctx* cx, Str id, bool disabled = false,
                   Listener onClick = {}, bool focusable = true,
                   const ButtonStyles* styles = nullptr, bool selected = false);
};
} // namespace gpui
#endif // GPUI_BASE_BUTTON_H_
