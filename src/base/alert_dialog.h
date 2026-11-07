#ifndef GPUI_BASE_ALERT_DIALOG_H_
#define GPUI_BASE_ALERT_DIALOG_H_
/* Unstyled alert dialog — crates/base/src/alert_dialog.rs */

#include "gpui/gpui.h"
#include "base/dialog.h"

namespace gpui {

// AlertDialog::new is `Dialog::new(cx).close_on_backdrop_press(false)`: an
// alert is a question the reader has to answer, so the backdrop never
// dismisses it. Everything else about it is a Dialog, including the escape and
// enter bindings, which DialogActionForKey answers for.
const bool kAlertDialogClosesOnBackdropPress = false;

struct AlertDialogBackdrop {
    static El* New(Ctx* cx);
};
struct AlertDialogPopup {
    static El* New(Ctx* cx);
};
struct AlertDialogTitle {
    static El* New(Ctx* cx);
};
struct AlertDialogDescription {
    static El* New(Ctx* cx);
};
// Both parts dispatch an action rather than carrying a handler of their
// own, which is what Rust's Cancel and Action wrappers do: their click calls
// `window.dispatch_action`, so the dialog's own Cancel and Confirm handlers
// are what run and the caller declares them once rather than passing the same
// listener to the button and to the keyboard.
struct AlertDialogCancel {
    static El* New(Ctx* cx);
};
struct AlertDialogAction {
    static El* New(Ctx* cx);
};
struct AlertDialogTrigger {
    static El* New(Ctx* cx, Listener onOpen = {}, DialogHandle handle = {});
};

// An alert is a Dialog, so it traps focus like one — under its own name,
// since an alert is what a dialog opens on top of.
struct AlertDialog : DialogBuilder<AlertDialog> {
    static AlertDialog* New(Ctx* cx);
    AlertDialog* Popup(El* popup);
};
} // namespace gpui
#endif // GPUI_BASE_ALERT_DIALOG_H_
