#ifndef GPUI_SRC_UI_THEME_H_
#define GPUI_SRC_UI_THEME_H_
/* The theme registry — crates/ui/src/theme/{registry.rs, schema.rs, color.rs}

   Upstream's palette is data, not code: `default-theme.json` names sixty-odd
   tokens, every other theme file names a subset of them, and `apply_config`
   resolves what a file leaves out from what it sets — `muted.foreground`
   falls back to the muted surface blended with 70% of the foreground, a
   button's hover to the input colour mixed toward transparent, and so on down
   a chain a hundred entries long.

   This is that reader. `ThemeConfig` is one theme out of a file, held as the
   parsed document plus its header; `ThemeConfigResolve` walks the chain into a
   `Theme`; and the registry is the table of them a picker lists. The colour
   grammar is `color.rs`: a hex string, or a shadcn name with an optional scale
   and an optional percentage — `neutral-200`, `white`, `red-500/40`.

   What is deliberately not here: the highlight styles
   (our colouring is a scanner, not tree-sitter), and the directory watcher —
   Rust reloads themes when the folder changes, and nothing in this tree
   watches a folder. */

#include "base/json.h"
#include "base/list_settings.h"
#include "base/motion.h"
#include "base/theme.h"
#include "ui/notification_settings.h"
#include "ui/sheet_settings.h"

namespace gpui {
// ─── theme (Default Dark) ─────────────────────────────────────────────────

struct App;

enum class ThemeMode : uint8_t {
    Light,
    Dark
};

// ThemeToken — one flat representative color plus the renderable fill a
// theme file supplied. Rust has From conversions; C++ keeps them as explicit
// value builders so a solid and a gradient cannot be confused accidentally.
struct ThemeToken {
    Rgba color = {};
    Background background = {};

    static ThemeToken New(Rgba color, Background background);
    static ThemeToken Solid(Rgba color);
};

// ThemeTokens — crates/ui/src/theme/theme_color.rs.
//
// Upstream keeps the palette twice: `ThemeColor`, where every token is one
// `Hsla`, and `ThemeTokens`, where every token is a `ThemeToken { color,
// background }` — the same colour plus the fill it actually paints with,
// which a theme file may spell as a gradient. `Theme` here is the first; this
// is the second, for the tokens `schema.rs` reads with
// `apply_background_color!` rather than `apply_color!`.
//
// A token's `color` always equals the flat field of the same name, so code
// that wants one colour goes on reading `theme.primary` and code that paints
// a surface reads `theme.tokens.primary`. Only the second can be a gradient.
struct ThemeTokens {
    Background background = {};
    Background titleBar = {};
    Background statusBar = {};
    Background tabBar = {};
    Background tabActiveBg = {};
    Background primary = {};
    Background secondary = {};
    Background accent = {};
    Background muted = {};
    Background popover = {};
    Background danger = {};
    Background info = {};
    Background success = {};
    Background warning = {};
    Background progress = {};
    Background scrollbarThumb = {};
    Background scrollbarThumbHover = {};
    Background skeleton = {};
    Background selection = {};
    Background listActive = {};
    Background tableBg = {};
    Background tableActive = {};
    Background tableEven = {};
    Background tableHead = {};
    Background tableFoot = {};
    Background sidebarAccent = {};
    Background sidebarPrimary = {};
    Background overlay = {};
    Background switchThumb = {};
    Background sliderThumb = {};
    // The rest of `apply_background_color!`, in schema.rs's own order. They
    // came in with the theme viewer, which lists every field of Rust's
    // `ThemeColor` and needs a value for each.
    Background button = {};
    Background buttonHover = {};
    Background buttonActive = {};
    Background primaryHover = {};
    Background primaryActive = {};
    Background buttonPrimary = {};
    Background buttonPrimaryHover = {};
    Background buttonPrimaryActive = {};
    Background secondaryHover = {};
    Background secondaryActive = {};
    Background buttonSecondary = {};
    Background buttonSecondaryHover = {};
    Background buttonSecondaryActive = {};
    Background successHover = {};
    Background successActive = {};
    Background buttonSuccess = {};
    Background buttonSuccessHover = {};
    Background buttonSuccessActive = {};
    Background infoHover = {};
    Background infoActive = {};
    Background buttonInfo = {};
    Background buttonInfoHover = {};
    Background buttonInfoActive = {};
    Background warningHover = {};
    Background warningActive = {};
    Background buttonWarning = {};
    Background buttonWarningHover = {};
    Background buttonWarningActive = {};
    Background dangerHover = {};
    Background dangerActive = {};
    Background buttonDanger = {};
    Background buttonDangerHover = {};
    Background buttonDangerActive = {};
    Background accordion = {};
    Background dropTarget = {};
    Background list = {};
    Background listEven = {};
    Background listHead = {};
    Background listHover = {};
    Background sliderBar = {};
    Background switchBg = {};
    Background tab = {};
    Background tabBarSegmented = {};
    Background tableHover = {};
    Background tiles = {};
    Background scrollbarBg = {};
    Background sidebar = {};
    Background groupBox = {};
    Background descListLabel = {};
};

// Semantic motion policy shared by styled components —
// crates/ui/src/theme/motion.rs. A component names the *role* of its motion
// rather than a duration and a curve, so a theme can retune the lot: a
// control that answers a click springs with `springControl`, something
// travelling from one place to another with `springMove`, and a value that
// runs to an end takes `durationNormalMs` along `easingMove`.
//
// Rust's two distances are `Rems`; there is no Rems in this tree, so they
// are the DIPs a 16 px root resolves them to — rems(0.25) and rems(0.5).
struct MotionTokens {
    float durationInstantMs = 0;
    float durationFastMs = 0;
    float durationNormalMs = 0;
    float durationSlowMs = 0;
    Easing easingEnter = Easing::EaseOut();
    Easing easingExit = Easing::EaseOut();
    Easing easingMove = Easing::EaseOut();
    Spring springControl = {};
    Spring springMove = {};
    float distanceShort = 0;
    float distanceMedium = 0;

