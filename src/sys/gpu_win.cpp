/* This process' share of the GPU, from the PDH performance counters —
 * crates/fps/src/gpu/windows.rs
 *
 * The `GPU Engine` counters are what Task Manager's GPU column reads. Each
 * instance is named after the process that owns it, so this process' share
 * falls out of the instance names and no driver SDK is involved: it works the
 * same on any vendor's adapter.
 */

#include "sys/gpu.h"

#include <windows.h>

#include <pdh.h>
// PDH_MORE_DATA and the rest of PDH's own status codes.
#include <pdhmsg.h>

namespace gpui {

// Every engine of every adapter, for every process; this process' instances
// are picked out of the result. PDH has no way to ask for one process' engines
// up front, since the instance name is not known until it is enumerated. The
// counter is registered under its English name so it resolves on a localized
// Windows too.
static const WCHAR* kCounterPath = L"\\GPU Engine(*)\\Utilization Percentage";

// Marks the end of an instance name's engine type, as in
// `pid_4242_luid_0x00000000_0x0000BEEF_phys_0_eng_1_engtype_3D`. The same name
// opens with the owning process, which is what `gOwner` matches.
static const char* kEngineType = "engtype_";

struct GpuProbe {
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    // How every instance name this process owns opens, as in `pid_4242_`.
    char owner[32] = {};
    int ownerLen = 0;
    // Reused across samples so a reading twice a second does not reallocate.
    // PDH writes the instance name strings into the tail of the same buffer,
    // past the array it fills, so this is sized in bytes.
    Vec<uint8_t> buffer;
    bool opened = false;
    bool failed = false;
};

static GpuProbe gProbe;
// FPS monitors can live in more than one window. Their samples run on the
// executor, while the PDH query and its reused result buffer are process-wide.
static Mutex gProbeLock;

// gProbeLock is held by every caller.
static bool ProbeOpenLocked() {
    if (gProbe.opened) {
        return true;
    }
    if (gProbe.failed) {
        return false;
    }
    gProbe.failed = true;
    if (PdhOpenQueryW(nullptr, 0, &gProbe.query) != ERROR_SUCCESS) {
        return false;
    }
    if (PdhAddEnglishCounterW(gProbe.query, kCounterPath, 0, &gProbe.counter) !=
        ERROR_SUCCESS) {
        PdhCloseQuery(gProbe.query);
        gProbe.query = nullptr;
        return false;
    }
    Str owner = fmt("pid_%d_", (int)GetCurrentProcessId());
    gProbe.ownerLen = len(owner) < (int)sizeof(gProbe.owner) - 1
                          ? len(owner)
                          : (int)sizeof(gProbe.owner) - 1;
    memcpy(gProbe.owner, owner.s, (size_t)gProbe.ownerLen);
    gProbe.owner[gProbe.ownerLen] = 0;

    // Utilization is a rate, so it is the difference between two collections.
    // This one is the baseline the first sample subtracts from; without it
    // that sample would read zero.
    PdhCollectQueryData(gProbe.query);
    gProbe.failed = false;
    gProbe.opened = true;
    return true;
}

bool GpuAvailable() {
    gProbeLock.Lock();
    bool available = ProbeOpenLocked();
    gProbeLock.Unlock();
    return available;
}

// The engine type an instance name ends with — `3D`, `Copy`, `VideoDecode` —
// or an empty Str when the name carries none.
static TempStr EngineOfTemp(const WCHAR* name) {
    int cap = (int)wcslen(name);
    TempStr buf = AllocStrTemp(cap);
    int n = 0;
    for (int i = 0; name[i] && n < cap; i++) {
        // Instance names are ASCII; anything else is not one of ours.
        buf.s[n++] = (char)(name[i] < 128 ? name[i] : '?');
    }
    buf.s[n] = 0;
    buf.len = n;
    int typeLen = (int)strlen(kEngineType);
    for (int i = n - typeLen; i >= 0; i--) {
        if (StrEq(Str(buf.s + i, typeLen), Str(kEngineType, typeLen))) {
            return Str(buf.s + i + typeLen, n - i - typeLen);
        }
    }
    return {};
}

// gProbeLock is held by the public wrapper for the full collection and buffer
// walk, so two background samplers cannot overwrite each other's PDH result.
static float GpuUsagePercentLocked() {
    if (!ProbeOpenLocked()) {
        return -1.f;
    }
    if (PdhCollectQueryData(gProbe.query) != ERROR_SUCCESS) {
        return -1.f;
    }

    // The first call reports the size it needs, which changes as processes
    // come and go.
    DWORD bytes = 0;
    DWORD count = 0;
    PDH_STATUS sized = PdhGetFormattedCounterArrayW(
        gProbe.counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr);
    // PDH_MORE_DATA is a DWORD constant, PDH_STATUS is a LONG; clang warns
    // on the signedness of the comparison unless the cast is spelled out.
    if (sized != (PDH_STATUS)PDH_MORE_DATA || bytes == 0) {
        return -1.f;
    }
    gProbe.buffer.len = 0;
    if (!VecAppendBlanks(gProbe.buffer, (int)bytes)) {
        return -1.f;
    }
    auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)&gProbe.buffer[0];
    if (PdhGetFormattedCounterArrayW(gProbe.counter, PDH_FMT_DOUBLE, &bytes,
                                     &count, items) != ERROR_SUCCESS) {
        return -1.f;
    }

    // The busiest engine *type* this process is using, not the sum over all of
    // them: 3D, Copy and Video Decode run concurrently, so adding them up can
    // pass 100% while the adapter still has headroom. Within one type this
    // process' engines are summed, since it can be on several at once.
    static constexpr int kMaxTypes = 16;
    Str names[kMaxTypes] = {};
    double busy[kMaxTypes] = {};
    int nTypes = 0;
    bool any = false;
    for (DWORD i = 0; i < count; i++) {
        const WCHAR* instance = items[i].szName;
        if (!instance) {
            continue;
        }
        TempStr whole = EngineOfTemp(instance);
        Str engine = whole;
        if (len(engine) == 0) {
            continue;
        }
        if (!StrStartsWith(whole, Str(gProbe.owner, gProbe.ownerLen))) {
            continue;
        }
        any = true;
        int slot = -1;
        for (int k = 0; k < nTypes; k++) {
            if (StrEq(names[k], engine)) {
                slot = k;
                break;
            }
        }
        if (slot < 0 && nTypes < kMaxTypes) {
            slot = nTypes++;
            names[slot] = StrDupTemp(engine);
            busy[slot] = 0;
        }
        if (slot >= 0) {
            busy[slot] += items[i].FmtValue.doubleValue;
        }
    }
    if (!any) {
        // Nothing means the adapter reported no engine of ours at all.
        return -1.f;
    }
    // Zero rather than nothing once the process has engines: it is idle, not
    // unmeasurable.
    double most = 0;
    for (int k = 0; k < nTypes; k++) {
        if (busy[k] > most) {
            most = busy[k];
        }
    }
    return (float)most;
}

float GpuUsagePercent() {
    gProbeLock.Lock();
    float usage = GpuUsagePercentLocked();
    gProbeLock.Unlock();
    return usage;
}

void GpuProbeFree() {
    gProbeLock.Lock();
    if (gProbe.query) {
        PdhCloseQuery(gProbe.query);
    }
    gProbe.query = nullptr;
    gProbe.counter = nullptr;
    VecReset(gProbe.buffer);
    gProbe.opened = false;
    gProbe.failed = false;
    gProbeLock.Unlock();
}

} // namespace gpui
