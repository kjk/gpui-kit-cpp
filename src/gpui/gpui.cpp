#include "gpui/gpui.h"
#include "gpui/keymap.h"
#include "base/scrollbar.h"
#include "gpui/image.h"
#include "gpui/paint.h"
#include "gpui/platform.h"
#include "base/positioner.h"
#include "base/text_boundary.h"
#include "gpui/svg.h"

#include <math.h>

// ─── color / theme ────────────────────────────────────────────────────────

namespace gpui {

// The float→byte rule the whole palette is written to: truncate, which is
// what `(rgb.r * 255.) as u32` does in `Colorize::to_hex` and everywhere else
// Rust turns one of its float colours into a byte. Rust keeps a colour as
// four floats and only quantises when it prints or paints; this tree keeps
// bytes, so the quantisation happens at every step — rounding it up half the
// time left the hex the theme viewer and the colour picker print one above
// the number Rust prints for the same colour.
static uint8_t ToByte(float v01) {
    if (v01 <= 0) {
        return 0;
    }
    return v01 >= 1.f ? 255 : (uint8_t)(v01 * 255.f);
}

Rgba RgbaOpacity(Rgba c, float a01) {
    if (a01 < 0) {
        a01 = 0;
    }
    if (a01 > 1) {
        a01 = 1;
    }
    c.a = (uint8_t)((float)c.a * a01);
    return c;
}

Rgba RgbaMix(Rgba a, Rgba b, float t) {
    if (t < 0) {
        t = 0;
    }
    if (t > 1) {
        t = 1;
    }
    Rgba o;
    o.r = (uint8_t)lroundf((float)a.r * t + (float)b.r * (1 - t));
    o.g = (uint8_t)lroundf((float)a.g * t + (float)b.g * (1 - t));
    o.b = (uint8_t)lroundf((float)a.b * t + (float)b.b * (1 - t));
    o.a = (uint8_t)lroundf((float)a.a * t + (float)b.a * (1 - t));
    return o;
}

static float Clamp01(float v) {
    if (v < 0) {
        return 0;
    }
    return v > 1 ? 1 : v;
}

// gpui::hsla().
Hsla HslaNew(float h, float s, float l, float a) {
    return Hsla{Clamp01(h), Clamp01(s), Clamp01(l), Clamp01(a)};
}

// `impl From<Hsla> for Rgba`, channel for channel. The branch is Rust's match
// on `(h * 6.).floor()`, including the arm that catches a hue of exactly 1
// and one that came out negative, and the clamp is Rust's — on the way out,
// not on the way in.
Rgba HslaToRgba(Hsla c) {
    float h = c.h, s = c.s, l = c.l;
    float k = (1.f - fabsf(2.f * l - 1.f)) * s;
    float x = k * (1.f - fabsf(fmodf(h * 6.f, 2.f) - 1.f));
    float m = l - k * 0.5f;
    float km = k + m;
    float xm = x + m;
    float r = 0, g = 0, b = 0;
    switch ((int)floorf(h * 6.f)) {
        case 0:
        case 6:
            r = km, g = xm, b = m;
            break;
        case 1:
            r = xm, g = km, b = m;
            break;
        case 2:
            r = m, g = km, b = xm;
            break;
        case 3:
            r = m, g = xm, b = km;
            break;
        case 4:
            r = xm, g = m, b = km;
            break;
        default:
            r = km, g = m, b = xm;
            break;
    }
    return Rgba{ToByte(Clamp01(r)), ToByte(Clamp01(g)), ToByte(Clamp01(b)),
                ToByte(c.a)};
}

// `impl From<Rgba> for Hsla`.
Hsla HslaFromRgba(Rgba c) {
    float r = (float)c.r / 255.f, g = (float)c.g / 255.f,
          b = (float)c.b / 255.f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float delta = mx - mn;
    float l = (mx + mn) * 0.5f;
    float s = 0;
    if (l != 0.f && l != 1.f) {
        s = l < 0.5f ? delta / (2.f * l) : delta / (2.f - 2.f * l);
    }
    float h = 0;
    if (delta != 0.f) {
        if (mx == r) {
            // rem_euclid, which is not fmod: a negative remainder comes back
            // on the positive side of the circle.
            h = fmodf((g - b) / delta, 6.f);
            if (h < 0) {
                h += 6.f;
            }
            h /= 6.f;
        } else if (mx == g) {
            h = ((b - r) / delta + 2.f) / 6.f;
        } else {
            h = ((r - g) / delta + 4.f) / 6.f;
        }
    }
    return Hsla{h, s, l, (float)c.a / 255.f};
}

Rgba RgbaHsla(float h, float s, float l, float a01) {
    return HslaToRgba(HslaNew(h, s, l, a01));
}

// Colorize::mix, which is bevy's HSL mix: the hue takes the shorter arc, the
// other three interpolate straight, and `factor` weights the receiver.
Rgba RgbaMixHsl(Rgba a, Rgba b, float factor) {
    factor = Clamp01(factor);
    float inv = 1.f - factor;
    Hsla x = HslaFromRgba(a);
    Hsla y = HslaFromRgba(b);
    // lerp_hue, in Rust's degrees: the difference brought into -180..180 so
    // the walk is the short way round, and the result back onto 0..360.
    float d = fmodf(y.h * 360.f - x.h * 360.f + 180.f, 360.f);
    if (d < 0) {
        d += 360.f;
    }
    d -= 180.f;
    float h = fmodf(x.h * 360.f + d * factor, 360.f);
    if (h < 0) {
        h += 360.f;
    }
    return HslaToRgba(Hsla{h / 360.f, x.s * factor + y.s * inv,
                           x.l * factor + y.l * inv, x.a * factor + y.a * inv});
}

Rgba RgbaWithHue(Rgba c, float h01) {
    Hsla hsl = HslaFromRgba(c);
    hsl.h = Clamp01(h01);
    return HslaToRgba(hsl);
}

// ─── background ───────────────────────────────────────────────────────────

Background BackgroundLinear(float angle, ColorStop from, ColorStop to) {
    Background b;
    // try_parse_theme_color: the flat colour a gradient stands in for is its
    // first stop, so a caller that only wants one colour gets a sensible one.
    b.color = from.color;
    b.from = from;
    b.to = to;
    b.angle = angle;
    b.gradient = true;
    return b;
}

Background BackgroundOpacity(Background b, float factor) {
    // RgbaOpacity scales what is already there, the way Colorize::opacity
    // does, so the two stops keep their ratio.
    b.color = RgbaOpacity(b.color, factor);
    if (b.gradient) {
        b.from.color = RgbaOpacity(b.from.color, factor);
        b.to.color = RgbaOpacity(b.to.color, factor);
    }
    return b;
}

// Hsla::alpha(a.min(max)): a ceiling, not a scale.
static Rgba CapAlpha(Rgba c, float max) {
    uint8_t cap = ToByte(max);
    if (c.a > cap) {
        c.a = cap;
    }
    return c;
}

Background BackgroundClampAlpha(Background b, float max) {
    b.color = CapAlpha(b.color, max);
    if (b.gradient) {
        b.from.color = CapAlpha(b.from.color, max);
        b.to.color = CapAlpha(b.to.color, max);
    }
    return b;
}

void BackgroundLine(const Background& b, Bounds box, Point* p0, Point* p1) {
    if (!p0 || !p1) {
        return;
    }
    float rad = b.angle * (kPi / 180.f);
    // CSS measures from "to top" and turns clockwise; y grows downward here,
    // so the direction the gradient runs in is (sin, -cos).
    float dx = sinf(rad);
    float dy = -cosf(rad);
    // The line is long enough for the corners to fall on its ends: the box
    // projected onto the direction.
    float len = fabsf(box.w * dx) + fabsf(box.h * dy);
    float cx = box.CenterX(), cy = box.CenterY();
    float sx = cx - dx * len * 0.5f, sy = cy - dy * len * 0.5f;
    p0->x = sx + dx * len * b.from.percentage;
    p0->y = sy + dy * len * b.from.percentage;
    p1->x = sx + dx * len * b.to.percentage;
    p1->y = sy + dy * len * b.to.percentage;
}

// ─── Colorize — crates/ui/src/theme/color.rs ─────────────────────────────

// gpui::transparent_black(), which every `mix_oklab` toward nothing takes.
Rgba RgbaTransparent() {
    return Rgba8(0, 0, 0, 0);
}

// gpui::Hsla::blend: `over` composited onto `base` by its own alpha. The
// result keeps the base's alpha, which is why `background.blend(x)` is opaque
// however faint `x` is.
Rgba RgbaBlend(Rgba base, Rgba over) {
    if (over.a >= 255) {
        return over;
    }
    if (over.a == 0) {
        return base;
    }
    float f = (float)over.a / 255.f;
    auto mix = [&](uint8_t b, uint8_t o) {
        return (uint8_t)((float)b * (1.f - f) + (float)o * f);
    };
    return Rgba8(mix(base.r, over.r), mix(base.g, over.g), mix(base.b, over.b),
                 base.a);
}

// Colorize::lighten / ::darken, which scale the HSL lightness rather than
// mixing toward white or black.
static Rgba ScaleLightness(Rgba c, float factor) {
    Hsla hsl = HslaFromRgba(c);
    hsl.l = Clamp01(hsl.l * factor);
    return HslaToRgba(hsl);
}

Rgba RgbaLighten(Rgba c, float amount) {
    return ScaleLightness(c, 1.f + Clamp01(amount));
}

Rgba RgbaDarken(Rgba c, float amount) {
    return ScaleLightness(c, 1.f - Clamp01(amount));
}

static float ToLinear(float c) {
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static float FromLinear(float c) {
    return c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1.f / 2.4f) - 0.055f;
}

static void RgbToOklab(Rgba c, float* L, float* A, float* B) {
    float lr = ToLinear((float)c.r / 255.f);
    float lg = ToLinear((float)c.g / 255.f);
    float lb = ToLinear((float)c.b / 255.f);
    float l = 0.4122214708f * lr + 0.5363325363f * lg + 0.0514459929f * lb;
    float m = 0.2119034982f * lr + 0.6806995451f * lg + 0.1073969566f * lb;
    float s = 0.0883024619f * lr + 0.2817188376f * lg + 0.6299787005f * lb;
    float l_ = cbrtf(l), m_ = cbrtf(m), s_ = cbrtf(s);
    *L = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
    *A = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
    *B = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;
}

static Rgba OklabToRgb(float L, float A, float B, float alpha) {
    float l_ = L + 0.3963377774f * A + 0.2158037573f * B;
    float m_ = L - 0.1055613458f * A - 0.0638541728f * B;
    float s_ = L - 0.0894841775f * A - 1.2914855480f * B;
    float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    float lr = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    float lg = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    float lb = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
    auto b8 = [](float v) { return ToByte(FromLinear(v)); };
    return Rgba8(b8(lr), b8(lg), b8(lb), ToByte(alpha));
}

// Colorize::mix_oklab, which is CSS `color-mix(in oklab, a factor%, b)`: the
// alpha is interpolated first and the Oklab channels are premultiplied by it,
// so mixing toward transparent fades without dragging the hue to black.
Rgba RgbaMixOklab(Rgba a, Rgba b, float factor) {
    factor = Clamp01(factor);
    float inv = 1.f - factor;
    float aa = (float)a.a / 255.f, ab = (float)b.a / 255.f;
    float alpha = aa * factor + ab * inv;
    if (alpha <= 0) {
        return RgbaTransparent();
    }
    // When one side is fully transparent it contributes nothing to the
    // premultiplied sum, so the mix is the other side's colour exactly and
    // only the alpha moves. Rust keeps its channels in f32 and never notices;
    // an `Rgba` here is eight bits a channel, so the `* aa * factor / alpha`
    // round trip through Oklab and back can land a channel on the far side of
    // a rounding boundary — which is a byte of drift that depends on the
    // platform's `cbrtf` and `powf`, not on the colour. Say what the maths
    // says instead.
    if (ab <= 0) {
        return Rgba8(a.r, a.g, a.b, ToByte(alpha));
    }
    if (aa <= 0) {
        return Rgba8(b.r, b.g, b.b, ToByte(alpha));
    }
    float l1, a1, b1, l2, a2, b2;
    RgbToOklab(a, &l1, &a1, &b1);
    RgbToOklab(b, &l2, &a2, &b2);
    float L = (l1 * aa * factor + l2 * ab * inv) / alpha;
    float A = (a1 * aa * factor + a2 * ab * inv) / alpha;
    float B = (b1 * aa * factor + b2 * ab * inv) / alpha;
    return OklabToRgb(L, A, B, alpha);
}

Str RgbaToHex(Arena* a, Rgba c, bool upper) {
    // Colorize::to_hex is written on an Hsla, so the digits are the ones its
    // conversion to Rgba produces — the round trip is the point, not an
    // accident.
    Rgba p = HslaToRgba(HslaFromRgba(c));
    if (c.a < 255) {
        return StrDup(a, upper ? fmt("#%02X%02X%02X%02X", p.r, p.g, p.b, p.a)
                               : fmt("#%02x%02x%02x%02x", p.r, p.g, p.b, p.a));
    }
    return StrDup(a, upper ? fmt("#%02X%02X%02X", p.r, p.g, p.b)
                           : fmt("#%02x%02x%02x", p.r, p.g, p.b));
}

struct RuntimeStyleState {
    RuntimeStyle style = {};
};

const RuntimeStyle& RuntimeStyleNow(const App* app) {
    static const RuntimeStyle fallback = {};
    RuntimeStyleState* state = AppGlobalEnsure<RuntimeStyleState>((App*)app);
    return state ? state->style : fallback;
}

void RuntimeStyleInstall(App* app, const RuntimeStyle& style) {
    RuntimeStyleState* state = AppGlobalEnsure<RuntimeStyleState>(app);
    if (state) {
        state->style = style;
    }
}

// ─── element builders ─────────────────────────────────────────────────────

static El* NewEl(Arena* a, ElKind k) {
    El* e = ArenaNew<El>(a);
    e->arena = a;
    e->kind = k;
    return e;
}

El* Div(Arena* a) {
    return NewEl(a, ElKind::Div);
}

El* TextEl(Arena* a, Str s) {
    El* e = NewEl(a, ElKind::Text);
    e->text = s;
    return e;
}

El* IconEl(Arena* a, IconName name) {
    El* e = NewEl(a, ElKind::Icon);
    e->icon = name;
    e->iconPath = IconNamePath(name);
    e->style.flexShrink = 0;
    return e;
}

El* IconEl(Arena* a, IconName name, float size) {
    El* e = IconEl(a, name);
    e->style.width = size;
    e->style.height = size;
    return e;
}

El* ImageEl(Arena* a, Str src, Str alt) {
    return ImageEl(a, ImageSource::FromResource(src), alt);
}

El* ImageEl(Arena* a, ImageSource source, Str alt) {
    El* e = NewEl(a, ElKind::Image);
    e->imageSource = source;
    e->text = alt;
    e->style.flexShrink = 0;
    return e;
}

ImageSource ImageSource::FromResource(Str resource) {
    ImageSource source;
    source.kind = ImageSourceKind::Resource;
    source.resource = resource;
    return source;
}

ImageSource ImageSource::FromRender(RenderImage* image) {
    ImageSource source;
    source.kind = ImageSourceKind::Render;
    source.render = image;
    return source;
}

ImageSource ImageSource::FromImage(const uint8_t* bytes, int len) {
    ImageSource source;
    source.kind = ImageSourceKind::Image;
    source.bytes = bytes;
    source.bytesLen = len;
    return source;
}

ImageSource ImageSource::FromCustom(ImageSourceLoader loader, void* user) {
    ImageSource source;
    source.kind = ImageSourceKind::Custom;
    source.loader = loader;
    source.user = user;
    return source;
}

Bounds ObjectFitBounds(ObjectFit fit, Bounds bounds, Size imageSize) {
    if (bounds.w <= 0 || bounds.h <= 0 || imageSize.w <= 0 ||
        imageSize.h <= 0) {
        return {};
    }
    if (fit == ObjectFit::Fill) {
        return bounds;
    }
    float sx = bounds.w / imageSize.w;
    float sy = bounds.h / imageSize.h;
    float scale = 1.f;
    if (fit == ObjectFit::Cover) {
        scale = sx > sy ? sx : sy;
    } else if (fit == ObjectFit::Contain) {
        scale = sx < sy ? sx : sy;
    } else if (fit == ObjectFit::ScaleDown) {
        scale = sx < sy ? sx : sy;
        if (scale > 1.f) {
            scale = 1.f;
        }
    }
    float w = imageSize.w * scale;
    float h = imageSize.h * scale;
    return {bounds.x + (bounds.w - w) * 0.5f, bounds.y + (bounds.h - h) * 0.5f,
            w, h};
}

El* ButtonEl(Arena* a, int clickId, Str label, BtnKind kind) {
    return ButtonSmall(a, clickId, label, kind, false);
}

El* ButtonSmall(Arena* a, int clickId, Str label, BtnKind kind, bool selected) {
    // Legacy arena-only helper: without an App it uses the runtime's neutral
    // light defaults. The component Button has the full active palette.
    const RuntimeStyle& th = RuntimeStyleNow(nullptr);
    El* b = Div(a)
                ->ItemsCenter()
                ->JustifyCenter()
                ->Radius(th.radius)
                ->Click(clickId)
                ->FocusId(clickId)
                ->FocusRing(true);
    if (kind == BtnKind::Primary) {
        b->PadX(16)
            ->PadY(8)
            ->Bg(th.legacyPrimary)
            ->HoverBg(th.legacyPrimaryHover);
        b->Child(TextEl(a, label)->Font(14)->Fg(th.legacyPrimaryForeground));
    } else if (kind == BtnKind::Outline) {
        b->PadX(16)->PadY(8)->Border(1, th.border)->HoverBg(th.legacyMuted);
        b->Child(TextEl(a, label)->Font(14)->Fg(th.foreground));
    } else {
        b->PadX(12)
            ->PadY(6)
            ->Bg(selected ? th.legacySecondaryActive : th.legacySecondary)
            ->HoverBg(th.legacySecondaryHover);
        b->Child(TextEl(a, label)
                     ->Font(selected ? 13.f : 14.f)
                     ->Fg(th.legacySecondaryForeground));
    }
    return b;
}

El* ProgressEl(Arena* a, float value01to100, float barW, float barH) {
    El* e = NewEl(a, ElKind::Progress);
    e->progress = value01to100;
    if (e->progress < 0) {
        e->progress = 0;
    }
    if (e->progress > 100) {
        e->progress = 100;
    }
    e->style.width = barW;
    e->style.height = barH;
    e->style.flexShrink = 0;
    e->style.radius = barH * 0.5f;
    return e;
}

El* ChartEl(Arena* a, const float* ys, int n, Rgba stroke, Rgba fillTop,
            Rgba fillBot, int tickMargin) {
    El* e = NewEl(a, ElKind::Chart);
    ChartSeries* chart = ArenaNew<ChartSeries>(a);
    e->chart = ArenaPtrOf(a, chart);
    chart->ys = ys;
    chart->n = n;
    chart->stroke = stroke;
    chart->fillTop = fillTop;
    chart->fillBot = fillBot;
    chart->tickMargin = tickMargin > 0 ? tickMargin : 15;
    e->style.flexGrow = 1;
    e->style.height = kFill;
    e->style.minH = 80;
    return e;
}

// The flex model is on for a box that says so, and for one that sets an
// alignment, a justification or a gap. Rust does not do the second half —
// `div().items_center()` there is a block container with a property that does
// nothing — but Rust callers write `h_flex()` when they mean a row, and this
// tree's callers wrote the alignment because until now every box was a flex
// container. Reading the intent from the alignment keeps those calls meaning
// what the person who wrote them saw, and leaves a bare `Div()` as the block
// container `div()` is.
El* El::Flex() {
    style.display = Display::Flex;
    return this;
}
El* El::FlexRow() {
    style.display = Display::Flex;
    style.dir = FlexDir::Row;
    return this;
}
El* El::FlexCol() {
    style.display = Display::Flex;
    style.dir = FlexDir::Col;
    return this;
}
El* El::FlexRowReverse() {
    style.display = Display::Flex;
    style.dir = FlexDir::RowReverse;
    return this;
}
El* El::FlexColReverse() {
    style.display = Display::Flex;
    style.dir = FlexDir::ColReverse;
    return this;
}
El* El::FlexWrap() {
    style.display = Display::Flex;
    style.flexWrap = true;
    return this;
}
El* El::Grow(float g) {
    style.flexGrow = g;
    return this;
}
El* El::Shrink0() {
    style.flexShrink = 0;
    return this;
}
El* El::Flex1() {
    style.flexGrow = 1;
    style.flexShrink = 1;
    style.flexBasis = 0;
    return this;
}
El* El::FlexNone() {
    style.flexGrow = 0;
    style.flexShrink = 0;
    style.flexBasis = kAuto;
    return this;
}
El* El::Basis(float v) {
    style.flexBasis = v;
    return this;
}
El* El::BasisFrac(float f) {
    style.flexBasisFrac = f;
    return this;
}
El* El::Shrink(float f) {
    style.flexShrink = f;
    return this;
}
El* El::W(float v) {
    style.width = v;
    return this;
}
El* El::WFrac(float f) {
    style.widthFrac = f;
    return this;
}
El* El::H(float v) {
    style.height = v;
    return this;
}
El* El::SizeFull() {
    style.width = kFill;
    style.height = kFill;
    style.flexGrow = 1;
    return this;
}
El* El::MinH(float v) {
    style.minH = v;
    return this;
}
El* El::MinW(float v) {
    style.minW = v;
    return this;
}
El* El::MaxW(float v) {
    style.maxW = v;
    return this;
}
El* El::MaxWFrac(float f) {
    style.maxWFrac = f;
    return this;
}
El* El::Aspect(float ratio) {
    style.aspect = ratio;
    return this;
}
El* El::MaxH(float v) {
    style.maxH = v;
    return this;
}
El* El::Gap(float v) {
    style.display = Display::Flex;
    style.gapX = v;
    style.gapY = v;
    return this;
}
El* El::GapX(float v) {
    style.display = Display::Flex;
    style.gapX = v;
    return this;
}
El* El::GapY(float v) {
    style.display = Display::Flex;
    style.gapY = v;
    return this;
}
El* El::Pad(float v) {
    style.pad = {v, v, v, v};
    return this;
}
El* El::PadX(float v) {
    style.pad.left = style.pad.right = v;
    return this;
}
El* El::PadY(float v) {
    style.pad.top = style.pad.bottom = v;
    return this;
}
El* El::PadL(float v) {
    style.pad.left = v;
    return this;
}
El* El::PadR(float v) {
    style.pad.right = v;
    return this;
}
El* El::PadT(float v) {
    style.pad.top = v;
    return this;
}
El* El::PadB(float v) {
    style.pad.bottom = v;
    return this;
}
El* El::Margin(float v) {
    style.margin = {v, v, v, v};
    return this;
}
El* El::MarginX(float v) {
    style.margin.left = style.margin.right = v;
    return this;
}
El* El::MarginY(float v) {
    style.margin.top = style.margin.bottom = v;
    return this;
}
El* El::MarginL(float v) {
    style.margin.left = v;
    return this;
}
El* El::MarginR(float v) {
    style.margin.right = v;
    return this;
}
El* El::MarginT(float v) {
    style.margin.top = v;
    return this;
}
El* El::MarginB(float v) {
    style.margin.bottom = v;
    return this;
}
El* El::ItemsCenter() {
    style.display = Display::Flex;
    style.align = FlexAlign::Center;
    return this;
}
El* El::ItemsStart() {
    style.display = Display::Flex;
    style.align = FlexAlign::Start;
    return this;
}
El* El::ItemsEnd() {
    style.display = Display::Flex;
    style.align = FlexAlign::End;
    return this;
}
El* El::ItemsStretch() {
    style.display = Display::Flex;
    style.align = FlexAlign::Stretch;
    return this;
}
El* El::SelfStart() {
    style.alignSelf = FlexAlign::Start;
    style.hasAlignSelf = true;
    return this;
}
El* El::SelfEnd() {
    style.alignSelf = FlexAlign::End;
    style.hasAlignSelf = true;
    return this;
}
El* El::SelfCenter() {
    style.alignSelf = FlexAlign::Center;
    style.hasAlignSelf = true;
    return this;
}
El* El::JustifyBetween() {
    style.display = Display::Flex;
    style.justify = Justify::SpaceBetween;
    return this;
}
El* El::JustifyAround() {
    style.display = Display::Flex;
    style.justify = Justify::SpaceAround;
    return this;
}
El* El::JustifyCenter() {
    style.display = Display::Flex;
    style.justify = Justify::Center;
    return this;
}
El* El::JustifyEnd() {
    style.display = Display::Flex;
    style.justify = Justify::End;
    return this;
}
El* El::JustifyStart() {
    style.display = Display::Flex;
    style.justify = Justify::Start;
    return this;
}
El* El::Bg(Background c) {
    style.bg = c;
    style.hasBg = true;
    return this;
}
El* El::Border(float width, Rgba c) {
    style.border = width;
    style.borderColor = c;
    return this;
}
El* El::BorderT(float width, Rgba c) {
    style.borderT = width;
    style.borderColor = c;
    return this;
}
El* El::BorderB(float width, Rgba c) {
    style.borderB = width;
    style.borderColor = c;
    return this;
}
El* El::BorderL(float width, Rgba c) {
    style.borderL = width;
    style.borderColor = c;
    return this;
}
El* El::BorderR(float width, Rgba c) {
    style.borderR = width;
    style.borderColor = c;
    return this;
}
El* El::Shadows(const BoxShadow* values, int count) {
    style.shadows = nullptr;
    style.shadowCount = 0;
    if (!values || count <= 0 || !arena ||
        count > 0x7fffffff / (int)sizeof(BoxShadow)) {
        return this;
    }
    BoxShadow* copy = (BoxShadow*)Alloc(arena, count * (int)sizeof(BoxShadow));
    if (!copy) {
        return this;
    }
    memcpy(copy, values, (size_t)count * sizeof(BoxShadow));
    style.shadows = copy;
    style.shadowCount = count;
    return this;
}
El* El::DashArray(float on, float off) {
    style.dashOn = on;
    style.dashOff = off;
    return this;
}
El* El::Radius(float r) {
    style.radius = r;
    return this;
}
El* El::Corners(float tl, float tr, float br, float bl) {
    style.corners = {tl, tr, br, bl};
    style.hasCorners = true;
    // `radius` stays what the uniform case reads, so anything that only knows
    // about one number — the focus ring's own corners — still lands near it.
    style.radius = tl > tr ? tl : tr;
    return this;
}
El* El::Fg(Rgba c) {
    style.color = c;
    style.hasColor = true;
    return this;
}
El* El::Font(float px) {
    style.fontSize = px;
    return this;
}
El* El::LineHeight(float mult) {
    style.lineHeight = mult;
    return this;
}
El* El::Truncate() {
    style.truncate = true;
    return this;
}
El* El::ClipY() {
    style.overflowY = Overflow::Hidden;
    return this;
}
El* El::ScrollY(float off) {
    style.overflowY = Overflow::Scroll;
    scrollY = off;
    return this;
}
El* El::ScrollX(float off) {
    style.overflowX = Overflow::Scroll;
    scrollX = off;
    return this;
}
El* El::ClipX() {
    style.overflowX = Overflow::Hidden;
    return this;
}
// The bar an element shows: its own when it named one, the theme's default
// otherwise.
static ScrollbarMode ElScrollMode(const El* e, const App* app) {
    return e->scrollModeSet ? e->scrollMode
                            : RuntimeStyleNow(app).scrollbarMode;
}

// The thumb's colour: `thumb_hover` under the pointer or in a drag, `thumb`
// otherwise, faded by however far through the Scrolling fade the bar is.
static Background ScrollbarThumbBg(const RuntimeStyle& th, bool hot,
                                   float alpha) {
    Background c = hot ? th.scrollbarThumbHover : th.scrollbarThumb;
    return alpha >= 1.f ? c : BackgroundOpacity(c, alpha);
}

enum class ScrollbarPaintState : uint8_t {
    Normal,
    HoverBar,
    HoverThumb,
    Active,
};

static Background ScrollbarThumbBg(const El* e, const RuntimeStyle& th,
                                   ScrollbarPaintState state, float alpha) {
    if (!e->scrollThemeSet) {
        return ScrollbarThumbBg(th,
                                state == ScrollbarPaintState::HoverThumb ||
                                    state == ScrollbarPaintState::Active,
                                alpha);
    }
    Background c =
        state == ScrollbarPaintState::Active
            ? e->scrollThumbActive
            : (state == ScrollbarPaintState::HoverThumb ? e->scrollThumbHover
                                                        : e->scrollThumb);
    return alpha >= 1.f ? c : BackgroundOpacity(c, alpha);
}

// The track behind it — `scrollbar.background`, which both default themes
// leave transparent.
static Background ScrollbarBarBg(const RuntimeStyle& th, float alpha) {
    Background c = th.scrollbarTrack;
    return alpha >= 1.f ? c : BackgroundOpacity(c, alpha);
}

static Background ScrollbarBarBg(const El* e, const RuntimeStyle& th,
                                 ScrollbarPaintState state, float alpha) {
    if (!e->scrollThemeSet) {
        return ScrollbarBarBg(th, alpha);
    }
    Background c =
        state == ScrollbarPaintState::Active ||
                state == ScrollbarPaintState::HoverThumb
            ? e->scrollTrackActive
            : (state == ScrollbarPaintState::HoverBar ? e->scrollTrackHover
                                                      : e->scrollTrack);
    return alpha >= 1.f ? c : BackgroundOpacity(c, alpha);
}

// clamp_thumb_radius: the theme's radius, never more than half the thumb.
static float ThumbRadius(const RuntimeStyle& th, float thumbW) {
    float r = th.radius;
    return r > thumbW * 0.5f ? thumbW * 0.5f : r;
}

static float ThumbRadius(const El* e, const RuntimeStyle& th,
                         ScrollbarPaintState state, float thumbW,
                         float thumbLength) {
    if (!e->scrollThemeSet) {
        float r = ThumbRadius(th, thumbW);
        return r > thumbLength * .5f ? thumbLength * .5f : r;
    }
    float r = state == ScrollbarPaintState::Active
                  ? e->scrollThumbActiveRadius
                  : (state == ScrollbarPaintState::HoverThumb
                         ? e->scrollThumbHoverRadius
                         : e->scrollThumbRadius);
    float max = thumbW < thumbLength ? thumbW * .5f : thumbLength * .5f;
    return r > max ? max : r;
}

static float ScrollbarTrackWidth(const El* e) {
    return e->scrollThemeSet ? e->scrollTrackWidth : kScrollbarBandW;
}

static float ScrollbarThumbWidth(const El* e, ScrollbarPaintState state) {
    if (!e->scrollThemeSet) {
        return state == ScrollbarPaintState::HoverThumb ||
                       state == ScrollbarPaintState::Active
                   ? kScrollbarThumbActiveW
                   : kScrollbarThumbW;
    }
    return state == ScrollbarPaintState::Active
               ? e->scrollThumbActiveWidth
               : (state == ScrollbarPaintState::HoverThumb
                      ? e->scrollThumbHoverWidth
                      : e->scrollThumbWidth);
}

static float ScrollbarThumbInset(const El* e, ScrollbarPaintState state) {
    if (!e->scrollThemeSet) return kScrollbarThumbMargin;
    return state == ScrollbarPaintState::Active
               ? e->scrollThumbActiveInset
               : (state == ScrollbarPaintState::HoverThumb
                      ? e->scrollThumbHoverInset
                      : e->scrollThumbInset);
}

static float ScrollbarThumbMinLength(const El* e, ScrollbarPaintState state) {
    if (!e->scrollThemeSet) return 48.f;
    return state == ScrollbarPaintState::Active
               ? e->scrollThumbActiveMinLength
               : (state == ScrollbarPaintState::HoverThumb
                      ? e->scrollThumbHoverMinLength
                      : e->scrollThumbMinLength);
}

// ─── the scrollbar's motion — crates/base/src/scrollbar.rs ───────────────
//
// A scrollbar appears and disappears rather than blinking: it fades in over
// `enter` with its position eased, holds for `idle`, and slides and fades out
// over `exit`. An interruption reverses from wherever it had got to, and the
// leg is shortened by the distance left to cover so it keeps its speed. The
// layout, the band and the press geometry never move — only what is painted.
//
// Rust keeps this in the scrollbar element's own keyed state; the tree here is
// rebuilt every frame, so it lives beside the tree and is found again by
// `El::ScrollId`. One entry per scroll area that has ever been drawn — a
// gallery has a handful — and they are dropped with the app.

// `ScalarTransition<T>`: a value on its way from `from` to `target`.
struct ScalarTransition {
    float from = 0;
    float target = 0;
    double startedAt = 0;
    float duration = 0;
};

// The two easings scrollbar.rs imports from `animation`. They are written out
// rather than reached for, because src/base sits above this file.
static float ScrollEaseOutCubic(float t) {
    float u = 1.f - t;
    return 1.f - u * u * u;
}

static float ScrollEaseInCubic(float t) {
    return t * t * t;
}

enum class ScrollCurve : uint8_t {
    Linear,
    InCubic,
    OutCubic
};

static float TransitionSample(const ScalarTransition& tr, double now,
                              ScrollCurve curve, bool* running) {
    if (tr.from == tr.target || tr.duration <= 0) {
        return tr.target;
    }
    float linear = (float)((now - tr.startedAt) / (double)tr.duration);
    if (linear >= 1.f) {
        return tr.target;
    }
    if (linear < 0) {
        linear = 0;
    }
    float f = linear;
    if (curve == ScrollCurve::InCubic) {
        f = ScrollEaseInCubic(linear);
    } else if (curve == ScrollCurve::OutCubic) {
        f = ScrollEaseOutCubic(linear);
    }
    if (running) {
        *running = true;
    }
    return tr.from + (tr.target - tr.from) * f;
}

static void TransitionStart(ScalarTransition* tr, float from, float target,
                            float duration, double now) {
    tr->from = from;
    tr->target = target;
    tr->startedAt = now;
    tr->duration = duration;
}

static void TransitionSettle(ScalarTransition* tr, float target, double now) {
    TransitionStart(tr, target, target, 0, now);
}

struct ScrollFade {
    int id = 0;
    float y = 0;
    float x = 0;
    // last_scroll_time: TimeNow() when the offset last changed, or when the
    // pointer last held the bar up. Unset until something scrolls, which is
    // what keeps a page from flashing every bar on it as it opens.
    double at = 0;
    bool hasLast = false;
    // VisibilityAnimation: what is drawn, and where it is drawn from.
    ScalarTransition opacity;
    ScalarTransition position;
    ScrollbarEntrance entrance = ScrollbarEntrance::Fade;
    // WidthAnimation, one per axis: the thumb growing to its hovered width
    // and settling back.
    ScalarTransition widthY;
    ScalarTransition widthX;
    bool widthYSet = false;
    bool widthXSet = false;
};

static Vec<ScrollFade> gScrollFades;

static ScrollFade* ScrollFadeFor(int id, float y, float x) {
    for (int i = 0; i < gScrollFades.len; i++) {
        if (gScrollFades[i].id == id) {
            return &gScrollFades[i];
        }
    }
    ScrollFade f;
    f.id = id;
    f.y = y;
    f.x = x;
    VecAppend(gScrollFades, f);
    return &gScrollFades[gScrollFades.len - 1];
}

void ScrollFadeClear() {
    VecReset(gScrollFades);
}

ScrollbarMotion ScrollbarMotionFor(ScrollbarMode mode) {
    ScrollbarMotion m;
    m.idle = 2.f;
    m.enter = 0.3f;
    m.exit = 0.5f;
    m.expand = 0.3f;
    // `with_thumb_hover_entrance`: hover mode slides, the other two fade.
    m.thumbHoverEntrance = mode == ScrollbarMode::Hover
                               ? ScrollbarEntrance::SlideAndFade
                               : ScrollbarEntrance::Fade;
    // Asked once. SPI_GETCLIENTAREAANIMATION is a system call, and this runs
    // per scrollable box per frame; src/base/motion.cpp caches it the same
    // way for the same reason.
    static int reduced = -1;
    if (reduced < 0) {
        reduced = PlatReduceMotion() ? 1 : 0;
    }
    if (reduced == 1) {
        // A motionless policy adopts its target outright, which is what a
        // zero duration means to every transition below.
        m.enter = 0;
        m.exit = 0;
        m.expand = 0;
    }
    return m;
}

// `VisibilityAnimation::sample`. Entering, the opacity is linear and the
// position eases out; leaving, both ease in.
static ScrollbarVisibility VisibilitySample(const ScrollFade* f, double now) {
    ScrollbarVisibility out;
    bool entering = f->opacity.target > f->opacity.from ||
                    f->position.target > f->position.from;
    out.opacity = TransitionSample(
        f->opacity, now, entering ? ScrollCurve::Linear : ScrollCurve::InCubic,
        &out.running);
    out.position = TransitionSample(
        f->position, now,
        entering ? ScrollCurve::OutCubic : ScrollCurve::InCubic, &out.running);
    return out;
}

// `VisibilityAnimation::set_visible`: reverse or start a leg toward `visible`.
// The leg runs for `enter` or `exit` scaled by the distance still to cover, so
// an interrupted transition keeps its speed instead of restarting.
static void VisibilitySetVisible(ScrollFade* f, bool visible,
                                 ScrollbarEntrance entrance, float enter,
                                 float exit, double now) {
    float target = visible ? 1.f : 0.f;
    float full = visible ? enter : exit;
    if (full <= 0) {
        TransitionSettle(&f->opacity, target, now);
        TransitionSettle(&f->position, target, now);
        f->entrance = entrance;
        return;
    }
    if (f->opacity.target == target && f->position.target == target &&
        f->entrance == entrance) {
        return;
    }
    ScrollbarVisibility s = VisibilitySample(f, now);
    // A fade entrance starts where a fade ends: fully in place, so only the
    // opacity moves.
    float fromPosition =
        (visible && entrance == ScrollbarEntrance::Fade) ? 1.f : s.position;
    float d1 = target - s.opacity;
    float d2 = target - fromPosition;
    d1 = d1 < 0 ? -d1 : d1;
    d2 = d2 < 0 ? -d2 : d2;
    float distance = d1 > d2 ? d1 : d2;
    float duration = full * distance;
    TransitionStart(&f->opacity, s.opacity, target, duration, now);
    TransitionStart(&f->position, fromPosition, target, duration, now);
    f->entrance = entrance;
}

void ScrollbarVisibilitySet(int scrollId, bool visible,
                            ScrollbarEntrance entrance, float enter, float exit,
                            double now) {
    ScrollFade* f = ScrollFadeFor(scrollId, 0, 0);
    VisibilitySetVisible(f, visible, entrance, enter, exit, now);
}

ScrollbarVisibility ScrollbarVisibilityAt(int scrollId, double now) {
    return VisibilitySample(ScrollFadeFor(scrollId, 0, 0), now);
}

float ScrollbarSlideOffset(float trackWidth, float position) {
    float p = position < 0 ? 0 : (position > 1 ? 1 : position);
    return trackWidth * (1.f - p);
}

// `WidthAnimation::set_target`, which eases out toward the width the state
// asks for. The first frame settles rather than animating, so a bar does not
// grow out of nothing the first time it is drawn.
static float WidthTarget(ScalarTransition* tr, bool* initialized, float target,
                         float duration, double now, bool* running) {
    if (duration <= 0 || !*initialized) {
        TransitionSettle(tr, target, now);
        *initialized = true;
    } else if (tr->target != target) {
        float from = TransitionSample(*tr, now, ScrollCurve::OutCubic, nullptr);
        TransitionStart(tr, from, target, duration, now);
    }
    return TransitionSample(*tr, now, ScrollCurve::OutCubic, running);
}

// `wants_visible`: an always-on bar, a drag, a hover in hover mode, or a
// scroll inside the idle hold.
static bool ScrollbarWantsVisible(ScrollbarMode mode, bool hovered,
                                  bool dragging, const ScrollFade* f,
                                  float idle, double now) {
    if (mode == ScrollbarMode::Always || dragging) {
        return true;
    }
    if (mode == ScrollbarMode::Hover && hovered) {
        return true;
    }
    return f->hasLast && (now - f->at) < (double)idle;
}

// `hover_keeps_visible`: the pointer holds a bar up in hover mode, and in
// scrolling mode only while it is already up.
static bool ScrollbarHoverKeepsVisible(ScrollbarMode mode, bool hovered,
                                       bool visible) {
    if (!hovered) {
        return false;
    }
    return mode == ScrollbarMode::Hover ||
           (mode == ScrollbarMode::Scrolling && visible);
}

// ─── the inspector's live style overrides ────────────────────────────────

struct StyleOverride {
    int clickId = 0;
    uint32_t fields = 0;
    Style style = {};
};

static Vec<StyleOverride> gStyleOverrides;

void StyleOverrideSet(int clickId, uint32_t fields, const Style& style) {
    if (clickId == 0) {
        return;
    }
    if (fields == 0) {
        StyleOverrideClear(clickId);
        return;
    }
    for (int i = 0; i < gStyleOverrides.len; i++) {
        if (gStyleOverrides[i].clickId == clickId) {
            gStyleOverrides[i].fields = fields;
            gStyleOverrides[i].style = style;
            return;
        }
    }
    StyleOverride o;
    o.clickId = clickId;
    o.fields = fields;
    o.style = style;
    VecAppend(gStyleOverrides, o);
}

void StyleOverrideClear(int clickId) {
    for (int i = 0; i < gStyleOverrides.len; i++) {
        if (gStyleOverrides[i].clickId == clickId) {
            for (int j = i + 1; j < gStyleOverrides.len; j++) {
                gStyleOverrides[j - 1] = gStyleOverrides[j];
            }
            gStyleOverrides.len--;
            return;
        }
    }
}

void StyleOverrideClearAll() {
    VecReset(gStyleOverrides);
}

void StyleApplyFields(Style* into, const Style& over, uint32_t fields) {
    if (!into || fields == 0) {
        return;
    }
    if (fields & StyleFieldBg) {
        into->bg = over.bg;
        into->hasBg = true;
    }
    if (fields & StyleFieldColor) {
        into->color = over.color;
        into->hasColor = true;
    }
    if (fields & StyleFieldBorderColor) {
        into->borderColor = over.borderColor;
    }
    if (fields & StyleFieldPad) {
        into->pad = over.pad;
    }
    if (fields & StyleFieldMargin) {
        into->margin = over.margin;
    }
    if (fields & StyleFieldGap) {
        into->gapX = over.gapX;
        into->gapY = over.gapY;
    }
    if (fields & StyleFieldRadius) {
        into->radius = over.radius;
    }
    if (fields & StyleFieldBorder) {
        into->border = over.border;
    }
    if (fields & StyleFieldBorderT) {
        into->borderT = over.borderT;
    }
    if (fields & StyleFieldBorderB) {
        into->borderB = over.borderB;
    }
    if (fields & StyleFieldBorderL) {
        into->borderL = over.borderL;
    }
    if (fields & StyleFieldBorderR) {
        into->borderR = over.borderR;
    }
    if (fields & StyleFieldFontSize) {
        into->fontSize = over.fontSize;
    }
    if (fields & StyleFieldWidth) {
        into->width = over.width;
    }
    if (fields & StyleFieldHeight) {
        into->height = over.height;
    }
    if (fields & StyleFieldOpacity) {
        into->opacity = over.opacity;
    }
    if (fields & StyleFieldHoverBg) {
        into->hoverBg = over.hoverBg;
        into->hasHoverBg = true;
    }
    if (fields & StyleFieldHoverFg) {
        into->hoverFg = over.hoverFg;
        into->hasHoverFg = true;
    }
    if (fields & StyleFieldActiveBg) {
        into->activeBg = over.activeBg;
        into->hasActiveBg = true;
    }
}

void StyleOverrideApply(El* e) {
    // Nothing picked, nothing edited: the common case costs one compare.
    if (gStyleOverrides.len == 0 || !e || e->clickId == 0) {
        return;
    }
    for (int i = 0; i < gStyleOverrides.len; i++) {
        const StyleOverride& o = gStyleOverrides[i];
        if (o.clickId != e->clickId) {
            continue;
        }
        StyleApplyFields(&e->style, o.style, o.fields);
        return;
    }
}

// How opaque a Scrolling bar is right now, and whether the window has to come
// back for the rest of the fade. `held` is the pointer resting on the bar,
// which Rust answers by stamping the time again.
El* El::AnchorFlip(bool on) {
    style.anchorFlip = on;
    return this;
}

El* El::Rotate(float turns) {
    style.rotate = turns;
    return this;
}

El* El::HideScrollbar() {
    noScrollbar = true;
    return this;
}

El* El::HideScrollbarX() {
    noScrollbarX = true;
    return this;
}

El* El::HideScrollbarY() {
    noScrollbarY = true;
    return this;
}

El* El::ScrollMask(Axis axis) {
    scrollMaskAxes |= axis == Axis::Horizontal ? 1 : 2;
    return this;
}

El* El::Opacity(float f) {
    style.opacity = f < 0 ? 0 : (f > 1 ? 1 : f);
    return this;
}

El* El::Grayscale(bool grayscale) {
    imageGrayscale = grayscale;
    return this;
}

El* El::ObjectFitMode(gpui::ObjectFit fit) {
    objectFit = fit;
    return this;
}

El* El::WithLoading(El* loading) {
    imageLoading = loading;
    return this;
}

El* El::WithFallback(El* fallback) {
    imageFallback = fallback;
    return this;
}

El* El::ScrollMode(ScrollbarMode m) {
    scrollModeSet = true;
    scrollMode = m;
    return this;
}
El* El::ScrollId(int v) {
    scrollId = v;
    scrollFromPath = false;
    return this;
}
El* El::ScrollFromPath() {
    scrollFromPath = true;
    return this;
}
El* El::Click(int v) {
    clickId = v;
    clickFromPath = false;
    return this;
}
El* El::Role(AccessibilityRole role) {
    accessibility.role = role;
    return this;
}
El* El::AccessibilityId(Str authorId) {
    accessibility.authorId = authorId;
    return this;
}
El* El::AriaLabel(Str label) {
    accessibility.label = label;
    return this;
}
El* El::AriaValue(Str value) {
    accessibility.value = value;
    return this;
}
El* El::AriaPlaceholder(Str placeholder) {
    accessibility.placeholder = placeholder;
    return this;
}
El* El::AriaDisabled(bool disabled) {
    accessibility.disabled = disabled;
    return this;
}
El* El::AriaToggled(AccessibilityToggled toggled) {
    accessibility.toggled = toggled;
    return this;
}
El* El::AriaSelected(bool selected) {
    accessibility.selected = selected;
    accessibility.hasSelected = true;
    return this;
}
El* El::AriaExpanded(bool expanded) {
    accessibility.expanded = expanded;
    accessibility.hasExpanded = true;
    return this;
}
El* El::AriaActiveDescendant(bool active) {
    accessibility.activeDescendant = active;
    return this;
}
El* El::AriaNumericValue(float value) {
    accessibility.numericValue = value;
    accessibility.hasNumericValue = true;
    return this;
}
El* El::AriaMinNumericValue(float value) {
    accessibility.minNumericValue = value;
    accessibility.hasMinNumericValue = true;
    return this;
}
El* El::AriaMaxNumericValue(float value) {
    accessibility.maxNumericValue = value;
    accessibility.hasMaxNumericValue = true;
    return this;
}
El* El::AriaNumericValueStep(float value) {
    accessibility.numericValueStep = value;
    accessibility.hasNumericValueStep = true;
    return this;
}
El* El::AriaOrientation(AccessibilityOrientation orientation) {
    accessibility.orientation = orientation;
    return this;
}
El* El::AriaPositionInSet(int position) {
    accessibility.positionInSet = position;
    accessibility.hasPositionInSet = true;
    return this;
}
El* El::AriaSizeOfSet(int size) {
    accessibility.sizeOfSet = size;
    accessibility.hasSizeOfSet = true;
    return this;
}
El* El::AriaRowCount(int count) {
    accessibility.rowCount = count;
    accessibility.hasRowCount = true;
    return this;
}
El* El::AriaColumnCount(int count) {
    accessibility.columnCount = count;
    accessibility.hasColumnCount = true;
    return this;
}
El* El::AriaRowIndex(int index) {
    accessibility.rowIndex = index;
    accessibility.hasRowIndex = true;
    return this;
}
El* El::AriaColumnIndex(int index) {
    accessibility.columnIndex = index;
    accessibility.hasColumnIndex = true;
    return this;
}
El* El::AriaLevel(int level) {
    accessibility.level = level;
    accessibility.hasLevel = true;
    return this;
}
El* El::OnAccessibilityDefault(Listener fn) {
    accessibilityDefault = fn;
    return this;
}
El* El::OnAccessibilityIncrement(Listener fn) {
    accessibilityIncrement = fn;
    return this;
}
El* El::OnAccessibilityDecrement(Listener fn) {
    accessibilityDecrement = fn;
    return this;
}
El* El::OnAccessibilityIncrement(Func0 fn) {
    accessibilityIncrementDirect = fn;
    return this;
}
El* El::OnAccessibilityDecrement(Func0 fn) {
    accessibilityDecrementDirect = fn;
    return this;
}
El* El::PathId(Str name) {
    id = name;
    clickFromPath = true;
    style.focusFromPath = true;
    return this;
}
El* El::PathClick(Str name) {
    id = name;
    clickFromPath = true;
    return this;
}
El* El::PathFocus(Str name) {
    id = name;
    style.focusFromPath = true;
    return this;
}
El* El::OnClick(Func0 fn) {
    onClick = fn;
    return this;
}
El* El::OnClick(Listener l) {
    listener = l;
    return this;
}
El* El::OnScroll(Listener l) {
    onScroll = l;
    return this;
}
El* El::OnHover(Listener l) {
    onHover = l;
    return this;
}
El* El::OnMouseMove(Listener l) {
    onMouseMove = l;
    return this;
}
El* El::OnMouseDown(Listener l, DispatchPhase phase) {
    onMouseDown = l;
    mouseDownPhase = phase;
    return this;
}
El* El::OnMouseUp(Listener l, DispatchPhase phase) {
    onMouseUp = l;
    mouseUpPhase = phase;
    return this;
}
El* El::OnDragMove(Listener l) {
    onDragMove = l;
    return this;
}
El* El::OnDrag(Str dragKind, int ix, void* data) {
    drag.kind = dragKind;
    drag.ix = ix;
    drag.data = data;
    return this;
}
El* El::OnMouseDownOut(Listener l) {
    onMouseDownOut = l;
    return this;
}
El* El::StopClick() {
    stopClick = true;
    return this;
}
El* El::StopMouseDown() {
    stopMouseDown = true;
    return this;
}
El* El::SuppressTextSelection() {
    suppressTextSelection = true;
    return this;
}
El* El::OnMouseUpOut(Listener l) {
    onMouseUpOut = l;
    return this;
}
El* El::OnDrop(Str acceptKind, Listener l) {
    dropKind = acceptKind;
    onDrop = l;
    return this;
}
ElStyleStates* El::StyleStates() {
    return ArenaPtrGet(arena, styleStates);
}
const ElStyleStates* El::StyleStates() const {
    return ArenaPtrGet(arena, styleStates);
}
ElStyleStates* El::EnsureStyleStates() {
    ElStyleStates* states = StyleStates();
    if (!states) {
        states = ArenaNew<ElStyleStates>(arena);
        styleStates = ArenaPtrOf(arena, states);
    }
    return states;
}
ChartSeries* El::Chart() {
    return ArenaPtrGet(arena, chart);
}
const ChartSeries* El::Chart() const {
    return ArenaPtrGet(arena, chart);
}
El* El::Hover(const StateStyle& s) {
    if (s.set) {
        ElStyleStates* states = EnsureStyleStates();
        StyleApplyFields(&states->hover, s.style, s.set);
        states->hoverSet |= s.set;
    }
    return this;
}

El* El::Active(const StateStyle& s) {
    if (s.set) {
        ElStyleStates* states = EnsureStyleStates();
        StyleApplyFields(&states->active, s.style, s.set);
        states->activeSet |= s.set;
    }
    return this;
}

El* El::Focus(const StateStyle& s) {
    if (s.set) {
        ElStyleStates* states = EnsureStyleStates();
        StyleApplyFields(&states->focus, s.style, s.set);
        states->focusSet |= s.set;
    }
    return this;
}
El* El::DragOver(Str dragKind, const StateStyle& s) {
    if (s.set) {
        ElStyleStates* states = EnsureStyleStates();
        states->dragOverKind = ArenaStrDup(arena, dragKind);
        StyleApplyFields(&states->dragOver, s.style, s.set);
        states->dragOverSet |= s.set;
    }
    return this;
}
El* El::Refine(const Style& s, uint32_t fields) {
    if (fields == 0) {
        return this;
    }
    // Two refinements on one element merge, the way StyleRefinement::refine
    // does: the second names what it names and leaves the rest.
    ElStyleStates* states = EnsureStyleStates();
    StyleApplyFields(&states->refine, s, fields);
    states->refineSet |= fields;
    return this;
}

El* El::BoundsOut(gpui::Bounds* out) {
    boundsOut = out;
    return this;
}
El* El::ReportLineSpan(float lineHeight) {
    lineSpan = true;
    lineSpanHeight = lineHeight;
    return this;
}
El* El::LineClamp(float cap, Listener onChange) {
    lineClamp = true;
    lineClampCap = cap > 0 ? cap : 0;
    onLineClamp = onChange;
    style.maxH = lineClampCap;
    style.overflowX = Overflow::Hidden;
    style.overflowY = Overflow::Hidden;
    return this;
}
El* El::Cursor(CursorKind c) {
    cursor = c;
    return this;
}
El* El::BindSlider(SliderState* s, Axis axis) {
    slider = s;
    sliderAxis = axis;
    return this;
}
El* El::BindSliderBounds(SliderState* s) {
    sliderBounds = s;
    return this;
}
El* El::BindInput(InputState* s) {
    input = s;
    // InputState::key_context: every binding state.rs installs is scoped to
    // it, so a field's own chords only resolve while a field has the
    // keyboard. Declared here rather than by each caller, since an element
    // bound to an InputState is the field.
    if (s) {
        if (style.focusId != 0) {
            // An explicitly named test/adapter handle is authoritative and
            // becomes the state's handle too.
            s->focus.id = style.focusId;
        } else if (!s->focus.IsValid()) {
            s->focus = FocusHandleNew((App*)nullptr);
        }
        InputInitKeys();
        KeyContext(InputContext());
        // A press on a field focuses it — `InputState::on_mouse_down` calls
        // `focus_handle.focus(window, cx)` — and focus is what stacks the
        // "Input" context over the keystroke, so every chord state.rs binds
        // resolves. Without it a clicked field took the characters (they go
        // to the focused InputState) but not the arrows, the escape or the
        // backspace, which are actions and so want the context.
        if (style.focusId == 0) {
            TrackFocus(s->focus);
        }
        FocusOnPress();
    }
    return this;
}
// InputElement paints the selection as a quad under the run and the caret as
// one on top of it. Both are measured against the shaped line, so a caret
// appearing and disappearing cannot shift the glyphs beside it.
El* El::SelRange(int lo, int hi, Rgba color) {
    selLo = lo;
    selHi = hi;
    selColor = color;
    return this;
}

El* El::CaretOut(float* outX, float* outY) {
    caretOutX = outX;
    caretOutY = outY;
    return this;
}
El* El::RangeOut(int lo, int hi, gpui::Bounds* out) {
    rangeOutLo = lo;
    rangeOutHi = hi;
    rangeOut = out;
    return this;
}
El* El::Washes(const TextSpan* runs, int n) {
    washes = runs;
    nWashes = n;
    return this;
}
El* El::Underlines(const TextSpan* runs, int n) {
    underlines = runs;
    nUnderlines = n;
    return this;
}
El* El::Spans(const TextSpan* runs, int n) {
    spans = runs;
    nSpans = n;
    return this;
}
int Utf8OffsetToUtf16(Str s, int u8) {
    if (u8 > s.len) {
        u8 = s.len;
    }
    int u16 = 0;
    int i = 0;
    while (i < u8) {
        unsigned char c = (unsigned char)s.s[i];
        int len = c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
        // Everything outside the basic plane is a surrogate pair over there.
        u16 += len == 4 ? 2 : 1;
        i += len;
    }
    return u16;
}

int Utf16OffsetToUtf8(Str s, int u16) {
    int at = 0;
    int i = 0;
    while (i < s.len && at < u16) {
        unsigned char c = (unsigned char)s.s[i];
        int len = c < 0x80 ? 1 : (c < 0xE0 ? 2 : (c < 0xF0 ? 3 : 4));
        at += len == 4 ? 2 : 1;
        i += len;
    }
    return i;
}

El* El::MarkRange(int lo, int hi) {
    markLo = lo;
    markHi = hi;
    return this;
}
El* El::Caret(int off, Rgba color, float width, bool lineEndAffinity) {
    caretOff = off;
    caretColor = color;
    caretW = width;
    caretLineEndAffinity = lineEndAffinity;
    return this;
}

BoxFill BoxFillFor(bool hasActiveBg, bool hasHoverBg, int clickId, int activeId,
                   int hoverId) {
    if (clickId == 0) {
        return BoxFill::Base;
    }
    if (hasActiveBg && clickId == activeId) {
        return BoxFill::Active;
    }
    if (hasHoverBg && clickId == hoverId) {
        return BoxFill::Hover;
    }
    return BoxFill::Base;
}

bool ClickFromRelease(bool pending, int pressedId, MouseButton pressedButton,
                      bool dragged, int upId, MouseButton upButton) {
    if (!pending) {
        return false;
    }
    if (dragged) {
        return false;
    }
    if (upButton != pressedButton) {
        return false;
    }
    return upId == pressedId;
}

bool ClickFromKeyRelease(bool pending, int pendingGen, int focusGen, int key,
                         bool modified) {
    if (!pending || modified) {
        return false;
    }
    if (key != KeyReturn && key != KeySpace) {
        return false;
    }
    // The focus moved between the two halves, so the element that would take
    // the click is not the one the key went down on.
    return pendingGen == focusGen;
}

int HashClickId(Str s) {
    uint32_t h = 2166136261u;
    if (s.s) {
        for (int i = 0; i < s.len; i++) {
            h ^= (uint8_t)s.s[i];
            h *= 16777619u;
        }
    }
    int id = (int)(h & 0x3fffffff);
    // Zero is what "nothing is hovered", "nothing is focused" and "no hit"
    // are spelled as, so it can never be an element's id. Nothing else is
    // reserved: this used to bump anything under 1000 clear of a band of
    // hand-assigned constants, and there are none left.
    if (id == 0) {
        id = 1;
    }
    return id;
}
El* El::Bold() {
    style.fontBold = true;
    style.fontWeight = (uint16_t)FontWeight::Bold;
    return this;
}
El* El::Semibold() {
    style.fontSemibold = true;
    style.fontWeight = (uint16_t)FontWeight::Semibold;
    return this;
}
El* El::Medium() {
    style.fontMedium = true;
    style.fontWeight = (uint16_t)FontWeight::Medium;
    return this;
}
El* El::Weight(FontWeight value) {
    style.fontWeight = (uint16_t)value;
    style.fontBold = value == FontWeight::Bold;
    style.fontSemibold = value == FontWeight::Semibold;
    style.fontMedium = value == FontWeight::Medium;
    return this;
}
El* El::Mono() {
    style.fontMono = true;
    return this;
}
El* El::Underline() {
    style.underline = true;
    return this;
}
El* El::Strikethrough() {
    style.strike = true;
    return this;
}
El* El::Italic() {
    style.italic = true;
    return this;
}
El* El::Selectable() {
    selectable = true;
    return this;
}
El* El::SelectionOwner(EntityId owner) {
    selectionOwner = owner;
    return this;
}
El* El::SelSrc(const SelSource* s, bool join) {
    selSrc = s;
    selJoin = join;
    return this;
}
El* El::Wrap() {
    style.wrap = true;
    return this;
}
El* El::Dashed() {
    style.borderDashed = true;
    return this;
}
El* El::Absolute() {
    style.absolute = true;
    return this;
}
El* El::Fixed() {
    style.absolute = true;
    style.fixed = true;
    return this;
}
El* El::Deferred() {
    style.deferred = true;
    return this;
}
El* El::DeferredLayer(int layer) {
    style.deferred = true;
    style.deferredLayer = (uint8_t)(layer > 0 ? layer : 0);
    return this;
}
El* El::AnchorBelow(float gap) {
    style.absolute = true;
    style.anchorBelow = true;
    style.anchorGap = gap;
    return this;
}
El* El::AnchorAbove(float gap) {
    style.absolute = true;
    style.anchorAbove = true;
    style.anchorGap = gap;
    return this;
}
El* El::AnchorCenterX() {
    style.absolute = true;
    style.anchorCenterX = true;
    return this;
}

El* El::AnchorCorner(Anchor anchor, float margin, float offsetY) {
    style.absolute = true;
    style.fixed = true;
    style.anchorCorner = true;
    style.anchor = anchor;
    style.anchorMargin = margin > 0 ? margin : 0;
    style.anchorGap = offsetY;
    return this;
}
El* El::Top(float v) {
    style.absTop = v;
    return this;
}
El* El::LeftRel(float frac) {
    style.absLeftRel = frac;
    return this;
}
El* El::RightRel(float frac) {
    style.absRightRel = frac;
    return this;
}
El* El::TopRel(float frac) {
    style.absTopRel = frac;
    return this;
}
El* El::BottomRel(float frac) {
    style.absBottomRel = frac;
    return this;
}
El* El::Left(float v) {
    style.absLeft = v;
    return this;
}
El* El::Bottom(float v) {
    style.absBottom = v;
    return this;
}
El* El::Right(float v) {
    style.absRight = v;
    return this;
}
El* El::HoverBg(Background c) {
    style.hoverBg = c;
    style.hasHoverBg = true;
    return this;
}
El* El::HoverFg(Rgba c) {
    style.hoverFg = c;
    style.hasHoverFg = true;
    return this;
}
El* El::ActiveBg(Background c) {
    style.activeBg = c;
    style.hasActiveBg = true;
    return this;
}

El* El::FocusOnPress(bool v) {
    style.focusOnPress = v;
    return this;
}

El* El::Group() {
    style.group = true;
    return this;
}

El* El::GroupHoverBg(Background c) {
    style.groupHoverBg = c;
    style.hasGroupHoverBg = true;
    return this;
}
El* El::GroupHoverVisible() {
    style.groupHoverVisible = true;
    return this;
}
El* El::FocusId(int v) {
    style.focusId = v;
    style.focusFromPath = false;
    return this;
}
El* El::TrackFocus(FocusHandle handle) {
    return FocusId(handle.id);
}
El* El::KeyContext(Str name) {
    style.keyContext = KeyContextOf(name);
    return this;
}
El* El::OnKeyDown(Listener fn) {
    return OnAction(ActionOf(StrL("gpui::KeyDown")), fn);
}
El* El::OnKeyUp(Listener fn) {
    return OnAction(ActionOf(StrL("gpui::KeyUp")), fn);
}
El* El::OnScrollWheel(Listener fn) {
    onScrollWheel = fn;
    return this;
}

El* El::OnClickAction(uint32_t action, intptr_t arg) {
    clickAction = action;
    clickActionArg = arg;
    return this;
}

El* El::OnAction(uint32_t action, Listener fn) {
    if (!action || !fn.IsValid()) {
        return this;
    }
    // Newest first, which reads the same way as adding a handler to a builder
    // and having it seen before the ones already there.
    ActionSlot* slot = ArenaNew<ActionSlot>(arena);
    slot->action = action;
    slot->fn = fn;
    slot->next = actions;
    actions = slot;
    return this;
}
El* El::TabIndex(int v) {
    style.tabIndex = v;
    return this;
}
El* El::TabStop(bool v) {
    style.tabStop = v;
    return this;
}
El* El::FocusRing(bool v) {
    style.focusRing = v;
    return this;
}
El* El::TrapId(int v) {
    style.trapId = v;
    return this;
}
El* El::Tip(Str s) {
    style.tooltip = s;
    return this;
}
El* El::Id(Str s) {
    id = s;
    return this;
}
El* El::Child(El* c) {
    if (!c) {
        return this;
    }
    c->next = nullptr;
    if (last) {
        last->next = c;
    } else {
        first = c;
    }
    last = c;
    return this;
}

// ─── measure / layout ─────────────────────────────────────────────────────

float PxToDip(PaintCtx* ctx, int px) {
    return (float)px * 96.f / (ctx->dpi > 0 ? ctx->dpi : 96.f);
}
int DipToPx(PaintCtx* ctx, float dip) {
    return (int)lroundf(dip * (ctx->dpi > 0 ? ctx->dpi : 96.f) / 96.f);
}

// Key wrap width: 0 = unconstrained. Round to 1 DIP so tiny parent-size
// jitter from extra layout passes still hits.
static float MeasKeyMaxW(float maxW, bool wrap) {
    if (!wrap || maxW <= 0) {
        return 0;
    }
    return floorf(maxW + 0.5f);
}

static float MeasKeyFont(float fontSize) {
    if (fontSize <= 0) {
        return 16.f;
    }
    return floorf(fontSize * 4.f + 0.5f) / 4.f;
}

static uint32_t MurmurHash2(const void* key, int n) {
    if (n <= 0) {
        return 0;
    }
    const uint32_t m = 0x5bd1e995;
    const int r = 24;
    uint32_t h = 5381u ^ (uint32_t)n;
    const uint8_t* data = (const uint8_t*)key;
    while (n >= 4) {
        uint32_t k = *(uint32_t*)data;
        k *= m;
        k ^= k >> r;
        k *= m;
        h *= m;
        h ^= k;
        data += 4;
        n -= 4;
    }
    switch (n) {
        case 3:
            h ^= data[2] << 16;
            [[fallthrough]];
        case 2:
            h ^= data[1] << 8;
            [[fallthrough]];
        case 1:
            h ^= data[0];
            h *= m;
            break;
        default:
            break;
    }
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;
    return h;
}

static uint32_t MurmurHash2(Str s) {
    return MurmurHash2(s.s, s.len);
}

struct TextMeasSlot {
    char* text = nullptr;
    int len = 0;
    uint32_t hash = 0;
    float fontSize = 0;
    float maxW = 0;
    // Line height multiplier; 0 = the default phi box (see kLineHeight).
    float lineH = 0;
    float w = 0;
    float h = 0;
    uint32_t lastUsed = 0;
    TextLayout* layout = nullptr;
    uint8_t wrap = 0;
    uint8_t bold = 0;
    uint8_t occupied = 0;
};

static uint32_t TextMeasHash(Str s, float fontSize, float maxW, bool wrap,
                             uint8_t weight, float lineH) {
    uint32_t h = MurmurHash2(s);
    uint32_t fs = 0;
    uint32_t mw = 0;
    uint32_t lh = 0;
    memcpy(&fs, &fontSize, sizeof(fs));
    memcpy(&mw, &maxW, sizeof(mw));
    memcpy(&lh, &lineH, sizeof(lh));
    h ^= fs * 0x9e3779b9u;
    h ^= mw * 0x85ebca6bu;
    h ^= lh * 0xc2b2ae35u;
    if (wrap) {
        h ^= 0x165667b1u;
    }
    if (weight) {
        h ^= 0x27d4eb2fu * (uint32_t)weight;
    }
    return h;
}

static bool TextMeasKeyEq(const TextMeasSlot* sl, uint32_t hash, Str s,
                          float fontSize, float maxW, bool wrap, uint8_t weight,
                          float lineH) {
    if (!sl->occupied || sl->hash != hash || sl->len != s.len) {
        return false;
    }
    if (sl->fontSize != fontSize || sl->maxW != maxW || sl->lineH != lineH ||
        sl->wrap != (wrap ? 1 : 0) || sl->bold != weight) {
        return false;
    }
    return StrEq(Str(sl->text, s.len), s);
}

static uint8_t ElTextWeight(const El* e) {
    uint8_t w = kFontWeightNormal;
    switch ((FontWeight)e->style.fontWeight) {
        case FontWeight::Thin:
            w = kFontWeightThin;
            break;
        case FontWeight::ExtraLight:
            w = kFontWeightExtraLight;
            break;
        case FontWeight::Light:
            w = kFontWeightLight;
            break;
        case FontWeight::Normal:
            w = kFontWeightExplicitNormal;
            break;
        case FontWeight::Medium:
            w = kFontWeightMedium;
            break;
        case FontWeight::Semibold:
            w = kFontWeightSemibold;
            break;
        case FontWeight::Bold:
            w = kFontWeightBold;
            break;
        case FontWeight::ExtraBold:
            w = kFontWeightExtraBold;
            break;
        case FontWeight::Black:
            w = kFontWeightBlack;
            break;
        default:
            break;
    }
    if (e->style.fontWeight == 0 && e->style.fontBold) {
        w = kFontWeightBold;
    } else if (e->style.fontWeight == 0 && e->style.fontSemibold) {
        w = kFontWeightSemibold;
    } else if (e->style.fontWeight == 0 && e->style.fontMedium) {
        w = kFontWeightMedium;
    }
    if (e->style.fontMono) {
        w |= kFontMono;
    }
    if (e->style.underline) {
        w |= kFontUnderline;
    }
    if (e->style.strike) {
        w |= kFontStrike;
    }
    if (e->style.italic) {
        w |= kFontItalic;
    }
    return w;
}

static void TextMeasFreeSlot(TextMeasSlot* sl) {
    if (!sl) {
        return;
    }
    if (sl->text) {
        StrFree(Str{sl->text, sl->len});
        sl->text = nullptr;
    }
    if (sl->layout) {
        TextLayoutRelease(sl->layout);
        sl->layout = nullptr;
    }
    sl->occupied = 0;
    sl->len = 0;
}

static TextMeasSlot* TextMeasFind(TextMeasCache* c, Str s, float fontSize,
                                  float maxW, bool wrap, uint8_t weight,
                                  float lineH, uint32_t* outHash) {
    float keyFont = MeasKeyFont(fontSize);
    float keyMaxW = MeasKeyMaxW(maxW, wrap);
    uint32_t hash = TextMeasHash(s, keyFont, keyMaxW, wrap, weight, lineH);
    if (outHash) {
        *outHash = hash;
    }
    if (!c->slots || c->cap <= 0) {
        return nullptr;
    }
    int mask = c->cap - 1;
    int i = (int)(hash & (uint32_t)mask);
    for (int n = 0; n < c->cap; n++) {
        TextMeasSlot* sl = &((TextMeasSlot*)c->slots)[i];
        if (!sl->occupied) {
            return nullptr;
        }
        if (TextMeasKeyEq(sl, hash, s, keyFont, keyMaxW, wrap, weight, lineH)) {
            return sl;
        }
        i = (i + 1) & mask;
    }
    return nullptr;
}

static void TextMeasInsertMove(TextMeasCache* c, TextMeasSlot* src);

static void TextMeasGrow(TextMeasCache* c, int minCap) {
    int cap = c->cap > 0 ? c->cap : 256;
    while (cap < minCap) {
        cap *= 2;
    }
    TextMeasSlot* old = (TextMeasSlot*)c->slots;
    int oldCap = c->cap;
    TextMeasSlot* neu = AllocArray<TextMeasSlot>(cap);
    if (!neu) {
        return;
    }
    c->slots = neu;
    c->cap = cap;
    c->used = 0;
    if (old) {
        for (int i = 0; i < oldCap; i++) {
            if (old[i].occupied) {
                TextMeasInsertMove(c, &old[i]);
            }
        }
        Free(nullptr, old);
    }
}

static void TextMeasInsertMove(TextMeasCache* c, TextMeasSlot* src) {
    if (!c->slots || c->cap <= 0) {
        return;
    }
    int mask = c->cap - 1;
    int i = (int)(src->hash & (uint32_t)mask);
    for (int n = 0; n < c->cap; n++) {
        TextMeasSlot* sl = &((TextMeasSlot*)c->slots)[i];
        if (!sl->occupied) {
            *sl = *src;
            sl->occupied = 1;
            c->used++;
            src->text = nullptr;
            src->layout = nullptr;
            src->occupied = 0;
            return;
        }
        i = (i + 1) & mask;
    }
    TextMeasFreeSlot(src);
}

static TextMeasSlot* TextMeasInsert(PaintCtx* ctx, Str s, float fontSize,
                                    float maxW, bool wrap, uint8_t weight,
                                    float lineH, float w, float h,
                                    TextLayout* layout) {
    TextMeasCache* c = &ctx->textCache;
    float keyFont = MeasKeyFont(fontSize);
    float keyMaxW = MeasKeyMaxW(maxW, wrap);
    uint32_t hash = TextMeasHash(s, keyFont, keyMaxW, wrap, weight, lineH);
    if (c->cap == 0 || (c->used + 1) * 10 > c->cap * 6) {
        TextMeasGrow(c, c->cap > 0 ? c->cap * 2 : 256);
    }
    if (!c->slots || c->cap <= 0) {
        return nullptr;
    }
    int mask = c->cap - 1;
    int i = (int)(hash & (uint32_t)mask);
    TextMeasSlot* sl = nullptr;
    for (int n = 0; n < c->cap; n++) {
        TextMeasSlot* cand = &((TextMeasSlot*)c->slots)[i];
        if (!cand->occupied) {
            sl = cand;
            break;
        }
        if (TextMeasKeyEq(cand, hash, s, keyFont, keyMaxW, wrap, weight,
                          lineH)) {
            sl = cand;
            break;
        }
        i = (i + 1) & mask;
    }
    if (!sl) {
        return nullptr;
    }
    if (!sl->occupied) {
        Str copy = StrDup(s);
        if (!copy.s) {
            return nullptr;
        }
        sl->text = copy.s;
        sl->len = copy.len;
        sl->hash = hash;
        sl->fontSize = keyFont;
        sl->maxW = keyMaxW;
        sl->lineH = lineH;
        sl->wrap = wrap ? 1 : 0;
        sl->bold = weight;
        sl->occupied = 1;
        c->used++;
    }
    sl->w = w;
    sl->h = h;
    sl->lastUsed = c->frame;
    if (layout && sl->layout != layout) {
        if (sl->layout) {
            TextLayoutRelease(sl->layout);
        }
        TextLayoutAddRef(layout);
        sl->layout = layout;
    }
    return sl;
}

void TextMeasBeginFrame(PaintCtx* ctx) {
    if (!ctx) {
        return;
    }
    ctx->textCache.frame++;
    if (ctx->textCache.frame == 0) {
        ctx->textCache.frame = 1;
    }
}

void TextMeasEndFrame(PaintCtx* ctx) {
    if (!ctx) {
        return;
    }
    TextMeasCache* c = &ctx->textCache;
    if (!c->slots || c->cap <= 0) {
        return;
    }
    uint32_t frame = c->frame;
    TextMeasSlot* slots = (TextMeasSlot*)c->slots;
    int cap = c->cap;

    // DirectWrite IDWriteTextLayout COM objects consume ~20 KB each of private
    // heap memory. Keeping thousands of them for off-screen rows wastes tens of
    // megabytes. We keep at most 256 active layout objects and release any
    // that have not been drawn in the last 30 frames (0.5s at 60 FPS).
    // The slot itself (string text, width, and height) remains intact in the
    // cache so MeasureText and subsequent layout passes continue to hit.
    const uint32_t kLayoutKeepFrames = 30;
    const int kMaxLiveLayouts = 256;
    int liveLayouts = 0;
    for (int i = 0; i < cap; i++) {
        if (!slots[i].occupied || !slots[i].layout) {
            continue;
        }
        if (frame > kLayoutKeepFrames &&
            slots[i].lastUsed + kLayoutKeepFrames < frame) {
            TextLayoutRelease(slots[i].layout);
            slots[i].layout = nullptr;
        } else {
            liveLayouts++;
        }
    }
    if (liveLayouts > kMaxLiveLayouts) {
        for (int i = 0; i < cap && liveLayouts > kMaxLiveLayouts; i++) {
            if (slots[i].occupied && slots[i].layout &&
                slots[i].lastUsed < frame) {
                TextLayoutRelease(slots[i].layout);
                slots[i].layout = nullptr;
                liveLayouts--;
            }
        }
    }

    TextMeasSlot* old = slots;
    int oldCap = cap;
    // Do not rebuild the table until it is large. Compacting every frame
    // that aged one slot out of a 90-frame window was the hitch on editor
    // scroll-back: alloc, rehash, and Release of every line just left, then
    // CreateTextLayout of the same lines on the way up. 4096 unique runs is
    // a long document's visible band visited several times over.
    const int kMaxKeep = 4096;
    if (c->used <= kMaxKeep) {
        return;
    }
    const uint32_t kKeep = 90;
    int keep = 0;
    int keepNow = 0;
    for (int i = 0; i < oldCap; i++) {
        if (!old[i].occupied) {
            continue;
        }
        if (old[i].lastUsed == frame) {
            keepNow++;
        }
        if (old[i].lastUsed + kKeep >= frame) {
            keep++;
        }
    }
    // A burst that filled the table in fewer than 90 frames still has to
    // shed something; keep only this frame's runs rather than none.
    uint32_t minKeep = (keep > kMaxKeep && keepNow > 0)
                           ? frame
                           : (frame > kKeep ? frame - kKeep : 1);
    if (minKeep == frame) {
        keep = keepNow;
    }
    if (keep == c->used) {
        return;
    }
    int newCap = c->cap;
    if (keep * 4 < newCap && newCap > 256) {
        newCap = 256;
        while (newCap < keep * 2) {
            newCap *= 2;
        }
    }
    TextMeasSlot* neu = AllocArray<TextMeasSlot>(newCap);
    if (!neu) {
        return;
    }
    c->slots = neu;
    c->cap = newCap;
    c->used = 0;
    for (int i = 0; i < oldCap; i++) {
        if (!old[i].occupied) {
            continue;
        }
        if (old[i].lastUsed < minKeep) {
            TextMeasFreeSlot(&old[i]);
            continue;
        }
        TextMeasInsertMove(c, &old[i]);
    }
    Free(nullptr, old);
}

void TextMeasClear(PaintCtx* ctx) {
    if (!ctx) {
        return;
    }
    TextMeasCache* c = &ctx->textCache;
    TextMeasSlot* slots = (TextMeasSlot*)c->slots;
    if (slots) {
        for (int i = 0; i < c->cap; i++) {
            if (slots[i].occupied) {
                TextMeasFreeSlot(&slots[i]);
            }
        }
        Free(nullptr, slots);
    }
    c->slots = nullptr;
    c->cap = 0;
    c->used = 0;
    c->frame = 0;
}

// Create or reuse a cached shaped run. Caller must TextLayoutRelease.
// `outCached` says whether the cache took a reference of its own, i.e. whether
// the run outlives the caller's; see El::laidLayout.
static TextLayout* TextMeasLayout(PaintCtx* ctx, Str s, float fontSize,
                                  float maxW, bool wrap, uint8_t weight,
                                  float lineH, Size* outSize,
                                  bool* outCached = nullptr) {
    if (outCached) {
        *outCached = false;
    }
    if (outSize) {
        outSize->w = 0;
        outSize->h =
            fontSize > 0 ? fontSize * (lineH > 0 ? lineH : kLineHeight) : 16.f;
    }
    if (!ctx || !ctx->pa || !s.s || s.len <= 0) {
        return nullptr;
    }
    // A run that does not wrap is the same size whatever width it was
    // measured against, which is why the cache key drops maxW for one. The
    // shaped run is not: the platform lays it out inside a box of that width
    // and draws it clipped to the box. Shaping it unconstrained is what makes
    // the key's premise true — a table cell whose min-content width was asked
    // for at one pixel would otherwise keep the one-pixel run it was given,
    // and paint a one-pixel smear where its text belongs. `truncate` does its
    // own cutting at paint time, against the box layout settled on.
    if (!wrap) {
        maxW = 0;
    }
    TextMeasCache* c = &ctx->textCache;
    TextMeasSlot* hit =
        TextMeasFind(c, s, fontSize, maxW, wrap, weight, lineH, nullptr);
    if (hit && hit->layout) {
        hit->lastUsed = c->frame;
        if (outCached) {
            *outCached = true;
        }
        if (outSize) {
            outSize->w = hit->w;
            outSize->h = hit->h;
        }
        TextLayoutAddRef(hit->layout);
        return hit->layout;
    }
    Size size = {};
    TextLayout* layout =
        TextLayoutNew(ctx, s, fontSize, maxW, wrap, weight, lineH, &size);
    if (!layout) {
        return nullptr;
    }
    if (outSize) {
        *outSize = size;
    }
    TextMeasSlot* sl = TextMeasInsert(ctx, s, fontSize, maxW, wrap, weight,
                                      lineH, size.w, size.h, layout);
    if (outCached) {
        *outCached = sl != nullptr;
    }
    return layout;
}

// The size alone, which is all the layout pass ever wants. Going through
// TextMeasLayout for it took a reference on the shaped run and gave it back
// one line later, and an IDWriteTextLayout's AddRef/Release pair is two
// interlocked ops on a shared cache line — 3% of a story frame, because
// taffy asks a text leaf for its size several times per pass and there are
// hundreds of them. On a cache hit the slot already holds the answer.
Size MeasureText(PaintCtx* ctx, Str s, float fontSize, float maxW, bool wrap,
                 int weight, float lineH) {
    Size size = {};
    size.h = fontSize > 0 ? fontSize * (lineH > 0 ? lineH : kLineHeight) : 16.f;
    if (!ctx || !ctx->pa || !s.s || s.len <= 0) {
        return size;
    }
    // Same premise as TextMeasLayout: a run that does not wrap measures the
    // same whatever width it was asked about, so the key drops maxW for one.
    if (!wrap) {
        maxW = 0;
    }
    TextMeasCache* c = &ctx->textCache;
    TextMeasSlot* hit = TextMeasFind(c, s, fontSize, maxW, wrap,
                                     (uint8_t)weight, lineH, nullptr);
    if (hit) {
        hit->lastUsed = c->frame;
        size.w = hit->w;
        size.h = hit->h;
        return size;
    }
    TextLayout* layout = TextMeasLayout(ctx, s, fontSize, maxW, wrap,
                                        (uint8_t)weight, lineH, &size);
    if (layout) {
        TextLayoutRelease(layout);
    }
    return size;
}

bool TextPointAt(PaintCtx* ctx, Str s, float fontSize, float maxW, bool wrap,
                 int off, float* outX, float* outY, float* outH, bool mono,
                 float lineHeight, bool lineEndAffinity) {
    if (!ctx) {
        return false;
    }
    // An empty line has no layout to measure, and asking for one would fail;
    // the only point in it is its start.
    if (s.len <= 0) {
        *outX = 0;
        *outY = 0;
        *outH = fontSize;
        return true;
    }
    uint8_t weight = mono ? (uint8_t)kFontMono : (uint8_t)0;
    if (off < 0) {
        off = 0;
    }
    if (off > s.len) {
        off = s.len;
    }
    TextLayout* tl = TextMeasLayout(ctx, s, fontSize, maxW, wrap, weight,
                                    lineHeight, nullptr);
    if (!tl) {
        return false;
    }
    Bounds r[32] = {};
    bool ok = false;
    if (s.len == 0) {
        *outX = 0;
        *outY = 0;
        *outH = fontSize;
        ok = true;
    } else if (off > 0 && (lineEndAffinity || off == s.len)) {
        // The trailing edge of everything before it, the way the caret is
        // placed.
        int n = TextLayoutRangeRects(tl, s, 0, off, r, 32);
        if (n > 0) {
            *outX = r[n - 1].x + r[n - 1].w;
            *outY = r[n - 1].y;
            *outH = r[n - 1].h;
            ok = true;
        }
    } else {
        // The leading edge of what follows. At a soft-wrap boundary this is
        // the start of the next visual row; the prefix branch above is the
        // trailing edge of the previous one.
        int n = TextLayoutRangeRects(tl, s, off, s.len, r, 32);
        if (n > 0) {
            *outX = r[0].x;
            *outY = r[0].y;
            *outH = r[0].h;
            ok = true;
        }
    }
    TextLayoutRelease(tl);
    return ok;
}

int TextIndexAt(PaintCtx* ctx, Str s, float fontSize, float maxW, bool wrap,
                float relX, float relY, bool mono, float lineHeight) {
    TextLayout* layout = TextMeasLayout(ctx, s, fontSize, maxW, wrap,
                                        mono ? (uint8_t)kFontMono : (uint8_t)0,
                                        lineHeight, nullptr);
    if (!layout) {
        return 0;
    }
    int off = TextLayoutHitPoint(layout, s, relX, relY);
    TextLayoutRelease(layout);
    return off;
}

// The marked range's underline: the same rects the selection quad is built
// from, one device pixel tall at the bottom of each. Rust hands the run an
// UnderlineStyle instead, which the shaper draws; the rects land in the same
// place and cost no new text machinery.
// The squiggle a wavy underline is: a run of half-period diagonals under the
// glyphs, drawn as one path so the joins are the stroke's own.
static void PaintWavyRun(PaintCtx* ctx, float x, float y, float w, Rgba color) {
    const float kPeriod = 4.f;
    const float kAmp = 1.5f;
    if (w <= 0) {
        return;
    }
    Path* p = PathNew(ctx, false);
    if (!p) {
        return;
    }
    PathMoveTo(p, x, y);
    bool up = true;
    const float kHalfPeriod = kPeriod * 0.5f;
    for (uint32_t i = 1;; ++i) {
        const float at = static_cast<float>(i) * kHalfPeriod;
        if (!(at < w)) {
            break;
        }
        PathLineTo(p, x + at, y + (up ? -kAmp : kAmp));
        up = !up;
    }
    PathStroke(ctx, p, 1.f, color);
    PathFree(p);
}

void PaintTextUnderline(PaintCtx* ctx, Str s, float fontSize, float maxW,
                        bool wrap, uint8_t weight, float lineH, float x,
                        float y, int u8a, int u8b, Rgba color, bool wavy) {
    if (!ctx || !ctx->rt || color.a == 0 || u8a >= u8b) {
        return;
    }
    TextLayout* layout =
        TextMeasLayout(ctx, s, fontSize, maxW, wrap, weight, lineH, nullptr);
    if (!layout) {
        return;
    }
    Bounds rects[32] = {};
    int n = TextLayoutRangeRects(layout, s, u8a, u8b, rects, 32);
    // Just under the glyphs rather than at the foot of the line box, which is
    // where a shaper puts an underline and where the leading would otherwise
    // drop it out of the field altogether.
    float baseline = TextLayoutBaseline(layout);
    for (int i = 0; i < n; i++) {
        float ux = x + rects[i].x;
        float uy = y + rects[i].y + baseline + 1.f;
        if (wavy) {
            PaintWavyRun(ctx, ux, uy + 1.f, rects[i].w, color);
        } else {
            CanvasFillRect(ctx, ux, uy, rects[i].w, 1.f, color);
        }
    }
    TextLayoutRelease(layout);
}

void PaintTextRange(PaintCtx* ctx, Str s, float fontSize, float maxW, bool wrap,
                    uint8_t weight, float lineH, float x, float y, int u8a,
                    int u8b, Rgba color) {
    if (!ctx || !ctx->rt || color.a == 0) {
        return;
    }
    if (u8a > u8b) {
        int t = u8a;
        u8a = u8b;
        u8b = t;
    }
    if (u8a == u8b) {
        return;
    }
    TextLayout* layout =
        TextMeasLayout(ctx, s, fontSize, maxW, wrap, weight, lineH, nullptr);
    if (!layout) {
        return;
    }
    // One rect per line the selection covers; 32 is more lines than any
    // selectable text block here has.
    Bounds rects[32] = {};
    int n = TextLayoutRangeRects(layout, s, u8a, u8b, rects, 32);
    for (int i = 0; i < n; i++) {
        CanvasFillRect(ctx, x + rects[i].x, y + rects[i].y, rects[i].w,
                       rects[i].h, color);
    }
    TextLayoutRelease(layout);
}

// ─── layout ───────────────────────────────────────────────────────────────
//
// The element tree is laid out by src/taffy — the C++ port of the taffy crate
// GPUI itself uses — rather than by an engine of its own. Each frame the El
// tree is translated into a taffy tree, taffy computes it, and the results
// are written back onto the El nodes.
//
// What taffy does not model, and this layer still does:
//
//   - `fixed`: out-of-flow in *window* coordinates. Those elements are
//     re-parented onto the root taffy node as absolutely positioned children,
//     so their insets resolve against the window rather than their El parent.
//   - `anchorBelow` / `anchorAbove` / `anchorCenterX` and the `relative(f)`
//     halves of `left` / `right`: positioning rules gpui-kit has and CSS
//     does not. They move an already-laid-out subtree afterwards, which is
//     what the old engine did too.
//   - `scrollX` / `scrollY`: taffy lays a scroll container's content out at
//     the origin and reports how big it is; the offset is applied to the
//     in-flow children as their absolute positions are accumulated.
//   - text, icon, image and progress sizing, which reach the port through a
//     taffy measure function the way Rust's `request_measured_layout` does.
//
// Two deliberate differences from what `Style::to_taffy` does in Rust, both
// of them recorded in port-status.md:
//
//   - `border` is not given to taffy, so a border still paints over the box
//     rather than reserving space inside it. That is what this tree's widgets
//     were built against; giving taffy the widths would move content by the
//     border width everywhere at the same time as the engine changed.
//   - `maxW` / `maxH` are plain floats whose default of 1e9 means "unset" and
//     maps to auto. `minW` / `minH` default to kAuto, so an element that
//     names no minimum gets CSS's content-based automatic minimum size and an
//     element that says `MinW(0)` gets the zero it asked for.

// ─── the layout cache ────────────────────────────────────────────────────
//
// The taffy tree is kept between frames and reconciled against the element
// tree rather than rebuilt from nothing, because taffy's own per-node cache
// is the thing worth keeping: a node whose style and content are the ones it
// had last frame answers `PerformChildLayout` out of that cache and its
// subtree is not walked at all. Rebuilding the tree threw the cache away
// every frame, which is why 66% of a story frame was inside
// `ComputeRootLayout` with nothing on the page having changed.
//
// The element tree has no identity to key on — an `El` is built afresh out of
// the frame arena every frame and `El::Click(id)` names only the handful of
// boxes that hit-test — so the key is position: the nth child of the nth
// child is the same node as last frame if the kinds still match. Where they
// do not, that subtree is dropped and built again, which is what a page
// switch does.
//
// What makes a node dirty, and nothing else does:
//
//   - its `taffy::Style` differs from the one the node carries (`operator==`
//     on Style is the derive Rust has and this port had left out), or
//   - it is a measured leaf whose measurement inputs differ — the text, the
//     font, the weight, the line height, the wrap, an image's natural size.
//     A hover that only recolours a box changes neither, so a hovered row
//     costs no layout at all.
//
// `__layout_reuse=off` (or `GPUI_LAYOUT_REUSE=off`) turns it back into the
// old behaviour by resetting the cache every frame, which is the first
// thing to try if a frame ever comes out laid out stale.

// What a node needs to know about its element, kept beside taffy rather than
// in it: `SetNodeContext` marks a node dirty, so handing taffy this frame's
// `El*` would undo the caching it is there to protect. The record is the
// node's context, set once when the node is made, and its fields are updated
// in place.
struct LayoutNode {
    El* el = nullptr;
    // What the measure function last read off the element. Zero for a node
    // that is not a measured leaf.
    uint64_t measKey = 0;
    uint8_t kind = 0;
};

struct LayoutCache {
    taffy::TaffyTree tree;
    bool ready = false;
    taffy::NodeId root = {};
    bool hasRoot = false;
    // Every record ever made, and the ones ready to be handed out again.
    Vec<LayoutNode*> pool;
    Vec<LayoutNode*> spare;
    // What the last reconcile did, for GPUI_FRAME_BENCH.
    LayoutCacheStats stats;
};

// The fixed elements found while building this frame's tree, which are
// re-parented onto the root.
static Vec<El*> gLayoutFixed;

// The tree a MeasureEl runs in. It is reset per call — a measure builds an
// element tree of its own that has nothing to do with the last one — so it
// keeps the node slots and the records and nothing else.
static LayoutCache gMeasureCache;

// Move a laid-out subtree without re-running layout. Positions are absolute,
// so shifting the origin shifts every descendant by the same delta; sizes are
// unaffected.
static void TranslateSubtree(El* e, float dx, float dy) {
    for (El* c = e->first; c; c = c->next) {
        // A fixed element was placed against the window, not against whatever
        // holds it, so sliding an ancestor into place must leave it where it
        // is — and its own subtree with it.
        if (c->style.fixed) {
            continue;
        }
        c->x += dx;
        c->y += dy;
        TranslateSubtree(c, dx, dy);
    }
}

// Move an element that has already been laid out to a new origin.
static void MoveEl(El* c, float cx, float cy) {
    float dx = cx - c->x;
    float dy = cy - c->y;
    if (dx == 0 && dy == 0) {
        return;
    }
    c->x = cx;
    c->y = cy;
    TranslateSubtree(c, dx, dy);
}

// gpui img(..): the box an image takes. Its own pixels are the natural size,
// at one DIP per pixel; a width or a height given by the document wins and the
// other side follows the aspect ratio, which is what html.rs reads out of the
// width / height attributes. Wider than the space it has, it shrinks to fit —
// max_w(relative(1.)) with object_fit(Contain), the pair node.rs gives a
// markdown image.
//
// An image with no size to measure is its alt text instead, measured here so
// the line it sits in is the right height for it.

// The picture's own size, whichever of the two kinds it is: a bitmap's pixels
// or a vector's viewBox. Zero when there is nothing to measure — a fetch
// still running, a missing asset, a format the platform does not read.
static Size ImageNaturalSize(PaintCtx* ctx, El* e) {
    RenderImage* img = ImageForSource(ctx ? ctx->pa : nullptr, e->imageSource);
    if (img) {
        return RenderImageSizePx(img);
    }
    int opsLen = 0;
    const uint8_t* ops =
        ImageVectorForSource(ctx ? ctx->pa : nullptr, e->imageSource, &opsLen);
    Size vb = {};
    if (ops && DrawOpsViewBox(ops, opsLen, &vb)) {
        return vb;
    }
    return {};
}

static Size LayoutImageSize(PaintCtx* ctx, El* e, float wSpec, float hSpec,
                            float availW, float font) {
    Size px = ImageNaturalSize(ctx, e);
    if (px.w <= 0 || px.h <= 0) {
        if (e->imageLoadState == ImageLoadState::Loading) {
            return {wSpec > 0 ? wSpec : 0, hSpec > 0 ? hSpec : 0};
        }
        Size text =
            MeasureText(ctx, e->text, font, availW > 0 ? availW : 0,
                        e->style.wrap, ElTextWeight(e), e->style.lineHeight);
        return {wSpec > 0 ? wSpec : text.w, hSpec > 0 ? hSpec : text.h};
    }
    float aspect = px.h / px.w;
    float w = wSpec > 0 ? wSpec : (hSpec > 0 ? hSpec / aspect : px.w);
    if (wSpec <= 0 && availW > 0 && w > availW) {
        w = availW;
    }
    float h = hSpec > 0 ? hSpec : w * aspect;
    if (wSpec > 0 && hSpec > 0) {
        h = hSpec;
    }
    return {w, h};
}

// ─── style translation ───────────────────────────────────────────────────

// A gpui length: kAuto means "as big as the content", kFill means "as big as
// the box holding it" (a full-width percentage), anything else is DIPs.
static taffy::Dimension ToDim(float v, float frac) {
    if (frac > 0) {
        return taffy::Dimension::Percent(frac);
    }
    if (v == kFill) {
        return taffy::Dimension::Percent(1.0f);
    }
    if (v == kAuto || v < 0) {
        return taffy::Dimension::Auto();
    }
    return taffy::Dimension::Length(v);
}

// A min-width / min-height: kAuto is the content-based automatic minimum,
// anything else is the length it says, zero included.
static taffy::Dimension ToMinDim(float v) {
    if (v == kAuto || v < 0) {
        return taffy::Dimension::Auto();
    }
    return taffy::Dimension::Length(v);
}

static taffy::LengthPercentageAuto ToInset(float v, float rel) {
    // The pixel and the `relative(f)` halves of one inset cannot both reach
    // taffy without a calc() node, so a mixed pair is finished off by
    // PlaceAnchored below and only the plain cases are handed over here.
    if (rel != 0) {
        return taffy::LengthPercentageAuto::Auto();
    }
    if (v == kAuto) {
        return taffy::LengthPercentageAuto::Auto();
    }
    return taffy::LengthPercentageAuto::Length(v);
}

static taffy::Overflow ToTaffyOverflow(Overflow o) {
    switch (o) {
        case Overflow::Hidden:
            return taffy::Overflow::Hidden;
        case Overflow::Scroll:
            return taffy::Overflow::Scroll;
        default:
            return taffy::Overflow::Visible;
    }
}

static taffy::OptAlignItems ToTaffyAlignItems(FlexAlign a) {
    using K = taffy::AlignItemsKeyword;
    switch (a) {
        case FlexAlign::Start:
            return taffy::OptAlignItems(taffy::AlignItems{K::Start});
        case FlexAlign::Center:
            return taffy::OptAlignItems(taffy::AlignItems{K::Center});
        case FlexAlign::End:
            return taffy::OptAlignItems(taffy::AlignItems{K::End});
        default:
            return taffy::OptAlignItems(taffy::AlignItems{K::Stretch});
    }
}

static taffy::FlexDirection ToTaffyFlexDir(FlexDir d) {
    switch (d) {
        case FlexDir::Col:
            return taffy::FlexDirection::Column;
        case FlexDir::RowReverse:
            return taffy::FlexDirection::RowReverse;
        case FlexDir::ColReverse:
            return taffy::FlexDirection::ColumnReverse;
        default:
            return taffy::FlexDirection::Row;
    }
}

static taffy::OptJustifyContent ToTaffyJustify(Justify j) {
    using K = taffy::AlignContentKeyword;
    switch (j) {
        case Justify::Center:
            return taffy::OptJustifyContent(taffy::AlignContent{K::Center});
        case Justify::End:
            return taffy::OptJustifyContent(taffy::AlignContent{K::End});
        case Justify::SpaceBetween:
            return taffy::OptJustifyContent(
                taffy::AlignContent{K::SpaceBetween});
        case Justify::SpaceAround:
            return taffy::OptJustifyContent(
                taffy::AlignContent{K::SpaceAround});
        default:
            return taffy::OptJustifyContent(taffy::AlignContent{K::Start});
    }
}

// Rust's `Style::to_taffy`, for the subset of CSS this tree's Style carries.
static taffy::Style ToTaffyStyle(const El* e) {
    const Style& s = e->style;
    taffy::Style t;
    t.display = s.display == Display::Flex ? taffy::Display::Flex
                                           : taffy::Display::Block;
    t.flexDirection = ToTaffyFlexDir(s.dir);
    t.flexWrap = s.flexWrap ? taffy::FlexWrap::Wrap : taffy::FlexWrap::NoWrap;
    t.alignItems = ToTaffyAlignItems(s.align);
    if (s.hasAlignSelf) {
        t.alignSelf = ToTaffyAlignItems(s.alignSelf);
    }
    t.justifyContent = ToTaffyJustify(s.justify);
    t.overflow = {ToTaffyOverflow(s.overflowX), ToTaffyOverflow(s.overflowY)};

    t.size = {ToDim(s.width, s.widthFrac), ToDim(s.height, 0)};
    if (s.aspect > 0) {
        t.aspectRatio = taffy::Some(s.aspect);
    }
    // An unset min is `auto`: a flex item may not shrink below its own
    // content, which is CSS's default and Rust's. An explicit zero is the
    // opposite instruction — `min_w_0()`, "this may shrink past its content"
    // — and it is what a pane holding something wider than the window says so
    // the window's width still wins.
    t.minSize = {ToMinDim(s.minW), ToMinDim(s.minH)};
    // Through ToDim rather than straight to Length: kFill in a max is
    // `max_w(relative(1.))` -- a hundred percent of what holds it, which is
    // how node.rs keeps a picture inside its column -- and a length of -2 is
    // a max of nothing at all, which collapses the box.
    t.maxSize = {s.maxWFrac > 0 ? taffy::Dimension::Percent(s.maxWFrac)
                                : (s.maxW < 1e9f ? ToDim(s.maxW, 0)
                                                 : taffy::Dimension::Auto()),
                 s.maxH < 1e9f ? ToDim(s.maxH, 0) : taffy::Dimension::Auto()};

    t.flexGrow = s.flexGrow;
    t.flexShrink = s.flexShrink;
    // An auto basis makes the main size the item's own size style, which is
    // what a plain `W()` means. `flex_1()` names zero instead, and taffy then
    // splits the whole line by the grow factors rather than only the slack.
    t.flexBasis =
        s.flexBasisFrac > 0
            ? taffy::Dimension::Percent(s.flexBasisFrac)
            : (s.flexBasis == kAuto ? taffy::Dimension::Auto()
                                    : taffy::Dimension::Length(s.flexBasis));

    t.padding = {taffy::LengthPercentage::Length(s.pad.left),
                 taffy::LengthPercentage::Length(s.pad.right),
                 taffy::LengthPercentage::Length(s.pad.top),
                 taffy::LengthPercentage::Length(s.pad.bottom)};
    t.margin = {taffy::LengthPercentageAuto::Length(s.margin.left),
                taffy::LengthPercentageAuto::Length(s.margin.right),
                taffy::LengthPercentageAuto::Length(s.margin.top),
                taffy::LengthPercentageAuto::Length(s.margin.bottom)};
    t.gap = {taffy::LengthPercentage::Length(s.gapX),
             taffy::LengthPercentage::Length(s.gapY)};
    // GPUI hands `Style::border_widths` straight to taffy, so a border takes
    // room the way CSS says it does: the box keeps its size and the content
    // inside it moves in by the width. This tree drew the border inside the
    // box and reserved nothing, which made every bordered box its border
    // narrower and shorter than upstream's.
    //
    // `border` is the all-round width and `borderT`/`B`/`L`/`R` are the
    // per-edge ones, and the two are independent — paint draws the rounded
    // rect for the first and a line per edge for the second, so an element
    // carrying both draws both. An edge therefore reserves the larger of the
    // two rather than their sum.
    auto edge = [](float all, float one) {
        return taffy::LengthPercentage::Length(one > all ? one : all);
    };
    t.border = {edge(s.border, s.borderL), edge(s.border, s.borderR),
                edge(s.border, s.borderT), edge(s.border, s.borderB)};

    if (s.absolute || s.fixed) {
        t.position = taffy::Position::Absolute;
        t.inset = {ToInset(s.absLeft, s.absLeftRel),
                   ToInset(s.absRight, s.absRightRel),
                   ToInset(s.absTop, s.absTopRel),
                   ToInset(s.absBottom, s.absBottomRel)};
    }
    return t;
}

// ─── measurement ─────────────────────────────────────────────────────────

// What a leaf's measure function is handed, beyond the node itself.
struct LayoutMeasureCtx {
    PaintCtx* ctx;
};

// The width a text run may use: a known width wins, then a definite
// constraint if the run wraps or truncates, else unconstrained.
static float TextMeasureWidth(const El* e, taffy::SizeFOpt known,
                              taffy::SizeAvail avail) {
    if (taffy::IsSome(known.w)) {
        return known.w;
    }
    if (!(e->style.wrap || e->style.truncate)) {
        return 0.0f;
    }
    if (avail.width.IsDefinite()) {
        return avail.width.value > 0 ? avail.width.value : 0.0f;
    }
    // A min-content constraint asks for the narrowest the run can be, which
    // for wrapped text is its longest word — what a one-pixel wrap box gives.
    // A run that only truncates cannot break, so its narrowest is its whole
    // width, which is what an unconstrained measure answers.
    if (avail.width.kind == taffy::AvailableSpace::Kind::MinContent &&
        e->style.wrap) {
        return 1.0f;
    }
    return 0.0f;
}

// Not snapped. gpui ceils what a leaf measures to the device pixel grid
// (`snap_measured_size_to_device_pixels`), and it is tempting to read the raw
// float handed over here as the sub-pixel drift between this tree and that
// one. It is not: gpui runs the *whole* layout in device pixels — available
// space multiplied by the scale factor on the way in, every authored length
// rounded in `to_taffy`, bounds divided back out on the way to paint — and
// ceiling the measure alone imports a quarter of that model. Measured against
// the engine this port replaced, over all 65 story pages, doing so moved
// every one of them further away and tripled the pixels that differ, because
// it quantises text boxes while the padding, gaps and borders around them
// stay where they were. Whole model or none of it; the numbers are in the
// paragraph above.
static taffy::SizeF LayoutMeasure(taffy::SizeFOpt known, taffy::SizeAvail avail,
                                  taffy::NodeId node, void* nodeContext,
                                  const taffy::Style* nodeStyle,
                                  void* userData) {
    (void)node;
    (void)nodeStyle;
    // The context is the cache's record for this node, not the element:
    // telling taffy about a new El* would mark the node dirty, which is the
    // one thing the cache is there to avoid.
    LayoutNode* rec = (LayoutNode*)nodeContext;
    El* e = rec ? rec->el : nullptr;
    LayoutMeasureCtx* mc = (LayoutMeasureCtx*)userData;
    if (!e) {
        return taffy::SizeF::Zero();
    }
    PaintCtx* ctx = mc ? mc->ctx : nullptr;
    float font = e->laidFont;

    switch (e->kind) {
        case ElKind::Text: {
            float measW = TextMeasureWidth(e, known, avail);
            Size text = {};
            int slot = -1;
            for (int i = 0; i < e->measCount; i++) {
                if (e->measKeyW[i] == measW) {
                    slot = i;
                    break;
                }
            }
            if (slot >= 0) {
                text = e->measSize[slot];
            } else {
                text = MeasureText(ctx, e->text, font, measW, e->style.wrap,
                                   ElTextWeight(e), e->style.lineHeight);
                // Four widths, oldest out. A leaf asked about more than four
                // in one pass simply measures again.
                int at = e->measCount < 4 ? e->measCount++ : e->measNext;
                e->measNext = (uint8_t)((e->measNext + 1) & 3);
                e->measKeyW[at] = measW;
                e->measSize[at] = text;
            }
            return {taffy::UnwrapOr(known.w, text.w),
                    taffy::UnwrapOr(known.h, text.h)};
        }
        case ElKind::Icon: {
            // svg().size(window.text_style().font_size): an icon that names
            // no size follows the text size inherited from its container.
            float size = font > 0 ? font : 16.0f;
            return {taffy::UnwrapOr(known.w, size),
                    taffy::UnwrapOr(known.h, size)};
        }
        case ElKind::Progress:
            return {taffy::UnwrapOr(known.w, 48.0f),
                    taffy::UnwrapOr(known.h, 8.0f)};
        case ElKind::Image: {
            float availW = avail.width.IsDefinite() ? avail.width.value : 0.0f;
            Size sz =
                LayoutImageSize(ctx, e, taffy::UnwrapOr(known.w, 0.0f),
                                taffy::UnwrapOr(known.h, 0.0f), availW, font);
            return {sz.w, sz.h};
        }
        default:
            return taffy::SizeF::Zero();
    }
}

// A childless Div is still a box with a size; only these kinds have content
// of their own to measure.
static bool ElIsMeasured(const El* e) {
    if (e->kind == ElKind::Image && e->imageReplacement) {
        return false;
    }
    switch (e->kind) {
        case ElKind::Text:
        case ElKind::Icon:
        case ElKind::Progress:
        case ElKind::Image:
            return true;
        default:
            return false;
    }
}

// ─── building the taffy tree ─────────────────────────────────────────────

// gpui's `img()` does not measure: `Img::request_layout` reads the decoded
// bitmap's size, stamps an aspect ratio on the style, and fills in whichever
// of width and height was auto — from the other one when that one is an
// absolute length, from the bitmap otherwise. Doing the same here is not a
// tidiness: an image left as a measured leaf is asked for its size with the
// cross axis already known, answers with the width that height implies, and
// that width becomes the flex base size the next pass stretches again. A run
// of markdown with a picture in it grew a little on every pass.
//
// An image that cannot be decoded — a remote URL, a format the platform does
// not read — has no size to resolve and stays a measured leaf, so its alt
// text is measured as the text it is. That is our stand-in for gpui's
// `fallback` element.
static void ResolveImageStyle(PaintCtx* ctx, El* e) {
    Size px = ImageNaturalSize(ctx, e);
    if (px.w <= 0 || px.h <= 0) {
        return;
    }
    Style& s = e->style;
    s.aspect = px.w / px.h;
    // An absolute length is the only thing the other axis can be derived
    // from; kFill and the fractional widths are a share of a box that is not
    // settled yet.
    auto absolute = [](float v) { return v != kAuto && v != kFill && v >= 0; };
    if (s.width == kAuto) {
        s.width = absolute(s.height) ? px.w * s.height / px.h : px.w;
    }
    if (s.height == kAuto) {
        s.height = (absolute(s.width) && s.widthFrac == 0)
                       ? px.h * s.width / px.w
                       : px.h;
    }
}

static void ResolveImageReplacement(PaintCtx* ctx, El* e) {
    double loadingSeconds = 0;
    e->imageLoadState = ImageSourceState(ctx ? ctx->pa : nullptr,
                                         e->imageSource, &loadingSeconds);
    if (e->imageLoadState == ImageLoadState::Loading) {
        // GPUI waits 200 ms before showing the loading element, avoiding a
        // flash for images already close to ready.
        if (e->imageLoading && loadingSeconds >= 0.2) {
            e->imageReplacement = e->imageLoading;
        } else if (e->imageLoading && ctx) {
            ctx->wantsAnimFrame = true;
        }
    } else if (e->imageLoadState == ImageLoadState::Failed) {
        e->imageReplacement = e->imageFallback;
    }
    if (e->imageReplacement) {
        e->imageReplacement->next = nullptr;
        e->first = e->imageReplacement;
        e->last = e->imageReplacement;
    }
}

// The style refinement, the inspector's live edit, the inherited font and the
// inherited color, resolved once per element before anything is measured.
// The old engine did this on the way down its own recursion.
static void PrepareEl(PaintCtx* ctx, El* e, float inheritFont, Rgba inheritFg) {
    // The element's own refinement first — a semantic state, which is meant
    // to win over whatever the caller chained on — and then the inspector's
    // live edit, which wins over everything.
    ElStyleStates* states = e->StyleStates();
    if (states && states->refineSet) {
        StyleApplyFields(&e->style, states->refine, states->refineSet);
        states->refineSet = 0;
    }
    // Then the two that hold only while something is true of the pointer.
    // Both need a click id of their own, for the same reason HoverBg does:
    // without one the element would match a hoverId of 0, which is what
    // "nothing is hovered" is spelled as.
    if (states && states->hoverSet && e->clickId && ctx &&
        e->clickId == ctx->hoverId) {
        StyleApplyFields(&e->style, states->hover, states->hoverSet);
    }
    if (states && states->activeSet && e->clickId && ctx &&
        e->clickId == ctx->activeId) {
        StyleApplyFields(&e->style, states->active, states->activeSet);
    }
    if (states && states->focusSet && e->style.focusId && ctx &&
        e->style.focusId == ctx->focusId) {
        StyleApplyFields(&e->style, states->focus, states->focusSet);
    }
    if (states && states->dragOverSet && e->clickId && ctx &&
        e->clickId == ctx->dragOverId &&
        base::StrEq(ArenaStrGet(e->arena, states->dragOverKind),
                    ctx->dragKind)) {
        StyleApplyFields(&e->style, states->dragOver, states->dragOverSet);
    }
    StyleOverrideApply(e);
    if (e->kind == ElKind::Image) {
        ResolveImageReplacement(ctx, e);
    }

    // An explicit size is in DIPs at the default font size and scales with
    // it; an inherited one has been scaled already, by the root or by
    // whichever ancestor set it.
    float font =
        e->style.fontSize > 0
            ? e->style.fontSize *
                  (RuntimeStyleNow(ctx ? ctx->app : nullptr).fontSize / 16.f)
            : inheritFont;
    Rgba fg = e->style.hasColor ? e->style.color : inheritFg;
    // Like HoverBg, this needs a click id of its own: without one the element
    // would match hoverId 0, which means nothing is hovered.
    if (e->style.hasHoverFg && e->clickId && ctx &&
        e->clickId == ctx->hoverId) {
        fg = e->style.hoverFg;
    }
    // `text_color` cascades in GPUI, and this is where. A Text or an Icon
    // resolves its colour when it paints, and what it used to resolve to
    // when it named none was `theme.foreground` — so a container that set a
    // colour coloured its icons and nothing else. An Alert's title is the
    // plainest case: `h_flex().text_color(variant.fg(cx))` around it, and the
    // port drew it black inside a blue box.
    e->style.color = fg;
    e->style.hasColor = true;
    // font_family inherits. Pushing the flag one level down here cascades it
    // through the subtree, since every child is prepared the same way.
    if (e->style.fontMono) {
        for (El* c = e->first; c; c = c->next) {
            c->style.fontMono = true;
        }
    }
    // font_weight inherits the same way — GPUI's `font_medium()` on a row is
    // what makes the string inside it medium, and an accordion's title is one
    // of those. A child that names a weight of its own keeps it; a child that
    // names none takes the one above.
    if (e->style.fontWeight || e->style.fontBold || e->style.fontSemibold ||
        e->style.fontMedium) {
        for (El* c = e->first; c; c = c->next) {
            if (c->style.fontWeight || c->style.fontBold ||
                c->style.fontSemibold || c->style.fontMedium) {
                continue;
            }
            c->style.fontWeight = e->style.fontWeight;
            c->style.fontBold = e->style.fontBold;
            c->style.fontSemibold = e->style.fontSemibold;
            c->style.fontMedium = e->style.fontMedium;
        }
    }
    e->laidFont = font;
    if (e->kind == ElKind::Icon && e->style.width == kAuto &&
        e->style.height == kAuto) {
        // Icon::render asks the inherited text style for its pixel size and
        // writes that into both SVG dimensions before layout. Leaving this
        // solely to intrinsic measurement would let align-stretch supply a
        // known cross size and turn the icon into a tall flex item.
        e->style.width = font > 0 ? font : 16.f;
        e->style.height = font > 0 ? font : 16.f;
    }
    if (e->kind == ElKind::Image && !e->imageReplacement) {
        ResolveImageStyle(ctx, e);
    }

    for (El* c = e->first; c; c = c->next) {
        PrepareEl(ctx, c, font, fg);
    }
}

// ─── reconciling the tree ────────────────────────────────────────────────

static uint64_t FnvMix(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}

// __layout_reuse=off rebuilds the tree every frame, which is what layout
// did before the cache. It is the first thing to try if a frame ever comes
// out laid out stale, and what the two are measured against. Argv wins;
// GPUI_LAYOUT_REUSE is the same switch if nothing on the command line set it.
static int gLayoutReuse = -1;

bool LayoutReuseTakeArg(Str arg) {
    const Str k = StrL("__layout_reuse=");
    if (!base::StrStartsWith(arg, k)) {
        return false;
    }
    Str value(arg.s + k.len, arg.len - k.len);
    if (base::StrEqI(value, "off") || base::StrEq(value, StrL("0"))) {
        gLayoutReuse = 0;
        logf("layout: reuse off (__layout_reuse=off), rebuilding every frame");
    } else if (base::StrEqI(value, "on") || base::StrEq(value, StrL("1"))) {
        gLayoutReuse = 1;
    }
    return true;
}

bool LayoutReuseOn() {
    if (gLayoutReuse >= 0) {
        return gLayoutReuse != 0;
    }
    gLayoutReuse = 1;
    const char* env = getenv("GPUI_LAYOUT_REUSE");
    if (env && env[0] &&
        (base::StrEqI(Str(env), "0") || base::StrEqI(Str(env), "off"))) {
        gLayoutReuse = 0;
        logf("layout: reuse off (GPUI_LAYOUT_REUSE), rebuilding every frame");
    }
    return gLayoutReuse != 0;
}

static LayoutNode* LayoutNodeTake(LayoutCache* lc, El* e) {
    LayoutNode* n = nullptr;
    if (lc->spare.len > 0) {
        n = lc->spare[lc->spare.len - 1];
        lc->spare.len--;
    } else {
        n = new LayoutNode();
        VecAppend(lc->pool, n);
    }
    n->el = e;
    n->measKey = 0;
    n->kind = (uint8_t)e->kind;
    return n;
}

static void LayoutNodeGiveBack(LayoutCache* lc, LayoutNode* n) {
    if (!n) {
        return;
    }
    n->el = nullptr;
    VecAppend(lc->spare, n);
}

// Everything a measured leaf's size depends on that its style does not say.
// A text run is its bytes, the font it was prepared with, its weight, its
// line height and whether it wraps; an image is its own pixels, which arrive
// after the load and change the answer when they do.
static uint64_t LayoutMeasureKey(PaintCtx* ctx, El* e) {
    if (!ElIsMeasured(e)) {
        return 0;
    }
    // FNV-1a over the bytes that decide the size.
    uint64_t h = 1469598103934665603ull;
    uint8_t kind = (uint8_t)e->kind;
    h = FnvMix(h, &kind, 1);
    if (e->kind == ElKind::Progress) {
        // Progress answers with a constant, so nothing about it moves it.
        return h;
    }
    if (e->text.len > 0 && e->text.s) {
        h = FnvMix(h, e->text.s, (size_t)e->text.len);
    }
    h = FnvMix(h, &e->laidFont, sizeof(e->laidFont));
    uint8_t weight = ElTextWeight(e);
    h = FnvMix(h, &weight, 1);
    h = FnvMix(h, &e->style.lineHeight, sizeof(e->style.lineHeight));
    uint8_t flags =
        (uint8_t)((e->style.wrap ? 1 : 0) | (e->style.truncate ? 2 : 0));
    h = FnvMix(h, &flags, 1);
    if (e->kind == ElKind::Image) {
        Size px = ImageNaturalSize(ctx, e);
        h = FnvMix(h, &px.w, sizeof(px.w));
        h = FnvMix(h, &px.h, sizeof(px.h));
    }
    return h;
}

static void LayoutDropSubtree(LayoutCache* lc, taffy::NodeId id) {
    // Backwards, so detaching a child never shifts the ones still to go.
    for (int i = lc->tree.ChildCount(id) - 1; i >= 0; i--) {
        LayoutDropSubtree(lc, lc->tree.ChildAtIndex(id, i));
    }
    LayoutNodeGiveBack(lc, (LayoutNode*)lc->tree.GetNodeContext(id));
    lc->tree.Remove(id);
    lc->stats.dropped++;
}

static void LayoutDropUnreachable(taffy::NodeId id, void* user) {
    LayoutDropSubtree((LayoutCache*)user, id);
}

// The root's own style is stretched to fill the space it was given — Rust's
// `stretch_auto_size_to_fill` — and that has to happen before the comparison
// below, or the root would differ from itself every frame.
static void StretchRootStyle(taffy::Style* ts, float availW, float availH) {
    if (ts->size.width.IsAuto() && availW > 0) {
        ts->size.width = taffy::Dimension::Length(availW);
    }
    if (ts->size.height.IsAuto() && availH > 0) {
        ts->size.height = taffy::Dimension::Length(availH);
    }
}

struct LayoutSyncCtx {
    LayoutCache* lc;
    PaintCtx* ctx;
    float availW;
    float availH;
};

static taffy::NodeId LayoutSync(LayoutSyncCtx* sc, El* e, taffy::NodeId prev,
                                bool havePrev, bool isRoot);

// Drop extra children of every reused node before LayoutSync builds
// anything. Sync is left-to-right: scrolling a virtualized list back to
// the top turns the spacer at index 0 into a row (LayoutBuild its guts)
// before the row at the other end becomes a spacer and is dropped, so
// InsertNode sees an empty free list and `new NodeData` on every scroll-up.
static void LayoutShrink(LayoutSyncCtx* sc, El* e, taffy::NodeId prev,
                         bool isRoot) {
    LayoutCache* lc = sc->lc;
    LayoutNode* rec = (LayoutNode*)lc->tree.GetNodeContext(prev);
    if (!rec || rec->kind != (uint8_t)e->kind) {
        return;
    }
    int want = 0;
    for (El* c = e->first; c; c = c->next) {
        if (!c->style.fixed) {
            want++;
        }
    }
    if (!isRoot) {
        bool dropped = false;
        for (int j = lc->tree.ChildCount(prev) - 1; j >= want; j--) {
            LayoutDropSubtree(lc, lc->tree.ChildAtIndex(prev, j));
            dropped = true;
        }
        // taffy's Remove does not dirty the parent, so a node that only
        // lost a child would keep the layout it had when the child was
        // still there. LayoutSync used to MarkDirty after its own
        // drop-first; shrink now does that drop, so it has to say so.
        if (dropped) {
            lc->tree.MarkDirty(prev);
        }
    }
    int i = 0;
    for (El* c = e->first; c; c = c->next) {
        if (c->style.fixed) {
            continue;
        }
        if (i < lc->tree.ChildCount(prev)) {
            taffy::NodeId old = lc->tree.ChildAtIndex(prev, i);
            LayoutNode* oldRec = (LayoutNode*)lc->tree.GetNodeContext(old);
            if (oldRec && oldRec->kind == (uint8_t)c->kind) {
                LayoutShrink(sc, c, old, false);
            }
        }
        i++;
    }
}

// A node and its subtree, made from nothing.
static taffy::NodeId LayoutBuild(LayoutSyncCtx* sc, El* e, bool isRoot) {
    LayoutCache* lc = sc->lc;
    taffy::Style ts = ToTaffyStyle(e);
    if (isRoot) {
        StretchRootStyle(&ts, sc->availW, sc->availH);
    }
    LayoutNode* rec = LayoutNodeTake(lc, e);
    taffy::NodeId id = lc->tree.NewLeafWithContext(ts, rec);
    rec->measKey = LayoutMeasureKey(sc->ctx, e);
    e->layoutNode = id.raw;
    lc->stats.made++;
    lc->stats.nodes++;

    for (El* c = e->first; c; c = c->next) {
        if (c->style.fixed) {
            // Placed against the window, so it hangs off the root instead.
            VecAppend(gLayoutFixed, c);
            continue;
        }
        lc->tree.AddChild(id, LayoutBuild(sc, c, false));
    }
    return id;
}

// `prev` is the node this element had last frame, if the tree still has one
// in that position. Answers the node the element has now, which is `prev`
// whenever it could be kept.
static taffy::NodeId LayoutSync(LayoutSyncCtx* sc, El* e, taffy::NodeId prev,
                                bool havePrev, bool isRoot) {
    LayoutCache* lc = sc->lc;
    LayoutNode* rec =
        havePrev ? (LayoutNode*)lc->tree.GetNodeContext(prev) : nullptr;
    // A node made for one kind of element cannot stand in for another: a
    // measured leaf and a box are not the same node even when their styles
    // agree.
    if (!rec || rec->kind != (uint8_t)e->kind) {
        // The old node stays where it is. Its caller holds an index into the
        // parent's child list and is about to put this new node there, and
        // taffy's Remove — Rust's too — takes a node *out* of that list,
        // which shifts every sibling after it: the replace would then land on
        // the wrong child, or past the end of a list that has just become
        // shorter, in which case the new subtree is never attached at all and
        // nothing lays it out. Dropping is the caller's, once the swap is
        // done. It is also Rust's order — `replace_child_at_index` and then
        // `remove`.
        return LayoutBuild(sc, e, isRoot);
    }

    lc->stats.nodes++;
    rec->el = e;
    e->layoutNode = prev.raw;

    taffy::Style ts = ToTaffyStyle(e);
    if (isRoot) {
        StretchRootStyle(&ts, sc->availW, sc->availH);
    }
    if (!(ts == lc->tree.GetStyle(prev))) {
        lc->tree.SetStyle(prev, ts);
        lc->stats.restyled++;
    }

    uint64_t key = LayoutMeasureKey(sc->ctx, e);
    if (key != rec->measKey) {
        rec->measKey = key;
        lc->tree.MarkDirty(prev);
        lc->stats.remeasured++;
    }

    // The children, in order. A `fixed` child is not one of this node's — it
    // hangs off the root — so it is collected and skipped here, and the index
    // only counts the ones that stay.
    //
    // Drop whatever this node has past the new child count *before* making
    // anything, so InsertNode recycles. LayoutShrink already did this for
    // every reused node; keeping it here covers a kind-mismatch replace
    // that shrink skipped. Skip that on the root: its taffy children
    // include last frame's `fixed` overlays, which the pass below reuses;
    // treating them as extras rebuilt every popover on every frame.
    int want = 0;
    for (El* c = e->first; c; c = c->next) {
        if (!c->style.fixed) {
            want++;
        }
    }
    bool dropped = false;
    if (!isRoot) {
        for (int j = lc->tree.ChildCount(prev) - 1; j >= want; j--) {
            LayoutDropSubtree(lc, lc->tree.ChildAtIndex(prev, j));
            dropped = true;
        }
    }
    int i = 0;
    for (El* c = e->first; c; c = c->next) {
        if (c->style.fixed) {
            VecAppend(gLayoutFixed, c);
            continue;
        }
        int had = lc->tree.ChildCount(prev);
        if (i < had) {
            taffy::NodeId old = lc->tree.ChildAtIndex(prev, i);
            LayoutNode* oldRec = (LayoutNode*)lc->tree.GetNodeContext(old);
            if (oldRec && oldRec->kind == (uint8_t)c->kind) {
                taffy::NodeId now = LayoutSync(sc, c, old, true, false);
                if (now != old) {
                    lc->tree.ReplaceChildAtIndex(prev, i, now);
                    LayoutDropSubtree(lc, old);
                }
            } else {
                // Kind mismatch: recycle the old subtree before building
                // so InsertNode does not `new NodeData` while the old
                // slots are still live.
                lc->tree.RemoveChildAtIndex(prev, i);
                LayoutDropSubtree(lc, old);
                taffy::NodeId now = LayoutBuild(sc, c, false);
                lc->tree.InsertChildAtIndex(prev, i, now);
            }
        } else {
            lc->tree.AddChild(prev, LayoutBuild(sc, c, false));
        }
        i++;
    }
    // taffy's Remove does not dirty the parent — Rust's does not either,
    // because Rust's callers reach for `set_children`, which does. A node
    // that lost a child and changed in no other way would otherwise keep the
    // layout it had when the child was still there.
    if (dropped) {
        lc->tree.MarkDirty(prev);
    }
    return prev;
}

// Everything in the cache goes back, so the next pass builds from nothing.
static void LayoutCacheReset(LayoutCache* lc) {
    lc->tree.Clear();
    lc->spare.len = 0;
    for (int i = 0; i < lc->pool.len; i++) {
        lc->pool[i]->el = nullptr;
        VecAppend(lc->spare, lc->pool[i]);
    }
    lc->hasRoot = false;
    lc->root = taffy::NodeId{};
}

// ─── writing the result back ─────────────────────────────────────────────

static void WriteBackEl(LayoutCache* lc, PaintCtx* ctx, El* e, float originX,
                        float originY);

static void WriteBackChildren(LayoutCache* lc, PaintCtx* ctx, El* e) {
    // A scrolled box slides its in-flow content; an out-of-flow child is
    // pinned to the box and does not move with it, which is what the old
    // engine's PlaceOutOfFlow did.
    float inFlowX = e->x - e->scrollX;
    float inFlowY = e->y - e->scrollY;
    for (El* c = e->first; c; c = c->next) {
        if (c->style.fixed) {
            continue;
        }
        if (c->style.absolute) {
            WriteBackEl(lc, ctx, c, e->x, e->y);
        } else {
            WriteBackEl(lc, ctx, c, inFlowX, inFlowY);
        }
    }
}

static void WriteBackEl(LayoutCache* lc, PaintCtx* ctx, El* e, float originX,
                        float originY) {
    const taffy::Layout& l = lc->tree.GetLayout(taffy::NodeId{e->layoutNode});
    e->x = originX + l.location.x;
    e->y = originY + l.location.y;
    e->w = l.size.w;
    e->h = l.size.h;
    e->contentW = l.contentSize.w;
    e->contentH = l.contentSize.h;

    // The shaped run paint wants, taken from the text cache at the size
    // layout settled on. Releasing our reference is safe because a cached run
    // belongs to the cache until TextMeasEndFrame, well after paint.
    if (e->kind == ElKind::Text) {
        bool constrain = e->style.wrap || e->style.truncate;
        float measW = constrain ? e->w : 0.0f;
        e->laidMaxW = measW;
        bool cached = false;
        TextLayout* tl = TextMeasLayout(ctx, e->text, e->laidFont, measW,
                                        e->style.wrap, (uint8_t)ElTextWeight(e),
                                        e->style.lineHeight, nullptr, &cached);
        e->laidLayout = cached ? tl : nullptr;
        if (tl) {
            TextLayoutRelease(tl);
        }
    }

    WriteBackChildren(lc, ctx, e);
}

static float PositionMax(float a, float b) {
    return a > b ? a : b;
}

static Bounds AnchoredClamp(Bounds b, Size view, float margin) {
    float rightLimit = PositionMax(view.w - margin, margin);
    float bottomLimit = PositionMax(view.h - margin, margin);
    if (b.Right() > rightLimit) {
        b.x -= b.Right() - rightLimit;
    }
    if (b.x < margin) {
        b.x = margin;
    }
    if (b.Bottom() > bottomLimit) {
        b.y -= b.Bottom() - bottomLimit;
    }
    if (b.y < margin) {
        b.y = margin;
    }
    return b;
}

AnchoredPosition AnchoredSideResolve(Bounds trigger, Size popup, Size view,
                                     float margin, int preferred, int align,
                                     float offset) {
    float rightLimit = PositionMax(view.w - margin, margin);
    float bottomLimit = PositionMax(view.h - margin, margin);
    float availLeft = PositionMax(trigger.x - margin, 0.f);
    float availRight = PositionMax(rightLimit - trigger.Right(), 0.f);
    float availAbove = PositionMax(trigger.y - margin, 0.f);
    float availBelow = PositionMax(bottomLimit - trigger.Bottom(), 0.f);

    // Placement ordinals: Top 0, Bottom 1, Left 2, Right 3. The arms are in
    // positioner.rs order: preferred, opposite, then the roomier side.
    int placed = 0;
    if (preferred == 3) {
        placed =
            popup.w <= availRight
                ? 3
                : (popup.w <= availLeft ? 2
                                        : (availRight >= availLeft ? 3 : 2));
    } else if (preferred == 2) {
        placed =
            popup.w <= availLeft
                ? 2
                : (popup.w <= availRight ? 3
                                         : (availLeft >= availRight ? 2 : 3));
    } else if (preferred == 1) {
        placed =
            popup.h <= availBelow
                ? 1
                : (popup.h <= availAbove ? 0
                                         : (availBelow >= availAbove ? 1 : 0));
    } else {
        placed =
            popup.h <= availAbove
                ? 0
                : (popup.h <= availBelow ? 1
                                         : (availBelow >= availAbove ? 1 : 0));
    }

    float alignedX = trigger.x;
    float alignedY = trigger.y;
    if (align == 1) { // Center
        alignedX = trigger.CenterX() - popup.w * 0.5f;
        alignedY = trigger.CenterY() - popup.h * 0.5f;
    } else if (align == 2) { // End
        alignedX = trigger.Right() - popup.w;
        alignedY = trigger.Bottom() - popup.h;
    }

    Bounds bounds = {0, 0, popup.w, popup.h};
    if (placed == 0) {
        bounds.x = alignedX;
        bounds.y = trigger.y - popup.h - offset;
    } else if (placed == 1) {
        bounds.x = alignedX;
        bounds.y = trigger.Bottom() + offset;
    } else if (placed == 2) {
        bounds.x = trigger.x - popup.w - offset;
        bounds.y = alignedY;
    } else {
        bounds.x = trigger.Right() + offset;
        bounds.y = alignedY;
    }

    AnchoredPosition out = {};
    out.bounds = AnchoredClamp(bounds, view, margin);
    out.placement = (int8_t)placed;
    return out;
}

AnchoredPosition AnchoredCornerResolve(Anchor anchor, Point at, Size popup,
                                       Size view, float margin) {
    Bounds bounds = BoundsAt(at, popup);
    if (anchor == Anchor::TopCenter || anchor == Anchor::BottomCenter) {
        bounds.x -= popup.w * 0.5f;
    } else if (anchor == Anchor::TopRight || anchor == Anchor::BottomRight ||
               anchor == Anchor::RightCenter) {
        bounds.x -= popup.w;
    }
    if (anchor == Anchor::BottomLeft || anchor == Anchor::BottomCenter ||
        anchor == Anchor::BottomRight) {
        bounds.y -= popup.h;
    } else if (anchor == Anchor::LeftCenter || anchor == Anchor::RightCenter) {
        bounds.y -= popup.h * 0.5f;
    }
    AnchoredPosition out = {};
    out.bounds = AnchoredClamp(bounds, view, margin);
    return out;
}

// The positioning rules gpui-kit has and CSS does not: an overlay
// anchored under or over its trigger, one centred on it, and the
// `relative(f)` half of a left/right inset. Each moves a subtree that taffy
// has already sized and placed.
static void PlaceAnchored(El* e, float viewW, float viewH, float clientInset) {
    for (El* c = e->first; c; c = c->next) {
        PlaceAnchored(c, viewW, viewH, clientInset);
        const Style& s = c->style;
        bool anchored = s.anchorBelow || s.anchorAbove || s.anchorCenterX ||
                        s.anchorCorner || s.explicitPositioner;
        if (!anchored && (c->style.fixed || !c->style.absolute)) {
            continue;
        }
        if (!anchored && s.absLeftRel == 0 && s.absRightRel == 0) {
            continue;
        }
        float innerW = e->w - e->style.pad.HorizontalAxisSum();
        if (innerW < 0) {
            innerW = 0;
        }
        float ax = c->x;
        float ay = c->y;
        // A `fixed` popup was laid out against the window — which is the
        // point, since that is the width its content had to shape against,
        // the way Rust's Positioner sits in the deferred layer rather than
        // inside the trigger. Where it goes is still the trigger's business,
        // so the inset it named is read off the trigger's box here.
        if (s.fixed && anchored) {
            ax = e->x + (s.absLeft == kAuto ? 0.f : s.absLeft);
            if (s.absRight != kAuto) {
                ax = e->x + e->w - s.absRight - c->w;
            }
            ay = e->y;
        }
        if (s.absLeftRel != 0) {
            float absL = (s.absLeft == kAuto ? 0.f : s.absLeft);
            ax = e->x + e->style.pad.left + absL + innerW * s.absLeftRel;
        }
        if (s.absRightRel != 0) {
            float absR = (s.absRight == kAuto ? 0.f : s.absRight);
            ax = e->x + e->w - e->style.pad.right - absR -
                 innerW * s.absRightRel - c->w;
        }
        bool below = s.anchorBelow;
        if ((s.anchorBelow || s.anchorAbove) && s.anchorFlip && viewH > 0) {
            // `Positioner::side`, written where the anchor is applied: the
            // requested side if the popup fits there, the opposite side if it
            // fits and the first does not, and the roomier of the two when
            // neither does. The clamp below is what runs afterwards either
            // way, which is the order positioner.rs places and then clamps in.
            float roomBelow =
                viewH - (e->y + e->h) - s.anchorGap - s.anchorMargin;
            float roomAbove = e->y - s.anchorGap - s.anchorMargin;
            float want = below ? roomBelow : roomAbove;
            float other = below ? roomAbove : roomBelow;
            if (want < c->h) {
                below = other >= c->h ? !below : roomBelow >= roomAbove;
            }
        }
        if (s.anchorBelow || s.anchorAbove) {
            ay = below ? (e->y + e->h + s.anchorGap)
                       : (e->y - c->h - s.anchorGap);
        }
        if (s.anchorCenterX) {
            ax = e->x + (e->w - c->w) * 0.5f;
        }
        if (s.anchorCorner) {
            // popup.rs::resolved_corner followed by
            // Bounds::from_anchor_and_size. Bottom anchors deliberately move
            // the point one trigger height above its origin; this unusual
            // arithmetic is pinned by upstream's own test.
            Point at = {e->x, e->y};
            switch (s.anchor) {
                case Anchor::TopCenter:
                case Anchor::BottomCenter:
                    at.x = e->x + e->w * 0.5f;
                    break;
                case Anchor::TopRight:
                case Anchor::BottomRight:
                    at.x = e->x + e->w;
                    break;
                default:
                    break;
            }
            if (s.anchor == Anchor::BottomLeft ||
                s.anchor == Anchor::BottomCenter ||
                s.anchor == Anchor::BottomRight) {
                at.y = e->y - e->h;
            }
            ax = at.x;
            ay = at.y;
            if (s.anchor == Anchor::TopCenter ||
                s.anchor == Anchor::BottomCenter) {
                ax -= c->w * 0.5f;
            } else if (s.anchor == Anchor::TopRight ||
                       s.anchor == Anchor::BottomRight ||
                       s.anchor == Anchor::RightCenter) {
                ax -= c->w;
            }
            if (s.anchor == Anchor::BottomLeft ||
                s.anchor == Anchor::BottomCenter ||
                s.anchor == Anchor::BottomRight) {
                ay -= c->h;
            } else if (s.anchor == Anchor::LeftCenter ||
                       s.anchor == Anchor::RightCenter) {
                ay -= c->h * 0.5f;
            }
            ay += s.anchorGap;
        }
        if (s.explicitPositioner) {
            float margin = s.anchorMargin + clientInset;
            AnchoredPosition resolved =
                s.positionerCorner
                    ? AnchoredCornerResolve(s.anchor, s.positionerPoint,
                                            {c->w, c->h}, {viewW, viewH},
                                            margin)
                    : AnchoredSideResolve(s.positionerTrigger, {c->w, c->h},
                                          {viewW, viewH}, margin,
                                          s.positionerPlacement,
                                          s.positionerAlign, s.anchorGap);
            ax = resolved.bounds.x;
            ay = resolved.bounds.y;
        }
        // positioner.rs `clamp`: whatever the corner worked out, the popup is
        // then pulled back inside the viewport with WINDOW_MARGIN to spare.
        // It never flips — that is the side strategy's job — so a popup with
        // nowhere to go simply sits against the edge.
        if (anchored && viewW > 0 && viewH > 0) {
            float m = s.anchorMargin + clientInset;
            float rightLimit = viewW - m > m ? viewW - m : m;
            float bottomLimit = viewH - m > m ? viewH - m : m;
            if (ax + c->w > rightLimit) {
                ax = rightLimit - c->w;
            }
            if (ax < m) {
                ax = m;
            }
            if (ay + c->h > bottomLimit) {
                ay = bottomLimit - c->h;
            }
            if (ay < m) {
                ay = m;
            }
        }
        if (s.explicitPositioner) {
            // positioner.rs rounds the offset it installs for prepaint. The
            // C++ element itself is moved instead, so rounding its resolved
            // window origin is the equivalent for this absolute group.
            ax = roundf(ax);
            ay = roundf(ay);
        }
        MoveEl(c, ax, ay);
    }
}

// The one layout pass, with the space it is run in left to the caller: the
// frame runs it against the viewport, a measure runs it against MinContent.
static void LayoutElIn(LayoutCache* lc, PaintCtx* ctx, El* e, float x, float y,
                       float availW, float availH, bool minContent,
                       float inheritFont, Rgba inheritFg) {
    if (!e || !lc) {
        return;
    }
    if (!lc->ready) {
        lc->tree.Init(256);
        // GPUI calls `taffy.disable_rounding()`; everything above paint here
        // is DIPs, and the backends snap to the pixel grid themselves.
        lc->tree.DisableRounding();
        lc->ready = true;
    }
    if (!LayoutReuseOn()) {
        LayoutCacheReset(lc);
    }
    gLayoutFixed.len = 0;
    lc->stats = LayoutCacheStats{};
    lc->tree.allocs = 0;

    PrepareEl(ctx, e, inheritFont, inheritFg);

    LayoutSyncCtx sc = {lc, ctx, availW, availH};
    // Recycle a root of a different kind before building, so InsertNode
    // reuses its slots. A child in a list cannot be dropped first — that
    // shifts the indices the caller is walking.
    if (lc->hasRoot) {
        LayoutNode* rec = (LayoutNode*)lc->tree.GetNodeContext(lc->root);
        if (!rec || rec->kind != (uint8_t)e->kind) {
            LayoutDropSubtree(lc, lc->root);
            lc->hasRoot = false;
            lc->root = taffy::NodeId{};
        }
    }
    if (lc->hasRoot) {
        LayoutShrink(&sc, e, lc->root, true);
    }
    // Rust's `stretch_auto_size_to_fill` is applied to the root's style
    // inside the sync, so that the style the node carries is the one the
    // next frame compares against.
    taffy::NodeId root = LayoutSync(&sc, e, lc->root, lc->hasRoot, true);
    if (lc->hasRoot && root != lc->root) {
        LayoutDropSubtree(lc, lc->root);
    }
    lc->root = root;
    lc->hasRoot = true;

    // A `fixed` element resolves its insets against the window, so it hangs
    // off the root rather than off whatever built it. They come after the
    // root's own children, in the order the walk found them, which is what
    // makes the position they are matched by stable.
    int own = 0;
    for (El* c = e->first; c; c = c->next) {
        if (!c->style.fixed) {
            own++;
        }
    }
    int wantFixed = own + gLayoutFixed.len;
    bool droppedFixed = false;
    for (int j = lc->tree.ChildCount(root) - 1; j >= wantFixed; j--) {
        LayoutDropSubtree(lc, lc->tree.ChildAtIndex(root, j));
        droppedFixed = true;
    }
    for (int i = 0; i < gLayoutFixed.len; i++) {
        El* f = gLayoutFixed[i];
        int at = own + i;
        bool had = at < lc->tree.ChildCount(root);
        if (!had) {
            lc->tree.AddChild(root, LayoutBuild(&sc, f, false));
            continue;
        }
        taffy::NodeId old = lc->tree.ChildAtIndex(root, at);
        LayoutNode* oldRec = (LayoutNode*)lc->tree.GetNodeContext(old);
        if (oldRec && oldRec->kind == (uint8_t)f->kind) {
            taffy::NodeId now = LayoutSync(&sc, f, old, true, false);
            if (now != old) {
                lc->tree.ReplaceChildAtIndex(root, at, now);
                LayoutDropSubtree(lc, old);
            }
        } else {
            lc->tree.RemoveChildAtIndex(root, at);
            LayoutDropSubtree(lc, old);
            lc->tree.InsertChildAtIndex(root, at, LayoutBuild(&sc, f, false));
        }
    }
    if (droppedFixed) {
        lc->tree.MarkDirty(root);
    }

    lc->tree.EachUnreachable(root, LayoutDropUnreachable, lc);

    taffy::SizeAvail space;
    if (minContent) {
        space.width = taffy::AvailableSpace::MinContent();
        space.height = taffy::AvailableSpace::MinContent();
    } else {
        space.width = availW > 0 ? taffy::AvailableSpace::Definite(availW)
                                 : taffy::AvailableSpace::MaxContent();
        space.height = availH > 0 ? taffy::AvailableSpace::Definite(availH)
                                  : taffy::AvailableSpace::MaxContent();
    }

    LayoutMeasureCtx mc = {ctx};
    lc->tree.ComputeLayoutWithMeasure(root, space, LayoutMeasure, &mc);

    WriteBackEl(lc, ctx, e, x, y);
    // The fixed elements are laid out as children of the root, so their boxes
    // come out in window coordinates already.
    for (int i = 0; i < gLayoutFixed.len; i++) {
        WriteBackEl(lc, ctx, gLayoutFixed[i], 0, 0);
    }
    PlaceAnchored(e, ctx ? ctx->viewW : 0.f, ctx ? ctx->viewH : 0.f,
                  ctx ? ctx->clientInset : 0.f);
    lc->stats.allocs = lc->tree.allocs;
}

void LayoutEl(PaintCtx* ctx, El* e, float x, float y, float availW,
              float availH, float inheritFont, Rgba inheritFg,
              LayoutCache* lc) {
    if (!lc) {
        LayoutCacheReset(&gMeasureCache);
        lc = &gMeasureCache;
    }
    LayoutElIn(lc, ctx, e, x, y, availW, availH, false, inheritFont, inheritFg);
}

Size MeasureEl(PaintCtx* ctx, El* e, float inheritFont, Rgba inheritFg) {
    if (!e) {
        return Size{0, 0};
    }
    // A measure builds an element tree of its own that has nothing to do with
    // the last one, so its cache keeps the node slots and the records and
    // starts over on the tree.
    LayoutCacheReset(&gMeasureCache);
    LayoutElIn(&gMeasureCache, ctx, e, 0, 0, 0, 0, true, inheritFont,
               inheritFg);
    return Size{e->w, e->h};
}

LayoutCache* LayoutCacheNew() {
    return new LayoutCache();
}

void LayoutCacheFree(LayoutCache* lc) {
    if (!lc) {
        return;
    }
    for (int i = 0; i < lc->pool.len; i++) {
        delete lc->pool[i];
    }
    VecReset(lc->pool);
    VecReset(lc->spare);
    lc->tree.Free();
    delete lc;
}

LayoutCacheStats LayoutCacheLastStats(const LayoutCache* lc) {
    return lc ? lc->stats : LayoutCacheStats{};
}

int LayoutCacheNodeCount(const LayoutCache* lc) {
    return lc ? lc->tree.TotalNodeCount() : 0;
}

int LayoutCacheSlotCount(const LayoutCache* lc) {
    return lc ? lc->tree.SlotCount() : 0;
}

// The scratch cache is a static, so the app's teardown is what gives its
// records and its node slots back — the way ImageCacheClear and the rest of
// AppFree's list do.
void LayoutScratchFree() {
    for (int i = 0; i < gMeasureCache.pool.len; i++) {
        delete gMeasureCache.pool[i];
    }
    VecReset(gMeasureCache.pool);
    VecReset(gMeasureCache.spare);
    gMeasureCache.tree.Free();
    gMeasureCache.ready = false;
    gMeasureCache.hasRoot = false;
    gMeasureCache.root = taffy::NodeId{};
}

// ─── paint ────────────────────────────────────────────────────────────────

// No corner larger than half the shorter side, which is how Rust clamps a
// radius and what CornersPath already did for the per-corner path. A backend
// that takes one radius hands it to an API that clamps each axis on its own —
// D2D's rounded rect does — so a 4-tall rail asked for `radius_full` would
// come out a lens rather than a pill without this.
// The four corners of a box, as one path: a quarter turn at each corner that
// asked for one and a plain corner where it did not. Built here rather than in
// the two backends because the path API is already portable and a rounded box
// is nothing but four arcs — D2D's own rounded rectangle takes one radius, and
// so does cairo's and Core Graphics'.
static void CornersPath(Path* p, float x, float y, float w, float h,
                        const Corners& c) {
    // No corner larger than half the box, the way Rust clamps a radius.
    float lim = (w < h ? w : h) * 0.5f;
    float tl = c.tl < lim ? c.tl : lim;
    float tr = c.tr < lim ? c.tr : lim;
    float br = c.br < lim ? c.br : lim;
    float bl = c.bl < lim ? c.bl : lim;
    float r = x + w;
    float b = y + h;
    PathMoveTo(p, x + tl, y);
    PathLineTo(p, r - tr, y);
    if (tr > 0) {
        PathArcTo(p, r - tr, y + tr, tr, -kPi * 0.5f, 0.f, true);
    }
    PathLineTo(p, r, b - br);
    if (br > 0) {
        PathArcTo(p, r - br, b - br, br, 0.f, kPi * 0.5f, true);
    }
    PathLineTo(p, x + bl, b);
    if (bl > 0) {
        PathArcTo(p, x + bl, b - bl, bl, kPi * 0.5f, kPi, true);
    }
    PathLineTo(p, x, y + tl);
    if (tl > 0) {
        PathArcTo(p, x + tl, y + tl, tl, kPi, kPi * 1.5f, true);
    }
    PathClose(p);
}

// The same two calls as FillRound / DrawRoundStroke, for a box whose corners
// differ. `Style::hasCorners` is what picks between them.
static void FillCorners(PaintCtx* ctx, float x, float y, float w, float h,
                        const Corners& c, Rgba col) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if (c.IsUniform()) {
        CanvasFillRound(ctx, x, y, w, h, ClampRadius(c.tl, w, h), col);
        return;
    }
    Path* p = PathNew(ctx, true);
    CornersPath(p, x, y, w, h, c);
    PathFill(ctx, p, col);
    PathFree(p);
}

