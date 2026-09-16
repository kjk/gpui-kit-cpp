#ifndef GPUI_BASE_SCROLLBAR_H_
#define GPUI_BASE_SCROLLBAR_H_
/* Unstyled scrollbar — crates/base/src/scrollbar.rs */

#include "gpui/gpui.h"

namespace gpui {

// The runtime paints integrated scrollbars and therefore owns the storage
// declarations used by El. These redeclarations put the source types back on
// the Base module's public surface without introducing an upward dependency.
enum class ScrollbarEntrance : uint8_t;
struct ScrollbarMotion;

// A trait-object-shaped adapter for ScrollHandle, UniformListScrollHandle and
// ListState. The caller owns `user`; all operations are synchronous like the
// three Rust implementations.
struct ScrollbarHandle {
    void* user = nullptr;
    Bounds (*viewportBounds)(void* user) = nullptr;
    Point (*offset)(void* user) = nullptr;
    void (*setOffset)(void* user, Point value) = nullptr;
    Size (*contentSize)(void* user) = nullptr;
    void (*startDrag)(void* user) = nullptr;
    void (*endDrag)(void* user) = nullptr;

    bool IsValid() const {
        return user && viewportBounds && offset && setOffset && contentSize;
    }
    Bounds ViewportBounds() const {
        return viewportBounds ? viewportBounds(user) : Bounds{};
    }
    Point Offset() const { return offset ? offset(user) : Point{}; }
    void SetOffset(Point value) const {
        if (setOffset) {
            setOffset(user, value);
        }
    }
    Size ContentSize() const {
        return contentSize ? contentSize(user) : Size{};
    }
    void StartDrag() const {
        if (startDrag) {
            startDrag(user);
        }
    }
    void EndDrag() const {
        if (endDrag) {
            endDrag(user);
        }
    }
};

// Paint-only refinements live with the source Scrollbar module. Theme holds
// six of them but does not define their vocabulary.
struct ScrollbarTrackStyle {
    Background background = {};
    Rgba border = {};
    float width = 0;
    bool hasBackground = false;
    bool hasBorder = false;
    bool hasWidth = false;

    ScrollbarTrackStyle Bg(Background value) const {
        ScrollbarTrackStyle out = *this;
        out.background = value;
        out.hasBackground = true;
        return out;
    }
    ScrollbarTrackStyle BorderColor(Rgba value) const {
        ScrollbarTrackStyle out = *this;
        out.border = value;
        out.hasBorder = true;
        return out;
    }
    ScrollbarTrackStyle Width(float value) const {
        ScrollbarTrackStyle out = *this;
        out.width = value;
        out.hasWidth = true;
        return out;
    }
};

struct ScrollbarThumbStyle {
    Background background = {};
    float width = 0;
    float inset = 0;
    float radius = 0;
    float minLength = 0;
    bool hasBackground = false;
    bool hasWidth = false;
    bool hasInset = false;
    bool hasRadius = false;
    bool hasMinLength = false;

    ScrollbarThumbStyle Bg(Background value) const {
        ScrollbarThumbStyle out = *this;
        out.background = value;
        out.hasBackground = true;
        return out;
    }
    ScrollbarThumbStyle Width(float value) const {
        ScrollbarThumbStyle out = *this;
        out.width = value;
        out.hasWidth = true;
        return out;
    }
    ScrollbarThumbStyle Inset(float value) const {
        ScrollbarThumbStyle out = *this;
        out.inset = value;
        out.hasInset = true;
        return out;
    }
    ScrollbarThumbStyle Radius(float value) const {
        ScrollbarThumbStyle out = *this;
        out.radius = value;
        out.hasRadius = true;
        return out;
    }
    ScrollbarThumbStyle MinLength(float value) const {
        ScrollbarThumbStyle out = *this;
        out.minLength = value;
        out.hasMinLength = true;
        return out;
    }
};

struct ScrollbarStyles {
    ScrollbarTrackStyle track = {};
    ScrollbarTrackStyle trackHover = {};
    ScrollbarTrackStyle trackActive = {};
    ScrollbarThumbStyle thumb = {};
    ScrollbarThumbStyle thumbHover = {};
    ScrollbarThumbStyle thumbActive = {};

    ScrollbarStyles Track(ScrollbarTrackStyle value) const {
        ScrollbarStyles out = *this;
        out.track = value;
        return out;
    }
    ScrollbarStyles TrackHover(ScrollbarTrackStyle value) const {
        ScrollbarStyles out = *this;
        out.trackHover = value;
        return out;
    }
    ScrollbarStyles TrackActive(ScrollbarTrackStyle value) const {
        ScrollbarStyles out = *this;
        out.trackActive = value;
        return out;
    }
    ScrollbarStyles Thumb(ScrollbarThumbStyle value) const {
        ScrollbarStyles out = *this;
        out.thumb = value;
        return out;
    }
    ScrollbarStyles ThumbHover(ScrollbarThumbStyle value) const {
        ScrollbarStyles out = *this;
        out.thumbHover = value;
        return out;
    }
    ScrollbarStyles ThumbActive(ScrollbarThumbStyle value) const {
        ScrollbarStyles out = *this;
        out.thumbActive = value;
        return out;
    }
};

// One longitudinal geometry drives painting, track clicks and dragging. The
// visible `length` excludes both insets; `travel` excludes the full logical
// thumb, and `extent` is the scrollable content distance.
struct ScrollbarThumbGeometry {
    float origin = 0;
    float inset = 0;
    float length = 0;
    float travel = 0;
    float extent = 0;

