/* No GPU probe yet — crates/fps/src/gpu/unsupported.rs
 *
 * The remaining gap is the DRM fdinfo entries under `/proc/self/fdinfo`,
 * with `drm-engine-*` nanoseconds differenced against the wall clock. Until
 * then the HUD leaves the row out, which is what Rust's `None` does.
 */

#include "sys/gpu.h"

namespace gpui {

bool GpuAvailable() {
    return false;
}

float GpuUsagePercent() {
    return -1.f;
}

void GpuProbeFree() {}

} // namespace gpui
