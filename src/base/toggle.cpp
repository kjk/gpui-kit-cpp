#include "base/toggle.h"
#include "base/button.h"

namespace gpui {

ToggleStyles& ToggleStyles::Pressed(const StateStyle& style) {
    StateStyleRefine(&pressed, style);
    return *this;
}

ToggleStyles& ToggleStyles::Disabled(const StateStyle& style) {
    StateStyleRefine(&disabled, style);
    return *this;
}

El* Toggle::New(Ctx* cx, Str id, bool pressed, bool disabled, Listener onChange,
                const ToggleStyles* styles, const StateStyle* instance) {
    Arena* a = cx->a;
    // Button's neutral control geometry: a fixed-size toggle centers
    // ordinary content without themed padding.
    El* e = ControlBox(a, id)
                ->Role(AccessibilityRole::Button)
                ->AriaToggled(pressed ? AccessibilityToggled::True
                                      : AccessibilityToggled::False)
                ->AriaDisabled(disabled);
    if (styles || instance) {
        StateStyle base = instance ? *instance : StateStyle{};
        const StateStyle* states[2] = {
            pressed && styles ? &styles->pressed : nullptr,
            disabled && styles ? &styles->disabled : nullptr,
        };
        ElRefine(e, StateStyleResolve(base, states, 2));
    }
    if (disabled) {
        return e->StopMouseDown();
    }
    e->PathId(id);
    if (onChange.IsValid()) {
        e->OnClick(ListenerFill(onChange, !pressed));
    }
    return e;
}
} // namespace gpui
