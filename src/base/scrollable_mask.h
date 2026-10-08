#ifndef GPUI_BASE_SCROLLABLE_MASK_H_
#define GPUI_BASE_SCROLLABLE_MASK_H_
/* The wheel mask over a scroll viewport — crates/base/src/scrollable_mask.rs

   It moved down from `crates/ui/src/scroll/` when the Markdown table came to
   gpui-base: everything it needs — the scrollbar handle, the axis vocabulary,
   the ongoing-scroll axis lock — was already here, and node.rs is what wants
   it. `src/ui/scroll.h` re-exports it, so `component::ScrollableMask` still
   names this type.

   GPUI paints the mask as a transparent sibling over a ScrollHandle and takes
   the wheel in the *capture* phase, so an ancestor scroller — `gpui::list`
   under a scrollable TextView — cannot consume the vertical half of a
   diagonal swipe first. The integrated C++ scroll element owns its handle, so
   the sibling collapses onto the viewport itself: `Apply` marks the axis
   whose gestures the viewport traps, and `window_common.cpp` runs the same
   per-gesture axis lock and edge rule the Rust element does — a vertical mask
   hands the event to the ancestor at its edge, a horizontal one keeps it. */

#include "gpui/gpui.h"

namespace gpui {

struct ScrollableMask {
    Arena* a = nullptr;
    Axis axis = Axis::Vertical;
    El* element = nullptr;
    Str id = {};
    bool debug = false;

    static ScrollableMask* New(Ctx* cx, Axis axis, El* element);
    static El* Apply(El* element, Axis axis);
    ScrollableMask* Id(Str v);
    ScrollableMask* Debug(bool v = true);
    El* IntoEl();
};

// The square outside one rounded corner: side `radius`, anchored at the
// corner, with the quarter-disc arc that cuts the notch out of it.
// `xDir` and `yDir` are ±1 and point from the corner toward the inside.
struct CornerNotch {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
    float cx = 0;
    float cy = 0;
    float radius = 0;
    float a0 = 0;
    float a1 = 0;
    bool clockwise = false;
};

CornerNotch CornerNotchGeometry(float cornerX, float cornerY, float xDir,
                                float yDir, float radius);

// Fills the four notches of a rounded frame so content scrolled under a
// rectangular clip does not show square corners. A sibling of the scrolled
// element, not a child: a child would move with the scroll offset.
struct RoundedFrameCover {
    Arena* a = nullptr;
    // When set, the notches are painted over this element's border box and
    // the radii are read from its style. A standalone cover uses its own
    // layout box and the radii below.
    El* viewport = nullptr;
    Corners radii = {};
    Rgba backdrop = {};
    bool hasBackdrop = false;
    float frameBorder = 0;

    static RoundedFrameCover* Uniform(Ctx* cx, float radius);
    RoundedFrameCover* FrameBorder(float width);
    RoundedFrameCover* Backdrop(Rgba color);
    El* IntoEl();
};

// `horizontal_scroll_area(id, handle, style, child)`: a viewport that clips
// and scrolls sideways with a horizontal mask over it, so a vertical wheel
// keeps bubbling to the document. The caller refines the frame — background,
// border and radius — onto `viewport` before this call. The cover is a
// sibling, so those corners stay put while the track scrolls.
El* HorizontalScrollArea(Ctx* cx, Str id, El* viewport);

} // namespace gpui
#endif // GPUI_BASE_SCROLLABLE_MASK_H_
