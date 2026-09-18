#ifndef BASE_H_
#define BASE_H_
/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#ifndef UNICODE
#define UNICODE
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// C/C++ standard headers we use often
#include <cstdint>
#include <cstddef> // for offsetof
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>       // for placement new
#include <algorithm> // for std::min, std::max
#include <utility>   // for std::forward

// ─── os ──────────────────────────────────────────────────────────────────
//
// Exactly one of these is 1 on every build. Prefer a portable function
// implemented in a platform-suffixed source over an
// #if in shared code: these are for the handful of places where a single
// expression differs.

// __EMSCRIPTEN__ is tested first: an emscripten build is a POSIX-shaped libc
// on top of a browser, and the checks below would happily claim it.
#if defined(__EMSCRIPTEN__)
#define GPUI_OS_WINDOWS 0
#define GPUI_OS_LINUX 0
#define GPUI_OS_MAC 0
#define GPUI_OS_IOS 0
#define GPUI_OS_ANDROID 0
#define GPUI_OS_WASM 1
#elif defined(_WIN32)
#define GPUI_OS_WINDOWS 1
#define GPUI_OS_LINUX 0
#define GPUI_OS_MAC 0
#define GPUI_OS_IOS 0
#define GPUI_OS_ANDROID 0
#define GPUI_OS_WASM 0
#elif defined(__ANDROID__)
#define GPUI_OS_WINDOWS 0
#define GPUI_OS_LINUX 0
#define GPUI_OS_MAC 0
#define GPUI_OS_IOS 0
#define GPUI_OS_ANDROID 1
#define GPUI_OS_WASM 0
#elif defined(__APPLE__) && \
    defined(__ENVIRONMENT_IPHONE_OS_VERSION_MIN_REQUIRED__)
#define GPUI_OS_WINDOWS 0
#define GPUI_OS_LINUX 0
#define GPUI_OS_MAC 0
#define GPUI_OS_IOS 1
#define GPUI_OS_ANDROID 0
#define GPUI_OS_WASM 0
#elif defined(__APPLE__)
#define GPUI_OS_WINDOWS 0
#define GPUI_OS_LINUX 0
#define GPUI_OS_MAC 1
#define GPUI_OS_IOS 0
#define GPUI_OS_ANDROID 0
#define GPUI_OS_WASM 0
#elif defined(__linux__)
#define GPUI_OS_WINDOWS 0
#define GPUI_OS_LINUX 1
#define GPUI_OS_MAC 0
#define GPUI_OS_IOS 0
#define GPUI_OS_ANDROID 0
#define GPUI_OS_WASM 0
#else
#error "unsupported platform"
#endif

// Everything that is not Windows is a POSIX host here, which is what the
// _posix.cpp half of the platform layer is written against. Emscripten is
// one of them: its libc has the strings, the directories and the clock this
// tree asks POSIX for. What it does not have is virtual memory with a
// reserve/commit split, so the mmap half lives in _mem_posix.cpp and wasm
// answers for itself.
#define GPUI_OS_POSIX (!GPUI_OS_WINDOWS)

#if GPUI_OS_WINDOWS
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#else
#include <pthread.h>
#include <limits.h>
#endif

// The SumatraPDF base this tree is built on — Str, Vec, Arena, Func0/Func1,
// fmt/logf, the Plat* platform shims — in a namespace of its own.
//
// It is `base` rather than `gpui` so that a module which is a port of a crate
// that has never heard of gpui can be written against this and nothing else.
// `src/taffy` and `src/markdown` are those modules: they include this header,
// they name `base::Str` and `base::Arena`, and they reach for no other part
// of the tree. gpui itself pulls the whole namespace in with a using-directive
// (see the top of gpui.h), so its own code writes `Str` unqualified the way it
// always has, and `gpui::Str` still names this type for a caller outside.
namespace base {

// Array count for source files compiled independently. The declaration is
// enough: sizeof only needs the returned reference's bound and never calls it.
template <typename T, size_t N>
char (&DimofSizeHelper(T (&array)[N]) noexcept)[N];
#ifndef dimof
#define dimof(array) (sizeof(base::DimofSizeHelper(array)))
#endif

// The longest path we put on the stack. Windows spells it MAX_PATH; Linux
// PATH_MAX, which is 4096 and too big for the fixed arrays here.
enum : uint16_t {
    kMaxPath = 1024
};

struct Arena;

struct Str {
    char* s;
    int len;

    constexpr Str() noexcept : s(nullptr), len(0) {}
    // Explicit, so that a `const char*` cannot become a Str by accident:
    // a string literal should go through StrL(), which knows the length at
    // compile time, and a runtime pointer should say `Str(p)` where it means
    // "walk this to its NUL". SumatraPDF's Str is explicit for the same
    // reason.
    explicit Str(const char* s_) : s((char*)s_), len(0) {
        len = s_ ? (int)strlen(s_) : 0;
    }
    constexpr explicit Str(const char* s_, int len_) noexcept
        : s((char*)s_), len(len_) {}
    explicit Str(char* s_) : s(s_), len(0) { len = s ? (int)strlen(s) : 0; }
    constexpr explicit Str(char* s_, int len_) noexcept : s(s_), len(len_) {}

    explicit operator bool() const { return len > 0 && s; }
};

// Parse a floating-point value from a string slice. The input is assumed to
// contain a valid number; malformed or empty input returns zero.
float StrToFloatUnchecked(Str s);

void log(Str s);

using TempStr = Str;

#define StrL(lit) ::base::Str{(char*)(lit), (int)dimof(lit) - 1}

TempStr AllocStrTemp(int size);
TempStr StrDupTemp(Str s);
TempStr ReadBoundedFileTemp(Str path, int limit);

#if GPUI_OS_WINDOWS
// UTF-8 Str -> null-terminated UTF-16 for the OS calls that need it.
// Temp-arena backed: do not free, and do not keep it past the next
// ResetTempArena().
WCHAR* ToCWstrTemp(Str s);
#endif

// ─── platform layer ──────────────────────────────────────────────────────
//
// Implemented in Base_win.cpp / Base_linux.cpp.

uint64_t PlatPageSize();
uint64_t PlatLargePageSize();
// How much address space an arena reserves when nothing says otherwise. A
// host with real virtual memory reserves far more than it will ever touch,
// because untouched reservations are free there; wasm has no reserve/commit
// split at all — a reservation is an allocation — so it answers with what
// it is willing to spend. An arena that outgrows its reserve chains another
// block, so this is a first guess and never a limit.
uint64_t PlatArenaReserveSize();
// Reserve address space without backing it. Returns null on failure.
void* PlatMemReserve(uint64_t size);
bool PlatMemCommit(void* base, uint64_t size, bool largePages);
void* PlatMemReserveCommit(uint64_t size, bool largePages);
void PlatMemRelease(void* base, uint64_t size);

// Case-insensitive ASCII compares and a bounded copy: the CRT spells these
// _stricmp / _strnicmp / strncpy_s on Windows and strcasecmp / strncasecmp /
// strlcpy elsewhere.
int StrCmpI(const char* a, const char* b);
int StrCmpNI(const char* a, const char* b, int n);
// Copies at most cap-1 bytes and always null-terminates.
void StrCopyZ(char* dst, int cap, const char* src);

// The filesystem bits the asset loader needs; reading a file is plain stdio.
bool PlatDirExists(const char* path);
// Whether a file is there, without reading it: the question the asset
// lookup asks, and it asks it many times a frame.
bool PlatFileExists(const char* path);
void PlatGetCwd(char* out, int cap);
// Resolves an existing file or directory to an absolute path, following
// symlinks/reparse points. False includes a result that would not fit.
bool PlatCanonicalPath(const char* path, char* out, int cap);
// The directory the running binary sits in.
void PlatGetExeDir(char* out, int cap);

// One entry of a directory listing.
struct DirEntry {
    char name[260] = {};
    bool isDir = false;
    bool isFile = false;
    bool isSymlink = false;
    uint64_t size = 0;
    uint64_t modified = 0;
};

// Lists `dir`, skipping "." and "..". Returns how many entries were written,
// 0 if the directory could not be read.
int PlatListDir(const char* dir, DirEntry* out, int max);

// Logical cores, at least 1.
int PlatCoreCount();
// This process' own CPU time in 100 ns units and its resident set, for the
// FPS HUD. False if the OS would not say.
bool PlatSelfUsage(uint64_t* cpu100ns, uint64_t* memBytes);

void* AllocZero(int count, int size);

template <typename T>
inline T* AllocArray(int n) {
    return (T*)AllocZero(n, (int)sizeof(T));
}

template <typename T>
inline void ZeroStruct(T* s) {
    memset((void*)s, 0, sizeof(T));
}

struct Func0 {
    // A function that takes nothing at all still has to have something in
    // userData, and Func1 keeps a flag in that word's lowest bit, so the
    // sentinel is even: ~1 rather than -1.
    static constexpr uintptr_t kFuncNoArg = ~(uintptr_t)1;

