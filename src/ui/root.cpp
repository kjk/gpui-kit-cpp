#include "ui/root.h"
#include "ui/window_border.h"
#include "ui/global_state.h"
#include "ui/touch_selection.h"
#include "ui/native_menu.h"

namespace gpui {

namespace component {

int RootDialogOverlayIndex(const bool* wantsOverlay, int n) {
    int ix = -1;
    for (int i = 0; i < n; i++) {
        if (wantsOverlay && wantsOverlay[i]) {
            ix = i;
        }
    }
    return ix;
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
        El* layer = Div(a)
                        ->Absolute()
                        ->Left(in.left)
                        ->Top(in.top)
                        ->Right(in.right)
                        ->Bottom(in.bottom)
                        ->Child(list);
        add(layer->Deferred());
    }
    if (wl->hasSheet) {
        if (El* s = EntityRender(cx->app, cx->win, a, wl->sheet.view)) {
            add(s->Deferred());
        }
    }
    // The dialogs draw over the sheet, in the order they were opened.
    for (int i = 0; i < wl->dialogs.len; i++) {
        if (El* d = EntityRender(cx->app, cx->win, a, wl->dialogs[i].view)) {
            add(d->Deferred());
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
