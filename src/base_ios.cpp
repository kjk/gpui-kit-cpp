/* iOS answers for the Plat* facts (base.h's PlatFacts): what differs here
   from the defaults. Strings, directories, threads, the clock and stat
   timestamps are in base_posix.cpp. The shortcut modifier, fonts and window
   chrome are not macOS's — iOS is not GPUI_OS_MAC — and there is no
   executable path to report. */

#include "base.h"

namespace base {

const PlatFacts kPlatFacts = {
    .scrollBounce = true,
    .scrollBounceTouch = true,
};

} // namespace base
