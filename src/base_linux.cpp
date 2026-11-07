/* The Linux-only half of the Base platform layer. Everything POSIX shares
   with macOS is in Base_posix.cpp; what is left reads /proc. */

#include "base.h"

#include <stdio.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace base {

void PlatGetExeDir(char* out, int cap) {
    PlatExeDirFromExePath(out, cap);
}

bool PlatSelfUsage(uint64_t* cpu100ns, uint64_t* memBytes) {
    // getrusage gives the CPU time; the resident set only comes out of
    // /proc/self/statm, whose second field is pages.
    struct rusage ru = {};
    if (getrusage(RUSAGE_SELF, &ru) != 0) {
        return false;
    }
    if (cpu100ns) {
        uint64_t us = (uint64_t)ru.ru_utime.tv_sec * 1000000ull +
                      (uint64_t)ru.ru_utime.tv_usec +
                      (uint64_t)ru.ru_stime.tv_sec * 1000000ull +
                      (uint64_t)ru.ru_stime.tv_usec;
        *cpu100ns = us * 10ull;
    }
    if (memBytes) {
        *memBytes = 0;
        FILE* f = fopen("/proc/self/statm", "rb");
        if (f) {
            unsigned long total = 0, resident = 0;
            if (fscanf(f, "%lu %lu", &total, &resident) == 2) {
                long page = sysconf(_SC_PAGESIZE);
                *memBytes =
                    (uint64_t)resident * (uint64_t)(page > 0 ? page : 4096);
            }
            fclose(f);
        }
    }
    return true;
}

const PlatFacts kPlatFacts = {
    // The title bar is ours, close button included.
    .titleBarCloseIsElement = true,
    .blockSelectUsesControl = true,
    // A client-decorated X11 window draws its own shadow.
    .windowShadowSize = 20.f,
    .exePath = PlatProcSelfExePath,
};

} // namespace base
