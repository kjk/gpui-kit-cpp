#include "ui/root.h"
#include "gpui/platform.h"
#include "ui/window_border.h"
#include "ui/global_state.h"
#include "ui/touch_selection.h"

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

Root* Root::New(Ctx* cx) {
    Arena* a = cx->a;
    Root* r = ArenaNew<Root>(a);
    r->a = a;
    r->cx = cx;
    // Root::new does this on macOS: the window forwards accessibility hit
    // tests to the view, so what the page drew is reachable. The window only
    // takes it once, however many frames build a Root.
    PlatInstallAccessibilityHitTest(cx->win);
    return r;
}
Root* Root::Bordered(bool v) {
    bordered = v;
    return this;
}
Root* Root::ShadowSize(float v) {
    shadowSize = v;
    return this;
}
Root* Root::Child(El* e) {
    child = e;
    return this;
}
Root* Root::Notifications(El* e) {
    notifications = e;
    return this;
}
Root* Root::Sheet(El* e, SheetPlacement placement, float size) {
    sheet = e;
    hasSheet = e != nullptr;
    sheetPlacement = placement;
    sheetSize = size;
    return this;
}
Root* Root::Dialog(El* e, bool overlay) {
    if (e) {
        dialogs.Append(a, e);
        dialogOverlay.Append(a, overlay);
    }
    return this;
}

Root* Root::UseWindowLayers(bool v) {
    windowLayers = v;
    return this;
}

El* Root::IntoEl() {
    UiSelectionFrameBegin(cx->app);
    const Theme& th = ThemeNow(cx->app);
    El* e = Div(a)->FlexCol()->SizeFull()->Bg(th.tokens.background);
    if (child) {
        e->Child(child);
    }

    // What `window.open_dialog` / `open_sheet` / `push_notification` left on
    // the window, rendered alongside whatever the page passed in. Rust's Root
    // owns these outright; here they join the page's, so a tree that built
    // its own layers keeps working and one that opens a dialog from a handler
    // with no view of its own also does.
    if (windowLayers) {
        if (gpui::WindowLayers* wl = WindowLayersOf(cx->win)) {
            if (!notifications && wl->notifications.IsValid()) {
                notifications = NotificationList::New(cx, wl->notifications)
                                    ->IntoEl();
            }
            if (!sheet && wl->hasSheet) {
                if (El* s = EntityRender(cx->app, cx->win, a, wl->sheet.view)) {
                    Sheet(s, wl->sheet.placement, wl->sheet.size);
                }
            }
            for (int i = 0; i < wl->dialogs.len; i++) {
                Dialog(EntityRender(cx->app, cx->win, a, wl->dialogs[i].view),
                       wl->dialogs[i].overlay);
            }
        }
    }

    // The notification layer covers the window, less the room the sheet takes
    // on its own edge.
    if (notifications) {
        Edges in = RootNotificationInsets(hasSheet, sheetPlacement, sheetSize);
        El* layer = Div(a)
                        ->Absolute()
                        ->Left(in.left)
                        ->Top(in.top)
                        ->Right(in.right)
                        ->Bottom(in.bottom)
                        ->Child(notifications);
        e->Child(layer->Deferred());
    }
    if (sheet) {
        e->Child(sheet->Deferred());
    }
    // The dialogs draw over the sheet, in the order they were opened; the one
    // overlay there is belongs to the last of them that asked for one, which
    // is what keeps a stack of dialogs from tinting the page twice.
    for (El* dialog : dialogs) {
        e->Child(dialog->Deferred());
    }
    // After the content, so the edit menu floats above whatever was
    // selected. Handles are painted by the owning text.
    if (El* overlay = WindowTouchSelectionOverlay(cx)) {
        e->Child(overlay);
    }

    if (!bordered) {
        return e;
    }
    // Root::bordered(true) wraps the view in the window border: the shadow
    // padding a client-decorated window keeps, and the frame inside it that
    // dims while another window has the focus.
    return WindowBorder::New(cx)->ShadowSize(shadowSize)->Child(e)->IntoEl();
}

} // namespace component
} // namespace gpui
