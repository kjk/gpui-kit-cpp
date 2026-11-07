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
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    SysStateReset(s, (int)si.dwNumberOfProcessors);
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
    // Kernel time includes idle.
    SysCpuSample(s, FtToU64(idle), FtToU64(kernel) + FtToU64(user));
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
                SysDiskSet(s, total.QuadPart, freeBytes.QuadPart);
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

static void RefreshProcesses(SysState* s) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        return;
    }

    SysProcScan scan;
    PROCESSENTRY32W pe = {sizeof(pe)};
    FILETIME nowFt;
    GetSystemTimeAsFileTime(&nowFt);
    SysProcScanBegin(&scan, FtToU64(nowFt));

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
                    SysProcScanSample(s, &scan, &pi, FtToU64(k) + FtToU64(u));
                }
                CloseHandle(h);
            }
            VecAppend(scan.procs, pi);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    SysProcScanEnd(s, &scan);
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

// Rated cycles per second from the processor's ~MHz key. GetThreadTimes only
// moves in 10–16 ms quanta, which is coarser than a frame, so a per-frame
// sample from it is zero. QueryThreadCycleTime counts cycles the thread
// actually ran; dividing by the rated frequency is a stable milliseconds
// figure on one machine, not a cross-machine clock.
static double RatedCyclesPerSec() {
    static double hz = -1;
    if (hz >= 0) {
        return hz;
    }
    hz = 0;
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                      "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0,
                      KEY_READ, &key) == 0) {
        DWORD mhz = 0;
        DWORD size = sizeof(mhz);
        if (RegQueryValueExA(key, "~MHz", nullptr, nullptr, (BYTE*)&mhz,
                             &size) == 0 &&
            mhz > 0) {
            hz = (double)mhz * 1000000.0;
        }
        RegCloseKey(key);
    }
    return hz;
}

bool SysSelfThreadCpu(double* seconds) {
    double hz = RatedCyclesPerSec();
    ULONG64 cycles = 0;
    if (hz > 0 && QueryThreadCycleTime(GetCurrentThread(), &cycles)) {
        if (seconds) {
            *seconds = (double)cycles / hz;
        }
        return true;
    }
    FILETIME created, exited, kernel, user;
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel,
                        &user)) {
        return false;
    }
    if (seconds) {
        *seconds = (double)(FtToU64(kernel) + FtToU64(user)) / 10000000.0;
    }
    return true;
}

} // namespace gpui
