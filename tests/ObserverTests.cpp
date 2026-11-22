/* Ported from crates/gpui's `App::notify` and `cx.observe` — the untyped half
 * of the pair EventEmitterTests covers.
 *
 * What is worth pinning: an entity that notifies wakes whoever observes it and
 * says which entity it was, an observation can be given up, and the window a
 * notify reaches is the one that has that entity on it. The last of those is
 * GPUI's `Window::dirty_views` and is the reason a notify in one window does
 * not redraw another. */

#include "Test.h"

namespace {

struct Model {
    int value = 0;
};

struct Watcher {
    int seen = 0;
    EntityId last = {};

    static El* Render(Watcher*, Ctx* cx) { return Div(cx->a); }

    static void OnChanged(Watcher* self, Ctx*, const EntityId* who) {
        self->seen++;
        self->last = *who;
    }
};

// A window that has rendered a set of entities, without a platform window
// behind it: `AppInvalidate` is what a real one would answer, and a window
// with no `plat` swallows it, so what is checked is the set itself.
static Window* FakeWindow(App* app) {
    Window* win = new Window();
    win->app = app;
    VecAppend(app->windows, win);
    return win;
}

} // namespace

static void AnObserverHearsTheEntityThatNotified() {
    App app;
    Window* win = FakeWindow(&app);
    Entity<Model> model = EntityNewState<Model>(&app);
    Entity<Model> other = EntityNewState<Model>(&app);
    Entity<Watcher> watcher = EntityNew<Watcher>(&app);

    ObserveTo(&app, model, watcher, &Watcher::OnChanged);
    utassert(EntityObserverCount(&app, model.id) == 1);
    utassert(EntityObserverCount(&app, other.id) == 0);

    NotifyEntity(&app, model.id, win);
    utassert(watcher.Get(&app)->seen == 1);
    utassert(watcher.Get(&app)->last == model.id);

    // Nothing observes the other one, so nothing hears it.
    NotifyEntity(&app, other.id, win);
    utassert(watcher.Get(&app)->seen == 1);

    VecReset(app.windows);
    delete win;
    EntityDropAll(&app);
}

static void TwoObserversBothHearIt() {
    App app;
    Window* win = FakeWindow(&app);
    Entity<Model> model = EntityNewState<Model>(&app);
    Entity<Watcher> a = EntityNew<Watcher>(&app);
    Entity<Watcher> b = EntityNew<Watcher>(&app);

    ObserveTo(&app, model, a, &Watcher::OnChanged);
    ObserveTo(&app, model, b, &Watcher::OnChanged);
    utassert(EntityObserverCount(&app, model.id) == 2);

    NotifyEntity(&app, model.id, win);
    utassert(a.Get(&app)->seen == 1 && b.Get(&app)->seen == 1);

    VecReset(app.windows);
    delete win;
    EntityDropAll(&app);
}

static void AnObservationCanBeGivenUp() {
    App app;
    Window* win = FakeWindow(&app);
    Entity<Model> model = EntityNewState<Model>(&app);
    Entity<Watcher> watcher = EntityNew<Watcher>(&app);

    Subscription sub = ObserveTo(&app, model, watcher, &Watcher::OnChanged);
    NotifyEntity(&app, model.id, win);
    utassert(watcher.Get(&app)->seen == 1);

    EntityUnobserve(&app, sub);
    utassert(EntityObserverCount(&app, model.id) == 0);
    NotifyEntity(&app, model.id, win);
    utassert(watcher.Get(&app)->seen == 1);

    VecReset(app.windows);
    delete win;
    EntityDropAll(&app);
}

// An observer whose entity has gone is swept rather than called, the way a
// subscription whose subscriber went away is.
static void AnObserverThatWentAwayIsSwept() {
    App app;
    Window* win = FakeWindow(&app);
    Entity<Model> model = EntityNewState<Model>(&app);
    Entity<Watcher> watcher = EntityNew<Watcher>(&app);

    ObserveTo(&app, model, watcher, &Watcher::OnChanged);
    EntityDrop(&app, watcher.id);
    // Gone from the list itself, not just skipped by the count.
    utassert(len(app.observers) == 0);
    utassert(EntityObserverCount(&app, model.id) == 0);
    // And with the observed entity, as Rust drops its observers with it.
    Entity<Watcher> second = EntityNew<Watcher>(&app);
    ObserveTo(&app, model, second, &Watcher::OnChanged);
    utassert(len(app.observers) == 1);
    EntityDrop(&app, model.id);
    utassert(len(app.observers) == 0);
    // The notify still runs; there is simply nobody left to hear it.
    NotifyEntity(&app, model.id, win);

    VecReset(app.windows);
    delete win;
    EntityDropAll(&app);
}

// GPUI marks a window dirty only when the entity that notified is one of the
// views it rendered. `Window::rendered` is that set, filled by EntityRender.
static void ANotifyNamesTheWindowsThatRenderedIt() {
    App app;
    Window* a = FakeWindow(&app);
    Window* b = FakeWindow(&app);
    Entity<Watcher> onA = EntityNew<Watcher>(&app);
    Entity<Watcher> onB = EntityNew<Watcher>(&app);
    Arena* arena = ArenaNew();

    EntityRender(&app, a, arena, onA.id);
    EntityRender(&app, b, arena, onB.id);
    utassert(len(a->rendered) == 1 && a->rendered[0] == onA.id);
    utassert(len(b->rendered) == 1 && b->rendered[0] == onB.id);

    // A frame starts over: the set is this frame's, not every frame's.
    a->rendered.len = 0;
    EntityRender(&app, a, arena, onA.id);
    EntityRender(&app, a, arena, onB.id);
    utassert(len(a->rendered) == 2);

    ArenaDelete(arena);
    VecReset(app.windows);
    delete a;
    delete b;
    EntityDropAll(&app);
}

// A destructor may create entities, enough to move the slot array, and
// while it runs its own handle is already stale (Rust's EntityMap takes the
// entity out before dropping it).
struct Spawner {
    static App* app;
    static EntityId self;
    static bool sawSelf;
    ~Spawner() {
        sawSelf = EntityGet(app, self) != nullptr;
        for (int i = 0; i < 200; i++) {
            EntityNewState<Model>(app);
        }
    }
};
App* Spawner::app = nullptr;
EntityId Spawner::self = {};
bool Spawner::sawSelf = true;

static void ADestructorMayCreateEntities() {
    App app;
    Spawner::app = &app;
    Entity<Spawner> spawner = EntityNewState<Spawner>(&app);
    Entity<Model> other = EntityNewState<Model>(&app);
    other.Get(&app)->value = 7;
    Spawner::self = spawner.id;
    EntityDrop(&app, spawner.id);
    utassert(!Spawner::sawSelf);
    utassert(spawner.Get(&app) == nullptr);
    utassert(other.Get(&app) && other.Get(&app)->value == 7);
    EntityDropAll(&app);
}

void TestObservers() {
    TestSuite("observers");
    AnObserverHearsTheEntityThatNotified();
    TwoObserversBothHearIt();
    AnObservationCanBeGivenUp();
    AnObserverThatWentAwayIsSwept();
    ANotifyNamesTheWindowsThatRenderedIt();
    ADestructorMayCreateEntities();
}
