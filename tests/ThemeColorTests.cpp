/* The palette, against crates/ui/src/theme/default-theme.json resolved through
 * default-colors.json.
 *
 * Only a handful of the sixty tokens are pinned here, and they are the ones a
 * reading of the two files by hand got wrong: a semantic foreground in a dark
 * theme is the saturated colour rather than the near-black the light theme
 * puts on the same surface, which is easy to assume the other way round. The
 * anchors either side of them are there so a wholesale palette change cannot
 * pass. */

#include "Test.h"

static bool IsA(Rgba c, uint32_t hex, uint8_t a) {
    return c.r == ((hex >> 16) & 0xff) && c.g == ((hex >> 8) & 0xff) &&
           c.b == (hex & 0xff) && c.a == a;
}

static bool Is(Rgba c, uint32_t hex) {
    return IsA(c, hex, 255);
}

static bool SameColor(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void TheLightPaletteIsDefaultLight() {
    const Theme& t = ThemeLight();
    utassert(Is(t.background, 0xffffff));
    utassert(Is(t.foreground, 0x0a0a0a));
    utassert(Is(t.border, 0xe5e5e5));
    utassert(Is(t.mutedFg, 0x737373));
    utassert(Is(t.primary, 0x171717));
    // The four semantic pairs. A light theme's is the saturated background
    // with a near-white foreground on it.
    utassert(Is(t.danger, 0xef4444) && Is(t.dangerFg, 0xfafafa));
    utassert(Is(t.info, 0x06b6d4) && Is(t.infoFg, 0xfafafa));
    utassert(Is(t.success, 0x22c55e) && Is(t.successFg, 0xfafafa));
    utassert(Is(t.warning, 0xeab308) && Is(t.warningFg, 0xfafafa));
    utassert(Is(t.skeleton, 0xf5f5f5));
    // selection.background is a colour of its own, not `accent` faded — which
    // on a white field is a tint nobody can see. apply_config caps whatever a
    // file writes there at 30%, since it is painted over a row of text: 0x4c,
    // because 0.3 truncates to 76 the way every float→byte here does.
    utassert(IsA(t.selection, 0x55a0fc, 0x4c));
}

static void TheDarkPaletteIsDefaultDark() {
    const Theme& t = ThemeDark();
    utassert(Is(t.background, 0x0a0a0a));
    utassert(Is(t.foreground, 0xfafafa));
    utassert(Is(t.border, 0x262626));
    utassert(Is(t.primary, 0xfafafa));
    // And a dark theme's foreground is the 600 of the same hue, not the
    // near-black the light theme would put there.
    utassert(Is(t.danger, 0xf87171) && Is(t.dangerFg, 0xdc2626));
    utassert(Is(t.info, 0x22d3ee) && Is(t.infoFg, 0x0891b2));
    utassert(Is(t.success, 0x4ade80) && Is(t.successFg, 0x16a34a));
    utassert(Is(t.warning, 0xfacc15) && Is(t.warningFg, 0xca8a04));
    utassert(Is(t.skeleton, 0x171717));
    utassert(IsA(t.selection, 0x1d4ed8, 0x4c));
}

// theme_tokens.rs: the palette read as roles rather than as component names,
// and the same set written back onto one.
static void TheSemanticTokensAreTheRolesOfThePalette() {
    const Theme& t = ThemeLight();
    SemanticThemeTokens tk = ThemeSemanticTokens(t);
    utassert(Is(tk.colors.background, 0xffffff));
    utassert(Is(tk.colors.foreground, 0x0a0a0a));
    // `surface` is the popover pair, which both default themes give the
    // window's own colours.
    utassert(Is(tk.colors.surface, 0xffffff));
    utassert(Is(tk.colors.surfaceForeground, 0x0a0a0a));
    utassert(Is(tk.colors.destructive,
                t.danger.r << 16 | t.danger.g << 8 | t.danger.b));
    // radius_tokens: sm is half the radius, xl twice it, full a number no box
    // can reach.
    utassert(tk.radius.md == t.radius);
    utassert(tk.radius.sm == t.radius / 2.f);
    utassert(tk.radius.xl == t.radius * 2.f);
    utassert(tk.radius.lg == t.radiusLg);
    utassert(tk.radius.full == 9999.f);
    // The scales that are defaults and nothing else.
    utassert(tk.spacing.md == 12.f && tk.spacing.xxl == 32.f);
    utassert(tk.typography.sm.size == 14.f && tk.typography.sm
                                                      .lineHeight == 20.f);
    utassert(tk.typography.monoMd.size == kMonoFontSize);
    // ShadowTokens::elevations, at 18% black.
    utassert(len(tk.shadow.sm) == 1 && len(tk.shadow.md) == 1 &&
             len(tk.shadow.lg) == 1);
    utassert(tk.shadow.md[0].y == 4.f && tk.shadow.md[0].blur == 8.f &&
             tk.shadow.md[0].spread == -2.f);
    utassert(tk.shadow.lg[0].blur == 24.f);
}

static void SourceTokenDefaultsKeepTypographyAndShadowStructure() {
    SemanticThemeTokens tk;
    utassert(tk.radius.none == 0.f && tk.radius.sm == 3.f &&
             tk.radius.md == 6.f && tk.radius.full == 9999.f);
    utassert(tk.spacing.xxs == 2.f && tk.spacing.md == 12.f &&
             tk.spacing.xxl == 32.f);
    utassert(base::StrEq(tk.typography.sans, StrL(".SystemUIFont")));
#if GPUI_OS_WINDOWS
    utassert(base::StrEq(tk.typography.mono, StrL("Consolas")));
#elif GPUI_OS_MAC
    utassert(base::StrEq(tk.typography.mono, StrL("Menlo")));
#else
    utassert(base::StrEq(tk.typography.mono, StrL("DejaVu Sans Mono")));
#endif
    utassert(tk.typography.xs.size == 12.f && tk.typography.xs
                                                      .lineHeight == 16.f);
    utassert(tk.typography.monoMd.size == 13.f &&
             tk.typography.monoMd.weight == FontWeight::Normal);
    utassert(len(tk.shadow.sm) == 0 && len(tk.shadow.md) == 0 &&
             len(tk.shadow.lg) == 0);

    ShadowTokens elevated = ShadowTokens::Elevations(Rgba8(1, 2, 3, 4));
    utassert(len(elevated.sm) == 1 && len(elevated.md) == 1 &&
             len(elevated.lg) == 1);
    VecAppend(elevated.md, BoxShadow{1, 2, 3, 4, Rgb(5, 6, 7), true});
    utassert(len(elevated.md) == 2);
    utassert(elevated.md[1].inset && elevated.md[1].spread == 4.f);
}

// apply_semantic_tokens: the subset the legacy palette can hold comes back,
// tokens included, and the rest is dropped on the floor.
static void ASemanticSetAppliesBackToAPalette() {
    Theme t = ThemeLight();
    SemanticThemeTokens tk = ThemeSemanticTokens(t);
    tk.colors.background = Rgb(0x11, 0x22, 0x33);
    tk.colors.surface = Rgb(0x44, 0x55, 0x66);
    tk.colors.destructive = Rgb(0x77, 0x00, 0x00);
    tk.radius.md = 10;
    tk.radius.lg = 14;
    ThemeApplySemanticTokens(&t, tk);
    utassert(Is(t.background, 0x112233));
    utassert(Is(t.popover, 0x445566));
    utassert(Is(t.danger, 0x770000));
    utassert(t.radius == 10.f && t.radiusLg == 14.f);
    // The renderable tokens follow the flat colours, so a gradient left over
    // from the palette this was applied to cannot outlive it.
    utassert(Is(t.tokens.popover.color, 0x445566));
    utassert(Is(t.tokens.background.color, 0x112233));
}

// SemanticThemeConfig: a document in the token vocabulary, resolved over a
// set. Every field is optional and what it leaves out stays as it was.
static void ASemanticConfigOnlyChangesWhatItNames() {
    Arena* a = ArenaNew();
    SemanticThemeTokens tk = ThemeSemanticTokens(ThemeLight());
    float wasLg = tk.radius.lg;
    Str doc = StrL(
        "{\"tokens\":{\"colors\":{\"primary\":\"#123456\","
        "\"ring\":\"blue-500\"},\"radius\":{\"md\":10},"
        "\"spacing\":{\"md\":20},"
        "\"typography\":{\"mono\":\"Cascadia\","
        "\"sm\":{\"size\":15,\"weight\":700}},"
        "\"shadow\":{\"md\":{\"y\":6,\"color\":\"#00000033\"}}}}");
    JsonValue* json = JsonParse(a, doc);
    utassert(json != nullptr);
    utassert(ThemeSemanticConfigApply(json, &tk));
    utassert(Is(tk.colors.primary, 0x123456));
    utassert(Is(tk.colors.ring, 0x3b82f6));
    utassert(tk.radius.md == 10.f);
    // Untouched by the document.
    utassert(tk.radius.lg == wasLg);
    utassert(tk.spacing.md == 20.f && tk.spacing.lg == 16.f);
    utassert(tk.typography.sm.size == 15.f && tk.typography.sm
                                                      .lineHeight == 20.f);
    utassert(tk.typography.sm.weight == FontWeight::Bold);
    utassert(StrEqI(tk.typography.mono, "Cascadia"));
    utassert(tk.shadow.md[0].y == 6.f && tk.shadow.md[0].blur == 8.f);
    // A document with no `tokens` object is not one of these at all.
    JsonValue* other = JsonParse(a, StrL("{\"themes\":[]}"));
    utassert(!ThemeSemanticConfigApply(other, &tk));
    ArenaDelete(a);
}

static void SourceColorAndTokenVocabularyIsTyped() {
    int count = 0;
    const ColorName* colors = ColorNameAll(&count);
    utassert(colors && count == 19 && colors[0] == ColorName::Neutral &&
             colors[18] == ColorName::Rose);
    ColorName parsed = ColorName::Black;
    utassert(ColorNameParse(StrL("BLUE"), &parsed) &&
             parsed == ColorName::Blue);
    utassert(!ColorNameParse(StrL("stone"), &parsed));
    utassert(Is(ColorNameScale(ColorName::Blue, 500), 0x3b82f6));
    utassert(SameColor(ColorNameScale(ColorName::Blue, 123),
                       ColorNameScale(ColorName::Blue, 500)));
    utassert(Is(ThemeBlack(), 0x000000) && Is(ThemeWhite(), 0xffffff));
    utassert(Is(ThemeHsl(0, 100, 50), 0xff0000));

    Background gradient;
    gradient.color = Rgb(0x11, 0x22, 0x33);
    gradient.from = {gradient.color, 0};
    gradient.to = {Rgb(0x44, 0x55, 0x66), 1};
    gradient.angle = 90;
    gradient.gradient = true;
    ThemeToken token = ThemeToken::New(gradient.color, gradient);
    ThemeToken solid = ThemeToken::Solid(Rgb(0xaa, 0xbb, 0xcc));
    utassert(token.background.gradient &&
             SameColor(token.color, gradient.color));
    utassert(!solid.background.gradient &&
             SameColor(solid.color, solid.background.color));
    ThemeColor palette = ThemeDefaultDark();
    utassert(Is(palette.background, 0x0a0a0a));
}

static void TypedSemanticSchemaParsesAndAppliesArrays() {
    Arena* arena = ArenaNew();
    JsonValue* json = JsonParse(
        arena, StrL("{\"tokens\":{\"colors\":{\"surface\":\"#111827\"},"
                    "\"radius\":{\"lg\":10},\"typography\":{\"sans\":\"Inter\","
                    "\"md\":{\"line_height\":22}},\"shadow\":{\"sm\":[{"
                    "\"x\":1,\"y\":2,\"blur_radius\":3,"
                    "\"spread_radius\":4,\"color\":\"#00000080\","
                    "\"inset\":true}]}}}"));
    SemanticThemeConfigFile file;
    utassert(SemanticThemeConfigFileParse(json, &file));
    utassert(file.tokens.colors.surface.has &&
             StrEqI(file.tokens.colors.surface.value, "#111827"));
    utassert(file.tokens.radius.lg.has && file.tokens.radius.lg.value == 10.f);
    SemanticThemeTokens tokens = ThemeSemanticTokens(ThemeLight());
    utassert(file.tokens.ApplyTo(&tokens));
    utassert(Is(tokens.colors.surface, 0x111827));
    utassert(tokens.radius.lg == 10.f);
    utassert(StrEqI(tokens.typography.sans, "Inter") &&
             tokens.typography.md.lineHeight == 22.f);
    utassert(len(tokens.shadow.sm) == 1 && tokens.shadow.sm[0].x == 1.f &&
             tokens.shadow.sm[0].y == 2.f && tokens.shadow.sm[0].blur == 3.f &&
             tokens.shadow.sm[0].spread == 4.f && tokens.shadow.sm[0].inset);
    VecReset(tokens.shadow.sm);
    VecReset(tokens.shadow.md);
    VecReset(tokens.shadow.lg);
    ArenaDelete(arena);
}

// theme_tokens.rs default_colors_are_the_light_palette_and_both_palettes_are_
// readable. The zeroed default was transparent on transparent, which is what
// a Base application that installs no palette used to render as.
static void DefaultColorsAreTheLightPaletteAndBothAreReadable() {
    ColorTokens light = ColorTokens::Light();
    ColorTokens dark = ColorTokens::Dark();

    utassert(ColorTokens() == light);
    utassert(light.background.r == 255 && light.background.g == 255 &&
             light.background.b == 255);
    utassert(light.foreground.r < light.background.r);
    utassert(dark.background.r < dark.foreground.r);
    utassert(light.primary.a == 255);
    utassert(dark.primary.a == 255);
    // The selection wash is translucent in both, so glyphs stay legible.
    utassert(light.selection.a > 0 && light.selection.a < 255);
    utassert(dark.selection.a > 0 && dark.selection.a < 255);
    utassert(light != dark);
}

void TestThemeColor() {
    TestSuite("theme_color");
    DefaultColorsAreTheLightPaletteAndBothAreReadable();
    TheLightPaletteIsDefaultLight();
    TheDarkPaletteIsDefaultDark();
    TheSemanticTokensAreTheRolesOfThePalette();
    SourceTokenDefaultsKeepTypographyAndShadowStructure();
    ASemanticSetAppliesBackToAPalette();
    ASemanticConfigOnlyChangesWhatItNames();
    SourceColorAndTokenVocabularyIsTyped();
    TypedSemanticSchemaParsesAndAppliesArrays();
}
