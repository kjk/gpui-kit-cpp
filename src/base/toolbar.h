#ifndef GPUI_BASE_TOOLBAR_H_
#define GPUI_BASE_TOOLBAR_H_
/* Unstyled toolbar — crates/base/src/toolbar.rs

   A container that groups a set of controls and owns roving keyboard focus
   among them. It exposes `Toolbar` semantics (a horizontal toolbar role) and
   moves focus between its focusable descendants with Left and Right,
   wrapping at either end. When disabled the arrow keys do nothing; hosted
   controls are disabled by their owner. The container is not itself a tab
   stop, so Tab enters and leaves through its items, and an input inside it
   keeps its own arrow keys (place inputs at the trailing end).

   Rust constrains the traversal to the subtree through the container's focus
   handle. A focus handle here knows containment only through focus traps,
   and a trap would also keep Tab inside the toolbar, so the toolbar records
   its laid-out box and takes as its items the window's tab stops inside it.
*/

#include "gpui/gpui.h"

namespace gpui {

// MAX_FOCUS_ATTEMPTS: Rust's bound on tab-stop hops. The traversal here walks
// the frame's focus list once, so it needs no bound; kept for the name.
const int kToolbarMaxFocusAttempts = 100;

// The toolbar's element state: its box as last laid out, and whether its own
// navigation is off.
struct ToolbarState {
    Bounds bounds = {};
    bool disabled = false;

    static void OnKeyDown(ToolbarState* self, Ctx* cx, const KeyEvent* ev);
};

// move_focus: focus the next (or previous) tab stop inside `container`,
// wrapping at either end. Nothing moves when the focus is not inside or when
// it is the only item. Answers whether the focus moved.
bool ToolbarMoveFocus(Window* win, Bounds container, bool forward);

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
