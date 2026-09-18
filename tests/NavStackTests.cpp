/* Ported from crates/base/src/nav_stack.rs.
 *
 * Rust's views are AnyView; here a view is the EntityId of an entity with a
 * Render, which is what the stack keeps and what NavStack renders. The last
 * two tests drive the element itself: a frame is one IntoEl against a window
 * whose clock the test moves, which is what `window.draw(cx)` plus
 * `executor().advance_clock(..)` do over there. */

#include "Test.h"

namespace {

struct NavTestPage {
    static El* Render(NavTestPage*, Ctx* cx) { return Div(cx->a); }
};

struct NavTestEvents {
    Vec<int> events;

    static void OnEvent(NavTestEvents* self, Ctx*, const NavStackEvent* ev) {
        VecAppend(self->events, (int)*ev);
    }
};

// App, one window and one arena: everything a Ctx needs, with no platform
// window behind it. animFrame is pre-set so a running transition's request for
// the next frame does not arm a timer on a window that has no hwnd.
struct NavFixture {
    App app;
    Window* win = nullptr;
    Arena* a = nullptr;
    Ctx cx = {};
    Entity<NavStackState> stack = {};
    Entity<NavTestEvents> events = {};
    bool wasReduced = false;

    NavFixture() {
        win = new Window();
        win->app = &app;
        win->animFrame = true;
        a = ArenaNew();
        cx = {&app, win, a, {}};
        wasReduced = MotionReduced();
        // A transition that is skipped for accessibility would make every
        // change immediate, which is the one thing these tests are not about.
        MotionSetReduced(false);
        stack = NavStackStateNew(&app);
        events = EntityNewState<NavTestEvents>(&app);
        SubscribeTo(&app, stack, events, &NavTestEvents::OnEvent);
    }

    ~NavFixture() {
        MotionSetReduced(wasReduced);
        WindowMotionFree(win);
        EntityDropAll(&app);
        ArenaDelete(a);
        delete win;
    }

    NavStackState* State() { return stack.Get(&app); }

    EntityId Page() { return EntityNew<NavTestPage>(&app).id; }

    const Vec<int>& Events() { return events.Get(&app)->events; }
};

EntityId NavPush(NavFixture& f, EntityId view, NavMotion motion) {
    NavStackPush(f.State(), &f.cx, view, motion);
    return view;
}

} // namespace

static void PushAndPopKeepTheRoot() {
    NavFixture f;
    EntityId root = f.Page();
    EntityId second = f.Page();

    NavPush(f, root, NavMotion::Animated);
    utassert(!f.State()->hasTransit);

    NavPush(f, second, NavMotion::Animated);
    utassert(f.State()->Depth() == 2);
    utassert(f.State()->Current() == second);
    utassert(f.State()->hasTransit); // a push over a view transitions
    utassert(f.State()->transit.operation == NavOperation::Push);
    utassert(f.State()->transit.outgoing == root);

    EntityId popped = NavStackPop(f.State(), &f.cx, NavMotion::Animated);
    utassert(popped == second);
    utassert(f.State()->Depth() == 1);
    utassert(f.State()->ViewAt(0) == root);
    utassert(f.State()->hasTransit); // pop transitions
    utassert(f.State()->transit.operation == NavOperation::Pop);
    utassert(f.State()->transit.outgoing == second);
    // The popped view keeps its position.
    utassert(f.State()->transit.index == 1);

    utassert(!NavStackPop(f.State(), &f.cx, NavMotion::Animated).IsValid());
    utassert(f.State()->Depth() == 1);

    const Vec<int>& events = f.Events();
    utassert(len(events) == 3);
    utassert(events[0] == (int)NavStackEvent::Pushed);
    utassert(events[1] == (int)NavStackEvent::Pushed);
    utassert(events[2] == (int)NavStackEvent::Popped);
}

