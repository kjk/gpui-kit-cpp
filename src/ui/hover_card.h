#ifndef GPUI_UI_HOVER_CARD_H_
#define GPUI_UI_HOVER_CARD_H_
/* Themed hover card — crates/ui/src/hover_card.rs */

#include "ui/sizing.h"
#include "base/popup.h"

namespace gpui {

namespace component {

// HoverCard::anchor: which corner of the trigger the card hangs off.
// HoverCard::anchor is `gpui::Anchor`, the same one the Popover takes, so it
// is the same enum here rather than a second one with the corners reordered.
using HoverCardAnchor = PopupAnchor;

// Whether the card of this id is showing. The delayed open and close live in
// gpui_base::HoverCardState, keyed off the id the way Rust keys it off
// use_keyed_state, so a page asks rather than deciding: it needs the answer
// before it builds the content, which is what Rust's `content` closure is for.
bool HoverCardOpen(Ctx* cx, Str id);

struct HoverCard {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    El* trigger = nullptr;
    El* content = nullptr;
    // HoverCard::content(closure): built only while the card shows, in place
    // of `content`.
    El* (*contentFn)(void* user, Ctx* cx) = nullptr;
    void* contentUser = nullptr;
    // Set only by Open(); otherwise the state decides.
    bool controlled = false;
    bool open = false;
    int openDelayMs = 600;
    int closeDelayMs = 300;
    Listener onOpenChange = {};
    // hover_card.rs: `anchor: Anchor::TopCenter`.
    HoverCardAnchor anchor = HoverCardAnchor::TopCenter;

    static HoverCard* New(Ctx* cx);
    static HoverCard* New(Ctx* cx, Str id);
    HoverCard* Trigger(El* e);
    HoverCard* Content(El* e);
    HoverCard* ContentBuilder(El* (*fn)(void* user, Ctx* cx), void* user);
    HoverCard* Open(bool v);
    HoverCard* OpenDelay(int ms);
    HoverCard* CloseDelay(int ms);
    HoverCard* OnOpenChange(Listener fn);
    HoverCard* Anchor(HoverCardAnchor a);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_UI_HOVER_CARD_H_
