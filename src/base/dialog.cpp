#include "base/dialog.h"
#include "base/element_ext.h"
#include "base/focus_trap.h"
#include "base/actions.h"
#include "gpui/keymap.h"

namespace gpui {

DialogHandle DialogHandle::New(Ctx* cx, bool open) {
    DialogHandle handle;
    handle.state = EntityNewState<DialogHandleState>(cx->app);
    if (DialogHandleState* state = handle.state.Get(cx)) {
        state->self = handle.state.id;
        state->open = open;
    }
    return handle;
}

bool DialogHandle::IsOpen(App* app, bool fallback) const {
    DialogHandleState* value = state.Get(app);
    return value ? value->open : fallback;
}

void DialogHandle::OnOpenChange(App* app, Listener listener) const {
    if (DialogHandleState* value = state.Get(app)) {
        value->onOpenChange = listener;
    }
}

bool DialogHandle::SetOpen(Ctx* cx, bool open,
                           DialogChangeReason reason) const {
    DialogHandleState* value = state.Get(cx);
    if (!value || value->open == open) {
        return false;
    }
    value->open = open;
    if (value->onOpenChange.IsValid()) {
        DialogOpenChangeEvent event = {open, reason};
        ListenerCall(cx->app, cx->win, value->onOpenChange, &event);
    }
    NotifyEntity(cx->app, value->self, cx->win);
    return true;
}

bool DialogHandle::Open(Ctx* cx) const {
    return SetOpen(cx, true, DialogChangeReason::Imperative);
}

bool DialogHandle::Close(Ctx* cx) const {
    return SetOpen(cx, false, DialogChangeReason::Imperative);
}

Str DialogContext() {
    return StrL("Dialog");
}

void DialogInitKeys() {
    static uint32_t bound = 0;
    if (bound == KeymapGeneration()) {
        return;
    }
    bound = KeymapGeneration();
    const char* ctx = "Dialog";
    KeyBinding bindings[] = {
        {"escape", action::Cancel(), ctx},
        {"enter", action::Confirm(), ctx},
    };
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
}

DialogAction DialogActionOf(uint32_t id) {
    if (id == action::Cancel()) {
        return DialogAction::Cancel;
    }
    if (id == action::Confirm()) {
        return DialogAction::Confirm;
    }
    return DialogAction::None;
}

void DialogKeys::OnAction(DialogKeys* self, Ctx* cx, const ActionEvent* ev) {
    if (!self) {
        return;
    }
    Listener l = {};
    switch (DialogActionOf(ev->action)) {
        case DialogAction::Cancel:
            l = self->onCancel.IsValid() ? self->onCancel : self->onClose;
            break;
        case DialogAction::Confirm:
            l = self->onOk;
            break;
        default:
            break;
    }
    if (!l.IsValid()) {
        // Nothing to run. Rust's handler still consumes the keystroke — the
        // dialog is modal — so this does too, rather than propagating into
        // whatever is behind the backdrop.
        return;
    }
    // `let event = ClickEvent::default(); confirm(&event, window, cx)`: the
    // keyboard runs the same handler the button does.
    ClickEvent click = {};
    ListenerCall(cx->app, cx->win, l, &click);
}

void DialogBindKeys(Ctx* cx, El* popup, Str name, Listener onCancel,
                    Listener onOk, Listener onClose) {
    if (!cx || !popup) {
        return;
    }
    DialogInitKeys();
    Entity<DialogKeys> keys =
        ElementStateEntity<DialogKeys>(cx, name, StrL("gpui::DialogKeys"));
    if (DialogKeys* k = keys.Get(cx)) {
        k->onCancel = onCancel;
        k->onOk = onOk;
        k->onClose = onClose;
    }
    // track_focus(&self.focus): the host is focusable so that a dialog with
    // nothing focusable inside it still has somewhere for focus to be — and
    // not a tab stop, so Tab still visits the controls rather than the box
    // around them.
    Listener onAction = ListenTo(keys, &DialogKeys::OnAction);
    popup->KeyContext(DialogContext())
        ->FocusId(HashClickId(name))
        ->TabStop(false)
        ->OnAction(action::Cancel(), onAction)
        ->OnAction(action::Confirm(), onAction);
}

bool DialogBackdropCloses(bool overlayClosable, bool topmost,
                          MouseButton button, float pressY,
                          float dismissBelowY) {
    // Above the reserved band the press is not the backdrop's — that is where
    // a title bar a dialog was opened over still is.
    if (pressY < dismissBelowY) {
        return false;
    }
    // Rust computes `overlay_closable && topmost` once, so a dialog under
    // another one never answers a backdrop press.
    return button == MouseButton::Left && overlayClosable && topmost;
}

struct DialogTriggerState {
    DialogHandle handle = {};
    Listener onOpen = {};