    // `impl Default for MotionTokens`, which is what every theme starts with
    // and what the components read today.
    static MotionTokens Default();
};

struct Theme {
    Rgba background;
    Rgba foreground;
    Rgba border;
    Rgba mutedFg;
    // input.border, and theme.input_background(): the surface an input paints
    // itself on — the window background in light, the input border at 70% in
    // dark, the way Rust mixes it toward transparent.
    Rgba inputBorder;
    Rgba inputBg;
    // ring: the focus ring color. caret: the text cursor.
    Rgba ring;
    Rgba caret;
    // selection.background: what a run of picked-out text is tinted with. It
    // is a colour of its own in default-theme.json, not `accent` faded — a
    // blue, in both themes.
    Rgba selection;
    // drag_border: the accent a drag paints with — a resize handle under the
    // pointer, a drop target's rule. The same blue in both themes.
    Rgba dragBorder;
    Rgba titleBar;
    Rgba titleBarBorder;
    // status_bar.background, which falls back to the title bar — the two are
    // the same surface at opposite ends of the window, and most themes only
    // name one of them.
    Rgba statusBar;
    // status_bar.border, which falls back to the title bar's the way the
    // surface above falls back to the title bar's own.
    Rgba statusBarBorder;
    Rgba tabBar;
    Rgba tabActiveBg;
    Rgba tabActiveFg;
    Rgba tabFg;
    Rgba tableBg;
    Rgba tableHead;
    Rgba tableHeadFg;
    // table.foot.background / table.foot.foreground, which fall back to the
    // list's head surface and to muted_foreground (theme/schema.rs).
    Rgba tableFoot;
    Rgba tableFootFg;
    Rgba tableRowBorder;
    Rgba tableEven;
    // list.active.background / list.active.border, and the table pair that
    // falls back to them (theme/schema.rs). What ListSettings::active_highlight
    // picks instead of plain `accent` for a selected row: a translucent tint
    // with a solid rule around it, rather than a filled block.
    Rgba listActive;
    Rgba listActiveBorder;
    Rgba tableActive;
    Rgba tableActiveBorder;
    Rgba progress;
    Rgba red;
    Rgba green;
    Rgba blue;
    Rgba yellow;
    Rgba cyan;
    Rgba magenta;
    // base.<hue>.light. A theme file may name them; where it does not they
    // are the background blended with 80% of the base hue, which is what
    // `apply_color!(red_light, fallback = ..)` in theme/schema.rs says. The
    // colour picker's featured row is these twelve.
    Rgba redLight;
    Rgba greenLight;
    Rgba blueLight;
    Rgba yellowLight;
    Rgba cyanLight;
    Rgba magentaLight;
    // chart_1..chart_5, and the pair a candlestick closes on. Both themes
    // give them the same five blues (default-theme.json).
    Rgba chart1;
    Rgba chart2;
    Rgba chart3;
    Rgba chart4;
    Rgba chart5;
    Rgba chartBullish;
    Rgba chartBearish;
    Rgba danger;
    Rgba dangerFg;
    // popover.background / popover.foreground: the surface something floating
    // over the page sits on — a menu, a dropdown, a notification, the find
    // bar. Both default themes give it the window's own background and
    // foreground, so it only parts from them in a theme that says so.
    Rgba popover;
    Rgba popoverFg;
    Rgba secondaryHover;
    Rgba secondaryActive;
    Rgba secondaryFg;
    Rgba secondary;
    Rgba muted;
    Rgba accent;
    // accent.foreground, and the primary pair a theme file can name beside
    // its background (theme/schema.rs). Both are resolved with the same
    // fallbacks Rust gives them: a hover is the background blended with the
    // colour, an active one is the colour darkened.
    Rgba accentFg;
    Rgba primary;
    Rgba primaryFg;
    Rgba primaryHover;
    Rgba primaryActive;
    Rgba sidebar;
    Rgba sidebarFg;
    Rgba sidebarPrimary;
    Rgba sidebarPrimaryFg;
    Rgba sidebarAccent;
    Rgba sidebarAccentFg;
    Rgba sidebarBorder;
    Rgba scrollbarThumb;
    // scrollbar.thumb.hover.background: what the thumb takes while the
    // pointer is on it or a drag has hold of it. Falls back to the thumb's
    // own colour, as schema.rs does.
    Rgba scrollbarThumbHover;
    // scrollbar.background: the track behind the thumb. Transparent in both
    // default themes, which is why nothing but a theme that names it shows
    // one.
    Rgba scrollbarBg;
    Rgba info;
    Rgba infoFg;
    Rgba success;
    Rgba successFg;
    Rgba warning;
    Rgba warningFg;
    Rgba skeleton;
    // switch.thumb.background and slider.thumb.background, both of which fall
    // back to the window background. They are fields of their own only so a
    // theme that spells either as a gradient gets one.
    Rgba switchThumb;
    Rgba sliderThumb;
    // theme.overlay: what a dialog backdrop tints the page with. 5% black in
    // light, 20% in dark (default-theme.json).
    Rgba overlay;
    // group_box.background / group_box.foreground: the surface a filled
    // GroupBox puts its content on.
    Rgba groupBox;
    Rgba groupBoxFg;
    // description_list_label: the label cell of a DescriptionList.
    Rgba descListLabel;
    Rgba descListLabelFg;
    // The rest of `ThemeColor`, which the theme viewer lists and a theme file
    // can name. What paints with them is still, in most cases, the expression
    // the component was written with — the token layer came first and the
    // components follow it one at a time — so a file that names one of these
    // changes the viewer and whatever has been moved over, and no more. Said
    // where it matters in the token comments below.
    //
    // button.*: the four families a Button has, each with its own foreground,
    // hover and active. The plain one is the input border mixed toward
    // transparent in dark and the window background in light.
    Rgba button;
    Rgba buttonFg;
    Rgba buttonHover;
    Rgba buttonActive;
    Rgba buttonPrimary;
    Rgba buttonPrimaryFg;
    Rgba buttonPrimaryHover;
    Rgba buttonPrimaryActive;
    Rgba buttonSecondary;
    Rgba buttonSecondaryFg;
    Rgba buttonSecondaryHover;
    Rgba buttonSecondaryActive;
    Rgba buttonDanger;
    Rgba buttonDangerFg;
    Rgba buttonDangerHover;
    Rgba buttonDangerActive;
    Rgba buttonSuccess;
    Rgba buttonSuccessFg;
    Rgba buttonSuccessHover;
    Rgba buttonSuccessActive;
    Rgba buttonInfo;
    Rgba buttonInfoFg;
    Rgba buttonInfoHover;
    Rgba buttonInfoActive;
    Rgba buttonWarning;
    Rgba buttonWarningFg;
    Rgba buttonWarningHover;
    Rgba buttonWarningActive;
    // The hover and active halves of the four semantic surfaces, which the
    // palette had only as a background and a foreground.
    Rgba dangerHover;
    Rgba dangerActive;
    Rgba successHover;
    Rgba successActive;
    Rgba infoHover;
    Rgba infoActive;
    Rgba warningHover;
    Rgba warningActive;
    // accordion.background: the surface an accordion item paints, which falls
    // back to the window's own.
    Rgba accordion;
    // drop_target.background: what a dock or a tile paints under a drag that
    // would land there.
    Rgba dropTarget;
    // link / link.active / link.hover: the colour a piece of text that is a
    // link takes. `TextView` draws a markdown link in `link`, which is what
    // node.rs does.
    Rgba link;
    Rgba linkActive;
    Rgba linkHover;
    // list.background and the three rows beside it — the surface the table
    // tokens fall back to.
    Rgba list;
    Rgba listEven;
    Rgba listHead;
    Rgba listHover;
    Rgba tableHover;
    // slider.background: the bar behind a slider's thumb.
    Rgba sliderBar;
    // switch.background: an unchecked switch's track.
    Rgba switchBg;
    // tab.background and tab_bar.segmented.background: a tab that is not the
    // selected one, and the strip a segmented bar paints itself on.
    Rgba tab;
    Rgba tabBarSegmented;
    // tiles.background: the canvas `Tiles` lays its panels on.
    Rgba tiles;
    // window.border: the rule around a window that draws its own frame.
    Rgba windowBorder;
    // crates/ui/src/theme/mod.rs: radius 6, radius_lg 8 (Dialog, Notification).
    float radius;
    float radiusLg;
    // `Theme::radius_full`: the radius that rounds a shape as far as its own
    // size allows — a circle if it is square, a pill if it is not — and zero
    // when the theme squares its corners. Anything past half the shorter side
    // is clamped when it is painted, so this is simply "as round as it goes".
    // Every avatar, badge dot, radio, slider thumb and progress bar takes it
    // rather than half its own height, so one setting governs the lot.
    float radiusFull;
    ThemeMode mode = ThemeMode::Light;
    Str fontFamily = Str(".SystemUIFont");
    float fontSize = 16.f;
#if GPUI_OS_MAC
    Str monoFontFamily = Str("Menlo");
#elif GPUI_OS_WINDOWS
    Str monoFontFamily = Str("Consolas");
#else
    Str monoFontFamily = Str("DejaVu Sans Mono");
#endif
    float monoFontSize = kMonoFontSize;
    bool shadow = true;
    bool focusRing = true;
    ScrollbarMode scrollbarMode = ScrollbarMode::Scrolling;
    // Theme::transparent, which is `gpui::transparent_black()`. Spelled out
    // because an Rgba defaults to opaque, so `= {}` would be black.
    Rgba transparent = Rgba8(0, 0, 0, 0);
    float tileGridSize = 8.f;
    bool tileShadow = true;
    float tileRadius = 0.f;
    // Rust can name both Theme::colors.list and Theme::list because the
    // palette is nested under `colors`; this port flattens palette reads, so
    // the behavioral setting keeps the unambiguous storage name.
    ListSettings listSettings = {};
    // Component behavior settings live beside the palette in Rust's Theme.
    component::NotificationSettings notification = {};
    component::SheetSettings sheet = {};
    // Theme::motion / Theme::motion_tokens(): the styled layer's semantic
    // motion policy. Read it as a field, the way every other setting here is
    // read; Rust needs the accessor only because its palette is nested.
    MotionTokens motion = MotionTokens::Default();
    // The renderable half of the palette, for the tokens a theme file may
    // spell as a gradient. Every one of these carries the flat colour of the
    // same name, so reading a token instead of a field is never wrong.
    ThemeTokens tokens = {};
};

// The C++ palette keeps ThemeColor's flat fields directly on Theme so all
// existing component reads stay one field access. The alias is the source
// value type; copying it still produces an independent palette snapshot.
using ThemeColor = Theme;

// Theme::radius_2xl / radius_3xl / radius_4xl — the surface tiers above the
// `xl` token. Every one derives from the application-controlled base radius,
// so squaring the theme squares them too. Rust spells them as methods on
// Theme; this port keeps the palette a POD, so they are free functions over
// it the way the other derived values are.
inline float ThemeRadius2xl(const Theme& t) {
    return t.radius * 2.5f;
}
inline float ThemeRadius3xl(const Theme& t) {
    return t.radius * 3.f;
}
inline float ThemeRadius4xl(const Theme& t) {
    return t.radius * 3.5f;
}

// Every token set to the flat colour of the same name — for a palette built
// in code, and for the tokens a resolved theme file left alone. A token that
// already carries a gradient is kept.
void ThemeTokensReset(Theme* t);

// The tokens schema.rs derives rather than reads: every one whose fallback is
// an expression over the tokens above it. A palette written in code fills
// them by calling this once it has set the rest; `ThemeConfigResolve` calls
// it too, and then lets the file's own words stand over the top. `dark` is
// the mode, which two of the fallbacks read (`active_darken`, and the plain
// button's surface).
void ThemeFillDerived(Theme* t, bool dark);

// The immutable defaults, used to resolve a theme file and by pure logic
// tests. Active palettes are application-owned and take an App.
const Theme& ThemeDark();
const Theme& ThemeLight();
const Theme& ThemeDark(const App* app);
const Theme& ThemeLight(const App* app);
// ThemeColor::dark() / ::light(): the palette a theme file is resolved
// against, before any config has been applied to it. It never changes, which
// is what keeps two themes applied in a row from compounding.
const Theme& ThemeDefaultDark();
const Theme& ThemeDefaultLight();
// The last step of Theme::apply_config: the resolved palette becomes the
// light or the dark theme. `ui/theme.h` is what produces one.
void ThemeInstall(App* app, ThemeMode mode, const Theme& t);
// Theme::font_size and Theme::radius, which the story's Appearance menu
// writes the way Rust writes `Theme::global_mut(cx).font_size`. The themes
// `radius_lg` follows Rust's rule: two more than the radius, or nothing when
// the radius is nothing.
void ThemeSetRadius(App* app, float radius);
// The root font size every element inherits from, and what an explicit size
// is measured against: a `Font(12)` is twelve at the default 16, and grows
// with it, which is what Rust gets for free by spelling its sizes in rems.
float ThemeFontSize(const App* app);
// One wheel notch, in DIPs. GPUI carries a notch as `ScrollDelta::Lines` —
// SPI_GETWHEELSCROLLLINES lines, three by default — and turns it into pixels
// with the line height of the text being scrolled, at the point the delta is
// applied. There is no per-element text style where a wheel event is built,
// so this is the window's own: the theme font size at `gpui::phi()`, which is
// what TextStyle::line_height defaults to. Three lines of 16px text is 78
// DIPs, and a fixed 48 was what this tree scrolled before.
float WheelNotchPixels(const App* app);
void ThemeSetFontSize(App* app, float px);
// The theme belongs to App, the way Rust keeps it as a Global; read it with
// ThemeNow(cx->app). ThemeNow() is the paint-time fallback for code below Ctx.
// Theme::focus_ring. The ring is painted outside the element's border, so an
// ancestor that clips its content cuts it off; an application whose layout
// clips heavily turns it off here and keeps the tinted border, which takes no
// room.
bool ThemeFocusRing(const App* app);
void ThemeSetFocusRing(App* app, bool on);
const Theme& ThemeNow(const App* app);
void ThemeSet(App* app, ThemeMode mode);
ThemeMode ThemeGet(const App* app);
// Theme::scrollbar_mode. An element that names its own mode wins.
ScrollbarMode ScrollbarModeNow(const App* app);
void ScrollbarModeSet(App* app, ScrollbarMode mode);

// Theme::semantic_tokens and ::apply_semantic_tokens: the projection between
// the component palette and gpui-base's role-based vocabulary. These belong
// here because Base knows nothing about the component Theme.
SemanticThemeTokens ThemeSemanticTokens(const Theme& theme,
                                        float fontSize = 0.f);
void ThemeApplySemanticTokens(Theme* theme, const SemanticThemeTokens& tokens);

// ─── the shadcn scales, generated into theme_data.cpp ────────────────────

const int kNumShadcnColumns = 11;

// One hue and the eleven scales it carries, as 0xRRGGBB.
struct ShadcnScale {
    const char* name;
    uint32_t hex[kNumShadcnColumns];
};

extern const ShadcnScale kShadcnScales[];
extern const int kNumShadcnScales;
extern const int kShadcnScaleNums[kNumShadcnColumns];
// stone, which no ColorName can reach; the colour picker's palette grid is
// the only thing that names it.
extern const uint32_t kShadcnStone[kNumShadcnColumns];
extern const uint32_t kShadcnBlack;
extern const uint32_t kShadcnWhite;
// default-theme.json verbatim: the theme set the registry starts with.
extern const char* const kDefaultThemeJson;

// try_parse_color. `#rgb`, `#rgba`, `#rrggbb` and `#rrggbbaa`, or a colour
// name with an optional `-scale` and an optional `/percent`. A scale that no
// hue carries falls back to 500, the way `ColorName::scale` does. False for
// anything else, which is what leaves a token on its fallback.
bool ThemeParseColor(Str s, Rgba* out);

// try_parse_background. Everything `ThemeParseColor` takes, and CSS two-stop
// `linear-gradient(...)` besides: an angle in degrees or one of the eight
// `to ..` directions, then two colour stops with optional percentages.
//
//     linear-gradient(180deg, #1E293B, #0F172A)
//     linear-gradient(to right, red-500 25%, blue-600 75%)
//
// A gradient's `color` is its first stop, which is what `try_parse_theme_color`
// keeps for the flat palette field beside the renderable token.
bool ThemeParseBackground(Str s, Background* out);

// Public color.rs vocabulary over the generated shadcn table.
enum class ColorName : uint8_t {
    White,
    Black,
    Neutral,
    Gray,
    Red,
    Orange,
    Amber,
    Yellow,
    Lime,
    Green,
    Emerald,
    Teal,
    Cyan,
    Sky,
    Blue,
    Indigo,
    Violet,
    Purple,
    Fuchsia,
    Pink,
    Rose
};

const ColorName* ColorNameAll(int* count);
bool ColorNameParse(Str value, ColorName* out);
Rgba ColorNameScale(ColorName name, int scale = 500);
Rgba ThemeHsl(float hueDegrees, float saturationPercent,
              float lightnessPercent);
Rgba ThemeOklch(float lightness, float chroma, float hueDegrees);
inline Rgba Oklch(float lightness, float chroma, float hueDegrees) {
    return ThemeOklch(lightness, chroma, hueDegrees);
}
Rgba ThemeBlack();
Rgba ThemeWhite();

// Rust extension traits are free color operations and an application lookup
// in this tree. Keep their exact mapping explicit for callers and the audit.
inline const Theme& ActiveTheme(const App* app) {
    return ThemeNow(app);
}

template <typename T>
struct ThemeConfigValue {
    T value = {};
    bool has = false;

