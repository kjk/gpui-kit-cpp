/* sys/executor.h: the main-thread queue and the pool behind it.

   Not a port — GPUI's own executor tests drive a deterministic single-thread
   executor through `run_until_parked` and `advance_clock`, and there is no
   counterpart here. What is checked instead is what this tree's version
   promises: that a post runs on the main thread and only when the queue is
   drained, that it runs in the order it was posted, that a spawned job runs
   somewhere else and reports back through that queue, and that a job which
   has not started can be called off.

   Nothing here sleeps for a fixed time and hopes. The pool is driven to a
   known state — every worker held inside a job — and then released, so each
   assertion is about what happened rather than about how long it took. */

#include "Test.h"

// Everything a worker touches goes through this lock: base has no atomics,
// and the executor is the thing under test rather than the thing to lean on.
static Mutex gLock;
static int gRan = 0;
static int gDone = 0;
static int gOrder[8] = {};
static int gOrderN = 0;
static uint64_t gWorkerThread = 0;
static bool gRelease = false;

static void Reset() {
    gLock.Lock();
    gRan = 0;
    gDone = 0;
    gOrderN = 0;
    gWorkerThread = 0;
    gRelease = false;
    gLock.Unlock();
}

static void Bump() {
    gLock.Lock();
    gRan++;
    gLock.Unlock();
}

// Runs on the main thread only, so it needs no lock.
static void MarkDone() {
    gDone++;
}

static void Note1() {
    gOrder[gOrderN++] = 1;
}
static void Note2() {
    gOrder[gOrderN++] = 2;
}
static void Note3() {
    gOrder[gOrderN++] = 3;
}

static void SawCapturedArg(int* box) {
    *box = 42;
}

static void PostBump() {
    ExecPost(MkFunc0Void(Bump));
}

static void APostRunsOnTheMainThreadAndOnlyWhenDrained() {
    Reset();
    utassert(ExecOnMainThread());

    // uitask::Post: queued, and nothing has run yet.
    ExecPost(MkFunc0Void(Bump));
    utassert(gRan == 0);
    utassert(ExecQueued() == 1);
    utassert(ExecDrain() == 1);
    utassert(gRan == 1);
    utassert(ExecQueued() == 0);
    // Draining an empty queue is not an error and runs nothing.
    utassert(ExecDrain() == 0);

    // uitask::PostOptimized: already on the main thread, so it runs here.
    ExecPostNow(MkFunc0Void(Bump));
    utassert(gRan == 2);
    utassert(ExecQueued() == 0);
}

static void PostsRunInTheOrderTheyWereMade() {
    Reset();
    // Each queued task is a Func0, and a no-argument callback needs no dummy
    // argument word.
    ExecPost(MkFunc0Void(Note1));
    ExecPost(MkFunc0Void(Note2));
    ExecPost(MkFunc0Void(Note3));
    utassert(ExecDrain() == 3);
    utassert(gOrderN == 3);
    utassert(gOrder[0] == 1 && gOrder[1] == 2 && gOrder[2] == 3);
}

static void APostCarriesCapturedState() {
    int box = 0;
    ExecPost(MkFunc0(SawCapturedArg, &box));
    utassert(box == 0);
    utassert(ExecDrain() == 1);
    utassert(box == 42);
}

// A post made from inside a drain belongs to the next pass rather than
// extending this one, which is the rule WindowTimerTick follows too.
static void APostMadeWhileDrainingWaitsForTheNextPass() {
    Reset();
    ExecPost(MkFunc0Void(PostBump));
    utassert(ExecDrain() == 1);
    utassert(gRan == 0);
    utassert(ExecQueued() == 1);
    utassert(ExecDrain() == 1);
    utassert(gRan == 1);
}

static void SpawnedWork() {
    gLock.Lock();
    gWorkerThread = PlatThreadId();
    gRan++;
    gLock.Unlock();
}