static void PopToRootReturnsEverythingAboveIt() {
    NavFixture f;
    EntityId pages[3];
    for (int i = 0; i < 3; i++) {
        pages[i] = NavPush(f, f.Page(), NavMotion::Animated);
    }

    Vec<EntityId> popped =
        NavStackPopToRoot(f.State(), &f.cx, NavMotion::Animated);
    utassert(len(popped) == 2);
    utassert(popped[0] == pages[1]);
    utassert(popped[1] == pages[2]);

    utassert(f.State()->Depth() == 1);
    utassert(f.State()->ViewAt(0) == pages[0]);
    utassert(f.State()->hasTransit); // pop_to_root transitions
    utassert(f.State()->transit.outgoing == pages[2]);
    // The previous top keeps its position.
    utassert(f.State()->transit.index == 2);

    utassert(NavStackPopToRoot(f.State(), &f.cx, NavMotion::Animated).len == 0);
}

static void ReplaceSwapsTheTopAndPushesIntoAnEmptyStack() {
    NavFixture f;
    EntityId first = f.Page();
    EntityId second = f.Page();

    utassert(!NavStackReplace(f.State(), &f.cx, first, NavMotion::Animated)
                  .IsValid());
    utassert(NavStackReplace(f.State(), &f.cx, second, NavMotion::Animated) ==
             first);

    utassert(f.State()->Depth() == 1);
    utassert(f.State()->ViewAt(0) == second);
    utassert(f.State()->hasTransit); // replace transitions
    utassert(f.State()->transit.operation == NavOperation::Replace);
    utassert(f.State()->transit.outgoing == first);
    // The replaced view sat where the new one sits.
    utassert(f.State()->transit.index == 0);

    NavStackClear(f.State(), &f.cx);
    utassert(f.State()->IsEmpty());
    utassert(!f.State()->hasTransit);

    const Vec<int>& events = f.Events();
    utassert(len(events) == 3);
    utassert(events[0] == (int)NavStackEvent::Pushed);
    utassert(events[1] == (int)NavStackEvent::Replaced);
    utassert(events[2] == (int)NavStackEvent::Cleared);
}

static void PoppedViewsWaitForForwardUntilTheNextPush() {
    NavFixture f;
    EntityId pages[3];
    for (int i = 0; i < 3; i++) {
        pages[i] = NavPush(f, f.Page(), NavMotion::Animated);
    }
    utassert(!NavStackForward(f.State(), &f.cx, NavMotion::Animated).IsValid());

    NavStackPop(f.State(), &f.cx, NavMotion::Animated);
    NavStackPop(f.State(), &f.cx, NavMotion::Animated);
    utassert(f.State()->Depth() == 1);
    utassert(f.State()->ForwardCount() == 2);
    utassert(f.State()->ForwardViewAt(0) == pages[1]);
    utassert(f.State()->ForwardViewAt(1) == pages[2]);

    EntityId broughtBack =
        NavStackForward(f.State(), &f.cx, NavMotion::Animated);
    utassert(broughtBack == pages[1]);
    utassert(f.State()->Current() == pages[1]);
    utassert(f.State()->hasTransit); // forward transitions like a push
    utassert(f.State()->transit.operation == NavOperation::Push);
    utassert(f.State()->transit.outgoing == pages[0]);
    utassert(f.State()->ForwardCount() == 1);

    NavPush(f, f.Page(), NavMotion::Animated);
    utassert(f.State()->ForwardCount() == 0);

    const Vec<int>& events = f.Events();
    utassert(events[len(events) - 1] == (int)NavStackEvent::Pushed);
    bool forwarded = false;
    for (int i = 0; i < len(events); i++) {
        forwarded = forwarded || events[i] == (int)NavStackEvent::Forwarded;
    }
    utassert(forwarded);
}

static void AnImmediateChangeRecordsItsMotionAndSupersedesTheRunningOne() {
    NavFixture f;
    NavPush(f, f.Page(), NavMotion::Animated);
    EntityId second = NavPush(f, f.Page(), NavMotion::Animated);
    EntityId third = NavPush(f, f.Page(), NavMotion::Immediate);

    utassert(f.State()->Current() == third);
    utassert(f.State()->hasTransit); // the change is recorded
    utassert(f.State()->transit.motion == NavMotion::Immediate);
    // The running push was superseded.
    utassert(f.State()->transit.outgoing == second);

    utassert(NavStackPop(f.State(), &f.cx, NavMotion::Immediate) == third);
    utassert(f.State()->transit.motion == NavMotion::Immediate);
    utassert(f.State()->transit.outgoing == third);
    utassert(f.Events().len == 4);
}