// The same two, for a fill that may be a gradient. A gradient is painted as
// a path rather than a rectangle because the path API already carries one on
// all three backends — D2D's linear-gradient brush, cairo's linear pattern,
// Core Graphics' CGGradient — and a rounded box is nothing but four arcs. A
// solid Background takes the rectangle route it always did.
static void FillBackground(PaintCtx* ctx, float x, float y, float w, float h,
                           float r, const Corners* c, const Background& bg) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if (!bg.gradient) {
        if (c) {
            FillCorners(ctx, x, y, w, h, *c, bg.color);
        } else {
            FillRound(ctx, x, y, w, h, r, bg.color);
        }
        return;
    }
    Point p0 = {}, p1 = {};
    BackgroundLine(bg, Bounds{x, y, w, h}, &p0, &p1);
    // Two stops at the same place have no line to run along; the first stop
    // is what the whole box would be anyway.
    if (fabsf(p1.x - p0.x) < 1e-4f && fabsf(p1.y - p0.y) < 1e-4f) {
        if (c) {
            FillCorners(ctx, x, y, w, h, *c, bg.from.color);
        } else {
            FillRound(ctx, x, y, w, h, r, bg.from.color);
        }
        return;
    }
    Corners uniform = {r, r, r, r};
    Path* p = PathNew(ctx, true);
    CornersPath(p, x, y, w, h, c ? *c : uniform);
    PathFillGradient(ctx, p, p0.x, p0.y, p1.x, p1.y, bg.from.color,
                     bg.to.color);
    PathFree(p);
}

