#include "shell/root.h"

#include "base/dialog.h"
#include "base/sheet.h"
#include "ui/root.h"
#include "ui/theme.h"
#include "ui/window_ext.h"

namespace gpui {

static uint32_t ShellRootWindowKey() {
    return (uint32_t)HashClickId(StrL("gpui-shell-root"));
}

struct ShellRootWindowState {
    EntityId root = {};
};

static ShellRootWindowState* RootWindowState(Window* window) {
    if (!window) return nullptr;
    return (ShellRootWindowState*)WindowKeyedState(
        window, ShellRootWindowKey(), new ShellRootWindowState(),
        &EntityDropT<ShellRootWindowState>);
}

const char* ToastLevelName(ToastLevel level) {
    static const char names[] = "info\0success\0warning\0error\0";
    Str name = SeqStrByIndex(names, (int)level);
    return name ? name.s : names;
}

static const char kFpsAnchorNames[] =
    "top_left\0top_right\0bottom_left\0bottom_right\0top_center\0"
    "bottom_center\0left_center\0right_center\0";

SeqStrings FpsAnchorNames() {
    return kFpsAnchorNames;
}

bool FpsAnchorFromName(Str name, FpsAnchor* out) {
    if (StrEq(name, StrL("top_left")))
        *out = FpsAnchor::TopLeft;
    else if (StrEq(name, StrL("top_right")))
        *out = FpsAnchor::TopRight;
    else if (StrEq(name, StrL("bottom_left")))
        *out = FpsAnchor::BottomLeft;
    else if (StrEq(name, StrL("bottom_right")))
        *out = FpsAnchor::BottomRight;
    else if (StrEq(name, StrL("top_center")))
        *out = FpsAnchor::TopCenter;
    else if (StrEq(name, StrL("bottom_center")))
        *out = FpsAnchor::BottomCenter;
    else if (StrEq(name, StrL("left_center")))
        *out = FpsAnchor::LeftCenter;
    else if (StrEq(name, StrL("right_center")))
        *out = FpsAnchor::RightCenter;
    else
        return false;
    return true;
}

bool ToastLevelFromName(Str name, ToastLevel* out) {
    if (StrEq(name, StrL("info")))
        *out = ToastLevel::Info;
    else if (StrEq(name, StrL("success")))
        *out = ToastLevel::Success;
    else if (StrEq(name, StrL("warning")))
        *out = ToastLevel::Warning;
    else if (StrEq(name, StrL("error")))
        *out = ToastLevel::Error;
    else
        return false;
    return true;
}

// Rebuilds a script overlay's description before it draws.
//
// An overlay's content is a function, and what it closes over is somebody
// else's state — that is the contract open_dialog and open_sheet document, and
// the only one they can have: neither answers a view handle, so there is
// nothing for a script to notify when the state behind the closure moves.
// Without this, an overlay materializes the description it was built with,
// once, for as long as it is open: a dialog that looks up what someone typed
// shows the answer to nothing.
//
// So the root rebuilds it whenever the root itself draws, which is what
// window.refresh() — the call whose whole purpose is "there is no view to
// notify" — now reaches. Marking it dirty schedules no frame of its own: the
// overlay is about to render as part of this one, and it renders from the
// script rather than from the cache. A non-script overlay owns its own state
// and never reaches here.
static void RebuildScriptOverlay(Ctx* cx, Entity<ScriptView> content) {
    if (ScriptView* view = content.Get(cx->app)) view->dirty = true;
}

ShellRoot::~ShellRoot() {
    if (app && content.IsValid()) EntityDrop(app, content);
}

Entity<ShellRoot> ShellRoot::New(App* app, EntityId content) {
    Entity<ShellRoot> root = EntityNew<ShellRoot>(app);
    if (ShellRoot* state = root.Get(app)) {
        state->app = app;
        state->content = content;
    }
    return root;
}

El* ShellRoot::Render(ShellRoot* self, Ctx* cx) {
    if (!self) return Div(cx->a)->SizeFull();
    if (ShellRootWindowState* state = RootWindowState(cx->win))
        state->root = cx->self;
    El* content = self->content.IsValid()
                      ? EntityRender(cx->app, cx->win, cx->a, self->content)
                      : nullptr;
    // The HUD, when a script has asked for one. Above every other layer: it is
    // a diagnostic, and a dialog over it would hide the reading the dialog's
    // own frames are producing.
    El* hud = nullptr;
    if (self->fpsHudVisible) {
        // The same keyed slot the `fps_monitor()` element form uses, so the
        // monitor is one per window and a HUD hidden and shown again keeps its
        // history.
        auto* slot = KeyedState<Entity<FpsMonitor>>(
            cx, (uint32_t)HashClickId(StrL("gpui-fps-monitor")));
        if (slot) {
            if (!slot->IsValid()) *slot = EntityNew<FpsMonitor>(cx);
            // FpsOverlayOpts is what the HUD element takes since the fps
            // crate's own checkins landed: it puts the anchor and the budget
            // on the monitor before it renders, which is the assignment this
            // did by hand.
            FpsOverlayOpts opts;
            opts.anchor = self->fpsHud.anchor;
            if (self->fpsHud.hasFrameBudget) {
                opts.frameBudget = self->fpsHud.frameBudget;
            }
            hud = FpsOverlayEl(cx, *slot, opts);
        }
    }
    // The window's base text size, from the theme rather than from the
    // runtime's default rem.
    //
    // Everything this root draws itself — toasts, the sheet, the dialog
    // scrim's chrome — states no size of its own and inherits this one.
    // Without it that chrome sat at the 16px default while the components an
    // application builds set their own sizes, so a dense application drawn at
    // 12px got 16px notifications over it. `md` is the base by the library's
    // own convention. The default `md` is that same 16px, so a theme that says
    // nothing about type is drawn exactly as it was before this existed.
    const BaseTheme* base = BaseThemeGlobal(cx->app);
    float baseSize =
        base ? base->tokens.typography.md.size : TypographyTokens{}.md.size;
    component::Root* root = component::Root::New(cx)->Bordered(false)->Child(
        content ? content : Div(cx->a)->SizeFull());
    if (hud) root->Child(hud);
    return root->IntoEl()->Font(baseSize);
}

ShellRoot* ShellRootOf(Window* window, App* app) {
    ShellRootWindowState* state = RootWindowState(window);
    if (!state || !state->root.IsValid() || state->root != window->root)
        return nullptr;
    return Entity<ShellRoot>{state->root}.Get(app);
}

// Invalidates the root itself, which is what a change to one of its own fields
// wants: `Notify(cx)` would mark whichever view is calling, and the HUD is not
// in that view's description.
static void NotifyRoot(Ctx* cx) {
    ShellRootWindowState* state = RootWindowState(cx->win);
    if (state && state->root.IsValid())
        NotifyEntity(cx->app, state->root, cx->win);
}

static void RestoreOverlayFocus(Ctx* cx, FocusHandle restore) {
    if (!FocusHandleRestore(cx->win, restore)) WindowSetFocusId(cx->win, 0);
}

struct ShellDialogLayer {
    App* app = nullptr;
    Entity<ScriptView> content = {};
    DialogOptions options = {};
    FocusHandle focus = {};
    FocusHandle restore = {};