static void ANewOperationReplacesTheRunningTransition() {
    NavFixture f;
    EntityId pages[3];
    for (int i = 0; i < 3; i++) {
        pages[i] = NavPush(f, f.Page(), NavMotion::Animated);
    }
    NavStackPop(f.State(), &f.cx, NavMotion::Animated);
    utassert(f.State()->transit.operation == NavOperation::Pop);
    utassert(f.State()->transit.outgoing == pages[2]);
}

// window.draw(cx).clear(cx): one frame of the host, at the clock the test says.
static void NavDrawFrame(NavFixture& f, double atSeconds) {
    f.win->frameNow = atSeconds;
    f.win->frameSeq++;
    NavStack::New(&f.cx, f.stack)
        ->Transition(motion::Transition::New(200))
        ->IntoEl();
    WindowMotionSweep(f.win);
    f.a->Reset();
}

static void TheOutgoingViewIsDroppedOnceItsExitHasRun() {
    NavFixture f;
    double t0 = 100.0;
    f.win->frameNow = t0;
    NavPush(f, f.Page(), NavMotion::Immediate);
    NavPush(f, f.Page(), NavMotion::Animated);

    NavDrawFrame(f, t0);
    utassert(f.State()->hasTransit);

    NavDrawFrame(f, t0 + 0.100);
    utassert(f.State()->hasTransit);

    NavDrawFrame(f, t0 + 0.250);
    utassert(!f.State()->hasTransit);

    // An immediate change is gone after the frame that draws it.
    NavStackPop(f.State(), &f.cx, NavMotion::Immediate);
    NavDrawFrame(f, t0 + 0.260);
    utassert(!f.State()->hasTransit);
}

// The paint order and the progress both pages of a change read, which is what
// the item renderer is handed. Rust has no test for this; the C++ item is a
// function rather than a closure, so it is cheap to check here.
static Vec<NavPage> gNavSeen;

static El* NavRecordItem(void*, Ctx*, const NavPage& page) {
    VecAppend(gNavSeen, page);
    return page.el;
}

static void TheItemRendererSeesBothPagesOfAChange() {
    NavFixture f;
    double t0 = 500.0;
    f.win->frameNow = t0;
    EntityId root = NavPush(f, f.Page(), NavMotion::Immediate);
    // The root settles on a frame of its own first, the way it would in an
    // application: an exit reads its presence back from present, so the
    // change starts at zero rather than already over.
    NavDrawFrame(f, t0);
    EntityId second = NavPush(f, f.Page(), NavMotion::Animated);

    VecClear(gNavSeen);
    f.win->frameSeq++;
    NavStack::New(&f.cx, f.stack)
        ->Transition(motion::Transition::New(200))
        ->Item(&NavRecordItem)
        ->IntoEl();

    // A pushed view paints over what it covers: the outgoing page first.
    utassert(len(gNavSeen) == 2);
    utassert(gNavSeen[0].view == root);
    utassert(gNavSeen[0].Phase() == PresencePhase::Exiting);
    utassert(gNavSeen[0].Index() == 0);
    utassert(gNavSeen[1].view == second);
    utassert(gNavSeen[1].Phase() == PresencePhase::Entering);
    utassert(gNavSeen[1].Index() == 1);
    // Both read one clock, and a change starts at zero.
    utassert(gNavSeen[0].HasOperation());
    utassert(gNavSeen[0].Operation() == NavOperation::Push);
    utassert(TestNear(gNavSeen[0].Progress(), gNavSeen[1].Progress()));
    utassert(TestNear(gNavSeen[0].Progress(), 0.f));
    f.a->Reset();
    VecReset(gNavSeen);
}

void TestNavStack() {
    TestSuite("nav_stack");
    PushAndPopKeepTheRoot();
    PopToRootReturnsEverythingAboveIt();
    ReplaceSwapsTheTopAndPushesIntoAnEmptyStack();
    PoppedViewsWaitForForwardUntilTheNextPush();
    AnImmediateChangeRecordsItsMotionAndSupersedesTheRunningOne();
    ANewOperationReplacesTheRunningTransition();
    TheOutgoingViewIsDroppedOnceItsExitHasRun();
    TheItemRendererSeesBothPagesOfAChange();
}
