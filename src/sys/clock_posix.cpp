/* sys/clock.h on every POSIX target, the browser included: emscripten's libc
   answers CLOCK_MONOTONIC from performance.now(). */

#include "sys/clock.h"

#include <time.h>

namespace gpui {

uint64_t ClockMonotonicNs() {
    struct timespec ts = {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

double ClockMonotonicSecondsSinceStart() {
    static bool started = false;
    static struct timespec start = {};
    struct timespec now = {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!started) {
        start = now;
        started = true;
    }
    return (double)(now.tv_sec - start.tv_sec) +
           (double)(now.tv_nsec - start.tv_nsec) / 1e9;
}

bool ClockThreadCpuSeconds(double* seconds) {
    struct timespec ts = {};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) {
        return false;
    }
    if (seconds) {
        *seconds = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    }
    return true;
}

} // namespace gpui
