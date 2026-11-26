#ifndef GPUI_GPUI_ACCESSIBILITY_WIN_H_
#define GPUI_GPUI_ACCESSIBILITY_WIN_H_
/* Windows UI Automation adapter for Window::accessibility. */

#include "gpui/gpui.h"

namespace gpui {

#if GPUI_OS_WINDOWS
struct WinAccessibility;

WinAccessibility* AccessibilityWinNew(Window* win, void* hwnd);
void AccessibilityWinClose(WinAccessibility* accessibility);
intptr_t AccessibilityWinGetObject(WinAccessibility* accessibility,
                                   uintptr_t wParam, intptr_t lParam);
void AccessibilityWinTreeChanged(WinAccessibility* accessibility);
void AccessibilityWinFocusChanged(WinAccessibility* accessibility);
void AccessibilityWinHostFocusChanged(WinAccessibility* accessibility,
                                      bool focused);
#endif

} // namespace gpui
#endif // GPUI_GPUI_ACCESSIBILITY_WIN_H_
