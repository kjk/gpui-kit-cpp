#include "ui/progress.h"
#include "base/motion.h"
#include "gpui/paint.h"

#include <math.h>

namespace gpui {

namespace component {

// progress.rs: a value that changes runs the theme's `duration_normal`
// along its `easing_move`, and the indeterminate sweep is a one-second loop
// of its own — a repeat is not a transition and takes no policy.
static const float kProgressLoopMs = 1000.f;

// Transition::new(motion.duration_normal).easing(motion.easing_move), which
// both the bar and the circle carry a value along.
static Motion ProgressMotion(const Theme& th) {
    return MotionNew(th.motion.durationNormalMs).Ease(th.motion.easingMove);
}

Progress* Progress::New(Ctx* cx) {
    Arena* a = cx->a;
    Progress* p = ArenaNew<Progress>(a);
    p->a = a;
    p->cx = cx;
    return p;
}

Progress* Progress::Value(float v) {
    value = ProgressClampValue(v);
    return this;
}
Progress* Progress::W(float v) {
    w = v;
    return this;
}
Progress* Progress::H(float v) {
    h = v;
    return this;
}
Progress* Progress::WithSize(UiSize s) {
    // progress.rs render: XSmall 4, Small 6, Medium 8, Large 10, Size(s) s.
    switch (s.kind) {
        case UiSize::Kind::XSmall:
            h = 4;
            break;
        case UiSize::Kind::Small:
            h = 6;
            break;
        case UiSize::Kind::Large:
            h = 10;
            break;
        case UiSize::Kind::Size:
            h = s.pixels;
            break;
        default:
            h = 8;
            break;
    }
    return this;
}
Progress* Progress::Loading(bool v) {
    loading = v;
    return this;
}
Progress* Progress::Id(Str v) {
    id = v;
    return this;
}
Progress* Progress::AccessibilityLabel(Str s) {
    accessibilityLabel = s;
    return this;
}

El* Progress::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    El* indicator = gpui::ProgressIndicator::New(cx)
                        ->H(h)
                        ->Radius(th.radiusFull)
                        ->Bg(th.tokens.progress);
    if (loading && MotionReduced()) {
        // Reduced motion: the bar says the same thing standing still, in the
        // middle of the track. A spinner keeps spinning under this setting —
        // a still spinner says the wrong thing — but an indeterminate bar
        // reads as "working" without the sweep.
        indicator->Absolute()
            ->Top(0)
            ->Left(0)
            ->Right(0)
            ->LeftRel(0.325f)
            ->RightRel(0.325f);
    } else if (loading) {
        // The indeterminate sweep: both edges are fractions of the track, and
        // the trailing one only starts moving halfway through the loop, so the
        // bar grows out of the left and then leaves by the right.
        float delta = MotionRepeat(cx, MotionId(StrL("progress-loading"), id),
                                   kProgressLoopMs);
        float start = EaseInOutQuad(ClampF01((delta - 0.5f) / 0.5f));
        float end = EaseInOutQuad(1.f - delta);
        indicator->Absolute()
            ->Top(0)
            ->Left(0)
            ->Right(0)
            ->LeftRel(start)
            ->RightRel(end);
    } else {
        // The indicator is a fraction of the track, the way Rust spells
        // w(relative(value / 100.)) — so it works whether the bar has a fixed
        // width or fills its parent. A value that moves takes 0.15 s to get
        // there rather than jumping.
        float v = MotionValue(cx, MotionId(StrL("progress"), id), value,
                              ProgressMotion(th));
        indicator->WFrac(v / 100.f);
    }
    El* root = gpui::Progress::New(cx, id.s ? id : StrL("progress"), value,
                                   loading, accessibilityLabel)
                   ->W(w);
    return root->Child(gpui::ProgressTrack::New(cx)
                           ->W(w)
                           ->H(h)
                           ->Radius(th.radiusFull)
                           ->Bg(BackgroundOpacity(th.tokens.progress, 0.2f))
                           ->Child(indicator));
}

ProgressCircle* ProgressCircle::New(Ctx* cx) {
    Arena* a = cx->a;
    ProgressCircle* p = ArenaNew<ProgressCircle>(a);
    p->a = a;
    p->cx = cx;
    return p;
}
ProgressCircle* ProgressCircle::Value(float v) {
    value = ProgressClampValue(v);
    return this;
}
ProgressCircle* ProgressCircle::Size(float v) {
    size = v;
    return this;
}
ProgressCircle* ProgressCircle::Color(Rgba c) {
    color = c;
    hasColor = true;
    return this;
}
ProgressCircle* ProgressCircle::Label(bool v) {
    showLabel = v;
    return this;
}
ProgressCircle* ProgressCircle::Loading(bool v) {
    loading = v;
    return this;
}
ProgressCircle* ProgressCircle::Id(Str v) {
    id = v;
    return this;
}
ProgressCircle* ProgressCircle::AccessibilityLabel(Str s) {
    accessibilityLabel = s;
    return this;
}

static void PaintCircleProgress(PaintCtx* ctx, El* e, void* user) {
    auto* p = (ProgressCircle*)user;
    if (!p || !ctx->rt) {
        return;
    }
    float cx = e->x + e->w * 0.5f;
    float cy = e->y + e->h * 0.5f;
    float r = (e->w < e->h ? e->w : e->h) * 0.42f;
    if (r < 3) {
        return;
    }
    float sw = r * 0.22f;
    if (sw < 1.5f) {
        sw = 1.5f;
    }
    Rgba col = p->hasColor ? p->color : ThemeNow(ctx->app).foreground;
    CanvasEllipse(ctx, cx, cy, r, r, sw, RgbaOpacity(col, 0.2f));
    float v = p->value;
    v = ClampF(v, 0, 100);
    if (v <= 0) {
        return;
    }
    float from = p->startValue;
    from = ClampF(from, 0, v);
    // render_circle(start, end): twelve o'clock is zero, and the arc runs
    // clockwise from wherever it starts.
    float start = -kPi * 0.5f + 2.f * kPi * (from / 100.f);
    float sweep = 2.f * kPi * ((v - from) / 100.f);
    Path* arc = PathNew(ctx, true);
    if (arc) {
        PathArcTo(arc, cx, cy, r, start, start + sweep, true);
        PathStroke(ctx, arc, sw, col);
        PathFree(arc);
    }
}

El* ProgressCircle::IntoEl() {
    float accessibilityValue = value;
    if (loading) {
        // The same sweep as the bar's, around a circle: the leading edge runs
        // the whole loop and the trailing one chases it from halfway.
        float delta = MotionRepeat(
            cx, MotionId(StrL("progress-circle-loading"), id), kProgressLoopMs);
        value = EaseInOutQuad(delta) * 100.f;
        startValue = EaseInOutQuad(ClampF01((delta - 0.5f) / 0.5f)) * 100.f;
    } else {
        value = MotionValue(cx, MotionId(StrL("progress-circle"), id), value,
                            ProgressMotion(ThemeNow(cx->app)));
        startValue = 0;
    }
    El* e = Div(a)
                ->Id(id.s ? id : StrL("progress-circle"))
                ->Role(AccessibilityRole::ProgressIndicator)
                ->AriaMinNumericValue(0)
                ->AriaMaxNumericValue(100)
                ->W(size)
                ->H(size)
                ->ItemsCenter()
                ->JustifyCenter();
    if (!loading) {
        e->AriaNumericValue(ProgressClampValue(accessibilityValue));
    }
    if (accessibilityLabel.s) {
        e->AriaLabel(accessibilityLabel);
    }
    e->customPaint = PaintCircleProgress;
    e->customUser = this;
    if (showLabel && size >= 28) {
        e->Child(TextEl(a, StrDup(a, fmt("%.0f%%", value)))
                     ->Font(size * 0.22f)
                     ->Fg(hasColor ? color : ThemeNow(cx->app).foreground));
    }
    return e;
}

} // namespace component
} // namespace gpui
