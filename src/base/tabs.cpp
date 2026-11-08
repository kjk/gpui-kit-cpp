#include "base/tabs.h"
#include "base/button.h"

namespace gpui {

TabStyles& TabStyles::Selected(const StateStyle& style) {
    StateStyleRefine(&selected, style);
    return *this;
}

TabStyles& TabStyles::Disabled(const StateStyle& style) {
    StateStyleRefine(&disabled, style);
    return *this;
}

El* Tabs::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    return Div(a)->Id(id)->Role(AccessibilityRole::TabList);
}

El* Tab::New(Ctx* cx, Str id, bool disabled, Listener onClick, bool selected,
             Str accessibilityLabel, int positionInSet, int sizeOfSet,
             const TabStyles* styles, const StateStyle* instance) {
    Arena* a = cx->a;
    // `div().id(ix)`: a tab is named by its place in the strip, which the bar
    // above it scopes.
    // Button's neutral control geometry: a fixed-size tab centers ordinary
    // content while its caller owns the size, spacing and visual treatment.
    El* e = ControlBox(a, id)
                ->Role(AccessibilityRole::Tab)
                ->AriaSelected(selected)
                ->AriaDisabled(disabled)
                // Rust installs this on every tab. The tab owns its press even
                // though it deliberately does not own keyboard focus yet.
                ->StopMouseDown();
    if (styles || instance) {
        StateStyle base = instance ? *instance : StateStyle{};
        const StateStyle* states[2] = {
            selected && styles ? &styles->selected : nullptr,
            disabled && styles ? &styles->disabled : nullptr,
        };
        ElRefine(e, StateStyleResolve(base, states, 2));
    }
    if (accessibilityLabel.s) {
        e->AriaLabel(accessibilityLabel);
    }
    if (positionInSet > 0) {
        e->AriaPositionInSet(positionInSet);
    }
    if (sizeOfSet > 0) {
        e->AriaSizeOfSet(sizeOfSet);
    }
    if (!disabled && onClick.IsValid()) {
        e->OnClick(onClick);
    }
    return e;
}
} // namespace gpui