static void StrokeCorners(PaintCtx* ctx, float x, float y, float w, float h,
                          const Corners& c, float stroke, Rgba col) {
    if (w <= 0 || h <= 0) {
        return;
    }
    if (c.IsUniform()) {
        CanvasStrokeRound(ctx, x, y, w, h, ClampRadius(c.tl, w, h), stroke,
                          col);
        return;
    }
    Path* p = PathNew(ctx, true);
    CornersPath(p, x, y, w, h, c);
    PathStroke(ctx, p, stroke, col);
    PathFree(p);
}

// styled.rs FOCUS_RING_WIDTH and FOCUS_RING_OPACITY.
static const float kFocusRingWidth = 3.f;
static const float kFocusRingOpacity = 0.5f;

// GPUI's renderer blurs an alpha mask for a box shadow. The portable paint
// seam has no filter primitive, so approximate the same Gaussian falloff by
// painting nested rounded masks from the blur's outside edge inward. Each
// mask adds only the delta to the target opacity at that distance; unlike a
// handful of arbitrary outlines this retains the declared blur radius,
// spread, offset, colour and multiple-shadow ordering on every backend.
static void PaintBoxShadow(PaintCtx* ctx, const El* e,
                           const BoxShadow& shadow) {
    if (shadow.inset || shadow.color.a == 0) {
        // No component currently requests an inset shadow. Retain the field
        // in the value contract, but do not turn it into an outward shadow.
        return;
    }
    float x = e->x + shadow.x - shadow.spread;
    float y = e->y + shadow.y - shadow.spread;
    float w = e->w + shadow.spread * 2.f;
    float h = e->h + shadow.spread * 2.f;
    if (w <= 0 || h <= 0) {
        return;
    }
    float radius = e->style.radius + shadow.spread;
    if (radius < 0) {
        radius = 0;
    }
    float blur = shadow.blur > 0 ? shadow.blur : 0;
    if (blur <= 0.01f) {
        FillRound(ctx, x, y, w, h, radius, shadow.color);
        return;
    }
    int steps = (int)ceilf(blur);
    if (steps < 2) {
        steps = 2;
    }
    if (steps > 32) {
        steps = 32;
    }
    float previous = 0;
    for (int i = steps; i >= 0; i--) {
        float distance = blur * (float)i / (float)steps;
        float unit = distance / blur;
        // Half of an opaque half-plane contributes at its edge. The
        // exp(-2x^2) tail is visually close to the source Gaussian while
        // remaining expressible through the existing rounded-fill seam.
        float target = 0.5f * expf(-2.f * unit * unit);
        float delta = target - previous;
        previous = target;
        if (delta <= 0.0001f) {
            continue;
        }
        Rgba color = RgbaOpacity(shadow.color, delta);
        FillRound(ctx, x - distance, y - distance, w + distance * 2.f,
                  h + distance * 2.f, radius + distance, color);
    }
}

