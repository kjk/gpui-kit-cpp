#include "ui/root.h"
#include "ui/dialog.h"
#include "ui/window_border.h"
#include "ui/global_state.h"
#include "ui/touch_selection.h"
#include "ui/native_menu.h"

namespace gpui {

namespace component {

int RootDialogOverlayIndex(const bool* wantsOverlay, const bool* closing,
                           int n) {
    int openIx = -1;
    int anyIx = -1;
    for (int i = 0; i < n; i++) {
        if (!wantsOverlay || !wantsOverlay[i]) {
            continue;
        }
        anyIx = i;
        if (!closing || !closing[i]) {
            openIx = i;
        }
    }
    return openIx >= 0 ? openIx : anyIx;
}

int RootDialogOverlayIndex(const bool* wantsOverlay, int n) {
    return RootDialogOverlayIndex(wantsOverlay, nullptr, n);
}

Edges RootNotificationInsets(bool hasSheet, SheetPlacement placement,
                             float size) {
    Edges e = {};
    if (!hasSheet) {
        return e;
    }
    switch (placement) {
        case SheetPlacement::Top:
            e.top = size;
            break;
        case SheetPlacement::Right:
            e.right = size;
            break;
        case SheetPlacement::Bottom:
            e.bottom = size;
            break;
        case SheetPlacement::Left:
            e.left = size;
            break;
    }
    return e;
}

El* WindowStateLayers(Ctx* cx) {
    gpui::WindowLayers* wl = WindowLayersOf(cx->win);
    if (!wl) {
        return nullptr;
    }
    Arena* a = cx->a;
    El* layers = nullptr;
    auto add = [&](El* e) {
        if (!e) {
            return;
        }
        if (!layers) {
            layers = Div(a)->Absolute()->Left(0)->Top(0)->Right(0)->Bottom(0);
        }
        layers->Child(e);
    };
    // The notification layer covers the window, less the room the sheet takes
    // on its own edge.
    if (wl->notifications.IsValid()) {
        El* list = NotificationList::New(cx, wl->notifications)->IntoEl();
        Edges in = RootNotificationInsets(wl->hasSheet, wl->sheet.placement,
                                          wl->sheet.size);
        // Keep toasts out from under the system bars and the keyboard.
        // The sheet still pushes them off its own edge, inside that area.
        Edges safe = WindowSafeInsets(cx->win);
        El* inner = Div(a)
                        ->Absolute()
                        ->Left(in.left)
                        ->Top(in.top)
                        ->Right(in.right)
                        ->Bottom(in.bottom)
                        ->Child(list);
        El* layer = Div(a)
                        ->Absolute()
                        ->Left(safe.left)
                        ->Top(safe.top)
                        ->Right(safe.right)
                        ->Bottom(safe.bottom)
                        ->Child(inner);
        add(layer->Deferred());
    }
    if (wl->hasSheet) {
        if (El* s = EntityRender(cx->app, cx->win, a, wl->sheet.view)) {
            add(s->Deferred());
        }
    }
    // A closing dialog keeps its layer only to finish leaving. Focus that
    // landed inside one is put back where it was before that dialog opened.
    for (int i = 0; i < wl->dialogs.len; i++) {
        WindowLayer& dialog = wl->dialogs[i];
        if (!dialog.closing || !dialog.trapFocus) {
            continue;
        }
        int focused = cx->win->focusId;
        if (focused != dialog.trapFocus &&
            !WindowFocusContains(cx->win, dialog.trapFocus, focused)) {
            continue;
        }
        if (dialog.previousFocus.IsValid()) {
            FocusHandleRestore(cx->win, dialog.previousFocus);
        } else {
            WindowSetFocusId(cx->win, 0);
        }
    }
    // The overlay belongs to the newest dialog still open. With none left,
    // the newest closing dialog keeps the dim so it fades instead of
    // switching off. A closing dialog under an open one paints no dim.
    Vec<bool> wantsOverlay;
    Vec<bool> closing;
    int topOpen = -1;
    for (int i = 0; i < wl->dialogs.len; i++) {
        VecAppend(wantsOverlay, wl->dialogs[i].overlay);
        VecAppend(closing, wl->dialogs[i].closing);
        if (!wl->dialogs[i].closing) {
            topOpen = i;
        }
    }
    int overlaySlot =
        RootDialogOverlayIndex(wantsOverlay.els, closing.els, wl->dialogs.len);
    Vec<uint32_t> dropIds;
    for (int i = 0; i < wl->dialogs.len; i++) {
        DialogMount mount;
        mount.bound = true;
        mount.id = wl->dialogs[i].id;
        mount.layerIx = i;
        mount.closing = wl->dialogs[i].closing;
        mount.topmost = i == topOpen;
        mount.showOverlay = i == overlaySlot;
        DialogBindMount(&mount);
        El* dialog = EntityRender(cx->app, cx->win, a, wl->dialogs[i].view);
        DialogBindMount(nullptr);
        wl->dialogs[i].exitEnabled = mount.exitEnabled;
        wl->dialogs[i].visibility = mount.visibility;
        wl->dialogs[i].surfaceY = mount.surfaceY;
        wl->dialogs[i].trapFocus = mount.trapFocus;
        if (mount.unmount) {
            VecAppend(dropIds, wl->dialogs[i].id);
            continue;
        }
        if (dialog) {
            add(dialog->Deferred());
        }
    }
    // Unmount after the view has returned. Dropping it mid-render would free
    // the entity whose Render is still on the stack.
    for (int d = 0; d < len(dropIds); d++) {
        for (int i = wl->dialogs.len - 1; i >= 0; i--) {
            if (wl->dialogs[i].id != dropIds[d] || !wl->dialogs[i].closing) {
                continue;
            }
            EntityId view = wl->dialogs[i].view;
            VecRemoveAt(wl->dialogs, i);
            EntityDrop(cx->app, view);
            Notify(cx);
            break;
        }
    }
    return layers;
}

// WindowState::new: the window's layers, owned by its Root.
static void* WindowStateBuild(Window* window, App* app) {
    WindowLayers* layers = new WindowLayers();
    layers->app = app;
    layers->win = window;
    return layers;
}

static void WindowStateDrop(void* state) {
    delete (WindowLayers*)state;
}

// prepare: the window's rem size from the theme, and the active
// text-selection scope.
static void WindowStatePrepare(void*, Ctx* cx) {
    WindowSetRemSize(cx->win, ThemeNow(cx->app).fontSize);
    UiSelectionFrameBegin(cx->app);
}

static void WindowStateStyle(void*, El* surface, Ctx* cx) {
    const Theme& th = ThemeNow(cx->app);
    surface->Bg(th.tokens.background)->Fg(th.foreground);
}

static El* WindowStateDecorate(void*, El* surface, const Root*, Ctx* cx) {
    return WindowBorder::New(cx)->Child(surface)->IntoEl();
}

static El* WindowStateRender(void*, Ctx* cx) {
    El* layers = WindowStateLayers(cx);
    // After the layers, so the edit menu floats above whatever was selected.
    // Handles are painted by the owning text. The tooltip overlay Rust
    // mounts here is the window's own (win->tooltip); the native menu's drawn
    // fallback is mounted here, as Rust's is.
    El* touch = WindowTouchSelectionOverlay(cx);
    El* menu = NativeMenuFallbackOverlay(cx);
    if (!touch && !menu) {
        return layers;
    }
    if (!layers) {
        layers = Div(cx->a)->Absolute()->Left(0)->Top(0)->Right(0)->Bottom(0);
    }
    if (touch) {
        layers->Child(touch);
    }
    if (menu) {
        layers->Child(menu);
    }
    return layers;
}

const RootPlugin kWindowStatePlugin = {
    &WindowStateBuild, &WindowStateDrop,     &WindowStatePrepare,
    &WindowStateStyle, &WindowStateDecorate, &WindowStateRender,
};

void RootInit(App* app) {
    Root::RegisterPlugin(app, &kWindowStatePlugin);
}

} // namespace component
} // namespace gpui
