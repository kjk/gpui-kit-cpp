#ifndef GPUI_GPUI_ACCESSIBILITY_LINUX_H_
#define GPUI_GPUI_ACCESSIBILITY_LINUX_H_

#include "gpui/gpui.h"

namespace gpui {

#if GPUI_OS_LINUX
// Dependency-free AT-SPI adapter. The implementation speaks the small D-Bus
// subset an application-side provider needs and is drained by the X11 loop.
void AccessibilityLinuxInit(App* app, Str busAddress);
void AccessibilityLinuxShutdown();
int AccessibilityLinuxFd();
void AccessibilityLinuxPump();
bool AccessibilityLinuxReady();
void AccessibilityLinuxTreeChanged(Window* win);
void AccessibilityLinuxForgetWindow(Window* win);
void AccessibilityLinuxFocusChanged(Window* win, int focusId);
// The adapter keeps its geometry in window coordinates. X11 owns the one
// platform-specific conversion needed for AT-SPI's screen coordinates.
Point AccessibilityLinuxWindowOrigin(Window* win);
#endif

} // namespace gpui
#endif // GPUI_GPUI_ACCESSIBILITY_LINUX_H_
