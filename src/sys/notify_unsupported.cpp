/* No system notification center, where GPUI_NOTIFY_UNSUPPORTED
   (sys/notify.h) says so. `SysNotifyAvailable` answers false and a post goes
   nowhere, so the system half is dropped; an `InAppAndSystem` notification
   keeps its in-app half.

   - macOS: UNUserNotificationCenter refuses to hand a notification to an
     application that is not a bundle in a location the system trusts —
     which is what Rust documents about its own macOS backend, and what an
     unbundled Rust build gets from the system center.
   - Linux: org.freedesktop.Notifications is a D-Bus service, and this tree
     carries no D-Bus client — the X11 half is Xlib, cairo and Pango and
     nothing else. Rust's Linux backend is itself the weakest of the three
     (it documents that a daemon must be running and that retraction is
     unsupported), and a system notification that cannot be retracted is one
     this component would have to describe as best-effort anyway. This is
     what a Linux host with no daemon gets.
   - wasm: the browser's Notification API exists, and it is the one platform
     here that would need a permission prompt driven by a user gesture before
     anything could be posted. Nothing in this tree asks for one. */

#include "sys/notify.h"

#if GPUI_NOTIFY_UNSUPPORTED

namespace gpui {

bool SysNotifyAvailable() {
    return false;
}
void SysNotifySetAppIdentity(Str, Str) {}
bool SysNotifyShow(Str, Str, Str) {
    return false;
}
void SysNotifyDismiss(Str) {}
void SysNotifyOnResponse(SysNotifyResponseFn) {}
void SysNotifyShutdown() {}

} // namespace gpui

#endif // GPUI_NOTIFY_UNSUPPORTED
