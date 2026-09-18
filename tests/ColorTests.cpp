/* gpui::Hsla and the operations written on it — crates/gpui/src/color.rs's
 * two `From` impls, and the `Colorize` ones in crates/ui/src/theme/color.rs
 * that work in HSL rather than in the bytes this tree paints with.
 *
 * The mix and lighten cases are color.rs's own test_mix, test_lighten and
 * test_darken. Where Rust asserts a float that a colour here has already been
 * quantised into, what is asserted is the operation against the byte's own
 * value; the comment on each says so. */

#include "Test.h"

static bool Is(Rgba c, uint32_t hex) {
    return c.r == ((hex >> 16) & 0xff) && c.g == ((hex >> 8) & 0xff) &&
           c.b == (hex & 0xff);
}

// Two lightnesses one byte apart or closer, which is as near as eight bits a
// channel can hold one.
static bool NearByte(float a, float b) {
    float d = a - b;
    return (d < 0 ? -d : d) <= 1.f / 255.f;
}

static bool HexIs(Rgba c, const char* want) {
    Str got = RgbaToHex(GetTempArena(), c);
    return base::StrEq(got, want);
}

// impl From<Rgba> for Hsla, on the colours whose HSL is known by hand.
static void TheConversionIntoHslIsRusts() {
    Hsla red = HslaFromRgba(Rgb(0xff, 0, 0));
    utassertnear(red.h, 0.f);
    utassertnear(red.s, 1.f);
    utassertnear(red.l, 0.5f);
    utassertnear(red.a, 1.f);
    // Green sits a third of the way round, blue two thirds.
    utassertnear(HslaFromRgba(Rgb(0, 0xff, 0)).h, 1.f / 3.f);
    utassertnear(HslaFromRgba(Rgb(0, 0, 0xff)).h, 2.f / 3.f);
    // `l == 0. || l == 1.` reports no saturation at all, which is the arm
    // that keeps a division by nothing out of it.
    utassertnear(HslaFromRgba(Rgb(0, 0, 0)).s, 0.f);
    utassertnear(HslaFromRgba(Rgb(0xff, 0xff, 0xff)).s, 0.f);
    utassertnear(HslaFromRgba(Rgb(0xff, 0xff, 0xff)).l, 1.f);
    // A grey has no hue and no saturation, and its lightness is its level.
    Hsla grey = HslaFromRgba(Rgb(0x80, 0x80, 0x80));
    utassertnear(grey.h, 0.f);
    utassertnear(grey.s, 0.f);
    utassert(grey.l > 0.5f && grey.l < 0.505f);
    // Magenta's hue comes off the red arm as a negative remainder, and
    // rem_euclid brings it back round rather than leaving it below zero.
    utassertnear(HslaFromRgba(Rgb(0xff, 0, 0xff)).h, 5.f / 6.f);
    // Alpha rides along untouched.
    utassertnear(HslaFromRgba(Rgba8(0, 0, 0, 0x80)).a, 0x80 / 255.f);
}

