#include "ui/window_ext.h"
#include "ui/root.h"

namespace gpui {

WindowLayers::~WindowLayers() {
    if (app) {
        for (int i = 0; i < dialogs.len; i++) {
            EntityDrop(app, dialogs[i].view);
        }
        if (hasSheet) {
            EntityDrop(app, sheet.view);
        }
        if (notifications.IsValid()) {
            EntityDrop(app, notifications.id);
        }
    }
    if (win) {
        if (notifyTimer) {
            WindowCancelTimer(win, notifyTimer);
        }
        component::NotificationSystemDismissAll(win);
    }
    VecReset(dialogs);
}

WindowLayers* WindowLayersOf(Window* win) {
    // `window.root::<Root>()??.read(cx).plugin::<WindowState>()`.
    Root* root = Root::Read(win);
    if (!root) {
        return nullptr;
    }
    return (WindowLayers*)root->Plugin(&component::kWindowStatePlugin);
}

// WindowState::update / read: `Self::entity(window, cx).expect(ROOT_MISSING)`.
static WindowLayers* LayersOf(Ctx* cx) {
    WindowLayers* layers = cx ? WindowLayersOf(cx->win) : nullptr;
    if (!layers) {
        Panic(
            "component window state is missing; call component::Init "
            "before KitOpenWindow");
    }
    return layers;
}

// ─── dialogs ─────────────────────────────────────────────────────────────

void WindowOpenDialog(Ctx* cx, EntityId view, bool overlay) {
    WindowLayers* l = LayersOf(cx);
    if (!l || !EntityGet(cx->app, view)) {
        return;
    }
    // Opening the same dialog twice raises the one already up rather than
    // stacking a second copy of it.
    for (int i = 0; i < l->dialogs.len; i++) {
        if (l->dialogs[i].view == view) {
            return;
        }
    }
    WindowLayer layer;
    layer.view = view;
    layer.overlay = overlay;
    VecAppend(l->dialogs, layer);
    Notify(cx);
}

bool WindowHasActiveDialog(Ctx* cx) {
    return WindowDialogCount(cx) > 0;
}

int WindowDialogCount(Ctx* cx) {
    WindowLayers* l = LayersOf(cx);
    return l ? l->dialogs.len : 0;
}

void WindowCloseDialog(Ctx* cx) {
    WindowLayers* l = LayersOf(cx);
    if (!l || l->dialogs.len == 0) {
        return;
    }
    l->dialogs.len--;
    EntityDrop(cx->app, l->dialogs[l->dialogs.len].view);
    Notify(cx);
}

void WindowCloseAllDialogs(Ctx* cx) {
    WindowLayers* l = LayersOf(cx);
    if (!l || l->dialogs.len == 0) {
        return;
    }
    for (int i = 0; i < l->dialogs.len; i++) {
        EntityDrop(cx->app, l->dialogs[i].view);
    }
    l->dialogs.len = 0;
    Notify(cx);
}

// ─── the sheet ───────────────────────────────────────────────────────────

void WindowOpenSheetAt(Ctx* cx, EntityId view,
                       component::SheetPlacement placement, float size) {
    WindowLayers* l = LayersOf(cx);
    if (!l || !EntityGet(cx->app, view)) {
        return;
    }
    if (l->hasSheet && l->sheet.view != view) {
        EntityDrop(cx->app, l->sheet.view);
    }
    l->sheet.view = view;
    l->sheet.placement = placement;
    l->sheet.size = size;
    l->hasSheet = true;
    Notify(cx);
}

bool WindowHasActiveSheet(Ctx* cx) {
    WindowLayers* l = LayersOf(cx);
    return l && l->hasSheet;
}

void WindowCloseSheet(Ctx* cx) {
    WindowLayers* l = LayersOf(cx);
    if (!l || !l->hasSheet) {
        return;
    }
    l->hasSheet = false;
    EntityDrop(cx->app, l->sheet.view);
    l->sheet = {};
    Notify(cx);
}

// ─── notifications ───────────────────────────────────────────────────────

Entity<component::NotificationListState> WindowNotifications(Ctx* cx) {
    WindowLayers* l = LayersOf(cx);
    if (!l) {
        return {};
    }
    if (!l->notifications.IsValid()) {
        l->notifications =
            EntityNewState<component::NotificationListState>(cx->app);
        if (component::NotificationListState* st = l->notifications.Get(cx)) {
            st->useThemeSettings = true;
            // What a system notification's response is dispatched back to,
            // stamped here as well as at render because a push can come
            // before the list has ever drawn.
            st->self = l->notifications.id;
        }
        // The clock is not armed here. Rust used to spawn its 50 ms task from
        // `NotificationList::new`, so every window paid twenty wakeups a
        // second for the whole process whether or not a notification was ever
        // shown; it now starts the loop from `push` and ends it once the last
        // toast is unmounted. NotificationPush arms and the tick cancels, so
        // an idle window arms no timer.
    }
    return l->notifications;
}

int WindowPushNotification(Ctx* cx, component::Notification item,
                           int timeoutMs) {
    component::NotificationListState* st = WindowNotifications(cx).Get(cx);
    if (!st) {
        return 0;
    }
    int id = component::NotificationPush(st, cx, item, timeoutMs);
    Notify(cx);
    return id;
}

int WindowPushNotification(Ctx* cx, Str message) {
    component::Notification item = component::Notification::New();
    item.Message(message);
    return WindowPushNotification(cx, item, 5000);
}

int WindowPushNotification(Ctx* cx, component::NotificationType kind,
                           Str message) {
    component::Notification item = component::Notification::New();
    item.Message(message).WithType(kind);
    // Notification::timeout, Duration::from_secs(5).
    return WindowPushNotification(cx, item, 5000);
}

void WindowClearNotifications(Ctx* cx) {
    WindowLayers* l = LayersOf(cx);
    if (!l || !l->notifications.IsValid()) {
        return;
    }
    if (component::NotificationListState* st = l->notifications.Get(cx)) {
        component::NotificationClear(st, cx);
        Notify(cx);
    }
}

int WindowNotificationCount(Ctx* cx) {
    WindowLayers* l = LayersOf(cx);
    if (!l || !l->notifications.IsValid()) {
        return 0;
    }
    component::NotificationListState* st = l->notifications.Get(cx);
    return st ? st->items.len : 0;
}

void WindowRemoveNotifications(Ctx* cx, component::NotificationTypeId type) {
    WindowLayers* l = LayersOf(cx);
    if (!l || !l->notifications.IsValid()) {
        return;
    }
    if (component::NotificationListState* st = l->notifications.Get(cx)) {
        component::NotificationDismissByType(st, cx, type);
        Notify(cx);
    }
}

void WindowRemoveNotification1(Ctx* cx, component::NotificationTypeId type,
                               uint32_t key) {
    WindowLayers* l = LayersOf(cx);
    if (!l || !l->notifications.IsValid()) {
        return;
    }
    if (component::NotificationListState* st = l->notifications.Get(cx)) {
        component::NotificationDismissByTypeKey(st, cx, type, key);
        Notify(cx);
    }
}

// ─── the focused input ───────────────────────────────────────────────────

InputState* WindowFocusedInput(Ctx* cx) {
    if (!cx || !cx->win || !cx->win->input) {
        return nullptr;
    }
    Window* win = cx->win;
    InputState* input = win->input;
    bool registered = input->focused && input->focusWin == win;
    for (int i = 0; registered && i < win->paint.hits.len; i++) {
        if (win->paint.hits[i].input == input) {
            return input;
        }
    }
    // Rust drops a focused_input registration lazily when its focus handle
    // is no longer present. The frame's input hit records are that live
    // registration here; a focused state removed from the tree is blurred
    // on the first WindowExt query.
    InputBlur(input, cx->app, win);
    return nullptr;
}

bool WindowHasFocusedInput(Ctx* cx) {
    return WindowFocusedInput(cx) != nullptr;
}

} // namespace gpui
