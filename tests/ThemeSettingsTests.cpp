/* The theme knobs the story's Appearance menu writes — crates/story's
 * FontSizeSelector, which sets `Theme::global_mut(cx).font_size`, `.radius`
 * and `Theme::set_scrollbar_mode`. Both the styled theme and its Base
 * projection are application-owned globals, as they are in Rust. */

#include "Test.h"

static bool SameColor(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// on_select_radius: radius_lg is two more than the radius, except at zero,
// where a large radius is no radius either.
static void TheLargeRadiusFollowsTheSmallOne() {
    App app;
    ThemeSetRadius(&app, 8);
    utassertnear(ThemeLight(&app).radius, 8.f);
    utassertnear(ThemeLight(&app).radiusLg, 10.f);
    // Both palettes move together: the mode can be switched under them.
    utassertnear(ThemeDark(&app).radius, 8.f);
    utassertnear(ThemeDark(&app).radiusLg, 10.f);

    ThemeSetRadius(&app, 0);
    utassertnear(ThemeLight(&app).radius, 0.f);
    utassertnear(ThemeLight(&app).radiusLg, 0.f);

    // theme/mod.rs: 6 and 8 are what a theme starts with.
    ThemeSetRadius(&app, 6);
    utassertnear(ThemeLight(&app).radius, 6.f);
    utassertnear(ThemeLight(&app).radiusLg, 8.f);
    AppGlobalClear(&app);
}

// on_select_font: the root size every element inherits from. A size of zero
// is not one, so it answers with the default rather than collapsing the text.
static void TheFontSizeIsTheOneEverythingIsMeasuredAgainst() {
    App app;
    utassertnear(ThemeFontSize(&app), 16.f);
    ThemeSetFontSize(&app, 18);
    utassertnear(ThemeFontSize(&app), 18.f);
    ThemeSetFontSize(&app, 0);
    utassertnear(ThemeFontSize(&app), 16.f);
    AppGlobalClear(&app);
}

// Theme::set_scrollbar_mode: the default an element that names none gets.
static void TheScrollbarModeIsTheThemesUntilAnElementSaysOtherwise() {
    App app;
    utassert(ScrollbarModeNow(&app) == ScrollbarMode::Scrolling);
    ScrollbarModeSet(&app, ScrollbarMode::Hover);
    utassert(ScrollbarModeNow(&app) == ScrollbarMode::Hover);
    ScrollbarModeSet(&app, ScrollbarMode::Always);
    AppGlobalClear(&app);
}

// Theme::focus_ring: off drops the ring outside the border and leaves the
// tinted border, which is the half that costs no room.
static void TheFocusRingIsOnUntilAnApplicationTurnsItOff() {
    App app;
    utassert(ThemeFocusRing(&app));
    ThemeSetFocusRing(&app, false);
    utassert(!ThemeFocusRing(&app));
    ThemeSetFocusRing(&app, true);
    utassert(ThemeFocusRing(&app));
    AppGlobalClear(&app);
}

static void ThemeSettingsAreIsolatedPerApplication() {
    App first;
    App second;
    ThemeSetRadius(&first, 12);
    ThemeSetFontSize(&first, 20);
    ThemeSetFocusRing(&first, false);
    ScrollbarModeSet(&first, ScrollbarMode::Hover);
    utassertnear(ThemeLight(&first).radius, 12.f);
    utassertnear(ThemeLight(&second).radius, 6.f);
    utassertnear(ThemeFontSize(&first), 20.f);
    utassertnear(ThemeFontSize(&second), 16.f);
    utassert(!ThemeFocusRing(&first));
    utassert(ThemeFocusRing(&second));
    utassert(ScrollbarModeNow(&first) == ScrollbarMode::Hover);
    utassert(ScrollbarModeNow(&second) == ScrollbarMode::Scrolling);
    AppGlobalClear(&first);
    AppGlobalClear(&second);
}

static void StyledThemeChangesProjectIntoBase() {
    App first;
    App second;
    component::Init(&first);
    component::Init(&second);

    ThemeSet(&first, ThemeMode::Dark);
    ThemeSetRadius(&first, 0);
    ScrollbarModeSet(&first, ScrollbarMode::Hover);
    const BaseTheme* a = BaseThemeGlobal(&first);
    const BaseTheme* b = BaseThemeGlobal(&second);
    utassert(a && b);
    utassert(a->scrollbar.mode == ScrollbarMode::Hover);
    utassert(b->scrollbar.mode == ScrollbarMode::Scrolling);
    utassert(a->scrollbar.motion
                 .thumbHoverEntrance == ScrollbarEntrance::SlideAndFade);
    utassert(a->scrollbar.styles.thumb.hasBackground);
    utassert(a->scrollbar.styles.thumb.hasRadius);
    utassertnear(a->scrollbar.styles.thumb.radius, 0.f);
    utassert(a->appearance == BaseThemeAppearance::Dark);
    utassert(b->appearance == BaseThemeAppearance::Light);
    utassert(a->resizable.hasHandle && a->resizable.hasActiveHandle);
    utassert(SameColor(a->resizable.handle, ThemeNow(&first).border));
    utassert(SameColor(a->resizable.activeHandle, ThemeNow(&first).dragBorder));
    utassert(
        SameColor(a->tokens.colors.background, ThemeNow(&first).background));
    utassert(
        SameColor(b->tokens.colors.background, ThemeNow(&second).background));

    Arena* arena = ArenaNew();
    Ctx cx = {};
    cx.a = arena;
    cx.app = &first;
    El* scrollbar = Scrollbar::New(&cx);
    utassert(scrollbar->scrollThemeSet);
    utassert(scrollbar->scrollMode == ScrollbarMode::Hover);
    utassert(scrollbar->scrollMotion
                 .thumbHoverEntrance == ScrollbarEntrance::SlideAndFade);
    utassertnear(scrollbar->scrollThumbRadius, 0.f);
    ArenaDelete(arena);

    AppGlobalClear(&first);
    AppGlobalClear(&second);
}

static void UnprojectedBaseVisualsResolveFromSemanticTokens() {
    SemanticThemeTokens tokens;
    tokens.colors.foreground = Rgb(0xfa, 0xfa, 0xfa);
    tokens.colors.mutedForeground = Rgb(0xa3, 0xa3, 0xa3);
    tokens.colors.surface = Rgb(0x0a, 0x0a, 0x0a);
    tokens.colors.border = Rgb(0x26, 0x26, 0x26);
    tokens.colors.ring = Rgb(0x60, 0xa5, 0xfa);
    tokens.colors.accent = Rgb(0x4a, 0xde, 0x80);

    InputEditorStyle projected;
    InputEditorStyle resolved = InputEditorStyleResolve(projected, tokens);
    utassert(SameColor(resolved.foreground, tokens.colors.foreground));
    utassert(SameColor(resolved.caret, tokens.colors.foreground));
    utassert(
        SameColor(resolved.mutedForeground, tokens.colors.mutedForeground));
    utassert(SameColor(resolved.background, tokens.colors.surface));
    utassert(SameColor(resolved.border, tokens.colors.border));
    utassert(resolved.selection.a == 102);

    Rgba chosen = Rgb(1, 2, 3);
    projected.foreground = chosen;
    resolved = InputEditorStyleResolve(projected, tokens);
    utassert(SameColor(resolved.foreground, chosen));
    utassert(SameColor(resolved.caret, chosen));

    BaseTheme base;
    base.tokens = tokens;
    utassert(
        SameColor(ResizableHandleColor(base, false), tokens.colors.border));
    utassert(SameColor(ResizableHandleColor(base, true), tokens.colors.ring));
    base.resizable.handle = chosen;
    base.resizable.hasHandle = true;
    utassert(SameColor(ResizableHandleColor(base, false), chosen));

    App app;
    BaseThemeSet(&app, base);
    Arena* arena = ArenaNew();
    Ctx cx = {&app, nullptr, arena, {}};
    El* scrollbar = Scrollbar::ApplyStyles(&cx, Div(arena), {});
    utassert(SameColor(scrollbar->scrollThumb.color,
                       RgbaOpacity(tokens.colors.foreground, 0.35f)));
    ScrollbarStyles explicitStyle;
    explicitStyle.thumb = explicitStyle.thumb.Bg(chosen);
    scrollbar = Scrollbar::ApplyStyles(&cx, Div(arena), explicitStyle);
    utassert(SameColor(scrollbar->scrollThumb.color, chosen));
    ArenaDelete(arena);
    AppGlobalClear(&app);
}

static void BaseThemeSourceContractBuildsAndOwnsGlobals() {
    ScrollbarMotion motion;
    motion.enter = 0.12f;
    ScrollbarStyles styles;
    styles.thumb.hasRadius = true;
    styles.thumb.radius = 7;
    base_theme::ScrollbarTheme scrollbar = base_theme::ScrollbarTheme::New()
                                               .WithMode(ScrollbarMode::Hover)
                                               .WithMotion(motion)
                                               .WithStyles(styles);
    utassert(scrollbar.Mode() == ScrollbarMode::Hover);
    utassertnear(scrollbar.Motion().enter, 0.12f);
    utassert(scrollbar.Styles().thumb.hasRadius);
    utassertnear(scrollbar.Styles().thumb.radius, 7.f);

    App app;
    base_theme::Theme fallback = base_theme::Theme::Global(&app);
    utassert(fallback.scrollbar.mode == ScrollbarMode::Scrolling);
    base_theme::Theme* installed = base_theme::Theme::GlobalMut(&app);
    utassert(installed);
    installed->scrollbar = scrollbar;
    base_theme::Theme copy = base_theme::Theme::Global(&app);
    utassert(copy.scrollbar.mode == ScrollbarMode::Hover);
    utassert(BaseThemeGlobal(&app) == installed);
    AppGlobalClear(&app);
}

// The renderer sees only the narrow projection. Component-only table, list,
// sidebar and button families never enter src/gpui's style state.
static void StyledThemeChangesProjectIntoTheRuntimeSeam() {
    App app;
    component::Init(&app);
    ThemeSet(&app, ThemeMode::Dark);
    ThemeSetRadius(&app, 9);
    ThemeSetFontSize(&app, 18);
    ThemeSetFocusRing(&app, false);
    ScrollbarModeSet(&app, ScrollbarMode::Hover);

    const Theme& theme = ThemeNow(&app);
    const RuntimeStyle& runtime = RuntimeStyleNow(&app);
    utassert(SameColor(runtime.background, theme.background));
    utassert(SameColor(runtime.foreground, theme.foreground));
    utassert(SameColor(runtime.mutedForeground, theme.mutedFg));
    utassert(SameColor(runtime.border, theme.border));
    utassert(SameColor(runtime.ring, theme.ring));
    utassert(SameColor(runtime.popover, theme.popover));
    utassert(SameColor(runtime.popoverForeground, theme.popoverFg));
    utassert(runtime.progress.gradient == theme.tokens.progress.gradient);
    utassert(runtime.scrollbarThumb.gradient == theme.tokens.scrollbarThumb
                                                    .gradient);
    utassertnear(runtime.radius, 9.f);
    utassertnear(runtime.fontSize, 18.f);
    utassert(runtime.scrollbarMode == ScrollbarMode::Hover);
    utassert(!runtime.focusRing);

    AppGlobalClear(&app);
}

// GPUI's track_focus carries keyboard focus and nothing visual. The UI layer
// calls focus_ring_style only on controls whose design asks for it, so a raw
// focusable element must opt into the equivalent appearance here.
static void AFocusableElementMustRequestTheFocusAppearance() {
    Arena* a = ArenaNew();
    El* e = Div(a)->FocusId(7);
    utassert(!e->style.focusRing);
    e->FocusRing();
    utassert(e->style.focusRing);
    e->FocusRing(false);
    utassert(!e->style.focusRing);
    // Appearance never changes focus: the element keeps its handle, so Tab
    // still reaches it and Enter still fires.
    utassert(e->style.focusId == 7);
    ArenaDelete(a);
}

// Theme::update — crates/component/src/theme/mod.rs update_tests.
static void EditingColorsUpdatesTheTokensAndTheBaseProjection() {
    App app;
    component::Init(&app);
    Rgba sidebar = RgbaHex(0x123456);
    Rgba primary = RgbaHex(0xabcdef);
    ThemeUpdate(&app, [&](Theme* t) {
        t->sidebar = sidebar;
        t->primary = primary;
        t->radius = 0;
    });
    const Theme& theme = ThemeNow(&app);
    utassert(SameColor(theme.tokens.sidebar.color, sidebar));
    utassert(!theme.tokens.sidebar.gradient);
    utassert(SameColor(theme.tokens.primary.color, primary));
    const BaseTheme* base = BaseThemeGlobal(&app);
    utassert(base);
    utassert(SameColor(base->tokens.colors.primary, primary));
    utassertnear(base->tokens.radius.md, 0.f);
    AppGlobalClear(&app);
}

static void ReplacingThePaletteRewritesEveryToken() {
    App app;
    ThemeUpdate(&app, [](Theme* t) { ThemeSetColors(t, ThemeDefaultDark()); });
    Theme expected = ThemeDefaultDark();
    ThemeTokensReset(&expected);
    utassert(ThemeTokensEq(ThemeNow(&app).tokens, expected.tokens));
    AppGlobalClear(&app);
}

static void AGradientSurvivesUntilItsOwnColorIsEdited() {
    App app;
    Rgba from = RgbaHex(0x4f46e5);
    Rgba to = RgbaHex(0x06b6d4);
    Background token =
        BackgroundLinear(135.f, ColorStopAt(from, 0.f), ColorStopAt(to, 1.f));
    ThemeUpdate(&app, [&](Theme* t) { t->tokens.primary = token; });
    utassert(SameColor(ThemeNow(&app).primary, from));

    ThemeUpdate(&app, [](Theme* t) { t->secondary = RgbaHex(0x222222); });
    utassert(ThemeNow(&app).tokens.primary.gradient);
    utassert(SameColor(ThemeNow(&app).tokens.primary.color, from));

    Rgba solid = RgbaHex(0x999999);
    ThemeUpdate(&app, [&](Theme* t) { t->primary = solid; });
    utassert(!ThemeNow(&app).tokens.primary.gradient);
    utassert(SameColor(ThemeNow(&app).tokens.primary.color, solid));
    AppGlobalClear(&app);
}

static void SettingTheModeLoadsThatModesTheme() {
    App app;
    component::Init(&app);
    Rgba lightBackground = ThemeNow(&app).background;
    ThemeUpdate(&app, [](Theme* t) { t->mode = ThemeMode::Dark; });
    const Theme& theme = ThemeNow(&app);
    utassert(theme.mode == ThemeMode::Dark);
    utassert(!SameColor(theme.background, lightBackground));
    utassert(SameColor(theme.tokens.background.color, theme.background));
    const BaseTheme* base = BaseThemeGlobal(&app);
    utassert(base);
    utassert(base->appearance == BaseThemeAppearance::Dark);
    utassert(SameColor(base->tokens.colors.background, theme.background));
    AppGlobalClear(&app);
}

static void ApplyingAConfigKeepsItsGradients() {
    App app;
    const char* doc =
        "{ \"themes\": [ { \"name\": \"Gradient\", \"mode\": \"light\", "
        "\"colors\": { \"primary\": \"#4F46E5\", \"primary.background\": "
        "\"linear-gradient(135deg, #4F46E5, #06B6D4)\" } } ] }";
    utassert(ThemeRegistryLoadStr(&app, Str(doc)) >= 1);
    const ThemeConfig* cfg = ThemeRegistryFind(&app, StrL("Gradient"));
    utassert(cfg);
    ThemeUpdate(&app, [&](Theme* t) { ThemeApplyConfig(&app, t, cfg); });
    const Theme& theme = ThemeNow(&app);
    utassert(SameColor(theme.tokens.primary.color, theme.primary));
    utassert(theme.tokens.primary.gradient);
    AppGlobalClear(&app);
}

static void EditsAfterApplyingAConfigOfTheOtherModeSurvive() {
    App app;
    component::Init(&app);
    const char* doc =
        "{ \"themes\": [ { \"name\": \"Rounded Dark\", \"mode\": \"dark\", "
        "\"radius\": 12, \"colors\": { \"primary\": \"#4F46E5\" } } ] }";
    utassert(ThemeRegistryLoadStr(&app, Str(doc)) >= 1);
    const ThemeConfig* cfg = ThemeRegistryFind(&app, StrL("Rounded Dark"));
    utassert(cfg);
    utassert(ThemeGet(&app) == ThemeMode::Light);
    Rgba red = Rgb(255, 0, 0);
    ThemeUpdate(&app, [&](Theme* t) {
        ThemeApplyConfig(&app, t, cfg);
        t->radius = 0;
        t->primary = red;
    });
    const Theme& theme = ThemeNow(&app);
    utassert(theme.mode == ThemeMode::Dark);
    utassertnear(theme.radius, 0.f);
    utassert(SameColor(theme.primary, red));
    utassert(SameColor(theme.tokens.primary.color, red));
    const BaseTheme* base = BaseThemeGlobal(&app);
    utassert(base);
    utassert(SameColor(base->tokens.colors.primary, red));
    AppGlobalClear(&app);
}

void TestThemeSettings() {
    TestSuite("theme settings");
    TheFocusRingIsOnUntilAnApplicationTurnsItOff();
    AFocusableElementMustRequestTheFocusAppearance();
    TheLargeRadiusFollowsTheSmallOne();
    TheFontSizeIsTheOneEverythingIsMeasuredAgainst();
    TheScrollbarModeIsTheThemesUntilAnElementSaysOtherwise();
    ThemeSettingsAreIsolatedPerApplication();
    StyledThemeChangesProjectIntoBase();
    UnprojectedBaseVisualsResolveFromSemanticTokens();
    BaseThemeSourceContractBuildsAndOwnsGlobals();
    StyledThemeChangesProjectIntoTheRuntimeSeam();
    EditingColorsUpdatesTheTokensAndTheBaseProjection();
    ReplacingThePaletteRewritesEveryToken();
    AGradientSurvivesUntilItsOwnColorIsEdited();
    SettingTheModeLoadsThatModesTheme();
    ApplyingAConfigKeepsItsGradients();
    EditsAfterApplyingAConfigOfTheOtherModeSurvive();
}
