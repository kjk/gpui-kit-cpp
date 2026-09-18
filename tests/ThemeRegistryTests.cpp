/* The theme registry, against crates/ui/src/theme/{color,schema,registry}.rs */

#include "Test.h"

// offsetof, for the token table below. MSVC and libc++ hand it over through
// one of the headers base.h already pulls in; libstdc++ does not, so gcc
// wants it named.
#include <stddef.h>
#include <stdio.h>

static Rgba Parsed(const char* s) {
    Rgba c = {};
    utassert(ThemeParseColor(Str(s), &c));
    return c;
}

static bool Is(Rgba c, uint32_t rgb, uint8_t a = 255) {
    return c.r == ((rgb >> 16) & 0xff) && c.g == ((rgb >> 8) & 0xff) &&
           c.b == (rgb & 0xff) && c.a == a;
}

// try_parse_color: the whole grammar a theme file writes a colour in.
static void AColourIsAHexOrAName() {
    utassert(Is(Parsed("#ff8800"), 0xff8800));
    utassert(Is(Parsed("#f80"), 0xff8800));
    // The short forms double each digit, so #f80c is the same as #ff8800cc.
    utassert(Is(Parsed("#f80c"), 0xff8800, 0xcc));
    utassert(Is(Parsed("#ff880080"), 0xff8800, 0x80));
    utassert(Is(Parsed("white"), 0xffffff));
    utassert(Is(Parsed("black"), 0x000000));
    // A name with no scale is the 500 column.
    utassert(Is(Parsed("neutral-500"), 0x737373));
    utassert(Is(Parsed("neutral"), 0x737373));
    utassert(Is(Parsed("neutral-200"), 0xe5e5e5));
    // And a scale no hue carries falls back to 500 rather than failing.
    utassert(Is(Parsed("neutral-123"), 0x737373));
    // `/percent` is the alpha, out of a hundred. 50% is 127 rather than 128:
    // Rust holds the alpha as 0.5 and truncates when it makes a byte of it,
    // and so does this.
    utassert(Is(Parsed("red-500/50"), 0xef4444, 127));

    Rgba c = {};
    utassert(!ThemeParseColor(StrL("nosuchcolour-500"), &c));
    utassert(!ThemeParseColor(StrL("#12345"), &c));
    utassert(!ThemeParseColor(StrL(""), &c));
    // An opacity over a hundred is an error, not a clamp.
    utassert(!ThemeParseColor(StrL("red-500/140"), &c));
}

// The embedded default-theme.json is what the registry starts with, and its
// two entries are the pair every other theme is resolved against.
static void TheDefaultsAreInTheTable() {
    App app;
    utassert(ThemeRegistryCount(&app) >= 2);
    const ThemeConfig* light = ThemeRegistryFind(&app, StrL("Default Light"));
    const ThemeConfig* dark = ThemeRegistryFind(&app, StrL("Default Dark"));
    utassert(light && dark);
    utassert(light->isDefault && dark->isDefault);
    utassert(light->mode == ThemeMode::Light);
    utassert(dark->mode == ThemeMode::Dark);
    utassert(light->colors && dark->colors);
    // sorted_themes puts the defaults first and light before dark.
    utassert(ThemeRegistryAt(&app, 0) == light);
    utassert(ThemeRegistryAt(&app, 1) == dark);
    utassert(ThemeRegistryFind(&app, StrL("No Such Theme")) == nullptr);
    AppGlobalClear(&app);
}

