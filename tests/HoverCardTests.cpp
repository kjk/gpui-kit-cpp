/* Ported from crates/base/src/hover_card.rs.
 *
 * A hover card is not "is the trigger hovered": it waits out an open delay
 * before it appears and a close delay before it goes, and the close only
 * lands if nothing is hovered by the time it arrives — which is what lets the
 * pointer travel from the trigger to the card. Rust's own case there drives a
 * window and advances a clock; these are the rules underneath, plus the keyed
 * state that is what makes them survive a frame at all. */

#include "Test.h"

struct HoverCardRecorder {
    int changes = 0;
    bool lastOpen = false;

    static void OnOpenChange(HoverCardRecorder* self, Ctx*,
                             const HoverCardOpenChangeEvent* ev) {
        self->changes++;
        self->lastOpen = ev->open;
    }
};

// window.use_keyed_state(self.id, ..): the card makes its own state, so a
// page declares no field for one. Two cards are two states, and the same card
// asked twice is one — which is the whole point, since the tree that armed a
// countdown is thrown away before it fires.
static void EveryCardIdIsItsOwnStateAndKeepsIt() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;

    Entity<HoverCardState> one = HoverCardStateFor(&cx, StrL("card-1"));
    Entity<HoverCardState> two = HoverCardStateFor(&cx, StrL("card-2"));
    utassert(one.IsValid() && two.IsValid());
    utassert(one.id != two.id);

    HoverCardState* s = one.Get(&cx);
    utassert(s && !s->open);
    s->open = true;
    // The next frame asks again and gets the same state back, still open.
    utassert(HoverCardIsOpen(&cx, StrL("card-1")));
    utassert(!HoverCardIsOpen(&cx, StrL("card-2")));

    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

// schedule_close's closure asks again when it lands: `if state.epoch == epoch
// && !is_hovering_trigger && !is_hovering_content`. The countdown started
// because nothing was hovered, but by the time it fires the pointer may have
// arrived on the card.
static void ACloseThatLandsOnAHoveredCardDoesNothing() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;

    HoverCardState s;
    s.open = true;
    s.hoveringContent = true;
    HoverCardState::OnClose(&s, &cx, nullptr);
    utassert(s.open);

    // And with the pointer gone from both, the same landing closes it.
    s.hoveringContent = false;
    HoverCardState::OnClose(&s, &cx, nullptr);
    utassert(!s.open);

    delete win;
    EntityDropAll(&app);
}

static void DelayedTransitionsAnnounceWhenTheyLand() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Ctx cx = {&app, win, nullptr, {}};
    Entity<HoverCardState> card = EntityNewState<HoverCardState>(&app);
    Entity<HoverCardRecorder> recorder =
        EntityNewState<HoverCardRecorder>(&app);
    cx.self = recorder.id;
    HoverCardSetDelays(&cx, card, 10, 10,
                       Listen(&cx, &HoverCardRecorder::OnOpenChange));

    HoverCardState* state = card.Get(&cx);
    HoverCardState::OnOpen(state, &cx, nullptr);
    HoverCardState::OnOpen(state, &cx, nullptr);
    HoverCardState::OnClose(state, &cx, nullptr);
    HoverCardRecorder* seen = recorder.Get(&app);
    utassert(seen->changes == 2);
    utassert(!seen->lastOpen);

    delete win;
    EntityDropAll(&app);
}

static void TapCardsToggleWithoutHoverAndDismissOutside() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    Entity<HoverCardState> state = EntityNewState<HoverCardState>(&app);
    HoverCardState* s = state.Get(&app);

    HoverCard* card = HoverCard::New(&cx, StrL("tap-card"), state);
    card->tapToOpen = true;
    El* trigger = Div(a);
    card->Trigger(trigger);
    utassert(trigger->listener.IsValid());
    utassert(!trigger->onHover.IsValid());
    ListenerCall(&app, win, trigger->listener, nullptr);
    utassert(s->open);

    El* content = Div(a);
    card->Content(content);
    utassert(content->onMouseUpOut.IsValid());
    utassert(!content->onHover.IsValid());
    ListenerCall(&app, win, content->onMouseUpOut, nullptr);
    utassert(!s->open);

    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

// hover_card.rs trigger_style_is_applied_to_the_trigger_container: w_full
// on the container stretches it across the row, and the card follows that
// right edge. On the trigger itself it would only fill the content wrap.
static Bounds HoverTriggerStyledContent(bool styled) {
    App app;
    Window* win = new Window();
    Arena* a = ArenaNew();
    win->app = &app;
    win->frameArena = a;
    component::Init(&app);
    Ctx cx = {&app, win, a, {}};
    PaintCtx ctx = {};
    ctx.viewW = 1024;
    ctx.viewH = 768;
    El* content = nullptr;
    El* root = nullptr;
    for (int frame = 0; frame < 2; frame++) {
        content = Div(a)->W(20)->H(20);
        component::HoverCard* card =
            component::HoverCard::New(&cx, StrL("trigger-style-hover-card"))
                ->Open(true)
                ->OpenDelay(0)
                ->Anchor(PopupAnchor::TopRight)
                ->Trigger(Div(a)->W(40)->H(40))
                ->Content(content);
        if (styled) {
            Style full;
            full.width = kFill;
            card->TriggerStyle(full, StyleFieldWidth);
        }
        root = Div(a)->W(1024)->H(768)->Child(
            Div(a)->Absolute()->Left(100)->Top(100)->W(200)->FlexRow()->Child(
                card->IntoEl()));
    }
    LayoutEl(&ctx, root, 0, 0, 1024, 768, 14, Rgba{});
    Bounds out = {content->x, content->y, content->w, content->h};
    WindowKeyedFree(win);
    delete win;
    ArenaDelete(a);
    EntityDropAll(&app);
    return out;
}

static void TriggerStyleIsAppliedToTheTriggerContainer() {
    Bounds plain = HoverTriggerStyledContent(false);
    utassertnear(plain.x + plain.w, 140.f);
    Bounds styled = HoverTriggerStyledContent(true);
    utassertnear(styled.x + styled.w, 300.f);
}

void TestHoverCard() {
    TriggerStyleIsAppliedToTheTriggerContainer();
    TestSuite("hover_card");
    EveryCardIdIsItsOwnStateAndKeepsIt();
    ACloseThatLandsOnAHoveredCardDoesNothing();
    DelayedTransitionsAnnounceWhenTheyLand();
    TapCardsToggleWithoutHoverAndDismissOutside();
}
