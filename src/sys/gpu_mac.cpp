/* This process' share of the GPU, from IOKit accelerator clients —
 * crates/fps/src/gpu/macos.rs
 *
 * Sums `accumulatedGPUTime` this process' accelerator clients report in the
 * IO registry, and turns the time gained between two readings into a share of
 * the wall clock that passed. That is the counter behind Activity Monitor's
 * per-process GPU column, and reading it needs no privileges — unlike
 * `powermetrics`.
 *
 * The accelerator's own `PerformanceStatistics` would be easier to read but
 * is device wide, so it would report the compositor and every other
 * application as this one's load. `task_info`'s `task_gpu_utilisation` is
 * per process but is left at zero on Apple silicon, so it is no use either.
 */

#include "sys/gpu.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

#include <time.h>
#include <unistd.h>

#ifndef kIOMainPortDefault
#define kIOMainPortDefault kIOMasterPortDefault
#endif

namespace gpui {

struct GpuProbe {
    // How the registry tags the clients this process opened, as in
    // `pid 4242, my_app`. Only the pid is matched — the name is truncated to
    // 16 characters, so it is not reliable to compare against.
    CFStringRef creator = nullptr;
    uint64_t lastNs = 0;
    uint64_t lastAt = 0;
    bool primed = false;
    bool tried = false;
    bool available = false;
};

static GpuProbe gProbe;
// FPS monitors can live in more than one window. Their samples run on the
// executor, while the probe is process-wide.
static base::Mutex gProbeLock;

static uint64_t MonoNanos() {
    struct timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static bool NumberU64(CFNumberRef number, uint64_t* out) {
    if (!number || CFGetTypeID(number) != CFNumberGetTypeID()) {
        return false;
    }
    int64_t value = 0;
    if (!CFNumberGetValue(number, kCFNumberSInt64Type, &value)) {
        return false;
    }
    *out = value < 0 ? 0 : (uint64_t)value;
    return true;
}

// What one client has accumulated, or false if it belongs to another process
// or reports no usage at all — the latter being how an Intel Mac, which does
// not publish `AppUsage`, ends up with no GPU row.
static bool ClientNanoseconds(io_registry_entry_t client, CFStringRef creator,
                              uint64_t* out) {
    CFMutableDictionaryRef properties = nullptr;
    if (IORegistryEntryCreateCFProperties(
            client, &properties, kCFAllocatorDefault, 0) != KERN_SUCCESS ||
        !properties) {
        return false;
    }

    bool ok = false;
    auto owner = (CFStringRef)CFDictionaryGetValue(
        properties, CFSTR("IOUserClientCreator"));
    if (owner && CFGetTypeID(owner) == CFStringGetTypeID() &&
        CFStringHasPrefix(owner, creator)) {
        auto usage =
            (CFArrayRef)CFDictionaryGetValue(properties, CFSTR("AppUsage"));
        if (usage && CFGetTypeID(usage) == CFArrayGetTypeID()) {
            uint64_t total = 0;
            CFIndex n = CFArrayGetCount(usage);
            for (CFIndex i = 0; i < n; i++) {
                auto queue = (CFDictionaryRef)CFArrayGetValueAtIndex(usage, i);
                if (!queue || CFGetTypeID(queue) != CFDictionaryGetTypeID()) {
                    continue;
                }
                auto time = (CFNumberRef)CFDictionaryGetValue(
                    queue, CFSTR("accumulatedGPUTime"));
                uint64_t ns = 0;
                if (NumberU64(time, &ns)) {
                    total += ns;
                }
            }
            *out = total;
            ok = true;
        }
    }
    CFRelease(properties);
    return ok;
}

// Nanoseconds every accelerator has spent on this process, or false when it
// owns no client that reports them.
static bool AccumulatedNanoseconds(CFStringRef creator, uint64_t* out) {
    CFMutableDictionaryRef matching = IOServiceMatching("IOAccelerator");
    if (!matching) {
        return false;
    }
    io_iterator_t accelerators = IO_OBJECT_NULL;
    // IOServiceGetMatchingServices consumes `matching` on every path.
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching,
                                     &accelerators) != KERN_SUCCESS) {
        return false;
    }

