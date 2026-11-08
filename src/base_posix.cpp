/* The part of the Base platform layer every POSIX-shaped target shares: the
   POSIX spellings of the case-insensitive string calls, directory walking,
   threads and the clock. Linux, macOS, iOS, Android and wasm all compile
   this, so nothing here may test GPUI_OS_* or call an API wasm does not have.

   Virtual memory is not here, because wasm has none of the reserve/commit
   shape mmap gives: that half is Base_mem_posix.cpp. What differs per target
   — where the executable lives, how a process reports its own usage, and
   the target's kPlatFacts — is in Base_linux.cpp, Base_mac.cpp,
   Base_wasm.cpp, Base_ios.cpp and Base_android.cpp. */

#include "base.h"

#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace base {

// A stat's modification time in nanoseconds. POSIX.1-2008 names the field
// st_mtim (Linux, Android, wasm); Apple's libc names it st_mtimespec (macOS,
// iOS). Overload resolution picks whichever member this libc's struct has,
// so the one body serves every POSIX target without asking which it is.
template <typename S>
static auto StatMtime(const S* st, int) -> decltype(st->st_mtim) {
    return st->st_mtim;
}
template <typename S>
static auto StatMtime(const S* st, long) -> decltype(st->st_mtimespec) {
    return st->st_mtimespec;
}

uint64_t PlatStatModifiedNs(const struct stat* st) {
    auto t = StatMtime(st, 0);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

bool PlatProcSelfExePath(char* out, int cap) {
    ssize_t n = readlink("/proc/self/exe", out, (size_t)cap - 1);
    if (n <= 0) {
        return false;
    }
    out[n] = 0;
    return true;
}

int StrCmpI(const char* a, const char* b) {
    return strcasecmp(a ? a : "", b ? b : "");
}

int StrCmpNI(const char* a, const char* b, int n) {
    if (n <= 0) {
        return 0;
    }
    return strncasecmp(a ? a : "", b ? b : "", (size_t)n);
}

bool PlatDirExists(const char* path) {
    if (!path || !path[0]) {
        return false;
    }
    struct stat st = {};
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool PlatFileIsExecutable(const char* path) {
    if (!path || !path[0]) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return (st.st_mode & 0111) != 0;
}

bool PlatMakeExecutable(const char* path) {
    if (!path || !path[0]) return false;
    return chmod(path, 0755) == 0;
}

// The process environment. unistd.h does not declare it on every target
// this file is compiled for, emscripten included. C linkage is the libc
// symbol; a C++ declaration in this namespace would be a different one.
extern "C" char** environ;

bool PlatVisitEnv(void (*visit)(Str name, Str value, void* user), void* user) {
    if (!visit) return false;
    if (!environ) return true;
    for (char** at = environ; *at; at++) {
        char* eq = strchr(*at, '=');
        if (!eq || eq == *at) continue;
        visit(Str(*at, (int)(eq - *at)), Str(eq + 1), user);
    }
    return true;
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
        if (len(full) >= kMaxPath || lstat(full.s, &st) != 0) {
            continue;
        }
        e.isSymlink = S_ISLNK(st.st_mode);
        e.isDir = S_ISDIR(st.st_mode);
        e.isFile = S_ISREG(st.st_mode);
        e.size = e.isFile && st.st_size > 0 ? (uint64_t)st.st_size : 0;
        e.modified = PlatStatModifiedNs(&st);
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

uint64_t PlatProcessId() {
    return (uint64_t)getpid();
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