    void* fn = nullptr;
    uintptr_t userData = 0;

    Func0() = default;

    bool IsValid() const { return fn != nullptr; }
    void Call() const {
        if (!fn) {
            return;
        }
        if (userData == kFuncNoArg) {
            auto func = (void (*)())fn;
            func();
            return;
        }
        auto func = (void (*)(uintptr_t))fn;
        func(userData);
    }
};

template <typename T>
Func0 MkFunc0(void (*fn)(T*), T* d) {
    auto res = Func0{};
    res.fn = (void*)fn;
    res.userData = (uintptr_t)d;
    return res;
}

// A function with nothing to close over. Everything that takes a Func0 takes
// one of these too, so a caller with no state does not have to invent a
// pointer to pass.
inline Func0 MkFunc0Void(void (*fn)()) {
    auto res = Func0{};
    res.fn = (void*)fn;
    res.userData = Func0::kFuncNoArg;
    return res;
}

template <typename T>
struct Func1 {
    // Bit 0 of userData says the function does not look at the argument, so
    // Call() drops it — that is how a Func0 stands in for a Func1. Everything
    // stored here is at least 2-byte aligned and kFuncNoArg is even, so the
    // bit is free and the struct stays two words.
    static constexpr uintptr_t kDropsArgBit = 1;
    static constexpr uintptr_t kFuncNoArg = Func0::kFuncNoArg;

    // Untyped, like Func0's, because Call below reads it as three different
    // signatures and gcc's -Wcast-function-type refuses a cast from one
    // function type straight to another. Every one of them goes through the
    // void* instead, which is the cast MkFunc0 has always made.
    void* fn = nullptr;
    uintptr_t userData = 0;

    Func1() = default;
    // A Func0 is a Func1 that ignores its argument. Implicit on purpose: a
    // queue of Func1 is then also a queue of Func0.
    Func1(const Func0& that) { // NOLINT
        this->fn = that.fn;
        this->SetData(that.userData, true);
    }

    void SetData(uintptr_t d, bool dropsArg) {
        userData = d | (dropsArg ? kDropsArgBit : 0);
    }
    bool IsValid() const { return fn != nullptr; }
    void Call(T arg) const {
        if (!fn) {
            return;
        }
        uintptr_t d = userData & ~kDropsArgBit;
        if (userData & kDropsArgBit) {
            if (d == kFuncNoArg) {
                auto func = (void (*)())fn;
                func();
            } else {
                auto func = (void (*)(uintptr_t))fn;
                func(d);
            }
            return;
        }
        if (d == kFuncNoArg) {
            auto func = (void (*)(T))fn;
            func(arg);
            return;
        }
        auto func = (void (*)(uintptr_t, T))fn;
        func(d, arg);
    }
};

template <typename T1, typename T2>
Func1<T2> MkFunc1(void (*fn)(T1*, T2), T1* d) {
    auto res = Func1<T2>{};
    res.fn = (void*)fn;
    res.SetData((uintptr_t)d, false);
    return res;
}

// The argument and nothing else.
template <typename T2>
Func1<T2> MkFunc1Void(void (*fn)(T2)) {
    auto res = Func1<T2>{};
    res.fn = (void*)fn;
    res.SetData(Func1<T2>::kFuncNoArg, false);
    return res;
}

// A plain non-recursive lock.
struct Mutex {
#if GPUI_OS_WINDOWS
    SRWLOCK lock = SRWLOCK_INIT;
    void Lock() { AcquireSRWLockExclusive(&lock); }
    void Unlock() { ReleaseSRWLockExclusive(&lock); }
#else
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    void Lock() { pthread_mutex_lock(&lock); }
    void Unlock() { pthread_mutex_unlock(&lock); }
#endif
    Mutex() = default;
    ~Mutex() = default;
};

// The other half of a lock: sleep until someone says there is something to
// do. A worker that spun on its queue instead would burn a core for as long
// as the process ran, so the pool in sys/executor.h waits on one of these.
//
// Wait() must be called with `m` held, and re-takes it before returning. A
// wake can arrive with nothing to show for it, which is why every Wait() in
// this tree sits inside a loop over the condition it is waiting for.
struct CondVar {
#if GPUI_OS_WINDOWS
    CONDITION_VARIABLE cv = CONDITION_VARIABLE_INIT;
    void Wait(Mutex* m, int timeoutMs) {
        DWORD t = timeoutMs < 0 ? INFINITE : (DWORD)timeoutMs;
        SleepConditionVariableSRW(&cv, &m->lock, t, 0);
    }
    void WakeOne() { WakeConditionVariable(&cv); }
    void WakeAll() { WakeAllConditionVariable(&cv); }
#else
    pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
    void Wait(Mutex* m, int timeoutMs);
    void WakeOne() { pthread_cond_signal(&cv); }
    void WakeAll() { pthread_cond_broadcast(&cv); }
#endif
    CondVar() = default;
    ~CondVar() = default;
};

// ─── threads ─────────────────────────────────────────────────────────────
//
// Everything this tree runs off the main thread runs through sys/executor.h;
// these three are what that is built out of, and are not otherwise called.

// Runs `f` on a thread of its own and forgets it. Nothing here joins a
// thread: a worker reports itself by writing somewhere both sides can see.
// False if the OS would not start one, and then `f` never runs.
bool PlatThreadRun(Func0 f);
// Identifies the calling thread. Only ever compared, never interpreted.
uint64_t PlatThreadId();
void PlatSleepMs(int ms);

static const uint64_t kArenaHeaderSize = 256;

struct Arena {
    Arena* prev;
    Arena* current;
    uint64_t flags;
    uint64_t commitChunkSize;
    uint64_t reserveChunkSize;
    uint64_t basePos;
    uint64_t pos;
    uint64_t committed;
    uint64_t reserved;
    const char* allocationSiteFile;
    int allocationSiteLine;
    const char* name;
    bool usesExternalBuffer;
    Mutex lock;
    uint64_t nAllocsLifetime;
    uint64_t peakBytesLifetime;
    uint64_t nAllocsSinceReset;
    uint64_t peakBytesSinceReset;

    void* Alloc(int size);
    void Reset();
    void* Push(uint64_t size, uint64_t align = 8, bool zero = true);
    void PopTo(uint64_t pos);

