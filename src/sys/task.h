#ifndef GPUI_SYS_TASK_H_
#define GPUI_SYS_TASK_H_
/* A coroutine that suspends on background work, so a sequence of it reads as
   a sequence.

   This is the smallest thing that makes the port closer to GPUI. Rust spawns
   an `async` block and writes `.await` between the steps; without coroutines
   the same sequence is a job struct, a `work` callback, a `done` callback,
   and — the part that actually costs — a preamble at the top of `done` that
   proves the thing which asked for the work is still there before touching
   anything it owns. `src/shell/runtime.cpp` writes that preamble five times.

     let value = cx.background_spawn(work).await;
     co_await BackgroundSpawn(work);

   What this owns is that preamble. A coroutine registered here carries a
   `TaskGuard`, and the one resume path checks it: if the guard says the owner
   is gone, the continuation is dropped and the frame is destroyed with its
   locals, which is what dropping a Rust `Task` with its entity does.

   Three things it deliberately is not:

   - Not a general async runtime. There is one awaitable — background work —
     and no combinators, no `select`, no cancellation token. Anything that
     needs those wants a bigger reason than a tidier call site.
   - Not `Task<T>`. A coroutine here returns nothing; results live in the
     coroutine's own locals, which is where a job struct's fields were going
     anyway, and they stay alive across the suspend for free.
   - Not scope-cancelled. Rust's `Task` cancels when the view drops it, but a
     view here is an entity with a generation, not a C++ scope, so a `Task`
     that cancelled at the end of the spawning function would cancel every
     one of them. `Task` is an id — the same vocabulary `ExecSpawn` and
     `WindowSetInterval` already hand out — and `TaskCancel` is the drop.

   `<coroutine>` is the one standard header outside the usual list, and hard
   rule 1 in AGENTS.md names it for this reason: it is compiler support rather
   than a library, in the same family as `<new>` — no container, no allocator,
   no exceptions — and the compiler will not accept a hand-written substitute.
   Verified building and running under this tree's own flags on MSVC,
   clang-cl, g++, clang++ with libstdc++, Apple clang with libc++, and
   emscripten.

   Layering: this is `src/sys`, so it knows the executor and nothing above it.
   Entities, windows and policies reach it through `TaskGuard`, the way the
   platform reaches the executor through `ExecSetWake`. A timer awaitable
   belongs in `src/gpui` for the same reason — `WindowSetTimeout` is a window's
   and cancels with it.

   Writing one:

     static Task LintDocument(TaskGuard guard, Doc* doc) {
         LintJob job{doc};
         co_await BackgroundSpawn(MkFunc0(LintWork, &job));
         // Back on the main thread, and `guard` said the owner is still here.
         doc->diagnostics = job.result;
     }

   The guard is the coroutine's first parameter because the promise is built
   from the coroutine's own arguments, and the frame runs to its first suspend
   before the caller sees the `Task` — there is no moment afterwards in which
   to set it. A coroutine with no owner to outlive takes `TaskGuard{}`, which
   is always alive. */

#include "base.h"
#include "sys/executor.h"

#include <coroutine>

namespace gpui {

// Whether the thing that asked for the work is still there. Rust holds a weak
// entity handle and upgrades it on resume; this is that upgrade, supplied by
// whichever layer knows what an owner is. A default-constructed guard has no
// owner and is always alive.
struct TaskGuard {
    bool (*alive)(void* user) = nullptr;
    void* user = nullptr;

    bool IsAlive() const { return !alive || alive(user); }
};

// What a coroutine registered here answers to. A null handle is no task: what
// a coroutine that already finished leaves behind, and what TaskCancel
// ignores. Generational like `EntityId` rather than a bare int like the
// executor's `TaskId`, because a slot is recycled and a stale handle must read
// back as gone instead of naming its successor.
struct TaskHandle {
    int32_t index = -1;
    uint32_t gen = 0; // 0 == no task