// impl From<Hsla> for Rgba: nothing on the way in is clamped, the three
// channels are clamped on the way out, and the sixth is picked by
// `(h * 6.).floor()`.
static void TheConversionOutOfHslIsRusts() {
    Rgba red = HslaToRgba(Hsla{0.f, 1.f, 0.5f, 1.f});
    utassert(red.r == 0xff && red.g == 0 && red.b == 0 && red.a == 0xff);
    // A hue of exactly 1 is the sixth Rust catches with `0 | 6`, and is the
    // same red as a hue of 0.
    utassert(Is(HslaToRgba(Hsla{1.f, 1.f, 0.5f, 1.f}), 0xff0000));
    // Each sixth, at full saturation and half lightness.
    utassert(HexIs(HslaToRgba(Hsla{1.f / 6.f, 1.f, 0.5f, 1.f}), "#FFFF00"));
    utassert(HexIs(HslaToRgba(Hsla{2.f / 6.f, 1.f, 0.5f, 1.f}), "#00FF00"));
    utassert(HexIs(HslaToRgba(Hsla{3.f / 6.f, 1.f, 0.5f, 1.f}), "#00FFFF"));
    utassert(HexIs(HslaToRgba(Hsla{4.f / 6.f, 1.f, 0.5f, 1.f}), "#0000FF"));
    utassert(HexIs(HslaToRgba(Hsla{5.f / 6.f, 1.f, 0.5f, 1.f}), "#FF00FF"));
    // A saturation that ran past 1 lands on a colour rather than on nonsense,
    // because it is the channels that get clamped.
    utassert(Is(HslaToRgba(Hsla{0.f, 2.f, 0.5f, 1.f}), 0xff0000));
    // gpui::hsla() is the one place Rust clamps its arguments, so the same
    // four through it are pinned rather than passed on.
    Hsla clamped = HslaNew(2.f, -1.f, 3.f, 4.f);
    utassertnear(clamped.h, 1.f);
    utassertnear(clamped.s, 0.f);
    utassertnear(clamped.l, 1.f);
    utassertnear(clamped.a, 1.f);
}

// A colour that went out through HSL and back comes back as itself, or one
// byte short of it. Rust never makes this trip — its colour *is* an Hsla, and
// only `to_hex` and the GPU ever quantise it — so the byte is what keeping
// four of them per colour costs: the float that comes back is a hair under
// the one that went out, and ToByte truncates.
static void TheRoundTripKeepsTheColour() {
    const uint32_t hexes[] = {0xffffff, 0x000000, 0x0a0a0a, 0xfafafa, 0xe5e5e5,
                              0x737373, 0x171717, 0xef4444, 0x06b6d4, 0x22c55e,
                              0xeab308, 0x55a0fc, 0x1e293b, 0x0f172a};
    for (uint32_t hex : hexes) {
        Rgba c = RgbaHex(hex);
        Rgba back = HslaToRgba(HslaFromRgba(c));
        utassert(back.r <= c.r && c.r - back.r <= 1);
        utassert(back.g <= c.g && c.g - back.g <= 1);
        utassert(back.b <= c.b && c.b - back.b <= 1);
        utassert(back.a == c.a);
    }
    // A grey has no saturation to lose, so every one of them comes back
    // exactly — and near-grey is where the interface's own colours live.
    for (int v = 0; v < 256; v++) {
        Rgba c = Rgb((uint8_t)v, (uint8_t)v, (uint8_t)v);
        Rgba back = HslaToRgba(HslaFromRgba(c));
        utassert(back.r == c.r && back.g == c.g && back.b == c.b);
    }
}

// color.rs test_mix. Rust asserts these through `to_hex`; the bytes are the
// same bytes, and this asserts them where they are computed — a colour here
// is quantised already, so putting one back through `to_hex` re-enters HSL
// from bytes and costs the third case's green channel a step.
static void MixWalksTheShorterHueArc() {
    Rgba red = RgbaHex(0xff0000);
    Rgba blue = RgbaHex(0x0000ff);
    Rgba green = RgbaHex(0x00ff00);
    Rgba yellow = RgbaHex(0xffff00);
    utassert(Is(RgbaMixHsl(red, blue, 0.5f), 0xff00ff));
    utassert(Is(RgbaMixHsl(green, red, 0.5f), 0xffff00));
    utassert(Is(RgbaMixHsl(blue, yellow, 0.2f), 0x0098ff));
    // The plain channel blend is a different function and stays one: the same
    // two colours mixed in RGB come out half-lit, where the arc through HSL
    // keeps the lightness and lands on magenta.
    utassert(Is(RgbaMix(red, blue, 0.5f), 0x800080));
}

