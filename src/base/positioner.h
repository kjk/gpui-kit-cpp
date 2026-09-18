#ifndef GPUI_BASE_POSITIONER_H_
#define GPUI_BASE_POSITIONER_H_
/* Popup positioning — crates/base/src/positioner.rs
 *
 * Every anchored surface resolves its position here, so flipping, alignment
 * and viewport clamping cannot drift apart between popups, tooltips and menus.
 * The pure resolver takes the trigger, the popup's measured size and the
 * viewport and hands back where the popup goes. Positioner is the matching
 * unstyled element builder; it lets layout measure the child group first and
 * applies that same resolver in the runtime's post-layout anchored pass.
 */

#include "base/geometry.h"

namespace gpui {

// Where the popup sits along the side it was placed on. This is the exact
// public name and vocabulary of positioner.rs; the runtime's flex-box
// alignment is separately named FlexAlign.
enum class Align : uint8_t {
    Start,
    Center,
    End
};
using PopupAlign = Align; // compatibility with the earlier C++ spelling

// Where the popup ended up, and the side it took. `hasPlacement` is false for
// corner positioning, which has no notion of a side.
struct ResolvedPosition {
    Bounds bounds;
    gpui::Placement placement = gpui::Placement::Top;
    bool hasPlacement = false;
};
using Positioned = ResolvedPosition; // compatibility with the earlier port

// Rust keeps requested child LayoutIds here between request_layout and
// prepaint. C++'s El::layoutNode and window LayoutCache own those ids, so the
// corresponding public-but-doc-hidden state is an empty structural marker.
struct PositionerState {};

// The standalone unstyled positioning element. It is a frame-arena builder,
// just like Rust's RenderOnce element: children are measured as one flex row,
// then the complete group is moved in window coordinates during the shared
// post-layout anchored pass.
struct Positioner {
    enum class Strategy : uint8_t {
        Side,
        Corner
    };

    Arena* a = nullptr;
    Strategy strategy = Strategy::Side;
    Bounds trigger = {};
    Anchor anchor = Anchor::TopLeft;
    Point point = {};
    gpui::Placement placement = gpui::Placement::Top;
    bool hasPlacement = false;
    gpui::Align align = gpui::Align::Center;
    float offset = 0;
    float margin = 4;
    bool occlude = false;
    ArenaVec<El*> children;

    static Positioner* Side(Ctx* cx, Bounds trigger);
    static Positioner* Corner(Ctx* cx, Anchor anchor, Point point);
    Positioner* Placement(gpui::Placement value);
    Positioner* Align(gpui::Align value);
    Positioner* Offset(float value);
    // Blocks the mouse over the positioned popup.
    //
    // Off by default, because a tooltip that swallowed the pointer would
    // un-hover the very trigger keeping it open. An interactive surface — a
    // popover, a menu, a dropdown — wants it on: what the surface covers is
    // the surface's, not the panel underneath.
    Positioner* Occlude();
    Positioner* Margin(float value);
    Positioner* Child(El* child);
    El* IntoEl();
};

// WINDOW_MARGIN: the distance kept between a popup and the viewport edge.
constexpr float kPopupMargin = 4.f;

// Places the popup on a side of the trigger. It takes the preferred side when
// the popup fits there, otherwise the opposite side, otherwise whichever side
// has more room; the result is then clamped into the viewport. `preferred` is
// null for Rust's `None`, which prefers Top.
ResolvedPosition PositionSide(Bounds trigger, Size popup, Size view,
                              float margin, const Placement* preferred,
                              Align align, float offset);

// Puts `anchor`'s corner of the popup at `at`, then clamps into the
// viewport. Never flips, and never reports a side.
ResolvedPosition PositionCorner(Anchor anchor, Point at, Size popup, Size view,
                                float margin);
ResolvedPosition PositionCorner(Anchor anchor, Point at, Size popup, Size view,
                                Edges margin);

// positioner.rs frame_insets: the part of the viewport that is frame rather
// than content, per side. A tiled edge of a client-decorated window draws no
// shadow, so the inset is zero there; a server-decorated window has none.
Edges PositionerFrameInsets(bool clientDecorated, Tiling tiling,
                            float clientInset);

} // namespace gpui
#endif // GPUI_BASE_POSITIONER_H_