// Resolving the file the hardcoded palette was transcribed from has to give
// the hardcoded palette back. That is the whole point of reading the file:
// the two cannot drift without this failing.
static void TheDefaultThemeResolvesToTheDefaultPalette() {
    App app;
    struct Token {
        const char* name;
        size_t off;
    };
#define TOK(f)                 \
    {                          \
        #f, offsetof(Theme, f) \
    }
    static const Token kTokens[] = {
        TOK(background),
        TOK(foreground),
        TOK(border),
        TOK(mutedFg),
        TOK(inputBorder),
        TOK(inputBg),
        TOK(ring),
        TOK(caret),
        TOK(selection),
        TOK(dragBorder),
        TOK(chart1),
        TOK(chart2),
        TOK(chart3),
        TOK(chart4),
        TOK(chart5),
        TOK(chartBullish),
        TOK(chartBearish),
        TOK(titleBar),
        TOK(titleBarBorder),
        TOK(tabBar),
        TOK(tabActiveBg),
        TOK(tabActiveFg),
        TOK(tabFg),
        TOK(tableBg),
        TOK(tableHead),
        TOK(tableHeadFg),
        TOK(tableRowBorder),
        TOK(tableEven),
        TOK(listActive),
        TOK(listActiveBorder),
        TOK(tableActive),
        TOK(tableActiveBorder),
        TOK(progress),
        TOK(red),
        TOK(green),
        TOK(blue),
        TOK(yellow),
        TOK(cyan),
        TOK(magenta),
        TOK(danger),
        TOK(dangerFg),
        TOK(secondaryHover),
        TOK(secondaryActive),
        TOK(secondaryFg),
        TOK(secondary),
        TOK(muted),
        TOK(accent),
        TOK(primary),
        TOK(primaryFg),
        TOK(sidebar),
        TOK(sidebarFg),
        TOK(sidebarPrimary),
        TOK(sidebarPrimaryFg),
        TOK(sidebarAccent),
        TOK(sidebarAccentFg),
        TOK(sidebarBorder),
        TOK(scrollbarThumb),
        TOK(info),
        TOK(infoFg),
        TOK(success),
        TOK(successFg),
        TOK(warning),
        TOK(warningFg),
        TOK(skeleton),
        TOK(overlay),
        TOK(groupBox),
        TOK(groupBoxFg),
        TOK(descListLabel),
        TOK(descListLabelFg),
    };
#undef TOK

    const char* names[2] = {"Default Light", "Default Dark"};
    const Theme* bases[2] = {&ThemeDefaultLight(), &ThemeDefaultDark()};
    for (int m = 0; m < 2; m++) {
        const ThemeConfig* cfg = ThemeRegistryFind(&app, Str(names[m]));
        utassert(cfg != nullptr);
        if (!cfg) {
            continue;
        }
        Theme got = {};
        ThemeConfigResolve(&got, cfg, *bases[m]);
        for (size_t i = 0; i < sizeof(kTokens) / sizeof(kTokens[0]); i++) {
            Rgba a = *(const Rgba*)((const char*)&got + kTokens[i].off);
            Rgba b = *(const Rgba*)((const char*)bases[m] + kTokens[i].off);
            bool same = a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
            if (!same) {
                printf(
                    "  theme drift %s.%s: file %02x%02x%02x%02x, "
                    "hardcoded %02x%02x%02x%02x\n",
                    names[m], kTokens[i].name, a.r, a.g, a.b, a.a, b.r, b.g,
                    b.b, b.a);
            }
            utassert(same);
        }
    }
    AppGlobalClear(&app);
}

// try_parse_background: everything above, and a two-stop linear-gradient.
// The two cases here are upstream's own tests in color.rs.
static Background Bg(const char* str) {
    Background b;
    utassert(ThemeParseBackground(Str(str), &b));
    return b;
}

