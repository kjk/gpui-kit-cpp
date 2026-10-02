#ifndef GPUI_BASE_CHECKBOX_H_
#define GPUI_BASE_CHECKBOX_H_
/* Unstyled checkbox — crates/base/src/checkbox.rs */

#include "base/state_style.h"

namespace gpui {

// The semantic value an unstyled checkbox exposes. Rust's CheckboxState.
enum class CheckboxState : uint8_t {
    Unchecked,
    Checked,
    Indeterminate
};

// What an activation produces: an indeterminate box becomes checked, a
// checked one clears. Rust's CheckboxState::activated.
CheckboxState CheckboxActivated(CheckboxState state);

// Semantic state refinements are layered after the instance style, with
// disabled last. These are the POD-friendly projections of Rust's
// CheckboxStyles and CheckboxIndicatorStyles builders.
struct CheckboxStyles {
    StateStyle checked = {};
    StateStyle indeterminate = {};
    StateStyle disabled = {};

    CheckboxStyles& Checked(const StateStyle& style);
    CheckboxStyles& Indeterminate(const StateStyle& style);
    CheckboxStyles& Disabled(const StateStyle& style);
};

struct CheckboxIndicatorStyles {
    StateStyle checked = {};
    StateStyle indeterminate = {};
    StateStyle disabled = {};

    CheckboxIndicatorStyles& Checked(const StateStyle& style);
    CheckboxIndicatorStyles& Indeterminate(const StateStyle& style);
    CheckboxIndicatorStyles& Disabled(const StateStyle& style);
};

// Rust's `Checkbox::new(id).state(..).disabled(..).on_change(..)`. The box
// owns identity, focus and activation; the caller owns every pixel of it.
// `onChange` is handed the state the activation produces, the way Rust hands
// its handler `next_state` — read it with a
// `void On(T*, Ctx*, const ClickEvent*, int64_t next)` and compare against
// CheckboxState. A disabled box keeps its id, so it still hit-tests and
// hovers, but takes neither focus nor the click.
struct Checkbox {
    static El* New(Ctx* cx, Str id,
                   CheckboxState state = CheckboxState::Unchecked,
                   bool disabled = false, Listener onChange = {},
                   const CheckboxStyles* styles = nullptr,
                   const StateStyle* instance = nullptr,
                   Str accessibilityLabel = {}, int tabIndex = 0,
                   bool tabStop = true, FocusHandle focus = {},
                   AccessibilityRole role = AccessibilityRole::CheckBox);
};

struct CheckboxIndicator {
    static El* New(Ctx* cx, CheckboxState state = CheckboxState::Unchecked,
                   bool disabled = false,
                   const CheckboxIndicatorStyles* styles = nullptr,
                   const StateStyle* instance = nullptr);
};
} // namespace gpui
#endif // GPUI_BASE_CHECKBOX_H_
