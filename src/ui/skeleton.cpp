#include "ui/skeleton.h"
#include "base/motion.h"

namespace gpui {

namespace component {

// skeleton.rs: a two-second loop of bounce(ease_in_out).
static const float kSkeletonPeriodMs = 2000.f;

Skeleton* Skeleton::New(Ctx* cx) {
    Arena* a = cx->a;
    Skeleton* s = ArenaNew<Skeleton>(a);
    s->a = a;
    s->cx = cx;
    s->h = Rems(cx, 1.f); // h_4
    return s;
}

Skeleton* Skeleton::Secondary() {
    secondary = true;
    return this;
}

Skeleton* Skeleton::W(float v) {
    w = v;
    return this;
}

Skeleton* Skeleton::H(float v) {
    h = v;
    return this;
}

El* Skeleton::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    Background bg = th.tokens.skeleton;
    if (secondary) {
        bg = BackgroundOpacity(bg, 0.5f);
    }
    // `1 - delta * 0.5` of the element's opacity, pulsing there and back.
    // Rust names the animation "skeleton"; every block on a page shares the
    // phase, which is what makes a stack of them read as one thing loading.
    float delta = MotionRepeat(cx, MotionId(StrL("skeleton")),
                               kSkeletonPeriodMs, EaseBounceInOut);
    return Div(a)->W(w)->H(h)->Bg(bg)->Radius(4)->Opacity(1.f - delta * 0.5f);
}

} // namespace component
} // namespace gpui