// Layout lands on fractions of a pixel, which spreads a hairline over two
// rows however it is inset. A border line is snapped to the nearest device
// pixel center so it covers exactly one.
static float EdgeLine(PaintCtx* ctx, float v) {
    float scale = ctx->dpi > 0 ? (float)ctx->dpi / 96.f : 1.f;
    float px = v * scale;
    return (floorf(px) + 0.5f) / scale;
}

// The ends of a border line, snapped to the pixel boundary: a dash pattern
// starts at the path's start, so a fractional one smears every dash.
static float EdgeEnd(PaintCtx* ctx, float v) {
    float scale = ctx->dpi > 0 ? (float)ctx->dpi / 96.f : 1.f;
    return floorf(v * scale + 0.5f) / scale;
}

static void DrawLine(PaintCtx* ctx, float x1, float y1, float x2, float y2,
                     float stroke, Rgba c) {
    CanvasLine(ctx, x1, y1, x2, y2, stroke, c);
}

void DrawTextAt(PaintCtx* ctx, Str s, float x, float y, float w, float h,
                float fontSize, Rgba c, bool truncate, bool wrap,
                float measMaxW, int weight, float lineH) {
    if (!s.s || s.len <= 0 || !ctx->pa) {
        return;
    }
    (void)w;
    (void)h;
    // A wrapping run is shaped to the width it wraps at; a truncating one is
    // shaped to the width it is cut at, which is what gives the backend
    // something to put the ellipsis against. Everything else is unconstrained.
    float keyW = wrap ? (measMaxW >= 0 ? measMaxW : (w > 0 ? w : 0))
                      : (truncate && w > 0 ? w : 0);
    TextLayout* layout = TextMeasLayout(ctx, s, fontSize, keyW, wrap,
                                        (uint8_t)weight, lineH, nullptr);
    if (!layout) {
        return;
    }
    TextLayoutDraw(ctx, layout, x, y, c, truncate, truncate ? keyW : 0.f);
    TextLayoutRelease(layout);
}