// Colorize::lighten / ::darken scale the lightness — color.rs test_lighten
// and test_darken, which walk these factors. Rust asserts the float that
// comes out (0.45, 0.675, 0.48); a colour here has been through eight bits a
// channel on the way in, so what is pinned is the scaling itself, against the
// lightness the bytes actually carry.
static void LightenAndDarkenScaleTheLightness() {
    // hsl(240, 5, 30), the colour test_lighten starts from.
    Rgba c = RgbaHsla(240.f / 360.f, 0.05f, 0.30f, 1.f);
    float l0 = HslaFromRgba(c).l;
    utassert(l0 > 0.29f && l0 < 0.31f);
    // Within a byte, which is as close as a lightness carried in eight bits
    // a channel can hold a factor.
    Rgba once = RgbaLighten(c, 0.5f);
    utassert(NearByte(HslaFromRgba(once).l, l0 * 1.5f));
    utassert(NearByte(HslaFromRgba(RgbaLighten(once, 0.5f)).l,
                      HslaFromRgba(once).l * 1.5f));
    // hsl(240, 5, 96) halved twice, which is test_darken.
    Rgba d = RgbaHsla(240.f / 360.f, 0.05f, 0.96f, 1.f);
    float d0 = HslaFromRgba(d).l;
    Rgba dark = RgbaDarken(d, 0.5f);
    utassert(NearByte(HslaFromRgba(dark).l, d0 * 0.5f));
    utassert(NearByte(HslaFromRgba(RgbaDarken(dark, 0.5f)).l,
                      HslaFromRgba(dark).l * 0.5f));
    // Neither touches the hue or the saturation. Pinned on a colour that has
    // both to spare: five per cent of saturation on a near-white does not
    // survive the trip through bytes, and that is the storage, not the
    // operation.
}

// impl Lerp for Hsla: the four channels straight, hue included — which is not
// the shorter arc that mix walks, and is why Rust says it is meant for
// interface colours that are near grey.
static void TheColourLerpWalksInHsl() {
    // Red halfway to blue goes the long way round through green, where the
    // same step in RGB would pass through a dull purple.
    utassert(Is(Lerp(Rgb(0xff, 0, 0), Rgb(0, 0, 0xff), 0.5f), 0x00ff00));
    // The colours it is meant for: two greys, halfway, is the grey between
    // them — the same answer either space gives.
    utassert(
        Is(Lerp(Rgb(0x0a, 0x0a, 0x0a), Rgb(0xfa, 0xfa, 0xfa), 0.5f), 0x828282));
    // The ends are the colours themselves, not a round trip through HSL.
    utassert(
        Is(Lerp(Rgb(0x12, 0x34, 0x56), Rgb(0xfe, 0xdc, 0xba), 0.f), 0x123456));
    utassert(
        Is(Lerp(Rgb(0x12, 0x34, 0x56), Rgb(0xfe, 0xdc, 0xba), 1.f), 0xfedcba));
    // Alpha walks with the rest.
    Rgba fade = Lerp(Rgba8(0, 0, 0, 0), Rgba8(0, 0, 0, 0xff), 0.5f);
    utassert(fade.a == 127);
}

// Colorize::lighten and ::darken on a colour with a hue and a saturation to
// keep: neither is touched, whatever the lightness does.
static void LightenAndDarkenKeepTheHue() {
    Rgba red = Rgb(0xff, 0, 0);
    Hsla dark = HslaFromRgba(RgbaDarken(red, 0.5f));
    utassertnear(dark.h, 0.f);
    utassertnear(dark.s, 1.f);
    utassert(NearByte(dark.l, 0.25f));
    Hsla light = HslaFromRgba(RgbaLighten(Rgb(0, 0x80, 0), 0.5f));
    utassertnear(light.h, 1.f / 3.f);
    utassertnear(light.s, 1.f);
}

void TestColor() {
    TestSuite("color");
    TheConversionIntoHslIsRusts();
    TheConversionOutOfHslIsRusts();
    TheRoundTripKeepsTheColour();
    MixWalksTheShorterHueArc();
    LightenAndDarkenScaleTheLightness();
    LightenAndDarkenKeepTheHue();
    TheColourLerpWalksInHsl();
}