    ~ShellDialogLayer() {
        if (app && content.IsValid()) EntityDrop(app, content.id);
    }

    static void Close(ShellDialogLayer* self, Ctx* cx, const void*) {
        if (!self) return;
        FocusHandle restore = self->restore;
        WindowCloseDialog(cx);
        RestoreOverlayFocus(cx, restore);
    }

    static void OnBackdrop(ShellDialogLayer* self, Ctx* cx,
                           const MouseDownEvent* event) {
        if (!self || !event || event->button != MouseButton::Left ||
            !self->options.backdropDismissable)
            return;
        WindowLayers* layers = WindowLayersOf(cx->win);
        bool topmost = layers && layers->dialogs.len > 0 &&
                       layers->dialogs[layers->dialogs.len - 1]
                               .view == cx->self;
        if (!topmost) return;
        WindowStopPropagation(cx);
        Close(self, cx, event);
    }

    static El* Render(ShellDialogLayer* self, Ctx* cx) {
        RebuildScriptOverlay(cx, self->content);
        const Theme& theme = ThemeNow(cx->app);
        WindowLayers* layers = WindowLayersOf(cx->win);
        bool topmost = layers && layers->dialogs.len > 0 &&
                       layers->dialogs[layers->dialogs.len - 1]
                               .view == cx->self;
        El* backdrop = nullptr;
        if (topmost) {
            backdrop =
                DialogBackdrop::New(cx)
                    ->Bg(Rgba8(0, 0, 0, 128))
                    ->OnMouseDown(Listen(cx, &ShellDialogLayer::OnBackdrop));
        }
        El* child =
            self->content.IsValid()
                ? EntityRender(cx->app, cx->win, cx->a, self->content.id)
                : nullptr;
        El* surface = Div(cx->a)
                          ->FlexCol()
                          ->Bg(theme.popover)
                          ->Fg(theme.popoverFg)
                          ->Border(1, theme.border)
                          ->Radius(theme.radiusLg)
                          ->Pad(16)
                          ->Child(child ? child : Div(cx->a));
        El* popup = DialogPopup::New(cx)
                        ->Absolute()
                        ->Top(0)
                        ->Left(0)
                        ->Right(0)
                        ->Bottom(0)
                        ->Flex()
                        ->ItemsCenter()
                        ->JustifyCenter()
                        ->Child(surface);
        Str trap = StrDup(cx->a, fmt("shell-dialog-%d", cx->self.index));
        if (topmost && self->options.escapeDismissable)
            DialogBindKeys(cx, popup, trap, {}, {},
                           Listen(cx, &ShellDialogLayer::Close));
        return Dialog::New(cx)
            ->Trap(trap)
            ->Backdrop(backdrop)
            ->Popup(popup)
            ->IntoEl()
            ->DeferredLayer((int)(10 + (layers ? layers->dialogs.len : 0)));
    }
};

int ShellRootOpenDialog(Ctx* cx, Entity<ScriptView> content,
                        DialogOptions options) {
    if (!cx || !content.IsValid() || !ShellRootOf(cx->win, cx->app)) return 0;
    Entity<ShellDialogLayer> layer = EntityNew<ShellDialogLayer>(cx->app);
    ShellDialogLayer* state = layer.Get(cx);
    if (!state) return 0;
    state->app = cx->app;
    state->content = content;
    state->options = options;
    state->focus = FocusHandleNew(cx);
    state->restore = WindowFocused(cx->win);
    WindowOpenDialog(cx, layer.id, true);
    FocusHandleFocus(cx->win, state->focus);
    return WindowDialogCount(cx);
}

bool ShellRootCloseDialog(Ctx* cx) {
    if (!cx || !ShellRootOf(cx->win, cx->app)) return false;
    WindowLayers* layers = WindowLayersOf(cx->win);
    if (!layers || layers->dialogs.len == 0) return false;
    Entity<ShellDialogLayer> layer{layers->dialogs[layers->dialogs.len - 1]
                                       .view};
    ShellDialogLayer* state = layer.Get(cx);
    FocusHandle restore = state ? state->restore : FocusHandle{};
    WindowCloseDialog(cx);
    RestoreOverlayFocus(cx, restore);
    return true;
}

int ShellRootCloseAllDialogs(Ctx* cx) {
    if (!cx || !ShellRootOf(cx->win, cx->app)) return 0;
    WindowLayers* layers = WindowLayersOf(cx->win);
    int count = layers ? layers->dialogs.len : 0;
    if (count == 0) return 0;
    ShellDialogLayer* first = Entity<ShellDialogLayer>{layers->dialogs[0].view}
                                  .Get(cx);
    FocusHandle restore = first ? first->restore : FocusHandle{};
    WindowCloseAllDialogs(cx);
    RestoreOverlayFocus(cx, restore);
    return count;
}

bool ShellRootHasDialog(Ctx* cx) {
    return cx && ShellRootOf(cx->win, cx->app) && WindowHasActiveDialog(cx);
}

struct ShellSheetLayer {
    App* app = nullptr;
    Entity<ScriptView> content = {};
    component::SheetPlacement placement = component::SheetPlacement::Right;
    FocusHandle focus = {};
    FocusHandle restore = {};