void DrawTextBaseline(PaintCtx* ctx, Str s, float x, float baselineY,
                      float fontSize, Rgba color, int weight) {
    if (!s.s || s.len <= 0 || !ctx || !ctx->pa) {
        return;
    }
    TextLayout* layout =
        TextMeasLayout(ctx, s, fontSize, 0, false, (uint8_t)weight, 0, nullptr);
    if (!layout) {
        return;
    }
    TextLayoutDraw(ctx, layout, x, baselineY - TextLayoutBaseline(layout),
                   color, false);
    TextLayoutRelease(layout);
}

// The value domain a chart's y axis is scaled to: what the caller named, or
// the extent of the data — which is what a ScaleLinear over it comes to.
static void ChartDomain(const ChartSeries& c, float* outMin, float* outMax) {
    if (c.domainMin != 0 || c.domainMax != 0) {
        *outMin = c.domainMin;
        *outMax = c.domainMax;
        return;
    }
    float lo = 0;
    float hi = 0;
    bool seen = false;
    for (int i = 0; i < c.n; i++) {
        const float* series[4] = {c.ys, c.opens, c.highs, c.lows};
        for (int k = 0; k < 4 + c.nMore; k++) {
            const float* ys = k < 4 ? series[k] : c.more[k - 4].ys;
            if (!ys) {
                continue;
            }
            float v = ys[i];
            if (!seen || v < lo) {
                lo = v;
            }
            if (!seen || v > hi) {
                hi = v;
            }
            seen = true;
        }
    }
    if (!seen || hi <= lo) {
        *outMin = 0;
        *outMax = hi > 0 ? hi : 1;
        return;
    }
    // A bar, an area and a radar are read against a baseline, so their domain
    // starts at zero unless the data goes below it. A line or a candle is
    // read against itself, so it keeps the extent of its own values with a
    // little air either side.
    if (c.kind == ChartKind::Line || c.kind == ChartKind::Candlestick) {
        float pad = (hi - lo) * 0.1f;
        *outMin = lo - pad;
        *outMax = hi + pad;
        return;
    }
    *outMin = lo > 0 ? 0 : lo;
    *outMax = hi;
}

// A value-axis tick label: whole numbers plain, anything else to one place,
// which is what a chart's own labels do upstream.
static Str ChartValueLabel(float v) {
    float rounded = (float)lroundf(v);
    float d = v - rounded;
    if ((d < 0 ? -d : d) < 0.05f) {
        return fmt("%d", (int)rounded);
    }
    return fmt("%.1f", (double)v);
}

// StrokeStyle, as the run of segments after the opening move_to. Natural is
// the Catmull-Rom the plot draws by default, turned into the cubic Beziers a
// path can carry; StepAfter holds each value until the next point's x, and
// leaves off the last riser the way Rust's windows(2) loop does.
template <typename FX, typename FY>
static void ChartRun(Path* p, const ChartSeries& c, FX Xat, FY Yat,
                     const float* ys, int n) {
    if (n < 2) {
        return;
    }
    if (c.strokeStyle == ChartStroke::Linear) {
        for (int i = 1; i < n; i++) {
            PathLineTo(p, Xat(i), Yat(ys[i]));
        }
        return;
    }
    if (c.strokeStyle == ChartStroke::StepAfter) {
        for (int i = 0; i + 1 < n; i++) {
            PathLineTo(p, Xat(i + 1), Yat(ys[i]));
            if (i < n - 2) {
                PathLineTo(p, Xat(i + 1), Yat(ys[i + 1]));
            }
        }
        return;
    }
    for (int i = 0; i + 1 < n; i++) {
        int i0 = i == 0 ? 0 : i - 1;
        int i3 = i + 2 < n ? i + 2 : n - 1;
        float x0 = Xat(i0), y0 = Yat(ys[i0]);
        float x1 = Xat(i), y1 = Yat(ys[i]);
        float x2 = Xat(i + 1), y2 = Yat(ys[i + 1]);
        float x3 = Xat(i3), y3 = Yat(ys[i3]);
        PathCubicTo(p, x1 + (x2 - x0) / 6.f, y1 + (y2 - y0) / 6.f,
                    x2 - (x3 - x1) / 6.f, y2 - (y3 - y1) / 6.f, x2, y2);
    }
}

// A row chart writes its band names down one side and its values at the far
// end of each bar, so the value axis gives up a gutter at each end. Rust
// measures the widest of each; a fixed pair is close enough at these sizes.
const float kBarRowBandGap = 52.f;
const float kBarRowValueGap = 36.f;

// One bar: which edge it grows from, what it is filled with, and the value
// written at its growing end.
static void DrawBar(PaintCtx* ctx, const ChartSeries& c, int i, float bx,
                    float bw, float x, float y, float w, float plotH, float lo,
                    float hi, const RuntimeStyle& th) {
    float t = hi > lo ? (c.ys[i] - lo) / (hi - lo) : 0.f;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    // Where zero sits along the value axis. Bars grow from here rather than
    // from the geometric baseline, so a negative value extends to the
    // opposite side; with no negative data zero is the domain minimum and the
    // two are the same place.
    float zero = hi > lo ? (0.f - lo) / (hi - lo) : 0.f;
    zero = zero < 0 ? 0 : (zero > 1 ? 1 : zero);
    // Where the bar starts: zero, unless a stack put it on top of the series
    // below.
    float t0 = zero;
    if (c.bases) {
        t0 = hi > lo ? (c.bases[i] - lo) / (hi - lo) : 0.f;
        t0 = t0 < 0 ? 0 : (t0 > 1 ? 1 : t0);
    }
    // A bar that runs the other way is drawn from the smaller end, so the two
    // swap rather than the length going negative.
    bool below = t < t0;
    if (below) {
        float swap = t;
        t = t0;
        t0 = swap;
    }
    bool horizontal =
        c.barAlign == BarAlign::Left || c.barAlign == BarAlign::Right;
    // The band runs across the plot for a column chart and down it for a row
    // one, so the two sides of the box swap with the alignment.
    float rx = 0, ry = 0, rw = 0, rh = 0;
    if (horizontal) {
        float bandY = y + (bx - x) * (plotH / (w > 0 ? w : 1));
        float bandH = bw * (plotH / (w > 0 ? w : 1));
        // The value axis runs between the two gutters, from the side the
        // bars are anchored to.
        float ax = x + (c.barAlign == BarAlign::Left ? kBarRowBandGap
                                                     : kBarRowValueGap);
        float aw = w - kBarRowBandGap - kBarRowValueGap;
        if (aw < 1) {
            aw = 1;
        }
        float len = aw * (t - t0);
        if (len < 1) {
            len = 1;
        }
        rx = c.barAlign == BarAlign::Left ? ax + aw * t0
                                          : ax + aw - aw * t0 - len;
        ry = bandY;
        rw = len;
        rh = bandH < 1 ? 1 : bandH;
    } else {
        float span = plotH - 10.f;
        float len = span * (t - t0);
        if (len < 1) {
            len = 1;
        }
        rx = bx;
        rw = bw;
        rh = len;
        ry = c.barAlign == BarAlign::Top ? y + 10.f + span * t0
                                         : y + plotH - span * t0 - len;
    }
    Rgba fill = c.barFills ? c.barFills[i] : c.stroke;
    if (c.barGradient) {
        // fill_gradient: across the chart's own range by default, so a tall
        // bar reaches further up the ramp than a short one; per-bar runs the
        // whole ramp inside every bar.
        Rgba from = c.barFillFrom;
        Rgba to = c.barFillTo;
        if (!c.barGradientPerBar && !c.barGradientDiagonal) {
            // The stop the bar actually reaches, as a mix of the two ends.
            Rgba hit = RgbaMix(from, to, t);
            to = hit;
        }
        // The ramp runs along the bar, which is top-to-bottom for a column
        // and left-to-right for a row.
        Path* box = PathNew(ctx, true);
        if (box) {
            PathMoveTo(box, rx, ry);
            PathLineTo(box, rx + rw, ry);
            PathLineTo(box, rx + rw, ry + rh);
            PathLineTo(box, rx, ry + rh);
            PathClose(box);
            if (c.barGradientDiagonal) {
                // Where the bar's two corners fall along the plot's
                // bottom-left to top-right diagonal, as a fraction of it.
                float pw = w > 1e-6f ? w : 1e-6f;
                float ph = plotH > 1e-6f ? plotH : 1e-6f;
                float denom = pw * pw + ph * ph;
                auto project = [&](float px, float py) {
                    return ((px - x) * pw + (ph - (py - y)) * ph) / denom;
                };
                // RgbaMix(a, b, t) is a*t + b*(1-t), so sampling a ramp that
                // runs `from` to `to` at p means mixing the far stop in at p.
                auto sample = [&](float pos) { return RgbaMix(to, from, pos); };
                PathFillGradient(ctx, box, rx, ry + rh, rx + rw, ry,
                                 sample(project(rx, ry + rh)),
                                 sample(project(rx + rw, ry)));
            } else if (horizontal) {
                PathFillGradient(ctx, box, rx, ry, rx + rw, ry,
                                 c.barAlign == BarAlign::Left ? from : to,
                                 c.barAlign == BarAlign::Left ? to : from);
            } else {
                PathFillGradientV(ctx, box, ry, ry + rh,
                                  c.barAlign == BarAlign::Top ? to : from,
                                  c.barAlign == BarAlign::Top ? from : to);
            }
            PathFree(box);
        }
    } else {
        FillRound(ctx, rx, ry, rw, rh, c.barRadius, fill);
    }
    if (!c.barLabels) {
        return;
    }
    // label(..): the value at the end the bar grew to, just inside it.
    Str text = fmt("%.0f", (double)c.ys[i]);
    if (horizontal) {
        float tx = c.barAlign == BarAlign::Left ? rx + rw + 4 : rx - 34;
        DrawTextAt(ctx, text, tx, ry + rh * 0.5f - 7.f, 30, 14, 10,
                   th.mutedForeground, c.barAlign != BarAlign::Left);
    } else {
        float ty = c.barAlign == BarAlign::Top ? ry + rh + 2 : ry - 14.f;
        DrawTextAt(ctx, text, rx + rw * 0.5f - 20.f, ty, 40, 14, 10,
                   th.mutedForeground, true);
    }
}

void TextLayoutDrawSpans(PaintCtx* ctx, TextLayout* layout, Str text, float x,
                         float y, Rgba base, const TextSpan* spans, int n) {
    if (PaintTextLayoutSpans(ctx, layout, text, x, y, base, spans, n)) return;
    Bounds rects[32] = {};
    int at = 0;
    for (int i = 0; i <= n; i++) {
        int lo = i < n ? spans[i].lo : text.len;
        int hi = i < n ? spans[i].hi : text.len;
        if (lo > at) {
            int count = TextLayoutRangeRects(layout, text, at, lo, rects, 32);
            for (int r = 0; r < count; r++) {
                CanvasPushClip(ctx, x + rects[r].x, y + rects[r].y, rects[r].w,
                               rects[r].h);
                TextLayoutDraw(ctx, layout, x, y, base, false);
                CanvasPopClip(ctx);
            }
        }
        if (i >= n || hi <= lo) {
            at = lo > at ? lo : at;
            continue;
        }
        int count = TextLayoutRangeRects(layout, text, lo, hi, rects, 32);
        for (int r = 0; r < count; r++) {
            CanvasPushClip(ctx, x + rects[r].x, y + rects[r].y, rects[r].w,
                           rects[r].h);
            TextLayoutDraw(ctx, layout, x, y, spans[i].color, false);
            CanvasPopClip(ctx);
        }
        at = hi;
    }
}

// One shaped layout: backgrounds first, foreground colors through the
// backend's single pass or range-clip fallback, then decorations.
static void PaintTextSpans(PaintCtx* ctx, El* e, float font, Rgba base) {
    float maxW = e->laidMaxW > 0 ? e->laidMaxW : e->w;
    TextLayout* layout =
        TextMeasLayout(ctx, e->text, font, maxW, e->style.wrap, ElTextWeight(e),
                       e->style.lineHeight, nullptr, nullptr);
    if (!layout) {
        DrawTextAt(ctx, e->text, e->x, e->y, e->w, e->h, font, base,
                   e->style.truncate, e->style.wrap, e->laidMaxW,
                   ElTextWeight(e), e->style.lineHeight);
        return;
    }
    Bounds rects[32] = {};
    // The washes go under every glyph, so they all go down first. This is
    // also the editor's decoration-background pass: an editor row hands its
    // composed spans over here, so a decoration carrying a background paints
    // under the indent guides, the selection and the text without a pass of
    // its own. Rust needs `LineLayout::paint_background` and a per-line
    // `has_background` flag because gpui's `ShapedLine::paint` draws only
    // glyphs, underlines and strikethroughs; the equivalent early-out here is
    // the per-span alpha test below, which costs nothing on a line with no
    // highlight.
    for (int i = 0; i < e->nSpans; i++) {
        const TextSpan& sp = e->spans[i];
        if (sp.bg.a == 0 || sp.hi <= sp.lo) {
            continue;
        }
        int n = TextLayoutRangeRects(layout, e->text, sp.lo, sp.hi, rects, 32);
        for (int r = 0; r < n; r++) {
            CanvasFillRect(ctx, e->x + rects[r].x, e->y + rects[r].y,
                           rects[r].w, rects[r].h, sp.bg);
        }
    }
    TextLayoutDrawSpans(ctx, layout, e->text, e->x, e->y, base, e->spans,
                        e->nSpans);
    // The rules last, so nothing paints over them.
    for (int i = 0; i < e->nSpans; i++) {
        const TextSpan& sp = e->spans[i];
        if (!sp.underline || sp.hi <= sp.lo) {
            continue;
        }
        PaintTextUnderline(ctx, e->text, font, maxW, e->style.wrap,
                           ElTextWeight(e), e->style.lineHeight, e->x, e->y,
                           sp.lo, sp.hi, sp.color, sp.wavy);
    }
    TextLayoutRelease(layout);
}

