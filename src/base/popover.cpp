#include "base/popover.h"
#include "base/actions.h"
#include "base/global_state.h"
#include "gpui/keymap.h"

namespace gpui {

void PopoverInitKeys() {
    CancelInitKeys("Popover");
    static uint32_t generation = UINT32_MAX;
    uint32_t current = KeymapGeneration();
    if (generation == current) {
        return;
    }
    generation = current;
    KeyBinding bindings[] = {
        {"enter", action::Confirm(), "Popover"},
        {"space", action::Confirm(), "Popover"},
    };
    KeymapBind(bindings, dimof(bindings));
}

static void PopoverReportOpenChange(PopoverState* s, Ctx* cx) {
    if (!s || !s->onOpenChange.IsValid()) {
        return;
    }
    PopoverOpenChangeEvent ev = {s->open};
    ListenerCall(cx->app, cx->win, s->onOpenChange, &ev);
}

static void PopoverReportDismiss(PopoverState* s, Ctx* cx, float x, float y,
                                 MouseButton button, int clickCount,
                                 Modifiers modifiers) {
    if (!s || !s->onDismiss.IsValid()) {
        return;
    }
    ClickEvent ev = {};
    ev.x = x;
    ev.y = y;
    ev.button = button;
    ev.clickCount = clickCount;
    ev.modifiers = modifiers;
    ListenerCall(cx->app, cx->win, s->onDismiss, &ev);
}

bool PopoverIsOpen(Ctx* cx, Entity<PopoverState> state) {
    PopoverState* s = state.Get(cx);
    return s && s->open;
}

void PopoverSetOpen(Ctx* cx, Entity<PopoverState> state, bool open) {
    PopoverState* s = state.Get(cx);
    if (s) {
        s->self = state.id;
        s->open = open;
        BaseDeferredPopoverSet(cx->app, state.id, open);
    }
}

// toggle_open's focus half, which is the same for every widget that opens
// something over the page. On the way in the previously focused element is
// parked and the popover — or whatever it tracks — takes focus; on the way
// out focus goes back, but only if the popover still has it, since a click
// somewhere else has already moved it on purpose.
void PopoverSetOpenFocused(PopoverState* s, Ctx* cx, bool open) {
    if (!s || s->open == open) {
        return;
    }
    if (open) {
        s->previousFocus = WindowFocused(cx->win);
        FocusHandle take =
            s->trackedFocus.IsValid() ? s->trackedFocus : s->focus;
        FocusHandleFocus(cx->win, take);
    } else {
        if (s->previousFocus.IsValid() &&
            (FocusHandleContainsFocused(cx->win, s->focus) ||
             FocusHandleContainsFocused(cx->win, s->trackedFocus))) {
            FocusHandleRestore(cx->win, s->previousFocus);
        }
        s->previousFocus = {};
    }
    s->open = open;
    BaseDeferredPopoverSet(cx->app, s->self.IsValid() ? s->self : cx->self,
                           open);
    PopoverReportOpenChange(s, cx);
}

// toggle_open, off the trigger's press. Rust stops propagation here so the
// press does not also reach whatever the popover sits in; the hit test only
// reports the innermost rect, so that is already true.
void PopoverToggle(PopoverState* self, Ctx* cx, const MouseDownEvent* ev,
                   intptr_t button) {
    if (ev->button != (MouseButton)button) {
        return;
    }
    PopoverSetOpenFocused(self, cx, !self->open);
    Notify(cx);
}

void PopoverConfirm(PopoverState* self, Ctx* cx, const ActionEvent* ev) {
    if (!self || ev->action != action::Confirm()) {
        const_cast<ActionEvent*>(ev)->propagate = true;
        return;
    }
    PopoverSetOpenFocused(self, cx, !self->open);
    Notify(cx);
}

void PopoverDismiss(PopoverState* self, Ctx* cx, const ClickEvent* ev) {
    if (!self->open) {
        return;
    }
    PopoverSetOpenFocused(self, cx, false);
    PopoverReportDismiss(self, cx, ev->x, ev->y, ev->button, ev->clickCount,
                         ev->modifiers);
    Notify(cx);
}

void PopoverDismissOnMouseDown(PopoverState* self, Ctx* cx,
                               const MouseDownEvent* ev) {
    if (!self->open) {
        return;
    }
    PopoverSetOpenFocused(self, cx, false);
    PopoverReportDismiss(self, cx, ev->x, ev->y, ev->button, ev->clickCount,
                         ev->modifiers);
    Notify(cx);
}

Popover* Popover::New(Ctx* cx, Str id, Entity<PopoverState> state,
                      MouseButton button) {
    Arena* a = cx->a;
    Popover* p = ArenaNew<Popover>(a);
    p->a = a;
    p->cx = cx;
    p->id = id;
    p->state = state;
    p->button = button;
    // The popover's own focus handle. Rust hangs it off the content, not off
    // the trigger's container, so opening moves focus into what came up and
    // Tab from the trigger still walks the page.
    // `PopoverState::new` asks the app for a handle once and keeps it; the
    // state outlives the frame, so the handle it holds is what the content
    // picks up again each time. Nothing about it comes from `id` any more —
    // the old `HashClickId(id) * 31 + 1` existed only to stay clear of the
    // click id that same name produced.
    if (PopoverState* st = state.Get(cx)) {
        st->self = state.id;
        // The keyed state outlives a frame; these are frame-supplied builder
        // values, so omission in a later frame clears rather than retaining
        // a callback or tracked handle which is no longer rendered.
        st->trackedFocus = {};
        st->onOpenChange = {};
        st->onDismiss = {};
        if (st->open) {
            BaseDeferredPopoverSet(cx->app, state.id, true);
        }
        if (!st->focus.IsValid()) {
            st->focus = FocusHandleNew(cx);
        }
        p->focus = st->focus;
    }
    return p;
}

Popover* Popover::TrackedFocus(FocusHandle tracked) {
    if (PopoverState* st = state.Get(cx)) {
        st->trackedFocus = tracked;
    }
    return this;
}

Popover* Popover::OverlayClosable(bool closable) {
    overlayClosable = closable;
    return this;
}

Popover* Popover::OnOpenChange(Listener fn) {
    if (PopoverState* st = state.Get(cx)) {
        st->onOpenChange = fn;
    }
    return this;
}

Popover* Popover::OnDismiss(Listener fn) {
    if (PopoverState* st = state.Get(cx)) {
        st->onDismiss = fn;
    }
    return this;
}

Popover* Popover::Trigger(El* e) {
    if (!e) {
        return this;
    }
    // Rust re-renders a Selectable trigger with `.open(is_open)` so the
    // control lights while the popover is up without touching selected. The
    // trigger here is already an El, so the caller applies Button::Open
    // (or Selected, for a control that has not split the two) when building
    // it.
    if (state.IsValid()) {
        // A press, not a click: Rust hangs the toggle off on_mouse_down so the
        // popover is up before the button comes back. The handler reads the
        // event's own button, since one element hears every press it is over.
        e->OnMouseDown(ListenTo(state, &PopoverToggle, (intptr_t)button));
    }
    trigger = e;
    return this;
}

Popover* Popover::Anchor(PopupAnchor v) {
    anchor = v;
    return this;
}

Popover* Popover::Content(El* e) {
    if (e) {
        // What `Popup` does with it, because Rust's Popover *is* a Popup:
        // `Popup::new(id, trigger).content(..)`. The exact requested corner
        // is deferred, so it draws over later siblings and is not clipped by
        // an ancestor's overflow. The styled UI layer supplies its own
        // top_1/bottom_1 visual offset.
        e->Role(AccessibilityRole::Dialog);
        // track_focus, not focus_ring_style: the surface takes focus and
        // does not draw a ring around itself for it.
        e->TrackFocus(focus)->FocusRing(false);
        if (overlayClosable && state.IsValid()) {
            e->OnMouseDownOut(ListenTo(state, &PopoverDismissOnMouseDown));
        }
        content = e;
    }
    return this;
}

El* Popover::IntoEl() {
    if (!trigger) {
        return Div(a)->Id(StrL("empty"));
    }
    // popover.rs builds Popup::new(id, trigger).anchor(anchor), rather than
    // duplicating its capture and positioning lifecycle.
    El* root = Popup::New(cx, id, trigger, anchor)->Content(content)->IntoEl();
    if (state.IsValid()) {
        PopoverInitKeys();
        root->KeyContext(StrL("Popover"))
            ->OnAction(action::Confirm(), ListenTo(state, &PopoverConfirm));
    }
    return root;
}
} // namespace gpui
