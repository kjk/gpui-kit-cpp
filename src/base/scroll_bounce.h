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