    uint64_t total = 0;
    bool any = false;
    io_registry_entry_t accelerator = IO_OBJECT_NULL;
    while ((accelerator = IOIteratorNext(accelerators))) {
        io_iterator_t clients = IO_OBJECT_NULL;
        kern_return_t kr = IORegistryEntryGetChildIterator(
            accelerator, kIOServicePlane, &clients);
        IOObjectRelease(accelerator);
        if (kr != KERN_SUCCESS || clients == IO_OBJECT_NULL) {
            continue;
        }
        io_registry_entry_t client = IO_OBJECT_NULL;
        while ((client = IOIteratorNext(clients))) {
            uint64_t used = 0;
            if (ClientNanoseconds(client, creator, &used)) {
                total += used;
                any = true;
            }
            IOObjectRelease(client);
        }
        IOObjectRelease(clients);
    }
    IOObjectRelease(accelerators);
    if (!any) {
        return false;
    }
    *out = total;
    return true;
}

static bool HasAccelerators() {
    CFMutableDictionaryRef matching = IOServiceMatching("IOAccelerator");
    if (!matching) {
        return false;
    }
    io_iterator_t accelerators = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching,
                                     &accelerators) != KERN_SUCCESS) {
        return false;
    }
    io_registry_entry_t accelerator = IOIteratorNext(accelerators);
    bool any = accelerator != IO_OBJECT_NULL;
    if (accelerator) {
        IOObjectRelease(accelerator);
    }
    IOObjectRelease(accelerators);
    return any;
}

// gProbeLock is held by every caller.
static bool ProbeOpenLocked() {
    if (gProbe.tried) {
        return gProbe.available;
    }
    gProbe.tried = true;
    // Doubles as the support check: no accelerator, no readings, and the HUD
    // leaves the row out rather than showing a flat zero.
    if (!HasAccelerators()) {
        return false;
    }
    TempStr creator = fmt("pid %d,", (int)getpid());
    gProbe.creator = CFStringCreateWithCString(kCFAllocatorDefault, creator.s,
                                               kCFStringEncodingUTF8);
    if (!gProbe.creator) {
        return false;
    }
    gProbe.available = true;
    return true;
}

bool GpuAvailable() {
    gProbeLock.Lock();
    bool available = ProbeOpenLocked();
    gProbeLock.Unlock();
    return available;
}

static float GpuUsagePercentLocked() {
    if (!ProbeOpenLocked()) {
        return -1.f;
    }
    // `None` while this process has yet to open an accelerator client, which
    // is true for the moment between startup and the first frame.
    uint64_t used = 0;
    if (!AccumulatedNanoseconds(gProbe.creator, &used)) {
        return -1.f;
    }
    uint64_t now = MonoNanos();
    if (!gProbe.primed) {
        gProbe.lastNs = used;
        gProbe.lastAt = now;
        gProbe.primed = true;
        return -1.f;
    }
    uint64_t elapsed = now - gProbe.lastAt;
    uint64_t previous = gProbe.lastNs;
    gProbe.lastNs = used;
    gProbe.lastAt = now;
    if (elapsed == 0) {
        return -1.f;
    }
    // Saturating because a client that goes away takes its share of the
    // total with it, which can walk the sum backwards.
    uint64_t busy = used >= previous ? used - previous : 0;
    double percent = (double)busy / (double)elapsed * 100.0;
    percent = ClampD(percent, 0, 100);
    return (float)percent;
}

float GpuUsagePercent() {
    gProbeLock.Lock();
    float usage = GpuUsagePercentLocked();
    gProbeLock.Unlock();
    return usage;
}

void GpuProbeFree() {
    gProbeLock.Lock();
    if (gProbe.creator) {
        CFRelease(gProbe.creator);
    }
    gProbe.creator = nullptr;
    gProbe.lastNs = 0;
    gProbe.lastAt = 0;
    gProbe.primed = false;
    gProbe.tried = false;
    gProbe.available = false;
    gProbeLock.Unlock();
}

} // namespace gpui
