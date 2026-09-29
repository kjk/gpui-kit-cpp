#include "base/toolbar.h"

namespace gpui {

static bool ToolbarContains(Bounds container, Bounds item) {
    float x = item.x + item.w * 0.5f;
    float y = item.y + item.h * 0.5f;
    return x >= container.x && x <= container.x + container.w &&
           y >= container.y && y <= container.y + container.h;
}

bool ToolbarMoveFocus(Window* win, Bounds container, bool forward) {
    if (!win || win->focusId == 0 || container.w <= 0 || container.h <= 0) {
        return false;
    }
    // The toolbar's items in tab order, and where the focus is among them.
    int n = win->focusEls.len;
    int cur = -1;
    int count = 0;
    for (int i = 0; i < n; i++) {
        const FocusRect& fr = win->focusEls[i];
        if (!fr.tabStop || !ToolbarContains(container, fr.bounds)) {
            continue;
        }
        if (fr.id == win->focusId) {
            cur = count;
        }
        count++;
    }
    if (cur < 0 || count < 2) {
        // Not inside, or the only item: focus stays put.
        return false;
    }
    int want = forward ? (cur + 1) % count : (cur - 1 + count) % count;
    int seen = 0;
    for (int i = 0; i < n; i++) {
        const FocusRect& fr = win->focusEls[i];
        if (!fr.tabStop || !ToolbarContains(container, fr.bounds)) {
            continue;
        }
        if (seen++ == want) {
            WindowSetFocusId(win, fr.id);
            return true;
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
    ToolbarMoveFocus(cx->win, self->bounds, forward);
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
    // The state survives across frames so the key handler reads the box the
    // last frame laid out, which is what the focus list was collected from.
    Entity<ToolbarState> state =
        ElementStateEntity<ToolbarState>(cx, id, StrL("gpui::Toolbar"));
    if (ToolbarState* s = state.Get(cx)) {
        s->disabled = disabled;
        root->BoundsOut(&s->bounds);
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
