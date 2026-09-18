/* Linux system metrics, all of it out of /proc and /sys.

   The CPU numbers are jiffies from /proc/stat and /proc/<pid>/stat, which is
   what `top` reads; memory comes from /proc/meminfo, the disk from statvfs on
   "/", and the battery from /sys/class/power_supply. */

#include "sys/sysinfo.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace gpui {

// One tick of /proc CPU time, in the 100 ns units the Windows half reports.
static uint64_t TickTo100ns() {
    static uint64_t per = 0;
    if (per == 0) {
        long hz = sysconf(_SC_CLK_TCK);
        if (hz <= 0) {
            hz = 100;
        }
        per = 10000000ull / (uint64_t)hz;
    }
    return per;
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
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    s->ncpu = n > 0 ? (int)n : 1;
}

void SysStateFree(SysState* s) {
    VecReset(s->prevProcs);
    VecReset(s->procs);
}

static void RefreshCpu(SysState* s) {
    TempStr buf = ReadBoundedFileTemp(StrL("/proc/stat"), 511);
    if (!buf) {
        return;
    }
    // cpu  user nice system idle iowait irq softirq steal guest guest_nice
    unsigned long long v[10] = {};
    int n = sscanf(
        buf.s, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0],
        &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9]);
    if (n < 4) {
        return;
    }
    uint64_t idle = v[3] + v[4];
    uint64_t total = 0;
    for (int i = 0; i < n; i++) {
        total += v[i];
    }
    SysTimes cur;
    cur.idle = idle;
    // The Windows half splits kernel and user; here the whole total goes in
    // `kernel` (which includes idle there too) and `user` stays 0.
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

// The value of a "Key:  1234 kB" line, in bytes.
static uint64_t MeminfoKb(Str text, Str key) {
    int at = StrFind(text, key);
    if (at < 0) {
        return 0;
    }
    at += len(key);
    while (at < len(text) &&
           (text.s[at] == ' ' || text.s[at] == ':' || text.s[at] == '\t')) {
        at++;
    }
    uint64_t value = 0;
    while (at < len(text) && text.s[at] >= '0' && text.s[at] <= '9') {
        value = value * 10 + (uint64_t)(text.s[at++] - '0');
    }
    return value * 1024ull;
}

static void RefreshMemory(SysState* s) {
    TempStr buf = ReadBoundedFileTemp(StrL("/proc/meminfo"), 4095);
    if (!buf) {
        return;
    }
    uint64_t total = MeminfoKb(buf, StrL("MemTotal"));
    uint64_t avail = MeminfoKb(buf, StrL("MemAvailable"));
    if (total == 0) {
        return;
    }
    if (avail == 0) {
        // Pre-3.14 kernels: fall back to the free + reclaimable estimate.
        avail = MeminfoKb(buf, StrL("MemFree")) +
                MeminfoKb(buf, StrL("Cached")) +
                MeminfoKb(buf, StrL("Buffers"));
    }
    if (avail > total) {
        avail = total;
    }
    s->memTotal = total;
    s->memUsed = total - avail;
    s->mem = (float)((double)s->memUsed * 100.0 / (double)total);
}

static void RefreshDisk(SysState* s) {
    struct statvfs st = {};
    if (statvfs("/", &st) != 0 || st.f_blocks == 0) {
        return;
    }
    uint64_t unit = st.f_frsize ? st.f_frsize : st.f_bsize;
    s->disk.total = (uint64_t)st.f_blocks * unit;
    uint64_t avail = (uint64_t)st.f_bavail * unit;
    s->disk.used = s->disk.total > avail ? s->disk.total - avail : 0;
    if (s->disk.total > 0) {
        s->disk.usedPct =
            (float)((double)s->disk.used * 100.0 / (double)s->disk.total);
    }
}

static void RefreshBattery(SysState* s) {
    s->battery = {};
    DIR* d = opendir("/sys/class/power_supply");
    if (!d) {
        return;
    }
    struct dirent* ent = nullptr;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        TempStr path = fmt("/sys/class/power_supply/%s/type", Str(ent->d_name));
        TempStr buf = ReadBoundedFileTemp(path, 127);
        if (!buf) {
            continue;
        }
        if (!base::StrStartsWithI(Str(buf.s), "Battery")) {
            continue;
        }
        path = fmt("/sys/class/power_supply/%s/capacity", Str(ent->d_name));
        buf = ReadBoundedFileTemp(path, 127);
        if (!buf) {
            continue;
        }
        s->battery.present = true;
        s->battery.pct = (float)StrToIntUnchecked(Str(buf.s));
        path = fmt("/sys/class/power_supply/%s/status", Str(ent->d_name));
        buf = ReadBoundedFileTemp(path, 127);
        if (buf) {
            s->battery.charging = base::StrStartsWithI(Str(buf.s), "Charging");
        }
        break;
    }
    closedir(d);
}

static uint64_t FindPrevCpu(const Vec<ProcSample>& prev, uint32_t pid) {
    for (int i = 0; i < len(prev); i++) {
        if (prev[i].pid == pid) {
            return prev[i].cpu100ns;
        }
    }
    return 0;
}

