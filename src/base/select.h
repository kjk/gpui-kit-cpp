#ifndef GPUI_BASE_SELECT_H_
#define GPUI_BASE_SELECT_H_
/* Unstyled select — crates/base/src/select.rs */

#include "gpui/gpui.h"

namespace gpui {

// What a keystroke asks a select to do. Rust binds up, down, enter and escape
// to SelectUp, SelectDown, Confirm and Cancel in the select's key context;
// this is the same table, read as an answer rather than dispatched as actions,
// since there is no action system here to route them through.
enum class SelectAction : uint8_t {
    // The key was not one of the select's, or the select is disabled and Rust
    // would have called cx.propagate() and done nothing itself.
    None,
    // Open it. Up and Down both open a closed select; so does Enter.
    Open,
    // Take the highlighted option. Enter, while open.
    Confirm,
    // Close it and put focus back on the trigger. Escape, while open —
    // Escape on a closed select propagates instead.
    Dismiss
};

// select.rs::init: up, down, enter, secondary-enter and escape in the
// "Select" key context.
void SelectInitKeys();
Str SelectContext();

// The rules, whole. `open` and `disabled` are the select's current state.
//
// Rust also moves focus to the content handle whenever Up, Down or Enter is
// taken, and back to the trigger on Cancel. That is a pair of focus handles a
// select does not have here; the trigger and the query field are both inside
// the element that declares the context, so a chord finds the select wherever
// of the two focus is.
SelectAction SelectActionOf(uint32_t id, bool open, bool disabled);

struct Select {
    // `accessibilityValue` is the committed value the controlled root
    // exposes — a readable selection title, not the current search query or
    // cursor — so a platform adapter that flattens the trigger child still
    // reads what is selected. Activation sits on the root for the same
    // reason, and a disabled select exposes none.
    static El* New(Ctx* cx, Str id, bool open = false, bool disabled = false,
                   Str accessibilityLabel = {}, Listener onOpenChange = {},
                   Str accessibilityValue = {});
};
} // namespace gpui
#endif // GPUI_BASE_SELECT_H_
