#include "base/checkbox.h"
#include "base/button.h"

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

// The instance style refined by the state's style (checked or
// indeterminate), then by the disabled one. A null state style adds nothing.
static StateStyle ResolveCheckboxStyle(CheckboxState state, bool disabled,
                                       const StateStyle* instance,
                                       const StateStyle* checked,
                                       const StateStyle* indeterminate,
                                       const StateStyle* disabledStyle) {
    StateStyle base = instance ? *instance : StateStyle{};
    const StateStyle* states[2] = {
        state == CheckboxState::Checked ? checked : nullptr,
        state == CheckboxState::Indeterminate ? indeterminate : nullptr,
    };
    StateStyle resolved = StateStyleResolve(base, states, 2);
    if (disabled && disabledStyle) {
        const StateStyle* disabledState[] = {disabledStyle};
        resolved = StateStyleResolve(resolved, disabledState, 1);
    }
    return resolved;
}

// The state styles a checkbox or its indicator was given, resolved for
// `state` and `disabled` and refined onto `e`; nothing when it has none.
template <typename Styles>
static void RefineCheckbox(El* e, CheckboxState state, bool disabled,
                           const Styles* styles, const StateStyle* instance) {
    if (styles || instance) {
        ElRefine(e,
                 ResolveCheckboxStyle(state, disabled, instance,
                                      styles ? &styles->checked : nullptr,
                                      styles ? &styles->indeterminate : nullptr,
                                      styles ? &styles->disabled : nullptr));
    }
}

El* Checkbox::New(Ctx* cx, Str id, CheckboxState state, bool disabled,
                  Listener onChange, const CheckboxStyles* styles,
                  const StateStyle* instance, Str accessibilityLabel,
                  int tabIndex, bool tabStop, FocusHandle focus,
                  AccessibilityRole role) {
    Arena* a = cx->a;
    // The path id is unconditional. A focused handle can still anchor Tab
    // traversal when activation just disabled the control.
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
    RefineCheckbox(e, state, disabled, styles, instance);
    ControlFocus(e, focus, id, tabIndex, tabStop, disabled, cx->win);
    if (disabled) {
        return e;
    }
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
    RefineCheckbox(e, state, disabled, styles, instance);
    return e;
}
} // namespace gpui
