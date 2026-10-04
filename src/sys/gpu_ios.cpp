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
