#include "base/animation.h"

// Declared in motion.h. Keeping these two declarations here avoids making
// animation.h and motion.h include one another merely for EffectTransition's
// implementation.
namespace gpui {
uint32_t MotionName(Ctx* cx, Str name);
float MotionAppear(Ctx* cx, uint32_t key, float durationMs, EaseFn ease);
} // namespace gpui

namespace gpui {

// The same three-line clamp scrollbar.cpp and slider.cpp each keep: base has
// no float clamp of its own, and one header for one expression is worse.
float CubicBezier(float x1, float y1, float x2, float y2, float t) {
    t = ClampF(t, 0.f, 1.f);
    // Polynomial form of the unit bezier, where p0 = (0, 0) and p3 = (1, 1).
    float cx = 3.f * x1;
    float cy = 3.f * y1;
    float bx = 3.f * (x2 - x1) - cx;
    float by = 3.f * (y2 - y1) - cy;
    float ax = 1.f - cx - bx;
    float ay = 1.f - cy - by;

    // Solve `x(s) = t` for the curve parameter `s`: Newton first, which
    // converges in a handful of steps over most of the curve, then bisection
    // for the flat stretches where the slope is no use. This is the step the
    // old sampling skipped — it read `t` as the curve's parameter and took
    // `y` straight off it, so every curve ran slower than the same control
    // points do in CSS.
    float s = t;
    bool solved = false;
    for (int i = 0; i < 8; i++) {
        float x = ((ax * s + bx) * s + cx) * s;
        float error = x - t;
        if (error < 0 ? -error < 1e-6f : error < 1e-6f) {
            solved = true;
            break;
        }
        float slope = (3.f * ax * s + 2.f * bx) * s + cx;
        if (slope < 0 ? -slope < 1e-6f : slope < 1e-6f) {
            break;
        }
        s = ClampF(s - error / slope, 0.f, 1.f);
    }
    if (!solved) {
        float low = 0.f;
        float high = 1.f;
        s = t;
        for (int i = 0; i < 32; i++) {
            float x = ((ax * s + bx) * s + cx) * s;
            float error = x - t;
            if (error < 0 ? -error < 1e-6f : error < 1e-6f) {
                break;
            }
            if (x < t) {
                low = s;
            } else {
                high = s;
            }
            s = (low + high) * 0.5f;
        }
    }
    return ((ay * s + by) * s + cy) * s;
}

float EaseLinear(float t) {
    return ClampF(t, 0.f, 1.f);
}

float EaseOutCubic(float t) {
    t = ClampF(t, 0.f, 1.f);
    float u = 1.f - t;
    return 1.f - u * u * u;
}

float EaseInCubic(float t) {
    t = ClampF(t, 0.f, 1.f);
    return t * t * t;
}

float EaseInOutCubic(float t) {
    t = ClampF(t, 0.f, 1.f);
    if (t < 0.5f) {
        return 4.f * t * t * t;
    }
    float u = -2.f * t + 2.f;
    return 1.f - (u * u * u) / 2.f;
}

float ClampF01(float t) {
    return ClampF(t, 0.f, 1.f);
}

float EaseQuadratic(float t) {
    return t * t;
}

float EaseInOutQuad(float t) {
    if (t < 0.5f) {
        return 2.f * t * t;
    }
    float x = -2.f * t + 2.f;
    return 1.f - x * x / 2.f;
}

float EaseOutQuint(float t) {
    float u = 1.f - t;
    return 1.f - u * u * u * u * u;
}

float EaseBounce(EaseFn e, float t) {
    if (!e) {
        e = EaseLinear;
    }
    return t < 0.5f ? e(t * 2.f) : e((1.f - t) * 2.f);
}

float EaseBounceInOut(float t) {
    return EaseBounce(EaseInOutQuad, t);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

Point Lerp(Point a, Point b, float t) {
    return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t)};
}

// `impl Lerp for Hsla`: the four channels interpolated straight, hue
// included. Rust's colour is an Hsla and stays one; ours is four bytes, so
// this converts either side, walks in HSL and converts back — which is the
// path Rust takes and not the one through RGB.
//
// The ends are handed back as they came in rather than through the round
// trip. Rust's `a + (b - a) * t` returns the endpoint's own floats at 0 and
// 1; eight bits a channel cannot promise that, and a transition that settled
// a byte away from its target would stay there.
Rgba Lerp(Rgba a, Rgba b, float t) {
    if (t <= 0.f) {
        return a;
    }
    if (t >= 1.f) {
        return b;
    }
    Hsla x = HslaFromRgba(a);
    Hsla y = HslaFromRgba(b);
    return HslaToRgba(Hsla{Lerp(x.h, y.h, t), Lerp(x.s, y.s, t),
                           Lerp(x.l, y.l, t), Lerp(x.a, y.a, t)});
}

EffectTransition* EffectTransition::New(Ctx* cx, float durationMs) {
    if (!cx || !cx->a) {
        return nullptr;
    }
    EffectTransition* out = ArenaNew<EffectTransition>(cx->a);
    out->cx = cx;
    out->durationMs = durationMs > 0 ? durationMs : 0;
    return out;
}

EffectTransition* EffectTransition::Ease(EaseFn fn) {
    easing = fn ? fn : EaseOutCubic;
    return this;
}

EffectTransition* EffectTransition::Add(EffectKind kind, float from, float to) {
    effects.Append(cx->a, Effect{kind, from, to});
    return this;
}

EffectTransition* EffectTransition::SlideY(float from, float to) {
    return Add(EffectKind::SlideY, from, to);
}

EffectTransition* EffectTransition::SlideX(float from, float to) {
    return Add(EffectKind::SlideX, from, to);
}

EffectTransition* EffectTransition::Fade(float from, float to) {
    return Add(EffectKind::Fade, from, to);
}

EffectTransition* EffectTransition::Width(float from, float to) {
    return Add(EffectKind::Width, from, to);
}

EffectTransition* EffectTransition::Height(float from, float to) {
    return Add(EffectKind::Height, from, to);
}

El* EffectTransition::Apply(El* element, Str id) {
    if (!element || !cx) {
        return element;
    }
    float delta = MotionAppear(cx, MotionName(cx, id), durationMs, easing);
    for (const Effect& effect : effects) {
        float value = Lerp(effect.from, effect.to, delta);
        switch (effect.kind) {
            case EffectKind::SlideY:
                element->Top(value);
                break;
            case EffectKind::SlideX:
                element->Left(value);
                break;
            case EffectKind::Fade:
                element->Opacity(value);
                break;
            case EffectKind::Width:
                element->W(value);
                break;
            case EffectKind::Height:
                element->H(value);
                break;
        }
    }
    return element;
}

} // namespace gpui
