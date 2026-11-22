/* The Linux-only half of the Base platform layer. Everything POSIX shares
   with macOS is in Base_posix.cpp; what is left reads /proc. */

#include "base.h"

#include <stdio.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace base {

const PlatFacts kPlatFacts = {
    // The title bar is ours, close button included.
    .titleBarCloseIsElement = true,
    .autoHideScrollbars = false,
    .blockSelectUsesControl = true,
    // A client-decorated X11 window draws its own shadow.
    .windowShadowSize = 20.f,
    .exePath = PlatProcSelfExePath,
};

} // namespace base
