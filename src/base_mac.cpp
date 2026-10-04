/* The macOS-only half of the Base platform layer. Everything POSIX shares
   with Linux is in Base_posix.cpp; what is left is dyld and Mach. */

#include "base.h"

#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <sys/stat.h>

namespace base {

void PlatDirNameInPlace(char* path);

void PlatGetExeDir(char* out, int cap) {
    if (!out || cap <= 0) {
        return;
    }
    out[0] = 0;
    uint32_t n = (uint32_t)cap;
    if (_NSGetExecutablePath(out, &n) != 0) {
        out[0] = 0;
        return;
    }
    out[cap - 1] = 0;
    PlatDirNameInPlace(out);
}

bool PlatSelfUsage(uint64_t* cpu100ns, uint64_t* memBytes) {
    // task_info answers both halves for this process: TASK_BASIC_INFO carries
    // the resident size, TASK_THREAD_TIMES_INFO the CPU split. The basic info
    // struct also has user/system time, but only for terminated threads, so
    // the live threads have to be added back.
    mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
    task_basic_info_data_t basic = {};
    if (task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&basic,
                  &count) != KERN_SUCCESS) {
        return false;
    }
    if (memBytes) {
        *memBytes = (uint64_t)basic.resident_size;
    }
    if (cpu100ns) {
        uint64_t us = (uint64_t)basic.user_time.seconds * 1000000ull +
                      (uint64_t)basic.user_time.microseconds +
                      (uint64_t)basic.system_time.seconds * 1000000ull +
                      (uint64_t)basic.system_time.microseconds;
        count = TASK_THREAD_TIMES_INFO_COUNT;
        task_thread_times_info_data_t threads = {};
        if (task_info(mach_task_self(), TASK_THREAD_TIMES_INFO,
                      (task_info_t)&threads, &count) == KERN_SUCCESS) {
            us += (uint64_t)threads.user_time.seconds * 1000000ull +
                  (uint64_t)threads.user_time.microseconds +
                  (uint64_t)threads.system_time.seconds * 1000000ull +
                  (uint64_t)threads.system_time.microseconds;
        }
        *cpu100ns = us * 10ull;
    }
    return true;
}

uint64_t PlatStatModifiedNs(const struct stat* st) {
    return (uint64_t)st->st_mtimespec.tv_sec * 1000000000ull +
           (uint64_t)st->st_mtimespec.tv_nsec;
}

bool PlatSecondaryIsCommand() {
    return true;
}
bool PlatShowsWindowControls() {
    return false;
}
float PlatCaretWidth() {
    return 1.5f;
}
bool PlatScrollBounce() {
    return false;
}
const char* PlatMonoFontName() {
    return "Menlo";
}
const char* PlatShellDataDir() {
    return "/Library/Application Support";
}
const char* PlatShellPlatformName() {
    return "macos";
}
bool PlatBlockSelectUsesControl() {
    return false;
}
bool PlatScrollGestureLocks() {
    return true;
}
bool PlatAsyncIo() {
    return false;
}
float PlatWindowShadowSize() {
    return 0.f;
}

} // namespace base