static void DrawChart(PaintCtx* ctx, El* e) {
    const RuntimeStyle& th = RuntimeStyleNow(ctx->app);
    float x = e->x;
    float y = e->y;
    float w = e->w;
    float h = e->h;
    const float axisGap = 18.f;
    float plotH = h - axisGap;
    if (plotH < 8 || w < 8) {
        return;
    }
    const ChartSeries* chart = e->Chart();
    if (!chart) {
        return;
    }
    const ChartSeries& c = *chart;
    // VALUE_AXIS_GAP: what the value-axis tick labels take out of the band
    // axis — left of vertical bars, and below horizontal ones, where they sit
    // past the end of the band axis and so need none of it. Like the axis gap
    // above it this is a fixed budget rather than a measured one, because the
    // same scale is rebuilt while hit-testing, where no text can be shaped.
    const float kValueAxisGap = 32.f;
    bool valueAxis = c.kind == ChartKind::Bar && c.valueAxis;
    bool valueAxisSide = valueAxis && c.barAlign != BarAlign::Left &&
                         c.barAlign != BarAlign::Right;
    if (valueAxisSide) {
        x += kValueAxisGap;
        w -= kValueAxisGap;
        if (w < 8) {
            return;
        }
    }
    int n = c.n;
    const float* ys = c.ys;

    // A radar has no axis along the bottom: its grid is the rings the values
    // are plotted on.
    if (c.kind == ChartKind::Radar) {
        if (!ys || n < 3) {
            return;
        }
        float lo = 0;
        float hi = 0;
        ChartDomain(c, &lo, &hi);
        float cx = x + w * 0.5f;
        float cy = y + h * 0.5f;
        // resolve_outer_radius: two fifths of the box's height, and the
        // caller's own radius where it gave one.
        float radius = h * 0.4f;
        if (c.radarRadius > 0) {
            radius = c.radarRadius;
        }
        // "The domain includes zero so non-negative data starts at the
        // center" — radar_chart.rs chains a zero into the scale's domain, so
        // the smallest value is a short spoke rather than a point on the hub.
        if (lo > 0) {
            lo = 0;
        }
        if (hi < 0) {
            hi = 0;
        }
        if (radius < 8) {
            return;
        }
        int levels = c.gridLevels > 0 ? c.gridLevels : 4;
        // The rings, and a spoke out to every axis. An overlaid series draws
        // on the rings the first one put down.
        for (int ring = 1; ring <= (c.overlay ? 0 : levels); ring++) {
            float rr = radius * (float)ring / (float)levels;
            Path* p = PathNew(ctx, false);
            if (!p) {
                break;
            }
            for (int i = 0; i <= n; i++) {
                float a = -1.5707963f + 6.2831853f * (float)(i % n) / (float)n;
                float px = cx + rr * cosf(a);
                float py = cy + rr * sinf(a);
                if (i == 0) {
                    PathMoveTo(p, px, py);
                } else {
                    PathLineTo(p, px, py);
                }
            }
            PathStroke(ctx, p, 1.f, th.border);
            PathFree(p);
        }
        for (int i = 0; i < (c.overlay ? 0 : n); i++) {
            float a = -1.5707963f + 6.2831853f * (float)i / (float)n;
            DrawLine(ctx, cx, cy, cx + radius * cosf(a), cy + radius * sinf(a),
                     1.f, th.border);
        }
        // The values themselves, as one closed shape.
        Path* shape = PathNew(ctx, true);
        if (shape) {
            for (int i = 0; i < n; i++) {
                float t = hi > lo ? (ys[i] - lo) / (hi - lo) : 0.f;
                if (t < 0) {
                    t = 0;
                }
                if (t > 1) {
                    t = 1;
                }
                float a = -1.5707963f + 6.2831853f * (float)i / (float)n;
                float px = cx + radius * t * cosf(a);
                float py = cy + radius * t * sinf(a);
                if (i == 0) {
                    PathMoveTo(shape, px, py);
                } else {
                    PathLineTo(shape, px, py);
                }
            }
            PathClose(shape);
            PathFill(ctx, shape, c.fillTop);
            PathStroke(ctx, shape, 2.f, c.stroke);
            PathFree(shape);
        }
        // dot(): a mark on every vertex of the ring.
        if (c.dot) {
            for (int i = 0; i < n; i++) {
                float t = hi > lo ? (ys[i] - lo) / (hi - lo) : 0.f;
                t = t < 0 ? 0 : (t > 1 ? 1 : t);
                float a = -1.5707963f + 6.2831853f * (float)i / (float)n;
                float px = cx + radius * t * cosf(a);
                float py = cy + radius * t * sinf(a);
                FillRound(ctx, px - 3.f, py - 3.f, 6.f, 6.f, 3.f, c.stroke);
            }
        }
        if (c.labels && !c.overlay) {
            // label_anchor: the label ring is DEFAULT_LABEL_GAP past the
            // outer one, and a label takes its alignment from the side it is
            // on — left of the anchor going left, right of it going right,
            // and centred at twelve and six o'clock.
            const float kLabelGap = 10.f;
            for (int i = 0; i < n; i++) {
                float a = -1.5707963f + 6.2831853f * (float)i / (float)n;
                float dx = cosf(a);
                float px = cx + (radius + kLabelGap) * dx;
                float py = cy + (radius + kLabelGap) * sinf(a);
                Str label = Str(c.labels[i]);
                float tw = MeasureText(ctx, label, 10, 0).w;
                float tx = px;
                if (dx < -1e-3f) {
                    tx = px - tw;
                } else if (dx <= 1e-3f) {
                    tx = px - tw * 0.5f;
                }
                DrawTextAt(ctx, label, tx, py - 5.f, tw, 14, 10,
                           th.mutedForeground, false);
            }
        }
        return;
    }

    // A row chart's value axis runs across rather than up, so its grid and
    // its band names turn with it.
    bool barRow = c.kind == ChartKind::Bar && (c.barAlign == BarAlign::Left ||
                                               c.barAlign == BarAlign::Right);

    // An overlay series draws over the grid and axis the first one drew.
    if (!c.overlay) {
        const float kGridDash[2] = {4.f, 2.f};
        if (barRow) {
            for (int i = 1; i <= 4; i++) {
                float gx = x + w * ((float)i / 4.f);
                CanvasLine(ctx, gx, y, gx, y + plotH, 1.f, th.border,
                           kGridDash);
            }
        } else {
            // `value_tick_count` even intervals over the whole range, which
            // is what the value-axis labels are placed on as well.
            int ticks = c.valueTickCount > 0 ? c.valueTickCount : 4;
            for (int i = 0; i < ticks; i++) {
                float gy = y + plotH * ((float)i / (float)ticks);
                CanvasLine(ctx, x, gy, x + w, gy, 1.f, th.border, kGridDash);
            }
            DrawLine(ctx, x, y + plotH, x + w, y + plotH, 1.f, th.border);
        }
        if (valueAxis) {
            // One label per grid interval, and one at the top, reading the
            // value the line stands for.
            float lo = 0;
            float hi = 0;
            ChartDomain(c, &lo, &hi);
            int ticks = c.valueTickCount > 0 ? c.valueTickCount : 4;
            for (int i = 0; i <= ticks; i++) {
                float f = (float)i / (float)ticks;
                float v = hi - (hi - lo) * f;
                Str label = ChartValueLabel(v);
                float tw = MeasureText(ctx, label, 10, 0, false, 0, 0).w;
                float ty = y + plotH * f - 6.f;
                if (valueAxisSide) {
                    DrawTextAt(ctx, label, x - 4.f - tw, ty, tw, 12, 10,
                               th.mutedForeground, false);
                } else {
                    // A row chart's value axis runs along the bottom, so its
                    // labels go under the plot rather than beside it.
                    float tx = x + w * f - tw * 0.5f;
                    DrawTextAt(ctx, label, tx, y + plotH + 2.f, tw, 12, 10,
                               th.mutedForeground, false);
                }
            }
        }
    }

    if (!ys || n <= 0) {
        return;
    }
    float lo = 0;
    float hi = 0;
    ChartDomain(c, &lo, &hi);

    auto Xat = [&](int i) -> float {
        if (n <= 1) {
            return x + w * 0.5f;
        }
        return x + (w * (float)i / (float)(n - 1));
    };
    auto Yat = [&](float v) -> float {
        float t = hi > lo ? (v - lo) / (hi - lo) : 0.f;
        if (t < 0) {
            t = 0;
        }
        if (t > 1) {
            t = 1;
        }
        return y + 10.f + (1.f - t) * (plotH - 10.f);
    };

    if (c.kind == ChartKind::Bar || c.kind == ChartKind::Candlestick) {
        // ScaleBand: every point takes a band of the width, with the padding
        // between them coming off each one.
        const float range[2] = {0.f, w};
        component::ScaleBand band = component::ScaleBand::New(n, range, 2);
        band.paddingInner = c.bandPadding;
        band.paddingOuter = c.bandPadding * 0.5f;
        float bw = band.BandWidth();
        if (bw < 1) {
            bw = 1;
        }
        for (int i = 0; i < n; i++) {
            float bx = 0;
            if (!band.Tick(i, &bx)) {
                continue;
            }
            bx += x;
            if (c.kind == ChartKind::Bar) {
                DrawBar(ctx, c, i, bx, bw, x, y, w, plotH, lo, hi, th);
                continue;
            }
            // A candle: the wick from low to high, and the body between open
            // and close, colored by which way it closed.
            float open = c.opens ? c.opens[i] : ys[i];
            float close = ys[i];
            float high = c.highs ? c.highs[i] : (open > close ? open : close);
            float low = c.lows ? c.lows[i] : (open < close ? open : close);
            Rgba color = close >= open ? c.up : c.down;
            float mid = bx + bw * 0.5f;
            DrawLine(ctx, mid, Yat(high), mid, Yat(low), 1.f, color);
            float top = Yat(open > close ? open : close);
            float bot = Yat(open > close ? close : open);
            float bh = bot - top;
            if (bh < 1) {
                bh = 1;
            }
            // body_width_ratio: the body is that much of the band, centred
            // on the wick.
            float ratio = c.bodyWidthRatio > 0 ? c.bodyWidthRatio : 0.8f;
            float bodyW = bw * ratio;
            if (bodyW < 1) {
                bodyW = 1;
            }
            FillRound(ctx, mid - bodyW * 0.5f, top, bodyW, bh, 1.f, color);
        }
    } else {
        // Area and line are the same run of points; only the area fills what
        // is under it. Every series is that run again over the same axes, in
        // the order the caller named them, so a later one draws over an
        // earlier one the way Rust's do.
        auto Band = [&](const float* vs, Rgba stroke, Rgba fillTop,
                        Rgba fillBot) {
            if (!vs) {
                return;
            }
            if (c.kind == ChartKind::Area) {
                Path* area = PathNew(ctx, true);
                if (area) {
                    PathMoveTo(area, Xat(0), y + plotH);
                    PathLineTo(area, Xat(0), Yat(vs[0]));
                    ChartRun(area, c, Xat, Yat, vs, n);
                    PathLineTo(area, Xat(n - 1), y + plotH);
                    PathClose(area);
                    PathFillGradientV(ctx, area, y, y + plotH, fillTop,
                                      fillBot);
                    PathFree(area);
                }
            }
            if (n == 1) {
                DrawLine(ctx, x, Yat(vs[0]), x + w, Yat(vs[0]), 2.f, stroke);
            } else {
                Path* line = PathNew(ctx, false);
                if (line) {
                    PathMoveTo(line, Xat(0), Yat(vs[0]));
                    ChartRun(line, c, Xat, Yat, vs, n);
                    PathStroke(ctx, line, 2.f, stroke);
                    PathFree(line);
                }
            }
            // dot(): a filled mark on every point.
            if (c.dot) {
                for (int i = 0; i < n; i++) {
                    FillRound(ctx, Xat(i) - 3.f, Yat(vs[i]) - 3.f, 6.f, 6.f,
                              3.f, stroke);
                }
            }
        };
        Band(ys, c.stroke, c.fillTop, c.fillBot);
        for (int k = 0; k < c.nMore; k++) {
            const ChartSeriesExtra& more = c.more[k];
            Band(more.ys, more.stroke, more.fillTop, more.fillBot);
        }
    }

    // The crosshair and the tooltip: a chart that asked for them shows what
    // the pointer is over. Rust hangs this off a hover state; the pointer's
    // position is already in the paint context here, so the chart reads it.
    if (c.tooltip && ctx->mouseX >= x && ctx->mouseX <= x + w &&
        ctx->mouseY >= y && ctx->mouseY <= y + plotH) {
        int index = 0;
        float lineX = ctx->mouseX;
        if (c.kind == ChartKind::Bar || c.kind == ChartKind::Candlestick) {
            const float range[2] = {0.f, w};
            component::ScaleBand band = component::ScaleBand::New(n, range, 2);
            band.paddingInner = c.bandPadding;
            band.paddingOuter = c.bandPadding * 0.5f;
            index = band.LeastIndex(ctx->mouseX - x);
            float bx = 0;
            if (band.Tick(index, &bx)) {
                lineX = x + bx + band.BandWidth() * 0.5f;
            }
        } else {
            float t = n > 1 ? (ctx->mouseX - x) / (w / (float)(n - 1)) : 0.f;
            index = (int)lroundf(t);
            if (index < 0) {
                index = 0;
            }
            if (index > n - 1) {
                index = n - 1;
            }
            lineX = Xat(index);
        }
        // CrossLine: a dashed hairline down the plot, and a dot on the value.
        // `border.mix(foreground, 0.8)` — the border walked a fifth of the way
        // to the ink, in HSL, which is what makes it read on both themes.
        const float kCrossDash[2] = {4.f, 3.f};
        CanvasLine(ctx, lineX, y, lineX, y + plotH, 1.f,
                   RgbaMixHsl(th.border, th.foreground, 0.8f), kCrossDash);
        float dotY = Yat(ys[index]);
        FillRound(ctx, lineX - 3.f, dotY - 3.f, 6.f, 6.f, 3.f, c.stroke);
        for (int k = 0; k < c.nMore; k++) {
            const ChartSeriesExtra& more = c.more[k];
            if (more.ys) {
                FillRound(ctx, lineX - 3.f, Yat(more.ys[index]) - 3.f, 6.f, 6.f,
                          3.f, more.stroke);
            }
        }

        // The box hugs the cursor and flips toward the middle past halfway,
        // which is what keeps it inside the plot. Every series names its own
        // line, the way Rust's tooltip lists them.
        Str title = c.labels ? Str(c.labels[index]) : fmt("%d", index);
        Str value = c.name.s ? fmt("%s  %.1f", c.name, (double)ys[index])
                             : fmt("%.1f", (double)ys[index]);
        Size titleSz = MeasureText(ctx, title, 11, 200);
        Size valueSz = MeasureText(ctx, value, 11, 200);
        float boxW = (titleSz.w > valueSz.w ? titleSz.w : valueSz.w) + 16.f;
        float boxH = titleSz.h + valueSz.h + 12.f;
        // The extra lines, measured before the box is drawn so it holds them.
        Str extra[4] = {};
        int nExtra = c.nMore < 4 ? c.nMore : 4;
        for (int k = 0; k < nExtra; k++) {
            const ChartSeriesExtra& more = c.more[k];
            extra[k] = more.name.s
                           ? fmt("%s  %.1f", more.name, (double)more.ys[index])
                           : fmt("%.1f", (double)more.ys[index]);
            Size sz = MeasureText(ctx, extra[k], 11, 200);
            if (sz.w + 16.f > boxW) {
                boxW = sz.w + 16.f;
            }
            boxH += sz.h;
        }
        Point at = component::PlotTooltipPlace(
            {ctx->mouseX - x, ctx->mouseY - y}, {w, plotH}, {boxW, boxH}, 8.f);
        FillRound(ctx, x + at.x, y + at.y, boxW, boxH, 6.f, th.background);
        DrawRoundStroke(ctx, x + at.x, y + at.y, boxW, boxH, 6.f, 1.f,
                        th.border);
        DrawTextAt(ctx, title, x + at.x + 8, y + at.y + 4, boxW, titleSz.h, 11,
                   th.foreground, false);
        DrawTextAt(ctx, value, x + at.x + 8, y + at.y + 6 + titleSz.h, boxW,
                   valueSz.h, 11, th.mutedForeground, false);
        float lineY = y + at.y + 6 + titleSz.h + valueSz.h;
        for (int k = 0; k < nExtra; k++) {
            DrawTextAt(ctx, extra[k], x + at.x + 8, lineY, boxW, valueSz.h, 11,
                       th.mutedForeground, false);
            lineY += valueSz.h;
        }
    }

    // x labels every tickMargin
    int step = c.tickMargin;
    if (step < 1) {
        step = 15;
    }
    if (c.overlay) {
        return;
    }
    // build_point_x_labels keeps the point whose one-based index divides by
    // the margin, so a margin of eight names the eighth point and not the
    // first. The name is centred on its tick, except at the two ends, where
    // it is pulled inside the plot rather than hung over the edge.
    for (int i = 0; i < n; i++) {
        if (step > 1 && ((i + 1) % step) != 0) {
            continue;
        }
        float lx = Xat(i) - 16;
        float ly = y + plotH + 2;
        float lw = 60;
        bool centered = false;
        if (c.kind == ChartKind::Bar || c.kind == ChartKind::Candlestick) {
            // A band's name sits under the band, not under a point — or in
            // the gutter beside it, when the bands run down the side.
            const float range[2] = {0.f, w};
            component::ScaleBand band = component::ScaleBand::New(n, range, 2);
            band.paddingInner = c.bandPadding;
            band.paddingOuter = c.bandPadding * 0.5f;
            float bx = 0;
            if (band.Tick(i, &bx)) {
                if (barRow) {
                    lw = kBarRowBandGap - 6.f;
                    lx = c.barAlign == BarAlign::Left
                             ? x
                             : x + w - kBarRowBandGap + 6.f;
                    ly = y +
                         (bx + band.BandWidth() * 0.5f) *
                             (plotH / (w > 0 ? w : 1)) -
                         7.f;
                    // A left-anchored row's names end right up against the
                    // bars, so they are centred in the gutter rather than
                    // starting at its left edge.
                    centered = c.barAlign == BarAlign::Left;
                } else {
                    lx = x + bx + band.BandWidth() * 0.5f - 16.f;
                }
            }
        }
        Str label = c.labels ? Str(c.labels[i]) : Str(fmt("%ds", i));
        if (!centered && c.kind != ChartKind::Bar &&
            c.kind != ChartKind::Candlestick) {
            // TextAlign::Left at the first point, Right at the last, Center
            // in between — measured, so the box the name is put in is the
            // width the name actually takes.
            Size ls = MeasureText(ctx, label, 10, 0, false, 0, 0);
            float tick = Xat(i);
            lx = i == 0         ? tick
                 : (i == n - 1) ? tick - ls.w
                                : tick - ls.w * 0.5f;
            lw = ls.w;
        }
        DrawTextAt(ctx, label, lx, ly, lw, 16, 10, th.mutedForeground,
                   centered);
    }
}

static void PaintElNode(PaintCtx* ctx, El* e, bool skipOverlay);

bool LineSafeClipBottom(const LineSpan* spans, int count, float boxBottom,
                        float contentBottom, float* outBottom) {
    const float epsilon = 1.f;
    float clip = boxBottom;
    for (int i = 0; spans && i < count; i++) {
        const LineSpan& span = spans[i];
        if (span.lineHeight <= 0 || span.top >= boxBottom ||
            span.bottom <= boxBottom + epsilon) {
            continue;
        }
        float wholeLines = floorf((boxBottom - span.top) / span.lineHeight);
        float lineTop = span.top + span.lineHeight * wholeLines;
        // A line beginning on the box edge is below it, not straddling it.
        if (lineTop < boxBottom - epsilon && lineTop < clip) {
            clip = lineTop;
        }
    }

    bool found = false;
    float lastBottom = 0;
    float lastHeight = 0;
    for (int i = 0; spans && i < count; i++) {
        const LineSpan& span = spans[i];
        if (span.lineHeight <= 0) {
            continue;
        }
        float bottom = span.top + span.lineHeight;
        while (bottom <= span.bottom + epsilon) {
            if (bottom <= clip + epsilon && (!found || bottom > lastBottom)) {
                found = true;
                lastBottom = bottom;
                lastHeight = span.lineHeight;
            }
            bottom += span.lineHeight;
        }
        // A platform shaper can make the final row a little taller than the
        // regular leading. Inline's own bottom is that row's safe boundary.
        if (span.bottom <= clip + epsilon &&
            (!found || span.bottom > lastBottom)) {
            found = true;
            lastBottom = span.bottom;
            lastHeight = span.lineHeight;
        }
    }
    if (!found) {
        // A first line taller than the whole budget keeps the part that fits;
        // an empty clamp looks broken where a cut one reads as more to come.
        return false;
    }
    if (contentBottom > boxBottom + epsilon) {
        float strip = clip - lastBottom;
        if (strip > epsilon && strip < lastHeight) {
            clip = lastBottom;
        }
    }
    if (clip >= boxBottom - epsilon) {
        return false;
    }
    if (outBottom) {
        *outBottom = clip;
    }
    return true;
}

static Bounds BoundsIntersect(Bounds a, Bounds b) {
    float x = a.x > b.x ? a.x : b.x;
    float y = a.y > b.y ? a.y : b.y;
    float right = a.Right() < b.Right() ? a.Right() : b.Right();
    float bottom = a.Bottom() < b.Bottom() ? a.Bottom() : b.Bottom();
    return {x, y, right > x ? right - x : 0.f, bottom > y ? bottom - y : 0.f};
}

static Bounds HitMaskedBounds(PaintCtx* ctx, Bounds bounds) {
    return ctx->hasHitMask ? BoundsIntersect(bounds, ctx->hitMask) : bounds;
}

static void LineClampCollect(El* e, Vec<LineSpan>* spans,
                             float* contentBottom) {
    if (!e) {
        return;
    }
    if (e->lineSpan && e->lineSpanHeight > 0) {
        VecAppend(*spans, LineSpan{e->y, e->y + e->h, e->lineSpanHeight});
    }
    for (El* c = e->first; c; c = c->next) {
        // Deferred controls over a code block and window-level overlays are
        // not document content. Rust's TextView state bounds excludes both.
        if (c->style.absolute || c->style.fixed) {
            continue;
        }
        float bottom = c->y + c->h;
        if (bottom > *contentBottom) {
            *contentBottom = bottom;
        }
        LineClampCollect(c, spans, contentBottom);
    }
}

static bool ResolveLineClamp(PaintCtx* ctx, El* e, float* clipBottom) {
    if (!e->lineClamp) {
        return false;
    }
    Vec<LineSpan> spans;
    float contentBottom = e->y + e->h;
    LineClampCollect(e, &spans, &contentBottom);
    float boxBottom = e->y + e->h;
    bool clamped = contentBottom > boxBottom + 1.f;
    if (e->onLineClamp.IsValid()) {
        LineClampEvent ev = {clamped};
        ListenerCall(ctx->app, ctx->window, e->onLineClamp, &ev);
    }
    bool tighter =
        clamped && LineSafeClipBottom(spans.els, spans.len, boxBottom,
                                      contentBottom, clipBottom);
    VecReset(spans);
    return tighter;
}

static bool IsOverlay(El* e) {
    return e->style.fixed || e->style.deferred;
}

// GPUI paints deferred elements after the tree they came from, so a dialog or
// an open dropdown covers the page instead of being covered by the siblings
// that follow it. Painting last also hit-tests first: HitTestRect walks the
// rects backwards.
static void PaintOverlays(PaintCtx* ctx, El* e) {
    if (!e) {
        return;
    }
    if (IsOverlay(e)) {
        int previousLayer = ctx->paintLayer;
        if (e->style.deferredLayer) {
            ctx->paintLayer = e->style.deferredLayer;
        }
        PaintElNode(ctx, e, false);
        ctx->paintLayer = previousLayer;
        return;
    }
    for (El* c = e->first; c; c = c->next) {
        PaintOverlays(ctx, c);
    }
}

// InputElement's cursor_bounds: where the caret sits inside the run this
// element painted. Rust measures it in prepaint from the shaped line and
// paints a quad there; the shaped line is already in hand here, so the two
// steps fold together. A run with no text puts the caret at its left edge,
// which is where an empty field with a placeholder shows it.
static void PaintCaret(PaintCtx* ctx, El* e, float font) {
    if (e->caretOff < 0 || e->caretColor.a == 0) {
        return;
    }
    float x = e->x;
    float y = e->y;
    float h = e->h;
    if (e->text.s && e->text.len > 0) {
        float maxW = e->laidMaxW > 0 ? e->laidMaxW : e->w;
        // The weight and line height the run was drawn with, or the rects
        // come back measured against a different font -- the mono family is a
        // weight sentinel -- and the caret drifts further from the glyphs the
        // further along the line it stands.
        TextLayout* tl =
            TextMeasLayout(ctx, e->text, font, maxW, e->style.wrap,
                           ElTextWeight(e), e->style.lineHeight, nullptr);
        if (tl) {
            Bounds r[32] = {};
            int off = e->caretOff;
            if (off > e->text.len) {
                off = e->text.len;
            }
            int n = 0;
            if (off > 0 && (e->caretLineEndAffinity || off == e->text.len)) {
                // The trailing edge of everything before it.
                n = TextLayoutRangeRects(tl, e->text, 0, off, r, 32);
                if (n > 0) {
                    x = e->x + r[n - 1].x + r[n - 1].w;
                    y = e->y + r[n - 1].y;
                    h = r[n - 1].h;
                }
            } else {
                // The leading edge of what follows. At a wrap boundary this
                // puts a no-affinity caret on the next row.
                n = TextLayoutRangeRects(tl, e->text, off, e->text.len, r, 32);
                if (n > 0) {
                    x = e->x + r[0].x;
                    y = e->y + r[0].y;
                    h = r[0].h;
                }
            }
            TextLayoutRelease(tl);
        }
    }
    // last_layout: where the caret ended up inside the run, which is what
    // scroll_to measures against on the next move.
    if (e->input) {
        e->input->caretX = x - e->x;
    }
    if (e->caretOutX) {
        *e->caretOutX = x;
    }
    if (e->caretOutY) {
        *e->caretOutY = y + h;
    }
    CanvasFillRect(ctx, x, y, e->caretW, h, e->caretColor);
}

void PaintEl(PaintCtx* ctx, El* e) {
    PaintElNode(ctx, e, true);
    // The overlays are the tree's second stacking layer, and saying so is
    // what lets a scene keep them apart from the tree without knowing that
    // there were two walks. See PaintCtx::paintLayer.
    ctx->paintLayer = kPaintLayerPopup;
    PaintOverlays(ctx, e);
    ctx->paintLayer = kPaintLayerTree;
}

static void PaintElNodeInner(PaintCtx* ctx, El* e, bool skipOverlay);

// with_element_opacity: the opacity in force while this element and its
// children paint is the one around it times its own, and it goes back to what
// it was afterwards.
static void PaintElNode(PaintCtx* ctx, El* e, bool skipOverlay) {
    if (!e || !ctx) {
        return;
    }
    // `group("")`: what a descendant's group_hover asks about is the pointer
    // being in this box, which is not the same question as `hoverId` — the
    // close button drawn over a card takes the hover away from the card, and
    // Rust's group hitbox does not care.
    bool prevGroup = ctx->groupHovered;
    if (e->style.group) {
        ctx->groupHovered = e->w > 0 && e->h > 0 &&
                            e->Bounds().Contains({ctx->mouseX, ctx->mouseY});
    }
    if (e->style.opacity >= 1.f) {
        PaintElNodeInner(ctx, e, skipOverlay);
    } else {
        float prev = ctx->opacity;
        ctx->opacity = prev * e->style.opacity;
        PaintElNodeInner(ctx, e, skipOverlay);
        ctx->opacity = prev;
    }
    ctx->groupHovered = prevGroup;
}

