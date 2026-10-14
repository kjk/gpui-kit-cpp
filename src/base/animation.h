#ifndef GPUI_BASE_ANIMATION_H_
#define GPUI_BASE_ANIMATION_H_
/* Easing, interpolation and composable element effects —
   crates/base/src/animation.rs */

#include "gpui/gpui.h"

namespace gpui {

// cubic_bezier: Rust answers a closure over the four control points, so a
// caller names the curve once and calls it with t. There are no closures here,
// so the points travel with the sample.
//
// This is CSS's `cubic-bezier`: `x(s) = t` is solved for the curve parameter
// first — Newton, then bisection where the slope is no use — and `y(s)` is the
// answer. Reading `t` as the parameter and taking `y` off it directly, which
// is what this did before gpui-kit 5b3e18d1, makes every curve run
// slower than the same control points do in a browser.
//
// The output stays in [0, 1]. A curve whose y controls leave that range is
// sampled with UnclampedCubicBezier, which is what Easing::CubicBezier does.
float CubicBezier(float x1, float y1, float x2, float y2, float t);
// unclamped_cubic_bezier: the same solve, without the output clamp, so a y
// control outside 0..=1 keeps its anticipation or overshoot. Input progress
// is still clamped.
float UnclampedCubicBezier(float x1, float y1, float x2, float y2, float t);

// An easing curve. Rust takes `impl Fn(f32) -> f32`; the same thing here is a
// function pointer, which is what lets a Motion be a POD.
using EaseFn = float (*)(float);

// The identity, which is what Rust's tests ease with.
float EaseLinear(float t);
// Fast start, slow end. What a Motion defaults to, and what an entrance wants.
float EaseOutCubic(float t);
// Slow start, fast end. For an exit.
float EaseInCubic(float t);
// Slow at both ends. For something moving from one place to another.
float EaseInOutCubic(float t);

// GPUI's own easings, from crates/gpui/src/elements/animation.rs, which are
// what the components that loop are written against rather than the cubics
// above. Rust does not clamp these — the doc comment says so, to leave room
// for a curve that overshoots — so neither do these.
float EaseQuadratic(float t);
// gpui's ease_in_out: the quadratic pair, not the cubic one above it.
float EaseInOutQuad(float t);
float EaseOutQuint(float t);
// bounce(easing): the curve forwards over the first half and backwards over
// the second, which is what turns a loop into a pulse.
float EaseBounce(EaseFn e, float t);
// bounce(ease_in_out), which is the skeleton's.
float EaseBounceInOut(float t);

// 0..1, which is what an easing's parameter has to be. Rust spells it
// `.clamp(0., 1.)` at each site.
float ClampF01(float t);

// The Lerp trait. Rust implements it for f32, Pixels, Point<Pixels> and Hsla;
// Pixels is a float here, and the two aggregates are these overloads.
float Lerp(float a, float b, float t);
Point Lerp(Point a, Point b, float t);
// Rust interpolates the four HSLA channels, and says why: it is meant for
// transitions between near-grayscale interface colors, where the hue is not
// doing any work. This walks the same four, converting either side of the
// step through gpui::Hsla; the ends come back as they were given, since eight
// bits a channel cannot promise a round trip lands on the byte it started on.
Rgba Lerp(Rgba a, Rgba b, float t);

// animation.rs::EffectTransition. Rust stores a SmallVec of effects and hands
// a closure to GPUI's AnimationElement. A frame element cannot retain a
// closure here, so this arena builder samples the equivalent keyed one-shot
// state and writes that frame's values straight onto the element.
struct EffectTransition {
    float durationMs = 0;

    static EffectTransition* New(Ctx* cx, float durationMs);
    EffectTransition* Ease(EaseFn fn);
    EffectTransition* SlideY(float from, float to);
    EffectTransition* SlideX(float from, float to);
    EffectTransition* Fade(float from, float to);
    EffectTransition* Width(float from, float to);
    EffectTransition* Height(float from, float to);
    El* Apply(El* element, Str id);

  private:
    // Rust's TransitionEffect is private to animation.rs too: it is builder
    // storage, not another public component vocabulary.
    enum class EffectKind : uint8_t {
        SlideY,
        SlideX,
        Fade,
        Width,
        Height
    };
    struct Effect {
        EffectKind kind = EffectKind::Fade;
        float from = 0;
        float to = 0;
    };

    Ctx* cx = nullptr;
    EaseFn easing = EaseOutCubic;
    ArenaVec<Effect> effects;

    EffectTransition* Add(EffectKind kind, float from, float to);
};

// The deprecated upstream name remains source-compatible. motion.rs's other
// Transition is `Motion` here, so the two names do not collide in C++.
using Transition = EffectTransition;

} // namespace gpui
#endif // GPUI_BASE_ANIMATION_H_
