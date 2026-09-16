#include "base/scroll_bounce.h"

#include <math.h>

namespace gpui {

float ScrollBouncePhysics::Offset() const {
    if (!dragging) return position;
    float d = extent > 1 ? extent : 1;
    float tracked = motion.tracking;
    return position * tracked / (1.f + tracked * fabsf(position) / d);
}

void ScrollBouncePhysics::Begin(float viewportExtent) {
    float offset = Offset();
    if (offset == 0) extent = viewportExtent > 1 ? viewportExtent : 1;
    float denominator =
        motion.tracking * std::max(0.01f, 1.f - fabsf(offset) / extent);
    position = offset / denominator;
    velocity = 0;
    dragging = true;
    suppressMomentum = false;
}

float ScrollBouncePhysics::Pull(float delta) {
    float previous = position;
    float next = previous + delta;
    if (previous != 0 && ((previous < 0) != (next < 0))) {
        position = 0;
        return next;
    }
    position = next;
    return 0;
}

void ScrollBouncePhysics::Release() {
    position = Offset();
    dragging = false;
    if (position != 0) suppressMomentum = true;
}

bool ScrollBouncePhysics::Step(float seconds) {
    if (dragging || position == 0) return false;
    if (motion.responseMs <= 0) {
        position = 0;
        velocity = 0;
        return false;
    }
    const float tau = 6.2831853071795864769f;
    float omega = tau / (motion.responseMs / 1000.f);
    float decay = expf(-omega * std::max(0.f, seconds));
    float c = velocity + omega * position;
    position = (position + c * seconds) * decay;
    velocity = (velocity - omega * c * seconds) * decay;
    if (fabsf(position) < .1f && fabsf(velocity) < 1.f) {
        position = 0;
        velocity = 0;
        return false;
    }
    return true;
}

ScrollBounce* ScrollBounce::New(Ctx* cx, Str id, El* child) {
    ScrollBounce* out = ArenaNew<ScrollBounce>(cx->a);
    out->cx = cx;
    out->id = id;
    out->child = child;
    out->enabled = GPUI_OS_IOS || GPUI_OS_ANDROID;
    return out;
}

ScrollBounce* ScrollBounce::Enabled(bool value) {
    enabled = value;
    return this;
}

ScrollBounce* ScrollBounce::Motion(ScrollBounceMotion value) {
    motion = value;
    return this;
}

ScrollBounce* ScrollBounce::OnScroll(Listener listener) {
    onScroll = listener;
    return this;
}

El* ScrollBounce::IntoEl() {
    // The native mobile hosts are compile-only today and do not publish the
    // Started/Moved/Ended wheel stream this wrapper needs. Keep child layout
    // exact; ScrollBouncePhysics is the ready host-side policy rather than
    // guessing a release from desktop wheel momentum.
    return child ? Div(cx->a)->PathClick(id)->Child(child)
                 : Div(cx->a)->PathClick(id);
}

} // namespace gpui