    ~ShellSheetLayer() {
        if (app && content.IsValid()) EntityDrop(app, content.id);
    }

    static void Close(ShellSheetLayer* self, Ctx* cx, const void*) {
        if (!self) return;
        FocusHandle restore = self->restore;
        WindowCloseSheet(cx);
        RestoreOverlayFocus(cx, restore);
    }

    static El* Render(ShellSheetLayer* self, Ctx* cx) {
        RebuildScriptOverlay(cx, self->content);
        const Theme& theme = ThemeNow(cx->app);
        El* child =
            self->content.IsValid()
                ? EntityRender(cx->app, cx->win, cx->a, self->content.id)
                : nullptr;
        WinSize size = WindowSize(cx->win);
        El* surface = Div(cx->a)
                          ->FlexCol()
                          ->Absolute()
                          ->Bg(theme.popover)
                          ->Fg(theme.popoverFg)
                          ->Border(1, theme.border)
                          ->Pad(16)
                          ->Child(child ? child : Div(cx->a));
        switch (self->placement) {
            case component::SheetPlacement::Left:
                surface->Top(0)->Bottom(0)->Left(0)->W(size.dipW / 3.f);
                break;
            case component::SheetPlacement::Right:
                surface->Top(0)->Bottom(0)->Right(0)->W(size.dipW / 3.f);
                break;
            case component::SheetPlacement::Top:
                surface->Top(0)->Left(0)->Right(0)->H(size.dipH / 3.f);
                break;
            case component::SheetPlacement::Bottom:
                surface->Bottom(0)->Left(0)->Right(0)->H(size.dipH / 3.f);
                break;
        }
        return Sheet::New(cx)
            ->Trap(StrL("shell-sheet"))
            ->Overlay(Div(cx->a)
                          ->Absolute()
                          ->Top(0)
                          ->Left(0)
                          ->Right(0)
                          ->Bottom(0)
                          ->Bg(Rgba8(0, 0, 0, 128)))
            ->Surface(surface)
            ->RequestClose(Listen(cx, &ShellSheetLayer::Close))
            ->IntoEl()
            ->DeferredLayer(5);
    }
};

bool ShellRootOpenSheet(Ctx* cx, Entity<ScriptView> content,
                        component::SheetPlacement placement) {
    if (!cx || !content.IsValid() || !ShellRootOf(cx->win, cx->app))
        return false;
    FocusHandle restore = WindowFocused(cx->win);
    WindowLayers* layers = WindowLayersOf(cx->win);
    if (layers && layers->hasSheet) {
        ShellSheetLayer* current = Entity<ShellSheetLayer>{layers->sheet.view}
                                       .Get(cx);
        if (current) restore = current->restore;
    }
    Entity<ShellSheetLayer> layer = EntityNew<ShellSheetLayer>(cx->app);
    ShellSheetLayer* state = layer.Get(cx);
    if (!state) return false;
    state->app = cx->app;
    state->content = content;
    state->placement = placement;
    state->focus = FocusHandleNew(cx);
    state->restore = restore;
    WinSize size = WindowSize(cx->win);
    float extent = (placement == component::SheetPlacement::Left ||
                    placement == component::SheetPlacement::Right)
                       ? size.dipW / 3.f
                       : size.dipH / 3.f;
    WindowOpenSheetAt(cx, layer.id, placement, extent);
    FocusHandleFocus(cx->win, state->focus);
    return true;
}

bool ShellRootCloseSheet(Ctx* cx) {
    if (!cx || !ShellRootOf(cx->win, cx->app)) return false;
    WindowLayers* layers = WindowLayersOf(cx->win);
    if (!layers || !layers->hasSheet) return false;
    ShellSheetLayer* state = Entity<ShellSheetLayer>{layers->sheet.view}
                                 .Get(cx);
    FocusHandle restore = state ? state->restore : FocusHandle{};
    WindowCloseSheet(cx);
    RestoreOverlayFocus(cx, restore);
    return true;
}

bool ShellRootHasSheet(Ctx* cx) {
    return cx && ShellRootOf(cx->win, cx->app) && WindowHasActiveSheet(cx);
}

static component::NotificationType NotificationTypeFor(ToastLevel level) {
    switch (level) {
        case ToastLevel::Info:
            return component::NotificationType::Info;
        case ToastLevel::Success:
            return component::NotificationType::Success;
        case ToastLevel::Warning:
            return component::NotificationType::Warning;
        case ToastLevel::Error:
            return component::NotificationType::Error;
    }
    return component::NotificationType::Info;
}

bool ShellRootShowFpsMonitor(Ctx* cx, const FpsHudRequest& request) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    if (!root) return false;
    root->fpsHud = request;
    root->fpsHudVisible = true;
    NotifyRoot(cx);
    return true;
}

