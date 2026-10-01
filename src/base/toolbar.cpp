#include "base/toolbar.h"

namespace gpui {

bool ToolbarMoveFocus(Window* win, FocusHandle container, bool forward) {
    int start = WindowFocusedId(win);
    if (!start || !container.IsValid()) {
        return false;
    }
    // The first step and then up to MAX_FOCUS_ATTEMPTS more, as Rust takes.
    int at = start;
    for (int hop = 0; hop <= kToolbarMaxFocusAttempts; hop++) {
        at = FocusNextFrom(win, at, 0, !forward);
        if (at != start && WindowFocusContains(win, container.id, at)) {
            WindowSetFocusId(win, at);
            return true;
        }
        if (at == start) {
            break;
        }
    }
    return false;
}

void ToolbarState::OnKeyDown(ToolbarState* self, Ctx* cx, const KeyEvent* ev) {
    // Rust matches the key alone, whatever modifiers are held.
    if (self->disabled || !ev) {
        return;
    }
    bool forward = false;
    if (ev->vk == KeyLeft) {
        forward = false;
    } else if (ev->vk == KeyRight) {
        forward = true;
    } else {
        return;
    }
    ToolbarMoveFocus(cx->win, self->focus, forward);
    const_cast<KeyEvent*>(ev)->propagate = false;
    Notify(cx);
}

Toolbar* Toolbar::New(Ctx* cx, Str id) {
    Toolbar* t = ArenaNew<Toolbar>(cx->a);
    t->cx = cx;
    t->id = id;
    t->root = Div(cx->a)->Id(id);
    return t;
}

Toolbar* Toolbar::Disabled(bool value) {
    disabled = value;
    return this;
}

Toolbar* Toolbar::Child(El* child) {
    if (child) {
        root->Child(child);
    }
    return this;
}

El* Toolbar::IntoEl() {
    // The handle must survive across frames so containment checks and the
    // key handler refer to the same node; Rust keeps it in keyed state too.
    Entity<ToolbarState> state =
        ElementStateEntity<ToolbarState>(cx, id, StrL("gpui::Toolbar"));
    if (ToolbarState* s = state.Get(cx)) {
        if (!s->focus.IsValid()) {
            s->focus = FocusHandleNew(cx);
        }
        s->disabled = disabled;
        root->TrackFocus(s->focus)->TabStop(false);
    }
    return root->Role(AccessibilityRole::Toolbar)
        ->AriaOrientation(AccessibilityOrientation::Horizontal)
        ->OnKeyDown(ListenTo(state, &ToolbarState::OnKeyDown));
}

ToolbarGroup* ToolbarGroup::New(Ctx* cx, Str id) {
    ToolbarGroup* g = ArenaNew<ToolbarGroup>(cx->a);
    g->cx = cx;
    // One inline segment of the bar: its children flow along the row and
    // centre on the bar's cross axis. Spacing is the caller's, and a caller's
    // own style chained onto root afterwards wins.
    g->root = Div(cx->a)->Id(id)->FlexRow()->ItemsCenter();
    return g;
}

ToolbarGroup* ToolbarGroup::Label(Str value) {
    label = value;
    return this;
}

ToolbarGroup* ToolbarGroup::Child(El* child) {
    if (child) {
        root->Child(child);
    }
    return this;
}

El* ToolbarGroup::IntoEl() {
    root->Role(AccessibilityRole::Group);
    if (label.s) {
        root->AriaLabel(label);
    }
    return root;
}

} // namespace gpui
