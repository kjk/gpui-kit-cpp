#ifndef GPUI_BASE_TOGGLE_H_
#define GPUI_BASE_TOGGLE_H_
/* Unstyled toggle — crates/base/src/toggle.rs */

#include "base/state_style.h"

namespace gpui {

struct ToggleStyles {
    StateStyle pressed = {};
    StateStyle disabled = {};

    ToggleStyles& Pressed(const StateStyle& style);
    ToggleStyles& Disabled(const StateStyle& style);
};

// Rust's `Toggle::new(id).pressed(..).disabled(..).on_change(..)`: a
// controlled toggle button that owns identity, focus and activation and
// leaves every pixel to the caller. `onChange` is handed the value the
// activation produces — `!pressed` — which a
// `void On(T*, Ctx*, const ClickEvent*, int64_t next)` reads as a bool. A
// disabled toggle keeps its element id but takes neither focus nor the click.
struct Toggle {
    static El* New(Ctx* cx, Str id, bool pressed = false, bool disabled = false,
                   Listener onChange = {}, const ToggleStyles* styles = nullptr,
                   const StateStyle* instance = nullptr);
};
} // namespace gpui
#endif // GPUI_BASE_TOGGLE_H_
