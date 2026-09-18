/* Shared example implementation of Base.h log(). */

#include "gpui.h"

#include <stdio.h>

using namespace gpui;

// out/gpui.log next to the binary's working directory, appended to for the
// life of the process.
static void LogToFile(Str s) {
    static FILE* f = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        f = fopen("out/gpui.log", "wb");
    }
    if (!f || !s.s || len(s) <= 0) {
        return;
    }
    fwrite(s.s, 1, (size_t)len(s), f);
    if (s.s[len(s) - 1] != '\n') {
        fwrite("\n", 1, 1, f);
    }
    fflush(f);
}

// The second sink is the attached debugger on Windows and stderr on Linux;
// both are what a developer running the example is already watching.
static void LogToConsole(Str s) {
#if GPUI_OS_WINDOWS
    if (s.s && len(s) > 0) {
        OutputDebugStringA(s.s);
        if (s.s[len(s) - 1] != '\n') {
            OutputDebugStringA("\n");
        }
    } else {
        OutputDebugStringA("\n");
    }
#else
    if (s.s && len(s) > 0) {
        fwrite(s.s, 1, (size_t)len(s), stderr);
        if (s.s[len(s) - 1] != '\n') {
            fputc('\n', stderr);
        }
    } else {
        fputc('\n', stderr);
    }
    fflush(stderr);
#endif
}

void base::log(Str s) {
    LogToConsole(s);
    LogToFile(s);
}
