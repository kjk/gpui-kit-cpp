#ifndef GPUI_UI_CLIPBOARD_H_
#define GPUI_UI_CLIPBOARD_H_
/* Themed clipboard — crates/ui/src/clipboard.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// What on_copied is handed: the value that just went to the clipboard. Rust
// passes the `SharedString` itself; a listener here takes an event, so the
// value rides in one.
struct ClipboardEvent {
    Str value = {};
};

// The keyed state behind one clipboard button — Rust's ClipboardState, from
// `window.use_keyed_state(self.id, ..)`. `copied` is what turns the icon into
// a checkmark, and the timer is the two-second task that clears it.
//
// It also holds the value and the callback, which Rust's click closure
// captures instead. A listener here has no captures, and the caller refreshes
// both every frame, so a click always copies what the button was showing —
// which is what `value_fn` buys in Rust.
struct ClipboardState {
    bool copied = false;
    int timer = 0;
    Str value = {};
    Listener onCopied = {};

    ~ClipboardState();
    static void OnCopy(ClipboardState* self, Ctx* cx, const ClickEvent* ev);
    static void OnReset(ClipboardState* self, Ctx* cx, const TickEvent* ev);
};

struct Clipboard {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Str value = {};
    Str tooltipText = {};
    Listener onCopied;
    UiSize size = UiSize::XSmall;

    static Clipboard* New(Ctx* cx, Str id);
    Clipboard* Value(Str v);
    Clipboard* Tooltip(Str t);
    Clipboard* OnCopied(Listener fn);
    Clipboard* WithSize(UiSize sizeValue);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_CLIPBOARD_H_