bool ShellRootHideFpsMonitor(Ctx* cx) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    if (!root || !root->fpsHudVisible) return false;
    root->fpsHudVisible = false;
    NotifyRoot(cx);
    return true;
}

bool ShellRootFpsMonitorVisible(Ctx* cx) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    return root && root->fpsHudVisible;
}

bool ShellRootPushToast(Ctx* cx, const ToastRequest& request) {
    ShellRoot* root = cx ? ShellRootOf(cx->win, cx->app) : nullptr;
    if (!root || !request.title) return false;
    Str id = request.id;
    if (!request.hasId) {
        root->nextToastOrdinal++;
        id = StrDup(cx->a, fmt("shell-toast-%llu", root->nextToastOrdinal));
    }
    component::Notification toast = component::Notification::New();
    toast.Id1<ShellRoot>(id)
        .Title(request.title)
        .Message(request.description)
        .WithType(NotificationTypeFor(request.level));
    component::NotificationListState* list = WindowNotifications(cx).Get(cx);
    if (list) {
        list->width = 320;
        list->maxItems = 3;
    }
    return WindowPushNotification(cx, toast, request.timeoutMs) != 0;
}

bool ShellRootRemoveToast(Ctx* cx, Str id) {
    if (!cx || !id || !ShellRootOf(cx->win, cx->app)) return false;
    Entity<component::NotificationListState> handle = WindowNotifications(cx);
    component::NotificationListState* list = handle.Get(cx);
    uint32_t key = (uint32_t)HashClickId(id);
    bool found = false;
    for (int i = 0; i < list->items.len; i++) {
        const component::Notification& item = list->items[i];
        if (item.identityType == component::NotificationTypeOf<ShellRoot>() &&
            item.identityHasKey && item.identityKey == key) {
            found = true;
            break;
        }
    }
    if (found) WindowRemoveNotification1<ShellRoot>(cx, key);
    return found;
}

void ShellRootClearToasts(Ctx* cx) {
    if (cx && ShellRootOf(cx->win, cx->app)) WindowClearNotifications(cx);
}

int ShellRootToastCount(Ctx* cx) {
    return cx && ShellRootOf(cx->win, cx->app) ? WindowNotificationCount(cx)
                                               : 0;
}

} // namespace gpui