static void ASpawnRunsElsewhereAndReportsBack() {
    Reset();
    TaskId id = ExecSpawn(MkFunc0Void(SpawnedWork), MkFunc0Void(MarkDone));
    utassert(id != 0);
    utassert(ExecWaitIdle(5000));
    utassert(gRan == 1);
    // Rule 1: a worker never touches what the UI owns. This is the only place
    // that says out loud that it really was another thread — where there is
    // one. A wasm page has none, and the job runs on the main thread's queue
    // instead; rule 1 still holds, because the job is written as if it were
    // elsewhere.
    if (ExecHasThreads()) {
        utassert(gWorkerThread != 0 && gWorkerThread != PlatThreadId());
    } else {
        utassert(gWorkerThread == PlatThreadId());
    }
    // Rule 2: the completion came back here. ExecWaitIdle drains as it waits,
    // and a `done` that had not run would have left the queue behind.
    utassert(gDone == 1);
    utassert(ExecQueued() == 0);
    utassert(ExecPending() == 0);
}

static void EveryJobRuns() {
    Reset();
    for (int i = 0; i < 32; i++) {
        utassert(ExecSpawn(MkFunc0Void(Bump), MkFunc0Void(MarkDone)) != 0);
    }
    utassert(ExecWaitIdle(5000));
    utassert(gRan == 32);
    utassert(gDone == 32);
    // The pool grows to meet the work and no further, where there is a pool.
    utassert(!ExecHasThreads() || ExecWorkerCount() > 0);
    utassert(ExecWorkerCount() <= kExecMaxWorkers);
}

// Holds a worker until the main thread lets go. The bound is a backstop: a
// test that hangs is worse than a test that fails.
static void HoldWorker() {
    gLock.Lock();
    gRan++;
    gLock.Unlock();
    for (int waited = 0; waited < 5000; waited += 5) {
        gLock.Lock();
        bool go = gRelease;
        gLock.Unlock();
        if (go) {
            return;
        }
        PlatSleepMs(5);
    }
}

static void AJobThatHasNotStartedCanBeCalledOff() {
    Reset();
    // Fill every worker the pool is allowed to have, so the next job in has
    // nobody free to pick it up and is certain to still be in the queue.
    for (int i = 0; i < kExecMaxWorkers; i++) {
        utassert(ExecSpawn(MkFunc0Void(HoldWorker)) != 0);
    }
    TaskId victim = ExecSpawn(MkFunc0Void(Bump), MkFunc0Void(MarkDone));
    utassert(victim != 0);
    utassert(ExecCancel(victim));
    // Cancelled twice is not cancelled twice, and neither is a handle nobody
    // was ever given.
    utassert(!ExecCancel(victim));
    utassert(!ExecCancel(0));
    utassert(!ExecCancel(victim + 1000));

    gLock.Lock();
    gRelease = true;
    gLock.Unlock();
    utassert(ExecWaitIdle(10000));
    // Each held job bumped once as it started; the cancelled one never ran,
    // and neither did the completion it would have posted.
    utassert(gRan == kExecMaxWorkers);
    utassert(gDone == 0);
}

// A test app frees like any App, and freeing one stops the executor; the
// suites after it run without an App of their own and still post and drain,
// so TestAppFree leaves the executor running as TestAppNew found it.
static void ATestAppLeavesTheExecutorAsItFoundIt() {
    App* app = TestAppNew();
    TestAppFree(app);
    Reset();
    ExecPost(MkFunc0Void(Bump));
    utassert(ExecDrain() == 1);
    utassert(gRan == 1);
}

void TestExecutor() {
    TestSuite("executor");
    // The suite has no App, so this is where the main thread is named.
    ExecInit();

    APostRunsOnTheMainThreadAndOnlyWhenDrained();
    PostsRunInTheOrderTheyWereMade();
    APostCarriesCapturedState();
    APostMadeWhileDrainingWaitsForTheNextPass();
    ASpawnRunsElsewhereAndReportsBack();
    EveryJobRuns();
    AJobThatHasNotStartedCanBeCalledOff();

    // Leave nothing running behind the rest of the suite, and show that the
    // pool comes back after a shutdown — AppNew following AppFree is that.
    ExecShutdown();
    utassert(ExecWorkerCount() == 0);
    utassert(ExecPending() == 0);
    ExecInit();
    Reset();
    utassert(ExecSpawn(MkFunc0Void(Bump)) != 0);
    utassert(ExecWaitIdle(5000));
    utassert(gRan == 1);
    ExecShutdown();
    ExecInit();
    ATestAppLeavesTheExecutorAsItFoundIt();
}
