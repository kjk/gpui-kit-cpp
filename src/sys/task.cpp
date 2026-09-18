#include "sys/task.h"

namespace gpui {

// One live coroutine. POD, so a Vec can hold it.
struct TaskSlot {
    std::coroutine_handle<TaskPromise> handle;
    uint32_t gen;
    bool used;
};

static Vec<TaskSlot> gTasks;
static int gLiveTasks;

static TaskSlot* SlotOf(TaskHandle id) {
    if (!id.IsValid() || id.index >= len(gTasks)) {
        return nullptr;
    }
    TaskSlot& s = gTasks[id.index];
    if (!s.used || s.gen != id.gen) {
        return nullptr;
    }
    return &s;
}

// Gives the slot back without touching the frame. The generation moves on, so
// a handle the caller kept reads as stale rather than naming whoever takes the
// slot next.
static void ReleaseSlot(TaskSlot* s) {
    s->handle = {};
    s->used = false;
    s->gen++;
    if (s->gen == 0) {
        s->gen = 1;
    }
    gLiveTasks--;
}

// The slot, and the frame with it. Destroying a frame runs the destructors of
// everything the coroutine had live at its suspend point, which is the half a
// callback chain cannot do.
static void DropSlot(TaskSlot* s) {
    std::coroutine_handle<TaskPromise> h = s->handle;
    ReleaseSlot(s);
    if (h) {
        h.destroy();
    }
}

static TaskHandle RegisterTask(std::coroutine_handle<TaskPromise> h) {
    int index = -1;
    for (int i = 0; i < len(gTasks); i++) {
        if (!gTasks[i].used) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        TaskSlot fresh;
        fresh.handle = {};
        fresh.gen = 1;
        fresh.used = false;
        if (!VecAppend(gTasks, fresh)) {
            return {};
        }
        index = len(gTasks) - 1;
    }
    TaskSlot& s = gTasks[index];
    if (s.gen == 0) {
        s.gen = 1;
    }
    s.handle = h;
    s.used = true;
    gLiveTasks++;
    TaskHandle id;
    id.index = index;
    id.gen = s.gen;
    return id;
}

// The single resume path, and the reason this file exists. Everything that can
// wake a coroutine comes through here, so the owner check is written once
// rather than at the top of every continuation.
static void ResumeTask(TaskHandle id) {
    TaskSlot* s = SlotOf(id);
    if (!s) {
        return;
    }
    std::coroutine_handle<TaskPromise> h = s->handle;
    TaskPromise& promise = h.promise();
    promise.awaiting = false;
    // Cancelled while the work was in flight, or the entity that asked for it
    // is gone. Either way the continuation is dropped rather than run, which
    // is what dropping a Rust Task with its view does.
    if (promise.cancelled || !promise.guard.IsAlive()) {
        DropSlot(s);
        return;
    }
    // The slot stays registered across the resume: the handle names this
    // coroutine for its whole life, however many times it suspends. What ends
    // it is TaskFinal, when the body reaches its end.
    h.resume();
}

Task TaskPromise::get_return_object() {
    id = RegisterTask(std::coroutine_handle<TaskPromise>::from_promise(*this));
    Task t;
    t.id = id;
    return t;
}

bool TaskFinal::await_ready() noexcept {
    if (promise) {
        TaskSlot* s = SlotOf(promise->id);
        if (s) {
            ReleaseSlot(s);
        }
        promise->id = {};
    }
    // Never suspend: the frame is about to free itself, and the registry no
    // longer points at it.
    return true;
}

bool Task::IsRunning() const {
    return TaskLive(id);
}

bool TaskCancel(TaskHandle id) {
    TaskSlot* s = SlotOf(id);
    if (!s) {
        return false;
    }
    TaskPromise& promise = s->handle.promise();
    promise.cancelled = true;
    // A continuation is already on its way and carries a pointer into this
    // frame; it has to be the one to destroy it, once it has arrived. Without
    // one the frame is ours to drop now.
    if (!promise.awaiting) {
        DropSlot(s);
    }
    return true;
}

bool TaskLive(TaskHandle id) {
    return SlotOf(id) != nullptr;
}

int TaskCount() {
    return gLiveTasks;
}

int TaskCancelAll() {
    int dropped = 0;
    // Unconditional, unlike TaskCancel: ExecShutdown drops a queued `done`
    // without running it, so waiting for the continuation would wait forever
    // and leak the frame. The header says why this may only run once the pool
    // has stopped.
    for (int i = 0; i < len(gTasks); i++) {
        if (!gTasks[i].used) {
            continue;
        }
        DropSlot(&gTasks[i]);
        dropped++;
    }
    VecReset(gTasks);
    return dropped;
}

// ─── the awaitable ────────────────────────────────────────────────────────

// Posted to the main thread by the executor once `work` has run.
static void BackgroundDone(BackgroundSpawn* self) {
    // `self` points into the coroutine frame, so the handle is read out before
    // the resume: afterwards the frame may have finished and freed itself, and
    // after a drop it is gone outright.
    TaskHandle id = self->id;
    ResumeTask(id);
}

void BackgroundSpawn::await_suspend(std::coroutine_handle<TaskPromise> h) {
    waiting = h;
    TaskPromise& promise = h.promise();
    id = promise.id;
    if (!id.IsValid()) {
        // The frame could not be registered at all, so nothing can route a
        // continuation back to it. Resuming here would run the body with the
        // awaited result missing, so the frame is dropped instead.
        h.destroy();
        return;
    }
    promise.awaiting = true;
    if (!ExecSpawn(work, MkFunc0(BackgroundDone, this))) {
        // The pool refused it, so neither half will run and no continuation is
        // coming. Drop the frame the way a lost owner does.
        promise.awaiting = false;
        TaskSlot* s = SlotOf(id);
        if (s) {
            DropSlot(s);
        }
    }
}

} // namespace gpui
