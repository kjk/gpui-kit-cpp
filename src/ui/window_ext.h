#ifndef GPUI_SRC_UI_WINDOW_EXT_H_
#define GPUI_SRC_UI_WINDOW_EXT_H_
/* WindowExt — crates/ui/src/window_ext.rs

   Rust's `Root` is the window's own view and it owns the layers drawn over
   the page: the stack of dialogs, the one sheet, and the notification list.
   `window.open_dialog(cx, ..)` pushes onto them, so any handler anywhere can
   raise a dialog without the view rendering the page knowing about it.

   Here they are the state of Component's WindowState plugin (`ui/root.h`),
   which the window's Base Root holds and renders over the page. A window
   whose root view is not a Base Root has none, and an operation on such a
   window panics, as Rust's does (`ROOT_MISSING`). The
   text-selection forwarding methods are gone, as they are in Rust:
   `TextSelection` in base answers for them.

   A layer is an entity, as it is in Rust: `WindowOpenDialog` takes the entity
   whose `Render` builds the dialog, and closing it lets the entity go. That
   is what makes the callback Rust stores — `Fn(Dialog, &mut Window, &mut App)
   -> Dialog` — unnecessary: a view with a Render is the same thing, and this
   tree already has one.

   C++ extension traits are subsystem-prefixed free functions. The complete
   WindowExt behavior is here; generic builders hand over an Entity whose
   Render is the retained Rust closure, and typed notification ids use the
   no-RTTI token declared by notification.h. */

#include "base/text_selection.h"
#include "ui/notification.h"
#include "ui/sheet.h"

namespace gpui {

// One open layer. A dialog uses `view` and `overlay`; a sheet uses `view`,
// `placement` and `size`.
struct WindowLayer {
    EntityId view = {};
    // Dialog::has_overlay — whether this one tints the page under it.
    bool overlay = true;
    component::SheetPlacement placement = component::SheetPlacement::Right;
    float size = 0;
};

// Root's `active_dialogs`, `active_sheet` and `notification`, kept on the
// window rather than on a view, since that is whose they are.
struct WindowLayers {
    App* app = nullptr;
    Window* win = nullptr;
    // As many as are open, in the order they were opened.
    Vec<WindowLayer> dialogs;
    WindowLayer sheet = {};
    bool hasSheet = false;
    // Created the first time something pushes one, along with the timer that
    // advances it — Rust spawns a task for that when Root is built.
    Entity<component::NotificationListState> notifications = {};
    // Retained for source compatibility; the list owns its own clock now and
    // arms it only while something is mounted.
    int notifyTimer = 0;

    ~WindowLayers();
};

// WindowState::entity: the store the window's Base Root holds. Null when the
// window's root view is not a Base Root; the operations below panic then.
WindowLayers* WindowLayersOf(Window* win);

// open_dialog. The entity's Render builds the dialog; it draws over
// everything the page put down, and over any dialog opened before it.
//
// The layer owns the entity from here on: closing the dialog drops it, which
// is what Rust's `Vec<Entity<Dialog>>` losing its last handle does. So the
// usual call hands over a freshly made entity and forgets about it.
void WindowOpenDialog(Ctx* cx, EntityId view, bool overlay = true);
template <typename T>
inline void WindowOpenDialog(Ctx* cx, Entity<T> e, bool overlay = true) {
    WindowOpenDialog(cx, e.id, overlay);
}
// open_alert_dialog: the entity's Render builds the opinionated AlertDialog
// surface, replacing Rust's retained build closure.
inline void WindowOpenAlertDialog(Ctx* cx, EntityId view, bool overlay = true) {
    WindowOpenDialog(cx, view, overlay);
}
template <typename T>
inline void WindowOpenAlertDialog(Ctx* cx, Entity<T> e, bool overlay = true) {
    WindowOpenDialog(cx, e.id, overlay);
}
bool WindowHasActiveDialog(Ctx* cx);
int WindowDialogCount(Ctx* cx);
// close_dialog: the topmost one. close_all_dialogs: the lot.
void WindowCloseDialog(Ctx* cx);
void WindowCloseAllDialogs(Ctx* cx);

// open_sheet_at. There is one sheet at a time, so opening a second replaces
// the first — and drops it — which is what assigning `active_sheet` does in
// Rust. The sheet's entity is the layer's, the way a dialog's is.
void WindowOpenSheetAt(Ctx* cx, EntityId view,
                       component::SheetPlacement placement, float size);
template <typename T>
inline void WindowOpenSheetAt(Ctx* cx, Entity<T> e,
                              component::SheetPlacement placement, float size) {
    WindowOpenSheetAt(cx, e.id, placement, size);
}
// open_sheet: Placement::Right, which is the default Rust picks.
inline void WindowOpenSheet(Ctx* cx, EntityId view, float size) {
    WindowOpenSheetAt(cx, view, component::SheetPlacement::Right, size);
}
template <typename T>
inline void WindowOpenSheet(Ctx* cx, Entity<T> e, float size) {
    WindowOpenSheetAt(cx, e.id, component::SheetPlacement::Right, size);
}
bool WindowHasActiveSheet(Ctx* cx);
void WindowCloseSheet(Ctx* cx);

// notifications(): the window's list, created along with its tick timer the
// first time it is asked for.
Entity<component::NotificationListState> WindowNotifications(Ctx* cx);
// push_notification. `timeoutMs` of 0 is `autohide(false)`; Rust's default is
// five seconds. Answers the notification's id.
int WindowPushNotification(Ctx* cx, component::Notification item,
                           int timeoutMs = -1);
int WindowPushNotification(Ctx* cx, Str message);
// The shorthand every caller wants: one message, of one kind.
int WindowPushNotification(Ctx* cx, component::NotificationType kind,
                           Str message);
void WindowClearNotifications(Ctx* cx);
int WindowNotificationCount(Ctx* cx);
void WindowRemoveNotifications(Ctx* cx, component::NotificationTypeId type);
void WindowRemoveNotification1(Ctx* cx, component::NotificationTypeId type,
                               uint32_t key);
template <typename T>
inline void WindowRemoveNotification(Ctx* cx) {
    WindowRemoveNotifications(cx, component::NotificationTypeOf<T>());
}
template <typename T>
inline void WindowRemoveNotification1(Ctx* cx, uint32_t key) {
    WindowRemoveNotification1(cx, component::NotificationTypeOf<T>(), key);
}
template <typename T>
inline void WindowRemoveNotification1(Ctx* cx, Str key) {
    WindowRemoveNotification1<T>(cx, (uint32_t)HashClickId(key));
}

// focused_input / has_focused_input. Rust's Root tracks the focused
// `AnyInputState`; the window here already routes keys to one, so this is
// that field under the name Rust gives it.
InputState* WindowFocusedInput(Ctx* cx);
bool WindowHasFocusedInput(Ctx* cx);

} // namespace gpui
#endif // GPUI_SRC_UI_WINDOW_EXT_H_
