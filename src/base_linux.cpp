/* The Linux-only half of the Base platform layer. Everything POSIX shares
   with macOS is in Base_posix.cpp; what is left reads /proc. */

#include "base.h"

#include <stdio.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace base {

void PlatDirNameInPlace(char* path);

void PlatGetExeDir(char* out, int cap) {
    if (!out || cap <= 0) {
        return;
    }
    out[0] = 0;
    ssize_t n = readlink("/proc/self/exe", out, (size_t)cap - 1);
    if (n <= 0) {
        return;
    }
    out[n] = 0;
    PlatDirNameInPlace(out);
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

uint64_t PlatStatModifiedNs(const struct stat* st) {
    return (uint64_t)st->st_mtim.tv_sec * 1000000000ull + (uint64_t)st->st_mtim
                                                              .tv_nsec;
}

bool PlatSecondaryIsCommand() {
    return false;
}
bool PlatShowsWindowControls() {
    return true;
}
float PlatCaretWidth() {
    return 2.f;
}
bool PlatScrollBounce() {
    return false;
}
const char* PlatMonoFontName() {
    return "DejaVu Sans Mono";
}
const char* PlatShellDataDir() {
    return "/.local/share";
}
const char* PlatShellPlatformName() {
    return "linux";
}
bool PlatBlockSelectUsesControl() {
    return true;
}
bool PlatScrollGestureLocks() {
    return true;
}
bool PlatAsyncIo() {
    return false;
}
float PlatWindowShadowSize() {
    return 20.f;
}

} // namespace base
