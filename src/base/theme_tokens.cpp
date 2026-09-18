#include "base/theme_tokens.h"

namespace gpui {

// The selection wash both palettes carry: the same colour the themed layer's
// `selection.background` resolves to, at the alpha Rust writes.
static Rgba TokenSelection(uint32_t rgb) {
    return RgbaOpacity(RgbaHex(rgb), 0.3f);
}

ColorTokens::ColorTokens() {
    *this = Light();
}

ColorTokens ColorTokens::Light() {
    ColorTokens c{Empty{}};
    c.background = RgbaHsla(0.f, 0.f, 1.f, 1.f);
    c.foreground = RgbaHsla(0.f, 0.f, 0.039f, 1.f);
    c.surface = RgbaHsla(0.f, 0.f, 1.f, 1.f);
    c.surfaceForeground = RgbaHsla(0.f, 0.f, 0.039f, 1.f);
    c.primary = RgbaHsla(0.f, 0.f, 0.09f, 1.f);
    c.primaryForeground = RgbaHsla(0.f, 0.f, 0.98f, 1.f);
    c.secondary = RgbaHsla(0.f, 0.f, 0.898f, 1.f);
    c.secondaryForeground = RgbaHsla(0.f, 0.f, 0.09f, 1.f);
    c.muted = RgbaHsla(0.f, 0.f, 0.961f, 1.f);
    c.mutedForeground = RgbaHsla(0.f, 0.f, 0.451f, 1.f);
    c.accent = RgbaHsla(0.f, 0.f, 0.961f, 1.f);
    c.accentForeground = RgbaHsla(0.f, 0.f, 0.09f, 1.f);
    c.destructive = RgbaHsla(0.f, 0.842f, 0.602f, 1.f);
    c.destructiveForeground = RgbaHsla(0.f, 0.f, 0.98f, 1.f);
    c.border = RgbaHsla(0.f, 0.f, 0.898f, 1.f);
    c.input = RgbaHsla(0.f, 0.f, 0.898f, 1.f);
    c.ring = RgbaHsla(0.f, 0.f, 0.639f, 1.f);
    c.selection = TokenSelection(0x55a0fc);
    return c;
}

ColorTokens ColorTokens::Dark() {
    ColorTokens c{Empty{}};
    c.background = RgbaHsla(0.f, 0.f, 0.039f, 1.f);
    c.foreground = RgbaHsla(0.f, 0.f, 0.98f, 1.f);
    c.surface = RgbaHsla(0.f, 0.f, 0.039f, 1.f);
    c.surfaceForeground = RgbaHsla(0.f, 0.f, 0.98f, 1.f);
    c.primary = RgbaHsla(0.f, 0.f, 0.98f, 1.f);
    c.primaryForeground = RgbaHsla(0.f, 0.f, 0.09f, 1.f);
    c.secondary = RgbaHsla(0.f, 0.f, 0.149f, 1.f);
    c.secondaryForeground = RgbaHsla(0.f, 0.f, 0.98f, 1.f);
    c.muted = RgbaHsla(0.f, 0.f, 0.149f, 1.f);
    c.mutedForeground = RgbaHsla(0.f, 0.f, 0.639f, 1.f);
    c.accent = RgbaHsla(0.f, 0.f, 0.149f, 1.f);
    c.accentForeground = RgbaHsla(0.f, 0.f, 0.98f, 1.f);
    c.destructive = RgbaHsla(0.f, 0.906f, 0.708f, 1.f);
    c.destructiveForeground = RgbaHsla(0.f, 0.722f, 0.506f, 1.f);
    c.border = RgbaHsla(0.f, 0.f, 0.149f, 1.f);
    c.input = RgbaHsla(0.f, 0.f, 47.f / 255.f, 1.f);
    c.ring = RgbaHsla(0.f, 0.f, 0.451f, 1.f);
    c.selection = TokenSelection(0x1d4ed8);
    return c;
}

static bool TokenColorEq(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

bool operator==(const ColorTokens& a, const ColorTokens& b) {
    return TokenColorEq(a.background, b.background) &&
           TokenColorEq(a.foreground, b.foreground) &&
           TokenColorEq(a.surface, b.surface) &&
           TokenColorEq(a.surfaceForeground, b.surfaceForeground) &&
           TokenColorEq(a.primary, b.primary) &&
           TokenColorEq(a.primaryForeground, b.primaryForeground) &&
           TokenColorEq(a.secondary, b.secondary) &&
           TokenColorEq(a.secondaryForeground, b.secondaryForeground) &&
           TokenColorEq(a.muted, b.muted) &&
           TokenColorEq(a.mutedForeground, b.mutedForeground) &&
           TokenColorEq(a.accent, b.accent) &&
           TokenColorEq(a.accentForeground, b.accentForeground) &&
           TokenColorEq(a.destructive, b.destructive) &&
           TokenColorEq(a.destructiveForeground, b.destructiveForeground) &&
           TokenColorEq(a.border, b.border) && TokenColorEq(a.input, b.input) &&
           TokenColorEq(a.ring, b.ring) &&
           TokenColorEq(a.selection, b.selection);
}

ShadowTokens ShadowTokens::Elevations(Rgba color) {
    ShadowTokens out;
    VecAppend(out.sm, BoxShadow{0, 1, 2, 0, color, false});
    VecAppend(out.md, BoxShadow{0, 4, 8, -2, color, false});
    VecAppend(out.lg, BoxShadow{0, 12, 24, -4, color, false});
    return out;
}

SemanticShadowTokens SemanticShadowElevations(Rgba color) {
    return ShadowTokens::Elevations(color);
}

const BoxShadow* ShadowFirst(const Vec<BoxShadow>& level) {
    return len(level) > 0 ? &level[0] : nullptr;
}

BoxShadow* ShadowFirst(Vec<BoxShadow>& level) {
    return len(level) > 0 ? &level[0] : nullptr;
}

} // namespace gpui