    Arena() = delete;
    ~Arena() = delete;
};

Arena* ArenaNew();
void ArenaDelete(Arena* arena);

// How many bytes the arena has handed out, across every block in its chain.
// The chain shares one position space — a block's `basePos` is where it
// starts in it — so this is the number a caller means by "how big did it
// get".
uint64_t ArenaUsed(Arena* arena);

// ─── a number that costs as few bytes as it needs ─────────────────────────
//
// LEB128: seven bits to a byte, low bits first, the high bit saying another
// follows. A value under 128 is one byte, which is the whole reason to have
// it — a length written ahead of what it measures is nearly always small,
// and four bytes for it is three wasted.
int VarintSize(uint32_t v);
// Writes `v` and answers how many bytes that took. `dst` needs
// `VarintSize(v)` of room, which is at most five.
int VarintPut(char* dst, uint32_t v);
// Reads one back, answering how many bytes it was.
int VarintGet(const char* src, uint32_t* out);

// ─── a string that costs four bytes ───────────────────────────────────────
//
// A `Str` is a pointer and a length: sixteen bytes once the compiler has
// padded it. A structure that holds several of them and is allocated by the
// thousand — an mdast `Node` holds eight — spends most of its size on
// pointers into an arena it already knows the name of.
//
// An ArenaStr is four bytes: where the string starts in the arena's position
// space, and nothing else. The length lives with the bytes, varint-encoded
// ahead of them, so a string shorter than 128 characters — which is almost
// all of them — carries it in one byte:
//
//     [varint len][len bytes][NUL]
//
// A quarter of what a Str costs in the holder, at one byte in the arena: the
// four saved in every reference against the one spent once with the bytes.
// The NUL is still there, so a caller that needs a C string has one.
//
// The offset is the same position space `PopTo` and `ArenaPtr` take, so it
// stays valid as the arena chains onto a new block. Nothing may be freed out
// from under it: an ArenaStr into an arena that has been reset or popped
// past it is a dangling pointer spelled differently.
using ArenaStr = uint32_t;

constexpr ArenaStr kArenaStrNone = 0;

// Whether it names anything. A zero-length string that was allocated is still
// a string — `IsSet` asks whether one was stored at all, which is what a
// `Str` with a null `s` means.
constexpr bool ArenaStrIsSet(ArenaStr s) {
    return s != kArenaStrNone;
}

// Copies `src` into `a` and answers where it went. An empty or null `src`
// answers kArenaStrNone without allocating.
ArenaStr ArenaStrDup(Arena* a, Str src);

// How long it is, without going near the characters. One byte of the arena
// read and decoded, which is a load the caller was going to do anyway if it
// wanted the bytes.
uint32_t ArenaStrLen(Arena* a, ArenaStr s);

// Growing one. A string built a piece at a time — a text node's value, an
// autolink's URL — costs only the bytes appended, so long as it is still the
// newest thing in the arena: `more` is pushed straight onto its end and the
// length grows in place.
//
// That is the case worth having. Concatenating instead copies the whole
// accumulated value every time, so a value built from N pieces leaves N-1
// dead copies behind and costs O(total^2 / piece) bytes of arena. When the
// string is not the newest — something else allocated in between — this
// falls back to exactly that copy, so it is never wrong, only sometimes no
// better.
//
// The terminator is what makes the in-place path fit: the byte holding it is
// already the string's, so `more.len` new bytes are room for `more.len`
// characters and a new terminator. Crossing 128 characters costs one more,
// for the second byte the length prefix grows to, and shifts the characters
// over to make room for it.
ArenaStr ArenaStrAppend(Arena* a, ArenaStr s, Str more);

// Reading one back. The Str points into the arena and lives exactly as long
// as the arena's contents do.
Str ArenaStrGet(Arena* a, ArenaStr s);

// ─── a pointer that costs four bytes ──────────────────────────────────────
//
// The same trade as ArenaStr, for a pointer to anything else the arena
// holds. A tree whose nodes point at each other spends eight bytes a link on
// an address the arena can work out from four, and a vec of those links is
// half the size for it.
//
// The offset is into the same position space `PopTo` and ArenaStr use, so it
// survives the arena chaining onto a new block. Zero is null: a block's
// header sits at its own offset zero, so no allocation ever lands there.
constexpr uint32_t kArenaPtrNone = 0;

// Where `p` sits in `a`'s position space. A walk of the chain by address —
// short, and the newest block is where a just-allocated object is. Answers
// kArenaPtrNone for a pointer that is not in this arena, which is better
// than an offset into somebody else's bytes.
uint32_t ArenaOffsetOf(Arena* a, const void* p);

// And back. Null for kArenaPtrNone, or for an offset past what the arena
// holds.
//
// Inline, and not because it is short: ArenaVec follows one of these per
// append and per segment step, so a call here would be a call on the hot
// path of every parse. The chain is walked from the newest block back, and
// an arena that never outgrew its first reserve — which is most of them —
// leaves the loop on its first test with `basePos` zero, so the whole thing
// is two loads and an add.
inline void* ArenaAtOffset(Arena* a, uint32_t off) {
    if (off == kArenaPtrNone || !a) {
        return nullptr;
    }
    Arena* node = a->current;
    // The newest block, which is where all but a handful of offsets land and
    // the only block at all for an arena that never outgrew its reserve. Split
    // out so the common case is a compare and an add and the walk is a branch
    // the predictor never takes.
    if (node && node->basePos <= (uint64_t)off) {
        return (char*)node + ((uint64_t)off - node->basePos);
    }
    while (node && node->basePos > (uint64_t)off) {
        node = node->prev;
    }
    return node ? (char*)node + ((uint64_t)off - node->basePos) : nullptr;
}

// The typed wrapper. Four bytes, trivially copyable, and `T` is there to
// keep a node offset from being read back as an event offset.
template <typename T>
struct ArenaPtr {
    uint32_t off = kArenaPtrNone;