static void ABackgroundIsAColourOrAGradient() {
    // A plain colour is a solid fill and nothing more.
    Background solid = Bg("#4F46E5");
    utassert(BackgroundIsSolid(solid) && Is(solid.color, 0x4f46e5));

    // test_try_parse_background_linear_gradient
    Background g = Bg("linear-gradient(135deg, #4F46E5, #06B6D4)");
    utassert(g.gradient);
    utassertnear(g.angle, 135.f);
    utassert(Is(g.from.color, 0x4f46e5));
    utassertnear(g.from.percentage, 0.f);
    utassert(Is(g.to.color, 0x06b6d4));
    utassertnear(g.to.percentage, 1.f);
    // try_parse_theme_color: the flat colour is the first stop.
    utassert(Is(g.color, 0x4f46e5));

    // test_try_parse_background_linear_gradient_direction_and_stops
    Background d = Bg("linear-gradient(to right, red-500 25%, blue-600 75%)");
    utassertnear(d.angle, 90.f);
    utassert(Is(d.from.color, 0xef4444));
    utassertnear(d.from.percentage, 0.25f);
    utassert(Is(d.to.color, 0x2563eb));
    utassertnear(d.to.percentage, 0.75f);

    // Two stops and no angle is `to bottom`, which is 180.
    utassertnear(Bg("linear-gradient(#fff, #000)").angle, 180.f);
    // The eight directions, and a negative angle wrapping the way
    // rem_euclid(360) does.
    utassertnear(Bg("linear-gradient(to top, #fff, #000)").angle, 0.f);
    utassertnear(Bg("linear-gradient(to bottom left, #fff, #000)").angle,
                 225.f);
    utassertnear(Bg("linear-gradient(-90deg, #fff, #000)").angle, 270.f);
    // Case and spacing are the file's business, not the parser's.
    utassertnear(Bg("  LINEAR-GRADIENT( TO RIGHT , #fff , #000 )").angle, 90.f);

    // What the grammar refuses, which is what leaves a token on its fallback.
    Background junk;
    utassert(!ThemeParseBackground(StrL("linear-gradient(#fff)"), &junk));
    utassert(!ThemeParseBackground(StrL("linear-gradient(#fff, #000, #123)"),
                                   &junk));
    utassert(!ThemeParseBackground(StrL("linear-gradient(9turn, #fff, #000)"),
                                   &junk));
    utassert(!ThemeParseBackground(StrL("linear-gradient(to sideways, a, b)"),
                                   &junk));
    utassert(!ThemeParseBackground(StrL("linear-gradient(#fff, notacolour)"),
                                   &junk));
    utassert(!ThemeParseBackground(StrL("radial-gradient(#fff, #000)"), &junk));
}

// apply_background_color!: the token carries the gradient, the flat field
// beside it carries its first stop, and a token nobody named falls back
// through the same chain the colours do.
static void AGradientReachesTheTokenAndItsFallbacks() {
    App app;
    const char* doc =
        "{ \"name\": \"grad-set\", \"themes\": [ { \"name\": \"Grad\", "
        "\"mode\": \"light\", \"colors\": { "
        "\"primary.background\": \"linear-gradient(180deg, #1E293B, "
        "#0F172A)\", "
        "\"title_bar.background\": \"linear-gradient(to right, #FFFFFF, "
        "#F8FAFC)\", "
        "\"selection.background\": \"linear-gradient(180deg, #1D4ED8, "
        "#1D4ED8FF)\" "
        "} } ] }";
    utassert(ThemeRegistryLoadStr(&app, Str(doc)) >= 1);
    const ThemeConfig* cfg = ThemeRegistryFind(&app, StrL("Grad"));
    utassert(cfg != nullptr);

    Theme t = {};
    ThemeConfigResolve(&t, cfg, ThemeDefaultLight());

    // The gradient lands on the token; the flat field is its first stop, so
    // code that wants one colour still gets a sensible one.
    utassert(t.tokens.primary.gradient);
    utassert(Is(t.tokens.primary.from.color, 0x1e293b));
    utassert(Is(t.tokens.primary.to.color, 0x0f172a));
    utassert(Is(t.primary, 0x1e293b));
    utassertnear(t.tokens.titleBar.angle, 90.f);

    // A token the file leaves out falls back through the chain, gradient and
    // all: progress.bar and the sidebar's primary both fall back to primary.
    utassert(t.tokens.progress.gradient);
    utassert(Is(t.tokens.progress.to.color, 0x0f172a));
    utassert(t.tokens.sidebarPrimary.gradient);
    // status_bar falls back to the title bar, which the file did name.
    utassertnear(t.tokens.statusBar.angle, 90.f);
    utassert(t.tokens.statusBar.gradient);

    // And one nobody touched is simply its flat colour.
    utassert(!t.tokens.tableHead.gradient);
    utassert(Is(t.tokens.tableHead.color,
                (uint32_t)((t.tableHead.r << 16) | (t.tableHead.g << 8) |
                           t.tableHead.b),
                t.tableHead.a));

    // The selection is capped at a third whatever the file spells, and a
    // gradient is capped stop by stop rather than scaled, so the opaque
    // second stop comes down too.
    utassert(t.selection.a <= 0x4d + 1);
    utassert(t.tokens.selection.from.color.a <= 0x4d + 1);
    utassert(t.tokens.selection.to.color.a <= 0x4d + 1);
    AppGlobalClear(&app);
}

