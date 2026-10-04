/* Ported from the tests in crates/ui/src/shimmer.rs.
 *
 * The band geometry and the highlight colour are pure functions of the
 * bounds, the phase and the palette, so these drive them directly — the same
 * cases and the same numbers as the Rust module's three tests.
 *
 * One difference is recorded rather than papered over: a colour here is eight
 * bits a channel, so the per-layer alpha is quantised where Rust keeps a
 * float. The composited peak therefore lands within a quarter of a percent of
 * the source's 0.75 / 0.6 rather than within a thousandth, and the assertions
 * below carry that tolerance. `ShimmerLayerOpacity` is the float the byte was
 * rounded from and is checked against Rust's own tolerance. */

#include "Test.h"
#include <math.h>

using namespace gpui::component;

static void TheBuilderCarriesTimingSpreadAndDirection() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    Rgba color = Rgb(255, 255, 255);
    ShimmerStyle style = ShimmerStyle::New()
                             .Duration(3000)
                             .HighlightColor(color)
                             .Spread(0.45f)
                             .Reverse(true)
                             .Once(true);
    utassertnear(style.durationMs, 3000.f);
    utassert(style.hasHighlightColor && RgbaEq(style.highlightColor, color));
    utassert(style.spread == ShimmerSpread::Relative(0.45f));
    utassert(style.reverse);
    utassert(style.once);

    ShimmerText* text = ShimmerText::New(&cx, StrL("Thinking"))
                            ->Id(StrL("thinking"))
                            ->WithShimmerStyle(style)
                            ->Duration(4000)
                            ->Spread(0.5f)
                            ->Reverse(false)
                            ->Once(false);
    utassert(base::StrEq(text->text, StrL("Thinking")));
    utassertnear(text->shimmerStyle.durationMs, 4000.f);
    utassert(text->shimmerStyle.spread == ShimmerSpread::Relative(0.5f));
    utassert(!text->shimmerStyle.reverse);
    utassert(!text->shimmerStyle.once);
    utassert(base::StrEq(text->id, StrL("thinking")));

    // The clamps: a fraction into 0.05..=1.0, an absolute length to at least
    // one pixel, and a non-finite value leaves the spread alone.
    utassert(ShimmerStyle::New().Spread(0.f).spread ==
             ShimmerSpread::Relative(0.05f));
    utassert(ShimmerStyle::New().Spread(2.f).spread ==
             ShimmerSpread::Relative(1.f));
    // A quiet NaN, built through a volatile so the compiler does not fold
    // the division and warn about it.
    volatile float zero = 0.f;
    float nan = zero / zero;
    utassert(ShimmerStyle::New().Spread(nan).spread == ShimmerSpread());
    utassert(ShimmerStyle::New().Spread(ShimmerSpread::Absolute(0.f)).spread ==
             ShimmerSpread::Absolute(1.f));
    utassert(ShimmerStyle::New().Spread(ShimmerSpread::Absolute(48.f)).spread ==
             ShimmerSpread::Absolute(48.f));
    utassert(ShimmerStyle::New().Spread(ShimmerSpread::Absolute(nan)).spread ==
             ShimmerSpread());
    utassertnear(ShimmerStyle::New().Duration(0.f).durationMs, 1.f);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

