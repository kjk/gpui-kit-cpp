#ifndef GPUI_BASE_POPOVER_H_
#define GPUI_BASE_POPOVER_H_
/* Unstyled popover — crates/base/src/popover.rs */

#include "gpui/gpui.h"
#include "base/popup.h"

namespace gpui {

// Rust's PopoverState: the open lifecycle belongs to the popover, not to
// whichever page drew it. Uncontrolled by default — the trigger toggles it —
// and a caller that wants to drive it calls PopoverSetOpen every frame.
//
// Rust's state also parks the previously focused handle and restores it on
// close; so does this one. Outside dismissal is attached to the content's
// hitbox through El::OnMouseDownOut, so it remains per-popover rather than
// taking the window's single unhandled-click compatibility slot.
struct PopoverOpenChangeEvent {
    bool open = false;
};

struct PopoverState {
    // The Entity<PopoverState> that owns this state. Rust's Context<Self>
    // supplies it when deferred context is registered; C++ keeps it here for
    // calls that enter through a parent view.
    EntityId self = {};
    bool open = false;
    // Whether default_open has been applied yet. Rust passes it to
    // PopoverState::new, which only runs the first time the key is seen.
    bool seeded = false;
    // The popover's own focus handle — Rust hangs it off the content — and
    // `tracked_focus_handle`, what it focuses instead, which is how a select
    // hands focus to the query field inside it. `Popover::New` fills the
    // first in from the id. Focus that has since moved on to something
    // *inside* the content does not read as the popover's, so a popover the
    // reader tabbed into is left alone on close rather than pulled back;
    // Rust's handle answers for its whole subtree.
    FocusHandle focus = {};
    FocusHandle trackedFocus = {};
    // previous_focus_handle: where focus was when it opened, put back on
    // close if the popover still holds it.
    FocusHandle previousFocus = {};
    // Rust's Rc<dyn Fn(&bool, ..)>. Listener is the port's generational,
    // retained callback record; the event carries the new bool without
    // narrowing the callback to close-only.
    Listener onOpenChange = {};
    // Compatibility for the older themed C++ OnClose surface. It is invoked
    // only by dismissal (outside press or Escape), not by the trigger toggle.
    Listener onDismiss = {};
};

void PopoverInitKeys();

// Open or close, doing what Rust's `toggle_open` does around it: the focus
// goes into the popover (or into whatever it tracks) and comes back out to
// where it was. A widget that is a popover in all but name — a select, a
// dropdown menu — can call this with a state of its own.
void PopoverSetOpenFocused(PopoverState* s, Ctx* cx, bool open);

bool PopoverIsOpen(Ctx* cx, Entity<PopoverState> state);
void PopoverSetOpen(Ctx* cx, Entity<PopoverState> state, bool open);
// `button` is which press this popover answers to: one element hears every
// press it is under, so the filter Rust does at registration is done here.
void PopoverToggle(PopoverState* self, Ctx* cx, const MouseDownEvent* ev,
                   intptr_t button);
void PopoverConfirm(PopoverState* self, Ctx* cx, const ActionEvent* ev);
void PopoverDismiss(PopoverState* self, Ctx* cx, const ClickEvent* ev);
void PopoverDismissOnMouseDown(PopoverState* self, Ctx* cx,
                               const MouseDownEvent* ev);

// The popover. The trigger takes the *press*, not the click, and on whichever
// button was asked for — `Popover::mouse_button`, which is what a right-click
// popover is. `overlayClosable` is Rust's on_mouse_down_out.
struct Popover {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    El* trigger = nullptr;
    El* content = nullptr;
    Entity<PopoverState> state = {};
    FocusHandle focus = {};
    MouseButton button = MouseButton::Left;
    bool overlayClosable = true;
    // Popover::anchor, the gpui::Anchor Popup resolves.
    PopupAnchor anchor = PopupAnchor::TopLeft;
    // Gap from the trigger along the anchor's outward direction, zero by
    // default.
    float offset = 0;
    PopupOnPositionFn onPosition = nullptr;
    void* onPositionUser = nullptr;

    static Popover* New(Ctx* cx, Str id, Entity<PopoverState> state = {},
                        MouseButton button = MouseButton::Left);
    Popover* Anchor(PopupAnchor v);
    Popover* Offset(float offset);
    // Observe geometry to supply presentation such as a pointer arrow.
    Popover* OnPosition(PopupOnPositionFn fn, void* user);
    // Popover::tracked_focus_handle: what takes focus when it opens, instead
    // of the popover itself.
    Popover* TrackedFocus(FocusHandle tracked);
    Popover* OverlayClosable(bool closable);
    Popover* OnOpenChange(Listener fn);
    // C++ compatibility seam used by the themed facade. Rust expresses this
    // as an on_open_change callback which ignores the true half.
    Popover* OnDismiss(Listener fn);
    Popover* Trigger(El* trigger);
    Popover* Content(El* content);
    El* IntoEl();
};
} // namespace gpui
#endif // GPUI_BASE_POPOVER_H_