static void PaintElNodeInner(PaintCtx* ctx, El* e, bool skipOverlay) {
    if (!e || !ctx->rt) {
        return;
    }
    if (skipOverlay && IsOverlay(e)) {
        return;
    }
    // `.invisible()` until the group is hovered. The box was laid out either
    // way; this only stops it being drawn.
    if (e->style.groupHoverVisible && !ctx->groupHovered) {
        return;
    }
    // SliderIndicator::on_prepaint. Layout is over by the time an element
    // paints, so its box is final and the slider can map a position onto it.
    if (e->sliderBounds) {
        SliderSetBounds(e->sliderBounds, e->Bounds());
    }
    if (e->boundsOut) {
        *e->boundsOut = e->Bounds();
    }
    // The inspector picking an element. GPUI offers the topmost *hitbox*
    // under the pointer; the nearest thing to a hitbox here is an element
    // that draws something or answers to an id, which is what keeps an
    // invisible layout container — or the full-window layer the overlays are
    // painted into, which goes down after everything else — from standing in
    // front of the button you aimed at. Among those, the deepest wins, and a
    // tie goes to the one painted later.
    int tier = e->clickId != 0 ? 2
                               : (e->style.hasBg || e->style.border > 0 ||
                                          e->kind != ElKind::Div
                                      ? 1
                                      : 0);
    bool better = !ctx->pickHit || tier > ctx->pickTier ||
                  (tier == ctx->pickTier && ctx->paintDepth >= ctx->pick.depth);
    Bounds maskedBounds = HitMaskedBounds(ctx, e->Bounds());
    if (ctx->picking && tier > 0 && better && maskedBounds.w > 0 &&
        maskedBounds.h > 0 &&
        maskedBounds.Contains({ctx->mouseX, ctx->mouseY})) {
        InspectorPick p;
        p.id = e->clickId;
        p.elId = e->id;
        p.style = e->style;
        p.bounds = maskedBounds;
        p.kind = (int)e->kind;
        p.hasBg = e->style.hasBg;
        p.bg = e->style.bg.color;
        p.pad = e->style.pad.left;
        p.gap = e->style.gapX;
        p.radius = e->style.radius;
        p.border = e->style.border;
        p.row = e->style.dir == FlexDir::Row;
        p.font = e->style.fontSize;
        p.text = e->text;
        p.depth = ctx->paintDepth;
        ctx->pick = p;
        ctx->pickTier = tier;
        ctx->pickHit = true;
    }
    // What this element's children name as their ancestor: this element if it
    // recorded a hit rect, and whatever was around it if it did not.
    int outerHitParent = ctx->hitParent;
    if (e->clickId || e->onClick.IsValid() || e->listener.IsValid() ||
        e->clickAction || e->onHover.IsValid() || e->onMouseMove.IsValid() ||
        e->onMouseDown.IsValid() || e->onMouseUp.IsValid() ||
        e->onDragMove.IsValid() || e->onMouseDownOut.IsValid() ||
        e->onMouseUpOut.IsValid() || e->onScrollWheel.IsValid() ||
        e->drag.IsValid() || e->onDrop.IsValid() ||
        e->cursor != CursorKind::Arrow || e->slider || e->stopMouseDown ||
        e->suppressTextSelection || e->scrollMaskAxes) {
        HitRect hr;
        hr.id = e->clickId;
        hr.focusId = e->style.focusId;
        hr.bounds = maskedBounds;
        hr.onClick = e->onClick;
        hr.clickAction = e->clickAction;
        hr.clickActionArg = e->clickActionArg;
        hr.listener = e->listener;
        hr.onHover = e->onHover;
        hr.onMouseMove = e->onMouseMove;
        hr.tooltip = e->style.tooltip;
        hr.onMouseDown = e->onMouseDown;
        hr.onMouseUp = e->onMouseUp;
        hr.mouseDownPhase = e->mouseDownPhase;
        hr.mouseUpPhase = e->mouseUpPhase;
        hr.parent = ctx->hitParent;
        hr.onDragMove = e->onDragMove;
        hr.drag = e->drag;
        hr.onMouseDownOut = e->onMouseDownOut;
        hr.onMouseUpOut = e->onMouseUpOut;
        hr.onScrollWheel = e->onScrollWheel;
        hr.dropKind = e->dropKind;
        hr.onDrop = e->onDrop;
        hr.cursor = e->cursor;
        hr.slider = e->slider;
        hr.sliderAxis = e->sliderAxis;
        hr.input = e->input;
        hr.stopClick = e->stopClick;
        hr.stopMouseDown = e->stopMouseDown;
        hr.suppressTextSelection = e->suppressTextSelection;
        hr.paintLayer = ctx->paintLayer;
        VecAppend(ctx->hits, hr);
        // Everything under this element names it as the ancestor its events
        // pass through, which is the chain the two phases walk.
        ctx->hitParent = ctx->hits.len - 1;
    }
    if (e->style.overflowY == Overflow::Scroll ||
        e->style.overflowX == Overflow::Scroll) {
        ScrollRect sr;
        sr.id = e->scrollId;
        sr.bounds = e->Bounds();
        sr.contentH = e->contentH;
        sr.scrollY = e->scrollY;
        sr.contentW = e->contentW;
        sr.scrollX = e->scrollX;
        sr.mode = ElScrollMode(e, ctx->app);
        sr.barX = !e->noScrollbar && !e->noScrollbarX;
        sr.barY = !e->noScrollbar && !e->noScrollbarY;
        if (e->scrollThemeSet) {
            sr.trackWidth = e->scrollTrackWidth;
            sr.thumbWidth = e->scrollThumbWidth;
            sr.thumbHoverWidth = e->scrollThumbHoverWidth;
            sr.thumbActiveWidth = e->scrollThumbActiveWidth;
            sr.thumbInset = e->scrollThumbInset;
            sr.thumbHoverInset = e->scrollThumbHoverInset;
            sr.thumbActiveInset = e->scrollThumbActiveInset;
            sr.thumbMinLength = e->scrollThumbMinLength;
            sr.thumbHoverMinLength = e->scrollThumbHoverMinLength;
            sr.thumbActiveMinLength = e->scrollThumbActiveMinLength;
        }
        sr.maskAxes = e->scrollMaskAxes;
        sr.maskHit = e->scrollMaskAxes ? ctx->hitParent : -1;
        sr.onScroll = e->onScroll;
        sr.input = e->input;
        VecAppend(ctx->scrolls, sr);
    }

    // focus_ring_style: a focused control that explicitly opted in has its
    // own border take the ring colour.
    // That is the half of the focus appearance that costs no room, and the
    // half Rust keeps when a theme turns the ring off. The element is this
    // frame's arena copy, so writing the colour onto it is what Rust's
    // `.border_color(cx.theme().ring)` does to the style it is building.
    // `.when(is_focused && self.focus_ring_enabled, ..)`: the control's own
    // opt-out drops the whole focus appearance, both halves of it.
    bool focused = e->style.focusId && e->style.focusId == ctx->focusId &&
                   e->style.focusRing;
    if (focused) {
        e->style.borderColor = RuntimeStyleNow(ctx->app).ring;
    }

    BoxFill fill = BoxFillFor(e->style.hasActiveBg, e->style.hasHoverBg,
                              e->clickId, ctx->activeId, ctx->hoverId);
    // The group's hover is asked only when the element's own state has
    // nothing to say, which is the order the two refinements are applied in.
    bool groupFill =
        fill == BoxFill::Base && e->style.hasGroupHoverBg && ctx->groupHovered;
    for (int i = 0; i < e->style.shadowCount; i++) {
        PaintBoxShadow(ctx, e, e->style.shadows[i]);
    }
    if (fill != BoxFill::Base || groupFill || e->style.hasBg) {
        const Background& b = fill == BoxFill::Active  ? e->style.activeBg
                              : fill == BoxFill::Hover ? e->style.hoverBg
                              : groupFill              ? e->style.groupHoverBg
                                                       : e->style.bg;
        FillBackground(ctx, e->x, e->y, e->w, e->h, e->style.radius,
                       e->style.hasCorners ? &e->style.corners : nullptr, b);
    }
    if (e->style.border > 0) {
        if (e->style.borderDashed) {
            // In stroke widths; the default is what GPUI's border_dashed
            // draws. D2D's own DASH style is 2/2 and reads too sparse.
            const float dash[2] = {e->style.dashOn, e->style.dashOff};
            float half = e->style.border * 0.5f;
            if (e->style.radius <= 0) {
                // Square corners: stroke each side on its own, so both the
                // line and the dashes along it can land on whole pixels.
                float l = EdgeLine(ctx, e->x + half);
                float r = EdgeLine(ctx, e->x + e->w - half);
                float t = EdgeLine(ctx, e->y + half);
                float b = EdgeLine(ctx, e->y + e->h - half);
                float x0 = EdgeEnd(ctx, e->x);
                float x1 = EdgeEnd(ctx, e->x + e->w);
                float y0 = EdgeEnd(ctx, e->y);
                float y1 = EdgeEnd(ctx, e->y + e->h);
                Rgba bc = e->style.borderColor;
                float bw = e->style.border;
                CanvasLine(ctx, x0, t, x1, t, bw, bc, dash);
                CanvasLine(ctx, x0, b, x1, b, bw, bc, dash);
                CanvasLine(ctx, l, y0, l, y1, bw, bc, dash);
                CanvasLine(ctx, r, y0, r, y1, bw, bc, dash);
            } else {
                CanvasStrokeRound(ctx, e->x, e->y, e->w, e->h,
                                  ClampRadius(e->style.radius, e->w, e->h),
                                  e->style.border, e->style.borderColor, dash);
            }
        } else if (e->style.hasCorners) {
            StrokeCorners(ctx, e->x, e->y, e->w, e->h, e->style.corners,
                          e->style.border, e->style.borderColor);
        } else {
            DrawRoundStroke(ctx, e->x, e->y, e->w, e->h, e->style.radius,
                            e->style.border, e->style.borderColor);
        }
    }
    // An edge border sits inside the box and covers whole pixels: the line
    // goes half a stroke in from the edge, and lands on a device pixel.
    if (e->style.borderT > 0) {
        float y = EdgeLine(ctx, e->y + e->style.borderT * 0.5f);
        DrawLine(ctx, e->x, y, e->x + e->w, y, e->style.borderT,
                 e->style.borderColor);
    }
    if (e->style.borderB > 0) {
        float y = EdgeLine(ctx, e->y + e->h - e->style.borderB * 0.5f);
        DrawLine(ctx, e->x, y, e->x + e->w, y, e->style.borderB,
                 e->style.borderColor);
    }
    if (e->style.borderL > 0) {
        float x = EdgeLine(ctx, e->x + e->style.borderL * 0.5f);
        DrawLine(ctx, x, e->y, x, e->y + e->h, e->style.borderL,
                 e->style.borderColor);
    }
    if (e->style.borderR > 0) {
        float x = EdgeLine(ctx, e->x + e->w - e->style.borderR * 0.5f);
        DrawLine(ctx, x, e->y, x, e->y + e->h, e->style.borderR,
                 e->style.borderColor);
    }

    bool clip = e->style.overflowY != Overflow::Visible ||
                e->style.overflowX != Overflow::Visible;
    float clipBottom = e->y + e->h;
    ResolveLineClamp(ctx, e, &clipBottom);
    float clipH = clipBottom - e->y;
    if (clipH < 0) clipH = 0;
    if (clip) {
        CanvasPushClip(ctx, e->x, e->y, e->w, clipH);
    }

    Bounds previousHitMask = ctx->hitMask;
    bool previousHasHitMask = ctx->hasHitMask;
    if (clip) {
        Bounds ownMask = {e->x, e->y, e->w, clipH};
        ctx->hitMask = previousHasHitMask
                           ? BoundsIntersect(previousHitMask, ownMask)
                           : ownMask;
        ctx->hasHitMask = true;
    }

    // InputElement's input_bounds: the box a press maps against. The
    // outermost binding of the frame wins, so the themed field's whole
    // bordered box counts and not just the run inside it.
    if (e->input && e->kind != ElKind::Text) {
        bool seen = false;
        for (int i = 0; i < ctx->inputs.len && !seen; i++) {
            seen = ctx->inputs[i] == e->input;
        }
        if (!seen) {
            e->input->inputBounds = e->Bounds();
            // The box the field scrolls inside, less what it pads by.
            e->input->viewW = e->w - e->style.pad.HorizontalAxisSum();
            // The viewport is the clipping/scrolling box. An inner content
            // column laid out to the document height must not replace it —
            // that made the next frame treat every line as visible.
            if (e->style.overflowY == Overflow::Scroll ||
                e->style.overflowY == Overflow::Hidden) {
                float vh = e->h - e->style.pad.VerticalAxisSum();
                if (vh > 0) {
                    e->input->viewH = vh;
                }
            }
            // scroll_size.width: how wide the longest row came out, which is
            // what a sideways scroll clamps against. Only a box that scrolls
            // that way reports it — a field that clips instead never moves
            // sideways, and a stale width would leave its caret arithmetic
            // scrolling text that cannot move.
            if (e->style.overflowX == Overflow::Scroll) {
                e->input->contentW = e->contentW;
            }
            VecAppend(ctx->inputs, e->input);
        }
    }
    // An inline image takes a place in the document order without taking any
    // text: node.rs's ImageNode has no selection of its own, and
    // `selected_source` emits its `![alt](url)` when the selection runs into
    // it. The copier walks runs, so the image registers one — empty, and
    // carrying the Markdown on the SelSource beside it.
    if (e->kind == ElKind::Image && e->selectable && e->selSrc) {
        TextHit th;
        th.bounds = e->Bounds();
        th.docOff = ctx->textDocLen;
        th.owner = e->selectionOwner;
        th.src = e->selSrc;
        th.join = e->selJoin;
        th.atom = true;
        th.scope = e->style.trapId;
        th.paintLayer = ctx->paintLayer;
        VecAppend(ctx->texts, th);
        ctx->textDocLen += 1;
    }
    if (e->kind == ElKind::Text) {
        float font = e->laidFont > 0
                         ? e->laidFont
                         : (e->style.fontSize > 0 ? e->style.fontSize : 14.f);
        if (e->input) {
            e->input->lastBounds = e->Bounds();
            e->input->lastFont = font;
        }
        Rgba c = e->style.hasColor ? e->style.color
                                   : RuntimeStyleNow(ctx->app).foreground;
        int lo = e->selLo;
        int hi = e->selHi;
        if (e->selectable && e->text.s) {
            int docOff = ctx->textDocLen;
            TextHit th;
            th.bounds = e->Bounds();
            th.text = e->text;
            th.font = font;
            th.maxW = e->laidMaxW > 0 ? e->laidMaxW : e->w;
            th.wrap = e->style.wrap;
            th.docOff = docOff;
            th.owner = e->selectionOwner;
            th.src = e->selSrc;
            th.join = e->selJoin;
            // The trap this run sits in — a dialog, a sheet — which is the
            // TextSelectionScopeId a gesture inside it stays within.
            th.scope = e->style.trapId;
            th.paintLayer = ctx->paintLayer;
            VecAppend(ctx->texts, th);
            ctx->textDocLen += e->text.len + 1;
            int a = ctx->selA;
            int b = ctx->selB;
            if (ctx->selScope >= 0 && ctx->selScope != e->style.trapId) {
                a = -1;
                b = -1;
            }
            if (a >= 0 && b >= 0 && a != b) {
                if (a > b) {
                    int t = a;
                    a = b;
                    b = t;
                }
                int tlo = a > docOff ? a : docOff;
                int thi = b < docOff + e->text.len ? b : docOff + e->text.len;
                if (tlo < thi) {
                    lo = tlo - docOff;
                    hi = thi - docOff;
                }
            }
        }
        // truncate: a run that does not wrap is the same size whatever width
        // it was measured against, so the shaped run is cached without one and
        // the box it was drawn for cannot do the cutting. This can.
        //
        // Horizontally only. GPUI's `truncate()` is `overflow_hidden` *and*
        // `text_ellipsis`, and the first of those cut the glyphs of "g" and
        // "y" off at the line box, because a line box of relative 1.0 is the
        // font size while the ink of a descender runs below it. Upstream
        // dropped the `overflow_hidden` and kept the ellipsis; the same split
        // here is a clip that still ends the run at its width but leaves a
        // line box of slack above and below for the ink to finish in.
        bool clipText = e->style.truncate && e->laidMaxW > 0;
        if (clipText) {
            CanvasPushClip(ctx, e->x, e->y - e->h, e->laidMaxW, e->h * 3.f);
        }
        // Under the selection quad as well as under the glyphs: a match the
        // caret happens to be inside still reads as selected.
        for (int i = 0; i < e->nWashes; i++) {
            const TextSpan& w = e->washes[i];
            if (w.bg.a == 0 || w.hi <= w.lo) {
                continue;
            }
            PaintTextRange(ctx, e->text, font,
                           e->laidMaxW > 0 ? e->laidMaxW : e->w, e->style.wrap,
                           ElTextWeight(e), e->style.lineHeight, e->x, e->y,
                           w.lo, w.hi, w.bg);
        }
        if (lo >= 0 && hi > lo) {
            PaintTextRange(ctx, e->text, font,
                           e->laidMaxW > 0 ? e->laidMaxW : e->w, e->style.wrap,
                           ElTextWeight(e), e->style.lineHeight, e->x, e->y, lo,
                           hi, e->selColor);
        }
        if (e->markLo >= 0 && e->markHi > e->markLo) {
            PaintTextUnderline(
                ctx, e->text, font, e->laidMaxW > 0 ? e->laidMaxW : e->w,
                e->style.wrap, ElTextWeight(e), e->style.lineHeight, e->x, e->y,
                e->markLo, e->markHi, c);
        }
        if (e->nSpans > 0 && e->text.s) {
            PaintTextSpans(ctx, e, font, c);
        } else if (e->laidLayout) {
            TextLayoutDraw(ctx, e->laidLayout, e->x, e->y, c, e->style.truncate,
                           e->laidMaxW);
        } else {
            DrawTextAt(ctx, e->text, e->x, e->y, e->w, e->h, font, c,
                       e->style.truncate, e->style.wrap, e->laidMaxW,
                       ElTextWeight(e), e->style.lineHeight);
        }
        // range_to_bounds: where a named run of this text landed, for a
        // caller that hit-tests against it on a later frame.
        if (e->rangeOut && e->rangeOutHi > e->rangeOutLo) {
            *e->rangeOut = Bounds{};
            TextLayout* tl = TextMeasLayout(
                ctx, e->text, font, e->laidMaxW > 0 ? e->laidMaxW : e->w,
                e->style.wrap, (uint8_t)ElTextWeight(e), e->style.lineHeight,
                nullptr);
            if (tl) {
                Bounds r[8] = {};
                int n = TextLayoutRangeRects(tl, e->text, e->rangeOutLo,
                                             e->rangeOutHi, r, 8);
                if (n > 0) {
                    *e->rangeOut = {e->x + r[0].x, e->y + r[0].y, r[0].w,
                                    r[0].h};
                }
                TextLayoutRelease(tl);
            }
        }
        // The rules a diagnostic asked for, over whatever drew the glyphs.
        for (int i = 0; i < e->nUnderlines; i++) {
            const TextSpan& u = e->underlines[i];
            if (u.hi <= u.lo || u.color.a == 0) {
                continue;
            }
            PaintTextUnderline(
                ctx, e->text, font, e->laidMaxW > 0 ? e->laidMaxW : e->w,
                e->style.wrap, ElTextWeight(e), e->style.lineHeight, e->x, e->y,
                u.lo, u.hi, u.color, u.wavy);
        }
        if (clipText) {
            CanvasPopClip(ctx);
        }
        PaintCaret(ctx, e, font);
    } else if (e->kind == ElKind::Image && !e->imageReplacement) {
        // image.h resolves the src: the asset an application shipped, the
        // data: URI, or the body a worker thread fetched. PrepareEl has
        // already selected a delayed loading view or a failed-load fallback;
        // only the ready image reaches this branch.
        RenderImage* img = ImageForSource(ctx->pa, e->imageSource);
        int opsLen = 0;
        const uint8_t* ops =
            img ? nullptr
                : ImageVectorForSource(ctx->pa, e->imageSource, &opsLen);
        Bounds bounds = e->Bounds();
        bool drewSvg = false;
        if (img) {
            bool wantsAnimation = false;
            int frameIndex =
                ImageFrameIndex(img, PlatReduceMotion(), &wantsAnimation);
            if (wantsAnimation) {
                ctx->wantsAnimFrame = true;
            }
            Bounds imageBounds = ObjectFitBounds(
                e->objectFit, bounds, RenderImageSizePx(img, frameIndex));
            RenderImageDraw(ctx, img, bounds, imageBounds, frameIndex,
                            e->style.radius, e->imageGrayscale);
        } else if (ops) {
            Size imageSize = {};
            if (DrawOpsViewBox(ops, opsLen, &imageSize)) {
                Bounds imageBounds =
                    ObjectFitBounds(e->objectFit, bounds, imageSize);
                CanvasPushClip(ctx, bounds.x, bounds.y, bounds.w, bounds.h);
                drewSvg = SvgDrawOps(
                    ctx, ops, opsLen, imageBounds.x, imageBounds.y,
                    imageBounds.w, imageBounds.h,
                    e->style.hasColor ? e->style.color
                                      : RuntimeStyleNow(ctx->app).foreground,
                    0, e->imageGrayscale);
                CanvasPopClip(ctx);
            }
        }
        if (drewSvg) {
            // An SVG is not a bitmap for any of the three backends to decode;
            // it is the vector the icon renderer already walks, and a picture
            // with colours of its own keeps them.
        } else if (!img && e->imageLoadState == ImageLoadState::Failed &&
                   e->text.s && e->text.len > 0) {
            // The alt text, in the color the text around it uses.
            float font =
                e->laidFont > 0
                    ? e->laidFont
                    : (e->style.fontSize > 0 ? e->style.fontSize : 14.f);
            Rgba c = e->style.hasColor
                         ? e->style.color
                         : RuntimeStyleNow(ctx->app).mutedForeground;
            DrawTextAt(ctx, e->text, e->x, e->y, e->w, e->h, font, c, false,
                       e->style.wrap, e->laidMaxW, ElTextWeight(e),
                       e->style.lineHeight);
        }
    } else if (e->kind == ElKind::Icon) {
        Rgba c = e->style.hasColor ? e->style.color
                                   : RuntimeStyleNow(ctx->app).foreground;
        float s = e->w > 0 ? e->w : 16;
        // Every lucide icon is compiled in as draw-op bytecode
        // (asset_icons.cpp), so this reads no file; an application's own
        // `.svg` is converted to the same bytecode the first time it is
        // asked for.
        Str path = e->iconPath.s ? e->iconPath : IconNamePath(e->icon);
        SvgDraw(ctx, path, e->x, e->y, s, c, e->style.rotate);
    } else if (e->kind == ElKind::Progress) {
        const RuntimeStyle& th = RuntimeStyleNow(ctx->app);
        Background track = BackgroundOpacity(th.progress, 0.2f);
        FillBackground(ctx, e->x, e->y, e->w, e->h, e->style.radius, nullptr,
                       track);
        float fw = e->w * (e->progress / 100.f);
        if (fw > 0) {
            FillBackground(ctx, e->x, e->y, fw, e->h, e->style.radius, nullptr,
                           th.progress);
        }
    } else if (e->kind == ElKind::Chart) {
        DrawChart(ctx, e);
    }
    if (e->kind != ElKind::Text) {
        PaintCaret(ctx, e, e->laidFont > 0 ? e->laidFont : 14.f);
    }
    if (e->customPaint) {
        e->customPaint(ctx, e, e->customUser);
    }

    ctx->paintDepth++;
    for (El* c = e->first; c; c = c->next) {
        PaintElNode(ctx, c, skipOverlay);
    }
    ctx->hitMask = previousHitMask;
    ctx->hasHitMask = previousHasHitMask;
    ctx->hitParent = outerHitParent;
    ctx->paintDepth--;

    if (clip) {
        CanvasPopClip(ctx);
    }

    // `wants_visible` and the visibility animation under it: an always-on bar,
    // a drag, a hover in hover mode, or a scroll inside the idle hold puts the
    // bar up; anything else takes it away over `exit`. What moves is what is
    // painted — the band, the press geometry and the layout stay where they
    // are, which is what keeps a bar that is sliding out from swallowing a
    // click meant for the page.
    ScrollbarMode barMode = ElScrollMode(e, ctx->app);
    bool overBox = e->Bounds().Contains({ctx->mouseX, ctx->mouseY});
    bool dragging = ctx->scrollDragId != 0 && ctx->scrollDragId == e->scrollId;
    ScrollbarMotion barMotion =
        e->scrollThemeSet ? e->scrollMotion : ScrollbarMotionFor(barMode);
    float trackW = ScrollbarTrackWidth(e);
    if (trackW < 0) trackW = 0;
    bool canVertical = !e->noScrollbarY &&
                       e->style.overflowY == Overflow::Scroll &&
                       e->contentH > e->h + 1.f && e->h > 0;
    bool canHorizontal = !e->noScrollbarX &&
                         e->style.overflowX == Overflow::Scroll &&
                         e->contentW > e->w + 1.f && e->w > 0;
    bool onBand =
        overBox && ((canVertical && ctx->mouseX >= e->x + e->w - trackW) ||
                    (canHorizontal && ctx->mouseY >= e->y + e->h - trackW));
    bool barVisible = !e->noScrollbar;
    float barAlpha = 1.f;
    float barSlide = 0.f;
    if (barVisible && e->scrollId == 0) {
        // No ScrollId is no state: the area cannot be found again next frame,
        // so it keeps the bar rather than animating it from nothing every
        // time. Rust's state is keyed the same way, off the element id.
        barVisible = barMode == ScrollbarMode::Always || onBand;
    } else if (barVisible) {
        // is_hovered_on_bar: the pointer resting inside the band the thumb
        // runs down. It holds the bar up in hover mode, and in scrolling mode
        // only while the bar is already up — `hover_keeps_visible`.
        double now = TimeNow();
        ScrollFade* f = ScrollFadeFor(e->scrollId, e->scrollY, e->scrollX);
        bool wasVisible = f->opacity.target > 0.f;
        if (f->y != e->scrollY || f->x != e->scrollX || dragging ||
            ScrollbarHoverKeepsVisible(barMode, onBand, wasVisible)) {
            f->y = e->scrollY;
            f->x = e->scrollX;
            f->at = now;
            f->hasLast = true;
        }
        bool want = ScrollbarWantsVisible(barMode, onBand, dragging, f,
                                          barMotion.idle, now);
        // `entrance_for`: only a hover mode bar under the pointer's own thumb
        // slides; everything else fades in place.
        ScrollbarEntrance entrance = (barMode == ScrollbarMode::Hover && onBand)
                                         ? barMotion.thumbHoverEntrance
                                         : barMotion.entrance;
        float enter = barMode == ScrollbarMode::Always ? 0 : barMotion.enter;
        float exit = barMode == ScrollbarMode::Always ? 0 : barMotion.exit;
        VisibilitySetVisible(f, want, entrance, enter, exit, now);
        ScrollbarVisibility vis = VisibilitySample(f, now);
        barAlpha = vis.opacity;
        // `visibility_translation`: the bar sits `track_width` off its edge at
        // the start of the slide and arrives as the progress reaches 1.
        barSlide = ScrollbarSlideOffset(trackW, vis.position);
        barVisible = barAlpha > 0.f;
        if (vis.running) {
            ctx->wantsAnimFrame = true;
        }
    }
    // A bar nobody can see is a bar nobody can grab: the ScrollRect this frame
    // recorded says so, and ScrollbarAt skips it.
    if (e->scrollId != 0) {
        for (int i = ctx->scrolls.len - 1; i >= 0; i--) {
            if (ctx->scrolls[i].id == e->scrollId) {
                ctx->scrolls[i].barVisible = barVisible;
                break;
            }
        }
    }
    // style_for_normal / style_for_hovered_bar / style_for_hovered_thumb /
    // style_for_active. Hovering the track keeps the normal thumb style;
    // hovering the thumb or dragging selects its own full style tuple.
    ScrollFade* barState =
        e->scrollId != 0 ? ScrollFadeFor(e->scrollId, e->scrollY, e->scrollX)
                         : nullptr;
    // The thumb's radius is `theme.radius`, clamped to half the thumb — a
    // wider thumb rounds more, which is what `clamp_thumb_radius` says.
    const RuntimeStyle& barTheme = RuntimeStyleNow(ctx->app);
    bool hasVertical = barVisible && canVertical;
    bool hasHorizontal = barVisible && canHorizontal;
    if (hasVertical) {
        bool onBar = overBox && ctx->mouseX >= e->x + e->w - trackW;
        float normalInset = ScrollbarThumbInset(e, ScrollbarPaintState::Normal);
        float normalRaw = ScrollbarThumbSize(
            e->h, e->h, e->contentH,
            ScrollbarThumbMinLength(e, ScrollbarPaintState::Normal));
        float normalStart =
            ScrollbarThumbPos(e->h, normalRaw, e->scrollY, e->h, e->contentH);
        float normalLength = normalRaw - normalInset * 2.f;
        if (normalLength < 0) normalLength = 0;
        bool pointerOnThumb =
            onBar && ctx->mouseX <= e->x + e->w - normalInset &&
            ctx->mouseY >= e->y + normalStart + normalInset &&
            ctx->mouseY < e->y + normalStart + normalInset + normalLength;
        bool axisDragging = dragging && !ctx->scrollDragHorizontal;
        ScrollbarPaintState state =
            axisDragging
                ? ScrollbarPaintState::Active
                : (pointerOnThumb ? ScrollbarPaintState::HoverThumb
                                  : (onBar ? ScrollbarPaintState::HoverBar
                                           : ScrollbarPaintState::Normal));
        float inset = ScrollbarThumbInset(e, state);
        float rawThumbH = ScrollbarThumbSize(e->h, e->h, e->contentH,
                                             ScrollbarThumbMinLength(e, state));
        float thumbH = rawThumbH - inset * 2.f;
        if (thumbH < 0) thumbH = 0;
        float wantW = ScrollbarThumbWidth(e, state);
        float thumbW = wantW;
        if (barState) {
            thumbW =
                WidthTarget(&barState->widthY, &barState->widthYSet, wantW,
                            barMotion.expand, TimeNow(), &ctx->wantsAnimFrame);
        }
        float thumbX = e->x + e->w - thumbW - inset;
        float thumbY =
            e->y + inset +
            ScrollbarThumbPos(e->h, rawThumbH, e->scrollY, e->h, e->contentH);
        // The track, which every default theme leaves transparent — the band
        // is Rust's WIDTH and reaches the whole length of the box. A vertical
        // bar slides in from the right, so the slide is along x.
        FillBackground(ctx, e->x + e->w - trackW + barSlide, e->y, trackW, e->h,
                       0, nullptr,
                       ScrollbarBarBg(e, barTheme, state, barAlpha));
        FillBackground(ctx, thumbX + barSlide, thumbY, thumbW, thumbH,
                       ThumbRadius(e, barTheme, state, thumbW, thumbH), nullptr,
                       ScrollbarThumbBg(e, barTheme, state, barAlpha));
    }
    if (hasHorizontal) {
        // The horizontal bar is the same arithmetic along the other axis,
        // which is how Rust writes it: one path, `is_vertical` picking the
        // pair of numbers it reads.
        bool onBar = overBox && ctx->mouseY >= e->y + e->h - trackW;
        float marginEnd = hasVertical ? trackW : 0;
        float normalInset = ScrollbarThumbInset(e, ScrollbarPaintState::Normal);
        float normalRaw = ScrollbarThumbSize(
            e->w, e->w, e->contentW,
            ScrollbarThumbMinLength(e, ScrollbarPaintState::Normal));
        float normalStart = ScrollbarThumbPos(e->w, normalRaw, e->scrollX, e->w,
                                              e->contentW, marginEnd);
        float normalLength = normalRaw - normalInset * 2.f;
        if (normalLength < 0) normalLength = 0;
        bool pointerOnThumb =
            onBar && ctx->mouseY <= e->y + e->h - normalInset &&
            ctx->mouseX >= e->x + normalStart + normalInset &&
            ctx->mouseX < e->x + normalStart + normalInset + normalLength;
        bool axisDragging = dragging && ctx->scrollDragHorizontal;
        ScrollbarPaintState state =
            axisDragging
                ? ScrollbarPaintState::Active
                : (pointerOnThumb ? ScrollbarPaintState::HoverThumb
                                  : (onBar ? ScrollbarPaintState::HoverBar
                                           : ScrollbarPaintState::Normal));
        float inset = ScrollbarThumbInset(e, state);
        float rawThumbW = ScrollbarThumbSize(e->w, e->w, e->contentW,
                                             ScrollbarThumbMinLength(e, state));
        float thumbW = rawThumbW - inset * 2.f;
        if (thumbW < 0) thumbW = 0;
        float wantH = ScrollbarThumbWidth(e, state);
        float thumbH = wantH;
        if (barState) {
            thumbH =
                WidthTarget(&barState->widthX, &barState->widthXSet, wantH,
                            barMotion.expand, TimeNow(), &ctx->wantsAnimFrame);
        }
        float thumbY = e->y + e->h - thumbH - inset;
        float thumbX = e->x + inset +
                       ScrollbarThumbPos(e->w, rawThumbW, e->scrollX, e->w,
                                         e->contentW, marginEnd);
        // A horizontal bar slides up from the bottom, so its slide is along y.
        FillBackground(ctx, e->x, e->y + e->h - trackW + barSlide, e->w, trackW,
                       0, nullptr,
                       ScrollbarBarBg(e, barTheme, state, barAlpha));
        FillBackground(ctx, thumbX, thumbY + barSlide, thumbW, thumbH,
                       ThumbRadius(e, barTheme, state, thumbH, thumbW), nullptr,
                       ScrollbarThumbBg(e, barTheme, state, barAlpha));
    }

    if (focused && RuntimeStyleNow(ctx->app).focusRing) {
        // The other half of focus_ring_style: FOCUS_RING_WIDTH of the ring
        // colour at FOCUS_RING_OPACITY, in the three DIPs immediately outside
        // the element's border, with the corners widened to match. Rust hangs
        // it off the control's focus state after the UI layer explicitly calls
        // focus_ring_style — a control focused by a click shows it as much as
        // one reached with Tab — and paints it as an absolutely placed child,
        // which an ancestor that clips will cut off either way.
        // Which is why `Theme::focus_ring` exists: an application that clips
        // its containers turns the ring off and keeps the border above.
        Bounds ring = e->Bounds().Inset(-kFocusRingWidth);
        DrawRoundStroke(
            ctx, ring.x, ring.y, ring.w, ring.h,
            e->style.radius + kFocusRingWidth, kFocusRingWidth,
            RgbaOpacity(RuntimeStyleNow(ctx->app).ring, kFocusRingOpacity));
    }
}

const HitRect* HitTestRect(PaintCtx* ctx, float x, float y) {
    for (int i = ctx->hits.len - 1; i >= 0; i--) {
        const HitRect& h = ctx->hits[i];
        if (h.bounds.Contains({x, y})) {
            return &ctx->hits[i];
        }
    }
    return nullptr;
}

const HitRect* HitTestDrop(PaintCtx* ctx, float x, float y, Str kind) {
    if (kind.s == nullptr) {
        return nullptr;
    }
    for (int i = ctx->hits.len - 1; i >= 0; i--) {
        const HitRect& h = ctx->hits[i];
        if (!h.onDrop.IsValid() || h.dropKind.s == nullptr) {
            continue;
        }
        if (!base::StrEq(h.dropKind, kind)) {
            continue;
        }
        if (h.bounds.Contains({x, y})) {
            return &ctx->hits[i];
        }
    }
    return nullptr;
}

InputState* InputAtPosition(PaintCtx* ctx, float x, float y) {
    for (int i = ctx->inputs.len - 1; i >= 0; i--) {
        InputState* s = ctx->inputs[i];
        if (s->inputBounds.Contains({x, y})) {
            return s;
        }
    }
    return nullptr;
}

int HitTest(PaintCtx* ctx, float x, float y) {
    const HitRect* h = HitTestRect(ctx, x, y);
    return h ? h->id : 0;
}

const ScrollRect* HitScrollRect(PaintCtx* ctx, float x, float y) {
    for (int i = ctx->scrolls.len - 1; i >= 0; i--) {
        const ScrollRect& s = ctx->scrolls[i];
        if (s.bounds.Contains({x, y})) {
            return &ctx->scrolls[i];
        }
    }
    return nullptr;
}

static float DistToInterval(float v, float lo, float hi) {
    if (v < lo) {
        return lo - v;
    }
    if (v > hi) {
        return v - hi;
    }
    return 0.f;
}

// The selectable run under (x, y), plus where inside it the point landed.
// `nearest` widens the search to the closest run when none contains the point,
// which is what a drag past the end of a paragraph needs.
// `scope` is a TextSelectionScopeId — the trap the run sits in — and -1 is
// every one of them. A drag that began inside a dialog cannot reach the page
// behind it, which is what Rust's activate_scope arranges.
static const TextHit* TextHitFind(PaintCtx* ctx, float x, float y, bool nearest,
                                  Point* outRel, int scope = -1,
                                  int minLayer = 0) {
    if (!ctx) {
        return nullptr;
    }
    const TextHit* best = nullptr;
    float bestScore = 1e9f;
    for (int i = ctx->texts.len - 1; i >= 0; i--) {
        const TextHit& h = ctx->texts[i];
        if (h.paintLayer < minLayer) {
            continue;
        }
        if (scope >= 0 && h.scope != scope) {
            continue;
        }
        if (h.bounds.Contains({x, y})) {
            best = &h;
            nearest = false;
            break;
        }
        if (!nearest) {
            continue;
        }
        float dy = DistToInterval(y, h.bounds.y, h.bounds.Bottom());
        float dx = DistToInterval(x, h.bounds.x, h.bounds.Right());
        float score = dy * 1000.f + dx;
        if (score < bestScore) {
            bestScore = score;
            best = &h;
        }
    }
    if (!best || !best->text.s) {
        return nullptr;
    }
    Point rel = {x - best->bounds.x, y - best->bounds.y};
    if (nearest) {
        if (rel.x < 0) {
            rel.x = 0;
        }
        if (rel.y < 0) {
            rel.y = 0;
        }
        if (rel.x > best->bounds.w) {
            rel.x = best->bounds.w;
        }
        if (rel.y > best->bounds.h) {
            rel.y = best->bounds.h;
        }
    }
    *outRel = rel;
    return best;
}

// The byte offset inside `h` that `rel` points at, clamped into it.
static int TextHitLocal(PaintCtx* ctx, const TextHit* h, Point rel) {
    int local =
        TextIndexAt(ctx, h->text, h->font, h->maxW > 0 ? h->maxW : h->bounds.w,
                    h->wrap, rel.x, rel.y);
    if (local < 0) {
        local = 0;
    }
    if (local > h->text.len) {
        local = h->text.len;
    }
    return local;
}

int TextHitOffsetAt(PaintCtx* ctx, float x, float y, bool nearest) {
    return TextHitOffsetIn(ctx, x, y, nearest, -1, nullptr);
}

int TextHitOffsetIn(PaintCtx* ctx, float x, float y, bool nearest, int scope,
                    int* outScope, int minLayer) {
    Point rel = {};
    const TextHit* h = TextHitFind(ctx, x, y, nearest, &rel, scope, minLayer);
    if (!h) {
        return -1;
    }
    if (outScope) {
        *outScope = h->scope;
    }
    return h->docOff + TextHitLocal(ctx, h, rel);
}

// The character at `i` and how many bytes it took. A byte that is not valid
// UTF-8 counts as one character of its own value: the rules above only ask
// which class it lands in, and every stray byte lands in the same one.
int Utf8At(Str s, int i, uint32_t* out) {
    const uint8_t* p = (const uint8_t*)s.s + i;
    uint8_t c = p[0];
    if (c < 0x80) {
        *out = c;
        return 1;
    }
    int n = (c & 0xE0) == 0xC0   ? 2
            : (c & 0xF0) == 0xE0 ? 3
            : (c & 0xF8) == 0xF0 ? 4
                                 : 1;
    if (n == 1 || i + n > s.len) {
        *out = c;
        return 1;
    }
    uint32_t cp = (uint32_t)(c & (0xFF >> (n + 1)));
    for (int k = 1; k < n; k++) {
        if ((p[k] & 0xC0) != 0x80) {
            *out = c;
            return 1;
        }
        cp = (cp << 6) | (uint32_t)(p[k] & 0x3F);
    }
    *out = cp;
    return n;
}

// Where the character before `i` starts.
int Utf8Prev(Str s, int i) {
    int j = i - 1;
    while (j > 0 && ((uint8_t)s.s[j] & 0xC0) == 0x80) {
        j--;
    }
    return j < 0 ? 0 : j;
}

bool TextMultiClickRange(PaintCtx* ctx, float x, float y, int clickCount,
                         int* outA, int* outB) {
    return TextMultiClickRangeIn(ctx, x, y, clickCount, -1, outA, outB,
                                 nullptr);
}

bool TextMultiClickRangeIn(PaintCtx* ctx, float x, float y, int clickCount,
                           int scope, int* outA, int* outB, int* outScope) {
    if (clickCount < 2) {
        return false;
    }
    Point rel = {};
    const TextHit* h = TextHitFind(ctx, x, y, false, &rel, scope);
    if (!h) {
        return false;
    }
    int local = TextHitLocal(ctx, h, rel);
    int a = 0;
    int b = 0;
    if (clickCount == 2) {
        if (!TextWordRangeAt(h->text, local, &a, &b)) {
            return false;
        }
    } else {
        TextLineRangeAt(h->text, local, &a, &b);
    }
    if (a >= b) {
        return false;
    }
    if (outScope) {
        *outScope = h->scope;
    }
    *outA = h->docOff + a;
    *outB = h->docOff + b;
    return true;
}

int CopyTextHits(PaintCtx* ctx, int a, int b, char* out, int cap) {
    return CopyTextHitsIn(ctx, a, b, -1, out, cap);
}

// The copier's output cursor. Every piece goes through Put, so the cap is
// checked in one place and a document longer than the buffer stops cleanly
// rather than at a half-written affix.
struct CopyOut {
    char* buf = nullptr;
    int cap = 0;
    int n = 0;
};

static void CopyPut(CopyOut* o, Str s) {
    if (s.len <= 0 || !s.s) {
        return;
    }
    int take = s.len;
    if (o->n + take > o->cap - 1) {
        take = o->cap - 1 - o->n;
    }
    if (take <= 0) {
        return;
    }
    memcpy(o->buf + o->n, s.s, (size_t)take);
    o->n += take;
}

// The same, with every newline inside `s` followed by `linePre` — the `> ` a
// blockquote puts on each of its lines, applied to a run that carries its own
// line breaks (an unhighlighted code block is one such run).
static void CopyPutLines(CopyOut* o, Str s, Str linePre) {
    if (linePre.len <= 0) {
        CopyPut(o, s);
        return;
    }
    int at = 0;
    for (int i = 0; i < s.len; i++) {
        if (s.s[i] != '\n') {
            continue;
        }
        CopyPut(o, Str(s.s + at, i + 1 - at));
        CopyPut(o, linePre);
        at = i + 1;
    }
    CopyPut(o, Str(s.s + at, s.len - at));
}

