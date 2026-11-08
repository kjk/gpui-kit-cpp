/* What the hosted POSIX watchers (inotify, FSEvents, kqueue) share before
   each opens its own OS half. */

#include "sys/dir_watch.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>

namespace gpui {

void* DirWatchPlatAllocPosix(const char* dir, bool create, size_t size,
                             DirWatchError* err) {
    if (create && mkdir(dir, 0777) != 0 && errno != EEXIST) {
        *err = DirWatchError::Failed;
        return nullptr;
    }
    void* w = calloc(1, size);
    if (!w) {
        *err = DirWatchError::Failed;
    }
    return w;
}

} // namespace gpui
