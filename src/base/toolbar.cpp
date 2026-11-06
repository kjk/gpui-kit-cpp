#include "base/toolbar.h"

namespace gpui {

bool ToolbarMoveFocus(Window* win, FocusHandle container, bool forward) {
    int start = WindowFocusedId(win);
    if (!start || !container.IsValid()) {
        return false;
    }
    // Walk the tab order with no hop limit. It ends at the first repeated
    // stop, which is where a cycle that never returns to `start` stops, and
    // focus stays where it was. A stop outside the toolbar is passed over,
    // however many the rest of the window holds.
    Vec<int> visited;
    VecAppend(visited, start);
    int at = start;
    bool moved = false;
    for (;;) {
        int next = FocusNextFrom(win, at, 0, !forward);
        if (!next) {
            break;
        }
        bool seen = false;
        for (int i = 0; i < len(visited); i++) {
            if (visited[i] == next) {
                seen = true;
                break;
            }
        }
        if (seen) {
            break;
        }
        if (WindowFocusContains(win, container.id, next)) {
            WindowSetFocusId(win, next);
            moved = true;
            break;
        }
        VecAppend(visited, next);
        at = next;
    }
    VecReset(visited);
    return moved;
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

void ToolbarState::OnMouseDown(ToolbarState*, Ctx* cx, const MouseDownEvent*) {
    WindowPreventDefault(cx);
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
        ->OnKeyDown(ListenTo(state, &ToolbarState::OnKeyDown))
        ->OnMouseDown(ListenTo(state, &ToolbarState::OnMouseDown));
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
