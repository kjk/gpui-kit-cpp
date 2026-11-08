#include "base/sheet.h"
#include "base/focus_trap.h"

namespace gpui {

Str SheetContext() {
    return StrL("Sheet");
}

void SheetInitKeys() {
    CancelInitKeys("Sheet");
}

SheetOverlayPress SheetOverlayPressAction(bool overlayInteractive,
                                          bool overlayClosable,
                                          MouseButton button, float pressY,
                                          bool hasDismissBefore,
                                          float dismissBeforeY) {
    if (!overlayInteractive) {
        return SheetOverlayPress::Ignore;
    }
    if (hasDismissBefore && pressY < dismissBeforeY) {
        return SheetOverlayPress::Ignore;
    }
    if (overlayClosable && button == MouseButton::Left) {
        return SheetOverlayPress::Close;
    }
    return SheetOverlayPress::Swallow;
}

bool SheetClosesOnKey(int key) {
    return key == KeyEscape;
}

void SheetState::Close(Ctx* cx) {
    // sheet.rs constructs a default ClickEvent for both callbacks, including
    // an overlay press. Requesting the controlled state change always comes
    // before notifying the application that the sheet closed.
    ClickEvent click = {};
    if (requestClose.IsValid()) {
        ListenerCall(cx->app, cx->win, requestClose, &click);
    }
    if (onClose.IsValid()) {
        ListenerCall(cx->app, cx->win, onClose, &click);
    }
}

void SheetState::OnOverlay(SheetState* self, Ctx* cx,
                           const MouseDownEvent* ev) {
    if (!self || !ev) {
        return;
    }
    SheetOverlayPress action = SheetOverlayPressAction(
        self->overlayInteractive, self->overlayClosable, ev->button, ev->y,
        self->hasDismissBefore, self->dismissBeforeY);
    if (action == SheetOverlayPress::Ignore) {
        return;
    }
    WindowStopPropagation(cx);
    if (action == SheetOverlayPress::Close) {
        self->Close(cx);
    }
}

void SheetState::OnAction(SheetState* self, Ctx* cx, const ActionEvent* ev) {
    if (!ev) {
        return;
    }
    if (!self || ev->action != action::Cancel()) {
        const_cast<ActionEvent*>(ev)->propagate = true;
        return;
    }
    // Rust explicitly propagates Cancel before closing, so an enclosing
    // action handler may still observe Escape after this sheet does.
    const_cast<ActionEvent*>(ev)->propagate = true;
    self->Close(cx);
}

Sheet* Sheet::New(Ctx* cx) {
    Arena* a = cx->a;
    Sheet* s = ArenaNew<Sheet>(a);
    s->cx = cx;
    s->trap = StrL("sheet");
    s->root = Div(a)
                  ->Id(StrL("sheet-host"))
                  ->Fixed()
                  ->Top(0)
                  ->Left(0)
                  ->W(kFill)
                  ->H(kFill);
    return s;
}

Sheet* Sheet::Overlay(El* element) {
    overlay = element;
    return this;
}

Sheet* Sheet::Trap(Str name) {
    trap = name;
    return this;
}

Sheet* Sheet::Surface(El* element) {
    surface = element;
    return this;
}

Sheet* Sheet::OverlayInteractive(bool interactive) {
    overlayInteractive = interactive;
    return this;
}

Sheet* Sheet::OverlayClosable(bool closable) {
    overlayClosable = closable;
    return this;
}

Sheet* Sheet::DismissBeforeY(float y) {
    hasDismissBefore = true;
    dismissBeforeY = y;
    return this;
}

Sheet* Sheet::RequestClose(Listener handler) {
    requestClose = handler;
    return this;
}

Sheet* Sheet::OnClose(Listener handler) {
    onClose = handler;
    return this;
}

El* Sheet::IntoEl() {
    SheetInitKeys();
    Entity<SheetState> state =
        ElementStateEntity<SheetState>(cx, trap, StrL("gpui::SheetState"));
    if (SheetState* s = state.Get(cx)) {
        static_cast<SheetDismissal&>(*s) = *this;
    }

    // Rust tracks and traps focus on the host itself. It is not a Tab stop,
    // so controls inside remain the first places Tab visits.
    int id = FocusTrapId(trap);
    root->KeyContext(SheetContext())
        ->TrapId(id)
        ->FocusId(id)
        ->TabStop(false)
        ->FocusRing(false)
        ->OnAction(action::Cancel(), ListenTo(state, &SheetState::OnAction));
    FocusTrapArm(cx->win, id, id);

    if (overlay) {
        root->Child(overlay);
        if (overlayInteractive) {
            // This is Rust's second, transparent overlay child. Starting it
            // at the cutoff lets the C++ hit-chain reach a title bar behind
            // the sheet above that line, which is the observable purpose of
            // dismiss_before_y.
            El* capture =
                Div(cx->a)
                    ->Absolute()
                    ->Top(hasDismissBefore ? dismissBeforeY : 0)
                    ->Left(0)
                    ->Right(0)
                    ->Bottom(0)
                    ->OnMouseDown(ListenTo(state, &SheetState::OnOverlay));
            root->Child(capture);
        }
    }
    if (surface) {
        root->Child(surface);
    }
    return root;
}
} // namespace gpui
