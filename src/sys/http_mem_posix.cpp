/* Hosted POSIX start for HttpSendAsync. wasm launches the browser fetch
   from http_wasm.cpp, and that symbol is not linked anywhere else. */

#include "sys/http.h"

namespace gpui {

bool HttpAsyncLaunch(HttpAsyncJob* job) {
    return HttpAsyncLaunchHosted(job);
}

} // namespace gpui
