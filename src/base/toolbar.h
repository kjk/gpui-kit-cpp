#ifndef GPUI_BASE_TOOLBAR_H_
#define GPUI_BASE_TOOLBAR_H_
/* Unstyled toolbar — crates/base/src/toolbar.rs

   A container that groups a set of controls and owns roving keyboard focus
   among them. It exposes `Toolbar` semantics and moves focus between its
   focusable descendants, wrapping at either end. Horizontal (the default)
   uses Left and Right and reports a horizontal orientation. Vertical uses
   Up and Down and reports a vertical orientation. When disabled the arrow
   keys do nothing; hosted
   controls are disabled by their owner. The container tracks a focus handle
   that is not a tab stop, so Tab enters and leaves through its items, and an
   input inside it keeps its own arrow keys (place inputs at the trailing
   end).
*/

#include "gpui/gpui.h"

namespace gpui {

// The toolbar's element state: the focus handle its container tracks, kept
// across frames so containment and the key handler name the same node, and
// whether its own navigation is off.
struct ToolbarState {
    FocusHandle focus = {};
    bool disabled = false;
    // The direction captured when the bar was built. Horizontal moves with
    // Left and Right; vertical moves with Up and Down.
    Axis axis = Axis::Horizontal;

    static void OnKeyDown(ToolbarState* self, Ctx* cx, const KeyEvent* ev);
    // on_any_mouse_down: the handle is for containment only. A press on
    // blank space must not run the window's default, which would blur an
    // editor. A focusable item takes the press before this runs.
    static void OnMouseDown(ToolbarState* self, Ctx* cx, const MouseDownEvent*);
};

// move_focus: walk the window's tab order in the asked direction, passing
// over stops outside `container`, and land on the first stop inside it. The
// order wraps, so the walk comes back in at the container's first stop
// however many stops the rest of the window holds. It ends at the first
// repeated stop and leaves focus where it started, so a toolbar whose items
// have all left the tab order cannot hang the key handler. The hops are
// taken on the frame's focus list and only the landing one moves the focus.
// Answers whether the focus moved.
bool ToolbarMoveFocus(Window* win, FocusHandle container, bool forward);

struct Toolbar {
    Ctx* cx = nullptr;
    Str id = {};
    bool disabled = false;
    // Toolbar::axis. Horizontal is Left/Right; vertical is Up/Down.
    // The setter below is also named Axis. An unqualified Axis in this
    // class would mean that setter once the class is complete, which GCC
    // rejects (-Wchanges-meaning), so the field names the enum explicitly.
    ::gpui::Axis axis = ::gpui::Axis::Horizontal;
    // The container. Style it, and add children to it, before IntoEl.
    El* root = nullptr;

    static Toolbar* New(Ctx* cx, Str id);
    // Disables the toolbar's own keyboard navigation. Hosted controls are
    // not disabled with it.
    Toolbar* Disabled(bool value);
    // The direction the items run. Assistive technology hears the same
    // orientation. The default is horizontal, which is Left and Right.
    Toolbar* Axis(::gpui::Axis value);
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
