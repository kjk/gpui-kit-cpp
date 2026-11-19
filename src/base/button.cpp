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

El* ControlBox(Arena* a, Str id) {
    return Div(a)
        ->Flex()
        ->ItemsCenter()
        ->JustifyCenter()
        ->LineHeight(1.f)
        ->PathClick(id);
}

El* ControlFocus(El* e, FocusHandle focus, Str id, int tabIndex, bool tabStop,
                 bool disabled, Window* window) {
    if (focus.IsValid()) {
        if (disabled && !FocusHandleIsFocused(window, focus)) {
            return e;
        }
        e->TrackFocus(focus);
    } else {
        // A path handle resolves during layout, after the primitive builds.
        // Keep the slot so a focused control can still find its neighbors.
        e->PathId(id);
    }
    return e->TabIndex(tabIndex)->TabStop(tabStop && !disabled);
}

El* Button::New(Ctx* cx, Str id, bool disabled, Listener onClick,
                bool focusable, const ButtonStyles* styles, bool selected) {
    Arena* a = cx->a;
    // `div().id(self.id)`: a button's name only has to be unique among its
    // siblings, since GPUI scopes it by the stack of ids above it. That is why
    // upstream writes `Button::new("prev")` inside a pagination rather than
    // `Button::new(format!("{base}-prev"))` — the fold does that part.
    El* e = ControlBox(a, id)
                ->SuppressTextSelection()
                ->Role(AccessibilityRole::Button)
                ->AriaDisabled(disabled);
    e->buttonDisabled = disabled;
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

bool Button::IsDisabled(const El* button) {
    return button && button->buttonDisabled;
}

} // namespace gpui
