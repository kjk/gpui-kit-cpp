#ifndef GPUI_SYS_EXECUTOR_H_
#define GPUI_SYS_EXECUTOR_H_
/* The executor: work that runs somewhere other than here, and how it gets
   back.

   This is the port of GPUI's `BackgroundExecutor` / `ForegroundExecutor`
   pair, minus the futures. Rust spawns an `async` block and awaits inside it;
   here the two halves are spelled as callbacks:

     cx.background_spawn(work)          ExecSpawn(work, done)
     cx.spawn(|this, cx| ...)           ExecPost(f) / WindowPost(win, listener)
     Task<T> dropped                    ExecCancel(id)
     Timer::after(d).await              WindowSetTimeout(win, ms, listener)

   `sys/task.h` is the half that does read like the Rust: a coroutine whose
   `co_await BackgroundSpawn(work)` is this `ExecSpawn` underneath, so a
   sequence of steps is written as a sequence instead of as a job struct and a
   pair of callbacks. This file stays the layer under it, and is still the
   right thing to call directly for a single step that has nothing to await.

   The two rules GPUI has, this has:

   1. A worker never touches an entity, a window, or anything else the UI
      owns. It works on what it was handed and reports through the main
      thread.
   2. Everything the UI owns is touched on the main thread only, which is
      where `done` and everything posted with ExecPost runs.

   The main-thread queue is what makes rule 1 payable, and is modelled on
   SumatraPDF's `uitask::Post`: a lock, a list, and a nudge that gets the
   platform event loop to come back and look. Waking is the one part the
   platform owns — a posted Win32 message, a byte down a pipe, a block on the
   main dispatch queue — so the loop installs it with ExecSetWake and nothing
   here names an OS API.

   Timers stay where they are. GPUI has no timer list because it spawns a task
   per timer that sleeps and cancels with the entity that owns it;
   `WindowSetInterval` / `WindowSetTimeout` in gpui.h are that, already, and
   folding them in here would make them outlive the window they belong to. */

#include "base.h"

namespace gpui {

// What a spawned job answers to, so it can be called off. 0 is no job — what
// a spawn that could not start returns, and what ExecCancel ignores. GPUI
// spells this the `Task<T>` handle, where dropping the handle is the cancel;
// this tree hands out integer handles for the same reason `WindowSetInterval`
// does — nothing here is destroyed by leaving a scope.
using TaskId = int;

// ─── the main thread ──────────────────────────────────────────────────────

// Remembers the calling thread as the main one. AppNew calls it; a test that
// uses the executor without an App calls it itself. Idempotent, and calling
// it a second time from a different thread is how a test moves the main
// thread — nothing else should.
void ExecInit();
// Stops the pool, waits a moment for a job that is nearly done, and drops
// whatever is still queued without running it. AppFree calls it.
void ExecShutdown();

// Whether this is the thread ExecInit was called on. False before it was.
bool ExecOnMainThread();

// What ExecPost calls once something is queued, so an event loop that is
// blocked waiting for input comes back and drains it. The platform window
// installs it in PlatInit; without one, a post is still queued and still runs
// on the next pass the loop makes for another reason.
void ExecSetWake(Func0 wake);

// uitask::Post. Queue `f` to run on the main thread and wake the loop. Safe
// from any thread, including the main one. Captured state belongs in the
// Func0, for example `ExecPost(MkFunc0(Fn, self))`.
void ExecPost(Func0 f);
// uitask::PostOptimized. The same, except that on the main thread it runs `f`
// right here — faster, and a stack trace that says who asked.
void ExecPostNow(Func0 f);

// Runs what is queued and answers how many ran. The event loop calls it once
// a pass; nothing else needs to. Anything posted by a task running inside it
// waits for the next call rather than extending this one, which is the same
// rule WindowTimerTick follows. After 10 ms it stops, wakes the loop and
// leaves the rest first in line for the next call.
int ExecDrain();
// How many are waiting. An event loop about to block checks this: something
// queued is a reason not to.
int ExecQueued();

// ─── the pool ─────────────────────────────────────────────────────────────

// cx.background_spawn. `work` runs on a pool thread; when it returns, `done`
// is posted to the main thread. Either may be empty. Both usually close over
// the same heap job struct — `work` fills it in, `done` reads it and frees
// it, and nothing needs a lock because the two never run at once. The pool
// thread's temp arena is reset after `work`, so a result `done` reads must
// be on the heap, never a TempStr.
//
// Returns the handle to cancel it with, or 0 if the job could not be taken at
// all — the pool is shutting down, or there was no room to record it — and
// then neither half runs. A platform with no threads to give is not one of
// those: see ExecHasThreads.
TaskId ExecSpawn(Func0 work, Func0 done = Func0{});
// On a successful enqueue, exactly one of done or discard runs. Discard
// releases captures if cancellation or shutdown prevents completion. It
// may run on a worker and must not touch UI state.
TaskId ExecSpawn(Func0 work, Func0 done, Func0 discard);

// Dropping a `Task` in Rust cancels it. A job that has not started yet is
// dropped here and neither half of it runs; one that a worker already picked
// up is left to finish, and its `done` still posts — a thread cannot be
// stopped in the middle and pretending otherwise would leak whatever the job
// holds. True if the job was still in the queue. Its discard (if any)
// runs before returning.
bool ExecCancel(TaskId id);

// Jobs queued or running. Does not count a `done` that is waiting in the main
// queue; ExecQueued does.
int ExecPending();

// Blocks until nothing is pending and nothing is queued, draining the main
// queue as it goes, or until `timeoutMs` runs out. True if it got there.
// Shutdown and tests only: calling it from a handler stops the UI dead.
bool ExecWaitIdle(int timeoutMs);

// How many worker threads exist right now. The pool starts none and grows to
// kExecMaxWorkers as jobs arrive faster than they finish; a thread it starts
// lives until shutdown.
int ExecWorkerCount();

// Whether a spawned job runs somewhere other than the main thread. True on
// every hosted platform. False on a wasm page, which has no thread to give
// out: ExecSpawn then queues the job on the main thread's own queue, so the
// work still happens and `done` still lands where it always does, just later
// and on this thread.
//
// Answers true until a spawn has proved otherwise, since that is the moment
// the platform is asked. Anything that needs to know has spawned by then.
bool ExecHasThreads();

// Enough that a page of images decodes at once on any machine this runs on,
// few enough that a job which blocks on the network — which the fetcher's
// does, for up to fifteen seconds — cannot take the pool with it.
constexpr int kExecMaxWorkers = 8;

} // namespace gpui
#endif // GPUI_SYS_EXECUTOR_H_
