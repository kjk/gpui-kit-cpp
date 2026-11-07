/* Browser system metrics, which is a short list.

   A page is sandboxed from the machine it is on by design: there is no
   process table, no /proc, no disk and no host CPU time. What it can see is
   its own tab, and that is what this reports — the wasm heap as memory, the
   Battery Status API where the browser still offers it, and
   navigator.hardwareConcurrency for the core count.

   The numbers a widget cannot get are left at zero rather than invented, so
   `system_monitor` on the web draws a real chart of its own memory, an empty
   process table and a disk bar at nothing. Every other example asks for none
   of this. */

#include "sys/sysinfo.h"

#include <emscripten/emscripten.h>
#include <emscripten/heap.h>

namespace gpui {

// clang-format off

EM_JS(int, GpJsCoreCount, (), {
    return navigator.hardwareConcurrency || 1;
});

// performance.memory is Chrome's, and the only place a page is told how much
// it is allowed to allocate. 0 where it is not offered, and the caller falls
// back to the heap the module has already grown to.
EM_JS(double, GpJsJsHeapLimit, (), {
    const m = performance.memory;
    return m && m.jsHeapSizeLimit ? m.jsHeapSizeLimit : 0;
});

// getBattery() answers a promise, and SysRefresh cannot wait for one, so the
// answer is parked on a global and read on a later refresh — which, at the
// 500 ms cadence system_monitor asks at, is the next one.
EM_JS(void, GpJsBatteryStart, (), {
    if (globalThis.__gpuiBattery !== undefined) {
        return;
    }
    globalThis.__gpuiBattery = null;
    if (!navigator.getBattery) {
        return;
    }
    navigator.getBattery().then(function(b) {
        const read = function() {
            globalThis.__gpuiBattery = {pct: b.level * 100, charging: b.charging};
        };
        read();
        b.addEventListener("levelchange", read);
        b.addEventListener("chargingchange", read);
    }).catch(function() {});
});

EM_JS(int, GpJsBatteryPresent, (), {
    return globalThis.__gpuiBattery ? 1 : 0;
});

EM_JS(double, GpJsBatteryPct, (), {
    const b = globalThis.__gpuiBattery;
    return b ? b.pct : 0;
});

EM_JS(int, GpJsBatteryCharging, (), {
    const b = globalThis.__gpuiBattery;
    return b && b.charging ? 1 : 0;
});
// clang-format on

void SysStateInit(SysState* s) {
    SysStateReset(s, GpJsCoreCount());
    GpJsBatteryStart();
}

void SysStateFree(SysState* s) {
    VecReset(s->prevProcs);
    VecReset(s->procs);
}

void SysRefresh(SysState* s) {
    if (!s) {
        return;
    }

    // The heap the module has grown to, against what the engine says it may
    // grow to. Without a limit to measure against, wasm32's 4 GiB ceiling is
    // the only honest denominator.
    uint64_t used = (uint64_t)emscripten_get_heap_size();
    double limit = GpJsJsHeapLimit();
    uint64_t total = limit > 0 ? (uint64_t)limit : 4ull * 1024 * 1024 * 1024;
    if (total < used) {
        total = used;
    }
    s->memTotal = total;
    s->memUsed = used;
    s->mem = total > 0 ? (float)((double)used / (double)total * 100.0) : 0.f;

    // No host CPU time to sample. A tab cannot see the machine's load and has
    // no share of it to report; leaving this at zero says so.
    s->cpu = 0;

    // No filesystem beyond the one the build preloaded into memory, and that
    // is already counted as memory.
    ZeroStruct(&s->disk);

    s->battery.present = GpJsBatteryPresent() != 0;
    s->battery.pct = s->battery.present ? (float)GpJsBatteryPct() : 0.f;
    s->battery.charging = GpJsBatteryCharging() != 0;

    // No process table: a page is not told what else is running, and there is
    // nothing to approximate it with.
    VecReset(s->procs);
}

// crates/fps/src/memory/unsupported.rs: a page publishes no private-memory
// counter, so the FPS HUD stays on the linear heap PlatSelfUsage reports.
bool SysSelfPrivateMemory(uint64_t* bytes) {
    (void)bytes;
    return false;
}

bool SysSelfThreadCpu(double* seconds) {
    (void)seconds;
    return false;
}

} // namespace gpui