    bool IsSet() const { return off != kArenaPtrNone; }
    bool operator==(const ArenaPtr<T>& o) const { return off == o.off; }
    bool operator!=(const ArenaPtr<T>& o) const { return off != o.off; }
};

template <typename T>
ArenaPtr<T> ArenaPtrOf(Arena* a, const T* p) {
    return ArenaPtr<T>{ArenaOffsetOf(a, p)};
}

template <typename T>
T* ArenaPtrGet(Arena* a, ArenaPtr<T> p) {
    return (T*)ArenaAtOffset(a, p.off);
}

// The per-frame scratch arena that fmt() / AllocStrTemp() carve out of.
Arena* GetTempArena();
void ResetTempArena();
void DestroyTempArena();

void* Alloc(struct Arena* arena, int size);
void Free(struct Arena* arena, void* mem);

template <typename T, typename... Args>
T* ArenaNew(Arena* arena, Args&&... args) {
    void* mem = Alloc(arena, (int)sizeof(T));
    return new (mem) T(std::forward<Args>(args)...);
}

// Kept out of line so the Vec<T> templates stay small at every use site.
#if GPUI_OS_WINDOWS
#define GPUI_NOINLINE __declspec(noinline)
#else
#define GPUI_NOINLINE __attribute__((noinline))
#endif

// One arena block for an ArenaVec segment: `hdrSize` bytes of header
// followed by `count` elements. Out of line so the template does not
// carry the overflow checks into every instantiation.
void* ArenaVecAlloc(struct Arena* a, int count, int elSize, int align,
                    int hdrSize = 0);

GPUI_NOINLINE bool VecRealloc(struct Arena* a, void** els, int len, int* cap,
                              int newCap, int elSize);

// Doubling, never from a first capacity of one. The byte-sensitive floor is
// the policy measured by cmd/vec-log.ts.
inline int VecNextCap(int cap, int wanted, int elSize) {
    if (cap == 0) {
        int floorCap = elSize == 1 ? 8 : elSize <= 1024 ? 4 : 1;
        return std::max(floorCap, wanted);
    }
    return std::max(cap * 2, wanted);
}

// A negative cap is borrowed storage; the magnitude is the real capacity.
inline int VecAbsCap(int cap) {
    return cap < 0 ? -cap : cap;
}

// ─── growth instrumentation (debug builds only) ──────────────────────────
//
// The question these answer: what should `Vec`'s doubling and `ArenaVec`'s
// 4/16/64 segment climb actually be, generically and at the sites that grow
// the most. So every vec carries a serial number and the source location it
// was declared at, and a run writes three kinds of line to `GPUI_VEC_LOG`:
//
//     B <id> <V|A> <elSize> <func> <file>:<line>   a vec came into being
//     G <id> <len> <oldCap> <needed> <newCap>      a Vec reallocated
//     S <id> <len> <want> <lastSegCap> <newSegCap> <totalCap> <reused>
//                                                  a segment was taken
//     D <id> <len> <cap>                           a Vec was destroyed
//     E <id> <len> <totalCap> <segCount>           an ArenaVec was destroyed
//
// `needed` and `want` are what the caller asked for, not what the policy
// handed out, which is what makes the log enough to replay a run against a
// different policy without rebuilding.
//
// Only `B` prints the location; the others carry the id and the analysis
// joins on it. That is deliberate — an `ArenaVec` that lives in arena memory
// may never have run a constructor, so its `dbgFile` is whatever was in that
// memory, and dereferencing it would be a crash. An id of 0 says exactly
// that, and `NextSegment` hands such a vec a real id lazily.
//
// A member vec's location is the closing brace of the struct that holds it —
// the implicitly-defined constructor is what runs the default arguments — so
// several members of one struct land on one line. `func` is that struct's
// name, and the element size tells the rest apart.
//
// The location comes from `__builtin_FILE()` / `__builtin_LINE()` as default
// arguments, which all three compilers evaluate at the *call* site — the
// declaration of the vec, or of the constructor of the struct that holds one.
// (As a default member initializer MSVC evaluates them here in base.h
// instead, which names nothing.) The whole thing, fields included, is gone
// in a release build.
#if defined(DEBUG)
int VecDbgBirth(const char* file, int line, const char* func, char kind,
                int elSize) noexcept;
void VecDbgGrow(int id, int len, int oldCap, int needed, int newCap) noexcept;
void VecDbgSegment(int id, int len, int want, int lastSegCap, int newSegCap,
                   int totalCap, bool reused) noexcept;
void VecDbgDeath(int id, int len, int cap) noexcept;
void VecDbgArenaDeath(int id, int len, int totalCap, int segCount) noexcept;

// The trailing parameters, and the initializer that records them. Macros
// rather than an `#if` around each constructor, because a signature split
// across `#if` / `#else` with one body below it is not something
// clang-format can count braces through — it de-indents the rest of the
// struct. In a release build all three expand to nothing and the
// constructors read as they always did.
#define GPUI_VEC_DBG_ARGS0                                            \
    const char *dbgF = __builtin_FILE(), int dbgL = __builtin_LINE(), \
               const char *dbgFn = __builtin_FUNCTION()
#define GPUI_VEC_DBG_ARGS , GPUI_VEC_DBG_ARGS0
#define GPUI_VEC_DBG_INIT(kind) \
    : dbgId(VecDbgBirth(dbgF, dbgL, dbgFn, kind, (int)sizeof(T)))
#else
#define GPUI_VEC_DBG_ARGS0
#define GPUI_VEC_DBG_ARGS
#define GPUI_VEC_DBG_INIT(kind)
#endif

template <typename T>
struct Vec;

// Vec has no methods beyond what cannot be a free function (the ctors,
// operator=, the destructor, operator[] and begin/end). Everything else is a
// Vec*() free function, declared below in sections and defined after Vec in
// the same order.

// Makes a template parameter non-deduced, so VecAppend(Vec<Base*>&,
// Derived*) still picks T from the vec and converts the element.
template <typename T>
struct VecIdentity {
    using type = T;
};
template <typename T>
using VecIdentityT = typename VecIdentity<T>::type;

// Vec<T> with its element type erased. Vec<T>'s layout does not depend on T,
// so VecNT() is a cast rather than a copy and the shims below cost nothing
// beyond passing elSize. The storage bodies live in base.cpp and are compiled
// once rather than once per element type.
#if defined(__GNUC__) || defined(__clang__)
// The erased operations access a Vec<T> through this layout-compatible type.
// GCC's optimizer needs the aliasing contract stated explicitly; without it,
// an optimized Linux build can retain fields across VecReset() and free the
// old allocation a second time.
struct __attribute__((__may_alias__)) VecNonTemplated {
#else
struct VecNonTemplated {
#endif
    int len;
    int cap;
    void* els;
};

bool VecReserveNT(Arena* arena, VecNonTemplated* v, int elSize, int wantedSize);
void* VecInsertSpaceNT(VecNonTemplated* v, int elSize, int idx, int count);
bool VecResizeNT(VecNonTemplated* v, int elSize, int newSize);
void VecRemoveAtNT(VecNonTemplated* v, int elSize, int idx, int count);
void VecRemoveAtFastNT(VecNonTemplated* v, int elSize, int idx);
void VecFreeElementsNT(VecNonTemplated* v);
void VecClearNT(VecNonTemplated* v, int elSize);
void* VecTakeNT(VecNonTemplated* v, int elSize);
void VecCopyFromNT(VecNonTemplated* v, int elSize, int srcLen,
                   const void* srcEls, bool zeroTail);

template <typename T>
VecNonTemplated* VecNT(Vec<T>& v);

template <typename T>
struct Vec {
    int len = 0;
    // Negative means the elements sit in storage this vec does not own —
    // `VecUseExternalBuffer` put them in an array on the caller's stack — and
    // the capacity is `-cap`. The sign is only for the two places that have
    // to tell owned from borrowed, growing and freeing. It rides in the sign
    // rather than in a field of its own because a release Vec is 16 bytes and
    // is a member of a great many structs.
    //
    // A vec that borrows leaves the borrowed block alone forever: the first
    // append past it allocates and copies, and nothing frees the array. The
    // idioms elsewhere in the tree that take a vec's storage over by hand —
    // `events.els = t->events.els; events.cap = t->events.cap;` — are the one
    // thing this is not safe with, and none of them are handed one.
    int cap = 0;
    T* els = nullptr;
#if defined(DEBUG)
    int dbgId = 0;
#endif

    explicit Vec(GPUI_VEC_DBG_ARGS0) noexcept GPUI_VEC_DBG_INIT('V') {}

    // Still a copy constructor in a debug build: every parameter after the
    // first has a default, and those three are how the copy gets the
    // caller's location rather than this line in base.h.
    Vec(const Vec& other GPUI_VEC_DBG_ARGS) GPUI_VEC_DBG_INIT('V') {
        VecCopyFromNT(VecNT(*this), (int)sizeof(T), other.len,
                      (const void*)other.els, false);
    }

    Vec& operator=(const Vec& other) {
        if (this == &other) {
            return *this;
        }
        VecReset(*this);
        VecCopyFromNT(VecNT(*this), (int)sizeof(T), other.len,
                      (const void*)other.els, true);
        return *this;
    }

    ~Vec() {
#if defined(DEBUG)
        VecDbgDeath(dbgId, len, VecAbsCap(cap));
#endif
        VecReset(*this);
    }

    T& operator[](int idx) const { return els[idx]; }

