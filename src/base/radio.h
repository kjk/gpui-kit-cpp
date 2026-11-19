#ifndef GPUI_BASE_RADIO_H_
#define GPUI_BASE_RADIO_H_
/* Unstyled radio — crates/base/src/radio.rs */

#include "base/state_style.h"

namespace gpui {

// Semantic root styles supported by Radio. Calls refine the existing state,
// matching repeated `.styles(|styles| ...)` builders in Rust.
struct RadioStyles {
    StateStyle checked = {};
    StateStyle disabled = {};

    RadioStyles& Checked(const StateStyle& style);
    RadioStyles& Disabled(const StateStyle& style);
};

// Rust's `Radio::new(id).checked(..).disabled(..).on_change(..)`. Selection is
// the caller's; activating an unchecked radio asks for `true` through
// `onChange`. Picking the one that is already picked is not an activation, so
// a checked radio takes focus but no click — which is what the accessibility
// test pins by refusing it the Click action. Disabling a focused radio
// preserves its position in the focus order.
struct Radio {
    static El* New(Ctx* cx, Str id, bool checked = false, bool disabled = false,
                   Listener onChange = {}, const RadioStyles* styles = nullptr,
                   const StateStyle* instance = nullptr, int tabIndex = 0,
                   bool tabStop = true, FocusHandle focus = {});
};
} // namespace gpui
#endif // GPUI_BASE_RADIO_H_
