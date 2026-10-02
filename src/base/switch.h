#ifndef GPUI_BASE_SWITCH_H_
#define GPUI_BASE_SWITCH_H_
/* Unstyled switch — crates/base/src/switch.rs */

#include "base/state_style.h"

namespace gpui {

struct SwitchStyles {
    StateStyle checked = {};
    StateStyle disabled = {};

    SwitchStyles& Checked(const StateStyle& style);
    SwitchStyles& Disabled(const StateStyle& style);
};

struct SwitchTrackStyles {
    StateStyle checked = {};
    StateStyle disabled = {};

    SwitchTrackStyles& Checked(const StateStyle& style);
    SwitchTrackStyles& Disabled(const StateStyle& style);
};

struct SwitchThumbStyles {
    StateStyle checked = {};
    StateStyle disabled = {};

    SwitchThumbStyles& Checked(const StateStyle& style);
    SwitchThumbStyles& Disabled(const StateStyle& style);
};

// Rust's `Switch::new(id).checked(..).disabled(..).on_change(..)`. The checked
// value is the caller's; an activation reports the next one through
// `onChange`, which a
// `void On(T*, Ctx*, const ClickEvent*, int64_t next)` reads as a bool. A
// disabled switch keeps its element id but takes neither focus nor the click.
struct Switch {
    static El* New(Ctx* cx, Str id, bool checked = false, bool disabled = false,
                   Listener onChange = {}, const SwitchStyles* styles = nullptr,
                   const StateStyle* instance = nullptr,
                   Str accessibilityLabel = {}, int tabIndex = 0,
                   bool tabStop = true, FocusHandle focus = {});
};

// The track carries an id of its own — Rust builds it from `(id, "track")` —
// so it hovers as a part without shadowing the switch's own hit box.
struct SwitchTrack {
    static El* New(Ctx* cx, Str id, bool checked = false, bool disabled = false,
                   const SwitchTrackStyles* styles = nullptr,
                   const StateStyle* instance = nullptr);
};

struct SwitchThumb {
    static El* New(Ctx* cx, bool checked = false, bool disabled = false,
                   const SwitchThumbStyles* styles = nullptr,
                   const StateStyle* instance = nullptr);
};
} // namespace gpui
#endif // GPUI_BASE_SWITCH_H_
