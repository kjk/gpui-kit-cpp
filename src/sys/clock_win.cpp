/* sys/clock.h's interval clock on Windows: QueryPerformanceCounter. */

#include "sys/clock.h"

#include <windows.h>

namespace gpui {

double ClockMonotonicSecondsSinceStart() {
    static LARGE_INTEGER freq = {};
    static LARGE_INTEGER start = {};
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }
    LARGE_INTEGER now = {};
    QueryPerformanceCounter(&now);
    if (freq.QuadPart == 0) {
        return 0;
    }
    return (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart;
}

} // namespace gpui
