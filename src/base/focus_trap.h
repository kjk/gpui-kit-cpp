#ifndef GPUI_BASE_FOCUS_TRAP_H_
#define GPUI_BASE_FOCUS_TRAP_H_
/* Focus trap — crates/base/src/focus_trap.rs

   A container that keeps Tab inside it: a dialog, a sheet, an alert. Rust
   registers the container's focus handle in a global FocusTrapManager, and
   Root's Tab action asks `active_focus_trap` whether the focused element is
   inside one before it moves focus. The runtime here already carries the
   trap on the element (`El::TrapId`) and FocusNext already refuses to leave
   one, so this is the other half: which trap has focus, and how focus gets
   into a trap that has just opened.

   A trap id is a hash of a name, the way a click id is, so the same container
   comes back with the same trap across frames. */

#include "gpui/gpui.h"

namespace gpui {

// focus_trap.rs::init. Rust installs a global weak-handle registry; C++
// rebuilds the equivalent trap membership from the element tree every frame,
// so initialization has no allocation to perform but remains a named crate
// seam.
void FocusTrapInit(App* app);

// The concrete result of Rust's FocusTrapElement::focus_trap. Rust can
// delegate Element layout through a generic wrapper; the POD element tree
// refines the supplied element in place and returns it.
struct FocusTrapContainer {
    static El* New(Ctx* cx, Str id, FocusHandle focus, El* child);
};

// The name a container traps under. Same hash as a click id — a dialog can
// pass its own id and get a trap of its own without inventing a number.
int FocusTrapId(Str name);

// FocusTrapManager::find_active_trap: the innermost trap the focused element
// sits in, or 0 when focus is outside every trap. A child scope confines
// navigation even when an ancestor contains the same element.
int FocusTrapActive(const Window* win);

// The trap `focusId` belongs to, or 0.
int FocusTrapOf(const Window* win, int focusId);

// Root::on_action_tab / on_action_tab_prev: move focus, staying inside the
// active trap when there is one. Answers the id now focused.
int FocusTrapTab(Window* win, bool backward);

// Move focus to the first focusable inside `trapId` (the last, backwards).
// Answers whether focus moved; a trap with nothing focusable in it leaves
// focus where it was.
bool FocusTrapEnter(Window* win, int trapId, bool backward = false);

// A container says it is open this frame. Rust focuses a dialog when it opens;
// here the element tree is built before the focusables are known, so the
// request is armed during the build and settled after the frame's
// FocusCollect. A trap that was present in the preceding frame is left alone.
//
// `hostFocusId` is the container's own focus id, used only when the trap
// holds nothing that takes tab: Rust's container tracks focus itself, so a
// dialog that is all text still has somewhere for focus to be — and still
// hears escape. 0 leaves focus outside, which is what an ordinary trap of
// non-stops wants.
void FocusTrapArm(Window* win, int trapId, int hostFocusId = 0);

// Settle what FocusTrapArm asked for: if the armed trap is newly visible and
// does not already hold focus, focus its first element. Called once a frame,
// after FocusCollect.
void FocusTrapApplyPending(Window* win);

} // namespace gpui
#endif // GPUI_BASE_FOCUS_TRAP_H_