    using iterator = T*;
    using const_iterator = const T*;
    iterator begin() { return els; }
    const_iterator begin() const { return els; }
    iterator end() { return els ? els + len : nullptr; }
    const_iterator end() const { return els ? els + len : nullptr; }
};

// The erased view is a cast, not a copy. Debug Vec adds one field after these
// three; their offsets still do not depend on T.
static_assert(offsetof(Vec<char>, len) == offsetof(VecNonTemplated, len));
static_assert(offsetof(Vec<char>, cap) == offsetof(VecNonTemplated, cap));
static_assert(offsetof(Vec<char>, els) == offsetof(VecNonTemplated, els));
static_assert(offsetof(Vec<double>, els) == offsetof(VecNonTemplated, els));
#if !defined(DEBUG)
static_assert(sizeof(Vec<char>) == sizeof(VecNonTemplated));
static_assert(sizeof(Vec<double>) == sizeof(VecNonTemplated));
#endif

template <typename T>
VecNonTemplated* VecNT(Vec<T>& v) {
    return (VecNonTemplated*)&v;
}

template <typename T>
auto VecReserve(Arena* arena, T& v, int n) -> decltype(v.els) {
    static_assert(offsetof(T, len) == offsetof(VecNonTemplated, len));
    static_assert(offsetof(T, cap) == offsetof(VecNonTemplated, cap));
    static_assert(offsetof(T, els) == offsetof(VecNonTemplated, els));
    if (!VecReserveNT(arena, (VecNonTemplated*)&v, (int)sizeof(*v.els), n)) {
        return nullptr;
    }
    return v.els;
}

// Lend `v` an array to start in, instead of its first allocation. The vec
// must be empty and must not have storage yet — this is for the line right
// after it is declared:
//
//     FlexLine lineBuf[4];
//     Vec<FlexLine> flexLines;
//     VecUseExternalBuffer(flexLines, lineBuf);
//
// It appends into `buf` until `buf` is full, and the append past that
// allocates and copies the way a first allocation would, leaving `buf`
// alone. Nothing frees `buf`, so it must outlive the vec — a local array in
// the same scope, which is the only shape this is for. The vec must not
// then have its storage taken over by hand (`other.els = v.els; other.cap =
// v.cap;`), since the sign is what says the block is not the heap's.
template <typename T, int N>
inline void VecUseExternalBuffer(Vec<T>& v, T (&buf)[N]) {
    v.els = buf;
    v.cap = -N;
    v.len = 0;
}

template <typename T>
inline T* VecReserve(Vec<T>& v, int n) {
#if defined(DEBUG)
    int curCap = VecAbsCap(v.cap);
    if (n > curCap) {
        VecDbgGrow(v.dbgId, v.len, curCap, n,
                   VecNextCap(curCap, n, (int)sizeof(T)));
    }
#endif
    return VecReserve(nullptr, v, n);
}

template <typename T>
T* VecInsertSpace(Vec<T>& v, int idx, int count) {
    return (T*)VecInsertSpaceNT(VecNT(v), (int)sizeof(T), idx, count);
}

template <typename T>
bool VecResize(Vec<T>& v, int newSize) {
    return VecResizeNT(VecNT(v), (int)sizeof(T), newSize);
}

template <typename T>
void VecClear(Vec<T>& v) {
    VecClearNT(VecNT(v), (int)sizeof(T));
}

template <typename T>
void VecReset(Vec<T>& v) {
    VecFreeElementsNT(VecNT(v));
}

template <typename T>
T* VecTake(Vec<T>& v) {
    return (T*)VecTakeNT(VecNT(v), (int)sizeof(T));
}

template <typename T>
bool VecAppend(Vec<T>& v, const VecIdentityT<T>& el) {
    return VecInsertAt(v, v.len, el);
}

template <typename T>
bool VecAppendVec(Vec<T>& v, const Vec<T>& other) {
    return VecAppendN(v, other.els, other.len);
}

template <typename T>
bool VecAppendN(Vec<T>& v, const T* src, int count) {
    if (count == 0) {
        return true;
    }
    T* dst = VecInsertSpace(v, v.len, count);
    if (!dst) {
        return false;
    }
    memcpy((void*)dst, (const void*)src, (size_t)count * sizeof(T));
    return true;
}

template <typename T>
T* VecAppendBlanks(Vec<T>& v, int count) {
    return VecInsertSpace(v, v.len, count);
}

template <typename T>
bool VecInsertAt(Vec<T>& v, int idx, const VecIdentityT<T>& el) {
    T* p = VecInsertSpace(v, idx, 1);
    if (!p) {
        return false;
    }
    p[0] = el;
    return true;
}

template <typename T, typename E>
bool VecPush(Arena* arena, T& v, E el) {
    if (!VecReserve(arena, v, v.len + 1)) {
        return false;
    }
    v.els[v.len++] = el;
    return true;
}

template <typename T>
void VecRemoveAtN(Vec<T>& v, int idx, int count) {
    VecRemoveAtNT(VecNT(v), (int)sizeof(T), idx, count);
}

template <typename T>
void VecRemoveAt(Vec<T>& v, int idx) {
    VecRemoveAtN(v, idx, 1);
}

template <typename T>
T VecPopAt(Vec<T>& v, int idx) {
    T el = v.els[idx];
    VecRemoveAt(v, idx);
    return el;
}

template <typename T>
void VecRemoveAtFast(Vec<T>& v, int idx) {
    VecRemoveAtFastNT(VecNT(v), (int)sizeof(T), idx);
}

template <typename T>
void VecRemoveLast(Vec<T>& v) {
    if (v.len > 0) {
        VecRemoveAt(v, v.len - 1);
    }
}

template <typename T>
T VecPop(Vec<T>& v) {
    T el = v.els[v.len - 1];
    VecRemoveAtFast(v, v.len - 1);
    return el;
}

template <typename T>
int VecRemove(Vec<T>& v, const T& el) {
    int i = VecFind(v, el);
    if (i >= 0) {
        VecRemoveAt(v, i);
    }
    return i;
}

template <typename T>
bool VecIsValidIndex(const Vec<T>& v, int idx) {
    return idx >= 0 && idx < v.len;
}

template <typename T>
T& VecLast(const Vec<T>& v) {
    return v.els[v.len - 1];
}

template <typename T>
int VecFind(const Vec<T>& v, const T& el, int startAt = 0) {
    for (int i = startAt; i < v.len; i++) {
        if (v.els[i] == el) {
            return i;
        }
    }
    return -1;
}

template <typename T>
bool VecContains(const Vec<T>& v, const T& el) {
    return VecFind(v, el) >= 0;
}

// An array that grows into an arena. `Vec<T>` frees its storage in its
// destructor, which is wrong for anything built on a frame arena: an element
// tree and the builders that make one are allocated from the frame's arena and
// thrown away with it, never destructed. This has no destructor and never
// frees — the arena is what owns the memory — so a builder can take as many
// items as the caller has instead of declaring a cap for them.
//
// It is a list of segments rather than one block that reallocates. An arena
// hands back only the allocation on top of it, so a `Vec`-style grow of
// anything that is not the newest thing in the arena copies the elements and
// abandons the old block: the elements are copied over and over and the
// abandoned blocks are never reused. Segments never move, so a build is one
// store per element and the only waste is the tail of the last segment.
// Parsing 64 KB of markdown took 3257 KB of scratch arena the old way and
// takes 1538 KB this way, in fewer allocations.
//
// Two things follow from segments never moving. A `T*` or a `T&` taken from
// the vec stays good across an append, which the flat version could not
// promise. And the elements are not contiguous, so there is no one array to
// hand to something that wants one — `Flatten` is that, and it costs nothing
// for a vec that never left its first segment, which is most of them.
//
// A segment's header is sixteen bytes rather than twenty-four, because the
// link to the next one is an ArenaPtr rather than a pointer — an offset into
// the arena's position space, four bytes instead of eight. An offset needs
// the arena to be read back, so the vec keeps one: that is what lets
// `operator[]`, `begin` and `end` stay the no-argument calls they were while
// the links inside them cost half. Parsing 64 KB of markdown takes 3.9% less
// scratch arena for it, and the same time. See the note at the end.
template <typename T>
struct ArenaVecSegment {
    // The segment after this one. An ArenaPtr and not an
    // `ArenaVecSegment<T>*`: four bytes rather than eight, which is a
    // quarter of a header that half a million of them are allocated with in
    // one markdown parse.
    //
    // An offset into the arena's position space, and deliberately not a
    // delta from this segment's own address, which would be cheaper to
    // follow: two blocks of one arena are two separate reservations, and
    // address-space layout can put them further apart than an int32 reaches.
    ArenaPtr<ArenaVecSegment<T>> next;
    // Index of this segment's first element in the vec: the sum of the
    // lengths before it. What `At` walks on, and all a truncated segment
    // needs to be handed its elements back.
    int base;
    int len;
    // Only the last segment ever takes an append, so a capacity per segment
    // looks redundant — but `Truncate` makes an earlier segment the active
    // one again and has to know how much room it has.
    int cap;

    // The elements sit right behind the header, in the same arena block, so
    // where they are is an add on a pointer the caller already has rather
    // than a field to store and load. The offset is a constant per T.
    // A function and not a constant: inside its own definition the class is
    // only complete in a member function body, and `sizeof` needs it.
    static constexpr int HeaderSize() {
        return ((int)sizeof(ArenaVecSegment<T>) + (int)alignof(T) - 1) &
               ~((int)alignof(T) - 1);
    }

