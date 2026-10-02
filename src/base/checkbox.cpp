#include "base/checkbox.h"

namespace gpui {

CheckboxState CheckboxActivated(CheckboxState state) {
    return state == CheckboxState::Checked ? CheckboxState::Unchecked
                                           : CheckboxState::Checked;
}

CheckboxStyles& CheckboxStyles::Checked(const StateStyle& style) {
    StateStyleRefine(&checked, style);
    return *this;
}

CheckboxStyles& CheckboxStyles::Indeterminate(const StateStyle& style) {
    StateStyleRefine(&indeterminate, style);
    return *this;
}

CheckboxStyles& CheckboxStyles::Disabled(const StateStyle& style) {
    StateStyleRefine(&disabled, style);
    return *this;
}

CheckboxIndicatorStyles& CheckboxIndicatorStyles::Checked(
    const StateStyle& style) {
    StateStyleRefine(&checked, style);
    return *this;
}

CheckboxIndicatorStyles& CheckboxIndicatorStyles::Indeterminate(
    const StateStyle& style) {
    StateStyleRefine(&indeterminate, style);
    return *this;
}

CheckboxIndicatorStyles& CheckboxIndicatorStyles::Disabled(
    const StateStyle& style) {
    StateStyleRefine(&disabled, style);
    return *this;
}

El* Checkbox::New(Ctx* cx, Str id, CheckboxState state, bool disabled,
                  Listener onChange, const CheckboxStyles* styles,
                  const StateStyle* instance, Str accessibilityLabel,
                  int tabIndex, bool tabStop, FocusHandle focus,
                  AccessibilityRole role) {
    Arena* a = cx->a;
    // `div().id(id)` is unconditional in Rust; `track_focus` and `on_click`
    // both hang off `when(!disabled)`. The id is the fold of the name down
    // from the root, so a checkbox named among its siblings is still its own.
    AccessibilityToggled toggled =
        state == CheckboxState::Indeterminate ? AccessibilityToggled::Mixed
        : state == CheckboxState::Checked     ? AccessibilityToggled::True
                                              : AccessibilityToggled::False;
    El* e = Div(a)
                ->PathClick(id)
                ->Role(role)
                ->AriaToggled(toggled)
                ->AriaDisabled(disabled);
    if (accessibilityLabel.s) {
        e->AriaLabel(accessibilityLabel);
    }
    if (styles || instance) {
        StateStyle base = instance ? *instance : StateStyle{};
        const StateStyle* states[2] = {
            state == CheckboxState::Checked && styles ? &styles->checked
                                                      : nullptr,
            state == CheckboxState::Indeterminate && styles
                ? &styles->indeterminate
                : nullptr,
        };
        StateStyle resolved = StateStyleResolve(base, states, 2);
        if (disabled && styles) {
            const StateStyle* disabledState[] = {&styles->disabled};
            resolved = StateStyleResolve(resolved, disabledState, 1);
        }
        ElRefine(e, resolved);
    }
    if (disabled) {
        return e;
    }
    if (focus.IsValid()) {
        e->TrackFocus(focus);
    } else {
        e->PathId(id);
    }
    e->TabIndex(tabIndex)->TabStop(tabStop);
    if (onChange.IsValid()) {
        e->OnClick(ListenerFill(onChange, (int64_t)CheckboxActivated(state)));
    }
    return e;
}

El* CheckboxIndicator::New(Ctx* cx, CheckboxState state, bool disabled,
                           const CheckboxIndicatorStyles* styles,
                           const StateStyle* instance) {
    Arena* a = cx->a;
    El* e = Div(a);
    if (styles || instance) {
        StateStyle base = instance ? *instance : StateStyle{};
        const StateStyle* states[2] = {
            state == CheckboxState::Checked && styles ? &styles->checked
                                                      : nullptr,
            state == CheckboxState::Indeterminate && styles
                ? &styles->indeterminate
                : nullptr,
        };
        StateStyle resolved = StateStyleResolve(base, states, 2);
        if (disabled && styles) {
            const StateStyle* disabledState[] = {&styles->disabled};
            resolved = StateStyleResolve(resolved, disabledState, 1);
        }
        ElRefine(e, resolved);
    }
    return e;
}
} // namespace gpui
