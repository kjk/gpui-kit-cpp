/* The portable half of the directory watcher: the table of watches and the
   one-slot channel between the OS thread that sees an event and the main
   thread that acts on it. See sys/dir_watch.h. */

#include "sys/dir_watch.h"

#include "sys/executor.h"

namespace gpui {

struct DirWatchEntry {
    DirWatchId id = 0;
    Func0 onChange = {};
    DirWatchPlat* plat = nullptr;
    // The one slot of Rust's `smol::channel::bounded(1)`: set when a call is
    // posted, cleared when it starts. While it is set a signal is dropped,
    // the way `try_send` on a full channel is.
    bool queued = false;
};

// Signals arrive from OS threads, so the table is behind a lock. Nothing is
// called with it held: a callback may stop its own watch or start another.
static Mutex gDirWatchLock;
static Vec<DirWatchEntry> gDirWatches;
static DirWatchId gDirWatchNext = 1;

static int DirWatchIndex(DirWatchId id) {
    for (int i = 0; i < len(gDirWatches); i++) {
        if (gDirWatches[i].id == id) {
            return i;
        }
    }
    return -1;
}

const char* DirWatchErrorName(DirWatchError err) {
    switch (err) {
        case DirWatchError::None:
            return "no error";
        case DirWatchError::Unsupported:
            return "not supported on this platform";
        case DirWatchError::Failed:
            return "the folder could not be watched";
        case DirWatchError::Limit:
            return "the file watch limit was reached";
    }
    return "unknown error";
}

DirWatchId DirWatchAdd(Func0 onChange) {
    gDirWatchLock.Lock();
    DirWatchEntry e;
    e.id = gDirWatchNext++;
    e.onChange = onChange;
    VecAppend(gDirWatches, e);
    gDirWatchLock.Unlock();
    return e.id;
}

// The receiving end, on the main thread. The slot is emptied before the call
// so that an event arriving during it — a reload that itself takes a moment,
// an editor still writing — queues one more call rather than being lost.
static void DirWatchDeliver(uintptr_t raw) {
    DirWatchId id = (DirWatchId)raw;
    gDirWatchLock.Lock();
    int i = DirWatchIndex(id);
    if (i < 0) {
        gDirWatchLock.Unlock();
        return;
    }
    gDirWatches[i].queued = false;
    Func0 cb = gDirWatches[i].onChange;
    gDirWatchLock.Unlock();
    cb.Call();
}

void DirWatchSignal(DirWatchId id) {
    gDirWatchLock.Lock();
    int i = DirWatchIndex(id);
    if (i < 0 || gDirWatches[i].queued) {
        gDirWatchLock.Unlock();
        return;
    }
    gDirWatches[i].queued = true;
    gDirWatchLock.Unlock();
    // The handle rides in the callback's own word rather than a pointer into
    // the table, which may have moved or lost the entry by the time it runs.
    Func0 f;
    f.fn = (void*)&DirWatchDeliver;
    f.userData = (uintptr_t)id;
    ExecPost(f);
}

DirWatchId DirWatchStart(Str dir, Func0 onChange, bool create,
                         DirWatchError* err) {
    if (err) {
        *err = DirWatchError::None;
    }
    if (len(dir) <= 0 || len(dir) >= kMaxPath) {
        if (err) {
            *err = DirWatchError::Failed;
        }
        return 0;
    }
    DirWatchId id = DirWatchAdd(onChange);
    TempStr path = StrDupTemp(dir);
    DirWatchError e = DirWatchError::None;
    DirWatchPlat* plat = DirWatchPlatOpen(id, path.s, create, &e);
    gDirWatchLock.Lock();
    int i = DirWatchIndex(id);
    if (plat && i >= 0) {
        gDirWatches[i].plat = plat;
    } else if (i >= 0) {
        VecRemoveAt(gDirWatches, i);
    }
    gDirWatchLock.Unlock();
    if (!plat) {
        if (err) {
            *err = e == DirWatchError::None ? DirWatchError::Failed : e;
        }
        return 0;
    }
    return id;
}

void DirWatchStop(DirWatchId id) {
    if (id == 0) {
        return;
    }
    gDirWatchLock.Lock();
    int i = DirWatchIndex(id);
    DirWatchPlat* plat = nullptr;
    if (i >= 0) {
        plat = gDirWatches[i].plat;
        VecRemoveAt(gDirWatches, i);
    }
    gDirWatchLock.Unlock();
    if (plat) {
        DirWatchPlatClose(plat);
    }
}

int DirWatchCount() {
    gDirWatchLock.Lock();
    int n = len(gDirWatches);
    gDirWatchLock.Unlock();
    return n;
}

} // namespace gpui