    bool IsValid() const { return index >= 0 && gen != 0; }
};

inline bool operator==(TaskHandle a, TaskHandle b) {
    return a.index == b.index && a.gen == b.gen;
}
inline bool operator!=(TaskHandle a, TaskHandle b) {
    return !(a == b);
}

struct TaskPromise;

// The return object. A token, not an owner: the frame belongs to the registry
// from the moment it is created until the one resume path destroys it.
struct Task {
    using promise_type = TaskPromise;

    TaskHandle id;

    // True while the coroutine is suspended and its owner is still alive.
    bool IsRunning() const;
};

// ─── the registry ─────────────────────────────────────────────────────────

// Marks the task cancelled. The continuation it is waiting for still arrives —
// a worker that already started cannot be stopped, which is what ExecCancel
// says too — and drops itself when it does, destroying the frame and its
// locals. A task whose body is executing right now (one cancelling itself, or
// another task it resumed) is never destroyed from under its own code: it is
// marked, and dropped at its next suspend. Answers false for a task that had
// already finished.
bool TaskCancel(TaskHandle id);

// Whether `id` names a task that is still suspended. False once it has run to
// completion, been cancelled, or lost its owner.
bool TaskLive(TaskHandle id);

// How many are suspended. Tests and shutdown assertions.
int TaskCount();

// Destroys every registered frame, whether or not it is waiting on something.
// Answers how many.
//
// **Only after the executor has stopped.** ExecShutdown drops what is still
// queued without running it, so those continuations never arrive and their
// frames would leak; this is what collects them. Calling it while the pool can
// still deliver one destroys a frame that the delivery is about to touch.
// AppFree's order — ExecShutdown, then this — is the supported one.
int TaskCancelAll();

// ─── the promise ──────────────────────────────────────────────────────────

// Gives the slot back as the body ends. Never suspends: the frame frees
// itself immediately afterwards, so what this has to do is make sure the
// registry is not still pointing at it when it does.
struct TaskFinal {
    TaskPromise* promise = nullptr;

    bool await_ready() noexcept;
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
};

struct TaskPromise {
    TaskGuard guard;
    TaskHandle id;
    // Set while a continuation is outstanding, so the registry knows a resume
    // is still coming for this frame and must not destroy it from under one.
    bool awaiting = false;
    // Set while the body is on the stack — the eager first run and every
    // resume — so a cancel that reaches it marks instead of destroying.
    bool running = false;
    bool cancelled = false;

    TaskPromise() = default;
    // The promise is constructed from the coroutine's own arguments, which is
    // the only hook that runs before the body does.
    template <typename... Rest>
    explicit TaskPromise(TaskGuard g, Rest&&...) : guard(g) {}

    // Registers the frame, so the handle the caller receives already names it.
    // This runs before the body does, which is the only ordering that gives
    // the caller a usable handle: `initial_suspend` is `suspend_never`, so by
    // the time the caller has its `Task` the body has already reached its
    // first await.
    Task get_return_object();

    // Runs eagerly to the first suspend, the way `cx.spawn`'s block does.
    std::suspend_never initial_suspend() const noexcept { return {}; }
    // The frame frees itself when the body ends, so a completed task leaves
    // no handle for anyone to dangle on. TaskFinal releases the slot first.
    TaskFinal final_suspend() noexcept { return TaskFinal{this}; }
    void return_void() const noexcept {}
    // Exceptions are off tree-wide (/EHs-c-, -fno-exceptions), so this cannot
    // be reached.
    void unhandled_exception() const noexcept {}
};

// ─── the one awaitable ────────────────────────────────────────────────────

// cx.background_spawn(work).await. `work` runs on the executor pool; the
// coroutine resumes on the main thread, and only if its guard still says the
// owner is there.
//
// The awaitable lives in the coroutine frame for the length of the suspend,
// so the executor's `done` carries a pointer to it and nothing is allocated
// for the wait itself.
struct BackgroundSpawn {
    Func0 work;
    std::coroutine_handle<TaskPromise> waiting{};
    TaskHandle id;

    explicit BackgroundSpawn(Func0 w) : work(w) {}

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<TaskPromise> h);
    void await_resume() const noexcept {}
};

} // namespace gpui
#endif // GPUI_SYS_TASK_H_
