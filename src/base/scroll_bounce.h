#ifndef GPUI_BASE_SCROLL_BOUNCE_H_
#define GPUI_BASE_SCROLL_BOUNCE_H_
/* Boundary displacement wrapper — crates/base/src/scroll_bounce.rs. */

#include "base/motion.h"
#include "base/scrollbar.h"

namespace gpui {

// NSScrollView on a trackpad, UIScrollView on a touch platform. The touch
// numbers are not UIKit constants. Elsewhere the return decays at 12.5/s
// (response 2π/12.5 seconds) and the edge stops 36 px out.
struct ScrollBounceMotion {
    float tracking = 0.075f;
    float responseMs = 502.655f;
    float maxStretch = 36.f;
    bool hasMaxStretch = true;

    static ScrollBounceMotion Default();
    float Tracking() const { return tracking; }
    float Response() const { return responseMs; }
    bool HasMaxStretch() const { return hasMaxStretch; }
    float MaxStretch() const { return maxStretch; }

    // Rust panics on a non-positive or NaN tracking or stretch. There is
    // no exception path here, so those values are left as they were.
    ScrollBounceMotion WithTracking(float value) const {
        ScrollBounceMotion out = *this;
        if (value > 0 && value == value) out.tracking = value;
        return out;
    }
    ScrollBounceMotion WithResponse(float ms) const {
        ScrollBounceMotion out = *this;
        out.responseMs = std::max(0.f, ms);
        return out;
    }
    ScrollBounceMotion WithMaxStretch(float px) const {
        ScrollBounceMotion out = *this;
        if (px > 0 && px == px) {
            out.maxStretch = px;
            out.hasMaxStretch = true;
        }
        return out;
    }
};

struct ScrollBouncePrepaintState {
    float offset = 0;
    float velocity = 0;
};

// The source element's rubber-band and critically damped return. IntoEl
// consumes a TouchPhase ScrollWheel stream and paints the displacement.
// Pull returns displacement which crossed back into content.
struct ScrollBouncePhysics {
    float position = 0;
    float velocity = 0;
    bool dragging = false;
    bool suppressMomentum = false;
    float extent = 0;
    ScrollBounceMotion motion = ScrollBounceMotion::Default();

    ScrollBouncePhysics() : motion(ScrollBounceMotion::Default()) {}

    float Offset() const;
    void Begin(float viewportExtent);
    float Pull(float delta);
    void Release();
    // Let go the way AppKit does: return at full speed, carrying `vel`
    // outward. Velocity back toward the content is dropped.
    void LetGo(float vel);
    void Reset();
    bool IsDragging() const { return dragging; }
    bool IsMoving() const {
        return !dragging && (position != 0 || velocity != 0);
    }
    bool Step(float seconds);
};

// Boundary displacement shared by inputs and scroll viewports.
// crates/base/src/overscroll.rs. Logical scroll stays clamped; this is only
// how far the text is drawn from where it scrolled to.
struct ScrollOverscroll {
    ScrollBouncePhysics physics;
    double sampledAt = 0;
    double lastPacketAt = 0;
    double movedAt = 0;
    float velocity = 0;
    Vec<float> moveDy;
    Vec<double> moveAt;
    bool hasSampled = false;
    bool hasLastPacket = false;
    bool hasMoved = false;
    bool resting = false;
    bool spent = false;

    void SetMotion(ScrollBounceMotion value);
    float Offset() const { return physics.Offset(); }
    // `scroll` moves the text by a delta and returns how much it used.
    // The delta is negative-down, the same sign Rust's ScrollHandle uses.
    // Returns whether the stretch changed.
    bool Packet(float dy, TouchPhase phase, float extent, double now,
                float (*scroll)(void* user, float dy), void* user);
    bool Step(double now);
    void Reset();
    ~ScrollOverscroll();
};

// One vertical wheel against a handle. The area keeps the gesture whenever
// it can scroll, including at its ends. A horizontal-dominant wheel is left
// alone. `lock` may be null. Returns whether the gesture was claimed.
bool ScrollBounceOwnedWheel(ScrollOverscroll* over, OngoingScroll* lock,
                            ScrollbarHandle handle, bool enabled,
                            const ScrollWheelEvent* ev, Ctx* cx,
                            Listener onScroll);

// Step the return. The answer is how far to shift the content this frame,
// and it is zero when bouncing is off, motion is reduced, or nothing
// overflows.
float ScrollBounceOwnedOffset(ScrollOverscroll* over, Window* win, bool enabled,
                              float maxExtent);

// A positive-down offset, viewport and content (El::ScrollY's sign) reported
// to the bounce as a negative-down ScrollbarHandle.
struct PositiveScroll {
    float* offset = nullptr;
    float* viewport = nullptr;
    float* content = nullptr;
    // After layout, copy the child's content height. A virtual list already
    // knows its content and leaves this false.
    bool measureContent = false;
};

ScrollbarHandle PositiveScrollHandle(PositiveScroll* slot);

// Viewport and content for a scroll box whose size is only known after
// layout. `offset` is positive-down and is what the wheel writes. The entity
// outlives the frame, so the next wheel still sees last frame's size.
struct ScrollBounceMetrics {
    float offset = 0;
    float viewport = 0;
    float content = 0;
    PositiveScroll slot = {};

