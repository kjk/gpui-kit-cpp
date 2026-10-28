#ifndef GPUI_SRC_UI_SCROLL_H_
#define GPUI_SRC_UI_SCROLL_H_
/* Themed scroll — crates/ui/src/scroll */

#include "ui/sizing.h"
#include "base/scrollable_mask.h"

namespace gpui {

namespace component {

// ScrollbarAxis. The axis belongs to the box that scrolls, which is
// `base/scrollbar.h` — this is the same enum under the name the themed side
// has always used it by.
using ScrollbarAxis = gpui::ScrollAxis;
using ScrollAxis = ScrollbarAxis; // compatibility with the original C++ API

struct Scrollable {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    // The source element is the scroll area. When the vertical bounce wraps
    // it, that wrap keeps the caller's size, minimum, maximum and flex, and
    // the area fills the wrap with the maximum cleared on the scrolled axis
    // (scrollable.rs). A horizontal area is not wrapped: it is already the
    // viewport, so its maximum limits the area and the children overflow it.
    El* element = nullptr;
    Str id = {};
    float scrollY = 0;
    float scrollX = 0;
    float h = 0;
    bool hSet = false;
    ScrollAxis axis = ScrollAxis::Vertical;
    // No explicit mode reads the projected Base theme (Scrolling by default).
    ScrollbarMode mode = ScrollbarMode::Scrolling;
    bool modeSet = false;
    // None defers to the platform. 0 off, 1 on. Horizontal-only areas do
    // not bounce.
    int scrollBounce = -1;
    // Where a scrollbar press, a drag or the wheel reports the offset it
    // worked out. The view owns the offsets, so it is the one that stores
    // them.
    Listener onScroll;

    static Scrollable* New(Ctx* cx);
    static Scrollable* New(Ctx* cx, Str id);
    static Scrollable* New(Ctx* cx, El* element,
                           ScrollAxis axis = ScrollAxis::Both);
    Scrollable* Id(Str v);
    Scrollable* Child(El* e);
    Scrollable* ScrollY(float v);
    Scrollable* ScrollX(float v);
    Scrollable* Axis(ScrollAxis v);
    Scrollable* Mode(ScrollbarMode v);
    Scrollable* H(float v);
    Scrollable* OnScroll(Listener fn);
    // Bounce at the vertical boundaries. On by default on iOS, Android and
    // macOS, off elsewhere.
    Scrollable* ScrollBounce(bool v);
    El* IntoEl();
};

// crates/ui/src/scroll/scrollable.rs::ScrollableElement. Rust expresses this
// as an extension trait over Div/Stateful; C++ makes the same transformations
// explicit and returns the fluent Scrollable builder.
struct ScrollableElement {
    static El* Scrollbar(Ctx* cx, El* element, Str id, float scrollY,
                         float scrollX, Listener onScroll,
                         ScrollbarAxis axis = ScrollbarAxis::Vertical);
    static El* VerticalScrollbar(Ctx* cx, El* element, Str id, float scrollY,
                                 Listener onScroll);
    static El* HorizontalScrollbar(Ctx* cx, El* element, Str id, float scrollX,
                                   Listener onScroll);
    static Scrollable* OverflowScrollbar(Ctx* cx, El* element);
    static Scrollable* OverflowXScrollbar(Ctx* cx, El* element);
    static Scrollable* OverflowYScrollbar(Ctx* cx, El* element);
};

// `pub use gpui_base::ScrollableMask`. The mask moved to gpui-base with the
// Markdown table that wants it; scroll/mod.rs re-exports it and so does this.
using ScrollableMask = gpui::ScrollableMask;

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_SCROLL_H_
