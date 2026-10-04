/* The wasm-only half of the Base platform layer: virtual memory that is not
   virtual, where the program lives, and what it is using.

   Everything POSIX-shaped — the case-insensitive string calls, directory
   walking, threads, the clock — is in Base_posix.cpp, which emscripten's libc
   answers as well as Linux does. What it cannot answer is mmap with a
   reserve/commit split, so that half (Base_mem_posix.cpp on Linux and macOS)
   is written out again here against the one thing a wasm page really has: a
   linear heap that grows.

   Emscripten does have an mmap, and MAP_ANONYMOUS | PROT_NONE returns an
   address rather than failing — but the pages are real the moment they are
   handed out, so a 64 MB "reservation" costs 64 MB of the heap, and every
   mprotect prints `unsupported syscall`. Reserving through malloc says the
   same thing without the pretence. */

#include "base.h"

#include <emscripten/emscripten.h>
#include <emscripten/heap.h>
#include <sys/stat.h>
#include <stdlib.h>

namespace base {

void PlatDirNameInPlace(char* path);

// One wasm page. Not the 4 KB every other target reports: memory.grow works
// in 64 KB units, and emscripten's sysconf says so too.
uint64_t PlatPageSize() {
    return 65536;
}

uint64_t PlatLargePageSize() {
    // No huge pages in a wasm heap. The arena only uses this to round a
    // reservation, and PlatMemReserveCommit refuses large pages below, so
    // this never picks a size anything is allocated at.
    return 65536;
}

// There is no reserve here — a wasm page's address space *is* its memory —
// so a reservation is an allocation, and PlatMemCommit only has to make good
// on the promise the Arena reads out of a fresh reservation: that the bytes
// it has not written yet are zero.
void* PlatMemReserve(uint64_t size) {
    if (size == 0) {
        return nullptr;
    }
    // Aligned to a page so that the commit arithmetic in base.cpp, which
    // rounds a subrange out to page boundaries, never walks off the front.
    return aligned_alloc((size_t)PlatPageSize(), (size_t)size);
}

bool PlatMemCommit(void* base, uint64_t size, bool largePages) {
    (void)largePages;
    if (size == 0) {
        return true;
    }
    if (!base) {
        return false;
    }
    // The zeroing is the whole job. Arena::Push only memsets the part of a
    // block that was already committed and takes the newly committed part on
    // trust, because on a real OS a fresh page arrives zeroed. malloc makes
    // no such promise, so this is where it is kept.
    memset(base, 0, (size_t)size);
    return true;
}

void* PlatMemReserveCommit(uint64_t size, bool largePages) {
    if (largePages) {
        // No large pages: say so, and the caller drops the flag and comes
        // back through PlatMemReserve.
        return nullptr;
    }
    void* p = PlatMemReserve(size);
    if (p) {
        memset(p, 0, (size_t)size);
    }
    return p;
}

void PlatMemRelease(void* base, uint64_t size) {
    (void)size;
    free(base);
}

// 4 MB, where the hosted targets say 64. A reservation is spent memory here,
// and a page that opened six arenas at 64 MB apiece would ask the browser for
// nearly half a gigabyte before drawing a pixel. An arena that outgrows this
// chains another block, so the only cost of the smaller number is the odd
// extra block.
uint64_t PlatArenaReserveSize() {
    return 4ull * 1024ull * 1024ull;
}

// There is no executable and no directory it sits in. Assets are preloaded
// into MEMFS at the root by the build (see cmd/build-wasm.ts), which is what
// gpui/assets.cpp walks from.
void PlatGetExeDir(char* out, int cap) {
    if (!out || cap <= 0) {
        return;
    }
    StrCopyZ(out, cap, "/");
}

bool PlatSelfUsage(uint64_t* cpu100ns, uint64_t* memBytes) {
    if (cpu100ns) {
        // No per-process CPU time in a browser: emscripten_get_now() is the
        // page's wall clock, not this program's share of a core. The FPS HUD
        // reads a rising counter, so answer with wall time and let it show
        // 100% of one thread, which — single-threaded, in an animation-frame
        // loop — is not far off what the tab is doing.
        *cpu100ns = (uint64_t)(emscripten_get_now() * 10000.0);
    }
    if (memBytes) {
        // The whole linear heap. A wasm module cannot see what its allocator
        // has handed back, and the heap only grows, so this is the resident
        // set as far as the page is concerned.
        *memBytes = (uint64_t)emscripten_get_heap_size();
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
    return "emscripten";
}
bool PlatBlockSelectUsesControl() {
    return false;
}
bool PlatScrollGestureLocks() {
    return false;
}
bool PlatAsyncIo() {
    return true;
}
float PlatWindowShadowSize() {
    return 0.f;
}

} // namespace base