    T* Els() const { return (T*)((char*)(void*)this + HeaderSize()); }
};

// The first three segment sizes, then doubling: 4, 16 and 64 elements, but
// never more than 64, 256 and 1024 bytes of them. The progression started at
// 16/64/256 elements and then at 4/16/64, and this is the third pass — most
// of these vecs are an edit map entry holding one or two events, or a node
// holding three children, and a first segment they never fill is arena spent
// for nothing. 42% of them stay empty and take no segment at all; 97% hold
// seven elements or fewer.
//
// The byte caps are there because one element count cannot be right for a
// 32-byte `Event` and a 4-byte index at the same time. They bind only on the
// wide elements — anything 16 bytes or narrower gets 4/16/64 exactly as
// before, so nothing that was already the right size moves. `bun
// cmd/vec-log.ts bench markdown` replays the parse against both: the caps
// take it from 21.2 MB of arena to 17.1 MB, and an `Event` list starts at
// two rather than four, which is the length most of them stop at.
//
// Sizing the *whole* progression in bytes (64B/256B/1KB, ignoring the
// counts) reaches the same total, but by making the narrow vecs bigger as
// well as the wide ones smaller — which grew the mdast output arena on the
// prose benchmark by 23%, since `to_mdast`'s two per-tree stacks live in it.
// Caps only, therefore: this can spend less memory than the counts did and
// never more.
//
// Revisit them the same way, against `bun cmd/bench.ts markdown` and the
// replay, not against taste.
// What the four-byte link bought, measured with `bun cmd/bench.ts markdown`,
// release, best of four runs of twenty samples. Every byte of it is a segment
// header that went from twenty-four to sixteen, times the segments a parse
// allocates:
//
//                      scratch arena              tree          parse (min)
//   prose            1280.9 -> 1230.4 KB   272.0 -> 259.6 KB   7.99 -> 7.92 ms
//   nested quotes     979.8 ->  925.0 KB   110.2 -> 110.2      8.88 -> 8.85
//   gfm tables       4010.1 -> 3830.5 KB   250.5 -> 250.5     11.96 -> 11.87
//   character refs    886.9 ->  878.4 KB    65.6 ->  65.6      5.92 -> 5.88
//
// The arena numbers are exact and the times are level — a shade faster, which
// is what a smaller header does to a cache and is not worth claiming as a
// speed-up. The one case that moved is `markdown/phases`' to_mdast, 0.298 ->
// 0.311 ms: it reads the top of a stack once an event and now translates an
// offset to do it. Layout does not move at all — taffy has no ArenaVec in it
// — and the story gallery's frame is the same to three decimal places, build,
// layout and paint alike.
//
// What is *not* here, and could be, is the handle. It is twenty-four bytes —
// an arena, two offsets and a length — and twelve of them go if the arena
// comes from the caller at every call instead of living here. That is what an
// edit map entry and a compile frame would each stop carrying, and neither of
// them is in an arena, so neither shows up in the table above at all.
//
// It was written that way first and taken back out. The saving is real and
// invisible: it does not move any number this tree measures, because the
// structures that hold a handle are on the heap. What it costs is visible in
// every file — an `Arena*` in the signature of everything that so much as
// reads a vec, `v.At(a, i)` and `v.In(a)` and `v.Pop(a)` at about a hundred
// call sites — and the arena each of those readers had to be handed is the
// one the vec itself already knew. If it ever comes back, it should come back
// for a structure that holds a handle *in* an arena, where the twelve bytes
// would show up in a benchmark.

constexpr int kArenaVecCap0 = 4;
constexpr int kArenaVecCap1 = 16;
constexpr int kArenaVecCap2 = 64;
constexpr int kArenaVecBytes0 = 64;
constexpr int kArenaVecBytes1 = 256;
constexpr int kArenaVecBytes2 = 1024;

template <typename T>
struct ArenaVec {
    using Segment = ArenaVecSegment<T>;

    // The segments holding the elements, oldest first. `last` is the one an
    // append goes into, so appending does not walk; segments past it are ones
    // a `Truncate` let go of, kept linked and empty so a pop and a push at a
    // segment boundary do not allocate a segment per pass.
    //
    // ArenaPtrs, four bytes each, beside the arena they are offsets into.
    // The arena is here rather than at every call because an offset is
    // meaningless without one, and threading it through `operator[]`, `begin`
    // and `end` put it in the signature of everything that so much as reads a
    // vec. It is filled in where a segment is made and nowhere else — every
    // mutator was handed it already — so a vec still declares as
    // `ArenaVec<T> v = {}` and still reads as `v[i]`. There and not on every
    // append, because an append that has room writes one element and should
    // not also write the handle: doing it per append cost 1% of the parse.
    //
    // Null until the first segment, which is what a vec with no segment
    // wants: ArenaPtrGet answers null for a null arena, so the first append
    // finds no segment and goes and asks for one.
    //
    // Eight bytes and two fours and a four is twenty-four, which is what the
    // handle always was. What the ArenaPtrs buy here is that carrying the
    // arena costs nothing: two real pointers beside it would have made this
    // thirty-two. The saving that shows up in an arena is the segment header
    // above, which is sixteen bytes rather than twenty-four.
    Arena* a = nullptr;
    ArenaPtr<Segment> first = {};
    ArenaPtr<Segment> last = {};
    int len = 0;
#if defined(DEBUG)
    // Zero for a vec that never ran a constructor — one that lives in a
    // `Vec<ArenaVec<T>>` slot, which `VecRealloc` only ever zeroed.
    // `NextSegment` gives one of those an id the first time it allocates.
    int dbgId = 0;
    // Kept rather than walked: the segments are arena memory, and a vec's
    // destructor can run after the arena it grew in is gone.
    int dbgTotalCap = 0;
    int dbgSegs = 0;

    // The defaulted parameters are the declaration site; see the
    // instrumentation comment above `struct Vec`. Both of these live inside
    // the `#if`, unlike `Vec`'s, because in a release build this type has no
    // constructor of its own at all — it is an aggregate, and every one of
    // the `ArenaVec<T> v = {}` declarations in the tree wants it to stay one.
    ArenaVec(GPUI_VEC_DBG_ARGS0) noexcept GPUI_VEC_DBG_INIT('A') {}

    ~ArenaVec() { VecDbgArenaDeath(dbgId, len, dbgTotalCap, dbgSegs); }
#endif

    // A walk over the segments, not the elements: one compare for a vec that
    // never left its first, one more per segment after that. Reading the
    // elements in order this way is what `Iter` is for — `v[i]` in a loop
    // starts the walk over every time.
    T& operator[](int idx) const {
        // Most vecs never leave their first segment, and `first == last`
        // says so from two offsets of the handle itself, without going to
        // the arena for either.
        Segment* seg = ArenaPtrGet(a, first);
        if (first == last) {
            return seg->Els()[idx];
        }
        while (idx >= seg->base + seg->len) {
            seg = ArenaPtrGet(a, seg->next);
        }
        return seg->Els()[idx - seg->base];
    }

    // Reading the elements in order. The whole state is the segment and the
    // index inside it, so stepping is `++idx` and a compare, and only the
    // step that leaves a segment touches a pointer:
    //
    //     for (const Event& e : entry.add) { ... }
    //
    // A `Truncate` can leave empty segments linked after the last one, so
    // both ends normalize past anything empty and `end()` is a null segment.
    struct Iter {
        // The vec's own, handed over by `begin`: an offset cannot be followed
        // without it, and the iterator follows one per segment step.
        Arena* a;
        // Resolved once per segment and then stepped through, so reading an
        // element is the same add it always was and the arena is consulted
        // only where the old version followed a `next` pointer.
        Segment* seg;
        int idx;

        void Normalize() {
            while (seg && idx >= seg->len) {
                seg = ArenaPtrGet(a, seg->next);
                idx = 0;
            }
        }
        T& operator*() const { return seg->Els()[idx]; }
        T* operator->() const { return &seg->Els()[idx]; }
        Iter& operator++() {
            idx++;
            if (idx >= seg->len) {
                seg = ArenaPtrGet(a, seg->next);
                idx = 0;
                Normalize();
            }
            return *this;
        }
        bool operator!=(const Iter& o) const {
            return seg != o.seg || idx != o.idx;
        }
    };

    Iter begin() const {
        Iter it = {a, ArenaPtrGet(a, first), 0};
        it.Normalize();
        return it;
    }
    Iter end() const { return Iter{a, nullptr, 0}; }

    bool Append(Arena* arena, const T& el) { return AppendMany(arena, &el, 1); }

    bool AppendMany(Arena* arena, const T* src, int n) {
        while (n > 0) {
            Segment* seg = ArenaPtrGet(a, last);
            if (!seg || seg->len >= seg->cap) {
                seg = NextSegment(arena, n);
                if (!seg) {
                    return false;
                }
            }
            int room = seg->cap - seg->len;
            int take = n < room ? n : room;
            for (int i = 0; i < take; i++) {
                seg->Els()[seg->len + i] = src[i];
            }
            seg->len += take;
            len += take;
            src += take;
            n -= take;
        }
        return true;
    }