    static ThemeConfigValue Some(const T& value) {
        ThemeConfigValue out;
        out.value = value;
        out.has = true;
        return out;
    }
};

// schema.rs. Strings point into the JSON arena passed to Parse; numeric and
// enum options are copied. This is the no-STL equivalent of Option<T>.
struct SemanticColorConfig {
    ThemeConfigValue<Str> background;
    ThemeConfigValue<Str> foreground;
    ThemeConfigValue<Str> surface;
    ThemeConfigValue<Str> surfaceForeground;
    ThemeConfigValue<Str> primary;
    ThemeConfigValue<Str> primaryForeground;
    ThemeConfigValue<Str> secondary;
    ThemeConfigValue<Str> secondaryForeground;
    ThemeConfigValue<Str> muted;
    ThemeConfigValue<Str> mutedForeground;
    ThemeConfigValue<Str> accent;
    ThemeConfigValue<Str> accentForeground;
    ThemeConfigValue<Str> destructive;
    ThemeConfigValue<Str> destructiveForeground;
    ThemeConfigValue<Str> border;
    ThemeConfigValue<Str> input;
    ThemeConfigValue<Str> ring;
};

struct SemanticRadiusConfig {
    ThemeConfigValue<float> none;
    ThemeConfigValue<float> sm;
    ThemeConfigValue<float> md;
    ThemeConfigValue<float> lg;
    ThemeConfigValue<float> xl;
    ThemeConfigValue<float> full;
};

struct SemanticSpacingConfig {
    ThemeConfigValue<float> xxs;
    ThemeConfigValue<float> xs;
    ThemeConfigValue<float> sm;
    ThemeConfigValue<float> md;
    ThemeConfigValue<float> lg;
    ThemeConfigValue<float> xl;
    ThemeConfigValue<float> xxl;
};

struct SemanticTextStyleConfig {
    ThemeConfigValue<float> size;
    ThemeConfigValue<float> lineHeight;
    ThemeConfigValue<FontWeight> weight;
};

struct SemanticTypographyConfig {
    ThemeConfigValue<Str> sans;
    ThemeConfigValue<Str> mono;
    SemanticTextStyleConfig xs;
    SemanticTextStyleConfig sm;
    SemanticTextStyleConfig md;
    SemanticTextStyleConfig lg;
    SemanticTextStyleConfig xl;
    SemanticTextStyleConfig monoMd;
};

struct SemanticShadowConfig {
    // Each value is the source array (or the accepted one-object shorthand)
    // in the caller's JSON arena. ApplyTo owns the resolved Vec instead.
    const JsonValue* sm = nullptr;
    const JsonValue* md = nullptr;
    const JsonValue* lg = nullptr;
};

struct SemanticThemeConfig {
    SemanticColorConfig colors;
    SemanticRadiusConfig radius;
    SemanticSpacingConfig spacing;
    SemanticTypographyConfig typography;
    SemanticShadowConfig shadow;

