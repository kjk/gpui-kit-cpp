#include "base/button.h"

namespace gpui {

// The two semantic states, in resolve_style's order.
static void ApplyStyles(El* e, const ButtonStyles* styles, bool selected,
                        bool disabled) {
    if (!styles) {
        return;
    }
    const StateStyle* active[2] = {selected ? &styles->selected : nullptr,
                                   disabled ? &styles->disabled : nullptr};
    ElRefine(e, StateStyleResolve(StateStyle{}, active, 2));
}

El* Button::New(Ctx* cx, Str id, bool disabled, Listener onClick,
                bool focusable, const ButtonStyles* styles, bool selected) {
    Arena* a = cx->a;
    // `div().id(self.id)`: a button's name only has to be unique among its
    // siblings, since GPUI scopes it by the stack of ids above it. That is why
    // upstream writes `Button::new("prev")` inside a pagination rather than
    // `Button::new(format!("{base}-prev"))` — the fold does that part.
    El* e = Div(a)
                // Centering is control geometry: an ordinary child of a
                // fixed-height unstyled Button sits in the middle on both
                // axes, independent of the caller's padding.
                ->Flex()
                ->ItemsCenter()
                ->JustifyCenter()
                // relative(1): the inherited text leading must not make the
                // button taller than the geometry its caller requested.
                ->LineHeight(1.f)
                ->PathClick(id)
                ->SuppressTextSelection()
                ->Role(AccessibilityRole::Button)
                ->AriaDisabled(disabled);
    ApplyStyles(e, styles, selected, disabled);
    if (disabled) {
        // A focused button that disables itself has to stay in the focus
        // list, or the next Tab starts at the first stop instead of the
        // neighbor. It is not a stop, and it does not take the click.
        if (focusable) {
            e->PathId(id)->TabStop(false);
        }
        return e;
    }
    if (focusable) {
        // `.track_focus(&handle.tab_index(..).tab_stop(..))`, still standing
        // in for a handle with the same fold the hit id comes from.
        e->PathId(id);
    }
    if (onClick.IsValid()) {
        e->OnClick(onClick);
    }
    return e;
}
} // namespace gpui
