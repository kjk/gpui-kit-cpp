/* The part of the Base platform layer every POSIX-shaped target shares: the
   POSIX spellings of the case-insensitive string calls, directory walking,
   threads and the clock. Linux, macOS and wasm all compile this.

   Virtual memory is not here, because wasm has none of the reserve/commit
   shape mmap gives: that half is Base_mem_posix.cpp. What differs between the
   three — where the executable lives, how a process reports its own usage —
   is in Base_linux.cpp, Base_mac.cpp and Base_wasm.cpp. */

#include "base.h"

#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace base {

int StrCmpI(const char* a, const char* b) {
    return strcasecmp(a ? a : "", b ? b : "");
}

int StrCmpNI(const char* a, const char* b, int n) {
    if (n <= 0) {
        return 0;
    }
    return strncasecmp(a ? a : "", b ? b : "", (size_t)n);
}

void StrCopyZ(char* dst, int cap, const char* src) {
    if (!dst || cap <= 0) {
        return;
    }
    if (!src) {
        dst[0] = 0;
        return;
    }
    int n = (int)strlen(src);
    if (n > cap - 1) {
        n = cap - 1;
    }
    memcpy(dst, src, (size_t)n);
    dst[n] = 0;
}

bool PlatDirExists(const char* path) {
    if (!path || !path[0]) {
        return false;
    }
    struct stat st = {};
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool PlatFileExists(const char* path) {
    if (!path || !path[0]) {
        return false;
    }
    struct stat st = {};
    return stat(path, &st) == 0 && !S_ISDIR(st.st_mode);
}

void PlatGetCwd(char* out, int cap) {
    if (!out || cap <= 0) {
        return;
    }
    out[0] = 0;
    if (!getcwd(out, (size_t)cap)) {
        out[0] = 0;
    }
}

bool PlatCanonicalPath(const char* path, char* out, int cap) {
    if (!path || !path[0] || !out || cap <= 0) {
        return false;
    }
    out[0] = 0;
    char* resolved = realpath(path, nullptr);
    if (!resolved) {
        return false;
    }
    int n = (int)strlen(resolved);
    if (n >= cap) {
        free(resolved);
        return false;
    }
    memcpy(out, resolved, (size_t)n + 1);
    free(resolved);
    return true;
}

// Strip the file name off an absolute path, in place. PlatGetExeDir on both
// POSIX platforms ends with this.
void PlatDirNameInPlace(char* path) {
    if (!path) {
        return;
    }
    int i = (int)strlen(path);
    while (i > 0 && path[i - 1] != '/') {
        path[--i] = 0;
    }
    while (i > 1 && path[i - 1] == '/') {
        path[--i] = 0;
    }
}

int PlatListDir(const char* dir, DirEntry* out, int max) {
    if (!dir || !out || max <= 0) {
        return 0;
    }
    DIR* d = opendir(dir);
    if (!d) {
        return 0;
    }
    int n = 0;
    struct dirent* ent = nullptr;
    while (n < max && (ent = readdir(d)) != nullptr) {
        Str name = Str(ent->d_name);
        if (StrEq(name, StrL(".")) || StrEq(name, StrL(".."))) {
            continue;
        }
        DirEntry& e = out[n];
        StrCopyZ(e.name, (int)sizeof(e.name), ent->d_name);
        TempStr full = fmt("%s/%s", Str(dir), name);
        struct stat st = {};
        if (full.len >= kMaxPath || lstat(full.s, &st) != 0) {
            continue;
        }
        e.isSymlink = S_ISLNK(st.st_mode);
        e.isDir = S_ISDIR(st.st_mode);
        e.isFile = S_ISREG(st.st_mode);
        e.size = e.isFile && st.st_size > 0 ? (uint64_t)st.st_size : 0;
#if GPUI_OS_MAC || GPUI_OS_IOS
        e.modified = (uint64_t)st.st_mtimespec.tv_sec * 1000000000ull +
                     (uint64_t)st.st_mtimespec.tv_nsec;
#else
        e.modified = (uint64_t)st.st_mtim.tv_sec * 1000000000ull +
                     (uint64_t)st.st_mtim.tv_nsec;
#endif
        n++;
    }
    closedir(d);
    return n;
}

int PlatCoreCount() {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}
// ─── threads ─────────────────────────────────────────────────────────────

void CondVar::Wait(Mutex* m, int timeoutMs) {
    if (timeoutMs < 0) {
        pthread_cond_wait(&cv, &m->lock);
        return;
    }
    // pthread_cond_timedwait takes an absolute deadline on the same clock the
    // condvar was created with, which is CLOCK_REALTIME by default.
    struct timespec ts = {};
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeoutMs / 1000;
    ts.tv_nsec += (long)(timeoutMs % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    pthread_cond_timedwait(&cv, &m->lock, &ts);
}

static void* ThreadMain(void* arg) {
    auto* call = (Func0*)arg;
    call->Call();
    free(call);
    return nullptr;
}

bool PlatThreadRun(Func0 f) {
    auto* call = (Func0*)calloc(1, sizeof(Func0));
    if (!call) {
        return false;
    }
    *call = f;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    // Detached: nothing joins a worker, and an unjoined joinable thread leaks
    // its stack for as long as the process runs.
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t th = {};
    int err = pthread_create(&th, &attr, ThreadMain, call);
    pthread_attr_destroy(&attr);
    if (err != 0) {
        free(call);
        return false;
    }
    return true;
}

uint64_t PlatThreadId() {
    // pthread_t is a pointer on macOS and an integer on Linux; both fit, and
    // the value is only ever compared with another from the same host.
    pthread_t self = pthread_self();
    uint64_t id = 0;
    memcpy(&id, &self, sizeof(self) < sizeof(id) ? sizeof(self) : sizeof(id));
    return id;
}

void PlatSleepMs(int ms) {
    if (ms <= 0) {
        return;
    }
    struct timespec ts = {};
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, nullptr);
}

} // namespace base
