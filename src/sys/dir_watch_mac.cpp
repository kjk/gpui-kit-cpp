/* The directory watcher on macOS: FSEvents.

   notify's macOS backend is FSEvents too. A stream watches a tree and has no
   non-recursive switch, so it is created with file-level events and each
   event is kept only when the path it names is the folder or sits directly
   in it — which is what notify does for `RecursiveMode::NonRecursive` on
   this backend. The paths FSEvents reports are the resolved ones
   (/private/var, not /var), so the folder is resolved with realpath before
   it is compared. MustScanSubDirs and the two Dropped flags are notify's
   `need_rescan`, and signal whatever path they carry.

   The stream runs on one serial dispatch queue shared by every watch, so no
   thread of ours is involved; the callback calls DirWatchSignal from it. */

#include "sys/dir_watch.h"

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>

namespace gpui {

struct DirWatchPlat {
    DirWatchId id = 0;
    FSEventStreamRef stream = nullptr;
    char real[PATH_MAX] = {};
    int realLen = 0;
};

// Created once and kept: under ARC a dispatch queue is an object, and a
// global is the one place its ownership needs no bookkeeping.
static dispatch_queue_t gDirWatchQueue;

static dispatch_queue_t DirWatchQueue() {
    static dispatch_once_t once;
    dispatch_once(&once, ^{
      gDirWatchQueue =
          dispatch_queue_create("gpui.dirwatch", DISPATCH_QUEUE_SERIAL);
    });
    return gDirWatchQueue;
}

// Whether `path` is the folder itself or one entry directly inside it.
static bool DirWatchInFolder(const DirWatchPlat* w, const char* path) {
    int n = (int)strlen(path);
    while (n > 1 && path[n - 1] == '/') {
        n--;
    }
    if (n == w->realLen && memcmp(path, w->real, (size_t)n) == 0) {
        return true;
    }
    int slash = n - 1;
    while (slash >= 0 && path[slash] != '/') {
        slash--;
    }
    int parentLen = slash <= 0 ? 1 : slash;
    return parentLen == w->realLen &&
           memcmp(path, w->real, (size_t)parentLen) == 0;
}

static void DirWatchCallback(ConstFSEventStreamRef, void* info, size_t count,
                             void* paths, const FSEventStreamEventFlags* flags,
                             const FSEventStreamEventId*) {
    auto* w = (DirWatchPlat*)info;
    auto** names = (char**)paths;
    const FSEventStreamEventFlags rescan =
        kFSEventStreamEventFlagMustScanSubDirs |
        kFSEventStreamEventFlagUserDropped |
        kFSEventStreamEventFlagKernelDropped |
        kFSEventStreamEventFlagRootChanged;
    for (size_t i = 0; i < count; i++) {
        if ((flags[i] & rescan) || DirWatchInFolder(w, names[i])) {
            DirWatchSignal(w->id);
            return;
        }
    }
}

// Runs on the queue after the stream is invalidated, so any callback that
// was already queued has run by then and none can follow.
static void DirWatchFreeOnQueue(void* p) {
    auto* w = (DirWatchPlat*)p;
    if (w->stream) {
        FSEventStreamRelease(w->stream);
    }
    free(w);
}

DirWatchPlat* DirWatchPlatOpen(DirWatchId id, const char* dir, bool create,
                               DirWatchError* err) {
    if (create && mkdir(dir, 0777) != 0 && errno != EEXIST) {
        *err = DirWatchError::Failed;
        return nullptr;
    }
    auto* w = (DirWatchPlat*)calloc(1, sizeof(DirWatchPlat));
    if (!w) {
        *err = DirWatchError::Failed;
        return nullptr;
    }
    w->id = id;
    struct stat st = {};
    if (!realpath(dir, w->real) || stat(w->real, &st) != 0 ||
        !S_ISDIR(st.st_mode)) {
        free(w);
        *err = DirWatchError::Failed;
        return nullptr;
    }
    w->realLen = (int)strlen(w->real);

    CFStringRef path =
        CFStringCreateWithCString(nullptr, w->real, kCFStringEncodingUTF8);
    CFArrayRef paths =
        CFArrayCreate(nullptr, (const void**)&path, 1, &kCFTypeArrayCallBacks);
    FSEventStreamContext ctx = {0, w, nullptr, nullptr, nullptr};
    w->stream = FSEventStreamCreate(nullptr, &DirWatchCallback, &ctx, paths,
                                    kFSEventStreamEventIdSinceNow, 0.05,
                                    kFSEventStreamCreateFlagFileEvents |
                                        kFSEventStreamCreateFlagNoDefer |
                                        kFSEventStreamCreateFlagWatchRoot);
    CFRelease(paths);
    CFRelease(path);
    if (!w->stream) {
        free(w);
        *err = DirWatchError::Failed;
        return nullptr;
    }
    FSEventStreamSetDispatchQueue(w->stream, DirWatchQueue());
    if (!FSEventStreamStart(w->stream)) {
        FSEventStreamInvalidate(w->stream);
        FSEventStreamRelease(w->stream);
        free(w);
        *err = DirWatchError::Failed;
        return nullptr;
    }
    return w;
}

void DirWatchPlatClose(DirWatchPlat* w) {
    if (!w) {
        return;
    }
    FSEventStreamStop(w->stream);
    FSEventStreamInvalidate(w->stream);
    dispatch_async_f(DirWatchQueue(), w, &DirWatchFreeOnQueue);
}

} // namespace gpui