// Whether the selection has run into the inline image registered at `i`.
//
// node.rs walks a paragraph as the runs *between* its images and emits an
// image when the selection runs into it: the run before it was selected and
// selected to its end (`selected.emitted && selected.at_end`), and the run
// after it is selected from its beginning — `at_start` is what flushes the
// images the walk has queued. A paragraph that begins or ends with an image
// has no run on that side, and that counts as reaching it: a drag that stops
// at the last word before a trailing picture still takes the picture, and so
// does selecting the sentence after a leading one.
//
// The run before ends one byte before the image's own place in the document
// order, and the run after starts one past it — the gap of one the
// registration leaves between runs.
static bool TextHitOwnerMatches(const TextHit& hit, EntityId owner) {
    return !owner.IsValid() || hit.owner == owner;
}

static bool AtomReached(PaintCtx* ctx, int i, int a, int b, EntityId owner) {
    const TextHit& t = ctx->texts[i];
    const SelBlock* blk = t.src ? t.src->block : nullptr;
    const TextHit* prev = i > 0 ? &ctx->texts[i - 1] : nullptr;
    const TextHit* next = i + 1 < ctx->texts.len ? &ctx->texts[i + 1] : nullptr;
    if (prev && !TextHitOwnerMatches(*prev, owner)) prev = nullptr;
    if (next && !TextHitOwnerMatches(*next, owner)) next = nullptr;
    // Only a run of the same block is a run of this paragraph.
    if (prev && (!prev->src || prev->src->block != blk)) {
        prev = nullptr;
    }
    if (next && (!next->src || next->src->block != blk)) {
        next = nullptr;
    }
    int pos = t.docOff;
    if (!prev && !next) {
        // A paragraph that is nothing but the picture. Rust emits nothing for
        // it — the paragraph's own `source` is empty — and it is the document
        // walk that takes it when the blocks either side are selected. Here
        // that is the selection having run past the place it sits in.
        return a <= pos && b > pos;
    }
    if (!prev) {
        // Nothing before it to reach it from, so what says the selection got
        // there is the run after: selected, and from its beginning.
        return a <= next->docOff && b > next->docOff;
    }
    // The run before it, selected and to its end. A selection that starts
    // after the picture fails this, which is Rust dropping the queued image
    // when the run it flushes against is not selected at_start.
    return a < pos - 1 && b >= pos - 1;
}

static int CopyTextHitsFiltered(PaintCtx* ctx, int a, int b, int scope,
                                EntityId owner, char* out, int cap,
                                SelectionFormat fmt) {
    if (!out || cap <= 0) {
        return 0;
    }
    out[0] = 0;
    if (!ctx || a < 0 || b < 0 || a == b) {
        return 0;
    }
    if (a > b) {
        int t = a;
        a = b;
        b = t;
    }
    bool src = fmt == SelectionFormat::Source;
    CopyOut o = {out, cap, 0};
    // The block the selection is inside right now, so entering and leaving
    // one can emit its fences, and the mark group whose closing affix is
    // still owed. Both null until the first run that names one.
    const SelBlock* blk = nullptr;
    const SelSource* grp = nullptr;
    bool any = false;
    // Whether the selection has run through the gap between two registered
    // runs and so owes a separator. Reading the gap rather than "has anything
    // been emitted yet" is what keeps a drag that ends exactly at the start of
    // the next run reaching into it.
    bool sep = false;
    for (int i = 0; i < ctx->texts.len && o.n < cap - 1; i++) {
        const TextHit& t = ctx->texts[i];
        if ((scope >= 0 && t.scope != scope) ||
            !TextHitOwnerMatches(t, owner)) {
            continue;
        }
        int pos = t.docOff;
        int plen = t.text.len;
        int lo = a > pos ? a : pos;
        int hi = b < pos + plen ? b : pos + plen;
        int gap = pos + plen;
        bool spansGap = i + 1 < ctx->texts.len && a <= gap && b > gap;
        // An inline image: no text to slice, so what says the selection has
        // reached it is the runs either side of it (AtomReached above). It
        // carries its whole `![alt](url)` in the group's `pre`, which the
        // block walk below emits; the piece of text after it is empty. Plain
        // skips it: `Paragraph::text` lays the children's text end to end and
        // an image child has none.
        bool atom = t.atom && src && t.src && AtomReached(ctx, i, a, b, owner);
        if ((lo >= hi || !t.text.s) && !atom) {
            sep = sep || spansGap;
            continue;
        }
        Str piece =
            (lo < hi && t.text.s) ? Str(t.text.s + (lo - pos), hi - lo) : Str{};
        // Which run continues which line is the document's shape and holds in
        // both formats — a paragraph is one `InlineState.text` in Rust
        // however it is copied. Only the affixes are Markdown, and only
        // Source emits them.
        const SelSource* s = t.src;
        const SelBlock* want = s ? s->block : nullptr;
        // The same record still open, continued: one mark group split over
        // several word elements, which closes once and not per word.
        if (!(src && s && s == grp && t.join)) {
            if (src && grp) {
                CopyPut(&o, grp->post);
                grp = nullptr;
            }
            if (want != blk) {
                if (src && blk) {
                    CopyPut(&o, blk->post);
                }
                if (sep) {
                    // A block that continues the row before it — a table
                    // cell — is a space in the rendered text, and in the
                    // source is whatever its `pre` says.
                    bool cont = want && want->join;
                    CopyPut(&o,
                            cont ? (src ? Str{} : Str(" ", 1)) : Str("\n", 1));
                    sep = false;
                }
                if (src && want) {
                    CopyPut(&o, want->pre);
                }
                blk = want;
            } else if (!t.join && sep) {
                // A new line inside the same block — or, with no block at
                // all, the run-per-line the copier has always produced.
                CopyPut(&o, Str("\n", 1));
                if (src && blk) {
                    CopyPut(&o, blk->linePre);
                }
                sep = false;
            }
            if (src && s) {
                CopyPut(&o, s->pre);
                grp = s;
            }
        }
        CopyPutLines(&o, piece, (src && blk) ? blk->linePre : Str{});
        any = true;
        sep = spansGap;
    }
    if (src && grp) {
        CopyPut(&o, grp->post);
    }
    if (src && blk) {
        CopyPut(&o, blk->post);
    }
    // A selection that ran past the last run it took text from ends at the
    // line break it reached into.
    if (any && sep) {
        CopyPut(&o, Str("\n", 1));
    }
    out[o.n] = 0;
    return o.n;
}

int CopyTextHitsIn(PaintCtx* ctx, int a, int b, int scope, char* out, int cap,
                   SelectionFormat fmt) {
    return CopyTextHitsFiltered(ctx, a, b, scope, {}, out, cap, fmt);
}

int CopyTextHitsInEntity(PaintCtx* ctx, int a, int b, int scope, EntityId owner,
                         char* out, int cap, SelectionFormat fmt) {
    return CopyTextHitsFiltered(ctx, a, b, scope, owner, out, cap, fmt);
}

// A trap is a property of the container, the way Rust hangs it off the one
// focus handle the dialog tracks, so it reaches every focusable below it. The
// resolved id is written back onto the element: the focus ring paints from it,
// and nothing else has the tree to work it out again.
static void CollectFocus(El* e, Window* win, int trap, Listener increment,
                         Listener decrement, Func0 incrementDirect,
                         Func0 decrementDirect, int depth) {
    if (!e) {
        return;
    }
    if (e->style.trapId) {
        trap = e->style.trapId;
    }
    if (e->accessibilityIncrement.IsValid()) {
        increment = e->accessibilityIncrement;
    }
    if (e->accessibilityDecrement.IsValid()) {
        decrement = e->accessibilityDecrement;
    }
    if (e->accessibilityIncrementDirect.IsValid()) {
        incrementDirect = e->accessibilityIncrementDirect;
    }
    if (e->accessibilityDecrementDirect.IsValid()) {
        decrementDirect = e->accessibilityDecrementDirect;
    }
    // The context comes before this element's own handlers, so a walk out
    // from here finds the handlers first and then the context they sit in,
    // which is the order Rust reads them.
    int first = win->dispatch.len;
    if (e->style.keyContext) {
        DispatchNode n;
        n.context = e->style.keyContext;
        n.depth = depth;
        VecAppend(win->dispatch, n);
    }
    for (ActionSlot* slot = e->actions; slot; slot = slot->next) {
        DispatchNode n;
        n.action = slot->action;
        n.fn = slot->fn;
        n.depth = depth;
        VecAppend(win->dispatch, n);
    }
    if (e->style.focusId) {
        e->style.trapId = trap;
        FocusRect fr;
        fr.id = e->style.focusId;
        fr.trapId = trap;
        fr.tabIndex = e->style.tabIndex;
        fr.tabStop = e->style.tabStop;
        fr.focusOnPress = e->style.focusOnPress;
        // A marker of its own, so the element has a position inside its own
        // subtree whether or not it declared a context or a handler. Without
        // one, an element that declares neither would share an index with the
        // end of the subtree beside it and pick up that sibling's context.
        DispatchNode marker;
        marker.depth = depth;
        fr.dispatchIx = win->dispatch.len;
        VecAppend(win->dispatch, marker);
        fr.bounds = e->Bounds();
        fr.accessibilityIncrement = increment;
        fr.accessibilityDecrement = decrement;
        fr.accessibilityIncrementDirect = incrementDirect;
        fr.accessibilityDecrementDirect = decrementDirect;
        VecAppend(win->focusEls, fr);
    }
    for (El* c = e->first; c; c = c->next) {
        CollectFocus(c, win, trap, increment, decrement, incrementDirect,
                     decrementDirect, depth + 1);
    }
    // The subtree is closed: everything from here down was written between
    // `first` and now, so anything focused in it sits inside this span.
    for (int i = first; i < win->dispatch.len; i++) {
        if (win->dispatch[i].subtreeEnd == 0) {
            win->dispatch[i].subtreeEnd = win->dispatch.len;
        }
    }
}

// --- action dispatch ------------------------------------------------------
//
// Rust walks the focused handle's ancestry twice: once up, gathering the key
// contexts a keystroke is matched against, and once down again, offering the
// action to each `on_action` until one keeps it. The tree here is gone by the
// time a key arrives, so `win->dispatch` is that walk recorded in tree order
// with a depth on every node: the ancestors of a node are the ones before it
// with a strictly smaller depth, taken smallest-so-far first.

// cx.on_action's table. Small and fixed: these are the framework's own
// handlers, not an application's, which hangs its own off its elements.
struct AppAction {
    uint32_t action = 0;
    ActionFn fn = nullptr;
};

static const int kMaxAppActions = 32;
static AppAction gAppActions[kMaxAppActions];
static int gNAppActions = 0;

void AppOnAction(uint32_t action, ActionFn fn) {
    if (!action || !fn) {
        return;
    }
    for (int i = 0; i < gNAppActions; i++) {
        if (gAppActions[i].action == action && gAppActions[i].fn == fn) {
            return;
        }
    }
    if (gNAppActions >= kMaxAppActions) {
        return;
    }
    gAppActions[gNAppActions].action = action;
    gAppActions[gNAppActions].fn = fn;
    gNAppActions++;
}

// inspector::init. Rust registers this from `gpui_component::init(cx)`, which
// an application calls once; there is no such call here, so the framework's
// own bindings go in the first time a keystroke looks for one.
static void ToggleInspectorAction(Window* win, ActionEvent*) {
    WindowToggleInspector(win);
}

#ifdef __APPLE__
static void QuitAction(Window* win, ActionEvent*) {
    if (win && win->app) {
        AppQuitAll(win->app);
    }
}
#endif

static void KeymapDefaults() {
    static uint32_t done = 0;
    if (done == KeymapGeneration()) {
        return;
    }
    done = KeymapGeneration();
    uint32_t toggle = ActionOf(StrL("inspector::ToggleInspector"));
#ifdef __APPLE__
    uint32_t quit = ActionOf(StrL("gpui::Quit"));
    KeyBinding bindings[] = {
        {"cmd-alt-i", toggle, nullptr},
        // ⌘Q is the platform Quit chord. Examples that never call set_menus
        // still need it; a menu row that names its own Quit action keeps
        // that binding too, and the last one for the chord wins.
        {"cmd-q", quit, nullptr},
    };
#else
    KeyBinding bindings[] = {
        {"ctrl-shift-i", toggle, nullptr},
    };
#endif
    KeymapBind(bindings, (int)(sizeof(bindings) / sizeof(bindings[0])));
    AppOnAction(toggle, &ToggleInspectorAction);
#ifdef __APPLE__
    AppOnAction(quit, &QuitAction);
#endif
}

// Where in the dispatch list the focused element sits. Nothing focused is
// just after the root's own nodes — the window itself — so a menu action
// with no focused field (⌘Q, Quit) still reaches the root's on_action.
static int DispatchAnchor(Window* win) {
    if (win->focusId) {
        for (int i = 0; i < win->focusEls.len; i++) {
            if (win->focusEls[i].id == win->focusId) {
                return win->focusEls[i].dispatchIx;
            }
        }
    }
    // CollectFocus closes every subtree, so standing at `len` is past every
    // exclusive end and skipped the root too. Depth 0 is the window's own
    // nodes; stand just after them, the way a focused root would.
    int n = win->dispatch.len;
    int i = 0;
    while (i < n && win->dispatch[i].depth == 0) {
        i++;
    }
    return i;
}

// The reserved action a raw key listener is recorded under. No chord resolves
// to it, so the keymap never reaches these; only WindowDispatchKeyEvent does.
static uint32_t KeyDownAction() {
    return ActionOf(StrL("gpui::KeyDown"));
}

// The same, for the release half. GPUI's `on_key_up` walks the same focus
// path; nothing resolves to this action either.
static uint32_t KeyUpAction() {
    return ActionOf(StrL("gpui::KeyUp"));
}

static bool DispatchKeyChain(Window* win, KeyEvent* ev, uint32_t action) {
    int ix = DispatchAnchor(win);
    for (int i = ix - 1; i >= 0; i--) {
        if (win->dispatch[i].subtreeEnd <= ix ||
            win->dispatch[i].action != action ||
            !win->dispatch[i].fn.IsValid()) {
            continue;
        }
        ev->propagate = true;
        ListenerCall(win->app, win, win->dispatch[i].fn, ev);
        if (!ev->propagate) {
            return true;
        }
    }
    return false;
}

bool WindowDispatchKeyEvent(Window* win, KeyEvent* ev) {
    if (!win || !ev) {
        return false;
    }
    return DispatchKeyChain(win, ev, KeyDownAction());
}

bool WindowDispatchKeyUpEvent(Window* win, KeyEvent* ev) {
    if (!win || !ev) {
        return false;
    }
    return DispatchKeyChain(win, ev, KeyUpAction());
}

uint32_t WindowResolveKeyAction(Window* win, int vk, bool shift, bool ctrl,
                                bool alt, bool platform, bool function,
                                intptr_t* arg, bool* pending) {
    if (arg) {
        *arg = 0;
    }
    if (pending) {
        *pending = false;
    }
    if (!win || !vk) {
        return 0;
    }
    int ix = DispatchAnchor(win);

    // The contexts stacked over the focused element, innermost first; the
    // keymap reads as deep a stack as kMaxContextDepth.
    uint32_t contexts[kMaxContextDepth];
    int nContexts = 0;
    for (int i = ix - 1; i >= 0 && nContexts < kMaxContextDepth; i--) {
        if (win->dispatch[i].subtreeEnd <= ix || !win->dispatch[i].context) {
            continue;
        }
        contexts[nContexts++] = win->dispatch[i].context;
    }

    KeymapDefaults();
    KeyChord chord;
    chord.vk = vk;
    chord.shift = shift;
    chord.ctrl = ctrl;
    chord.alt = alt;
    chord.platform = platform;
    chord.function = function;
    KeyMatch m = KeymapMatch(chord, contexts, nContexts);
    if (m.pending) {
        // Half of a sequence. Rust holds the keystroke on the matcher and
        // dispatches nothing; here that is "eaten", so nothing under the
        // keymap sees it either.
        if (pending) {
            *pending = true;
        }
        return 0;
    }
    if (arg) {
        *arg = m.arg;
    }
    return m.action;
}

bool WindowDispatchKeyAction(Window* win, int vk, bool shift, bool ctrl,
                             bool alt, bool platform, bool function) {
    intptr_t arg = 0;
    bool pending = false;
    uint32_t action = WindowResolveKeyAction(
        win, vk, shift, ctrl, alt, platform, function, &arg, &pending);
    if (pending) {
        return true;
    }
    if (!action) {
        return false;
    }
    return WindowDispatchAction(win, action, arg);
}

// The handler half on its own: the chain over the focused element, then the
// application's. Rust's `window.dispatch_action(Box::new(Cancel), cx)` — a
// button that runs the same thing the escape key does, without a keystroke to
// resolve first.
bool WindowDispatchAction(Window* win, uint32_t action, intptr_t arg) {
    if (!win || !action) {
        return false;
    }
    int ix = DispatchAnchor(win);
    // A handler that propagates lets the search carry on outwards.
    for (int i = ix - 1; i >= 0; i--) {
        if (win->dispatch[i].subtreeEnd <= ix ||
            win->dispatch[i].action != action ||
            !win->dispatch[i].fn.IsValid()) {
            continue;
        }
        ActionEvent ev;
        ev.action = action;
        ev.arg = arg;
        ListenerCall(win->app, win, win->dispatch[i].fn, &ev);
        if (!ev.propagate) {
            return true;
        }
    }
    // Then the application's own, which is where the framework keeps the
    // handlers that belong to no element.
    for (int i = gNAppActions - 1; i >= 0; i--) {
        if (gAppActions[i].action != action) {
            continue;
        }
        ActionEvent ev;
        ev.action = action;
        ev.arg = arg;
        gAppActions[i].fn(win, &ev);
        if (!ev.propagate) {
            return true;
        }
    }
    // Bound but unhandled. Rust leaves the keystroke to whatever is under the
    // action dispatch, and so does this: the caller carries on.
    return false;
}

// GlobalElementId, folded. Rust pushes an ElementId per named element and
// compares the whole stack; a hit rect here is one int, so the stack is a
// rolling hash instead — the same FNV-1a `HashClickId` uses, so a name at the
// root hashes to what `HashClickId(name)` always gave it.
//
// An element with no name of its own inherits its parent's, which is what
// GPUI's `with_id` does by pushing nothing for an element that declared no
// ElementId.
static uint32_t IdFold(uint32_t parent, Str name) {
    uint32_t h = parent ? parent : 2166136261u;
    // The separator is what keeps "ab"+"c" apart from "a"+"bc".
    h ^= (uint8_t)'/';
    h *= 16777619u;
    for (int i = 0; i < name.len; i++) {
        h ^= (uint8_t)name.s[i];
        h *= 16777619u;
    }
    return h;
}

uint32_t IdFoldName(uint32_t parent, Str name) {
    return IdFold(parent, name);
}

static int IdToClick(uint32_t h) {
    int id = (int)(h & 0x3fffffff);
    // Zero is "nothing hovered / nothing focused / no hit".
    return id ? id : 1;
}

static void IdCollect(El* e, uint32_t parent) {
    uint32_t here = parent;
    if (e->id.s && e->id.len > 0) {
        here = IdFold(parent, e->id);
    }
    e->pathId = here;
    if (e->clickFromPath) {
        e->clickId = IdToClick(here);
    }
    // A focus handle is not a hit target: `track_focus` without `.id()` is
    // an element the keyboard can reach and the mouse cannot.
    if (e->style.focusFromPath) {
        e->style.focusId = IdToClick(here);
    }
    if (e->scrollFromPath) {
        e->scrollId = IdToClick(here);
    } else if (e->scrollId == 0 && e->id.s && e->id.len > 0 &&
               (e->style.overflowY == Overflow::Scroll ||
                e->style.overflowX == Overflow::Scroll)) {
        // A named scroll box has to be findable next frame, or a thumb
        // grab sets scrollDragId to 0 and the move handler treats that as
        // "not dragging". PathId fills clickId and leaves scrollId alone;
        // the highlighter's editor was the one that hit it.
        e->scrollId = IdToClick(here);
    }
    for (El* c = e->first; c; c = c->next) {
        IdCollect(c, here);
    }
}

// GPUI_ID_CHECK=1 reports every id two elements in one frame share. Rust
// cannot have this problem — it compares whole id paths, never a hash of one
// — so it is worth being able to ask whether this tree does. Two elements
// deliberately sharing an id (a tooltip and the trigger it stays open over)
// show up here too; the point is the ones that are a surprise.
static bool IdCheckOn() {
    static int on = -1;
    if (on >= 0) {
        return on != 0;
    }
    const char* env = getenv("GPUI_ID_CHECK");
    on = (env && env[0] && env[0] != '0') ? 1 : 0;
    return on != 0;
}

struct IdSeen {
    int id;
    Str name;
};

// A hit rect is found by `clickId` and a focus rect by `style.focusId`, and
// the two are one space rather than two: a press hands the *hit* id to
// WindowSetFocusId, so an element whose two ids disagree cannot be focused by
// pressing it. Both go in.
static void IdCheckCollect(El* e, Vec<IdSeen>* seen) {
    if (e->clickId > 0) {
        IdSeen s = {e->clickId, e->id};
        VecAppend(*seen, s);
    }
    if (e->style.focusId > 0 && e->style.focusId != e->clickId) {
        IdSeen s = {e->style.focusId, e->id};
        VecAppend(*seen, s);
    }
    for (El* c = e->first; c; c = c->next) {
        IdCheckCollect(c, seen);
    }
}

static void IdCheck(El* root) {
    Vec<IdSeen> seen;
    IdCheckCollect(root, &seen);
    int dups = 0;
    for (int i = 0; i < seen.len; i++) {
        for (int j = i + 1; j < seen.len; j++) {
            if (seen[i].id != seen[j].id) {
                continue;
            }
            dups++;
            Str a = seen[i].name.s ? seen[i].name : StrL("(unnamed)");
            Str b = seen[j].name.s ? seen[j].name : StrL("(unnamed)");
            logf("id-check: %d shared by \"%s\" and \"%s\"", seen[i].id, a, b);
        }
    }
    logf("id-check: %d ids, %d shared", seen.len, dups);
    VecReset(seen);
}

void IdsCollect(El* root) {
    if (!root) {
        return;
    }
    IdCollect(root, 0);
    if (IdCheckOn()) {
        IdCheck(root);
    }
}

static SliderState* AccessibilitySlider(El* e) {
    if (!e) {
        return nullptr;
    }
    if (e->slider) {
        return e->slider;
    }
    for (El* child = e->first; child; child = child->next) {
        if (SliderState* slider = AccessibilitySlider(child)) {
            return slider;
        }
    }
    return nullptr;
}

static InputState* AccessibilityInput(El* e) {
    if (!e) {
        return nullptr;
    }
    if (e->input) {
        return e->input;
    }
    for (El* child = e->first; child; child = child->next) {
        if (InputState* input = AccessibilityInput(child)) {
            return input;
        }
    }
    return nullptr;
}

static int AccessibilityVisualFocus(El* e, bool root = true) {
    if (!e || (!root && e->accessibility.role != AccessibilityRole::None)) {
        return 0;
    }
    if (e->style.focusId) {
        return e->style.focusId;
    }
    for (El* child = e->first; child; child = child->next) {
        if (int focus = AccessibilityVisualFocus(child, false)) {
            return focus;
        }
    }
    return 0;
}

static int AccessibilityInputFocus(El* e, InputState* input) {
    if (!e || !input) {
        return 0;
    }
    if (e->input == input && e->style.focusId) {
        return e->style.focusId;
    }
    for (El* child = e->first; child; child = child->next) {
        if (int focus = AccessibilityInputFocus(child, input)) {
            return focus;
        }
    }
    return 0;
}

static bool AccessibilityRoleCanFocus(AccessibilityRole role) {
    switch (role) {
        case AccessibilityRole::Button:
        case AccessibilityRole::CheckBox:
        case AccessibilityRole::ComboBox:
        case AccessibilityRole::DateInput:
        case AccessibilityRole::DateTimeInput:
        case AccessibilityRole::EmailInput:
        case AccessibilityRole::Link:
        case AccessibilityRole::ListBoxOption:
        case AccessibilityRole::MenuItem:
        case AccessibilityRole::MultilineTextInput:
        case AccessibilityRole::PasswordInput:
        case AccessibilityRole::PhoneNumberInput:
        case AccessibilityRole::RadioButton:
        case AccessibilityRole::Slider:
        case AccessibilityRole::SpinButton:
        case AccessibilityRole::Switch:
        case AccessibilityRole::Tab:
        case AccessibilityRole::TextInput:
        case AccessibilityRole::UrlInput:
            return true;
        default:
            return false;
    }
}

// AccessKit derives an accessible name from text descendants when a node has
// no explicit aria label. Keep the same useful default, but stop at another
// semantic node: its text names that child, not its parent. All text runs are
// joined in document order rather than silently taking only the first one.
static int AccessibilityTextSize(El* e, int* pieces, bool root = true) {
    if (!e || !pieces || *pieces < 0) {
        return 0;
    }
    if (!root && e->accessibility.role != AccessibilityRole::None) {
        return 0;
    }
    if (e->text.s && e->text.len > 0) {
        (*pieces)++;
        return e->text.len;
    }
    int size = 0;
    for (El* child = e->first; child; child = child->next) {
        int n = AccessibilityTextSize(child, pieces, false);
        if (n > 0) {
            if (size > INT_MAX - n) {
                *pieces = -1;
                return 0;
            }
            size += n;
        }
    }
    return size;
}

static void AccessibilityTextWrite(El* e, char* out, int* at, bool* wrote,
                                   bool root = true) {
    if (!e || (!root && e->accessibility.role != AccessibilityRole::None)) {
        return;
    }
    if (e->text.s && e->text.len > 0) {
        if (*wrote) {
            out[(*at)++] = ' ';
        }
        memcpy(out + *at, e->text.s, (size_t)e->text.len);
        *at += e->text.len;
        *wrote = true;
        return;
    }
    for (El* child = e->first; child; child = child->next) {
        AccessibilityTextWrite(child, out, at, wrote, false);
    }
}

static Str AccessibilityText(El* e) {
    int pieces = 0;
    int textSize = AccessibilityTextSize(e, &pieces);
    if (!e || !e->arena || textSize <= 0 || pieces <= 0 ||
        textSize > INT_MAX - (pieces - 1)) {
        return {};
    }
    int size = textSize + pieces - 1;
    char* out = (char*)Alloc(e->arena, size);
    if (!out) {
        return {};
    }
    int at = 0;
    bool wrote = false;
    AccessibilityTextWrite(e, out, &at, &wrote);
    return Str(out, at);
}

static bool AccessibilityNameFromContents(AccessibilityRole role) {
    switch (role) {
        case AccessibilityRole::ComboBox:
        case AccessibilityRole::DateInput:
        case AccessibilityRole::DateTimeInput:
        case AccessibilityRole::EmailInput:
        case AccessibilityRole::MultilineTextInput:
        case AccessibilityRole::PasswordInput:
        case AccessibilityRole::PhoneNumberInput:
        case AccessibilityRole::ProgressIndicator:
        case AccessibilityRole::Slider:
        case AccessibilityRole::SpinButton:
        case AccessibilityRole::TextInput:
        case AccessibilityRole::UrlInput:
            return false;
        default:
            return true;
    }
}

static void AccessibilityCollectNode(El* e, Vec<AccessibilityNode>* out,
                                     int semanticParent, uint32_t* nextId) {
    if (!e || !out || !nextId) {
        return;
    }
    int childParent = semanticParent;
    if (e->accessibility.role != AccessibilityRole::None) {
        AccessibilityNode node;
        node.id = (*nextId)++;
        node.parent = semanticParent;
        node.bounds = e->Bounds();
        node.info = e->accessibility;
        if (!node.info.label.s &&
            AccessibilityNameFromContents(node.info.role)) {
            node.info.label = AccessibilityText(e);
        }
        node.clickId = e->clickId;
        node.focusId = e->style.focusId;
        node.onClick = e->onClick;
        node.listener = e->listener;
        node.accessibilityDefault = e->accessibilityDefault;
        node.accessibilityIncrement = e->accessibilityIncrement;
        node.accessibilityDecrement = e->accessibilityDecrement;
        node.accessibilityIncrementDirect = e->accessibilityIncrementDirect;
        node.accessibilityDecrementDirect = e->accessibilityDecrementDirect;
        node.clickAction = e->clickAction;
        node.clickActionArg = e->clickActionArg;
        node.slider = e->slider;
        node.input = e->input;
        if (!node.slider && node.info.role == AccessibilityRole::Slider) {
            // The semantic slider wraps its visual track; the track owns the
            // state that mouse dragging needs. AccessKit puts the actions on
            // the wrapper, so find that same state below it.
            node.slider = AccessibilitySlider(e);
        }
        if (!node.input) {
            node.input = AccessibilityInput(e);
        }
        if (!node.focusId && AccessibilityRoleCanFocus(node.info.role)) {
            node.focusId =
                node.info.role == AccessibilityRole::SpinButton && node.input
                    ? AccessibilityInputFocus(e, node.input)
                    : AccessibilityVisualFocus(e);
        }
        if (!node.info.disabled &&
            (node.onClick.IsValid() || node.listener.IsValid() ||
             node.accessibilityDefault.IsValid() || node.clickAction)) {
            node.actions |= AccessibilityActionDefault;
        }
        if (!node.info.disabled &&
            (node.slider || node.accessibilityIncrement.IsValid() ||
             node.accessibilityIncrementDirect.IsValid())) {
            node.actions |= AccessibilityActionIncrement;
        }
        if (!node.info.disabled &&
            (node.slider || node.accessibilityDecrement.IsValid() ||
             node.accessibilityDecrementDirect.IsValid())) {
            node.actions |= AccessibilityActionDecrement;
        }
        if (!node.info.disabled && node.focusId) {
            node.actions |= AccessibilityActionFocus;
        }
        if (!node.info.disabled && node.input && !node.input->disabled &&
            !node.input->readonly) {
            node.actions |= AccessibilityActionSetValue;
        }
        VecAppend(*out, node);
        childParent = out->len - 1;
    }
    for (El* child = e->first; child; child = child->next) {
        AccessibilityCollectNode(child, out, childParent, nextId);
    }
}

void AccessibilityCollect(El* root, Vec<AccessibilityNode>* out) {
    if (!out) {
        return;
    }
    VecClear(*out);
    uint32_t nextId = 1;
    AccessibilityCollectNode(root, out, -1, &nextId);
}

void FocusCollect(Window* win, El* root) {
    VecClear(win->focusEls);
    VecClear(win->dispatch);
    CollectFocus(root, win, 0, {}, {}, {}, {}, 0);
    // The traversal order is the tab index first and the paint order within
    // it, so the sort has to be a stable one: an insertion sort over a list
    // this size, where almost every element is already index zero and nothing
    // moves at all.
    for (int i = 1; i < win->focusEls.len; i++) {
        FocusRect fr = win->focusEls[i];
        int j = i - 1;
        while (j >= 0 && win->focusEls[j].tabIndex > fr.tabIndex) {
            win->focusEls[j + 1] = win->focusEls[j];
            j--;
        }
        win->focusEls[j + 1] = fr;
    }
}

void WindowSetFocusId(Window* win, int id) {
    if (!win || win->focusId == id) {
        return;
    }
    win->focusId = id;
    // focus_generation: what a pending keystroke is stamped with, so the move
    // is what tells it the element under it changed.
    win->focusGen++;
    PlatAccessibilityFocusChanged(win, id);
}

// cx.focus_handle(). GPUI's slotmap hands out a refcounted key; a counter is
// enough here, since nothing is ever given back. Downwards from -1000: hashed
// element ids are positive and the window chrome is -1..-4, so a handle can
// never be mistaken for either.
static int gNextFocusHandle = -1000;

FocusHandle FocusHandleNew(App*) {
    FocusHandle h;
    h.id = gNextFocusHandle--;
    return h;
}
FocusHandle FocusHandleNew(Ctx* cx) {
    return FocusHandleNew(cx ? cx->app : nullptr);
}
bool FocusHandleIsFocused(const Window* win, FocusHandle h) {
    return win && h.IsValid() && win->focusId == h.id;
}
bool FocusHandleContainsFocused(const Window* win, FocusHandle h) {
    return h.IsValid() && WindowFocusWithin(win, h.id);
}
void FocusHandleFocus(Window* win, FocusHandle h) {
    if (win && h.IsValid()) {
        WindowSetFocusId(win, h.id);
    }
}
FocusHandle WindowFocused(const Window* win) {
    FocusHandle h;
    h.id = win ? win->focusId : 0;
    return h;
}
bool FocusHandleRestore(Window* win, FocusHandle h) {
    return h.IsValid() && WindowRestoreFocus(win, h.id);
}

int WindowFocusedId(const Window* win) {
    return win ? win->focusId : 0;
}

bool WindowFocusWithin(const Window* win, int id) {
    if (!win || !id) {
        return false;
    }
    if (win->focusId == id) {
        return true;
    }
    for (int i = 0; i < win->focusEls.len; i++) {
        if (win->focusEls[i].id == win->focusId) {
            return win->focusEls[i].trapId == id;
        }
    }
    return false;
}

bool WindowRestoreFocus(Window* win, int id) {
    if (!win || !id) {
        return false;
    }
    for (int i = 0; i < win->focusEls.len; i++) {
        if (win->focusEls[i].id == id) {
            WindowSetFocusId(win, id);
            return true;
        }
    }
    return false;
}

int FocusNext(Window* win, int trapId, bool backward) {
    int n = win->focusEls.len;
    if (n == 0) {
        return 0;
    }
    int cur = -1;
    for (int i = 0; i < n; i++) {
        if (win->focusEls[i].id == win->focusId) {
            cur = i;
            break;
        }
    }
    int step = backward ? -1 : 1;
    int i = cur;
    for (int k = 0; k < n; k++) {
        i = (i + step + n) % n;
        if (!win->focusEls[i].tabStop) {
            // Focusable, but not somewhere Tab stops.
            continue;
        }
        if (trapId && win->focusEls[i].trapId != trapId) {
            continue;
        }
        if (!trapId && win->focusEls[i].trapId) {
            // stay out of traps unless already inside
            if (cur < 0 || win->focusEls[cur].trapId == 0) {
                continue;
            }
            if (win->focusEls[i].trapId != win->focusEls[cur].trapId) {
                continue;
            }
        }
        WindowSetFocusId(win, win->focusEls[i].id);
        return win->focusId;
    }
    return win->focusId;
}
} // namespace gpui
