#include "ui/shimmer.h"
#include "base/motion.h"
#include <math.h>

namespace gpui {

namespace component {

// The width one character is assumed to advance, as a fraction of the font
// size. Rust resolves a band against the laid-out run's glyph positions; the
// element tree here is built before anything is measured, so an absolute
// spread — the only arm that is a length rather than a fraction — is resolved
// against this estimate. A relative spread never touches it.
static const float kShimmerAverageAdvance = 0.5f;

static bool ShimmerFinite(float v) {
    return v == v && v * 0.f == 0.f;
}

ShimmerSpread ShimmerSpread::Relative(float fraction) {
    ShimmerSpread out;
    out.kind = Kind::Relative;
    out.value = fraction;
    return out;
}

ShimmerSpread ShimmerSpread::Absolute(float length) {
    ShimmerSpread out;
    out.kind = Kind::Absolute;
    out.value = length;
    return out;
}

ShimmerStyle ShimmerStyle::New() {
    return ShimmerStyle{};
}

ShimmerStyle ShimmerStyle::Duration(float ms) const {
    ShimmerStyle out = *this;
    out.durationMs = ms > 1.f ? ms : 1.f;
    return out;
}

ShimmerStyle ShimmerStyle::HighlightColor(Rgba color) const {
    ShimmerStyle out = *this;
    out.highlightColor = color;
    out.hasHighlightColor = true;
    return out;
}

ShimmerStyle ShimmerStyle::Spread(ShimmerSpread value) const {
    ShimmerStyle out = *this;
    if (!ShimmerFinite(value.value)) {
        return out;
    }
    if (value.kind == ShimmerSpread::Kind::Relative) {
        float f = value.value;
        if (f < 0.05f) {
            f = 0.05f;
        }
        if (f > 1.f) {
            f = 1.f;
        }
        out.spread = ShimmerSpread::Relative(f);
        return out;
    }
    out.spread = ShimmerSpread::Absolute(value.value > 1.f ? value.value : 1.f);
    return out;
}

ShimmerStyle ShimmerStyle::Spread(float fraction) const {
    return Spread(ShimmerSpread::Relative(fraction));
}

ShimmerStyle ShimmerStyle::Reverse(bool value) const {
    ShimmerStyle out = *this;
    out.reverse = value;
    return out;
}

ShimmerStyle ShimmerStyle::Once(bool value) const {
    ShimmerStyle out = *this;
    out.once = value;
    return out;
}

ShimmerAnimation ShimmerLoadingAnimation(float durationMs, bool once) {
    ShimmerAnimation out;
    out.durationMs = durationMs;
    out.synced = !once;
    out.oneshot = once;
    return out;
}

float ShimmerPhase(Ctx* cx, uint32_t key, const ShimmerStyle& style) {
    ShimmerAnimation anim = ShimmerStyleAnimation(style);
    if (anim.durationMs <= 0) {
        return 0.f;
    }
    if (anim.oneshot) {
        return MotionAppear(cx, key, anim.durationMs, EaseLinear);
    }
    // Animation::repeat_synced: the phase comes from the shared clock, not
    // from the frame this element first appeared in, so every label of the
    // same duration sweeps together. MotionRepeat is the per-element repeat.
    double now = MotionNow(cx);
    double turns = now * 1000.0 / (double)anim.durationMs;
    double phase = turns - (double)(int64_t)turns;
    if (phase < 0) {
        phase += 1.0;
    }
    MotionWantsFrame(cx);
    return (float)phase;
}

float ShimmerLayerOpacity(bool dark) {
    float peak = dark ? 0.6f : 0.75f;
    return 1.f - powf(1.f - peak, 1.f / (float)kShimmerLayerCount);
}

Rgba ShimmerHighlightColor(Rgba text, Rgba background, Rgba foreground,
                           bool dark, const Rgba* overrideColor) {
    Rgba highlight = overrideColor
                         ? *overrideColor
                         : (dark ? RgbaMixOklab(text, foreground, 0.2f)
                                 : RgbaMixOklab(text, background, 0.2f));
    return RgbaOpacity(highlight, ShimmerLayerOpacity(dark));
}

bool ShimmerBandBounds(Bounds bounds, float phase, ShimmerSpread spread,
                       int layer, Bounds* out) {
    float width = bounds.w;
    if (width <= 0.f || bounds.h <= 0.f || layer < 0 ||
        layer >= kShimmerLayerCount) {
        return false;
    }
    float halfWidth = spread.kind == ShimmerSpread::Kind::Relative
                          ? width * spread.value
                          : spread.value;
    float padding = halfWidth / width + 0.05f;
    float center = (phase * (1.f + padding * 2.f) - padding) * width;
    float radius = halfWidth * (1.f - (float)layer / (float)kShimmerLayerCount);
    float left = center - radius;
    if (left < 0.f) {
        left = 0.f;
    }
    float right = center + radius;
    if (right > width) {
        right = width;
    }
    if (right <= left) {
        return false;
    }
    if (out) {
        out->x = bounds.x + left;
        out->y = bounds.y;
        out->w = right - left;
        out->h = bounds.h;
    }
    return true;
}

ShimmerText* ShimmerText::New(Ctx* cx, Str text) {
    Arena* a = cx->a;
    ShimmerText* s = ArenaNew<ShimmerText>(a);
    s->a = a;
    s->cx = cx;
    s->text = text;
    return s;
}

ShimmerText* ShimmerText::Id(Str value) {
    id = value;
    return this;
}

ShimmerText* ShimmerText::WithShimmerStyle(const ShimmerStyle& style) {
    shimmerStyle = style;
    return this;
}

ShimmerText* ShimmerText::Duration(float ms) {
    shimmerStyle = shimmerStyle.Duration(ms);
    return this;
}

ShimmerText* ShimmerText::HighlightColor(Rgba color) {
    shimmerStyle = shimmerStyle.HighlightColor(color);
    return this;
}

ShimmerText* ShimmerText::Spread(ShimmerSpread value) {
    shimmerStyle = shimmerStyle.Spread(value);
    return this;
}

ShimmerText* ShimmerText::Spread(float fraction) {
    shimmerStyle = shimmerStyle.Spread(fraction);
    return this;
}

ShimmerText* ShimmerText::Reverse(bool value) {
    shimmerStyle = shimmerStyle.Reverse(value);
    return this;
}

ShimmerText* ShimmerText::Once(bool value) {
    shimmerStyle = shimmerStyle.Once(value);
    return this;
}

ShimmerText* ShimmerText::Fg(Rgba color) {
    fg = color;
    hasFg = true;
    return this;
}

// How many of the twelve bands cover the point at `x`, which is what the
// source reaches by painting the glyph once per band it intersects.
static int ShimmerCoverage(Bounds run, float phase, ShimmerSpread spread,
                           float x) {
    int count = 0;
    Bounds band = {};
    for (int layer = 0; layer < kShimmerLayerCount; layer++) {
        if (!ShimmerBandBounds(run, phase, spread, layer, &band)) {
            continue;
        }
        if (x >= band.x && x < band.Right()) {
            count++;
        }
    }
    return count;
}

El* ShimmerText::IntoEl() {
    El* container = Div(a)->MinW(0);
    const Theme& th = ThemeNow(cx->app);
    Rgba base = hasFg ? fg : th.foreground;
    if (hasFg) {
        container->Fg(base);
    }
    El* run = TextEl(a, text);
    container->Child(run);
    if (MotionReduced()) {
        // Reduced motion keeps the text visible and asks for no frames.
        return container;
    }

    // The animation identity: an explicit id, or the text itself, which is
    // what `RenderOnce::render` falls back to.
    uint32_t key = len(id) > 0 ? MotionName(cx, id) : MotionName(cx, text);
    float phase = ShimmerPhase(cx, key, shimmerStyle);
    if (shimmerStyle.reverse) {
        phase = 1.f - phase;
    }

    const Rgba* override_ =
        shimmerStyle.hasHighlightColor ? &shimmerStyle.highlightColor : nullptr;
    Rgba highlight =
        ShimmerHighlightColor(base, th.background, th.foreground,
                              ThemeGet(cx->app) == ThemeMode::Dark, override_);
    float layerAlpha = (float)highlight.a / 255.f;

    // The run's own box, in the character-advance units the coverage test
    // uses. Only an absolute spread reads the font size; a relative one is a
    // fraction of this width whatever it is.
    const char* bytes = text.s;
    int n = len(text);
    int chars = 0;
    for (int i = 0; i < n; i++) {
        if (((uint8_t)bytes[i] & 0xC0) != 0x80) {
            chars++;
        }
    }
    if (chars == 0) {
        return container;
    }
    float advance = th.fontSize * kShimmerAverageAdvance;
    Bounds runBounds = {0.f, 0.f, (float)chars * advance, 1.f};

    TextSpan* spans = (TextSpan*)Alloc(a, chars * (int)sizeof(TextSpan));
    int nSpans = 0;
    int charIx = 0;
    int i = 0;
    while (i < n) {
        int next = i + 1;
        while (next < n && ((uint8_t)bytes[next] & 0xC0) == 0x80) {
            next++;
        }
        float x = ((float)charIx + 0.5f) * advance;
        int coverage =
            ShimmerCoverage(runBounds, phase, shimmerStyle.spread, x);
        if (coverage > 0) {
            float alpha = 1.f - powf(1.f - layerAlpha, (float)coverage);
            Rgba color = base;
            color.r = (uint8_t)((float)base.r * (1.f - alpha) +
                                (float)highlight.r * alpha);
            color.g = (uint8_t)((float)base.g * (1.f - alpha) +
                                (float)highlight.g * alpha);
            color.b = (uint8_t)((float)base.b * (1.f - alpha) +
                                (float)highlight.b * alpha);
            // Adjacent characters that came out the same colour share a run,
            // which is most of the band's shoulders.
            if (nSpans > 0 && spans[nSpans - 1].hi == i &&
                RgbaEq(spans[nSpans - 1].color, color)) {
                spans[nSpans - 1].hi = next;
            } else {
                TextSpan span;
                span.lo = i;
                span.hi = next;
                span.color = color;
                spans[nSpans++] = span;
            }
        }
        charIx++;
        i = next;
    }
    if (nSpans > 0) {
        run->Spans(spans, nSpans);
    }
    return container;
}

} // namespace component
} // namespace gpui
