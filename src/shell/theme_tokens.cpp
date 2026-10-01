#include "shell/theme_tokens.h"

#include "shell/scope.h"
#include "base/theme.h"

namespace gpui::shell {

struct ShellThemeCache {
    ColorTokens colors = {};
    RadiusTokens radius = {};
    SpacingTokens spacing = {};
    TypographyTokens typography = {};
    BaseThemeAppearance appearance = BaseThemeAppearance::Light;
    // Bumped only when the palette above changes, so a script's theme cache and
    // a ScriptView's snapshot both know when they are stale with one compare.
    uint32_t revision = 0;
    bool valid = false;
};

static thread_local ShellThemeCache gThemeCache;

static void SyncCurrentScope() {
    ScopeHostContext host = ScopeCurrentHost();
    if (host.IsSet()) ThemeTokensSync(host.GetApp());
}

static const char kColorNames[] =
    "background\0foreground\0surface\0surface_foreground\0primary\0"
    "primary_foreground\0secondary\0secondary_foreground\0muted\0"
    "muted_foreground\0accent\0accent_foreground\0destructive\0"
    "destructive_foreground\0border\0input\0ring\0";
static const char kSpacingNames[] = "xxs\0xs\0sm\0md\0lg\0xl\0xxl\0";
static const char kRadiusNames[] = "none\0sm\0md\0lg\0xl\0full\0";

// Whether two palettes would produce the same snapshot. The four token blocks
// are plain numbers except for typography's two font families, which are
// borrowed strings and are compared by what they say.
static bool ThemeKeyEqual(const ShellThemeCache& cache,
                          const SemanticThemeTokens& tokens,
                          BaseThemeAppearance appearance) {
    if (!cache.valid || cache.appearance != appearance) return false;
    if (memcmp(&cache.colors, &tokens.colors, sizeof(ColorTokens)) != 0)
        return false;
    if (memcmp(&cache.radius, &tokens.radius, sizeof(RadiusTokens)) != 0)
        return false;
    if (memcmp(&cache.spacing, &tokens.spacing, sizeof(SpacingTokens)) != 0)
        return false;
    const TypographyTokens& a = cache.typography;
    const TypographyTokens& b = tokens.typography;
    if (!StrEq(a.sans, b.sans) || !StrEq(a.mono, b.mono)) return false;
    const TextStyleToken* left[] = {&a.xs, &a.sm, &a.md,
                                    &a.lg, &a.xl, &a.monoMd};
    const TextStyleToken* right[] = {&b.xs, &b.sm, &b.md,
                                     &b.lg, &b.xl, &b.monoMd};
    for (int i = 0; i < 6; i++) {
        if (memcmp(left[i], right[i], sizeof(TextStyleToken)) != 0)
            return false;
    }
    return true;
}

uint32_t ThemeTokensSync(const App* app) {
    if (!app) {
        return gThemeCache.revision;
    }
    // Theme::global: gpui-base's active theme, or its default when none is
    // installed. A component library that styles the window projects its
    // palette onto this one; the runtime reads no library's theme itself.
    const BaseTheme* base = BaseThemeGlobal(app);
    SemanticThemeTokens tokens = base ? base->tokens : SemanticThemeTokens{};
    BaseThemeAppearance appearance =
        base ? base->appearance : BaseThemeAppearance::Light;
    if (ThemeKeyEqual(gThemeCache, tokens, appearance)) {
        return gThemeCache.revision;
    }
    gThemeCache.colors = tokens.colors;
    gThemeCache.radius = tokens.radius;
    gThemeCache.spacing = tokens.spacing;
    gThemeCache.typography = tokens.typography;
    gThemeCache.appearance = appearance;
    gThemeCache.valid = true;
    gThemeCache.revision++;
    return gThemeCache.revision;
}

uint32_t ThemeTokensRevision() {
    return gThemeCache.revision;
}

static bool ColorOf(const ColorTokens& colors, Str name, Rgba* out) {
    if (StrEq(name, StrL("background")))
        *out = colors.background;
    else if (StrEq(name, StrL("foreground")))
        *out = colors.foreground;
    else if (StrEq(name, StrL("surface")))
        *out = colors.surface;
    else if (StrEq(name, StrL("surface_foreground")))
        *out = colors.surfaceForeground;
    else if (StrEq(name, StrL("primary")))
        *out = colors.primary;
    else if (StrEq(name, StrL("primary_foreground")))
        *out = colors.primaryForeground;
    else if (StrEq(name, StrL("secondary")))
        *out = colors.secondary;
    else if (StrEq(name, StrL("secondary_foreground")))
        *out = colors.secondaryForeground;
    else if (StrEq(name, StrL("muted")))
        *out = colors.muted;
    else if (StrEq(name, StrL("muted_foreground")))
        *out = colors.mutedForeground;
    else if (StrEq(name, StrL("accent")))
        *out = colors.accent;
    else if (StrEq(name, StrL("accent_foreground")))
        *out = colors.accentForeground;
    else if (StrEq(name, StrL("destructive")))
        *out = colors.destructive;
    else if (StrEq(name, StrL("destructive_foreground")))
        *out = colors.destructiveForeground;
    else if (StrEq(name, StrL("border")))
        *out = colors.border;
    else if (StrEq(name, StrL("input")))
        *out = colors.input;
    else if (StrEq(name, StrL("ring")))
        *out = colors.ring;
    else
        return false;
    return true;
}

bool ThemeTokenColor(Str name, Hsla* out) {
    SyncCurrentScope();
    if (!out || !gThemeCache.valid) {
        return false;
    }
    Rgba value = {};
    if (!ColorOf(gThemeCache.colors, name, &value)) {
        return false;
    }
    *out = HslaFromRgba(value);
    return true;
}

bool ThemeTokenSpacing(Str name, float* out) {
    SyncCurrentScope();
    if (!out || !gThemeCache.valid) {
        return false;
    }
    if (StrEq(name, StrL("xxs")))
        *out = gThemeCache.spacing.xxs;
    else if (StrEq(name, StrL("xs")))
        *out = gThemeCache.spacing.xs;
    else if (StrEq(name, StrL("sm")))
        *out = gThemeCache.spacing.sm;
    else if (StrEq(name, StrL("md")))
        *out = gThemeCache.spacing.md;
    else if (StrEq(name, StrL("lg")))
        *out = gThemeCache.spacing.lg;
    else if (StrEq(name, StrL("xl")))
        *out = gThemeCache.spacing.xl;
    else if (StrEq(name, StrL("xxl")))
        *out = gThemeCache.spacing.xxl;
    else
        return false;
    return true;
}

bool ThemeTokenRadius(Str name, float* out) {
    SyncCurrentScope();
    if (!out || !gThemeCache.valid) {
        return false;
    }
    if (StrEq(name, StrL("none")))
        *out = gThemeCache.radius.none;
    else if (StrEq(name, StrL("sm")))
        *out = gThemeCache.radius.sm;
    else if (StrEq(name, StrL("md")))
        *out = gThemeCache.radius.md;
    else if (StrEq(name, StrL("lg")))
        *out = gThemeCache.radius.lg;
    else if (StrEq(name, StrL("xl")))
        *out = gThemeCache.radius.xl;
    else if (StrEq(name, StrL("full")))
        *out = gThemeCache.radius.full;
    else
        return false;
    return true;
}

bool ThemeTypographyTokens(TypographyTokens* out) {
    SyncCurrentScope();
    if (!out || !gThemeCache.valid) {
        return false;
    }
    *out = gThemeCache.typography;
    return true;
}

SeqStrings ThemeColorTokenNames() {
    return kColorNames;
}
SeqStrings ThemeSpacingTokenNames() {
    return kSpacingNames;
}
SeqStrings ThemeRadiusTokenNames() {
    return kRadiusNames;
}

} // namespace gpui::shell
