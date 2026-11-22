/* Virtual memory on a host that has it: mmap reserves address space without
   backing it and mprotect is the commit, which is the split base.cpp's Arena
   is written against. Linux, macOS, iOS and Android; wasm has no such split
   and answers for itself in base_wasm.cpp. Also what else a hosted POSIX
   process can say about itself: its directory and its own CPU and memory.

   The rest of the POSIX platform layer — strings, directories, threads, the
   clock — is in base_posix.cpp and is shared with wasm. */

#include "base.h"

#include <stdio.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#if GPUI_HAVE_MACH_TASK_INFO
#include <mach/mach.h>
#endif

namespace base {

uint64_t PlatPageSize() {
    static uint64_t pageSize = 0;
    if (pageSize == 0) {
        long n = sysconf(_SC_PAGESIZE);
        pageSize = n > 0 ? (uint64_t)n : 4096;
    }
    return pageSize;
}

// 2 MB on x86-64 and arm64. The arena only uses this to round a reservation,
// and asking the kernel for real huge pages needs a preallocated pool, so
// large pages are never actually requested below.
uint64_t PlatLargePageSize() {
    return 2ull * 1024ull * 1024ull;
}

// mmap has no reserve/commit split: PROT_NONE reserves the address range, and
// mprotect on a subrange is the commit.
void* PlatMemReserve(uint64_t size) {
    if (size == 0) {
        return nullptr;
    }
    void* p = mmap(nullptr, (size_t)size, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
}

bool PlatMemCommitPages(void* base, uint64_t size, bool largePages) {
    (void)largePages;
    // Round out to whole pages: the arena commits in its own chunk size, which
    // is not required to be page-aligned at the tail.
    uint64_t page = PlatPageSize();
    uintptr_t start = (uintptr_t)base & ~(uintptr_t)(page - 1);
    uintptr_t end =
        ((uintptr_t)base + (uintptr_t)size + page - 1) & ~(uintptr_t)(page - 1);
    return mprotect((void*)start, (size_t)(end - start),
                    PROT_READ | PROT_WRITE) == 0;
}

void* PlatMemReserveCommit(uint64_t size, bool largePages) {
    (void)largePages;
    if (size == 0) {
        return nullptr;
    }
    void* p = mmap(nullptr, (size_t)size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
}

void PlatMemRelease(void* base, uint64_t size) {
    if (base && size > 0) {
        munmap(base, (size_t)size);
    }
}

// The executable's directory: empty on iOS, which reports no executable
// path; on Android it is the directory of the app_process binary.
void PlatGetExeDir(char* out, int cap) {
    PlatExeDirFromExePath(out, cap);
}

#if GPUI_HAVE_MACH_TASK_INFO
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
#else
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
#endif

} // namespace base
