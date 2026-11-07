/* Android answers for the Plat* facts (base.h's PlatFacts): what differs
   here from the defaults. Strings, directories, threads, the clock and stat
   timestamps are in base_posix.cpp. */

#include "base.h"

namespace base {

const PlatFacts kPlatFacts = {
    .scrollBounce = true,
    // The back button arrives as Escape.
    .cleanOnEscape = false,
    .scrollBounceTouch = true,
    .exePath = PlatProcSelfExePath,
};

} // namespace base