// is_explicit, which the theme viewer's `Inherited Colors` toggle is the
// other side of: whether the file names this token itself or leaves it to
// the fallback chain. Rust builds the set from the config struct the file
// deserialized into, so an alias counts under the name schema.rs renamed it
// to; the registry keeps the parsed `colors` object and asks it the same
// question.
static void AConfigKnowsWhichKeysItsFileNamed() {
    App app;
    const char* doc =
        "{ \"name\": \"named-set\", \"themes\": [ { \"name\": \"Named\", "
        "\"mode\": \"light\", \"colors\": { "
        "\"primary\": \"#1E293B\", "
        "\"button.primary.hover.background\": \"#334155\", "
        "\"selection.background\": \"#1D4ED8\" "
        "} } ] }";
    utassert(ThemeRegistryLoadStr(&app, Str(doc)) >= 1);
    const ThemeConfig* cfg = ThemeRegistryFind(&app, StrL("Named"));
    utassert(cfg != nullptr);

    // Named by the file, and so shown whether or not the toggle is on.
    utassert(ThemeConfigNames(cfg, "primary"));
    utassert(ThemeConfigNames(cfg, "button.primary.hover.background"));
    utassert(ThemeConfigNames(cfg, "selection.background"));
    // Not named: these come out of the chain, and are the rows the toggle
    // hides. `primary.hover.background` is the one worth pinning — the file
    // named the *button's* hover and not the semantic one behind it, and the
    // two are separate keys.
    utassert(!ThemeConfigNames(cfg, "primary.hover.background"));
    utassert(!ThemeConfigNames(cfg, "background"));
    utassert(!ThemeConfigNames(cfg, "title_bar.background"));
    // A key no schema has is named by nothing.
    utassert(!ThemeConfigNames(cfg, "not.a.token"));
    // And a null config names nothing rather than crashing, which is what the
    // viewer holds before a theme has been picked.
    utassert(!ThemeConfigNames(nullptr, "primary"));
    AppGlobalClear(&app);
}

static void RegistriesAreIsolatedPerApplication() {
    App first;
    App second;
    const char* doc =
        "{\"themes\":[{\"name\":\"Only First\",\"mode\":\"light\"}]}";
    utassert(ThemeRegistryLoadStr(&first, Str(doc)) == 1);
    utassert(ThemeRegistryFind(&first, StrL("Only First")) != nullptr);
    utassert(ThemeRegistryFind(&second, StrL("Only First")) == nullptr);
    utassert(ThemeRegistryCount(&first) == ThemeRegistryCount(&second) + 1);
    AppGlobalClear(&first);
    AppGlobalClear(&second);
}