// /proc/<pid>/stat: the process name is field 2 in parentheses and can itself
// contain spaces and parens, so everything after the last ')' is parsed by
// position.
static bool ReadProcStat(const char* text, ProcessInfo* pi, uint64_t* cpu) {
    const char* open = strchr(text, '(');
    const char* close = strrchr(text, ')');
    if (!open || !close || close < open) {
        return false;
    }
    int nameLen = (int)(close - open - 1);
    if (nameLen > (int)sizeof(pi->name) - 1) {
        nameLen = (int)sizeof(pi->name) - 1;
    }
    if (nameLen > 0) {
        memcpy(pi->name, open + 1, (size_t)nameLen);
    }
    pi->name[nameLen > 0 ? nameLen : 0] = 0;

    // After ") " come state (3) then the rest; utime is 14 and stime 15.
    const char* p = close + 1;
    unsigned long long utime = 0, stime = 0;
    long long rss = 0;
    int field = 2;
    while (*p) {
        while (*p == ' ') {
            p++;
        }
        if (!*p) {
            break;
        }
        field++;
        if (field == 14) {
            utime = strtoull(p, nullptr, 10);
        } else if (field == 15) {
            stime = strtoull(p, nullptr, 10);
        } else if (field == 24) {
            rss = strtoll(p, nullptr, 10);
            break;
        }
        while (*p && *p != ' ') {
            p++;
        }
    }
    *cpu = (utime + stime) * TickTo100ns();
    long page = sysconf(_SC_PAGESIZE);
    pi->memory =
        rss > 0 ? (uint64_t)rss * (uint64_t)(page > 0 ? page : 4096) : 0;
    return true;
}

static void RefreshProcesses(SysState* s) {
    DIR* d = opendir("/proc");
    if (!d) {
        return;
    }
    Vec<ProcessInfo> next;
    Vec<ProcSample> samples;

    // Wall clock in the same 100 ns units the CPU totals use.
    static uint64_t sPrevWall = 0;
    struct timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now =
        (uint64_t)ts.tv_sec * 10000000ull + (uint64_t)(ts.tv_nsec / 100);
    uint64_t wallDelta = (sPrevWall && now > sPrevWall) ? (now - sPrevWall) : 0;
    sPrevWall = now;

    struct dirent* ent = nullptr;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] < '0' || ent->d_name[0] > '9') {
            continue;
        }
        uint32_t pid = (uint32_t)strtoul(ent->d_name, nullptr, 10);
        if (pid == 0) {
            continue;
        }
        TempStr path = fmt("/proc/%u/stat", pid);
        TempStr buf = ReadBoundedFileTemp(path, 2047);
        if (!buf) {
            continue;
        }
        ProcessInfo pi;
        pi.pid = pid;
        uint64_t cpu = 0;
        if (!ReadProcStat(buf.s, &pi, &cpu)) {
            continue;
        }
        uint64_t prev = FindPrevCpu(s->prevProcs, pid);
        if (prev && wallDelta > 0) {
            uint64_t delta = cpu >= prev ? cpu - prev : 0;
            pi.cpu = (float)((double)delta * 100.0 /
                             ((double)wallDelta * (double)s->ncpu));
        }
        ProcSample sm;
        sm.pid = pid;
        sm.cpu100ns = cpu;
        VecAppend(samples, sm);
        VecAppend(next, pi);
    }
    closedir(d);

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

// crates/fps/src/memory/linux.rs. RssAnon out of /proc/self/status: the
// resident anonymous memory of this process — its heap, its thread stacks and
// every private mapping — and none of the files it maps. Split out of VmRSS
// in 4.5, which is old enough not to need a fallback.
//
// RssAnon rather than the Private_Dirty of /proc/self/smaps_rollup, which is
// the closer analogue of what macOS and Windows report: smaps_rollup walks
// every mapping under the address space lock, ~425us a read against ~5us for
// status on a windowed process, and a HUD should not perturb what it measures
// to account for a few megabytes of relocations. The kernel publishes the
// value in kibibytes, and every counter in the file is in that unit, so it is
// parsed as a fixed `kB` rather than read back.
bool SysSelfPrivateMemory(uint64_t* bytes) {
    TempStr buf = ReadBoundedFileTemp(StrL("/proc/self/status"), 8191);
    if (!buf) {
        return false;
    }
    Str key = StrL("RssAnon:");
    for (int line = 0; line < len(buf);) {
        Str remaining = Str(buf.s + line, len(buf) - line);
        if (StrStartsWith(remaining, key)) {
            int at = line + len(key);
            while (at < len(buf) && (buf.s[at] == ' ' || buf.s[at] == '\t')) {
                at++;
            }
            if (at >= len(buf) || buf.s[at] < '0' || buf.s[at] > '9') {
                return false;
            }
            uint64_t value = 0;
            while (at < len(buf) && buf.s[at] >= '0' && buf.s[at] <= '9') {
                value = value * 10 + (uint64_t)(buf.s[at++] - '0');
            }
            if (bytes) {
                *bytes = value * 1024ull;
            }
            return true;
        }
        int newline = StrFind(remaining, "\n");
        if (newline < 0) break;
        line += newline + 1;
    }
    return false;
}

} // namespace gpui
