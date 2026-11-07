#include "sys/sysinfo.h"

namespace gpui {

static int CmpStrI(const char* a, const char* b) {
    return StrCmpI(a, b);
}

struct SortCtx {
    ProcessSort field;
    bool desc;
};

static int CmpProc(const ProcessInfo* a, const ProcessInfo* b,
                   ProcessSort field, bool desc) {
    int c = 0;
    switch (field) {
        case ProcessSort::Pid:
            c = (a->pid > b->pid) - (a->pid < b->pid);
            break;
        case ProcessSort::Name:
            c = CmpStrI(a->name, b->name);
            break;
        case ProcessSort::Cpu:
            c = (a->cpu > b->cpu) - (a->cpu < b->cpu);
            if (c == 0) {
                c = (a->memory > b->memory) - (a->memory < b->memory);
            }
            break;
        case ProcessSort::Memory:
            c = (a->memory > b->memory) - (a->memory < b->memory);
            break;
    }
    return desc ? -c : c;
}

static ProcessSort gSortField = ProcessSort::Cpu;
static bool gSortDesc = true;

static int QsortProc(const void* x, const void* y) {
    return CmpProc((const ProcessInfo*)x, (const ProcessInfo*)y, gSortField,
                   gSortDesc);
}

void SysSortProcesses(SysState* s, ProcessSort field, bool descending,
                      int keepTop) {
    if (s->procs.len <= 1) {
        return;
    }
    gSortField = field;
    gSortDesc = descending;
    qsort(s->procs.els, (size_t)s->procs.len, sizeof(ProcessInfo), QsortProc);
    if (keepTop > 0 && s->procs.len > keepTop) {
        s->procs.len = keepTop;
    }
}

void SysStateReset(SysState* s, int ncpu) {
    VecReset(s->prevProcs);
    VecReset(s->procs);
    s->cpu = 0;
    s->mem = 0;
    s->memTotal = 0;
    s->memUsed = 0;
    ZeroStruct(&s->prevCpu);
    ZeroStruct(&s->disk);
    ZeroStruct(&s->battery);
    s->ncpu = ncpu > 0 ? ncpu : 1;
}

void SysCpuSample(SysState* s, uint64_t idle, uint64_t total) {
    SysTimes cur;
    cur.idle = idle;
    // Windows splits kernel and user time, and its kernel time includes idle;
    // every platform hands over the sum, which goes in `kernel`.
    cur.kernel = total;
    cur.user = 0;
    cur.valid = true;
    if (s->prevCpu.valid) {
        uint64_t idleD = cur.idle - s->prevCpu.idle;
        uint64_t totalD = cur.kernel - s->prevCpu.kernel;
        if (totalD > 0) {
            double used = (double)(totalD - idleD) / (double)totalD;
            s->cpu = (float)(used * 100.0);
        }
    }
    s->prevCpu = cur;
}

void SysDiskSet(SysState* s, uint64_t total, uint64_t avail) {
    s->disk.total = total;
    s->disk.used = total > avail ? total - avail : 0;
    if (s->disk.total > 0) {
        s->disk.usedPct =
            (float)((double)s->disk.used * 100.0 / (double)s->disk.total);
    }
}

void SysMemorySet(SysState* s, uint64_t total, uint64_t avail) {
    if (avail > total) {
        avail = total;
    }
    s->memTotal = total;
    s->memUsed = total - avail;
    s->mem = (float)((double)s->memUsed * 100.0 / (double)total);
}

static uint64_t FindPrevCpu(const Vec<ProcSample>& prev, uint32_t pid) {
    for (int i = 0; i < len(prev); i++) {
        if (prev[i].pid == pid) {
            return prev[i].cpu100ns;
        }
    }
    return 0;
}

static uint64_t gPrevProcWall = 0;

void SysProcScanBegin(SysProcScan* scan, uint64_t wall100ns) {
    scan->wallDelta = (gPrevProcWall && wall100ns > gPrevProcWall)
                          ? (wall100ns - gPrevProcWall)
                          : 0;
    gPrevProcWall = wall100ns;
}

void SysProcScanSample(SysState* s, SysProcScan* scan, ProcessInfo* pi,
                       uint64_t cpu100ns) {
    uint64_t prev = FindPrevCpu(s->prevProcs, pi->pid);
    if (prev && scan->wallDelta > 0) {
        uint64_t delta = cpu100ns >= prev ? cpu100ns - prev : 0;
        pi->cpu = (float)((double)delta * 100.0 /
                          ((double)scan->wallDelta * (double)s->ncpu));
    }
    ProcSample sm;
    sm.pid = pi->pid;
    sm.cpu100ns = cpu100ns;
    VecAppend(scan->samples, sm);
}

void SysProcScanEnd(SysState* s, SysProcScan* scan) {
    VecReset(s->prevProcs);
    s->prevProcs = scan->samples;
    VecReset(s->procs);
    s->procs = scan->procs;
}

TempStr FormatBytesTemp(uint64_t bytes) {
    const uint64_t KB = 1024;
    const uint64_t MB = KB * 1024;
    const uint64_t GB = MB * 1024;
    if (bytes >= GB) {
        return fmt("%.1f GB", (double)bytes / (double)GB);
    }
    if (bytes >= MB) {
        return fmt("%.1f MB", (double)bytes / (double)MB);
    }
    if (bytes >= KB) {
        return fmt("%.1f KB", (double)bytes / (double)KB);
    }
    return fmt("%d B", (int)bytes);
}

TempStr FormatPctTemp(float v, int decimals) {
    if (decimals <= 0) {
        return fmt("%.0f%%", v);
    }
    return fmt("%.1f%%", v);
}
} // namespace gpui