    float Start(float offset) const;
    float DragOffset(float position, float grab, float current) const;
};

ScrollbarThumbGeometry ScrollbarGeometry(float origin, float container,
                                         float content, float marginEnd,
                                         float inset, float minLength);

// The three legacy arithmetic seams below project the same geometry along one
// axis. Rust branches on `is_vertical` and does the same thing to y or x, so
// the caller passes whichever pair the axis names.
//
// `track` is the bar's length, `container` the visible size, and `content` the
// scrolled size. Offsets here run positive-down, the way El::ScrollY does;
// Rust's run negative because it offsets the content rather than the view.

// The thumb's length. It shrinks with the ratio of what is visible, and stops
// at a floor so a very long document still leaves something to grab.
float ScrollbarThumbSize(float track, float container, float content);
float ScrollbarThumbSize(float track, float container, float content,
                         float minLength);

// Where the thumb starts, for a given offset.
float ScrollbarThumbPos(float track, float thumb, float offset, float container,
                        float content);
float ScrollbarThumbPos(float track, float thumb, float offset, float container,
                        float content, float marginEnd);

// A press on the track away from the thumb: the thumb jumps so its centre is
// under the press. Rust caps this at 1 without a floor, since a press below
// the origin cannot happen inside the bar.
float ScrollbarOffsetForTrackPress(float pos, float trackOrigin, float track,
                                   float thumb, float container, float content);

// A drag. `grab` is how far into the thumb the press landed, so the thumb
// stays under the same point of the pointer rather than snapping its centre
// there — which is the whole difference between this and the press above.
float ScrollbarOffsetForDrag(float pos, float grab, float trackOrigin,
                             float track, float thumb, float container,
                             float content);
float ScrollbarOffsetForDrag(float pos, float grab, float trackOrigin,
                             float track, float thumb, float container,
                             float content, float marginEnd);

// ScrollbarAxis: which bars a scrolled box shows, and which ways the wheel
// moves it. Rust names the axis on the bar — `Scrollbar::vertical(&handle)`,
// `::horizontal`, `::both` — because there the bar is a separate overlay
// element hung off a scrolled div. Here the bar belongs to the box that
// scrolls, the way it does in the renderer under this tree, so the axis is
// asked of the box.
enum class ScrollbarAxis : uint8_t {
    Vertical,
    Horizontal,
    Both
};

using ScrollAxis = ScrollbarAxis;

// What the source Element passes from prepaint into paint. The integrated
// renderer produces the same geometry directly; these values expose that
// boundary for custom painters and deterministic tests.
struct AxisPrepaintState {
    Axis axis = Axis::Vertical;
    // The runtime has no retained Hitbox object; its bounds are the source
    // `bar_hitbox` value at this seam.
    Bounds barHitbox = {};
    Bounds bounds = {};
    float radius = 0;
    Rgba bg = {};
    Rgba border = {};
    Bounds thumbBounds = {};
    Bounds thumbFillBounds = {};
    Background thumbBg = {};
    float scrollSize = 0;
    float containerSize = 0;
    float thumbSize = 0;
    float marginEnd = 0;
    float trackWidth = 0;
    float visibilityOpacity = 0;
    float visibilityPosition = 0;
    bool visibilityRequested = false;
};

struct PrepaintState {
    Bounds hitbox = {};
    // Key of the renderer-owned ScrollbarState entry.
    int scrollbarStateId = 0;
    AxisPrepaintState states[2] = {};
    int statesLen = 0;
};

AxisPrepaintState ScrollbarPrepaintAxis(Axis axis, Bounds track, float offset,
                                        float containerSize, float contentSize,
                                        const ScrollbarThumbStyle& style);

struct Scrollbar {
    // The bare box, for a caller that wires the scroll itself.
    static El* New(Ctx* cx);

    // `div().overflow_scroll().track_scroll(&handle).child(Scrollbar::new(&handle))`
    // in one: the box that clips, the offsets it is scrolled to, the id it
    // scrolls under and where it reports a wheel, a track press or a drag of
    // the thumb. Everything a scrolled region needs is here, so a caller
    // cannot get half of it — a box with no `onScroll` takes no wheel at all,
    // and used to leave the page behind it scrolling instead.
    //
    // `id` is the element id the scroll is tracked under, which is what
    // `track_scroll` names. The offsets are the view's: the box reports where
    // it should now be and the caller stores it, rather than the box moving
    // itself.
    static El* New(Ctx* cx, Str id, float scrollY, float scrollX,
                   Listener onScroll, ScrollAxis axis = ScrollAxis::Vertical);
    static El* New(Ctx* cx, Str id, float scrollY, float scrollX,
                   Listener onScroll, ScrollAxis axis, ScrollbarMode mode);

    // The renderer-backed form of ScrollableElement::scrollbar: preserve the
    // caller's element as the viewport and attach the integrated scroll
    // handle/bar behavior to it. New(...) is Apply(...) with a fresh Div.
    static El* Apply(Ctx* cx, El* element, Str id, float scrollY, float scrollX,
                     Listener onScroll, ScrollAxis axis = ScrollAxis::Vertical);
    static El* Apply(Ctx* cx, El* element, Str id, float scrollY, float scrollX,
                     Listener onScroll, ScrollAxis axis, ScrollbarMode mode);

    // Source `Scrollbar::styles`, represented as a direct immutable style
    // value instead of a Rust build closure.
    static El* ApplyStyles(Ctx* cx, El* element, const ScrollbarStyles& styles);

    // The vertical case, which is most of them.
    static El* Vertical(Ctx* cx, Str id, float scrollY, Listener onScroll);
    static El* Vertical(Ctx* cx, Str id, float scrollY, Listener onScroll,
                        ScrollbarMode mode);
};
} // namespace gpui
#endif // GPUI_BASE_SCROLLBAR_H_
