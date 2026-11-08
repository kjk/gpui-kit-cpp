#ifndef GPUI_BASE_SHEET_H_
#define GPUI_BASE_SHEET_H_
/* Unstyled sheet — crates/base/src/sheet.rs */

#include "gpui/gpui.h"
#include "base/actions.h"

namespace gpui {

// What a press on a sheet's overlay comes to. Rust's on_any_mouse_down has
// three outcomes and the middle one is easy to miss: an overlay that is
// interactive but not closable still stops the press, so it never reaches the
// page behind — that is what an overlay is for.
enum class SheetOverlayPress : uint8_t {
    // The overlay does not take presses, or this one landed above the cutoff.
    Ignore,
    // Taken and stopped, but the sheet stays.
    Swallow,
    // Taken, stopped, and the sheet closes.
    Close
};

// `hasDismissBefore` is Rust's Option on dismiss_before_y: a press above that
// line is not the overlay's, which is how a sheet leaves a title bar usable
// while it is open.
SheetOverlayPress SheetOverlayPressAction(bool overlayInteractive,
                                          bool overlayClosable,
                                          MouseButton button, float pressY,
                                          bool hasDismissBefore,
                                          float dismissBeforeY);

// Escape closes a sheet. Rust calls cx.propagate() first and closes anyway, so
// the key is not consumed — whatever encloses the sheet still sees it.
bool SheetClosesOnKey(int key);

// sheet.rs::init and its key context. BaseInit calls this eagerly; Sheet also
// calls it when used without the crate initializer, as the other key-driven
// primitives do.
void SheetInitKeys();
Str SheetContext();

// The Rust Sheet owns closures after its render value is consumed. This
// builder is frame-arena data, so the handlers and dismissal settings wait in
// keyed state between the build and input dispatch.
// How a sheet's overlay and close behave: what the builder is told and what
// waits in SheetState for input dispatch, copied across whole.
struct SheetDismissal {
    bool overlayInteractive = true;
    bool overlayClosable = true;
    bool hasDismissBefore = false;
    float dismissBeforeY = 0;
    Listener requestClose = {};
    Listener onClose = {};
};

struct SheetState : SheetDismissal {
    void Close(Ctx* cx);
    static void OnOverlay(SheetState* self, Ctx* cx, const MouseDownEvent* ev);
    static void OnAction(SheetState* self, Ctx* cx, const ActionEvent* ev);
};

struct Sheet : SheetDismissal {
    Ctx* cx = nullptr;
    El* root = nullptr;
    // focus_trap("sheet"): the host holds Tab while the sheet is open.
    Str trap = {};
    El* overlay = nullptr;
    El* surface = nullptr;

    static Sheet* New(Ctx* cx);
    Sheet* Trap(Str name);
    Sheet* Overlay(El* element);
    Sheet* Surface(El* element);
    Sheet* OverlayInteractive(bool interactive);
    Sheet* OverlayClosable(bool closable);
    Sheet* DismissBeforeY(float y);
    Sheet* RequestClose(Listener handler);
    Sheet* OnClose(Listener handler);
    El* IntoEl();
};
} // namespace gpui
#endif // GPUI_BASE_SHEET_H_
