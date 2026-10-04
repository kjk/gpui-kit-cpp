/* The window facts every non-Windows target shares. The browser's clipboard
   read is window_wasm.cpp; the synchronous stub for hosted POSIX is
   window_mem_posix.cpp, so this file can be compiled on wasm too. */

#include "gpui/platform.h"

namespace gpui {

void FrameBenchLogGpu() {}

bool WindowTakePaintArg(Str) {
    return false;
}

} // namespace gpui
