#include "ui/hover_card.h"

namespace gpui {

namespace component {

HoverCard* HoverCard::New(Ctx* cx) {
    Arena* a = cx->a;
    HoverCard* h = ArenaNew<HoverCard>(a);
    h->a = a;
    h->cx = cx;
    return h;
}
HoverCard* HoverCard::Trigger(El* e) {
    trigger = e;
    return this;
}
HoverCard* HoverCard::Content(El* e) {
    content = e;
    return this;
}
HoverCard* HoverCard::ContentBuilder(El* (*fn)(void* user, Ctx* cx),
                                     void* user) {
    contentFn = fn;
    contentUser = user;
    return this;
}
HoverCard* HoverCard::Open(bool v) {
    controlled = true;
    open = v;
    return this;
}
HoverCard* HoverCard::OpenDelay(int ms) {
    openDelayMs = ms;
    return this;
}
HoverCard* HoverCard::CloseDelay(int ms) {
    closeDelayMs = ms;
    return this;
}
HoverCard* HoverCard::OnOpenChange(Listener fn) {
    onOpenChange = fn;
    return this;
}

bool HoverCardOpen(Ctx* cx, Str id) {
    return HoverCardIsOpen(cx, id);
}
HoverCard* HoverCard::New(Ctx* cx, Str id) {
    HoverCard* h = New(cx);
    h->id = id;
    return h;
}
HoverCard* HoverCard::Anchor(HoverCardAnchor v) {
    anchor = v;
    return this;
}

El* HoverCard::IntoEl() {
    Str cardId = id.s ? id : StrL("hover-card");
    Entity<HoverCardState> st = HoverCardStateFor(cx, cardId);
    // sync(open_delay, close_delay): the caller's numbers every frame.
    HoverCardSetDelays(cx, st, openDelayMs, closeDelayMs);
    bool isOpen = controlled ? open : HoverCardIsOpen(cx, st);
    if (isOpen && contentFn) {
        content = contentFn(contentUser, cx);
    }
    El* card = isOpen ? content : nullptr;
    if (card) {
        // The eight anchors are Popup's own, including its edge point and
        // eight-pixel viewport clamp, with no offset of its own.
        // render_popover_content adds top_1 (bottom_1 for the bottom
        // anchors) to the surface, a four-pixel relative inset.
        if (anchor == PopupAnchor::BottomLeft ||
            anchor == PopupAnchor::BottomCenter ||
            anchor == PopupAnchor::BottomRight) {
            card->Bottom(4);
        } else {
            card->Top(4);
        }
        PopupPlaceContent(card, anchor);
    }
    return gpui::HoverCard::New(cx, cardId, st)
        ->OnOpenChange(onOpenChange)
        ->Trigger(trigger)
        ->Content(card)
        ->IntoEl();
}

} // namespace component
} // namespace gpui
