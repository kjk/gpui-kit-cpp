/* The platform-independent half of sys/gpu.h: the lock every probe call is
   made under, and crates/fps/src/gpu/unsupported.rs for the platforms with
   no probe.

   Linux has none yet. The remaining gap is the DRM fdinfo entries under
   `/proc/self/fdinfo`, with `drm-engine-*` nanoseconds differenced against
   the wall clock. Until then the HUD leaves the row out, which is what
   Rust's `None` does. */

#include "sys/gpu.h"

namespace gpui {

#if GPUI_GPU_PROBE

// Held for the whole of each call, so two background samplers cannot
// overwrite each other's reading.
static base::Mutex gProbeLock;

bool GpuAvailable() {
    gProbeLock.Lock();
    bool available = GpuProbeOpenLocked();
    gProbeLock.Unlock();
    return available;
}

float GpuUsagePercent() {
    gProbeLock.Lock();
    float usage = GpuUsagePercentLocked();
    gProbeLock.Unlock();
    return usage;
}

void GpuProbeFree() {
    gProbeLock.Lock();
    GpuProbeFreeLocked();
    gProbeLock.Unlock();
}

#else

bool GpuAvailable() {
    return false;
}

float GpuUsagePercent() {
    return -1.f;
}

void GpuProbeFree() {}

#endif

} // namespace gpui
