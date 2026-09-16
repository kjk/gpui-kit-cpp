#include "base/hover_card.h"

namespace gpui {

// cancel_tasks + next_epoch in one: there is at most one countdown in flight,
// so dropping it is the whole of what the epoch was protecting against.
static void HoverCardCancel(HoverCardState* self, Ctx* cx) {
    if (self->timer) {
        WindowCancelTimer(cx->win, self->timer);
        self->timer = 0;
    }
}

static void HoverCardSetOpen(HoverCardState* self, Ctx* cx, bool open) {
    if (self->open == open) {
        return;
    }
    self->open = open;
    Notify(cx);
    if (self->onOpenChange.IsValid()) {
        HoverCardOpenChangeEvent ev = {open};
        ListenerCall(cx->app, cx->win, self->onOpenChange, &ev);
    }
}

void HoverCardState::OnOpen(HoverCardState* self, Ctx* cx, const TickEvent*) {
    self->timer = 0;
    HoverCardSetOpen(self, cx, true);
}

void HoverCardState::OnClose(HoverCardState* self, Ctx* cx, const TickEvent*) {
    self->timer = 0;
    // The countdown started because nothing was hovered; by the time it lands
    // the pointer may have come back, so ask again.
    if (self->hoveringTrigger || self->hoveringContent) {
        return;
    }
    HoverCardSetOpen(self, cx, false);
}

void HoverCardState::OnTap(HoverCardState* self, Ctx* cx, const ClickEvent*,
                           intptr_t open) {
    HoverCardCancel(self, cx);
    HoverCardSetOpen(self, cx, open != 0);
}

void HoverCardState::OnDismiss(HoverCardState* self, Ctx* cx,
                               const MouseUpEvent*) {
    HoverCardCancel(self, cx);
    HoverCardSetOpen(self, cx, false);
}

static void HoverCardScheduleOpen(HoverCardState* self, Ctx* cx) {
    HoverCardCancel(self, cx);
    self->timer = WindowSetTimeout(cx->win, self->openDelayMs,
                                   Listen(cx, &HoverCardState::OnOpen));
}

static void HoverCardScheduleClose(HoverCardState* self, Ctx* cx) {
    HoverCardCancel(self, cx);
    self->timer = WindowSetTimeout(cx->win, self->closeDelayMs,
                                   Listen(cx, &HoverCardState::OnClose));
}

void HoverCardTriggerHover(HoverCardState* self, Ctx* cx,
                           const HoverEvent* ev) {
    self->hoveringTrigger = ev->hovered;
    if (ev->hovered) {
        HoverCardScheduleOpen(self, cx);
    } else if (!self->hoveringContent) {
        HoverCardScheduleClose(self, cx);
    }
}

void HoverCardContentHover(HoverCardState* self, Ctx* cx,
                           const HoverEvent* ev) {
    self->hoveringContent = ev->hovered;
    if (ev->hovered) {
        // Reaching the content is what keeps the card up: whatever countdown
        // was running is dropped and none replaces it.
        HoverCardCancel(self, cx);
    } else if (!self->hoveringTrigger) {
        HoverCardScheduleClose(self, cx);
    }
}

void HoverCardSetDelays(Ctx* cx, Entity<HoverCardState> state, int openMs,
                        int closeMs, Listener onOpenChange) {
    HoverCardState* s = state.Get(cx);
    if (!s) {
        return;
    }
    s->openDelayMs = openMs;
    s->closeDelayMs = closeMs;
    s->onOpenChange = onOpenChange;
}

bool HoverCardIsOpen(Ctx* cx, Entity<HoverCardState> state) {
    HoverCardState* s = state.Get(cx);
    return s && s->open;
}

// use_keyed_state(id): one state per card id, made on the frame that first
// asks for it and kept by the app afterwards. Without it the delays would be
// thrown away with the tree that armed them.
Entity<HoverCardState> HoverCardStateFor(Ctx* cx, Str id) {
    return KeyedEntity<HoverCardState>(cx, KeyedName(cx, id));
}

bool HoverCardIsOpen(Ctx* cx, Str id) {
    return HoverCardIsOpen(cx, HoverCardStateFor(cx, id));
}

HoverCard* HoverCard::New(Ctx* cx, Str id, Entity<HoverCardState> state) {
    Arena* a = cx->a;
    HoverCard* h = ArenaNew<HoverCard>(a);
    h->a = a;
    h->cx = cx;
    h->id = id;
    h->state = state.IsValid() ? state : HoverCardStateFor(cx, id);
    if (HoverCardState* s = h->state.Get(cx)) {
        // Builder callbacks are frame-supplied. Omission on a later frame
        // clears one that is no longer rendered, just like Rust's sync(None).
        s->onOpenChange = {};
    }
    h->root = Div(a)->Id(id);
    return h;
}

bool HoverCard::IsOpen() const {
    return HoverCardIsOpen(cx, state);
}

// A part only hovers if it can name itself, so one that came without an
// identity is given the name its place in the card asks for. The card's root
// is what scopes it, which is why the name needs no prefix.
static void HoverPart(El* e, const char* suffix) {
    // A hit target may be one the fold has not numbered yet — the id is
    // computed after the tree exists — so asking for the number here would
    // read zero off every element that names itself by its path.
    if (e->clickId || e->clickFromPath) {
        return;
    }
    e->PathClick(Str(suffix));
}

HoverCard* HoverCard::Trigger(El* trigger) {
    if (!trigger) {
        return this;
    }
    if (state.IsValid()) {
        HoverPart(trigger, "trigger");
        if (tapToOpen) {
            trigger->OnClick(
                ListenTo(state, &HoverCardState::OnTap, IsOpen() ? 0 : 1));
        } else {
            trigger->OnHover(ListenTo(state, &HoverCardTriggerHover));
        }
    }
    root->Child(trigger);
    return this;
}

HoverCard* HoverCard::Content(El* content) {
    if (!content) {
        return this;
    }
    // Under the trigger unless the caller already placed it.
    if (!content->style.absolute) {
        content->Absolute()->Top(22)->Left(0);
    }
    if (state.IsValid()) {
        HoverPart(content, "content");
        if (tapToOpen) {
            content->OnMouseUpOut(ListenTo(state, &HoverCardState::OnDismiss));
        } else {
            content->OnHover(ListenTo(state, &HoverCardContentHover));
        }
    }
    root->Child(content);
    return this;
}

HoverCard* HoverCard::OnOpenChange(Listener fn) {
    if (HoverCardState* s = state.Get(cx)) {
        s->onOpenChange = fn;
    }
    return this;
}

El* HoverCard::IntoEl() {
    return root;
}
} // namespace gpui
