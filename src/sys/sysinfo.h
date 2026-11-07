#ifndef GPUI_SYS_SYSINFO_H_
#define GPUI_SYS_SYSINFO_H_

#include "base.h"

namespace base {
int StrToIntUnchecked(Str s);
}

namespace gpui {

struct ProcessInfo {
    uint32_t pid = 0;
    char name[260] = {};
    float cpu = 0;
    uint64_t memory = 0;
};

struct ProcSample {
    uint32_t pid = 0;
    uint64_t cpu100ns = 0;
};

struct DiskInfo {
    float usedPct = 0;
    uint64_t total = 0;
    uint64_t used = 0;
};

struct BatteryInfo {
    bool present = false;
    bool charging = false;
    float pct = 0;
};

struct SysTimes {
    uint64_t idle = 0;
    uint64_t kernel = 0;
    uint64_t user = 0;
    bool valid = false;
};

struct SysState {
    SysTimes prevCpu;
    Vec<ProcSample> prevProcs;
    float cpu;
    float mem;
    uint64_t memTotal;
    uint64_t memUsed;
    DiskInfo disk;
    BatteryInfo battery;
    Vec<ProcessInfo> procs;
    int ncpu;

    SysState() : cpu(0), mem(0), memTotal(0), memUsed(0), ncpu(1) {
        ZeroStruct(&prevCpu);
        ZeroStruct(&disk);
        ZeroStruct(&battery);
    }
};

void SysStateInit(SysState* s);
void SysStateFree(SysState* s);
void SysRefresh(SysState* s);

enum class ProcessSort : uint8_t {
    Pid = 0,
    Name = 1,
    Cpu = 2,
    Memory = 3
};
void SysSortProcesses(SysState* s, ProcessSort field, bool descending,
                      int keepTop);

TempStr FormatBytesTemp(uint64_t bytes);
TempStr FormatPctTemp(float v, int decimals);

// crates/fps/src/memory.rs: how much memory this process is responsible for,
// as opposed to how many pages it happens to have mapped. The resident set
// counts the read-only pages of every shared library — on a windowed
// application the whole graphics stack, which every other window on the
// machine maps too, so the number moves when a *different* program starts.
// Each platform reads the counter its own activity monitor attributes to the
// process instead:
//
//   Windows  PrivateUsage from GetProcessMemoryInfo (Task Manager's commit)
//   Linux    RssAnon from /proc/self/status (resident anonymous memory)
//   macOS    ri_phys_footprint from proc_pid_rusage (Activity Monitor's
//            Memory column)
//
// Answers false where the platform publishes no such counter, or for a
// reading that is momentarily unavailable; the caller then falls back to the
// resident set PlatSelfUsage reports — a worse number, but a present one. The
// browser has none: a page only ever sees its own linear heap.
bool SysSelfPrivateMemory(uint64_t* bytes);

// CPU time this thread has consumed so far, user plus kernel, in seconds.
// The calling thread is the one that is measured, which for a frame is the
// main thread. False where the platform has no such clock (wasm). Examples
// call this instead of the platform API.
bool SysSelfThreadCpu(double* seconds);
} // namespace gpui
#endif // GPUI_SYS_SYSINFO_H_
