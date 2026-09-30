/* The directory watcher in the browser: none.

   A page's file system is emscripten's MEMFS, which nothing outside the page
   writes to, so there is nothing to watch. Rust does not compile its theme
   watcher for wasm either. */

#include "sys/dir_watch.h"

namespace gpui {

DirWatchPlat* DirWatchPlatOpen(DirWatchId, const char*, bool,
                               DirWatchError* err) {
    *err = DirWatchError::Unsupported;
    return nullptr;
}

void DirWatchPlatClose(DirWatchPlat*) {}

} // namespace gpui