    static void OnMouseDown(DialogTriggerState* self, Ctx* cx,
                            const MouseDownEvent* ev) {
        if (!ev || ev->button != MouseButton::Left) {
            return;
        }
        if (self->handle.IsValid()) {
            self->handle.SetOpen(cx, true, DialogChangeReason::TriggerPress);
        }
        if (self->onOpen.IsValid()) {
            ListenerCall(cx->app, cx->win, self->onOpen, ev);
        }
        WindowStopPropagation(cx);
    }
};

El* DialogTrigger::New(Ctx* cx, Listener onOpen, DialogHandle handle, Str id) {
    Arena* a = cx->a;
    El* e = Div(a);
    Entity<DialogTriggerState> trigger = ElementStateEntity<DialogTriggerState>(
        cx, id, StrL("gpui::DialogTriggerState"));
    if (DialogTriggerState* state = trigger.Get(cx)) {
        state->handle = handle;
        state->onOpen = onOpen;
    }
    e->OnMouseDown(ListenTo(trigger, &DialogTriggerState::OnMouseDown));
    return e;
}

El* DialogBackdrop::New(Ctx* cx) {
    Arena* a = cx->a;
    return UiRoot(a, StrL("dialog-backdrop"), 0);
}
El* DialogPopup::New(Ctx* cx) {
    Arena* a = cx->a;
    return UiRoot(a, StrL("dialog-popup"), 0);
}
El* DialogTitle::New(Ctx* cx) {
    Arena* a = cx->a;
    return UiRoot(a, StrL("dialog-title"), 0);
}
El* DialogDescription::New(Ctx* cx) {
    Arena* a = cx->a;
    return UiRoot(a, StrL("dialog-description"), 0);
}
El* DialogClose::New(Ctx* cx, int clickId) {
    Arena* a = cx->a;
    // footer.rs wraps this part specifically to raise Cancel. A numeric id
    // remains a compatibility identity, but dispatch is semantic just like
    // AlertDialogCancel and the Escape binding.
    return UiRoot(a, StrL("dialog-close"), clickId)
        ->OnClickAction(action::Cancel());
}
El* DialogCloseActivation(El* button) {
    if (!button) {
        return nullptr;
    }
    return button->AriaLabel(StrL("Close"))->OnClickAction(action::Cancel());
}
El* DialogClose::WithTrigger(Ctx* cx, El* trigger, int clickId) {
    Arena* a = cx->a;
    // `when(self.trigger.is_none(), on_click)`: the button handles the click,
    // so the wrapper does not.
    El* root = UiRoot(a, StrL("dialog-close"), clickId);
    if (trigger) {
        root->Child(trigger);
    }
    return root;
}

Dialog* Dialog::New(Ctx* cx) {
    Arena* a = cx->a;
    Dialog* d = ArenaNew<Dialog>(a);
    d->cx = cx;
    d->trap = StrL("dialog");
    d->root = Div(a)
                  ->Role(AccessibilityRole::Dialog)
                  ->Fixed()
                  ->Top(0)
                  ->Left(0)
                  ->W(kFill)
                  ->H(kFill);
    return d;
}

Dialog* Dialog::Trap(Str name) {
    trap = name;
    return this;
}

Dialog* Dialog::Open(bool value) {
    open = value;
    return this;
}

Dialog* Dialog::Handle(DialogHandle value) {
    handle = value;
    return this;
}

Dialog* Dialog::Backdrop(El* backdrop) {
    if (backdrop) {
        root->Child(backdrop);
    }
    return this;
}

Dialog* Dialog::Popup(El* popup) {
    if (popup) {
        // The popup is the trap container, not the backdrop: a Tab inside a
        // dialog reaches its own controls and nothing behind it.
        int id = FocusTrapId(trap);
        popup->TrapId(id);
        // The popup is also the host DialogBindKeys made focusable, under the
        // same name, so a dialog with no control in it still takes the focus.
        FocusTrapArm(cx->win, id, id);
        root->Child(popup);
    }
    return this;
}

El* Dialog::IntoEl() {
    bool visible = handle.IsValid() ? handle.IsOpen(cx->app, open) : open;
    return visible ? root : Div(cx->a);
}
} // namespace gpui