    // Room for `n` more appends without allocating. A caller who knows the
    // count skips the 4/16/64 climb, which is all the segment sizes cost.
    bool Reserve(Arena* arena, int n) {
        Segment* seg = ArenaPtrGet(a, last);
        if (seg && seg->cap - seg->len >= n) {
            return true;
        }
        return NextSegment(arena, n) != nullptr;
    }

    // Drop everything from `newLen` on. The segments that held it stay linked
    // and empty, for the next appends to fill again.
    void Truncate(int newLen) {
        if (newLen < 0) {
            newLen = 0;
        }
        if (newLen >= len) {
            return;
        }
        ArenaPtr<Segment> at = first;
        Segment* seg = ArenaPtrGet(a, at);
        while (seg && seg->base + seg->len <= newLen) {
            at = seg->next;
            seg = ArenaPtrGet(a, at);
        }
        if (seg) {
            seg->len = newLen - seg->base;
            last = at;
            for (Segment* s = ArenaPtrGet(a, seg->next); s;
                 s = ArenaPtrGet(a, s->next)) {
                s->len = 0;
            }
        }
        len = newLen;
    }

    void Pop() { Truncate(len - 1); }

    // The elements as one array, for a callee that takes a `const T*`. Free
    // for a vec that never grew past its first segment; a copy into `a`
    // otherwise.
    T* Flatten(Arena* into) const {
        if (len == 0) {
            return nullptr;
        }
        if (first == last) {
            return ArenaPtrGet(a, first)->Els();
        }
        T* out = (T*)ArenaVecAlloc(into, len, (int)sizeof(T), (int)alignof(T));
        if (!out) {
            return nullptr;
        }
        int at = 0;
        for (const T& el : *this) {
            out[at++] = el;
        }
        return out;
    }

    // `count` elements, or as many as fit in `bytes`, whichever is fewer —
    // and at least one, so a T wider than the whole budget still gets a
    // segment it can hold something in. Constant-folded per instantiation,
    // since sizeof(T) is a constant.
    static constexpr int CapFor(int count, int bytes) {
        int byBytes = bytes / (int)sizeof(T);
        if (byBytes < 1) {
            byBytes = 1;
        }
        return byBytes < count ? byBytes : count;
    }

    static int NextCap(int prevCap) {
        const int steps[3] = {
            CapFor(kArenaVecCap0, kArenaVecBytes0),
            CapFor(kArenaVecCap1, kArenaVecBytes1),
            CapFor(kArenaVecCap2, kArenaVecBytes2),
        };
        for (int s : steps) {
            if (prevCap < s) {
                return s;
            }
        }
        return prevCap * 2;
    }

    // The segment the next append goes into, with room for at least `want`.
    // Off the hot path: once per segment, not once per element.
    GPUI_NOINLINE Segment* NextSegment(Arena* arena, int want) {
        a = arena;
#if defined(DEBUG)
        if (dbgId == 0) {
            dbgId = VecDbgBirth("<no-constructor>", 0, "", 'A', (int)sizeof(T));
        }
#endif
        Segment* lastSeg = ArenaPtrGet(a, last);
#if defined(DEBUG)
        int dbgPrevCap = lastSeg ? lastSeg->cap : 0;
#endif
        ArenaPtr<Segment> reuseAt =
            lastSeg ? lastSeg->next : ArenaPtr<Segment>{};
        Segment* reuse = ArenaPtrGet(a, reuseAt);
        if (reuse && reuse->cap >= want) {
            reuse->len = 0;
            reuse->base = len;
            last = reuseAt;
#if defined(DEBUG)
            VecDbgSegment(dbgId, len, want, dbgPrevCap, reuse->cap, dbgTotalCap,
                          true);
#endif
            return reuse;
        }
        int cap = NextCap(lastSeg ? lastSeg->cap : 0);
        if (cap < want) {
            cap = want;
        }
        int align = (int)alignof(T) > 8 ? (int)alignof(T) : 8;
        void* mem =
            ArenaVecAlloc(a, cap, (int)sizeof(T), align, Segment::HeaderSize());
        if (!mem) {
            return nullptr;
        }
        Segment* seg = (Segment*)mem;
        seg->next = {};
        seg->base = len;
        seg->len = 0;
        seg->cap = cap;
        ArenaPtr<Segment> at = ArenaPtrOf(a, seg);
        if (lastSeg) {
            lastSeg->next = at;
        } else {
            first = at;
        }
        last = at;
#if defined(DEBUG)
        dbgTotalCap += cap;
        dbgSegs++;
        VecDbgSegment(dbgId, len, want, dbgPrevCap, cap, dbgTotalCap, false);
#endif
        return seg;
    }
};

// ─── float geometry ──────────────────────────────────────────────────────
//
// One point, one size and one edge-set, shared by gpui and by the taffy port.
// They were two sets before: taffy's `SizeF`/`PointF`/`RectF` (its `Size<f32>`,
// `Point<f32>` and `Rect<f32>`) and gpui's `Size`/`Point`/`Edges`, identical
// in shape and converted at the seam between them. They are the same types
// now, and gpui keeps its own names for them as aliases, since `Size` and
// `Edges` read better in a widget than `SizeF` and `RectF` do.
//
// Only what both sides use is a method here. Everything taffy does with a
// flex direction or a writing-mode axis — `Main`, `Cross`, `SetMain`, the
// `Rect` edge pickers — is a free function in `namespace taffy`, because the
// axis enums are taffy's and have no business in the base.

// Rust's `Point<f32>`. With a rect, the top-left corner.
struct PointF {
    float x = 0.0f;
    float y = 0.0f;

    static constexpr PointF Zero() { return {0.0f, 0.0f}; }
};

constexpr bool operator==(PointF a, PointF b) {
    return a.x == b.x && a.y == b.y;
}

constexpr bool operator!=(PointF a, PointF b) {
    return !(a == b);
}

constexpr PointF operator+(PointF a, PointF b) {
    return {a.x + b.x, a.y + b.y};
}

// Rust's `Size<f32>`. Rust spells the fields `width` and `height`; they are
// `w` and `h` here, which is what gpui has always called them and what the
// taffy port's own `SizeDim` and `SizeAvail` — which are not this type — still
// spell out in full. Taffy's `SizeFOpt` *is* this type: an `Option<f32>` there
// is a NaN-tagged float, so `Size<Option<f32>>` is `Size<f32>`.
struct SizeF {
    float w = 0.0f;
    float h = 0.0f;

    static constexpr SizeF Zero() { return {0.0f, 0.0f}; }
};

constexpr bool operator==(SizeF a, SizeF b) {
    return a.w == b.w && a.h == b.h;
}

constexpr bool operator!=(SizeF a, SizeF b) {
    return !(a == b);
}

constexpr SizeF operator+(SizeF a, SizeF b) {
    return {a.w + b.w, a.h + b.h};
}

constexpr SizeF operator-(SizeF a, SizeF b) {
    return {a.w - b.w, a.h - b.h};
}

// Rust's `Rect<f32>`: an axis-aligned rectangle, or — which is all either
// caller uses it for — a set of four edge values, so gpui spells it `Edges`.
// The axis sums are the *edge totals*, not a width or a height.
struct RectF {
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;

    static constexpr RectF Zero() { return {0.0f, 0.0f, 0.0f, 0.0f}; }
    static constexpr RectF New(float l, float r, float t, float b) {
        return {l, r, t, b};
    }

