#ifndef GPUI_BASE_HOVER_CARD_H_
#define GPUI_BASE_HOVER_CARD_H_
/* Unstyled hover card — crates/base/src/hover_card.rs */

#include "gpui/gpui.h"

namespace gpui {

// Rust's HoverCardState, the whole reason a hover card is not just
// "is the trigger hovered". A card waits out `openDelayMs` before it appears
// and `closeDelayMs` before it goes, and a close only lands if neither the
// trigger nor the content is hovered by the time it does — which is what lets
// the pointer travel from one to the other without the card vanishing
// underneath it.
//
// It is an entity because it owns a timer, the way BlinkCursor is. Rust hangs
// it off `window.use_keyed_state(id)`; KeyedEntity is the same hook, so a page
// gets one per card without declaring a field.
struct HoverCardState {
    bool open = false;
    bool hoveringTrigger = false;
    bool hoveringContent = false;
    int openDelayMs = 600;
    int closeDelayMs = 300;
    // The element that supplied this callback is discarded before either
    // delayed transition lands, so the retained state owns the listener.
    Listener onOpenChange = {};
    // The armed timer. Cancelling it is what Rust's epoch counter does: a
    // countdown that has been superseded must not fire.
    int timer = 0;

    static void OnOpen(HoverCardState* self, Ctx* cx, const TickEvent* ev);
    static void OnClose(HoverCardState* self, Ctx* cx, const TickEvent* ev);
    static void OnTap(HoverCardState* self, Ctx* cx, const ClickEvent* ev,
                      intptr_t open);
    static void OnDismiss(HoverCardState* self, Ctx* cx,
                          const MouseUpEvent* ev);
};

struct HoverCardOpenChangeEvent {
    bool open = false;
};

// The trigger and the content each report their own hovering, and the state
// decides what that means. Rust's on_trigger_hover / on_content_hover.
void HoverCardTriggerHover(HoverCardState* self, Ctx* cx, const HoverEvent* ev);
void HoverCardContentHover(HoverCardState* self, Ctx* cx, const HoverEvent* ev);

// `sync`: the delays are the caller's every frame, not just the first one.
void HoverCardSetDelays(Ctx* cx, Entity<HoverCardState> state, int openMs,
                        int closeMs, Listener onOpenChange = {});
bool HoverCardIsOpen(Ctx* cx, Entity<HoverCardState> state);
// The state behind one card id — `window.use_keyed_state(id, ..)`. One place
// keys it, so a skin that wants the delays and the card itself agree on which
// state that is.
Entity<HoverCardState> HoverCardStateFor(Ctx* cx, Str id);
// The same question asked of a card's id rather than its entity, for a caller
// that never named the state because the card made it.
bool HoverCardIsOpen(Ctx* cx, Str id);

// The card itself. `state` is the entity the two hover handlers run against.
// A caller that passes {} gets the card's own, keyed off its id — Rust's
// `window.use_keyed_state(self.id, ..)` in `HoverCard::render`, which is why
// no page upstream declares a field for one. Pass an entity only to share a
// card's state with something outside it.
//
// Hover is reported against an element identity, so the trigger and the
// content are each given one if they arrived without. Rust wraps the trigger
// in a `div().id("trigger")` instead; going onto the element the caller handed
// over keeps its box exactly as it was, which the six anchor corners depend
// on.
struct HoverCard {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    El* root = nullptr;
    Str id = {};
    Entity<HoverCardState> state = {};
    // Mobile targets have no persistent hover. Tests may override this field
    // to exercise the touch policy on a desktop host.
    bool tapToOpen = GPUI_OS_IOS || GPUI_OS_ANDROID;

    static HoverCard* New(Ctx* cx, Str id, Entity<HoverCardState> state = {});
    // Rust's content builder runs only when the card is open, so nothing is
    // built for a card that is not showing. A builder here is handed a tree
    // that is already made, so the caller asks first.
    bool IsOpen() const;
    HoverCard* Trigger(El* trigger);
    HoverCard* Content(El* content);
    HoverCard* OnOpenChange(Listener fn);
    El* IntoEl();
};
} // namespace gpui
#endif // GPUI_BASE_HOVER_CARD_H_
