/* Windows system metrics: toolhelp, psapi and the power/disk APIs. */

#include "sys/sysinfo.h"

#include <psapi.h>
#include <tlhelp32.h>

namespace gpui {

static uint64_t FtToU64(FILETIME ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

void SysStateInit(SysState* s) {
    VecReset(s->prevProcs);
    VecReset(s->procs);
    s->cpu = 0;
    s->mem = 0;
    s->memTotal = 0;
    s->memUsed = 0;
    ZeroStruct(&s->prevCpu);
    ZeroStruct(&s->disk);
    ZeroStruct(&s->battery);
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    s->ncpu = (int)si.dwNumberOfProcessors;
    if (s->ncpu < 1) {
        s->ncpu = 1;
    }
}

void SysStateFree(SysState* s) {
    VecReset(s->prevProcs);
    VecReset(s->procs);
}

static void RefreshCpu(SysState* s) {
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) {
        return;
    }
    SysTimes cur;
    cur.idle = FtToU64(idle);
    cur.kernel = FtToU64(kernel);
    cur.user = FtToU64(user);
    cur.valid = true;
    if (s->prevCpu.valid) {
        uint64_t idleD = cur.idle - s->prevCpu.idle;
        uint64_t totalD =
            (cur.kernel - s->prevCpu.kernel) + (cur.user - s->prevCpu.user);
        // kernel includes idle
        if (totalD > 0) {
            double used = (double)(totalD - idleD) / (double)totalD;
            s->cpu = (float)(used * 100.0);
        }
    }
    s->prevCpu = cur;
}

static void RefreshMemory(SysState* s) {
    MEMORYSTATUSEX ms = {sizeof(ms)};
    if (GlobalMemoryStatusEx(&ms)) {
        s->memTotal = ms.ullTotalPhys;
        s->memUsed = ms.ullTotalPhys - ms.ullAvailPhys;
        s->mem = (float)ms.dwMemoryLoad;
    }
}

static void RefreshDisk(SysState* s) {
    WCHAR drives[512];
    DWORD n = GetLogicalDriveStringsW(511, drives);
    if (n == 0 || n > 511) {
        return;
    }
    WCHAR* p = drives;
    while (*p) {
        UINT type = GetDriveTypeW(p);
        if (type == DRIVE_FIXED) {
            ULARGE_INTEGER freeBytes, total, totalFree;
            if (GetDiskFreeSpaceExW(p, &freeBytes, &total, &totalFree)) {
                s->disk.total = total.QuadPart;
                s->disk.used = total.QuadPart - freeBytes.QuadPart;
                if (s->disk.total > 0) {
                    s->disk.usedPct = (float)((double)s->disk.used * 100.0 /
                                              (double)s->disk.total);
                }
                return;
            }
        }
        p += wcslen(p) + 1;
    }
}

static void RefreshBattery(SysState* s) {
    SYSTEM_POWER_STATUS ps = {};
    s->battery = {};
    if (!GetSystemPowerStatus(&ps)) {
        return;
    }
    if (ps.BatteryFlag == 128 || ps.BatteryLifePercent == 255) {
        return;
    }
    s->battery.present = true;
    s->battery.charging = (ps.ACLineStatus == 1) && (ps.BatteryFlag & 8);
    s->battery.pct = (float)ps.BatteryLifePercent;
}

static uint64_t FindPrevCpu(const Vec<ProcSample>& prev, uint32_t pid) {
    for (int i = 0; i < len(prev); i++) {
        if (prev[i].pid == pid) {
            return prev[i].cpu100ns;
        }
    }
    return 0;
}

static void RefreshProcesses(SysState* s) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return;
    }

    Vec<ProcessInfo> next;
    Vec<ProcSample> samples;
    PROCESSENTRY32W pe = {sizeof(pe)};
    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    static uint64_t sPrevWall = 0;
    uint64_t now = FtToU64(nowFt);
    uint64_t wallDelta = (sPrevWall && now > sPrevWall) ? (now - sPrevWall) : 0;
    sPrevWall = now;

    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == 0) {
                continue;
            }
            ProcessInfo pi;
            pi.pid = pe.th32ProcessID;
            int n =
                WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, pi.name,
                                    (int)sizeof(pi.name) - 1, nullptr, nullptr);
            if (n <= 0) {
                pi.name[0] = 0;
            }

            HANDLE h =
                OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                            FALSE, pe.th32ProcessID);
            if (!h) {
                h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                pe.th32ProcessID);
            }
            if (h) {
                PROCESS_MEMORY_COUNTERS pmc = {sizeof(pmc)};
                if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) {
                    pi.memory = pmc.WorkingSetSize;
                }
                FILETIME c, e, k, u;
                if (GetProcessTimes(h, &c, &e, &k, &u)) {
                    uint64_t cpu = FtToU64(k) + FtToU64(u);
                    uint64_t prev = FindPrevCpu(s->prevProcs, pi.pid);
                    if (prev && wallDelta > 0) {
                        uint64_t d = cpu >= prev ? cpu - prev : 0;
                        pi.cpu = (float)((double)d * 100.0 /
                                         ((double)wallDelta * (double)s->ncpu));
                    }
                    ProcSample sm;
                    sm.pid = pi.pid;
                    sm.cpu100ns = cpu;
                    VecAppend(samples, sm);
                }
                CloseHandle(h);
            }
            VecAppend(next, pi);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    VecReset(s->prevProcs);
    s->prevProcs = samples;
    VecReset(s->procs);
    s->procs = next;
}

void SysRefresh(SysState* s) {
    RefreshCpu(s);
    RefreshMemory(s);
    RefreshDisk(s);
    RefreshBattery(s);
    RefreshProcesses(s);
}

// crates/fps/src/memory/windows.rs. PrivateUsage is this process' private
// commit: the pages it asked the memory manager for, and not the image pages
// its working set holds in common with every process mapping the same DLL.
// Private commit rather than the private *working set*, which is closer to
// what the other platforms report but is only reachable per page through
// QueryWorkingSetEx, a walk of the whole address space; the difference is the
// process' own memory that has been paged out, which is memory it is still
// responsible for.
//
// The call takes the shorter PROCESS_MEMORY_COUNTERS, and `cb` is what tells
// it the buffer is in fact the longer structure carrying PrivateUsage; the two
// share a prefix by definition, so the cast is the documented way to ask.
bool SysSelfPrivateMemory(uint64_t* bytes) {
    PROCESS_MEMORY_COUNTERS_EX counters = {};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              (PROCESS_MEMORY_COUNTERS*)&counters,
                              sizeof(counters))) {
        return false;
    }
    if (bytes) {
        *bytes = (uint64_t)counters.PrivateUsage;
    }
    return true;
}

} // namespace gpui