    bool ApplyTo(SemanticThemeTokens* tokens) const;
};

struct SemanticThemeConfigFile {
    SemanticThemeConfig tokens;
};

bool SemanticThemeConfigParse(const JsonValue* value, SemanticThemeConfig* out);
bool SemanticThemeConfigFileParse(const JsonValue* value,
                                  SemanticThemeConfigFile* out);

// ─── one theme out of a theme file ───────────────────────────────────────

// ThemeConfigColors remains a parsed view because duplicating the hundred
// optional legacy keys would retain the same JSON twice. It preserves the
// source boundary instead of exposing a raw pointer from ThemeConfig.
struct ThemeConfigColors {
    const JsonValue* value = nullptr;

    ThemeConfigColors& operator=(const JsonValue* json) {
        value = json;
        return *this;
    }
    operator const JsonValue*() const { return value; }
    explicit operator bool() const { return value != nullptr; }
};

// `ThemeSet` cannot be used as a C++ type spelling beside the long-standing
// `ThemeSet(app, mode)` operation. ThemeSetConfig is its parsed, arena-backed
// value equivalent; Themes points at the source array without duplicating it.
struct ThemeSetConfig {
    Str name = {};
    Str author = {};
    Str url = {};
    const JsonValue* themes = nullptr;

    int Count() const;
};

bool ThemeSetConfigParse(const JsonValue* value, ThemeSetConfig* out);

// ThemeConfig. The colours stay as the parsed `colors` object rather than as
// fields: a token's fallback is usually another token, so they can only be
// read in the order `apply_config` reads them, and a table of Options would
// be the same document twice.
struct ThemeConfig {
    Str name = {};
    Str author = {};
    Str url = {};
    ThemeMode mode = ThemeMode::Light;
    bool isDefault = false;
    // The `colors` object, or null for a theme that only sets metrics.
    ThemeConfigColors colors;
    // The metrics, or a non-positive number for one the file leaves out.
    float fontSize = 0;
    Str fontFamily = {};
    Str monoFontFamily = {};
    float monoFontSize = 0;
    float radius = -1;
    float radiusLg = -1;
    bool shadow = true;
    bool hasShadow = false;
    // Retained for the dependency-free highlighter adapter to inspect. The
    // tree-sitter HighlightTheme object itself is a standing exclusion.
    const JsonValue* highlight = nullptr;
};

// `is_explicit` in the theme viewer: whether the file names this token itself
// rather than leaving it to the fallback chain. `key` is the one schema.rs
// reads it from, and the aliases the resolver takes count — Rust's own set is
// built from the config struct the file was read into, not from its text.
bool ThemeConfigNames(const ThemeConfig* cfg, const char* key);

// ThemeColor::apply_config: every token of `out` is read from the config,
// falling back to `base` — Rust's `ThemeColor::light()` / `::dark()` — or to
// whatever the chain says when the config has neither. `base` is the palette
// a file is measured against, never the live one, so applying two themes in a
// row cannot compound.
void ThemeConfigResolve(Theme* out, const ThemeConfig* cfg, const Theme& base);

// ─── the registry ────────────────────────────────────────────────────────

struct ThemeRegistry {
    Arena* arena = nullptr;
    Vec<ThemeConfig> themes;
    Vec<Str> loadedDirs;
    Str active[2] = {};
    bool initialized = false;

