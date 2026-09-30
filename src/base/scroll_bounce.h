#ifndef GPUI_BASE_SCROLL_BOUNCE_H_
#define GPUI_BASE_SCROLL_BOUNCE_H_
/* Boundary displacement wrapper — crates/base/src/scroll_bounce.rs. */

#include "base/motion.h"

namespace gpui {

struct ScrollBounceMotion {
    float tracking = 0.55f;
    float responseMs = 524.f;

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
    ScrollBounceMotion motion = {};

    float Offset() const;
    void Begin(float viewportExtent);
    float Pull(float delta);
    void Release();
    bool Step(float seconds);
};

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
    ScrollBounceMotion motion = {};
    bool enabled = false;
    Listener onScroll = {};

    static ScrollBounce* New(Ctx* cx, Str id, El* child);
    ScrollBounce* Enabled(bool value = true);
    ScrollBounce* Motion(ScrollBounceMotion value);
    ScrollBounce* OnScroll(Listener listener);
    El* IntoEl();
};

} // namespace gpui
#endif // GPUI_BASE_SCROLL_BOUNCE_H_
