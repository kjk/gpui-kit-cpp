/* src/sys/task.h: the coroutine that suspends on background work.

   What is worth pinning is not that a co_await resumes — the compiler does
   that — but the three ways a suspended frame can end, since each of them is
   a lifetime rule written by hand:

   - the body runs to completion and frees its own frame;
   - it is cancelled while the work is in flight, and the continuation that
     arrives afterwards drops it instead of resuming;
   - its owner goes away, which is what dropping a Rust Task with its entity
     does, and the guard is how this tree learns that. */

// The amalgam already exports the executor and the task registry, so this
// includes neither seam directly.
#include "Test.h"

using namespace gpui;

namespace {

struct Counters {
    int workRan = 0;
    int resumedAfterFirst = 0;
    int resumedAfterSecond = 0;
    int finished = 0;
    bool ownerAlive = true;
};

Counters gCounters;

void TaskWork(Counters* c) {
    c->workRan++;
}

bool OwnerAlive(void* user) {
    return ((Counters*)user)->ownerAlive;
}

// Two suspends in a row, which is the shape the whole design exists for: a
// sequence written as a sequence rather than as two job structs.
Task TwoSteps(TaskGuard guard, Counters* c) {
    (void)guard;
    co_await BackgroundSpawn(MkFunc0(TaskWork, c));
    c->resumedAfterFirst++;
    co_await BackgroundSpawn(MkFunc0(TaskWork, c));
    c->resumedAfterSecond++;
    c->finished++;
}

// Holds a destructor across the suspend, so a dropped frame can be seen to
// have run it. This is the part a callback chain cannot do for you.
struct Tracked {
    int* destroyed;
    ~Tracked() { (*destroyed)++; }
};

int gTrackedDestroyed = 0;

Task HoldsALocal(TaskGuard guard, Counters* c) {
    (void)guard;
    Tracked tracked{&gTrackedDestroyed};
    co_await BackgroundSpawn(MkFunc0(TaskWork, c));
    c->finished++;
}

// Cancels itself from inside its own body, which must not destroy the frame
// the body is executing in. The handle is read from a global the test fills
// once the task has suspended for the first time.
TaskHandle gSelfId;
int gAfterSelfCancel = 0;

Task CancelsItself(TaskGuard guard, Counters* c) {
    (void)guard;
    Tracked tracked{&gTrackedDestroyed};
    co_await BackgroundSpawn(MkFunc0(TaskWork, c));
    utassert(TaskCancel(gSelfId));
    // Still here: the cancel marked the task, and the frame with this local
    // is intact until the next suspend.
    gAfterSelfCancel++;
    utassert(gTrackedDestroyed == 0);
    co_await BackgroundSpawn(MkFunc0(TaskWork, c));
    c->finished++;
}

// Runs the executor to quiescence the way the suite's other async tests do.
// The wait is on the wall clock rather than a slice count: a worker thread on
// a machine busy building can go unscheduled for longer than the suite's
// usual allowance, and nothing here is timing-sensitive.
void Settle() {
    double deadline = TimeNow() + 30.0;
    while (!ExecWaitIdle(1000) && TimeNow() < deadline) {
    }
    ExecDrain();
}

void ACoroutineRunsItsStepsInOrderAndFreesItself() {
    gCounters = {};
    Task t = TwoSteps(TaskGuard{}, &gCounters);
    // It suspended at the first await, so it is registered.
    utassert(TaskLive(t.id) && TaskCount() == 1);
    Settle();
    utassert(gCounters.workRan == 2 && gCounters.resumedAfterFirst == 1 &&
             gCounters.resumedAfterSecond == 1 && gCounters.finished == 1);
    // The body ended, so the frame freed itself and the handle reads stale.
    utassert(!TaskLive(t.id) && TaskCount() == 0);
}

void CancellingASuspendedTaskDropsItsContinuation() {
    gCounters = {};
    gTrackedDestroyed = 0;
    Task t = HoldsALocal(TaskGuard{}, &gCounters);
    utassert(TaskLive(t.id) && TaskCount() == 1);

    // Cancelled while the work is in flight. The continuation still arrives —
    // a worker that has started cannot be stopped — and drops the frame.
    utassert(TaskCancel(t.id));
    Settle();
    utassert(gCounters.finished == 0 && TaskCount() == 0 && !TaskLive(t.id));
    // The local's destructor ran even though the body never reached its end.
    utassert(gTrackedDestroyed == 1);
    // Cancelling twice is not an error, and a stale handle names nothing.
    utassert(!TaskCancel(t.id));
}

void CancellingARunningTaskDropsItAtItsNextSuspend() {
    gCounters = {};
    gTrackedDestroyed = 0;
    gAfterSelfCancel = 0;
    Task t = CancelsItself(TaskGuard{}, &gCounters);
    gSelfId = t.id;
    Settle();
    // The line after the cancel ran; the second await dropped the frame
    // instead of spawning, so the work ran once and the body never finished.
    utassert(gAfterSelfCancel == 1 && gCounters.workRan == 1 &&
             gCounters.finished == 0);
    utassert(gTrackedDestroyed == 1 && TaskCount() == 0 && !TaskLive(t.id));
}

// TaskCancelAll is what AppFree runs after the executor stopped: a frame
// whose continuation ExecShutdown dropped would otherwise wait forever, so
// it is destroyed, locals and all. The work holds out until the test lets it
// go, which is the only way to be sure the shutdown gives up on it.
Mutex gHoldLock;
bool gHoldReleased = false;

void HeldWork(Counters* c) {
    for (;;) {
        gHoldLock.Lock();
        bool released = gHoldReleased;
        gHoldLock.Unlock();
        if (released) {
            break;
        }
        PlatSleepMs(1);
    }
    c->workRan++;
}

Task HoldsALocalUntilReleased(TaskGuard guard, Counters* c) {
    (void)guard;
    Tracked tracked{&gTrackedDestroyed};
    co_await BackgroundSpawn(MkFunc0(HeldWork, c));
    c->finished++;
}

void CancelAllDropsTheFramesShutdownOrphaned() {
    gCounters = {};
    gTrackedDestroyed = 0;
    gHoldReleased = false;
    Task t = HoldsALocalUntilReleased(TaskGuard{}, &gCounters);
    utassert(TaskCount() == 1);
    // Gives up on the held worker and stops taking posts.
    ExecShutdown();
    utassert(TaskCancelAll() == 1);
    utassert(TaskCount() == 0 && !TaskLive(t.id) && gTrackedDestroyed == 1);
    // Now the work finishes; its `done` meets the stopped executor and is
    // dropped, which is why the frame could be destroyed above. Wait for the
    // worker to leave before the next test restarts the pool.
    gHoldLock.Lock();
    gHoldReleased = true;
    gHoldLock.Unlock();
    while (ExecWorkerCount() > 0) {
        PlatSleepMs(5);
    }
    utassert(gCounters.workRan == 1 && gCounters.finished == 0);
    ExecInit();
}

void ADeadOwnerDropsTheContinuationWithoutCancelling() {
    gCounters = {};
    TaskGuard guard;
    guard.alive = OwnerAlive;
    guard.user = &gCounters;
    Task t = TwoSteps(guard, &gCounters);
    utassert(TaskLive(t.id));

    // The view goes away while the work is in flight. Nothing cancels the
    // task; the guard is what the resume path consults.
    gCounters.ownerAlive = false;
    Settle();
    utassert(gCounters.workRan == 1 && gCounters.resumedAfterFirst == 0 &&
             gCounters.finished == 0);
    utassert(TaskCount() == 0 && !TaskLive(t.id));
}

void AStaleHandleNamesNothingAfterItsSlotIsReused() {
    gCounters = {};
    Task first = HoldsALocal(TaskGuard{}, &gCounters);
    TaskHandle stale = first.id;
    Settle();
    utassert(!TaskLive(stale));

    // The next task takes the same slot; the generation is what keeps the old
    // handle from naming it.
    gCounters = {};
    Task second = HoldsALocal(TaskGuard{}, &gCounters);
    utassert(second.id.index == stale.index && second.id.gen != stale.gen);
    utassert(TaskLive(second.id) && !TaskLive(stale));
    utassert(!TaskCancel(stale) && TaskLive(second.id));
    Settle();
    utassert(TaskCount() == 0);
}

} // namespace

void TestTask() {
    TestSuite("task");
    ExecInit();
    ACoroutineRunsItsStepsInOrderAndFreesItself();
    CancellingASuspendedTaskDropsItsContinuation();
    CancellingARunningTaskDropsItAtItsNextSuspend();
    CancelAllDropsTheFramesShutdownOrphaned();
    ADeadOwnerDropsTheContinuationWithoutCancelling();
    AStaleHandleNamesNothingAfterItsSlotIsReused();
    utassert(TaskCount() == 0);
}
