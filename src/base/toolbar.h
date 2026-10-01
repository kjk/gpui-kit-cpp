#ifndef GPUI_BASE_TOOLBAR_H_
#define GPUI_BASE_TOOLBAR_H_
/* Unstyled toolbar — crates/base/src/toolbar.rs

   A container that groups a set of controls and owns roving keyboard focus
   among them. It exposes `Toolbar` semantics (a horizontal toolbar role) and
   moves focus between its focusable descendants with Left and Right,
   wrapping at either end. When disabled the arrow keys do nothing; hosted
   controls are disabled by their owner. The container tracks a focus handle
   that is not a tab stop, so Tab enters and leaves through its items, and an
   input inside it keeps its own arrow keys (place inputs at the trailing
   end).
*/

#include "gpui/gpui.h"

namespace gpui {

// MAX_FOCUS_ATTEMPTS: upper bound on tab-stop hops when wrapping focus back
// into the toolbar, so a toolbar whose items all vanished from the tab order
// can never hang the key handler. Mirrors Root's focus-trap loop bound.
const int kToolbarMaxFocusAttempts = 100;

// The toolbar's element state: the focus handle its container tracks, kept
// across frames so containment and the key handler name the same node, and
// whether its own navigation is off.
struct ToolbarState {
    FocusHandle focus = {};
    bool disabled = false;

    static void OnKeyDown(ToolbarState* self, Ctx* cx, const KeyEvent* ev);
};

// move_focus: step through the window's tab stops, the way Root's focus trap
// cycles, until the focus lands on another one inside `container`; when the
// walk comes back to where it started the toolbar has no other item and the
// focus stays put. The hops are taken on the frame's focus list and only the
// last one moves the focus, where Rust focuses each one in turn and lets the
// effect cycle settle on the last; no blur runs for a stop passed over.
// Answers whether the focus moved.
bool ToolbarMoveFocus(Window* win, FocusHandle container, bool forward);

struct Toolbar {
    Ctx* cx = nullptr;
    Str id = {};
    bool disabled = false;
    // The container. Style it, and add children to it, before IntoEl.
    El* root = nullptr;

    static Toolbar* New(Ctx* cx, Str id);
    // Disables the toolbar's own keyboard navigation. Hosted controls are
    // not disabled with it.
    Toolbar* Disabled(bool value);
    Toolbar* Child(El* child);
    El* IntoEl();
};

// A semantic subgroup of a toolbar's items: a Group role and its accessible
// name, so a run of related controls reads as one unit. It carries no
// behavior; the toolbar's roving focus walks its items like any others.
struct ToolbarGroup {
    Ctx* cx = nullptr;
    Str label = {};
    El* root = nullptr;

    static ToolbarGroup* New(Ctx* cx, Str id);
    // The accessible name announced for the group, e.g. "History".
    ToolbarGroup* Label(Str value);
    ToolbarGroup* Child(El* child);
    El* IntoEl();
};

} // namespace gpui
#endif // GPUI_BASE_TOOLBAR_H_