static void SourceRegistryAndConfigSettingsAreRetained() {
    App app;
    const char* doc =
        "{\"themes\":[{\"name\":\"Metrics\",\"mode\":\"light\","
        "\"font.size\":18,\"font.family\":\"Inter\","
        "\"mono_font.family\":\"Cascadia Mono\","
        "\"mono_font.size\":14,\"radius\":9,\"shadow\":false,"
        "\"colors\":{\"primary.background\":\"#123456\"}}]}";
    Arena* arena = ArenaNew();
    JsonValue* parsed = JsonParse(arena, Str(doc));
    ThemeSetConfig set;
    utassert(ThemeSetConfigParse(parsed, &set) && set.Count() == 1);
    utassert(ThemeRegistryLoadStr(&app, Str(doc)) == 1);
    ThemeRegistry* registry = ThemeRegistry::Global(&app);
    const ThemeRegistry* read = ThemeRegistry::Global((const App*)&app);
    utassert(registry && read == registry &&
             registry->Count() == ThemeRegistryCount(&app));
    const ThemeConfig* config = ThemeRegistryFind(&app, StrL("Metrics"));
    utassert(config && config->colors && config->fontSize == 18.f &&
             StrEqI(config->fontFamily, "Inter") &&
             StrEqI(config->monoFontFamily, "Cascadia Mono") &&
             config->monoFontSize == 14.f && config->hasShadow &&
             !config->shadow);
    utassert(ThemeRegistryApply(&app, config));
    const Theme& theme = ThemeLight(&app);
    utassert(theme.mode == ThemeMode::Light && theme.fontSize == 18.f &&
             StrEqI(theme.fontFamily, "Inter") &&
             StrEqI(theme.monoFontFamily, "Cascadia Mono") &&
             theme.monoFontSize == 14.f && theme.radius == 9.f &&
             theme.radiusLg == 11.f && !theme.shadow);
    SemanticThemeTokens tokens = ThemeSemanticTokens(theme);
    utassert(tokens.shadow.sm.len == 0 && tokens.shadow.md.len == 0 &&
             tokens.shadow.lg.len == 0);
    utassert(StrEqI(tokens.typography.sans, "Inter") &&
             tokens.typography.md.size == 18.f &&
             tokens.typography.monoMd.size == 14.f);
    ArenaDelete(arena);
    AppGlobalClear(&app);
}

static void ApplyConfigReadsTheChartColors() {
    App app;
    const char* doc =
        "{\"themes\":[{\"name\":\"Palette\",\"mode\":\"light\","
        "\"colors\":{\"chart.1\":\"#111111\",\"chart.2\":\"not a color\","
        "\"chart.3\":\"#333333\",\"chart.bullish\":\"#00ff00\","
        "\"chart.bearish\":\"#ff0000\"}}]}";
    utassert(ThemeRegistryLoadStr(&app, Str(doc)) == 1);
    const ThemeConfig* config = ThemeRegistryFind(&app, StrL("Palette"));
    utassert(config && ThemeRegistryApply(&app, config));
    const Theme& theme = ThemeLight(&app);
    utassert(theme.chart1.r == 0x11 && theme.chart1.g == 0x11 &&
             theme.chart1.b == 0x11);
    Rgba chart2 = RgbaLighten(theme.blue, 0.2f);
    utassert(theme.chart2.r == chart2.r && theme.chart2.g == chart2.g &&
             theme.chart2.b == chart2.b);
    utassert(theme.chart3.r == 0x33 && theme.chart3.g == 0x33 &&
             theme.chart3.b == 0x33);
    Rgba chart4 = RgbaDarken(theme.blue, 0.2f);
    utassert(theme.chart4.r == chart4.r && theme.chart4.g == chart4.g &&
             theme.chart4.b == chart4.b);
    utassert(theme.chartBullish.r == 0 && theme.chartBullish.g == 255 &&
             theme.chartBullish.b == 0);
    utassert(theme.chartBearish.r == 255 && theme.chartBearish.g == 0 &&
             theme.chartBearish.b == 0);
    AppGlobalClear(&app);
}

void TestThemeRegistry() {
    TestSuite("theme_registry");
    AColourIsAHexOrAName();
    TheDefaultsAreInTheTable();
    TheDefaultThemeResolvesToTheDefaultPalette();
    ABackgroundIsAColourOrAGradient();
    AGradientReachesTheTokenAndItsFallbacks();
    AConfigKnowsWhichKeysItsFileNamed();
    RegistriesAreIsolatedPerApplication();
    SourceRegistryAndConfigSettingsAreRetained();
    ApplyConfigReadsTheChartColors();
}
