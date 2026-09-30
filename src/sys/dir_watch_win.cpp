/* The directory watcher on Windows: ReadDirectoryChangesW.

   One thread per watch issues an overlapped read of the folder's change
   records and waits on two events, the read completing and a stop. Every
   record counts — FILE_ACTION_ADDED, _REMOVED, _MODIFIED and both halves of
   a rename are notify's Create, Remove, Modify and Modify(Name) — so the
   records are not parsed at all: a completed read is one DirWatchSignal. A
   read that completes with no bytes is the kernel saying its buffer
   overflowed and records were lost, which is notify's `need_rescan`, and
   that is a signal too.

   bWatchSubtree is FALSE: `RecursiveMode::NonRecursive`. */

#include "sys/dir_watch.h"

namespace gpui {

struct DirWatchPlat {
    DirWatchId id = 0;
    HANDLE dir = INVALID_HANDLE_VALUE;
    HANDLE stop = nullptr;
    HANDLE ioDone = nullptr;
    OVERLAPPED ov = {};
    // What one read can report before it overflows. The records are never
    // read, so this only sets how long a burst can be before it is reported
    // as an overflow instead — which is also a signal.
    DWORD buf[4096] = {};
};

static const DWORD kDirWatchFilter =
    FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
    FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
    FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;

static void DirWatchFree(DirWatchPlat* w) {
    if (w->dir != INVALID_HANDLE_VALUE) {
        CloseHandle(w->dir);
    }
    if (w->stop) {
        CloseHandle(w->stop);
    }
    if (w->ioDone) {
        CloseHandle(w->ioDone);
    }
    w->~DirWatchPlat();
    free(w);
}

// Queue one read of the folder's change records. Changes are only recorded
// from the first read on, so the first is issued by DirWatchPlatOpen before
// it returns: a file written the moment DirWatchStart comes back is seen.
static bool DirWatchIssue(DirWatchPlat* w) {
    ZeroStruct(&w->ov);
    w->ov.hEvent = w->ioDone;
    if (!ReadDirectoryChangesW(w->dir, w->buf, sizeof(w->buf), FALSE,
                               kDirWatchFilter, nullptr, &w->ov, nullptr)) {
        logf("dir watch: ReadDirectoryChangesW failed (%d)",
             (int)GetLastError());
        return false;
    }
    return true;
}

static void DirWatchThread(DirWatchPlat* w) {
    HANDLE events[2] = {w->stop, w->ioDone};
    for (;;) {
        DWORD r = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (r != WAIT_OBJECT_0 + 1) {
            // Stopped. The read is still outstanding and writes into `buf`
            // when it finishes, so it is cancelled and waited out before the
            // buffer is freed.
            DWORD ignored = 0;
            CancelIoEx(w->dir, &w->ov);
            GetOverlappedResult(w->dir, &w->ov, &ignored, TRUE);
            break;
        }
        DWORD bytes = 0;
        if (!GetOverlappedResult(w->dir, &w->ov, &bytes, FALSE)) {
            DWORD e = GetLastError();
            if (e == ERROR_OPERATION_ABORTED) {
                break;
            }
            if (e != ERROR_NOTIFY_ENUM_DIR) {
                // The folder went away under the watch, or the handle did.
                // What was in it is gone too, which is worth one reload.
                logf("dir watch: the watch ended (%d)", (int)e);
                DirWatchSignal(w->id);
                break;
            }
        }
        DirWatchSignal(w->id);
        if (!DirWatchIssue(w)) {
            break;
        }
    }
    // The thread owns the watch's state and frees it, but only once the stop
    // has come: DirWatchPlatClose sets an event in it, so a watch that ended
    // on its own waits here, idle, until its owner lets go of it.
    WaitForSingleObject(w->stop, INFINITE);
    DirWatchFree(w);
}

DirWatchPlat* DirWatchPlatOpen(DirWatchId id, const char* dir, bool create,
                               DirWatchError* err) {
    WCHAR* path = ToCWstrTemp(Str(dir));
    if (!path) {
        *err = DirWatchError::Failed;
        return nullptr;
    }
    if (create) {
        DWORD attrs = GetFileAttributesW(path);
        if (attrs == INVALID_FILE_ATTRIBUTES &&
            !CreateDirectoryW(path, nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS) {
            *err = DirWatchError::Failed;
            return nullptr;
        }
    }
    auto* w = (DirWatchPlat*)calloc(1, sizeof(DirWatchPlat));
    if (!w) {
        *err = DirWatchError::Failed;
        return nullptr;
    }
    new (w) DirWatchPlat();
    w->id = id;
    // FILE_FLAG_BACKUP_SEMANTICS is what lets CreateFile open a directory;
    // FILE_SHARE_DELETE lets the folder be renamed or removed while watched.
    w->dir =
        CreateFileW(path, FILE_LIST_DIRECTORY,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    w->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    w->ioDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (w->dir == INVALID_HANDLE_VALUE || !w->stop || !w->ioDone) {
        DirWatchFree(w);
        *err = DirWatchError::Failed;
        return nullptr;
    }
    if (!DirWatchIssue(w)) {
        DirWatchFree(w);
        *err = DirWatchError::Failed;
        return nullptr;
    }
    if (!PlatThreadRun(MkFunc0(DirWatchThread, w))) {
        // The read is outstanding and writes into `buf`, so it is called
        // off and waited out before the buffer goes.
        DWORD ignored = 0;
        CancelIoEx(w->dir, &w->ov);
        GetOverlappedResult(w->dir, &w->ov, &ignored, TRUE);
        DirWatchFree(w);
        *err = DirWatchError::Failed;
        return nullptr;
    }
    return w;
}

void DirWatchPlatClose(DirWatchPlat* w) {
    if (w) {
        SetEvent(w->stop);
    }
}

} // namespace gpui
