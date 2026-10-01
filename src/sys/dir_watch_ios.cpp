/* The directory watcher on iOS: kqueue.

   FSEvents, which the macOS half uses, is not public on iOS, and notify's
   iOS backend is kqueue — so is this. A kqueue reports on open descriptors,
   not paths: EVFILT_VNODE on the folder says an entry was added, removed or
   renamed (NOTE_WRITE) but not that a file already in it was rewritten in
   place. notify's kqueue backend, watching non-recursively, therefore opens
   the folder and every entry directly in it; this does the same, and opens
   the entries again whenever the folder itself changes, so a file that has
   just appeared is watched from then on.

   One kqueue and one thread per watch. The thread blocks in kevent; an
   EVFILT_USER event, which DirWatchPlatClose triggers, is the stop. Any
   vnode event is one DirWatchSignal — the folder's being deleted, renamed
   or revoked included, as notify reports those too. Descriptors are opened
   with O_EVTONLY, which watches without keeping a volume busy. Running out
   of descriptors while opening the folder is the Limit error: a kqueue
   watch costs one descriptor per file, so that is this backend's
   MaxFilesWatch. */

#include "sys/dir_watch.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gpui {

struct DirWatchPlat {
    DirWatchId id = 0;
    int kq = -1;
    int dirFd = -1;
    char dir[kMaxPath] = {};
    // One O_EVTONLY descriptor per entry directly in the folder.
    int* files = nullptr;
    int nFiles = 0;
    int capFiles = 0;
};

// The EVFILT_USER event's ident; a vnode event's ident is its descriptor,
// and EVFILT_USER is a filter of its own, so any value would do.
static const uintptr_t kDirWatchStop = 1;

static const unsigned kDirWatchDirMask = NOTE_WRITE | NOTE_EXTEND |
                                         NOTE_ATTRIB | NOTE_LINK | NOTE_DELETE |
                                         NOTE_RENAME | NOTE_REVOKE;
static const unsigned kDirWatchFileMask = NOTE_WRITE | NOTE_EXTEND |
                                          NOTE_ATTRIB | NOTE_DELETE |
                                          NOTE_RENAME | NOTE_REVOKE;

static bool DirWatchAddVnode(int kq, int fd, unsigned mask) {
    struct kevent ev;
    EV_SET(&ev, (uintptr_t)fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, mask, 0,
           nullptr);
    return kevent(kq, &ev, 1, nullptr, 0, nullptr) == 0;
}

// Closing a descriptor removes its kevents with it.
static void DirWatchCloseFiles(DirWatchPlat* w) {
    for (int i = 0; i < w->nFiles; i++) {
        close(w->files[i]);
    }
    w->nFiles = 0;
}

// Watch every entry directly in the folder, dropping what was watched
// before. An entry that cannot be opened is left unwatched: its folder's
// events still reach us.
static void DirWatchOpenFiles(DirWatchPlat* w) {
    DirWatchCloseFiles(w);
    DIR* d = opendir(w->dir);
    if (!d) {
        return;
    }
    while (struct dirent* e = readdir(d)) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        int fd = openat(w->dirFd, e->d_name, O_EVTONLY | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        if (w->nFiles == w->capFiles) {
            int cap = w->capFiles ? w->capFiles * 2 : 16;
            auto* grown = (int*)realloc(w->files, sizeof(int) * (size_t)cap);
            if (!grown) {
                close(fd);
                break;
            }
            w->files = grown;
            w->capFiles = cap;
        }
        if (!DirWatchAddVnode(w->kq, fd, kDirWatchFileMask)) {
            close(fd);
            continue;
        }
        w->files[w->nFiles++] = fd;
    }
    closedir(d);
}

static void DirWatchFree(DirWatchPlat* w) {
    DirWatchCloseFiles(w);
    free(w->files);
    if (w->dirFd >= 0) {
        close(w->dirFd);
    }
    if (w->kq >= 0) {
        close(w->kq);
    }
    free(w);
}

static void DirWatchThread(DirWatchPlat* w) {
    struct kevent evs[16];
    for (;;) {
        int n = kevent(w->kq, nullptr, 0, evs, 16, nullptr);
        if (n < 0) {
            // Only the stop ends the thread: DirWatchPlatClose reads `w` to
            // trigger it, so `w` must still be here when it does.
            if (errno != EINTR) {
                PlatSleepMs(100);
            }
            continue;
        }
        bool stop = false;
        bool changed = false;
        bool dirChanged = false;
        for (int i = 0; i < n; i++) {
            if (evs[i].filter == EVFILT_USER) {
                stop = true;
            } else if (evs[i].filter == EVFILT_VNODE) {
                changed = true;
                if ((int)evs[i].ident == w->dirFd) {
                    dirChanged = true;
                }
            }
        }
        if (stop) {
            break;
        }
        if (dirChanged) {
            // An entry came, went or was renamed: watch the folder's
            // entries as they are now.
            DirWatchOpenFiles(w);
        }
        if (changed) {
            DirWatchSignal(w->id);
        }
    }
    DirWatchFree(w);
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
    w->kq = -1;
    w->dirFd = -1;
    size_t n = strlen(dir);
    if (n >= sizeof(w->dir)) {
        *err = DirWatchError::Failed;
        DirWatchFree(w);
        return nullptr;
    }
    memcpy(w->dir, dir, n + 1);
    w->kq = kqueue();
    if (w->kq < 0) {
        *err = errno == EMFILE || errno == ENFILE ? DirWatchError::Limit
                                                  : DirWatchError::Failed;
        DirWatchFree(w);
        return nullptr;
    }
    w->dirFd = open(dir, O_EVTONLY | O_DIRECTORY | O_CLOEXEC);
    if (w->dirFd < 0) {
        *err = errno == EMFILE || errno == ENFILE ? DirWatchError::Limit
                                                  : DirWatchError::Failed;
        DirWatchFree(w);
        return nullptr;
    }
    struct kevent stop;
    EV_SET(&stop, kDirWatchStop, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    if (kevent(w->kq, &stop, 1, nullptr, 0, nullptr) != 0 ||
        !DirWatchAddVnode(w->kq, w->dirFd, kDirWatchDirMask)) {
        *err = DirWatchError::Failed;
        DirWatchFree(w);
        return nullptr;
    }
    DirWatchOpenFiles(w);
    if (!PlatThreadRun(MkFunc0(DirWatchThread, w))) {
        *err = DirWatchError::Failed;
        DirWatchFree(w);
        return nullptr;
    }
    return w;
}

void DirWatchPlatClose(DirWatchPlat* w) {
    if (w) {
        // The thread wakes on this and frees everything, the kqueue
        // included, so nothing here touches `w` after the kevent call.
        struct kevent ev;
        EV_SET(&ev, kDirWatchStop, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
        kevent(w->kq, &ev, 1, nullptr, 0, nullptr);
    }
}

} // namespace gpui
