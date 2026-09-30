#ifndef GPUI_SYS_DIR_WATCH_H_
#define GPUI_SYS_DIR_WATCH_H_
/* Watching one directory for changes — the part of the `notify` crate that
   crates/component/src/theme/registry.rs uses.

   `ThemeRegistry::_watch_themes_dir` makes the folder if it is missing,
   watches it non-recursively, and turns every event that could mean a theme
   changed — a create, a modify, a remove, an `Any`, or a backend saying it
   lost events and wants a rescan — into a `try_send` on a one-slot channel.
   A foreground task receives from that channel and reloads. The channel is
   the coalescing: while a message is waiting, further sends are dropped, so
   a burst of events (an editor's save is several) is one reload, and an
   event that lands during a reload is one more.

   That is what this is. A watch answers to an integer handle, the way
   executor jobs do, and `onChange` runs on the main thread through ExecPost.
   Which file changed and how is not reported, since the one caller reloads
   the whole folder regardless.

   The OS half is one file per platform, each with a thread of its own (or a
   dispatch queue) that does nothing but wait and call DirWatchSignal:

   - Windows: ReadDirectoryChangesW, overlapped, on a thread per watch.
   - Linux: inotify, one descriptor and a thread per watch.
   - macOS: an FSEvents stream on a dispatch queue, reporting file-level
     events, of which those whose parent is not the folder are dropped —
     FSEvents watches a tree and has no non-recursive switch.
   - wasm, iOS, Android: none. DirWatchStart answers Unsupported, and a
     caller carries on without hot reload, which is what Rust does on wasm
     (it never compiles the watcher there). */

#include "base.h"

namespace gpui {

// What a watch answers to. 0 is no watch.
using DirWatchId = int;

enum class DirWatchError {
    None = 0,
    // This platform has no watcher (wasm, iOS, Android).
    Unsupported,
    // The folder could not be made, opened, or watched.
    Failed,
    // The OS has no watches left to hand out — inotify's
    // max_user_watches. notify reports this as ErrorKind::MaxFilesWatch
    // and Rust logs it as its own case.
    Limit,
};

// A short English phrase for a log line.
const char* DirWatchErrorName(DirWatchError err);

// Watch `dir`, not its subdirectories. When `create` is set and the folder
// does not exist, it is made first (one level; its parent must exist), which
// is what the theme watcher does before it watches. `onChange` is posted to
// the main thread after anything in the folder is created, written, renamed
// or removed, or the OS says it lost track; a burst of those is one call, and
// one that arrives while the call is queued is folded into it.
//
// Returns 0 and fills `err` when nothing is being watched.
DirWatchId DirWatchStart(Str dir, Func0 onChange, bool create = false,
                         DirWatchError* err = nullptr);

// Stop watching. `onChange` is not called again, even if a call was already
// queued. Unknown and 0 handles are ignored.
void DirWatchStop(DirWatchId id);

// How many watches are live.
int DirWatchCount();

// ─── the platform half ────────────────────────────────────────────────────
//
// Not for callers: what dir_watch.cpp and the dir_watch_<os>.cpp files say to
// each other. A test stands in for the OS by calling DirWatchAdd and
// DirWatchSignal, which is how the coalescing is checked without waiting for
// a real file system to deliver anything.

// A watch with no OS half: registered, answering to DirWatchSignal only.
DirWatchId DirWatchAdd(Func0 onChange);

// Something happened in watch `id`'s folder. Safe from any thread; the first
// signal queues one call to `onChange` on the main thread and later ones are
// dropped until that call starts. A stopped or unknown `id` is ignored, which
// is what lets an OS thread that has not noticed a stop yet keep calling.
void DirWatchSignal(DirWatchId id);

// The OS half's own state for one watch; each platform file defines it.
struct DirWatchPlat;

#if GPUI_OS_IOS || GPUI_OS_ANDROID
// The mobile hosts carry no watcher: an application that wants its themes
// reloaded from disk has the host tell it so. Stubbed here, the way
// platform.h stubs the reduce-motion listener, so the host adapter has
// nothing to supply.
inline DirWatchPlat* DirWatchPlatOpen(DirWatchId, const char*, bool,
                                      DirWatchError* err) {
    if (err) {
        *err = DirWatchError::Unsupported;
    }
    return nullptr;
}
inline void DirWatchPlatClose(DirWatchPlat*) {}
#else
// Start the OS half for watch `id` on `dir` (a NUL-terminated path), calling
// DirWatchSignal(id) from its own thread. Null with `err` filled on failure.
DirWatchPlat* DirWatchPlatOpen(DirWatchId id, const char* dir, bool create,
                               DirWatchError* err);
// Ask it to stop. It may still be finishing on its own thread and free itself
// there; the caller must not touch `plat` again.
void DirWatchPlatClose(DirWatchPlat* plat);
#endif

} // namespace gpui
#endif // GPUI_SYS_DIR_WATCH_H_
