#ifndef GPUI_BASE_DIALOG_H_
#define GPUI_BASE_DIALOG_H_
/* Unstyled dialog — crates/base/src/dialog.rs */

#include "gpui/gpui.h"

namespace gpui {

// Why a dialog's open state changed. Rust reports it alongside the new value
// so a caller can tell a confirm from a dismissal — the same close, but not
// the same outcome.
enum class DialogChangeReason : uint8_t {
    TriggerPress,
    BackdropPress,
    Cancel,
    Confirm,
    Imperative
};

struct DialogOpenChangeEvent {
    bool open = false;
    DialogChangeReason reason = DialogChangeReason::Imperative;
};

struct DialogHandleState {
    EntityId self = {};
    bool open = false;
    Listener onOpenChange = {};
};

// Rust clones an Rc<Cell<bool>>. This tree's shared, stale-safe ownership
// projection is an Entity handle: copies name the same open cell and become
// inert when the application drops it.
struct DialogHandle {
    Entity<DialogHandleState> state = {};

    static DialogHandle New(Ctx* cx, bool open = false);
    bool IsValid() const { return state.IsValid(); }
    bool IsOpen(App* app, bool fallback = false) const;
    void OnOpenChange(App* app, Listener listener) const;
    bool SetOpen(Ctx* cx, bool open, DialogChangeReason reason) const;
    bool Open(Ctx* cx) const;
    bool Close(Ctx* cx) const;
};

// What an action asks a dialog to do. Rust binds escape to Cancel and enter
// to Confirm in the "Dialog" key context.
enum class DialogAction : uint8_t {
    None,
    Cancel,
    Confirm
};

// dialog.rs::init, and the context its two bindings live in.
void DialogInitKeys();
Str DialogContext();
DialogAction DialogActionOf(uint32_t id);

// Where a dialog's two handlers wait between frames. Rust's Dialog is a view
// and owns them; the port's is a builder that is gone by the time a keystroke
// arrives, so they live in a keyed entity beside it — which is what an action
// can still find.
struct DialogKeys {
    // on_cancel and on_ok, as the caller gave them: ClickEvent handlers, the
    // same ones the Cancel and OK buttons carry. Rust calls them with a
    // ClickEvent::default() from the action, and so does this.
    Listener onCancel = {};
    Listener onOk = {};
    // What escape falls back to when the dialog has no Cancel of its own —
    // Rust's on_cancel defaults to "yes, close", and closing is what the x
    // and the backdrop do.
    Listener onClose = {};

    static void OnAction(DialogKeys* self, Ctx* cx, const ActionEvent* ev);
};

// `.when(self.keyboard, |this| this.key_context(CONTEXT))` and the two
// on_action handlers under it. `name` is the dialog's trap name, which is one
// per layer, so a dialog stacked on another keeps its own handlers. Not
// called at all for a dialog with the keyboard turned off, which is how Rust
// spells `close_on_escape(false)`: no context, so neither binding exists.
void DialogBindKeys(Ctx* cx, El* popup, Str name, Listener onCancel,
                    Listener onOk, Listener onClose);

// Whether a press on the backdrop dismisses. Rust checks four things in
// on_any_mouse_down: the press is below the region reserved at the top — a
// drag on the title bar is not a dismissal — the button is the left one, the
// dialog is `overlay_closable`, and it is the topmost of a stack, so a press
// only ever closes the one on top. `overlayClosable` is what
// `close_on_backdrop_press` sets.
bool DialogBackdropCloses(bool overlayClosable, bool topmost,
                          MouseButton button, float pressY,
                          float dismissBelowY);

struct DialogBackdrop {
    static El* New(Ctx* cx);
};
struct DialogPopup {
    static El* New(Ctx* cx);
};
struct DialogTitle {
    static El* New(Ctx* cx);
};
struct DialogDescription {
    static El* New(Ctx* cx);
};
struct DialogClose {
    static El* New(Ctx* cx, int clickId = 0);
    // trigger(build): the wrapper around a button that already supports
    // pointer, keyboard and accessibility activation. Rust builds
    // `Button::new("close").accessibility_label("Close").on_click(activate)`
    // and hands it to the builder for presentation; here the builder makes
    // the button and `DialogCloseActivation` puts the name and the Cancel
    // dispatch on it. The wrapper does not also handle clicks when a trigger
    // is supplied, so the close activates once.
    static El* WithTrigger(Ctx* cx, El* trigger, int clickId = 0);
};
// The accessible name "Close" and cancel activation a close trigger carries.
El* DialogCloseActivation(El* button);

// The trigger takes the press, not the click, and stops it there — Rust's
// DialogTrigger is an on_mouse_down with cx.stop_propagation().
struct DialogTrigger {
    static El* New(Ctx* cx, Listener onOpen = {}, DialogHandle handle = {},
                   Str id = StrL("dialog-trigger"));
};

struct Dialog {
    Ctx* cx = nullptr;
    El* root = nullptr;
    // focus_trap("dialog-{layer}"): the popup keeps Tab inside itself while
    // it is open. A stack of dialogs names one trap per layer, so the top one
    // traps on its own rather than sharing with the dialog underneath.
    Str trap = {};
    bool open = true;
    DialogHandle handle = {};

    static Dialog* New(Ctx* cx);
    Dialog* Open(bool value);
    Dialog* Handle(DialogHandle value);
    Dialog* Trap(Str name);
    Dialog* Backdrop(El* backdrop);
    Dialog* Popup(El* popup);
    El* IntoEl();
};
} // namespace gpui
#endif // GPUI_BASE_DIALOG_H_