    constexpr float HorizontalAxisSum() const { return left + right; }
    constexpr float VerticalAxisSum() const { return top + bottom; }
    constexpr SizeF SumAxes() const { return {left + right, top + bottom}; }
};

constexpr bool operator==(RectF a, RectF b) {
    return a.left == b.left && a.right == b.right && a.top == b.top &&
           a.bottom == b.bottom;
}

constexpr bool operator!=(RectF a, RectF b) {
    return !(a == b);
}

constexpr RectF operator+(RectF a, RectF b) {
    return {a.left + b.left, a.right + b.right, a.top + b.top,
            a.bottom + b.bottom};
}

// A calendar date, which is all the date widgets need out of the clock.
struct LocalDate {
    int year = 0;
    int month = 0; // 1..12
    int day = 0;   // 1..31
};

// Today, in local time.
LocalDate DateToday();
// `base` shifted by whole days, normalized. GPUI's date widgets get this from
// chrono's checked_add_days.
LocalDate DateAddDays(LocalDate base, int days);

void StrFree(Str s);
void StrFree(const char*) = delete;

Str StrDup(Arena*, Str str);
Str StrDup(Str s);
// Two strings in one heap block. `s1Out.s` is the allocation; `s2Out.s` is
// interior. Free with StrFree(s1Out) only — not `s2Out`, which is interior.
void StrDup2(Str s1, Str s2, Str& s1Out, Str& s2Out);

GPUI_NOINLINE bool StrEqRest(Str s1, Str s2);
inline bool StrEq(Str s1, Str s2) {
    if (s1.len != s2.len) {
        return false;
    }
    return StrEqRest(s1, s2);
}
inline bool StrEq(Str s1, const char* s2) {
    return StrEq(s1, Str(s2));
}
int StrCmp(Str s1, Str s2);
GPUI_NOINLINE bool StrEqIRest(Str s1, Str s2);
inline bool StrEqI(Str s1, Str s2) {
    if (s1.len != s2.len) {
        return false;
    }
    return StrEqIRest(s1, s2);
}
inline bool StrEqI(Str s1, const char* s2) {
    return StrEqI(s1, Str(s2));
}
bool StrStartsWith(Str s, Str prefix);
inline bool StrStartsWith(Str s, const char* prefix) {
    return StrStartsWith(s, Str(prefix));
}
bool StrStartsWithAny(Str s, const char* chars);
bool StrStartsWithI(Str s, Str prefix);
inline bool StrStartsWithI(Str s, const char* prefix) {
    return StrStartsWithI(s, Str(prefix));
}
bool StrEndsWith(Str s, Str suffix);
inline bool StrEndsWith(Str s, const char* suffix) {
    return StrEndsWith(s, Str(suffix));
}
bool StrEndsWithI(Str s, Str suffix);
inline bool StrEndsWithI(Str s, const char* suffix) {
    return StrEndsWithI(s, Str(suffix));
}
int StrFind(Str s, Str sub);
inline int StrFind(Str s, const char* sub) {
    return StrFind(s, Str(sub));
}
int StrFindI(Str s, Str sub);
inline int StrFindI(Str s, const char* sub) {
    return StrFindI(s, Str(sub));
}
inline bool StrContains(Str s, Str sub) {
    return StrFind(s, sub) >= 0;
}
inline bool StrContainsI(Str s, Str sub) {
    return StrFindI(s, sub) >= 0;
}
// Trims ASCII space, tab, newline, carriage return and form feed without
// allocating. The result is a slice into `s`.
Str StrTrimAscii(Str s);
// Replaces every non-overlapping occurrence of `from` with `to`. The result
// is temporary-arena backed; when `from` is empty or absent, `value` is
// returned unchanged.
Str StrReplaceAll(Str value, Str from, Str to);

// ─── sequential strings ───────────────────────────────────────────────────
//
// A run of NUL-terminated strings laid end to end, the run itself ended by an
// empty one:
//
//     "red green blue "
//
// which as a C literal already carries the final NUL. It is smaller than an
// array of `const char*` — no pointer per string and no relocation per
// pointer — and reading it is a linear scan, which the L1 cache is good at.
// The trade is that reaching the nth string means walking the ones before it,
// so this is for tables looked up rarely: a name to an index, an index back
// to a name. Ported from SumatraPDF's `src/base/Base.h`.
using SeqStrings = const char*;

// The first string in the run, or {} for an empty/null run.
Str SeqStrFirst(SeqStrings strs);
// The string after one returned by SeqStrFirst/SeqStrNext, or {} at the end.
// Its known length makes advancing a pointer addition rather than a scan.
Str SeqStrNext(Str s);
// Which string in the run this is, or -1. `IS` ignores case.
int SeqStrIndex(SeqStrings strs, Str toFind);
int SeqStrIndexIS(SeqStrings strs, Str toFind);
// Whether the run contains this string, ignoring case.
bool SeqStrContainsI(SeqStrings strs, Str toFind);
// The nth string, or {} past the end of the run.
Str SeqStrByIndex(SeqStrings strs, int idx);
// How many strings the run holds. Not one of Sumatra's — it is here because
// a run that parallels a table has to be as long as the table, and something
// has to be able to say so.
int SeqStrCount(SeqStrings strs);
// Lowercase A-Z in place. ASCII only, which is what the filters using it
// compare.
void StrLowerAscii(char* s);

// A Vec<char> that always keeps a NUL after the last char, so the storage is
// also a C string. The builder carries its allocator because every operation
// on one must use the same ownership model: null grows on the heap, otherwise
// storage belongs to the arena.
struct StrBuilder : Vec<char> {
    Arena* a = nullptr;

    explicit StrBuilder(Arena* arena = nullptr) : a(arena) {}

    void Reset(Str s = {});
    bool Reserve(int cap);
    bool AppendChar(char c);
    bool Append(Str src);
    char RemoveAt(int idx, int count = 1);
    char RemoveLast();
    Str TakeStr();
    char LastChar() const;
};

// Lend b a buffer to start in. One byte is held back for the NUL; growing
// past it allocates and copies, leaving the caller's storage alone.
void StrBuilderUseExternalBuffer(StrBuilder& b, Str buf);

struct FmtArg {
    enum class Kind : uint8_t {
        Char,
        Int,
        Ptr,
        Float,
        Double,
        Str,
        RawStr,
        Any,
        None,
    };

    Kind t{Kind::None};
    union {
        Str str;
        char c;
        int64_t i;
        float f;
        double d;
        const void* ptr;
    };

    FmtArg() : i{0} {}
    explicit FmtArg(char c_) : t{Kind::Char}, c{c_} {}
    explicit FmtArg(int arg) : t{Kind::Int}, i{(int64_t)arg} {}
    explicit FmtArg(unsigned int arg) : t{Kind::Int}, i{(int64_t)arg} {}
    explicit FmtArg(long arg) : t{Kind::Int}, i{(int64_t)arg} {}
    explicit FmtArg(unsigned long arg) : t{Kind::Int}, i{(int64_t)arg} {}
    explicit FmtArg(long long arg) : t{Kind::Int}, i{(int64_t)arg} {}
    explicit FmtArg(unsigned long long arg) : t{Kind::Int}, i{(int64_t)arg} {}
    explicit FmtArg(float f_) : t{Kind::Float}, f{f_} {}
    explicit FmtArg(double d_) : t{Kind::Double}, d{d_} {}
    explicit FmtArg(Str arg) : t{Kind::Str}, str{arg} {}
    explicit FmtArg(const void* p) : t{Kind::Ptr}, ptr{p} {}
    FmtArg(char*) = delete;
    FmtArg(const char*) = delete;
    FmtArg(wchar_t*) = delete;
    FmtArg(const wchar_t*) = delete;
};

TempStr FormatTempArgs(const char* fmt, const FmtArg** args, int nArgs);

inline TempStr FormatTemp(const char* fmt) {
    return FormatTempArgs(fmt, nullptr, 0);
}

template <typename... TArgs>
TempStr FormatTemp(const char* fmt, const TArgs&... args) {
    const FmtArg argv[] = {FmtArg(args)...};
    const FmtArg* argp[sizeof...(TArgs)];
    int n = (int)sizeof...(TArgs);
    for (int i = 0; i < n; i++) {
        argp[i] = &argv[i];
    }
    return FormatTempArgs(fmt, argp, n);
}

template <typename... TArgs>
inline TempStr fmt(const char* format, const TArgs&... args) {
    return FormatTemp(format, args...);
}

template <typename... TArgs>
inline void logf(const char* format, const TArgs&... args) {
    log(FormatTemp(format, args...));
}
} // namespace base
#endif // BASE_H_
