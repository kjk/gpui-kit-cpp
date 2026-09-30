#include "base/popup_menu.h"
#include "base/actions.h"
#include "gpui/keymap.h"

namespace gpui {

Str PopupMenuContext() {
    return StrL("PopupMenu");
}

void PopupMenuInitKeys() {
    static uint32_t bound = 0;
    if (bound == KeymapGeneration()) {
        return;
    }
    bound = KeymapGeneration();
    const char* ctx = "PopupMenu";
    KeyBinding bindings[] = {
        {"enter", action::Confirm(), ctx},
        {"escape", action::Cancel(), ctx},
        {"up", action::SelectUp(), ctx},
        {"down", action::SelectDown(), ctx},
        {"left", action::SelectLeft(), ctx},
        {"right", action::SelectRight(), ctx},
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

PopupMenuAction PopupMenuActionOf(uint32_t id, Side side) {
    if (id == action::SelectUp()) {
        return PopupMenuAction::SelectPrev;
    }
    if (id == action::SelectDown()) {
        return PopupMenuAction::SelectNext;
    }
    if (id == action::Confirm()) {
        return PopupMenuAction::Confirm;
    }
    if (id == action::Cancel()) {
        return PopupMenuAction::Cancel;
    }
    if (id == action::SelectLeft()) {
        // A menu whose submenus open to the right is stepped out of with
        // Left; one that opens to the left reaches in with it.
        return SideIsLeft(side) ? PopupMenuAction::OpenSubmenu
                                : PopupMenuAction::CloseSubmenu;
    }
    if (id == action::SelectRight()) {
        return SideIsLeft(side) ? PopupMenuAction::CloseSubmenu
                                : PopupMenuAction::OpenSubmenu;
    }
    return PopupMenuAction::None;
}

int PopupMenuNextIndex(const bool* clickable, int n, int selected) {
    if (n <= 0) {
        return -1;
    }
    if (selected < 0) {
        // set_selected_index(0): Rust takes the first row whether or not it
        // is clickable.
        return 0;
    }
    for (int i = selected + 1; i < n; i++) {
        if (clickable[i]) {
            return i;
        }
    }
    return 0;
}

int PopupMenuPrevIndex(const bool* clickable, int n, int selected) {
    if (n <= 0) {
        return -1;
    }
    int ix = selected < 0 ? 0 : selected;
    for (int i = ix - 1; i >= 0; i--) {
        if (clickable[i]) {
            return i;
        }
    }
    // No clickable row before it: the last clickable one, or 0 if there is
    // none at all.
    for (int i = n - 1; i >= 0; i--) {
        if (clickable[i]) {
            return i;
        }
    }
    return 0;
}

void PopupMenuOpen(PopupMenuState* s, Ctx* cx) {
    if (!s->open) {
        // previous_focus_handle: where focus was, so dismissing can put it
        // back. A submenu opening does not park anything — the menu it came
        // out of already has focus, and it is what focus goes back to.
        s->previousFocus = WindowFocused(cx->win);
    }
    s->open = true;
    s->selected = -1;
    s->openSubmenu = -1;
    Notify(cx);
}

void PopupMenuDismiss(PopupMenuState* s, Ctx* cx) {
    // The menu takes focus while it is up, so it is the one giving it back.
    // Focus that has already moved somewhere else is left where it is.
    if (s->open && s->previousFocus.IsValid() &&
        FocusHandleContainsFocused(cx->win, s->focus)) {
        FocusHandleRestore(cx->win, s->previousFocus);
    }
    s->previousFocus = {};
    s->open = false;
    s->selected = -1;
    s->openSubmenu = -1;
    Notify(cx);
}

void PopupMenuDismissAll(PopupMenuState* s, Ctx* cx) {
    Entity<PopupMenuState> parent = s->parent;
    PopupMenuDismiss(s, cx);
    while (parent.IsValid()) {
        PopupMenuState* p = parent.Get(cx);
        if (!p) {
            break;
        }
        parent = p->parent;
        PopupMenuDismiss(p, cx);
    }
}

void PopupMenuConfirm(PopupMenuState* s, Ctx* cx, int ix) {
    // `handler(&ClickEvent::default(), window, cx)`: the row's own handler,
    // for a click and for Enter alike.
    if (ix >= 0 && ix < len(s->rows) && s->rows[ix].handler.IsValid()) {
        Listener handler = s->rows[ix].handler;
        ClickEvent ev = {};
        ListenerCall(cx->app, cx->win, handler, &ev);
    }
    if (s->onConfirm.IsValid()) {
        ClickEvent ev = {};
        ListenerCall(cx->app, cx->win, ListenerFill(s->onConfirm, ix), &ev);
    }
    // The item ran, so this menu and every parent go. Rust spells this
    // dismiss_all; without it, choosing a nested item leaves the top-level
    // dropdown behind.
    PopupMenuDismissAll(s, cx);
}

void PopupMenuPerform(PopupMenuState* s, Ctx* cx, PopupMenuAction act,
                      const bool* clickable, const bool* hasSubmenu, int n) {
    switch (act) {
        case PopupMenuAction::SelectPrev:
            s->selected = PopupMenuPrevIndex(clickable, n, s->selected);
            s->openSubmenu = -1;
            Notify(cx);
            break;
        case PopupMenuAction::SelectNext:
            s->selected = PopupMenuNextIndex(clickable, n, s->selected);
            s->openSubmenu = -1;
            Notify(cx);
            break;
        case PopupMenuAction::Confirm:
            if (s->selected >= 0 && s->selected < n && clickable[s->selected]) {
                if (hasSubmenu && hasSubmenu[s->selected]) {
                    // Enter on a submenu row opens it rather than running it.
                    s->openSubmenu = s->selected;
                    Notify(cx);
                } else {
                    PopupMenuConfirm(s, cx, s->selected);
                }
            }
            break;
        case PopupMenuAction::Cancel:
            // Escape closes the submenu first, and the menu itself only once
            // there is no submenu left to close.
            if (s->openSubmenu >= 0) {
                s->openSubmenu = -1;
                Notify(cx);
            } else {
                PopupMenuDismiss(s, cx);
            }
            break;
        case PopupMenuAction::OpenSubmenu:
            if (s->selected >= 0 && s->selected < n && hasSubmenu &&
                hasSubmenu[s->selected]) {
                s->openSubmenu = s->selected;
                Notify(cx);
            }
            break;
        case PopupMenuAction::CloseSubmenu:
            if (s->openSubmenu >= 0) {
                s->openSubmenu = -1;
                Notify(cx);
            }
            break;
        default:
            break;
    }
}

void PopupMenuBeginRows(PopupMenuState* s) {
    if (s) {
        VecClear(s->rows);
    }
}

void PopupMenuAddRow(PopupMenuState* s, const PopupMenuRow& row) {
    if (s) {
        VecAppend(s->rows, row);
    }
}

void PopupMenuPerformRows(PopupMenuState* s, Ctx* cx, PopupMenuAction act) {
    if (!s) {
        return;
    }
    int n = s->rows.len;
    // Escape, or the arrow that steps out, inside a submenu: the row that
    // opened it lives in the parent, so that is who closes it. Rust dismisses
    // the submenu entity, which its parent is watching; here the parent holds
    // the flag, and focus follows it back on the next frame.
    if ((act == PopupMenuAction::Cancel ||
         act == PopupMenuAction::CloseSubmenu) &&
        s->openSubmenu < 0 && s->parent.IsValid()) {
        PopupMenuState* parent = s->parent.Get(cx);
        if (parent) {
            parent->openSubmenu = -1;
            Notify(cx);
            return;
        }
    }
    // A link row is PopupMenuItem::ElementItem upstream, and its handler
    // opens the URL rather than reporting an index — so Enter on one has to
    // do what the click does.
    int sel = s->selected;
    if (act == PopupMenuAction::Confirm && sel >= 0 && sel < n &&
        s->rows[sel].link && s->rows[sel].href.s) {
        OpenUrl(s->rows[sel].href);
        PopupMenuDismissAll(s, cx);
        return;
    }
    Arena* ta = GetTempArena();
    bool* clickable = (bool*)Alloc(ta, n + 1);
    bool* hasSubmenu = (bool*)Alloc(ta, n + 1);
    for (int i = 0; i < n; i++) {
        clickable[i] = s->rows[i].clickable;
        hasSubmenu[i] = s->rows[i].submenu;
    }
    PopupMenuPerform(s, cx, act, clickable, hasSubmenu, n);
}

void PopupMenuState::OnAction(PopupMenuState* self, Ctx* cx,
                              const ActionEvent* ev) {
    if (!self || !self->open) {
        // Not on screen: the action belongs to whatever is, which is
        // cx.propagate().
        const_cast<ActionEvent*>(ev)->propagate = true;
        return;
    }
    PopupMenuAction act = PopupMenuActionOf(ev->action, self->side);
    bool root = !self->parent.IsValid();
    bool unhandledOpen =
        act == PopupMenuAction::OpenSubmenu &&
        (self->selected < 0 || self->selected >= self->rows.len ||
         !self->rows[self->selected].submenu);
    bool unhandledClose =
        act == PopupMenuAction::CloseSubmenu && self->openSubmenu < 0;
    if (root && (unhandledOpen || unhandledClose)) {
        // AppMenuBar binds the same horizontal actions on the parent. A root
        // popup only consumes the arrow when it actually changes a submenu.
        const_cast<ActionEvent*>(ev)->propagate = true;
        return;
    }
    PopupMenuPerformRows(self, cx, act);
}

void PopupMenuState::OnItemClick(PopupMenuState* self, Ctx* cx,
                                 const ClickEvent*, intptr_t ix) {
    self->selected = (int)ix;
    PopupMenuConfirm(self, cx, (int)ix);
}

void PopupMenuState::OnTriggerClick(PopupMenuState* self, Ctx* cx,
                                    const ClickEvent*, intptr_t wasOpen) {
    // `state.set_open(open); state.toggle_open()`: the toggle is computed
    // from the menu as it was *drawn*, not from what the state has become
    // since the release began. The trigger sits outside the menu, so the
    // outside dismissal has already closed it by the time this click lands;
    // without the rewind the toggle would only ever reopen what that
    // dismissal had just closed. Only the flag is rewound — set_open is a
    // plain setter in Rust too, so the focus a dismissal put back stays put.
    self->open = wasOpen != 0;
    if (self->open) {
        PopupMenuDismiss(self, cx);
    } else {
        self->x = 0;
        self->y = 0;
        PopupMenuOpen(self, cx);
    }
}

// PopupMenu::handle_dismiss, with the two cases Rust refuses to close on: a
// press inside the menu this one came out of, and a press while a submenu of
// this one is up -- that one is the submenu's to answer, and closing first
// would take the row's own click with it.
void PopupMenuState::OnPressOutside(PopupMenuState* self, Ctx* cx,
                                    const MouseUpEvent* ev) {
    if (!self->open || self->openSubmenu >= 0) {
        return;
    }
    if (PopupMenuState* p = self->parent.Get(cx)) {
        if (p->open && p->bounds.Contains({ev->x, ev->y})) {
            return;
        }
    }
    PopupMenuDismissAll(self, cx);
}

void PopupMenuState::OnContextDown(PopupMenuState* self, Ctx* cx,
                                   const MouseDownEvent* ev) {
    // on_mouse_down(MouseButton::Right): the menu opens where the pointer is,
    // inside the element that owns it.
    if (ev->button != MouseButton::Right) {
        return;
    }
    self->x = ev->x - ev->el.x;
    self->y = ev->y - ev->el.y;
    PopupMenuOpen(self, cx);
}

void PopupMenuState::OnItemHover(PopupMenuState* self, Ctx* cx,
                                 const HoverEvent* ev, intptr_t ix) {
    // The hovered row is the selected one; leaving it deselects, unless it is
    // a submenu row, which stays selected while the pointer travels into the
    // submenu it opened.
    // A key press ends hover under a still pointer; keep the highlight then,
    // so the next arrow key moves on from this item (popup_menu.rs).
    if (ev->hovered) {
        self->selected = (int)ix;
        self->openSubmenu = -1;
    } else if (self->selected == (int)ix && self->openSubmenu != (int)ix &&
               !WindowLastInputWasKeyboard(cx->win)) {
        self->selected = -1;
    }
    Notify(cx);
}

void PopupMenuState::OnSubmenuClick(PopupMenuState* self, Ctx* cx,
                                    const ClickEvent*, intptr_t ix) {
    self->selected = (int)ix;
    self->openSubmenu = (int)ix;
    Notify(cx);
}

void PopupMenuState::OnSubmenuHover(PopupMenuState* self, Ctx* cx,
                                    const HoverEvent* ev, intptr_t ix) {
    // Rust renders a submenu as soon as its row becomes selected. Keep it
    // selected while the pointer crosses from the row into the child menu.
    if (ev->hovered) {
        self->selected = (int)ix;
        self->openSubmenu = (int)ix;
        Notify(cx);
    }
}

void PopupMenuState::OnScroll(PopupMenuState* self, Ctx* cx,
                              const ScrollEvent* ev) {
    self->scrollY = ev->offsetY;
    Notify(cx);
}

} // namespace gpui