    static void OnScroll(ScrollBounceMetrics* self, Ctx* cx,
                         const ScrollEvent* ev) {
        if (!self || !ev) return;
        self->offset = ev->offsetY;
        Notify(cx);
    }
};

inline Entity<ScrollBounceMetrics> ScrollBounceMetricsEntity(
    Ctx* cx, Str id, bool measureContent) {
    Entity<ScrollBounceMetrics> e = ElementStateEntity<ScrollBounceMetrics>(
        cx, id.s ? id : StrL("scroll-bounce"), StrL("ScrollBounceMetrics"));
    ScrollBounceMetrics* m = e.Get(cx);
    if (m) {
        m->slot.offset = &m->offset;
        m->slot.viewport = &m->viewport;
        m->slot.content = &m->content;
        m->slot.measureContent = measureContent;
    }
    return e;
}

// GPUI starts a normal touch pan only after its 8 px touch slop, but a touch
// catching a fling starts at zero displacement. A short catch should stop the
// old fling rather than turn a few fast pixels into a new one.
const float kCatchDragSlop = 8.f;

// State::short_drag_distance: how far a touch that started at zero
// displacement -- one catching a fling -- has moved, while it is still under
// the slop. Observe answers whether this packet ends such a short catch, in
// which case the momentum GPUI synthesizes from it must be suppressed.
struct ScrollBounceCatch {
    float distance = 0;
    bool tracking = false;

    bool Observe(TouchPhase phase, float deltaY);
};

// MOMENTUM_GAP: momentum arrives once per frame until it stops, so a longer
// silence means the suppressed stream has ended. Smooth-scrolling mouse
// drivers on macOS send precise deltas with no phase: they never send the
// Started that otherwise ends suppression, and would stay locked after one
// bounce.
const double kMomentumGapSeconds = 0.25;

// State::last_wheel_at and State::suppressed_direction: what ends a
// momentum suppression other than a new gesture's Started.
struct ScrollBounceSuppression {
    // When the last wheel packet arrived, or < 0 before the first.
    double lastWheelAt = -1;
    // The sign of the stream being suppressed, when it is known to be
    // momentum that cannot reverse; 0 suppresses both directions.
    float direction = 0;

    // State::release: release the edge. A stretch made outside a gesture is
    // momentum (or a phaseless wheel) hitting the edge; that stream only
    // pushes outward, so an inward packet is a new scroll and ends the
    // suppression. After a gesture's own release the suppressed momentum may
    // point either way.
    void Release(ScrollBouncePhysics* physics, bool fromRest);
    // A packet at `now` moving `deltaY`: records it, and answers whether it
    // ends the suppression — the stream paused for kMomentumGapSeconds, or
    // the packet points against a stream known to be one-way.
    bool Lifts(double now, float deltaY);
};

struct ScrollBounce {
    Ctx* cx = nullptr;
    Str id = {};
    El* child = nullptr;
    ScrollBounceMotion motion = ScrollBounceMotion::Default();
    ScrollbarHandle handle = {};
    bool enabled = false;
    // Own vertical wheel scrolling. Precise pixels can bounce; a line wheel
    // only scrolls and cancels a stretch. Off, the child scrolls itself.
    bool scrollWheel = true;
    Listener onScroll = {};
    PositiveScroll* laidOut = nullptr;

    static bool EnabledByDefault();
    static ScrollBounce* New(Ctx* cx, Str id, El* child);
    static ScrollBounce* New(Ctx* cx, Str id, ScrollbarHandle handle,
                             El* child);
    ScrollBounce* Enabled(bool value = true);
    ScrollBounce* ScrollWheel(bool value = true);
    ScrollBounce* Motion(ScrollBounceMotion value);
    ScrollBounce* OnScroll(Listener listener);
    // After layout, write the child's height (and content, when asked) into
    // `slot`. The slot has to outlive this frame.
    ScrollBounce* Measure(PositiveScroll* slot);
    El* IntoEl();
};

} // namespace gpui
#endif // GPUI_BASE_SCROLL_BOUNCE_H_
