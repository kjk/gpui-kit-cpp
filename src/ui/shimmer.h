#ifndef GPUI_SRC_UI_SHIMMER_H_
#define GPUI_SRC_UI_SHIMMER_H_
/* Themed text shimmer — crates/ui/src/shimmer.rs */

#include "ui/sizing.h"

namespace gpui {

namespace component {

// SHIMMER_LAYER_COUNT / DEFAULT_SHIMMER_SPREAD.
const int kShimmerLayerCount = 12;
const float kDefaultShimmerSpread = 0.3f;
// MIN_HIGHLIGHT_LIGHTNESS_GAP: the lightness difference below which a
// highlight would be indistinguishable from the text.
const float kMinHighlightLightnessGap = 0.1f;

// ShimmerSpread: the highlight half-width, either a fraction of the text
// width or a fixed length. Rust spells it as a payload enum; the POD port
// keeps the discriminator beside the one value both arms carry.
struct ShimmerSpread {
    enum class Kind : uint8_t {
        Relative,
        Absolute
    };

    Kind kind = Kind::Relative;
    float value = kDefaultShimmerSpread;

    static ShimmerSpread Relative(float fraction);
    static ShimmerSpread Absolute(float length);
};

inline bool operator==(ShimmerSpread a, ShimmerSpread b) {
    return a.kind == b.kind && a.value == b.value;
}
inline bool operator!=(ShimmerSpread a, ShimmerSpread b) {
    return !(a == b);
}

// The appearance and timing of a reusable text shimmer. Rust's builders
// consume `self`; the C++ value type returns a copy so one named style can be
// shared by several labels without retained ownership.
struct ShimmerStyle {
    float durationMs = 2000.f;
    Rgba highlightColor = {};
    bool hasHighlightColor = false;
    ShimmerSpread spread = {};
    bool reverse = false;
    bool once = false;

    static ShimmerStyle New();
    // A zero duration is clamped to one millisecond.
    ShimmerStyle Duration(float ms) const;
    ShimmerStyle HighlightColor(Rgba color) const;
    // A fraction is clamped to 0.05..=1.0; an absolute length has a
    // one-pixel minimum; a non-finite value leaves the spread alone.
    ShimmerStyle Spread(ShimmerSpread value) const;
    ShimmerStyle Spread(float fraction) const;
    ShimmerStyle Reverse(bool value) const;
    ShimmerStyle Once(bool value) const;
};

// Animation, as `loading_animation` builds it: one sweep of `durationMs`,
// looping in step with every other shimmer of the same duration unless the
// style asked for a single pass. GPUI's Animation is a retained value here
// too, so the fields the source sets are the fields this carries.
struct ShimmerAnimation {
    float durationMs = 0;
    // Animation::repeat_synced sets both; Animation::new(d) alone is a
    // one-shot, which GPUI spells `oneshot`.
    bool synced = false;
    bool oneshot = false;
};

ShimmerAnimation ShimmerLoadingAnimation(float durationMs, bool once);
inline ShimmerAnimation ShimmerStyleAnimation(const ShimmerStyle& style) {
    return ShimmerLoadingAnimation(style.durationMs, style.once);
}

// Where the animation has got to, 0..1. A looping style is synced to the
// shared clock — Animation::repeat_synced — so two labels sweep together; a
// one-shot style runs from the frame the element first appeared in.
float ShimmerPhase(Ctx* cx, uint32_t key, const ShimmerStyle& style);

// shimmer_highlight_color. The layer alpha is the per-layer share of the
// peak opacity: twelve of them composited reach 0.6 in a dark theme and 0.75
// in a light one. The C++ colour is eight bits a channel and a byte is
// truncated, not rounded — the rule the whole palette is written to — so
// twelve compounded layers land about a point and a half under the source's
// peak.
Rgba ShimmerHighlightColor(Rgba text, Rgba background, Rgba foreground,
                           bool dark, const Rgba* overrideColor);
// The float the alpha above is rounded from, for the arithmetic that
// composites the layers analytically rather than by overdrawing them.
float ShimmerLayerOpacity(bool dark);

// shimmer_band_bounds: the lit band of one layer, or nothing when the layer
// is off the end of the text or the box has no area. Answers whether a band
// exists and writes it to `out`.
bool ShimmerBandBounds(Bounds bounds, float phase, ShimmerSpread spread,
                       int layer, Bounds* out);

// Text with a smooth, theme-aware loading highlight.
//
// Rust keeps `StyledText` as the layout owner and paints each covered glyph
// again under twelve nested content masks. This tree has no per-glyph paint
// seam and no way to ask a run for its glyph positions while the element tree
// is being built, so the same visual result is reached analytically: a
// character's coverage count is the number of layers whose band contains it,
// the composited alpha is `1 - (1 - layerOpacity)^count`, and the highlight is
// mixed into the run's own colour over that character through El::Spans.
// Two consequences, both deliberate:
//
//   * a glyph's horizontal position is approximated by its index, so a run of
//     mixed advances (CJK beside Latin) sweeps at a slightly uneven rate;
//   * the run's colour cannot be read from the inherited text style at build
//     time, so `Fg` is what the highlight is composited over — unset means
//     the theme foreground, and a caller that recoloured the text (Marker
//     does) passes the same colour here.
struct ShimmerText {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str text = {};
    ShimmerStyle shimmerStyle = {};
    Str id = {};
    Rgba fg = {};
    bool hasFg = false;

    static ShimmerText* New(Ctx* cx, Str text);
    // An explicit animation identity, for sibling labels that read alike.
    ShimmerText* Id(Str value);
    ShimmerText* WithShimmerStyle(const ShimmerStyle& style);
    ShimmerText* Duration(float ms);
    ShimmerText* HighlightColor(Rgba color);
    ShimmerText* Spread(ShimmerSpread value);
    ShimmerText* Spread(float fraction);
    ShimmerText* Reverse(bool value = true);
    ShimmerText* Once(bool value = true);
    // Styled::text_color, and the colour the highlight is mixed into.
    ShimmerText* Fg(Rgba color);
    El* IntoEl();
};

} // namespace component
} // namespace gpui
#endif // GPUI_SRC_UI_SHIMMER_H_
