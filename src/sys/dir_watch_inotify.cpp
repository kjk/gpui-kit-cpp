/* The directory watcher on Linux and Android: inotify.

   Android is the Linux kernel and its NDK libc has inotify from API 21, so
   the one file serves both — the `_inotify.cpp` suffix puts it in each of
   their builds. Rust's notify picks its inotify backend for both too.

   One inotify descriptor per watch, with one watch on the folder (inotify
   is non-recursive by nature), and a thread that polls it beside the read
   end of a pipe that DirWatchPlatClose writes to. The mask is the events
   notify's inotify backend turns into Create, Modify and Remove — IN_CREATE,
   IN_MODIFY, IN_ATTRIB, both halves of a move, IN_DELETE, and the folder's
   own deletion or move. IN_Q_OVERFLOW, which the kernel sends whatever the
   mask, is notify's `need_rescan`. The records are not parsed: a read that
   returns anything is one DirWatchSignal. */

#include "sys/dir_watch.h"

#include <errno.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gpui {

struct DirWatchPlat {
    DirWatchId id = 0;
    int fd = -1;
    int stopRead = -1;
    int stopWrite = -1;
};

static const uint32_t kDirWatchMask =
    IN_CREATE | IN_MODIFY | IN_ATTRIB | IN_MOVED_FROM | IN_MOVED_TO |
    IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;

static void DirWatchFree(DirWatchPlat* w) {
    if (w->fd >= 0) {
        close(w->fd);
    }
    if (w->stopRead >= 0) {
        close(w->stopRead);
    }
    if (w->stopWrite >= 0) {
        close(w->stopWrite);
    }
    free(w);
}

static void DirWatchThread(DirWatchPlat* w) {
    // inotify_event is followed by its name, so the buffer is aligned to it.
    alignas(struct inotify_event) char buf[4096];
    bool live = true;
    for (;;) {
        struct pollfd fds[2] = {};
        fds[0].fd = w->stopRead;
        fds[0].events = POLLIN;
        fds[1].fd = live ? w->fd : -1;
        fds[1].events = POLLIN;
        int n = poll(fds, 2, -1);
        if (n < 0) {
            // Only the stop byte ends the thread: DirWatchPlatClose reads
            // `w` to write it, so `w` must still be here when it does.
            if (errno != EINTR) {
                PlatSleepMs(100);
            }
            continue;
        }
        if (fds[0].revents) {
            break;
        }
        if (fds[1].revents & POLLIN) {
            ssize_t got = read(w->fd, buf, sizeof(buf));
            if (got > 0) {
                DirWatchSignal(w->id);
            }
        } else if (fds[1].revents) {
            // The descriptor itself failed. Nothing more will come from it,
            // so the thread only waits for the stop from here on.
            logf("dir watch: inotify stopped reporting");
            live = false;
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
    w->fd = -1;
    w->stopRead = -1;
    w->stopWrite = -1;
    w->fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (w->fd < 0) {
        // Too many inotify instances is the same limit notify reports as
        // MaxFilesWatch, one level up.
        *err = errno == EMFILE ? DirWatchError::Limit : DirWatchError::Failed;
        DirWatchFree(w);
        return nullptr;
    }
    if (inotify_add_watch(w->fd, dir, kDirWatchMask) < 0) {
        // ENOSPC is max_user_watches: notify's ErrorKind::MaxFilesWatch.
        *err = errno == ENOSPC ? DirWatchError::Limit : DirWatchError::Failed;
        DirWatchFree(w);
        return nullptr;
    }
    int p[2] = {-1, -1};
    if (pipe(p) != 0) {
        *err = DirWatchError::Failed;
        DirWatchFree(w);
        return nullptr;
    }
    w->stopRead = p[0];
    w->stopWrite = p[1];
    if (!PlatThreadRun(MkFunc0(DirWatchThread, w))) {
        *err = DirWatchError::Failed;
        DirWatchFree(w);
        return nullptr;
    }
    return w;
}

void DirWatchPlatClose(DirWatchPlat* w) {
    if (w) {
        // The thread wakes on this byte and frees everything, this pipe
        // included, so nothing here touches `w` after the write.
        char b = 1;
        ssize_t r = write(w->stopWrite, &b, 1);
        (void)r;
    }
}

} // namespace gpui