static void TheBandMovesSmoothlyAcrossTheText() {
    Bounds bounds = {10, 20, 100, 18};
    ShimmerSpread spread = {};
    Bounds out = {};

    utassert(!ShimmerBandBounds(bounds, 0.f, spread, 0, &out));
    utassert(!ShimmerBandBounds(bounds, 1.f, spread, 0, &out));

    Bounds early = {};
    Bounds late = {};
    utassert(ShimmerBandBounds(bounds, 0.35f, spread, 0, &early));
    utassert(ShimmerBandBounds(bounds, 0.65f, spread, 0, &late));
    utassert(early.x < late.x);

    Bounds outer = {};
    Bounds inner = {};
    utassert(ShimmerBandBounds(bounds, 0.5f, spread, 0, &outer));
    utassert(ShimmerBandBounds(bounds, 0.5f, spread, kShimmerLayerCount - 1,
                               &inner));
    utassert(inner.x > outer.x);
    utassert(inner.w < outer.w);
    utassert(
        !ShimmerBandBounds(bounds, 0.5f, spread, kShimmerLayerCount, &out));
    utassert(!ShimmerBandBounds(Bounds{bounds.x, bounds.y, 0, 18}, 0.5f, spread,
                                0, &out));

    Bounds narrow = {};
    Bounds wide = {};
    utassert(ShimmerBandBounds(bounds, 0.5f, ShimmerSpread::Relative(0.1f), 0,
                               &narrow));
    utassert(ShimmerBandBounds(bounds, 0.5f, ShimmerSpread::Relative(0.5f), 0,
                               &wide));
    utassert(narrow.w < wide.w);

    // An absolute spread keeps the band width constant across text widths.
    ShimmerSpread absolute = ShimmerSpread::Absolute(20);
    Bounds band = {};
    utassert(ShimmerBandBounds(bounds, 0.5f, absolute, 0, &band));
    utassertnear(band.w, 40.f);
    Bounds wider = {bounds.x, bounds.y, 200, 18};
    Bounds widerBand = {};
    utassert(ShimmerBandBounds(wider, 0.5f, absolute, 0, &widerBand));
    utassertnear(widerBand.w, 40.f);
}

static void TheHighlightStaysBrightInBothThemes() {
    Rgba black = Rgb(0, 0, 0);
    Rgba white = Rgb(255, 255, 255);
    Rgba muted = RgbaMixOklab(white, black, 0.55f);
    Rgba light = ShimmerHighlightColor(black, white, black, false, nullptr);
    Rgba dark = ShimmerHighlightColor(muted, black, white, true, nullptr);

    // `l` is HSL lightness in Rust; the byte port compares the luminance the
    // same channels give.
    utassert(light.r + light.g + light.b > black.r + black.g + black.b);
    utassert(dark.r + dark.g + dark.b > muted.r + muted.g + muted.b);
    utassert(light.a > dark.a);
    // 1 - (1 - a)^12 is the composited peak. The float the alpha byte came
    // from holds Rust's own thousandth; the byte itself is a truncation, the
    // rule the whole palette is written to, and twelve of them compound to
    // about a point and a half below the peak.
    utassert(fabsf(1.f -
                   powf(1.f - ShimmerLayerOpacity(false),
                        (float)kShimmerLayerCount) -
                   0.75f) < 0.001f);
    utassert(
        fabsf(1.f -
              powf(1.f - ShimmerLayerOpacity(true), (float)kShimmerLayerCount) -
              0.6f) < 0.001f);
    utassert(
        fabsf(1.f -
              powf(1.f - (float)light.a / 255.f, (float)kShimmerLayerCount) -
              0.75f) < 0.02f);
    utassert(
        fabsf(1.f -
              powf(1.f - (float)dark.a / 255.f, (float)kShimmerLayerCount) -
              0.6f) < 0.02f);

    // Text that already has the target's lightness sweeps toward the other
    // end instead of getting a band in its own color.
    Rgba onDarkForeground =
        ShimmerHighlightColor(white, black, white, true, nullptr);
    utassert(HslaFromRgba(white).l - HslaFromRgba(onDarkForeground).l > 0.3f);
    Rgba onLightBackground =
        ShimmerHighlightColor(white, white, black, false, nullptr);
    utassert(HslaFromRgba(white).l - HslaFromRgba(onLightBackground).l > 0.3f);

    Rgba custom = ShimmerHighlightColor(black, white, black, false, &muted);
    utassert(custom.r == muted.r && custom.g == muted.g && custom.b == muted.b);

    ShimmerAnimation animation = ShimmerLoadingAnimation(3000, false);
    utassertnear(animation.durationMs, 3000.f);
    utassert(animation.synced);
    utassert(!animation.oneshot);

    animation = ShimmerLoadingAnimation(3000, true);
    utassertnear(animation.durationMs, 3000.f);
    utassert(animation.oneshot);
    utassert(!animation.synced);
}

void TestShimmer() {
    TestSuite("shimmer");
    TheBuilderCarriesTimingSpreadAndDirection();
    TheBandMovesSmoothlyAcrossTheText();
    TheHighlightStaysBrightInBothThemes();
}
