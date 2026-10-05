#ifndef GPUI_COMPONENT_SHELL_SCROLL_MOD_H_
#define GPUI_COMPONENT_SHELL_SCROLL_MOD_H_

// crates/component-shell/src/shell/scroll/: the retained scrolling capability
// and its two render surfaces. What the module's own tests reach.

#include "component_shell/support.h"
#include "base/scrollbar.h"

namespace gpui::component_shell::scroll::scroll {

// Rust's retained `gpui::ScrollHandle`. The offset here belongs to whoever
// stores it (El::ScrollY), so the handle is the entity the viewport reports
// its scroll to and reads it back from. A scrollbar here is painted by the
// box that scrolls rather than by an overlay reading the handle, so a
// Scrollbar leaves on the handle how its bar should look, and the Scroll
// viewport sharing the handle paints it.
struct ScrollHandleState {
    float offsetX = 0;
    float offsetY = 0;
    // Frame arena pointers are read only after matching both window and frame.
    Window* window = nullptr;
    uint64_t frame = UINT64_MAX;
    El* viewport = nullptr;
    bool hasBar = false;
    ScrollbarAxis barAxis = ScrollbarAxis::Both;
    bool hasBarMode = false;
    ScrollbarMode barMode = ScrollbarMode::Scrolling;

    static void OnScroll(ScrollHandleState* self, Ctx* cx,
                         const ScrollEvent* event);
};

struct Op {
    enum Kind : uint8_t {
        Axis,
        Mode,
        ViewportFromLayout,
    } kind = Axis;
    ScrollbarAxis axis = ScrollbarAxis::Vertical;
    ScrollbarMode mode = ScrollbarMode::Scrolling;
    bool flag = false;
};

// resolve_ops: the last call of each kind wins.
struct ResolvedOps {
    bool hasAxis = false;
    ScrollbarAxis axis = ScrollbarAxis::Vertical;
    bool hasMode = false;
    ScrollbarMode mode = ScrollbarMode::Scrolling;
    bool viewportFromLayout = false;

    void Fold(const Op& op);
};

// require_leaf: a Scrollbar takes neither children nor shell style.
bool RequireLeaf(int children, bool styled, Str* error);

} // namespace gpui::component_shell::scroll::scroll
#endif // GPUI_COMPONENT_SHELL_SCROLL_MOD_H_
