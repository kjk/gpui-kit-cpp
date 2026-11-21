/* No native frame to place, where GPUI_WINDOW_FRAME_UNSUPPORTED
   (gpui/platform.h) says so: the browser draws one canvas in a page, and an
   iOS or Android host owns the view the app draws in. The getters answer
   empty bounds, the window is always visible and never the active one in
   the platform's sense, and the setters do nothing. */

#include "gpui/platform.h"

#if GPUI_WINDOW_FRAME_UNSUPPORTED

namespace gpui {

ScreenBounds WindowFrameBounds(Window*) {
    return {};
}

ScreenBounds WindowContentBounds(Window*) {
    return {};
}

void WindowSetFrameBounds(Window*, ScreenBounds) {}

ScreenBounds WindowFrameForContent(Window*, const WinOpts&,
                                   ScreenBounds content) {
    return content;
}

ScreenBounds WindowDisplayBounds(Window*) {
    return {};
}

ScreenBounds WindowDisplayVisibleBounds(Window*) {
    return {};
}

// The one surface has nothing of the app beside or above it.
Window* WindowAtPoint(Window* win, Point pt, Point* out) {
    if (!win || !win->plat) {
        return nullptr;
    }
    WinSize ws = WindowSize(win);
    if (pt.x < 0 || pt.y < 0 || pt.x >= ws.dipW || pt.y >= ws.dipH) {
        return nullptr;
    }
    if (out) {
        *out = pt;
    }
    return win;
}

bool WindowIsVisible(Window* win) {
    return win && win->plat;
}

void WindowActivateInApp(Window*) {}

bool WindowIsActive(Window*) {
    return false;
}

void WindowSetMinSize(Window*, Size) {}

void WindowSetMaxSize(Window*, Size) {}

void WindowSetFrameAppearance(Window*, FrameAppearance) {}

} // namespace gpui

#endif
