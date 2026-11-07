#ifndef GPUI_SYS_CLOCK_H_
#define GPUI_SYS_CLOCK_H_
/* The POSIX clocks, read one way for every non-Windows file that measures an
   interval: the frame clock behind TimeNow, the process table's wall time,
   the GPU probe's sampling window, a thread's CPU time. Defined in
   clock_posix.cpp, so Windows has no definition; its intervals come from
   QueryPerformanceCounter and FILETIMEs. */

#include "base.h"

namespace gpui {

// CLOCK_MONOTONIC in nanoseconds; 0 if the clock cannot be read.
uint64_t ClockMonotonicNs();

// Seconds of CLOCK_MONOTONIC since the first call in this process.
double ClockMonotonicSecondsSinceStart();

// CLOCK_THREAD_CPUTIME_ID: the calling thread's user plus kernel time, in
// seconds. False when the clock cannot be read.
bool ClockThreadCpuSeconds(double* seconds);

} // namespace gpui
#endif // GPUI_SYS_CLOCK_H_
