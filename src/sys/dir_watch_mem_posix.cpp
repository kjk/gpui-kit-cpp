/* What the hosted POSIX watchers (inotify, FSEvents, kqueue) share before
   each opens its own OS half. */

#include "sys/dir_watch.h"

#include <errno.h>
#include <sys/stat.h>

namespace gpui {

bool DirWatchMakeDirPosix(const char* dir, bool create, DirWatchError* err) {
    if (create && mkdir(dir, 0777) != 0 && errno != EEXIST) {
        *err = DirWatchError::Failed;
        return false;
    }
    return true;
}

} // namespace gpui
