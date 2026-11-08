#ifndef GPUI_SYS_NOTIFY_H_
#define GPUI_SYS_NOTIFY_H_
/* The OS notification center — the platform half of gpui's SystemNotification.

   crates/ui/src/notification.rs bridges a notification to the system's own
   center through four calls on gpui's `App`: post one under a tag, retract
   one by that tag, learn the app's identity, and hear that the user clicked
   one. This is those four, and nothing else: no actions on a notification,
   no sound, no scheduling.

   What is behind them differs by more than the usual amount:

   - Windows: the shell's notification area. A hidden window owns one icon,
     and a post is a balloon on it, which Windows 10 and 11 turn into a real
     toast and keep in the Action Center. That is the whole of the WinRT
     toast API's benefit here without an app identity, a Start-menu shortcut
     or a COM activation server — the price is that only one balloon is on
     screen at a time (a second post replaces the first, which is what the
     tag semantics ask for anyway), that retraction takes the balloon off the
     screen but leaves what the Action Center has kept, and that the app has
     an icon in the notification area for as long as the process runs.
   - macOS, Linux, wasm: not implemented. `SysNotifyAvailable` answers false
     and the system half is dropped. `InAppAndSystem` still shows its in-app
     half; `System` intentionally remains system-only and therefore shows
     nothing, matching a Rust post rejected by an unavailable notification
     center.

   Rust's retraction on Linux is unsupported for the same reason it is
   partial here: the platform keeps what it has shown. */

#include "base.h"

// Not implemented: notify_unsupported.cpp answers for these, once. iOS and
// Android leave it to the host adapter.
#if GPUI_OS_MAC || GPUI_OS_LINUX || GPUI_OS_WASM
#define GPUI_NOTIFY_UNSUPPORTED 1
#else
#define GPUI_NOTIFY_UNSUPPORTED 0
#endif

namespace gpui {

// on_system_notification_response: the user clicked a notification, and this
// is the tag it was posted under. Called on the main thread, from whatever
// pumps the platform's events.
using SysNotifyResponseFn = Func1<Str>;

// Whether posts reach anything at all. False is not an error — the caller
// carries on without the system half.
bool SysNotifyAvailable();

// App::set_app_identity. Windows names the notification area icon with it;
// nothing else reads it yet. Call it before the first post.
void SysNotifySetAppIdentity(Str appId, Str appName);

// show_system_notification: post `title` / `body` under `tag`. A post with a
// tag already on screen replaces it. Answers false when nothing was posted.
bool SysNotifyShow(Str tag, Str title, Str body);

// dismiss_system_notification: retract the notification posted under `tag`,
// if it is still the one showing. A tag that was never posted is not an
// error.
void SysNotifyDismiss(Str tag);

// The single app-global handler, as gpui keeps a single one: a later call
// replaces an earlier. Null unregisters.
void SysNotifyOnResponse(SysNotifyResponseFn fn);

// Give back whatever the platform is holding — on Windows, the notification
// area icon. Called from AppFree's shutdown hooks; safe with nothing posted.
void SysNotifyShutdown();

} // namespace gpui
#endif // GPUI_SYS_NOTIFY_H_