    ~ThemeRegistry();
    static ThemeRegistry* Global(App* app);
    static const ThemeRegistry* Global(const App* app);
    int Count() const { return themes.len; }
};

// The default themes, from the embedded `default-theme.json`. Idempotent, and
// every other entry point calls it, so an application never has to.
void ThemeRegistryInit(App* app);

// Theme::sync_base: replace Base's application-owned theme with the active
// styled projection. Component initialization installs this as the callback
// for every public theme mutation.
void ThemeSyncBase(App* app);

// load_themes_from_str: every theme in a ThemeSet document joins the table
// under its own name. A name already taken is skipped, the way Rust's is.
// Returns how many were added.
int ThemeRegistryLoadStr(App* app, Str json);

// Rust's `reload()` over its `themes_dir`: every `*.json` in `dir`, with an
// unparseable one skipped rather than fatal. Returns how many themes were
// added. A relative path is resolved against the asset roots.
int ThemeRegistryLoadDir(App* app, Str dir);

// `Theme::apply_semantic_config_str`: a theme written in the semantic
// vocabulary — `{"tokens": {"colors": {..}, "radius": {..}, ..}}` — resolved
// over the palette in force for that mode and applied to it. Every field is
// optional and what a document leaves out stays as it was. False for a
// document that is not one. `out` takes the resolved set, which is what Rust
// hands back for application-owned UI to read.
bool ThemeApplySemanticConfigStr(App* app, ThemeMode mode, Str json,
                                 SemanticThemeTokens* out = nullptr);
// The resolve half on its own, over tokens the caller already has: what
// `SemanticThemeConfig::apply_to` does. Exposed for the tests, which drive it
// without installing a theme.
bool ThemeSemanticConfigApply(const JsonValue* doc, SemanticThemeTokens* io);

// sorted_themes: the defaults first, then light before dark, then by name
// folded to lower case.
int ThemeRegistryCount(const App* app);
const ThemeConfig* ThemeRegistryAt(const App* app, int ix);
const ThemeConfig* ThemeRegistryFind(const App* app, Str name);
// The name of the theme installed for each mode, which is what a picker ticks.
Str ThemeRegistryActive(const App* app, ThemeMode mode);

// Theme::apply_config: the config is resolved against its mode's default
// palette and becomes the light or the dark theme. Switching to that mode
// then paints with it. Answers false for a config that is not in the table.
bool ThemeRegistryApply(App* app, const ThemeConfig* cfg);
bool ThemeRegistryApply(App* app, Str name);

// Puts both modes back on the palettes the tree was built with.
void ThemeRegistryReset(App* app);

void ThemeRegistryFree(App* app);

} // namespace gpui
#endif // GPUI_SRC_UI_THEME_H_
