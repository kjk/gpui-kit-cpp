#ifndef GPUI_GPUI_WINDOW_LOOP_H_
#define GPUI_GPUI_WINDOW_LOOP_H_
/* The steps of AppRun that the X11 and AppKit loops take the same way. Both
   own their event loop, keep a `dirty` flag and a `nextTick` deadline (TimeNow
   seconds, 0 when the timer is off) on their PlatWindow, and go round:

     drain the OS's events -> RunLoopRedrawDirty -> RunLoopWaitFor -> block
     on the OS until the wait ends -> ExecDrain -> RunLoopTickDue

   PlatWindow is a different struct in each window_*.cpp, so these are
   templates over it, instantiated where it is complete:
   `RunLoopRedrawDirty<PlatWindow>(app, Redraw)`. */

#include "gpui/platform.h"

namespace gpui {

// Draw every window that asked for a frame since the last pass.
template <typename Plat>
void RunLoopRedrawDirty(App* app, void (*redraw)(Window*)) {
    for (int i = 0; i < app->windows.len; i++) {
        Window* w = app->windows[i];
        Plat* plat = w->plat;
        if (plat && plat->dirty) {
            redraw(w);
        }
    }
}

// How long the loop may sleep: until the nearest window tick, and never more
// than a second. `*anyDirty` is set when a window still wants a frame, which
// means it may not sleep at all.
template <typename Plat>
double RunLoopWaitFor(App* app, bool* anyDirty) {
    double now = TimeNow();
    double waitS = 1.0;
    *anyDirty = false;
    for (int i = 0; i < app->windows.len; i++) {
        Window* w = app->windows[i];
        Plat* plat = w->plat;
        if (!plat) {
            continue;
        }
        if (plat->dirty) {
            *anyDirty = true;
        }
        if (plat->nextTick > 0) {
            double d = plat->nextTick - now;
            if (d < waitS) {
                waitS = d;
            }
        }
    }
    return waitS;
}

// Run the tick of every window whose deadline has passed.
template <typename Plat>
void RunLoopTickDue(App* app) {
    double now = TimeNow();
    for (int i = 0; i < app->windows.len; i++) {
        Window* w = app->windows[i];
        Plat* plat = w->plat;
        if (!plat || plat->nextTick <= 0) {
            continue;
        }
        if (now >= plat->nextTick) {
            // WindowTimerTick re-arms through PlatSetTimer.
            WindowTimerTick(w);
        }
    }
}

} // namespace gpui
#endif // GPUI_GPUI_WINDOW_LOOP_H_
