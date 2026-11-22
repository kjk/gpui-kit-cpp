/* Ported from crates/gpui's EventEmitter — cx.emit, cx.subscribe and the
 * Subscription they hand back.
 *
 * Rust marks what an entity emits with a trait and drops a subscription when
 * its guard drops. EventEmitter<T, E> marks the same pairing here. Nothing
 * drops on scope exit, so the subscription remains an explicit handle. */

#include "Test.h"

namespace gpui {

// The emitter is a state rather than a view, the way a ListState is.
struct EventTicker {
    int unused = 0;
};

struct ResetTickEvent {
    int value = 0;
};

template <>
struct EventEmitter<EventTicker, ListEvent> {};

template <>
struct EventEmitter<EventTicker, ResetTickEvent> {};

static_assert(EmitsEvent<EventTicker, ListEvent>);
static_assert(EmitsEvent<EventTicker, ResetTickEvent>);
static_assert(!EmitsEvent<EventTicker, CalendarEvent>);

} // namespace gpui

namespace {

struct Counter {
    int seen = 0;
    int last = 0;
    int resets = 0;

    static El* Render(Counter*, Ctx* cx) { return Div(cx->a); }

    static void OnTick(Counter* self, Ctx*, const ListEvent* ev) {
        self->seen++;
        self->last = ev->index;
    }

    static void OnReset(Counter* self, Ctx*, const ResetTickEvent* ev) {
        self->resets++;
        self->last = ev->value;
    }
};

} // namespace

static void EverySubscriberHearsIt() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<EventTicker> ticker = EntityNewState<EventTicker>(&app);
    Entity<Counter> a = EntityNew<Counter>(&app);
    Entity<Counter> b = EntityNew<Counter>(&app);

    SubscribeTo(&app, ticker, a, &Counter::OnTick);
    SubscribeTo(&app, ticker, b, &Counter::OnTick);
    utassert(EntitySubscriberCount(&app, ticker.id) == 2);

    ListEvent ev = {ListEventKind::Confirm, 7, false};
    EntityEmit(&app, win, ticker, &ev);
    utassert(a.Get(&app)->seen == 1 && a.Get(&app)->last == 7);
    utassert(b.Get(&app)->seen == 1 && b.Get(&app)->last == 7);

    // Nothing else is listening to this one, so nothing hears it.
    Entity<EventTicker> other = EntityNewState<EventTicker>(&app);
    EntityEmit(&app, win, other, &ev);
    utassert(a.Get(&app)->seen == 1);

    delete win;
    EntityDropAll(&app);
}

static void ASubscriptionCanBeGivenUp() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<EventTicker> ticker = EntityNewState<EventTicker>(&app);
    Entity<Counter> a = EntityNew<Counter>(&app);
    Subscription sub = SubscribeTo(&app, ticker, a, &Counter::OnTick);
    utassert(sub.IsValid());

    ListEvent ev = {ListEventKind::Select, 1, false};
    EntityEmit(&app, win, ticker, &ev);
    utassert(a.Get(&app)->seen == 1);

    EntityUnsubscribe(&app, sub);
    utassert(EntitySubscriberCount(&app, ticker.id) == 0);
    EntityEmit(&app, win, ticker, &ev);
    utassert(a.Get(&app)->seen == 1);

    delete win;
    EntityDropAll(&app);
}

// A subscription is dead once either end of it is: Rust drops the whole list
// with the emitter, and the Subscription with the subscriber.
static void AStaleEndSweepsTheSubscription() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<EventTicker> ticker = EntityNewState<EventTicker>(&app);
    Entity<Counter> gone = EntityNew<Counter>(&app);
    Entity<Counter> alive = EntityNew<Counter>(&app);
    SubscribeTo(&app, ticker, gone, &Counter::OnTick);
    SubscribeTo(&app, ticker, alive, &Counter::OnTick);

    EntityDrop(&app, gone.id);
    utassert(EntitySubscriberCount(&app, ticker.id) == 1);

    ListEvent ev = {ListEventKind::Select, 2, false};
    EntityEmit(&app, win, ticker, &ev);
    utassert(alive.Get(&app)->seen == 1);

    // And the emitter going away takes the rest with it.
    EntityDrop(&app, ticker.id);
    utassert(EntitySubscriberCount(&app, ticker.id) == 0);
    utassert(len(app.subs) == 0);

    delete win;
    EntityDropAll(&app);
}

static void EventTypesAreSeparateChannels() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<EventTicker> ticker = EntityNewState<EventTicker>(&app);
    Entity<Counter> list = EntityNew<Counter>(&app);
    Entity<Counter> reset = EntityNew<Counter>(&app);

    SubscribeTo(&app, ticker, list, &Counter::OnTick);
    SubscribeTo(&app, ticker, reset, &Counter::OnReset);

    ListEvent tick = {ListEventKind::Confirm, 7, false};
    EntityEmit(&app, win, ticker, &tick);
    utassert(list.Get(&app)->seen == 1);
    utassert(reset.Get(&app)->resets == 0);

    ResetTickEvent resetEvent = {42};
    EntityEmit(&app, win, ticker, &resetEvent);
    utassert(list.Get(&app)->seen == 1);
    utassert(reset.Get(&app)->resets == 1);
    utassert(reset.Get(&app)->last == 42);

    delete win;
    EntityDropAll(&app);
}

// window.use_keyed_state remembers a slot by its key alone, so two kinds of
// state under one name would be one slot — and the second would read the
// first's memory as its own. KeyedKey is what keeps them apart: a popover's
// own state and the listeners its escape runs are both keyed by the popover's
// id, and they must not be the same entity.
static void TwoKindsUnderOneNameAreTwoSlots() {
    App app;
    Window* win = new Window();
    win->app = &app;

    uint32_t name = (uint32_t)HashClickId(StrL("popover-1"));
    uint32_t a = KeyedKey(name, (uint32_t)HashClickId(StrL("PopoverState")));
    uint32_t b = KeyedKey(name, (uint32_t)HashClickId(StrL("CancelKeys")));
    utassert(a != b);
    // And neither is the bare name, so an older caller keyed by it is not the
    // same slot either.
    utassert(a != name && b != name);

    EntityId ea = WindowKeyedEntity(win, &app, a, new EventTicker(),
                                    &EntityDropT<EventTicker>);
    EntityId eb =
        WindowKeyedEntity(win, &app, b, new Counter(), &EntityDropT<Counter>);
    utassert(ea.IsValid() && eb.IsValid() && ea != eb);
    // Asking again gives the same slot back, which is what keyed state is.
    EntityId again = WindowKeyedEntity(win, &app, a, new EventTicker(),
                                       &EntityDropT<EventTicker>);
    utassert(again == ea);

    WindowKeyedFree(win);
    delete win;
    EntityDropAll(&app);
}

void TestEventEmitter() {
    TestSuite("event_emitter");
    EverySubscriberHearsIt();
    ASubscriptionCanBeGivenUp();
    AStaleEndSweepsTheSubscription();
    EventTypesAreSeparateChannels();
    TwoKindsUnderOneNameAreTwoSlots();
}
