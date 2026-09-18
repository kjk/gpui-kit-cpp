/* The executor: one queue the main thread drains, one pool of threads that
   fills it.

   Two locks, deliberately. `gMainLock` guards the queue of things to run on
   the main thread; `gPoolLock` guards the job list and the workers waiting on
   it. A worker holds neither while it runs a job, and posts its completion
   under `gMainLock` only — so a job that spawns another job, or posts twice,
   never waits on the lock it is already inside.

   Nothing here allocates while a lock is held except the queues themselves,
   and neither queue is ever handed a pointer it owns: what a job carries
   belongs to whoever spawned it, and `done` is where it gets freed. */

#include "sys/executor.h"

namespace gpui {

// ─── the main thread's queue ──────────────────────────────────────────────

// The callback already carries its one captured word, just like SumatraPDF's
// uitask task record. It stays POD so the queue can remain a Vec.
struct MainTask {
    Func0 f = {};
};

static Mutex gMainLock;
static Vec<MainTask> gMainQueue;
static Func0 gWake;
static uint64_t gMainThreadId = 0;
static bool gStopping = false;

// Defined with the pool below: ExecShutdown latches it, and a second App in
// the same process — which a test run is — has to be able to spawn again.
static void PoolRestart();

void ExecInit() {
    gMainLock.Lock();
    gMainThreadId = PlatThreadId();
    gStopping = false;
    gMainLock.Unlock();
    PoolRestart();
}

bool ExecOnMainThread() {
    gMainLock.Lock();
    uint64_t id = gMainThreadId;
    gMainLock.Unlock();
    return id != 0 && id == PlatThreadId();
}

void ExecSetWake(Func0 wake) {
    gMainLock.Lock();
    gWake = wake;
    gMainLock.Unlock();
}

void ExecPost(Func0 f) {
    if (!f.IsValid()) {
        return;
    }
    MainTask t = {f};
    gMainLock.Lock();
    if (gStopping) {
        // Past ExecShutdown there is nobody left to run it. Dropping it is
        // the honest answer: the loop is gone and the process is on its way
        // out, which is when the last worker reports in.
        gMainLock.Unlock();
        return;
    }
    VecAppend(gMainQueue, t);
    Func0 wake = gWake;
    gMainLock.Unlock();
    // Outside the lock: waking is an OS call, and the loop it wakes takes the
    // same lock to drain.
    wake.Call();
}

void ExecPostNow(Func0 f) {
    if (!f.IsValid()) {
        return;
    }
    if (ExecOnMainThread()) {
        f.Call();
        return;
    }
    ExecPost(f);
}

int ExecQueued() {
    gMainLock.Lock();
    int n = len(gMainQueue);
    gMainLock.Unlock();
    return n;
}

int ExecDrain() {
    if (!ExecOnMainThread()) {
        return 0;
    }
    // Take the whole queue and run it outside the lock: a task is free to
    // post, spawn, or open a window, and each of those wants the lock back.
    // What it posts lands in the fresh queue and runs on the next pass.
    gMainLock.Lock();
    // The buffer moves rather than copies: Vec's assignment is a deep copy
    // and this runs on every pass of the event loop.
    Vec<MainTask> batch;
    batch.els = gMainQueue.els;
    batch.len = len(gMainQueue);
    batch.cap = gMainQueue.cap;
    gMainQueue.els = nullptr;
    gMainQueue.len = 0;
    gMainQueue.cap = 0;
    gMainLock.Unlock();

    for (int i = 0; i < len(batch); i++) {
        batch[i].f.Call();
    }
    int n = len(batch);
    VecReset(batch);
    return n;
}

// ─── the pool ─────────────────────────────────────────────────────────────

struct Job {
    TaskId id = 0;
    Func0 work = {};
    Func0 done = {};
};

static Mutex gPoolLock;
static CondVar gPoolWake;
static Vec<Job> gJobs;
static TaskId gNextTaskId = 1;
static int gWorkers = 0;
static int gIdle = 0;
static int gRunning = 0;
static bool gPoolStop = false;

// An idle worker sleeps until it is woken and not a moment less. A timed wait
// would have every thread in the pool wake several times a second for the
// life of an application that is doing nothing, which is a laptop's battery
// spent on looking at an empty queue. ExecShutdown wakes them all, repeatedly,
// so nothing waits here forever.
constexpr int kWorkerWaitForever = -1;

static void PoolRestart() {
    gPoolLock.Lock();
    gPoolStop = false;
    gPoolLock.Unlock();
}

static void WorkerMain() {
    gPoolLock.Lock();
    for (;;) {
        if (gPoolStop) {
            break;
        }
        if (len(gJobs) == 0) {
            gIdle++;
            gPoolWake.Wait(&gPoolLock, kWorkerWaitForever);
            gIdle--;
            continue;
        }
        Job job = gJobs[0];
        for (int i = 1; i < len(gJobs); i++) {
            gJobs[i - 1] = gJobs[i];
        }
        gJobs.len--;
        gRunning++;
        gPoolLock.Unlock();

        job.work.Call();
        // Posted from here rather than by the caller, so that a job which
        // wants its result on the main thread cannot forget to.
        ExecPost(job.done);

        gPoolLock.Lock();
        gRunning--;
    }
    gWorkers--;
    gPoolLock.Unlock();
}

// Latched the first time PlatThreadRun refuses, and what ExecHasThreads
// reads. Guarded by gPoolLock, like everything else about the pool.
static bool gNoThreads = false;

// The fallback for a target with no thread to run a job on: wasm, where the
// page is one thread and PlatThreadRun always fails. GPUI has no background
// executor there either. Rather than drop the job, ExecSpawn hands it to the
// main thread's own queue, so it runs late instead of never and `done` still
// lands where it lands everywhere else — on the main thread, after the work.
// Rule 1 is not weakened by this: the job is written as if it were elsewhere,
// and this only changes where "elsewhere" turned out to be.
//
// It also covers the ordinary failure that used to drop a job on the floor: a
// host that would not give out a thread at all.
static void RunOnMainThread(void* arg) {
    TaskId id = (TaskId)(intptr_t)arg;
    Job job;
    bool found = false;
    gPoolLock.Lock();
    for (int i = 0; i < len(gJobs); i++) {
        if (gJobs[i].id != id) {
            continue;
        }
        job = gJobs[i];
        for (int j = i + 1; j < len(gJobs); j++) {
            gJobs[j - 1] = gJobs[j];
        }
        gJobs.len--;
        found = true;
        break;
    }
    gPoolLock.Unlock();
    if (!found) {
        // ExecCancel got here first.
        return;
    }
    job.work.Call();
    job.done.Call();
}

TaskId ExecSpawn(Func0 work, Func0 done) {
    if (!work.IsValid() && !done.IsValid()) {
        return 0;
    }
    gPoolLock.Lock();
    if (gPoolStop) {
        gPoolLock.Unlock();
        return 0;
    }
    Job job;
    job.id = gNextTaskId++;
    job.work = work;
    job.done = done;
    if (!VecAppend(gJobs, job)) {
        gPoolLock.Unlock();
        return 0;
    }
    // Grow only when there is nobody free to take this. A pool sized to the
    // machine up front would cost eight stacks in every app that never
    // fetches anything.
    bool needWorker = gIdle == 0 && gWorkers < kExecMaxWorkers;
    if (needWorker) {
        gWorkers++;
    }
    TaskId id = job.id;
    gPoolLock.Unlock();

    if (needWorker) {
        if (!PlatThreadRun(MkFunc0Void(WorkerMain))) {
            gPoolLock.Lock();
            gWorkers--;
            bool alone = gWorkers == 0;
            if (alone) {
                gNoThreads = true;
            }
            gPoolLock.Unlock();
            if (alone) {
                // Nothing will ever pick this up off the job list, so it goes
                // to the one queue that is definitely drained. It stays in
                // `gJobs` while it waits, which is what keeps ExecCancel
                // working and what ExecPending counts.
                ExecPost(MkFunc0(RunOnMainThread, (void*)(intptr_t)id));
                return id;
            }
        }
    }
    gPoolWake.WakeOne();
    return id;
}

bool ExecCancel(TaskId id) {
    if (id == 0) {
        return false;
    }
    bool found = false;
    gPoolLock.Lock();
    for (int i = 0; i < len(gJobs); i++) {
        if (gJobs[i].id != id) {
            continue;
        }
        for (int j = i + 1; j < len(gJobs); j++) {
            gJobs[j - 1] = gJobs[j];
        }
        gJobs.len--;
        found = true;
        break;
    }
    gPoolLock.Unlock();
    return found;
}

int ExecPending() {
    gPoolLock.Lock();
    int n = len(gJobs) + gRunning;
    gPoolLock.Unlock();
    return n;
}

bool ExecHasThreads() {
    gPoolLock.Lock();
    bool no = gNoThreads;
    gPoolLock.Unlock();
    return !no;
}

int ExecWorkerCount() {
    gPoolLock.Lock();
    int n = gWorkers;
    gPoolLock.Unlock();
    return n;
}

bool ExecWaitIdle(int timeoutMs) {
    for (int waited = 0;; waited += 1) {
        ExecDrain();
        if (ExecPending() == 0 && ExecQueued() == 0) {
            return true;
        }
        if (waited >= timeoutMs) {
            return false;
        }
        PlatSleepMs(1);
    }
}

void ExecShutdown() {
    // A job that is nearly there is worth the wait; one that is blocked on a
    // name that will not resolve has fifteen seconds of its own and nobody is
    // closing a window to watch that.
    ExecWaitIdle(500);

    gPoolLock.Lock();
    gPoolStop = true;
    VecReset(gJobs);
    gPoolLock.Unlock();
    gPoolWake.WakeAll();

    // Wait for the workers to leave their loop. Nothing joins them, so this
    // is the only thing that keeps a thread from running into a freed queue
    // as the process tears down.
    for (int waited = 0; waited < 500 && ExecWorkerCount() > 0; waited += 5) {
        PlatSleepMs(5);
        gPoolWake.WakeAll();
    }

    gMainLock.Lock();
    gStopping = true;
    VecReset(gMainQueue);
    gWake = Func0{};
    gMainThreadId = 0;
    gMainLock.Unlock();
}

} // namespace gpui
