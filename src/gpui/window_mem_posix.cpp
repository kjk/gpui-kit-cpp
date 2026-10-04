/* Hosted POSIX clipboard: the read is synchronous, so there is nothing for
   ClipboardReadAsync to wait on. wasm keeps the real one in window_wasm.cpp.
   Not a _posix.cpp file, because that suffix is compiled on wasm too. */

#include "gpui/platform.h"

namespace gpui {

bool ClipboardReadAsync(Window* win, ClipboardReadFn done, void* data) {
    (void)win;
    (void)done;
    (void)data;
    return false;
}

} // namespace gpui
