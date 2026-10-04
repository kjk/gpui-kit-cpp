#ifndef GPUI_BASE_THEME_TOKENS_H_
#define GPUI_BASE_THEME_TOKENS_H_
/* Semantic design tokens — crates/base/src/theme_tokens.rs
 *
 * Visual roles and scales, named without component vocabulary. The themed
 * crate projects its palette into these values; Base does not depend on that
 * palette or its gradient-bearing ThemeTokens.
 */

#include "gpui/gpui.h"

namespace gpui {

struct ColorTokens {
    Rgba background = {};
    Rgba foreground = {};
    Rgba surface = {};
    Rgba surfaceForeground = {};
    Rgba primary = {};
    Rgba primaryForeground = {};
    Rgba secondary = {};
    Rgba secondaryForeground = {};
    Rgba muted = {};
    Rgba mutedForeground = {};
    Rgba accent = {};
    Rgba accentForeground = {};
    Rgba destructive = {};
    Rgba destructiveForeground = {};
    Rgba border = {};
    Rgba input = {};
    Rgba ring = {};
    // The background painted behind selected text. Selection quads go under
    // the glyphs, so this is a translucent wash rather than a solid fill.
    Rgba selection = {};

    // Every field zeroed is transparent on transparent, which is what a Base
    // application that never installs a palette used to render as. The
    // default is the light palette instead — `impl Default for ColorTokens`.
    ColorTokens();
    // The two palettes, aligned with gpui-kit's Default Light and
    // Default Dark themes.
    static ColorTokens Light();
    static ColorTokens Dark();

  private:
    // The zeroed state the two palettes are filled in from; the public
    // default constructor answers Light() and would otherwise recurse.
    struct Empty {};
    explicit ColorTokens(Empty) {}
};

bool operator==(const ColorTokens& a, const ColorTokens& b);
inline bool operator!=(const ColorTokens& a, const ColorTokens& b) {
    return !(a == b);
}

struct RadiusTokens {
    float none = 0;
    float sm = 3;
    float md = 6;
    float lg = 8;
    float xl = 12;
    float full = 9999;
};

struct SpacingTokens {
    float xxs = 2;
    float xs = 4;
    float sm = 8;
    float md = 12;
    float lg = 16;
    float xl = 24;
    float xxl = 32;
};

struct TextStyleToken {
    float size = 16;
    float lineHeight = 24;
    FontWeight weight = FontWeight::Normal;
};

struct TypographyTokens {
    Str sans = Str(".SystemUIFont");
    Str mono = Str(PlatMonoFontName());
    TextStyleToken xs = {12, 16, FontWeight::Normal};
    TextStyleToken sm = {14, 20, FontWeight::Normal};
    TextStyleToken md = {16, 24, FontWeight::Normal};
    TextStyleToken lg = {18, 28, FontWeight::Normal};
    TextStyleToken xl = {20, 28, FontWeight::Normal};
    TextStyleToken monoMd = {13, 20, FontWeight::Normal};
};

// Base calls GPUI's box-shadow value a semantic shadow when it appears in a
// theme token. It is the same source value, not a second representation.
using SemanticShadow = BoxShadow;

struct ShadowTokens {
    Vec<BoxShadow> sm;
    Vec<BoxShadow> md;
    Vec<BoxShadow> lg;

    ShadowTokens() = default;
    static ShadowTokens Elevations(Rgba color);
};

const float kMonoFontSize = 13.f;

struct SemanticThemeTokens {
    ColorTokens colors = {};
    RadiusTokens radius = {};
    SpacingTokens spacing = {};
    TypographyTokens typography = {};
    ShadowTokens shadow;

    SemanticThemeTokens() = default;
};

// Compatibility spellings retained for the component-theme conversion and
// callers written before the exact source names became canonical.
using SemanticColorTokens = ColorTokens;
using SemanticRadiusTokens = RadiusTokens;
using SemanticSpacingTokens = SpacingTokens;
using SemanticTextStyle = TextStyleToken;
using SemanticTypographyTokens = TypographyTokens;
using SemanticShadowTokens = ShadowTokens;

SemanticShadowTokens SemanticShadowElevations(Rgba color);
const BoxShadow* ShadowFirst(const Vec<BoxShadow>& level);
BoxShadow* ShadowFirst(Vec<BoxShadow>& level);

} // namespace gpui
#endif // GPUI_BASE_THEME_TOKENS_H_
