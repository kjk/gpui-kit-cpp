/* Virtual memory on a host that has it: mmap reserves address space without
   backing it and mprotect is the commit, which is the split base.cpp's Arena
   is written against. Linux and macOS both; wasm has no such split and
   answers for itself in Base_wasm.cpp.

   The rest of the POSIX platform layer — strings, directories, threads, the
   clock — is in Base_posix.cpp and is shared with wasm. */

#include "base.h"

#include <sys/mman.h>
#include <unistd.h>

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

} // namespace base
