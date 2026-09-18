#include "ui/theme.h"

#include "base/lib.h"
#include "gpui/assets.h"
#include "gpui/paint.h"
#include "ui/text.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace gpui {

// theme/motion.rs: the numbers every styled component's motion is tuned to.
// The three curves are CSS cubic Béziers, and both springs carry the
// tolerance their unit wants — normalized for a control, a tenth of a pixel
// for something travelling across the screen.
MotionTokens MotionTokens::Default() {
    MotionTokens m;
    m.durationInstantMs = 0.f;
    m.durationFastMs = 120.f;
    m.durationNormalMs = 180.f;
    m.durationSlowMs = 280.f;
    // Rust `.expect("static enter curve is valid")`: all three are constants
    // that have been checked, so the unwrap cannot fail.
    m.easingEnter = Easing::CubicBezier(0.16f, 1.f, 0.3f, 1.f).Unwrap();
    m.easingExit = Easing::CubicBezier(0.4f, 0.f, 1.f, 1.f).Unwrap();
    m.easingMove = Easing::CubicBezier(0.2f, 0.f, 0.f, 1.f).Unwrap();
    m.springControl = Spring::New(180.f);
    m.springMove = Spring::New(280.f).WithDamping(0.85f).WithEpsilon(0.1f);
    m.distanceShort = 4.f;
    m.distanceMedium = 8.f;
    return m;
}

ThemeToken ThemeToken::New(Rgba color, Background background) {
    ThemeToken out;
    out.color = color;
    out.background = background;
    return out;
}

ThemeToken ThemeToken::Solid(Rgba color) {
    return New(color, Background(color));
}

void ThemeTokensReset(Theme* t) {
    if (!t) {
        return;
    }
    if (!t->tokens.background.gradient) {
        t->tokens.background = t->background;
    }
    if (!t->tokens.titleBar.gradient) {
        t->tokens.titleBar = t->titleBar;
    }
    if (!t->tokens.statusBar.gradient) {
        t->tokens.statusBar = t->statusBar;
    }
    if (!t->tokens.tabBar.gradient) {
        t->tokens.tabBar = t->tabBar;
    }
    if (!t->tokens.tabActiveBg.gradient) {
        t->tokens.tabActiveBg = t->tabActiveBg;
    }
    if (!t->tokens.primary.gradient) {
        t->tokens.primary = t->primary;
    }
    if (!t->tokens.secondary.gradient) {
        t->tokens.secondary = t->secondary;
    }
    if (!t->tokens.accent.gradient) {
        t->tokens.accent = t->accent;
    }
    if (!t->tokens.muted.gradient) {
        t->tokens.muted = t->muted;
    }
    if (!t->tokens.danger.gradient) {
        t->tokens.danger = t->danger;
    }
    if (!t->tokens.info.gradient) {
        t->tokens.info = t->info;
    }
    if (!t->tokens.success.gradient) {
        t->tokens.success = t->success;
    }
    if (!t->tokens.warning.gradient) {
        t->tokens.warning = t->warning;
    }
    if (!t->tokens.progress.gradient) {
        t->tokens.progress = t->progress;
    }
    if (!t->tokens.popover.gradient) {
        t->tokens.popover = t->popover;
    }
    if (!t->tokens.scrollbarThumb.gradient) {
        t->tokens.scrollbarThumb = t->scrollbarThumb;
    }
    if (!t->tokens.scrollbarThumbHover.gradient) {
        t->tokens.scrollbarThumbHover = t->scrollbarThumbHover;
    }
    if (!t->tokens.skeleton.gradient) {
        t->tokens.skeleton = t->skeleton;
    }
    if (!t->tokens.selection.gradient) {
        t->tokens.selection = t->selection;
    }
    if (!t->tokens.listActive.gradient) {
        t->tokens.listActive = t->listActive;
    }
    if (!t->tokens.tableBg.gradient) {
        t->tokens.tableBg = t->tableBg;
    }
    if (!t->tokens.tableActive.gradient) {
        t->tokens.tableActive = t->tableActive;
    }
    if (!t->tokens.tableEven.gradient) {
        t->tokens.tableEven = t->tableEven;
    }
    if (!t->tokens.tableHead.gradient) {
        t->tokens.tableHead = t->tableHead;
    }
    if (!t->tokens.tableFoot.gradient) {
        t->tokens.tableFoot = t->tableFoot;
    }
    if (!t->tokens.sidebarAccent.gradient) {
        t->tokens.sidebarAccent = t->sidebarAccent;
    }
    if (!t->tokens.sidebarPrimary.gradient) {
        t->tokens.sidebarPrimary = t->sidebarPrimary;
    }
    if (!t->tokens.overlay.gradient) {
        t->tokens.overlay = t->overlay;
    }
    if (!t->tokens.switchThumb.gradient) {
        t->tokens.switchThumb = t->switchThumb;
    }
    if (!t->tokens.sliderThumb.gradient) {
        t->tokens.sliderThumb = t->sliderThumb;
    }
    if (!t->tokens.button.gradient) {
        t->tokens.button = t->button;
    }
    if (!t->tokens.buttonHover.gradient) {
        t->tokens.buttonHover = t->buttonHover;
    }
    if (!t->tokens.buttonActive.gradient) {
        t->tokens.buttonActive = t->buttonActive;
    }
    if (!t->tokens.primaryHover.gradient) {
        t->tokens.primaryHover = t->primaryHover;
    }
    if (!t->tokens.primaryActive.gradient) {
        t->tokens.primaryActive = t->primaryActive;
    }
    if (!t->tokens.buttonPrimary.gradient) {
        t->tokens.buttonPrimary = t->buttonPrimary;
    }
    if (!t->tokens.buttonPrimaryHover.gradient) {
        t->tokens.buttonPrimaryHover = t->buttonPrimaryHover;
    }
    if (!t->tokens.buttonPrimaryActive.gradient) {
        t->tokens.buttonPrimaryActive = t->buttonPrimaryActive;
    }
    if (!t->tokens.secondaryHover.gradient) {
        t->tokens.secondaryHover = t->secondaryHover;
    }
    if (!t->tokens.secondaryActive.gradient) {
        t->tokens.secondaryActive = t->secondaryActive;
    }
    if (!t->tokens.buttonSecondary.gradient) {
        t->tokens.buttonSecondary = t->buttonSecondary;
    }
    if (!t->tokens.buttonSecondaryHover.gradient) {
        t->tokens.buttonSecondaryHover = t->buttonSecondaryHover;
    }
    if (!t->tokens.buttonSecondaryActive.gradient) {
        t->tokens.buttonSecondaryActive = t->buttonSecondaryActive;
    }
    if (!t->tokens.successHover.gradient) {
        t->tokens.successHover = t->successHover;
    }
    if (!t->tokens.successActive.gradient) {
        t->tokens.successActive = t->successActive;
    }
    if (!t->tokens.buttonSuccess.gradient) {
        t->tokens.buttonSuccess = t->buttonSuccess;
    }
    if (!t->tokens.buttonSuccessHover.gradient) {
        t->tokens.buttonSuccessHover = t->buttonSuccessHover;
    }
    if (!t->tokens.buttonSuccessActive.gradient) {
        t->tokens.buttonSuccessActive = t->buttonSuccessActive;
    }
    if (!t->tokens.infoHover.gradient) {
        t->tokens.infoHover = t->infoHover;
    }
    if (!t->tokens.infoActive.gradient) {
        t->tokens.infoActive = t->infoActive;
    }
    if (!t->tokens.buttonInfo.gradient) {
        t->tokens.buttonInfo = t->buttonInfo;
    }
    if (!t->tokens.buttonInfoHover.gradient) {
        t->tokens.buttonInfoHover = t->buttonInfoHover;
    }
    if (!t->tokens.buttonInfoActive.gradient) {
        t->tokens.buttonInfoActive = t->buttonInfoActive;
    }
    if (!t->tokens.warningHover.gradient) {
        t->tokens.warningHover = t->warningHover;
    }
    if (!t->tokens.warningActive.gradient) {
        t->tokens.warningActive = t->warningActive;
    }
    if (!t->tokens.buttonWarning.gradient) {
        t->tokens.buttonWarning = t->buttonWarning;
    }
    if (!t->tokens.buttonWarningHover.gradient) {
        t->tokens.buttonWarningHover = t->buttonWarningHover;
    }
    if (!t->tokens.buttonWarningActive.gradient) {
        t->tokens.buttonWarningActive = t->buttonWarningActive;
    }
    if (!t->tokens.dangerHover.gradient) {
        t->tokens.dangerHover = t->dangerHover;
    }
    if (!t->tokens.dangerActive.gradient) {
        t->tokens.dangerActive = t->dangerActive;
    }
    if (!t->tokens.buttonDanger.gradient) {
        t->tokens.buttonDanger = t->buttonDanger;
    }
    if (!t->tokens.buttonDangerHover.gradient) {
        t->tokens.buttonDangerHover = t->buttonDangerHover;
    }
    if (!t->tokens.buttonDangerActive.gradient) {
        t->tokens.buttonDangerActive = t->buttonDangerActive;
    }
    if (!t->tokens.accordion.gradient) {
        t->tokens.accordion = t->accordion;
    }
    if (!t->tokens.dropTarget.gradient) {
        t->tokens.dropTarget = t->dropTarget;
    }
    if (!t->tokens.list.gradient) {
        t->tokens.list = t->list;
    }
    if (!t->tokens.listEven.gradient) {
        t->tokens.listEven = t->listEven;
    }
    if (!t->tokens.listHead.gradient) {
        t->tokens.listHead = t->listHead;
    }
    if (!t->tokens.listHover.gradient) {
        t->tokens.listHover = t->listHover;
    }
    if (!t->tokens.sliderBar.gradient) {
        t->tokens.sliderBar = t->sliderBar;
    }
    if (!t->tokens.switchBg.gradient) {
        t->tokens.switchBg = t->switchBg;
    }
    if (!t->tokens.tab.gradient) {
        t->tokens.tab = t->tab;
    }
    if (!t->tokens.tabBarSegmented.gradient) {
        t->tokens.tabBarSegmented = t->tabBarSegmented;
    }
    if (!t->tokens.tableHover.gradient) {
        t->tokens.tableHover = t->tableHover;
    }
    if (!t->tokens.tiles.gradient) {
        t->tokens.tiles = t->tiles;
    }
    if (!t->tokens.scrollbarBg.gradient) {
        t->tokens.scrollbarBg = t->scrollbarBg;
    }
    if (!t->tokens.sidebar.gradient) {
        t->tokens.sidebar = t->sidebar;
    }
    if (!t->tokens.groupBox.gradient) {
        t->tokens.groupBox = t->groupBox;
    }
    if (!t->tokens.descListLabel.gradient) {
        t->tokens.descListLabel = t->descListLabel;
    }
}

// ThemeFillDerived — the half of schema.rs's chain that is arithmetic rather
// than a key. Every expression here is the `fallback =` of the
// `apply_color!` / `apply_background_color!` with the same name, in the same
// order, so a palette in code and a theme file that names nothing land on the
// same numbers.
void ThemeFillDerived(Theme* t, bool dark) {
    if (!t) {
        return;
    }
    // The two constants the button and hover fallbacks are written against.
    const float activeDarken = dark ? 0.2f : 0.1f;
    const float hoverOpacity = 0.9f;
    const Rgba clear = RgbaTransparent();
    auto set = [](Rgba* flat, Background* tok, Rgba c) {
        *flat = c;
        *tok = c;
    };

    // Button. The plain one sits on the input border mixed toward
    // transparent in dark and on the window background in light.
    set(&t->button, &t->tokens.button,
        dark ? RgbaMixOklab(t->inputBorder, clear, 0.3f) : t->background);
    t->buttonFg = t->foreground;
    set(&t->buttonHover, &t->tokens.buttonHover,
        RgbaMixOklab(t->inputBorder, clear, 0.5f));
    set(&t->buttonActive, &t->tokens.buttonActive,
        RgbaMixOklab(t->inputBorder, clear, 0.7f));

    set(&t->buttonPrimary, &t->tokens.buttonPrimary, t->primary);
    t->buttonPrimaryFg = t->primaryFg;
    set(&t->buttonPrimaryHover, &t->tokens.buttonPrimaryHover, t->primaryHover);
    set(&t->buttonPrimaryActive, &t->tokens.buttonPrimaryActive,
        t->primaryActive);

    set(&t->buttonSecondary, &t->tokens.buttonSecondary, t->secondary);
    t->buttonSecondaryFg = t->secondaryFg;
    set(&t->buttonSecondaryHover, &t->tokens.buttonSecondaryHover,
        t->secondaryHover);
    set(&t->buttonSecondaryActive, &t->tokens.buttonSecondaryActive,
        t->secondaryActive);

    // The four semantic surfaces: a hover blended over the window, an active
    // one darkened, and a button family mixed toward transparent.
    set(&t->successHover, &t->tokens.successHover,
        RgbaBlend(t->background, RgbaOpacity(t->success, hoverOpacity)));
    set(&t->successActive, &t->tokens.successActive,
        RgbaDarken(t->success, activeDarken));
    set(&t->buttonSuccess, &t->tokens.buttonSuccess,
        RgbaMixOklab(t->success, clear, 0.2f));
    t->buttonSuccessFg = t->success;
    set(&t->buttonSuccessHover, &t->tokens.buttonSuccessHover,
        RgbaMixOklab(t->success, clear, 0.3f));
    set(&t->buttonSuccessActive, &t->tokens.buttonSuccessActive,
        RgbaMixOklab(t->success, clear, 0.4f));

    set(&t->infoHover, &t->tokens.infoHover,
        RgbaBlend(t->background, RgbaOpacity(t->info, hoverOpacity)));
    set(&t->infoActive, &t->tokens.infoActive,
        RgbaDarken(t->info, activeDarken));
    set(&t->buttonInfo, &t->tokens.buttonInfo,
        RgbaMixOklab(t->info, clear, 0.2f));
    t->buttonInfoFg = t->info;
    set(&t->buttonInfoHover, &t->tokens.buttonInfoHover,
        RgbaMixOklab(t->info, clear, 0.3f));
    set(&t->buttonInfoActive, &t->tokens.buttonInfoActive,
        RgbaMixOklab(t->info, clear, 0.4f));

    set(&t->warningHover, &t->tokens.warningHover,
        RgbaBlend(t->background, RgbaOpacity(t->warning, hoverOpacity)));
    // The one that is not a plain darken: warning's active is blended over
    // the window as well, which is what schema.rs writes.
    set(&t->warningActive, &t->tokens.warningActive,
        RgbaBlend(t->background, RgbaDarken(t->warning, activeDarken)));
    set(&t->buttonWarning, &t->tokens.buttonWarning,
        RgbaMixOklab(t->warning, clear, 0.2f));
    t->buttonWarningFg = t->warning;
    set(&t->buttonWarningHover, &t->tokens.buttonWarningHover,
        RgbaMixOklab(t->warning, clear, 0.3f));
    set(&t->buttonWarningActive, &t->tokens.buttonWarningActive,
        RgbaMixOklab(t->warning, clear, 0.4f));

    set(&t->dangerActive, &t->tokens.dangerActive,
        RgbaDarken(t->danger, activeDarken));
    set(&t->dangerHover, &t->tokens.dangerHover,
        RgbaBlend(t->background, RgbaOpacity(t->danger, hoverOpacity)));
    set(&t->buttonDanger, &t->tokens.buttonDanger,
        RgbaMixOklab(t->danger, clear, 0.2f));
    t->buttonDangerFg = t->danger;
    set(&t->buttonDangerHover, &t->tokens.buttonDangerHover,
        RgbaMixOklab(t->danger, clear, 0.3f));
    set(&t->buttonDangerActive, &t->tokens.buttonDangerActive,
        RgbaMixOklab(t->danger, clear, 0.4f));

    set(&t->accordion, &t->tokens.accordion, t->background);
    set(&t->dropTarget, &t->tokens.dropTarget, RgbaOpacity(t->primary, 0.2f));
    t->link = t->primary;
    t->linkActive = t->link;
    t->linkHover = t->link;
    set(&t->list, &t->tokens.list, t->background);
    set(&t->listEven, &t->tokens.listEven, t->list);
    set(&t->listHead, &t->tokens.listHead, t->list);
    set(&t->listHover, &t->tokens.listHover, RgbaOpacity(t->accent, 0.6f));
    set(&t->tableHover, &t->tokens.tableHover, t->listHover);
    set(&t->sliderBar, &t->tokens.sliderBar, t->primary);
    set(&t->switchBg, &t->tokens.switchBg, t->secondaryActive);
    set(&t->tab, &t->tokens.tab, t->background);
    set(&t->tabBarSegmented, &t->tokens.tabBarSegmented, t->secondary);
    set(&t->tiles, &t->tokens.tiles, t->background);
    t->windowBorder = t->border;
}

const Theme& ThemeDefaultDark() {
    static Theme t;
    static bool init = false;
    if (!init) {
        t.mode = ThemeMode::Dark;
        t.background = Rgb(0x0a, 0x0a, 0x0a);
        t.foreground = Rgb(0xfa, 0xfa, 0xfa);
        t.border = Rgb(0x26, 0x26, 0x26);
        t.mutedFg = Rgb(0xa3, 0xa3, 0xa3);
        t.inputBorder = Rgb(0x2f, 0x2f, 0x2f);
        t.inputBg = Rgba8(0x2f, 0x2f, 0x2f, 0x4c);
        t.ring = Rgb(0x73, 0x73, 0x73);
        t.caret = Rgb(0xfa, 0xfa, 0xfa);
        t.selection = Rgba8(0x1d, 0x4e, 0xd8, 0x4c);
        t.dragBorder = Rgb(0x3b, 0x82, 0xf6);
        t.titleBar = Rgb(0x17, 0x17, 0x17);
        t.titleBarBorder = Rgb(0x26, 0x26, 0x26);
        t.statusBarBorder = Rgb(0x26, 0x26, 0x26);
        t.tabBar = Rgb(0x17, 0x17, 0x17);
        t.tabActiveBg = Rgb(0x0a, 0x0a, 0x0a);
        t.tabActiveFg = Rgb(0xfa, 0xfa, 0xfa);
        t.tabFg = Rgb(0xd4, 0xd4, 0xd4);
        t.tableBg = Rgb(0x0a, 0x0a, 0x0a);
        t.tableHead = Rgba8(0x17, 0x17, 0x17, 0x66);
        t.tableHeadFg = Rgb(0x52, 0x52, 0x52);
        // table.foot has no entry of its own, so it takes list.head's
        // surface and muted_foreground, as schema.rs falls back.
        t.tableFoot = Rgba8(0x17, 0x17, 0x17, 0x66);
        t.tableFootFg = t.mutedFg;
        t.tableRowBorder = Rgba8(0x26, 0x26, 0x26, 0xb3);
        t.tableEven = Rgba8(0x17, 0x17, 0x17, 0x66);
        // default-theme.json dark: list.active.background #1e40af33,
        // list.active.border #1d4ed8. table.active has no entry of its own, so
        // it falls back to the list pair.
        t.listActive = Rgba8(0x1e, 0x40, 0xaf, 0x33);
        t.listActiveBorder = Rgb(0x1d, 0x4e, 0xd8);
        t.tableActive = t.listActive;
        t.tableActiveBorder = t.listActiveBorder;
        t.progress = Rgb(0xf5, 0xf5, 0xf5);
        t.red = Rgb(0xf8, 0x71, 0x71);
        t.green = Rgb(0x4a, 0xde, 0x80);
        t.blue = Rgb(0x60, 0xa5, 0xfa);
        t.yellow = Rgb(0xfa, 0xcc, 0x15);
        t.cyan = Rgb(0x22, 0xd3, 0xee);
        t.magenta = Rgb(0xc0, 0x84, 0xfc);
        // base.<hue>.light, one scale step lighter than the base in the
        // dark theme and one step lighter than the 600 in the light one.
        t.redLight = Rgb(0xfc, 0xa5, 0xa5);
        t.greenLight = Rgb(0x86, 0xef, 0xac);
        t.blueLight = Rgb(0x93, 0xc5, 0xfd);
        t.yellowLight = Rgb(0xfd, 0xe0, 0x47);
        t.cyanLight = Rgb(0x67, 0xe8, 0xf9);
        t.magentaLight = Rgb(0xd8, 0xb4, 0xfe);
        t.chart1 = Rgb(0x93, 0xc5, 0xfd);
        t.chart2 = Rgb(0x3b, 0x82, 0xf6);
        t.chart3 = Rgb(0x25, 0x63, 0xeb);
        t.chart4 = Rgb(0x1d, 0x4e, 0xd8);
        t.chart5 = Rgb(0x1e, 0x40, 0xaf);
        t.chartBullish = Rgb(0x16, 0xa3, 0x4a);
        t.chartBearish = Rgb(0xdc, 0x26, 0x26);
        t.danger = Rgb(0xf8, 0x71, 0x71);
        t.dangerFg = Rgb(0xdc, 0x26, 0x26);
        t.secondaryHover = Rgb(0x29, 0x29, 0x29);
        t.secondaryActive = Rgb(0x21, 0x21, 0x21);
        t.secondaryFg = Rgb(0xfa, 0xfa, 0xfa);
        t.secondary = Rgb(0x26, 0x26, 0x26);
        t.muted = Rgb(0x26, 0x26, 0x26);
        t.accent = Rgb(0x26, 0x26, 0x26);
        t.accentFg = Rgb(0xfa, 0xfa, 0xfa);
        t.primary = Rgb(0xfa, 0xfa, 0xfa);
        t.primaryFg = Rgb(0x17, 0x17, 0x17);
        t.primaryHover = Rgb(0xf5, 0xf5, 0xf5);
        t.primaryActive = Rgb(0xe5, 0xe5, 0xe5);
        t.sidebar = Rgb(0x0a, 0x0a, 0x0a);
        t.sidebarFg = Rgb(0xf5, 0xf5, 0xf5);
        t.sidebarPrimary = Rgb(0xf5, 0xf5, 0xf5);
        t.sidebarPrimaryFg = Rgb(0x0a, 0x0a, 0x0a);
        t.sidebarAccent = Rgb(0x26, 0x26, 0x26);
        t.sidebarAccentFg = Rgb(0xf5, 0xf5, 0xf5);
        t.sidebarBorder = Rgb(0x26, 0x26, 0x26);
        t.popover = Rgb(0x0a, 0x0a, 0x0a);
        t.popoverFg = Rgb(0xfa, 0xfa, 0xfa);
        t.scrollbarThumb = Rgba8(0x52, 0x52, 0x52, 0xe6);
        t.scrollbarThumbHover = Rgb(0x52, 0x52, 0x52);
        t.scrollbarBg = Rgba8(0x17, 0x17, 0x17, 0x00);
        t.info = Rgb(0x22, 0xd3, 0xee);
        t.infoFg = Rgb(0x08, 0x91, 0xb2);
        t.success = Rgb(0x4a, 0xde, 0x80);
        t.successFg = Rgb(0x16, 0xa3, 0x4a);
        t.warning = Rgb(0xfa, 0xcc, 0x15);
        t.warningFg = Rgb(0xca, 0x8a, 0x04);
        t.skeleton = Rgb(0x17, 0x17, 0x17);
        t.overlay = Rgba8(0, 0, 0, 0x33);
        t.groupBox = Rgb(0x0a, 0x0a, 0x0a);
        t.groupBoxFg = Rgb(0xfa, 0xfa, 0xfa);
        // description_list.label.background: the window background with the
        // border at 20% over it, which is what schema.rs falls back to. The
        // key default-theme.json spells is not the one the schema reads.
        t.descListLabel = Rgb(0x0f, 0x0f, 0x0f);
        // description_list.label.foreground falls back to muted_foreground,
        // not to the foreground: a label reads as a caption beside its value.
        t.descListLabelFg = Rgb(0xa3, 0xa3, 0xa3);
        t.radius = 6;
        t.radiusLg = 8;
        t.radiusFull = kRadiusFull;
        // The three that only exist so a theme can spell them as gradients,
        // on the fallbacks schema.rs gives them.
        t.statusBar = t.titleBar;
        t.switchThumb = t.background;
        t.sliderThumb = t.background;
        ThemeFillDerived(&t, true);
        ThemeTokensReset(&t);
        init = true;
    }
    return t;
}

const Theme& ThemeDefaultLight() {
    static Theme t;
    static bool init = false;
    if (!init) {
        t.mode = ThemeMode::Light;
        t.background = Rgb(0xff, 0xff, 0xff);
        t.foreground = Rgb(0x0a, 0x0a, 0x0a);
        t.border = Rgb(0xe5, 0xe5, 0xe5);
        t.mutedFg = Rgb(0x73, 0x73, 0x73);
        t.inputBorder = Rgb(0xe5, 0xe5, 0xe5);
        t.inputBg = Rgb(0xff, 0xff, 0xff);
        t.ring = Rgb(0xa3, 0xa3, 0xa3);
        t.caret = Rgb(0x0a, 0x0a, 0x0a);
        t.selection = Rgba8(0x55, 0xa0, 0xfc, 0x4c);
        t.dragBorder = Rgb(0x3b, 0x82, 0xf6);
        t.titleBar = Rgb(0xf8, 0xf8, 0xf8);
        t.titleBarBorder = Rgb(0xe5, 0xe5, 0xe5);
        t.statusBarBorder = Rgb(0xe5, 0xe5, 0xe5);
        t.tabBar = Rgb(0xf5, 0xf5, 0xf5);
        t.tabActiveBg = Rgb(0xff, 0xff, 0xff);
        t.tabActiveFg = Rgb(0x17, 0x17, 0x17);
        t.tabFg = Rgb(0x40, 0x40, 0x40);
        t.tableBg = Rgb(0xff, 0xff, 0xff);
        t.tableHead = Rgb(0xfa, 0xfa, 0xfa);
        t.tableHeadFg = Rgb(0x73, 0x73, 0x73);
        t.tableFoot = Rgb(0xfa, 0xfa, 0xfa);
        t.tableFootFg = t.mutedFg;
        t.tableRowBorder = Rgba8(0xe5, 0xe5, 0xe5, 0xb3);
        t.tableEven = Rgb(0xfa, 0xfa, 0xfa);
        // default-theme.json light: the same blue for the list and the table.
        t.listActive = Rgba8(0xbf, 0xdb, 0xfe, 0x33);
        t.listActiveBorder = Rgb(0x60, 0xa5, 0xfa);
        t.tableActive = t.listActive;
        t.tableActiveBorder = t.listActiveBorder;
        t.progress = Rgb(0x17, 0x17, 0x17);
        t.red = Rgb(0xdc, 0x26, 0x26);
        t.green = Rgb(0x16, 0xa3, 0x4a);
        t.blue = Rgb(0x25, 0x63, 0xeb);
        t.yellow = Rgb(0xca, 0x8a, 0x04);
        t.cyan = Rgb(0x08, 0x91, 0xb2);
        t.magenta = Rgb(0x93, 0x33, 0xea);
        t.redLight = Rgb(0xf8, 0x71, 0x71);
        t.greenLight = Rgb(0x4a, 0xde, 0x80);
        t.blueLight = Rgb(0x60, 0xa5, 0xfa);
        t.yellowLight = Rgb(0xfa, 0xcc, 0x15);
        t.cyanLight = Rgb(0x22, 0xd3, 0xee);
        t.magentaLight = Rgb(0xc0, 0x84, 0xfc);
        t.chart1 = Rgb(0x93, 0xc5, 0xfd);
        t.chart2 = Rgb(0x3b, 0x82, 0xf6);
        t.chart3 = Rgb(0x25, 0x63, 0xeb);
        t.chart4 = Rgb(0x1d, 0x4e, 0xd8);
        t.chart5 = Rgb(0x1e, 0x40, 0xaf);
        t.chartBullish = Rgb(0x16, 0xa3, 0x4a);
        t.chartBearish = Rgb(0xdc, 0x26, 0x26);
        t.danger = Rgb(0xef, 0x44, 0x44);
        t.dangerFg = Rgb(0xfa, 0xfa, 0xfa);
        t.secondaryHover = Rgb(0xe5, 0xe5, 0xe5);
        t.secondaryActive = Rgb(0xd4, 0xd4, 0xd4);
        t.secondaryFg = Rgb(0x17, 0x17, 0x17);
        t.secondary = Rgb(0xe5, 0xe5, 0xe5);
        t.muted = Rgb(0xf5, 0xf5, 0xf5);
        t.accent = Rgb(0xf5, 0xf5, 0xf5);
        t.accentFg = Rgb(0x17, 0x17, 0x17);
        t.primary = Rgb(0x17, 0x17, 0x17);
        t.primaryFg = Rgb(0xfa, 0xfa, 0xfa);
        t.primaryHover = Rgb(0x26, 0x26, 0x26);
        t.primaryActive = Rgb(0x0a, 0x0a, 0x0a);
        t.sidebar = Rgb(0xfa, 0xfa, 0xfa);
        t.sidebarFg = Rgb(0x17, 0x17, 0x17);
        t.sidebarPrimary = Rgb(0x17, 0x17, 0x17);
        t.sidebarPrimaryFg = Rgb(0xfa, 0xfa, 0xfa);
        t.sidebarAccent = Rgb(0xe5, 0xe5, 0xe5);
        t.sidebarAccentFg = Rgb(0x17, 0x17, 0x17);
        t.sidebarBorder = Rgb(0xe5, 0xe5, 0xe5);
        t.popover = Rgb(0xff, 0xff, 0xff);
        t.popoverFg = Rgb(0x0a, 0x0a, 0x0a);
        t.scrollbarThumb = Rgba8(0xa3, 0xa3, 0xa3, 0xe6);
        t.scrollbarThumbHover = Rgb(0xa3, 0xa3, 0xa3);
        t.scrollbarBg = Rgba8(0xfa, 0xfa, 0xfa, 0x00);
        t.info = Rgb(0x06, 0xb6, 0xd4);
        t.infoFg = Rgb(0xfa, 0xfa, 0xfa);
        t.success = Rgb(0x22, 0xc5, 0x5e);
        t.successFg = Rgb(0xfa, 0xfa, 0xfa);
        t.warning = Rgb(0xea, 0xb3, 0x08);
        t.warningFg = Rgb(0xfa, 0xfa, 0xfa);
        t.skeleton = Rgb(0xf5, 0xf5, 0xf5);
        t.overlay = Rgba8(0, 0, 0, 0x0d);
        t.groupBox = Rgb(0xf5, 0xf5, 0xf5);
        t.groupBoxFg = Rgb(0x17, 0x17, 0x17);
        t.descListLabel = Rgb(0xf9, 0xf9, 0xf9);
        t.descListLabelFg = Rgb(0x73, 0x73, 0x73);
        t.radius = 6;
        t.radiusLg = 8;
        t.radiusFull = kRadiusFull;
        // The three that only exist so a theme can spell them as gradients,
        // on the fallbacks schema.rs gives them.
        t.statusBar = t.titleBar;
        t.switchThumb = t.background;
        t.sliderThumb = t.background;
        ThemeFillDerived(&t, false);
        ThemeTokensReset(&t);
        init = true;
    }
    return t;
}

// Pure callers and theme resolution read the immutable defaults.
const Theme& ThemeLight() {
    return ThemeDefaultLight();
}

const Theme& ThemeDark() {
    return ThemeDefaultDark();
}

// theme/mod.rs: radius 6, radius_lg 8, font_size 16.
static const float kDefaultFontSize = 16.f;

struct AppThemeState {
    Theme active[2] = {};
    ThemeMode mode = ThemeMode::Light;
    bool initialized = false;
};

static ThemeRegistry* RegistryOf(const App* app);
static AppThemeState* ThemeStateOf(const App* app);

static void ThemeSyncRuntime(App* app, const AppThemeState* state) {
    if (!app || !state) {
        return;
    }
    const Theme& ui = state->active[(int)state->mode];
    RuntimeStyle style;
    style.background = ui.background;
    style.foreground = ui.foreground;
    style.mutedForeground = ui.mutedFg;
    style.border = ui.border;
    style.ring = ui.ring;
    style.inspectorAccent = ui.blue;
    style.popover = ui.popover;
    style.popoverForeground = ui.popoverFg;
    style.progress = ui.tokens.progress;
    style.scrollbarThumb = ui.tokens.scrollbarThumb;
    style.scrollbarThumbHover = ui.tokens.scrollbarThumbHover;
    style.scrollbarTrack = ui.tokens.scrollbarBg;
    style.legacyPrimary = ui.tokens.primary;
    style.legacyPrimaryForeground = ui.primaryFg;
    style.legacyPrimaryHover = RgbaMix(ui.primary, ui.foreground, 0.85f);
    style.legacyMuted = ui.tokens.muted;
    style.legacySecondary = ui.tokens.secondary;
    style.legacySecondaryForeground = ui.secondaryFg;
    style.legacySecondaryHover = ui.secondaryHover;
    style.legacySecondaryActive = ui.secondaryActive;
    style.radius = ui.radius;
    style.fontSize = ui.fontSize;
    style.scrollbarMode = ui.scrollbarMode;
    style.focusRing = ui.focusRing;
    RuntimeStyleInstall(app, style);
}

// font_name_with_fallbacks: `.SystemUIFont` is the platform face, `.ZedSans`
// / `Zed Plex Sans` are IBM Plex Sans, `.ZedMono` / `Zed Plex Mono` are Lilex.
static Str ThemeFontNameWithFallbacks(Str name) {
    if (StrEq(name, ".SystemUIFont")) {
        return PaintSystemUIFontMappedFamily();
    }
    if (StrEq(name, ".ZedSans") || StrEq(name, "Zed Plex Sans")) {
        return StrL("IBM Plex Sans");
    }
    if (StrEq(name, ".ZedMono") || StrEq(name, "Zed Plex Mono")) {
        return StrL("Lilex");
    }
    return name;
}

static bool ThemeFontNameLoads(Str name, const Str* installed, int n) {
    if (!name) {
        return false;
    }
    // Core Text's virtual system face is always loadable and is not always
    // in CTFontManagerCopyAvailableFontFamilyNames.
    if (StrEq(name, ".AppleSystemUIFont")) {
        return true;
    }
    for (int i = 0; i < n; i++) {
        if (StrEq(installed[i], name)) {
            return true;
        }
    }
    return false;
}

// resolve_font + get_font_for_id: the lookup key of the first family that
// loads. That is still `.SystemUIFont` when its mapped face is installed,
// which is why Windows and macOS keep the virtual name.
static Str ThemeResolvedSystemUIFont(const Str* installed, int n) {
    Str requested = StrL(".SystemUIFont");
    if (ThemeFontNameLoads(ThemeFontNameWithFallbacks(requested), installed,
                           n)) {
        return requested;
    }
    // text_system.rs fallback_font_stack. Virtual `.Zed*` keys map through
    // font_name_with_fallbacks; get_font_for_id then returns the key, so
    // substitute only names a fallback that is itself in `installed`.
    static const char* kFallbacks[] = {
        ".ZedMono",     ".ZedSans",  "Helvetica", "Segoe UI",    "Ubuntu",
        "Adwaita Sans", "Cantarell", "Noto Sans", "DejaVu Sans", "Arial",
    };
    for (int i = 0; i < (int)dimof(kFallbacks); i++) {
        Str key = Str(kFallbacks[i]);
        if (ThemeFontNameLoads(ThemeFontNameWithFallbacks(key), installed, n)) {
            return key;
        }
    }
    return {};
}

Str ThemeSubstituteSystemFont(Str requested, Str resolved, const Str* installed,
                              int n) {
    if (!resolved.s || StrEq(resolved, requested)) {
        return {};
    }
    for (int i = 0; i < n; i++) {
        if (StrEq(installed[i], resolved)) {
            return resolved;
        }
    }
    return {};
}

// system_font.rs resolve_default_font. Paint still uses its platform default
// face; this is the name Theme.fontFamily and semantic tokens expose.
static void ThemeResolveDefaultFont(App* app, AppThemeState* state) {
    int n = 0;
    const Str* installed =
        PaintInstalledFontNames(app ? app->paint : nullptr, &n);
    if (n == 0) {
        return;
    }
    Str resolved = {};
    bool resolvedReady = false;
    for (int i = 0; i < 2; i++) {
        Theme* t = &state->active[i];
        if (!StrEq(t->fontFamily, ".SystemUIFont")) {
            continue;
        }
        if (!resolvedReady) {
            resolved = ThemeResolvedSystemUIFont(installed, n);
            resolvedReady = true;
        }
        Str family = ThemeSubstituteSystemFont(StrL(".SystemUIFont"), resolved,
                                               installed, n);
        if (family.s) {
            t->fontFamily = family;
        }
    }
}

static void ThemeDidChange(App* app, AppThemeState* state) {
    if (state) {
        ThemeResolveDefaultFont(app, state);
        ThemeSyncRuntime(app, state);
        ThemeSyncBase(app);
    }
}

static bool BackgroundEq(const Background& a, const Background& b) {
    if (a.gradient != b.gradient || !RgbaEq(a.color, b.color)) {
        return false;
    }
    if (!a.gradient) {
        return true;
    }
    return RgbaEq(a.from.color, b.from.color) &&
           a.from.percentage == b.from.percentage &&
           RgbaEq(a.to.color, b.to.color) &&
           a.to.percentage == b.to.percentage && a.angle == b.angle;
}

bool ThemeTokensEq(const ThemeTokens& a, const ThemeTokens& b) {
#define TOK_EQ(field)                      \
    if (!BackgroundEq(a.field, b.field)) { \
        return false;                      \
    }
    TOK_EQ(background)
    TOK_EQ(titleBar)
    TOK_EQ(statusBar)
    TOK_EQ(tabBar)
    TOK_EQ(tabActiveBg)
    TOK_EQ(primary)
    TOK_EQ(secondary)
    TOK_EQ(accent)
    TOK_EQ(muted)
    TOK_EQ(popover)
    TOK_EQ(danger)
    TOK_EQ(info)
    TOK_EQ(success)
    TOK_EQ(warning)
    TOK_EQ(progress)
    TOK_EQ(scrollbarThumb)
    TOK_EQ(scrollbarThumbHover)
    TOK_EQ(skeleton)
    TOK_EQ(selection)
    TOK_EQ(listActive)
    TOK_EQ(tableBg)
    TOK_EQ(tableActive)
    TOK_EQ(tableEven)
    TOK_EQ(tableHead)
    TOK_EQ(tableFoot)
    TOK_EQ(sidebarAccent)
    TOK_EQ(sidebarPrimary)
    TOK_EQ(overlay)
    TOK_EQ(switchThumb)
    TOK_EQ(sliderThumb)
    TOK_EQ(button)
    TOK_EQ(buttonHover)
    TOK_EQ(buttonActive)
    TOK_EQ(primaryHover)
    TOK_EQ(primaryActive)
    TOK_EQ(buttonPrimary)
    TOK_EQ(buttonPrimaryHover)
    TOK_EQ(buttonPrimaryActive)
    TOK_EQ(secondaryHover)
    TOK_EQ(secondaryActive)
    TOK_EQ(buttonSecondary)
    TOK_EQ(buttonSecondaryHover)
    TOK_EQ(buttonSecondaryActive)
    TOK_EQ(successHover)
    TOK_EQ(successActive)
    TOK_EQ(buttonSuccess)
    TOK_EQ(buttonSuccessHover)
    TOK_EQ(buttonSuccessActive)
    TOK_EQ(infoHover)
    TOK_EQ(infoActive)
    TOK_EQ(buttonInfo)
    TOK_EQ(buttonInfoHover)
    TOK_EQ(buttonInfoActive)
    TOK_EQ(warningHover)
    TOK_EQ(warningActive)
    TOK_EQ(buttonWarning)
    TOK_EQ(buttonWarningHover)
    TOK_EQ(buttonWarningActive)
    TOK_EQ(dangerHover)
    TOK_EQ(dangerActive)
    TOK_EQ(buttonDanger)
    TOK_EQ(buttonDangerHover)
    TOK_EQ(buttonDangerActive)
    TOK_EQ(accordion)
    TOK_EQ(dropTarget)
    TOK_EQ(list)
    TOK_EQ(listEven)
    TOK_EQ(listHead)
    TOK_EQ(listHover)
    TOK_EQ(sliderBar)
    TOK_EQ(switchBg)
    TOK_EQ(tab)
    TOK_EQ(tabBarSegmented)
    TOK_EQ(tableHover)
    TOK_EQ(tiles)
    TOK_EQ(scrollbarBg)
    TOK_EQ(sidebar)
    TOK_EQ(groupBox)
    TOK_EQ(descListLabel)
#undef TOK_EQ
    return true;
}

void ThemeSetColors(Theme* t, const Theme& colors) {
    if (!t) {
        return;
    }
    memcpy(t, &colors, offsetof(Theme, radius));
}

static void ThemeTokensReconcile(Theme* t, const Theme* colorsBefore,
                                 const ThemeTokens* tokensBefore) {
    if (!t || !colorsBefore || !tokensBefore) {
        return;
    }
    // A field edited on colors wins: when its token no longer names that
    // color, the token becomes that solid color. A field edited only on the
    // tokens writes its solid color back. A field set on both sides, as
    // applying a theme config does, keeps its token.
#define RECONCILE(field)                                              \
    if (!RgbaEq(t->field, colorsBefore->field)) {                     \
        if (!RgbaEq(t->tokens.field.color, t->field)) {               \
            t->tokens.field = Background(t->field);                   \
        }                                                             \
    } else if (!BackgroundEq(t->tokens.field, tokensBefore->field)) { \
        t->field = t->tokens.field.color;                             \
    }
    RECONCILE(background)
    RECONCILE(titleBar)
    RECONCILE(statusBar)
    RECONCILE(tabBar)
    RECONCILE(tabActiveBg)
    RECONCILE(primary)
    RECONCILE(secondary)
    RECONCILE(accent)
    RECONCILE(muted)
    RECONCILE(popover)
    RECONCILE(danger)
    RECONCILE(info)
    RECONCILE(success)
    RECONCILE(warning)
    RECONCILE(progress)
    RECONCILE(scrollbarThumb)
    RECONCILE(scrollbarThumbHover)
    RECONCILE(skeleton)
    RECONCILE(selection)
    RECONCILE(listActive)
    RECONCILE(tableBg)
    RECONCILE(tableActive)
    RECONCILE(tableEven)
    RECONCILE(tableHead)
    RECONCILE(tableFoot)
    RECONCILE(sidebarAccent)
    RECONCILE(sidebarPrimary)
    RECONCILE(overlay)
    RECONCILE(switchThumb)
    RECONCILE(sliderThumb)
    RECONCILE(button)
    RECONCILE(buttonHover)
    RECONCILE(buttonActive)
    RECONCILE(primaryHover)
    RECONCILE(primaryActive)
    RECONCILE(buttonPrimary)
    RECONCILE(buttonPrimaryHover)
    RECONCILE(buttonPrimaryActive)
    RECONCILE(secondaryHover)
    RECONCILE(secondaryActive)
    RECONCILE(buttonSecondary)
    RECONCILE(buttonSecondaryHover)
    RECONCILE(buttonSecondaryActive)
    RECONCILE(successHover)
    RECONCILE(successActive)
    RECONCILE(buttonSuccess)
    RECONCILE(buttonSuccessHover)
    RECONCILE(buttonSuccessActive)
    RECONCILE(infoHover)
    RECONCILE(infoActive)
    RECONCILE(buttonInfo)
    RECONCILE(buttonInfoHover)
    RECONCILE(buttonInfoActive)
    RECONCILE(warningHover)
    RECONCILE(warningActive)
    RECONCILE(buttonWarning)
    RECONCILE(buttonWarningHover)
    RECONCILE(buttonWarningActive)
    RECONCILE(dangerHover)
    RECONCILE(dangerActive)
    RECONCILE(buttonDanger)
    RECONCILE(buttonDangerHover)
    RECONCILE(buttonDangerActive)
    RECONCILE(accordion)
    RECONCILE(dropTarget)
    RECONCILE(list)
    RECONCILE(listEven)
    RECONCILE(listHead)
    RECONCILE(listHover)
    RECONCILE(sliderBar)
    RECONCILE(switchBg)
    RECONCILE(tab)
    RECONCILE(tabBarSegmented)
    RECONCILE(tableHover)
    RECONCILE(tiles)
    RECONCILE(scrollbarBg)
    RECONCILE(sidebar)
    RECONCILE(groupBox)
    RECONCILE(descListLabel)
#undef RECONCILE
}

Theme* ThemeBeginUpdate(App* app, bool reloadMode, ThemeUpdateScope* scope) {
    static Theme dummy;
    if (!scope) {
        return &dummy;
    }
    *scope = ThemeUpdateScope{};
    scope->app = app;
    scope->reloadMode = reloadMode;
    AppThemeState* state = ThemeStateOf(app);
    if (!state) {
        dummy = ThemeDefaultLight();
        scope->colorsBefore = dummy;
        scope->tokensBefore = dummy.tokens;
        return &dummy;
    }
    Theme* t = &state->active[(int)state->mode];
    scope->modeBefore = state->mode;
    scope->colorsBefore = *t;
    scope->tokensBefore = t->tokens;
    scope->activeBefore[0] = ThemeRegistryActive(app, ThemeMode::Light);
    scope->activeBefore[1] = ThemeRegistryActive(app, ThemeMode::Dark);
    return t;
}

void ThemeEndUpdate(ThemeUpdateScope* scope) {
    if (!scope || !scope->app) {
        return;
    }
    App* app = scope->app;
    AppThemeState* state = ThemeStateOf(app);
    if (!state) {
        return;
    }
    Theme* t = &state->active[(int)scope->modeBefore];
    ThemeTokensReconcile(t, &scope->colorsBefore, &scope->tokensBefore);
    ThemeMode modeAfter = t->mode;
    bool modeChanged = modeAfter != scope->modeBefore;
    Str activeAfter = ThemeRegistryActive(app, modeAfter);
    bool installedByEdit =
        modeChanged &&
        !base::StrEq(activeAfter, scope->activeBefore[(int)modeAfter]);
    if (modeChanged) {
        if (installedByEdit) {
            // apply_config wrote the other mode onto this slot; move it
            // and put the snapshot back so the two palettes stay apart.
            state->active[(int)modeAfter] = *t;
            state->active[(int)scope->modeBefore] = scope->colorsBefore;
        } else {
            t->mode = scope->modeBefore;
        }
        state->mode = modeAfter;
    }
    // reloadMode is Theme::change: re-apply the registered file even when
    // the mode did not change. A plain mode switch shows the other slot,
    // which already holds that mode's installed palette.
    if (scope->reloadMode && !installedByEdit) {
        const ThemeConfig* cfg =
            ThemeRegistryFind(app, ThemeRegistryActive(app, modeAfter));
        if (cfg) {
            Theme loaded;
            bool dark = modeAfter == ThemeMode::Dark;
            ThemeConfigResolve(&loaded, cfg,
                               dark ? ThemeDefaultDark() : ThemeDefaultLight());
            loaded.mode = modeAfter;
            state->active[(int)modeAfter] = loaded;
            state->mode = modeAfter;
        }
    }
    ThemeDidChange(app, state);
    AppRefreshWindows(app);
}

bool ThemeApplyConfig(App* app, Theme* t, const ThemeConfig* cfg) {
    if (!t || !cfg) {
        return false;
    }
    bool dark = cfg->mode == ThemeMode::Dark;
    ThemeConfigResolve(t, cfg, dark ? ThemeDefaultDark() : ThemeDefaultLight());
    t->mode = cfg->mode;
    ThemeRegistry* registry = RegistryOf(app);
    if (registry) {
        registry->active[(int)cfg->mode] = cfg->name;
    }
    return true;
}

static AppThemeState* ThemeStateOf(const App* app) {
    AppThemeState* state = AppGlobalEnsure<AppThemeState>((App*)app);
    if (state && !state->initialized) {
        state->active[(int)ThemeMode::Light] = ThemeDefaultLight();
        state->active[(int)ThemeMode::Dark] = ThemeDefaultDark();
        state->initialized = true;
        ThemeDidChange((App*)app, state);
    }
    return state;
}

const Theme& ThemeLight(const App* app) {
    AppThemeState* state = ThemeStateOf(app);
    return state ? state->active[(int)ThemeMode::Light] : ThemeDefaultLight();
}

const Theme& ThemeDark(const App* app) {
    AppThemeState* state = ThemeStateOf(app);
    return state ? state->active[(int)ThemeMode::Dark] : ThemeDefaultDark();
}

void ThemeInstall(App* app, ThemeMode mode, const Theme& t) {
    AppThemeState* state = ThemeStateOf(app);
    if (state) {
        state->active[(int)mode] = t;
        state->active[(int)mode].mode = mode;
        ThemeDidChange(app, state);
    }
}

void ThemeSetRadius(App* app, float radius) {
    AppThemeState* state = ThemeStateOf(app);
    if (!state) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        Theme* t = &state->active[i];
        t->radius = radius;
        t->radiusLg = radius > 0 ? radius + 2 : 0;
        // `radius_full` goes with it: a theme that squares its corners squares
        // the avatars and the slider thumbs too, rather than leaving a scatter
        // of permanently round elements behind.
        t->radiusFull = radius > 0 ? kRadiusFull : 0;
    }
    ThemeDidChange(app, state);
}

float ThemeFontSize(const App* app) {
    return app ? ThemeNow(app).fontSize : kDefaultFontSize;
}

void ThemeSetFontSize(App* app, float px) {
    AppThemeState* state = ThemeStateOf(app);
    if (state) {
        float value = px > 0 ? px : kDefaultFontSize;
        state->active[0].fontSize = value;
        state->active[1].fontSize = value;
        ThemeDidChange(app, state);
    }
}

bool ThemeFocusRing(const App* app) {
    return app ? ThemeNow(app).focusRing : true;
}

void ThemeSetFocusRing(App* app, bool on) {
    AppThemeState* state = ThemeStateOf(app);
    if (state) {
        state->active[0].focusRing = on;
        state->active[1].focusRing = on;
        ThemeDidChange(app, state);
    }
}

ScrollbarMode ScrollbarModeNow(const App* app) {
    return app ? ThemeNow(app).scrollbarMode : ScrollbarMode::Scrolling;
}

void ScrollbarModeSet(App* app, ScrollbarMode m) {
    ThemeUpdate(app, [m](Theme* t) {
        if (t) {
            t->scrollbarMode = m;
        }
    });
    AppThemeState* state = ThemeStateOf(app);
    if (state) {
        state->active[0].scrollbarMode = m;
        state->active[1].scrollbarMode = m;
    }
}

void ThemeSet(App* app, ThemeMode mode) {
    ThemeUpdate(app, [mode](Theme* t) {
        if (t) {
            t->mode = mode;
        }
    });
}

ThemeMode ThemeGet(const App* app) {
    AppThemeState* state = ThemeStateOf(app);
    return state ? state->mode : ThemeMode::Light;
}

const Theme& ThemeNow(const App* app) {
    return ThemeGet(app) == ThemeMode::Dark ? ThemeDark(app) : ThemeLight(app);
}

SemanticThemeTokens ThemeSemanticTokens(const Theme& t, float fontSize) {
    SemanticThemeTokens out;
    SemanticColorTokens& c = out.colors;
    c.background = t.background;
    c.foreground = t.foreground;
    // `surface` is the popover pair: the role a thing floating over the page
    // plays, which is the closest the legacy palette has to a raised surface.
    c.surface = t.popover;
    c.surfaceForeground = t.popoverFg;
    c.primary = t.primary;
    c.primaryForeground = t.primaryFg;
    c.secondary = t.secondary;
    c.secondaryForeground = t.secondaryFg;
    c.muted = t.muted;
    c.mutedForeground = t.mutedFg;
    c.accent = t.accent;
    c.accentForeground = t.accentFg;
    c.destructive = t.danger;
    c.destructiveForeground = t.dangerFg;
    c.border = t.border;
    c.input = t.inputBorder;
    c.ring = t.ring;
    // `selection` joined the Base palette when rich text moved down: the
    // wash behind selected text had been a literal in three places.
    c.selection = t.selection;

    out.radius.none = 0;
    out.radius.sm = t.radius / 2.f;
    out.radius.md = t.radius;
    out.radius.lg = t.radiusLg;
    out.radius.xl = t.radius * 2.f;
    // radius_tokens().full is `radius_full()`, not the raw constant: a theme
    // that squares its corners squares the pill tier with them.
    out.radius.full = t.radiusFull;

    // `typography_tokens` overwrites the application base sizes and leaves
    // the source platform-default families and the rest of the scale intact.
    out.typography.sans = t.fontFamily;
    out.typography.mono = t.monoFontFamily;
    out.typography.md.size = fontSize > 0 ? fontSize : t.fontSize;
    out.typography.monoMd.size = t.monoFontSize;

    // `shadow_tokens`: the three elevations at 18% black. Rust gates them on
    // `Theme::shadow`, a flag a theme file can clear; nothing here reads such
    // a flag, so the elevations are always the ones a shadow would use.
    if (t.shadow) {
        out.shadow = SemanticShadowElevations(Rgba8(0, 0, 0, 46));
    }
    return out;
}

void ThemeApplySemanticTokens(Theme* t, const SemanticThemeTokens& tokens) {
    if (!t) {
        return;
    }
    const SemanticColorTokens& c = tokens.colors;
    t->background = c.background;
    t->foreground = c.foreground;
    t->popover = c.surface;
    t->popoverFg = c.surfaceForeground;
    t->primary = c.primary;
    t->primaryFg = c.primaryForeground;
    t->secondary = c.secondary;
    t->secondaryFg = c.secondaryForeground;
    t->muted = c.muted;
    t->mutedFg = c.mutedForeground;
    t->accent = c.accent;
    t->accentFg = c.accentForeground;
    t->danger = c.destructive;
    t->dangerFg = c.destructiveForeground;
    t->border = c.border;
    t->inputBorder = c.input;
    t->ring = c.ring;
    t->selection = c.selection;
    t->tokens.selection = Background(c.selection);
    // The seven tokens Rust writes back beside the flat colours, so a
    // gradient left over from the palette this was applied to does not
    // outlive the colour under it.
    t->tokens.background = Background(c.background);
    t->tokens.popover = Background(c.surface);
    t->tokens.primary = Background(c.primary);
    t->tokens.secondary = Background(c.secondary);
    t->tokens.muted = Background(c.muted);
    t->tokens.accent = Background(c.accent);
    t->tokens.danger = Background(c.destructive);
    t->radius = tokens.radius.md;
    t->radiusLg = tokens.radius.lg;
    t->radiusFull = t->radius > 0 ? kRadiusFull : 0;
    t->fontFamily = tokens.typography.sans;
    t->monoFontFamily = tokens.typography.mono;
    t->fontSize = tokens.typography.md.size;
    t->monoFontSize = tokens.typography.monoMd.size;
    t->shadow = tokens.shadow.sm.len > 0 || tokens.shadow.md.len > 0 ||
                tokens.shadow.lg.len > 0;
    // Spacing and individual text roles have no legacy storage; callers that
    // need the complete resolved snapshot retain SemanticThemeTokens.
}

void ThemeSyncBase(App* app) {
    if (!app) {
        return;
    }
    const Theme& ui = ThemeNow(app);
    BaseTheme base;
    base.appearance = ui.mode == ThemeMode::Dark ? BaseThemeAppearance::Dark
                                                 : BaseThemeAppearance::Light;
    base.tokens = ThemeSemanticTokens(ui, ThemeFontSize(app));
    base.scrollbar.mode = ScrollbarModeNow(app);
    base.scrollbar.motion = ScrollbarMotionFor(base.scrollbar.mode);

    ScrollbarStyles& styles = base.scrollbar.styles;
    styles.track.background = Background(ui.scrollbarBg);
    styles.track.hasBackground = true;
    styles.trackHover.background = Background(ui.scrollbarBg);
    styles.trackHover.hasBackground = true;
    styles.trackActive.background = Background(ui.scrollbarBg);
    styles.trackActive.border = ui.border;
    styles.trackActive.hasBackground = true;
    styles.trackActive.hasBorder = true;

    styles.thumb.background = ui.tokens.scrollbarThumb;
    styles.thumb.radius = ui.radius;
    styles.thumb.hasBackground = true;
    styles.thumb.hasRadius = true;
    styles.thumbHover.background = ui.tokens.scrollbarThumbHover;
    styles.thumbHover.radius = ui.radius;
    styles.thumbHover.hasBackground = true;
    styles.thumbHover.hasRadius = true;
    styles.thumbActive = styles.thumbHover;
    if (IsMobile()) {
        // The resting thumb is a 3px pill 2px from the edge, matching the
        // iOS/Android indicator. Hover and drag keep Base's 6px / 8px
        // widths and 4px inset so a grabbed thumb still grows.
        styles.thumb.width = 3.f;
        styles.thumb.inset = 2.f;
        styles.thumb.radius = kRadiusFull;
        styles.thumb.hasWidth = true;
        styles.thumb.hasInset = true;
        styles.thumbHover.width = 6.f;
        styles.thumbHover.inset = 4.f;
        styles.thumbHover.hasWidth = true;
        styles.thumbHover.hasInset = true;
        styles.thumbActive.width = 8.f;
        styles.thumbActive.inset = 4.f;
        styles.thumbActive.hasWidth = true;
        styles.thumbActive.hasInset = true;
    }

    base.resizable.handle = ui.border;
    base.resizable.activeHandle = ui.dragBorder;
    base.resizable.hasHandle = true;
    base.resizable.hasActiveHandle = true;
    BaseThemeSet(app, base);
    // `install_text_view_defaults`: rich text lives in Base and reads no
    // theme, so the themed palette and the themed syntax highlighter are
    // handed to it here, on the same beat the Base theme is replaced.
    component::TextViewInstallDefaults(app);
}

#if GPUI_OS_WINDOWS
static const char kSep = '\\';
#else
static const char kSep = '/';
#endif

// A whole text file, or an empty Str. Reading a file is plain stdio here;
// the asset loader's own reader answers for one relative path, and this walks
// a directory it has already resolved.
static Str ReadTextFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return {};
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (1 << 22)) {
        fclose(f);
        return {};
    }
    char* buf = AllocArray<char>((int)n + 1);
    if (!buf) {
        fclose(f);
        return {};
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = 0;
    return Str(buf, (int)got);
}

// ─── colour grammar — crates/ui/src/theme/color.rs ───────────────────────

static float Clamp01f(float v) {
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

static int HexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// gpui::Rgba::try_from(&str): three, four, six or eight hex digits after the
// hash, where the short forms double each digit.
static bool ParseHex(Str s, Rgba* out) {
    const char* p = s.s + 1;
    int n = len(s) - 1;
    if (n != 3 && n != 4 && n != 6 && n != 8) {
        return false;
    }
    int d[8];
    for (int i = 0; i < n; i++) {
        d[i] = HexDigit(p[i]);
        if (d[i] < 0) {
            return false;
        }
    }
    int v[4] = {0, 0, 0, 255};
    if (n <= 4) {
        for (int i = 0; i < n; i++) {
            v[i] = d[i] * 16 + d[i];
        }
    } else {
        for (int i = 0; i < n / 2; i++) {
            v[i] = d[(size_t)i * 2] * 16 + d[(size_t)i * 2 + 1];
        }
    }
    *out = Rgba8((uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3]);
    return true;
}

// ColorName::scale: the column for `scale`, or the one for 500 when no column
// carries that number.
static bool ScaleOf(const ShadcnScale* hue, int scale, Rgba* out) {
    int at = -1;
    int fallback = -1;
    for (int i = 0; i < kNumShadcnColumns; i++) {
        if (kShadcnScaleNums[i] == scale) {
            at = i;
        }
        if (kShadcnScaleNums[i] == 500) {
            fallback = i;
        }
    }
    if (at < 0) {
        at = fallback;
    }
    if (at < 0) {
        return false;
    }
    *out = RgbaHex(hue->hex[at]);
    return true;
}

// The integer at [s, s+len), or -1 for anything that is not one. Rust uses
// `parse::<usize>()`, which is equally all-or-nothing.
static int ParseUint(const char* s, int len) {
    if (len <= 0 || len > 9) {
        return -1;
    }
    int v = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

static float ParseFloatOr(const char* s, int len, float fallback) {
    if (len <= 0 || len >= 32) {
        return fallback;
    }
    TempStr buf = StrDupTemp(Str(s, len));
    char* end = nullptr;
    float v = (float)strtod(buf.s, &end);
    if (!end || *end) {
        return fallback;
    }
    return v;
}

bool ThemeParseColor(Str s, Rgba* out) {
    if (!s.s || len(s) <= 0 || !out) {
        return false;
    }
    if (s.s[0] == '#') {
        return ParseHex(s, out);
    }
    // name[-scale][/opacity]. Rust splits on the first `-` and the first `/`,
    // so a name with neither is the whole string and takes scale 500.
    int dash = -1;
    int slash = -1;
    for (int i = 0; i < len(s); i++) {
        if (s.s[i] == '-' && dash < 0 && slash < 0) {
            dash = i;
        } else if (s.s[i] == '/' && slash < 0) {
            slash = i;
        }
    }
    int nameLen = dash >= 0 ? dash : (slash >= 0 ? slash : len(s));
    Str name = Str(s.s, nameLen);
    if (len(name) <= 0) {
        return false;
    }
    int scale = 500;
    if (dash >= 0) {
        int end = slash >= 0 ? slash : len(s);
        scale = ParseUint(s.s + dash + 1, end - dash - 1);
        if (scale < 0) {
            // `parse::<usize>().ok()` leaving None is the same as no scale.
            scale = 500;
        }
    }
    Rgba c;
    if (base::StrEqI(name, "white")) {
        c = RgbaHex(kShadcnWhite);
    } else if (base::StrEqI(name, "black")) {
        c = RgbaHex(kShadcnBlack);
    } else {
        const ShadcnScale* hue = nullptr;
        for (int i = 0; i < kNumShadcnScales; i++) {
            if (base::StrEqI(name, kShadcnScales[i].name)) {
                hue = &kShadcnScales[i];
                break;
            }
        }
        if (!hue || !ScaleOf(hue, scale, &c)) {
            return false;
        }
    }
    if (slash >= 0) {
        float pct = ParseFloatOr(s.s + slash + 1, len(s) - slash - 1, -1.f);
        if (pct > 100.f) {
            return false;
        }
        if (pct >= 0) {
            c = RgbaOpacity(c, pct / 100.f);
        }
    }
    *out = c;
    return true;
}

// ─── the gradient grammar — color.rs parse_linear_gradient ───────────────

static const ColorName kPublicColorNames[] = {
    ColorName::Neutral, ColorName::Gray,   ColorName::Red,    ColorName::Orange,
    ColorName::Amber,   ColorName::Yellow, ColorName::Lime,   ColorName::Green,
    ColorName::Emerald, ColorName::Teal,   ColorName::Cyan,   ColorName::Sky,
    ColorName::Blue,    ColorName::Indigo, ColorName::Violet, ColorName::Purple,
    ColorName::Fuchsia, ColorName::Pink,   ColorName::Rose,
};

static const char* ColorNameText(ColorName name) {
    switch (name) {
        case ColorName::White:
            return "white";
        case ColorName::Black:
            return "black";
        case ColorName::Neutral:
            return "neutral";
        case ColorName::Gray:
            return "gray";
        case ColorName::Red:
            return "red";
        case ColorName::Orange:
            return "orange";
        case ColorName::Amber:
            return "amber";
        case ColorName::Yellow:
            return "yellow";
        case ColorName::Lime:
            return "lime";
        case ColorName::Green:
            return "green";
        case ColorName::Emerald:
            return "emerald";
        case ColorName::Teal:
            return "teal";
        case ColorName::Cyan:
            return "cyan";
        case ColorName::Sky:
            return "sky";
        case ColorName::Blue:
            return "blue";
        case ColorName::Indigo:
            return "indigo";
        case ColorName::Violet:
            return "violet";
        case ColorName::Purple:
            return "purple";
        case ColorName::Fuchsia:
            return "fuchsia";
        case ColorName::Pink:
            return "pink";
        case ColorName::Rose:
            return "rose";
    }
    return "black";
}

const ColorName* ColorNameAll(int* count) {
    if (count) {
        *count =
            (int)(sizeof(kPublicColorNames) / sizeof(kPublicColorNames[0]));
    }
    return kPublicColorNames;
}

bool ColorNameParse(Str value, ColorName* out) {
    if (!out) return false;
    if (base::StrEqI(value, "white")) {
        *out = ColorName::White;
        return true;
    }
    if (base::StrEqI(value, "black")) {
        *out = ColorName::Black;
        return true;
    }
    for (int i = 0;
         i < (int)(sizeof(kPublicColorNames) / sizeof(kPublicColorNames[0]));
         i++) {
        ColorName candidate = kPublicColorNames[i];
        if (base::StrEqI(value, ColorNameText(candidate))) {
            *out = candidate;
            return true;
        }
    }
    return false;
}

Rgba ColorNameScale(ColorName name, int scale) {
    if (name == ColorName::White) return ThemeWhite();
    if (name == ColorName::Black) return ThemeBlack();
    const char* value = ColorNameText(name);
    for (int i = 0; i < kNumShadcnScales; i++) {
        if (!base::StrEqI(Str(kShadcnScales[i].name), value)) continue;
        Rgba out;
        if (ScaleOf(&kShadcnScales[i], scale, &out)) return out;
        break;
    }
    return ThemeBlack();
}

Rgba ThemeHsl(float hueDegrees, float saturationPercent,
              float lightnessPercent) {
    return RgbaHsla(hueDegrees / 360.f, saturationPercent / 100.f,
                    lightnessPercent / 100.f, 1.f);
}

Rgba ThemeOklch(float lightness, float chroma, float hueDegrees) {
    return RgbaOklch(lightness, chroma, hueDegrees);
}

Rgba ThemeBlack() {
    return RgbaHex(kShadcnBlack);
}

Rgba ThemeWhite() {
    return RgbaHex(kShadcnWhite);
}

// split_top_level_commas: a comma inside parentheses belongs to whatever is
// there — `rgb(1, 2, 3)` is one stop, not three.
static int SplitTopLevelCommas(Str inner, Str* out, int cap) {
    int n = 0;
    int depth = 0;
    int start = 0;
    for (int i = 0; i < len(inner); i++) {
        char c = inner.s[i];
        if (c == '(') {
            depth++;
        } else if (c == ')') {
            if (depth > 0) {
                depth--;
            }
        } else if (c == ',' && depth == 0) {
            if (n < cap) {
                out[n] = base::StrTrimAscii(Str(inner.s + start, i - start));
            }
            n++;
            start = i + 1;
        }
    }
    if (n < cap) {
        out[n] = base::StrTrimAscii(Str(inner.s + start, len(inner) - start));
    }
    n++;
    return n;
}

// parse_linear_gradient_direction: the eight `to ..` keywords, as degrees.
static bool ParseGradientDirection(Str dir, float* out) {
    bool top = false, right = false, bottom = false, left = false;
    int i = 0;
    while (i < len(dir)) {
        while (i < len(dir) && (dir.s[i] == ' ' || dir.s[i] == '\t')) {
            i++;
        }
        int start = i;
        while (i < len(dir) && dir.s[i] != ' ' && dir.s[i] != '\t') {
            i++;
        }
        if (i == start) {
            break;
        }
        Str word = Str(dir.s + start, i - start);
        if (base::StrEqI(word, "top")) {
            top = true;
        } else if (base::StrEqI(word, "right")) {
            right = true;
        } else if (base::StrEqI(word, "bottom")) {
            bottom = true;
        } else if (base::StrEqI(word, "left")) {
            left = true;
        } else {
            return false;
        }
    }
    if (top && !right && !bottom && !left) {
        *out = 0.f;
    } else if (!top && right && !bottom && !left) {
        *out = 90.f;
    } else if (!top && !right && bottom && !left) {
        *out = 180.f;
    } else if (!top && !right && !bottom && left) {
        *out = 270.f;
    } else if (top && right && !bottom && !left) {
        *out = 45.f;
    } else if (!top && right && bottom && !left) {
        *out = 135.f;
    } else if (!top && !right && bottom && left) {
        *out = 225.f;
    } else if (top && !right && !bottom && left) {
        *out = 315.f;
    } else {
        return false;
    }
    return true;
}

// parse_linear_gradient_angle: `135deg`, or `to bottom right`.
static bool ParseGradientAngle(Str angle, float* out) {
    angle = base::StrTrimAscii(angle);
    if (len(angle) > 3 &&
        base::StrEqI(Str(angle.s + len(angle) - 3, 3), "deg")) {
        Str num = base::StrTrimAscii(Str(angle.s, len(angle) - 3));
        float deg = ParseFloatOr(num.s, len(num), 1e30f);
        if (deg >= 1e29f) {
            return false;
        }
        // rem_euclid(360): a negative angle comes back positive.
        deg = fmodf(deg, 360.f);
        if (deg < 0) {
            deg += 360.f;
        }
        *out = deg;
        return true;
    }
    if (base::StrStartsWithI(angle, "to ")) {
        return ParseGradientDirection(
            base::StrTrimAscii(Str(angle.s + 3, len(angle) - 3)), out);
    }
    return false;
}

// parse_linear_color_stop: a colour, and optionally where along the line it
// sits — `red-500 25%`. Without one it takes the end it was given.
static bool ParseColorStop(Str stop, float defaultPct, ColorStop* out) {
    stop = base::StrTrimAscii(stop);
    if (len(stop) <= 0) {
        return false;
    }
    float pct = defaultPct;
    if (stop.s[len(stop) - 1] == '%') {
        // The percentage is the last whitespace-separated word.
        int i = len(stop) - 1;
        while (i > 0 && stop.s[i - 1] != ' ' && stop.s[i - 1] != '\t') {
            i--;
        }
        float v = ParseFloatOr(stop.s + i, len(stop) - 1 - i, 1e30f);
        if (v >= 1e29f) {
            return false;
        }
        pct = Clamp01f(v / 100.f);
        stop = base::StrTrimAscii(Str(stop.s, i));
        if (len(stop) <= 0) {
            return false;
        }
    }
    Rgba c;
    if (!ThemeParseColor(stop, &c)) {
        return false;
    }
    out->color = c;
    out->percentage = pct;
    return true;
}

static bool ParseLinearGradient(Str s, Background* out) {
    s = base::StrTrimAscii(s);
    if (!base::StrStartsWithI(s, "linear-gradient(") ||
        s.s[len(s) - 1] != ')') {
        return false;
    }
    const int kPrefix = 16; // "linear-gradient("
    Str inner = Str(s.s + kPrefix, len(s) - kPrefix - 1);
    Str parts[4] = {};
    int n = SplitTopLevelCommas(inner, parts, 4);
    float angle = 180.f;
    Str fromS = {}, toS = {};
    if (n == 2) {
        fromS = parts[0];
        toS = parts[1];
    } else if (n == 3) {
        if (!ParseGradientAngle(parts[0], &angle)) {
            return false;
        }
        fromS = parts[1];
        toS = parts[2];
    } else {
        // Rust takes exactly two stops; anything else is an error, which
        // leaves the token on its fallback.
        return false;
    }
    ColorStop from = {}, to = {};
    if (!ParseColorStop(fromS, 0.f, &from) || !ParseColorStop(toS, 1.f, &to)) {
        return false;
    }
    *out = BackgroundLinear(angle, from, to);
    return true;
}

bool ThemeParseBackground(Str s, Background* out) {
    if (!s.s || len(s) <= 0 || !out) {
        return false;
    }
    Rgba c;
    if (ThemeParseColor(s, &c)) {
        *out = Background(c);
        return true;
    }
    return ParseLinearGradient(s, out);
}

// ─── the colour maths the fallbacks are written in ───────────────────────

// The colour maths lives in gpui.cpp now — a palette written in code derives
// its own tokens with it — and these are the names schema.rs's fallbacks are
// written in here.
static Rgba Transparent() {
    return RgbaTransparent();
}
static Rgba Blend(Rgba base, Rgba over) {
    return RgbaBlend(base, over);
}
static Rgba Lighten(Rgba c, float amount) {
    return RgbaLighten(c, amount);
}
static Rgba Darken(Rgba c, float amount) {
    return RgbaDarken(c, amount);
}
static Rgba MixOklab(Rgba a, Rgba b, float factor) {
    return RgbaMixOklab(a, b, factor);
}

// ─── apply_config — crates/ui/src/theme/schema.rs ────────────────────────

// apply_color! / apply_background_color!: the config's value for `key` when
// it has one the grammar takes, and `fb` otherwise. The two macros differ in
// Rust only by whether a gradient is allowed through, and a `Theme` field
// here is one colour, so they are the same read.
// The keys some theme files still spell differently from the serde names the
// schema declares (`chart_1` vs `chart.1`, `chart_bullish` vs `chart.bullish`,
// `drag_border` vs `drag.border`). default-theme.json now uses the dotted
// keys; the aliases keep older files applying. The second name is read after
// the schema's own.
//
// `description_list_label.background` and `.foreground` were on this list and
// are not any more. They are the two where reading the file's spelling makes
// a *visible* difference from the app this tree is a port of: a label painted
// in the foreground rather than in muted_foreground, which is what the Rust
// gallery shows on every row of its DescriptionList page. Matching the
// reference wins over honouring a key it ignores.
static const char* const kKeyAliases[][2] = {
    {"chart.1", "chart_1"},
    {"chart.2", "chart_2"},
    {"chart.3", "chart_3"},
    {"chart.4", "chart_4"},
    {"chart.5", "chart_5"},
    {"chart.bullish", "chart_bullish"},
    {"chart.bearish", "chart_bearish"},
    {"drag.border", "drag_border"},
    {"progress.bar.background", "progress_bar.background"},
};

// The string a token's key names, under the schema's spelling or the one
// default-theme.json uses. Null for a key the file leaves out, and for a
// value that is not a string.
static const JsonValue* FindColor(const JsonValue* colors, const char* key) {
    const JsonValue* v = JsonGet(colors, key);
    if (!v) {
        for (size_t i = 0; i < sizeof(kKeyAliases) / sizeof(kKeyAliases[0]);
             i++) {
            if (StrEq(Str(kKeyAliases[i][0]), key)) {
                v = JsonGet(colors, kKeyAliases[i][1]);
                break;
            }
        }
    }
    return (v && v->kind == JsonKind::String) ? v : nullptr;
}

static Rgba Pick(const JsonValue* colors, const char* key, Rgba fb) {
    const JsonValue* v = FindColor(colors, key);
    if (!v) {
        return fb;
    }
    Rgba c;
    if (!ThemeParseColor(v->str, &c)) {
        return fb;
    }
    return c;
}

// apply_background_color!: the same read, with a gradient allowed through.
static Background PickBg(const JsonValue* colors, const char* key,
                         Background fb) {
    const JsonValue* v = FindColor(colors, key);
    if (!v) {
        return fb;
    }
    Background b;
    if (!ThemeParseBackground(v->str, &b)) {
        return fb;
    }
    return b;
}

// The token takes the fill; the flat field beside it takes the token's
// colour, which for a gradient is its first stop.
static void SetToken(Rgba* flat, Background* tok, Background b) {
    *flat = b.color;
    *tok = b;
}

// The last step of apply_config: a highlight that is allowed to cover a row
// of text can never be more opaque than this, however the file spells it.
static Rgba ClampAlpha(Rgba c, float max) {
    // Truncated, like every other float→byte in the palette.
    uint8_t cap = (uint8_t)(Clamp01f(max) * 255.f);
    if (c.a <= cap) {
        return c;
    }
    return Rgba8(c.r, c.g, c.b, cap);
}

// The same cap over a token. A value the file named has each of its stops
// capped on its own, so a bright second stop cannot push the highlight past
// the cap; one that came from a fallback is scaled as a whole, which is what
// `Background::opacity` does and what Rust's `clamp_alpha` falls back to.
static void ClampToken(Rgba* flat, Background* tok, bool raw, float max) {
    float a = (float)tok->color.a / 255.f;
    float target = a < max ? a : max;
    Background b = raw ? BackgroundClampAlpha(*tok, max)
                       : BackgroundOpacity(*tok, a > 0 ? target / a : 1.f);
    b.color = ClampAlpha(tok->color, max);
    *tok = b;
    *flat = b.color;
}

bool ThemeConfigNames(const ThemeConfig* cfg, const char* key) {
    // The aliases count: Rust's set is built from the config *struct* it
    // deserialized the file into, so a key the file spells the old way is in
    // it under the new name.
    return cfg && cfg->colors && FindColor(cfg->colors, key) != nullptr;
}

void ThemeConfigResolve(Theme* out, const ThemeConfig* cfg, const Theme& base) {
    if (!out || !cfg) {
        return;
    }
    *out = base;
    out->mode = cfg->mode;
    const JsonValue* c = cfg->colors;
    bool dark = cfg->mode == ThemeMode::Dark;
    // The two constants every button and hover fallback is written against.
    const float activeDarken = dark ? 0.2f : 0.1f;
    const float hoverOpacity = 0.9f;
    const Rgba clear = Transparent();

    SetToken(&out->background, &out->tokens.background,
             PickBg(c, "background", base.tokens.background));

    // The base colours, which the semantic ones fall back to, and their
    // `.light` halves — the background blended with 80% of the base where the
    // file does not name one. The colour picker shows all twelve.
    out->red = Pick(c, "base.red", base.red);
    out->green = Pick(c, "base.green", base.green);
    out->blue = Pick(c, "base.blue", base.blue);
    out->magenta = Pick(c, "base.magenta", base.magenta);
    out->yellow = Pick(c, "base.yellow", base.yellow);
    out->cyan = Pick(c, "base.cyan", base.cyan);
    out->redLight = Pick(c, "base.red.light",
                         Blend(out->background, RgbaOpacity(out->red, 0.8f)));
    out->greenLight =
        Pick(c, "base.green.light",
             Blend(out->background, RgbaOpacity(out->green, 0.8f)));
    out->blueLight = Pick(c, "base.blue.light",
                          Blend(out->background, RgbaOpacity(out->blue, 0.8f)));
    out->magentaLight =
        Pick(c, "base.magenta.light",
             Blend(out->background, RgbaOpacity(out->magenta, 0.8f)));
    out->yellowLight =
        Pick(c, "base.yellow.light",
             Blend(out->background, RgbaOpacity(out->yellow, 0.8f)));
    out->cyanLight = Pick(c, "base.cyan.light",
                          Blend(out->background, RgbaOpacity(out->cyan, 0.8f)));

    out->border = Pick(c, "border", base.border);
    out->foreground = Pick(c, "foreground", base.foreground);
    out->inputBorder = Pick(c, "input.border", out->border);
    SetToken(&out->muted, &out->tokens.muted,
             PickBg(c, "muted.background", base.tokens.muted));
    out->mutedFg = Pick(c, "muted.foreground",
                        Blend(out->muted, RgbaOpacity(out->foreground, 0.7f)));

    // Theme::input_background(): a dark input sits on its own border mixed
    // toward transparent, a light one on the window background.
    out->inputBg =
        dark ? MixOklab(out->inputBorder, clear, 0.3f) : out->background;

    SetToken(&out->primary, &out->tokens.primary,
             PickBg(c, "primary.background", base.tokens.primary));
    out->primaryFg = Pick(c, "primary.foreground", out->foreground);
    SetToken(&out->primaryHover, &out->tokens.primaryHover,
             PickBg(c, "primary.hover.background",
                    Blend(out->background,
                          RgbaOpacity(out->primary, hoverOpacity))));
    SetToken(&out->primaryActive, &out->tokens.primaryActive,
             PickBg(c, "primary.active.background",
                    Darken(out->primary, activeDarken)));

    SetToken(&out->secondary, &out->tokens.secondary,
             PickBg(c, "secondary.background", base.tokens.secondary));
    out->secondaryFg = Pick(c, "secondary.foreground", out->foreground);
    SetToken(&out->secondaryHover, &out->tokens.secondaryHover,
             PickBg(c, "secondary.hover.background",
                    Blend(out->background,
                          RgbaOpacity(out->secondary, hoverOpacity))));
    SetToken(&out->secondaryActive, &out->tokens.secondaryActive,
             PickBg(c, "secondary.active.background",
                    Darken(out->secondary, activeDarken)));

    SetToken(&out->success, &out->tokens.success,
             PickBg(c, "success.background", out->green));
    out->successFg = Pick(c, "success.foreground", out->primaryFg);
    SetToken(&out->info, &out->tokens.info,
             PickBg(c, "info.background", out->cyan));
    out->infoFg = Pick(c, "info.foreground", out->primaryFg);
    SetToken(&out->warning, &out->tokens.warning,
             PickBg(c, "warning.background", out->yellow));
    out->warningFg = Pick(c, "warning.foreground", out->primaryFg);

    SetToken(&out->accent, &out->tokens.accent,
             PickBg(c, "accent.background", out->tokens.secondary));
    out->accentFg = Pick(c, "accent.foreground", out->foreground);
    Rgba accentFg = out->accentFg;
    SetToken(&out->groupBox, &out->tokens.groupBox,
             PickBg(c, "group_box.background",
                    Blend(out->background,
                          RgbaOpacity(out->secondary, dark ? 0.3f : 0.4f))));
    out->groupBoxFg = Pick(c, "group_box.foreground", out->foreground);
    out->caret = Pick(c, "caret", out->primary);

    out->chart1 = Pick(c, "chart.1", Lighten(out->blue, 0.4f));
    out->chart2 = Pick(c, "chart.2", Lighten(out->blue, 0.2f));
    out->chart3 = Pick(c, "chart.3", out->blue);
    out->chart4 = Pick(c, "chart.4", Darken(out->blue, 0.2f));
    out->chart5 = Pick(c, "chart.5", Darken(out->blue, 0.4f));
    out->chartBullish = Pick(c, "chart.bullish", out->green);
    out->chartBearish = Pick(c, "chart.bearish", out->red);

    SetToken(&out->danger, &out->tokens.danger,
             PickBg(c, "danger.background", out->red));
    out->dangerFg = Pick(c, "danger.foreground", out->primaryFg);
    SetToken(&out->descListLabel, &out->tokens.descListLabel,
             PickBg(c, "description_list.label.background",
                    Blend(out->background, RgbaOpacity(out->border, 0.2f))));
    out->descListLabelFg =
        Pick(c, "description_list.label.foreground", out->mutedFg);
    out->dragBorder = Pick(c, "drag.border", RgbaOpacity(out->primary, 0.65f));

    SetToken(&out->list, &out->tokens.list,
             PickBg(c, "list.background", out->tokens.background));
    SetToken(&out->listActive, &out->tokens.listActive,
             PickBg(c, "list.active.background",
                    Blend(out->background, RgbaOpacity(out->primary, 0.1f))));
    out->listActiveBorder =
        Pick(c, "list.active.border",
             Blend(out->background, RgbaOpacity(out->primary, 0.6f)));
    SetToken(&out->listEven, &out->tokens.listEven,
             PickBg(c, "list.even.background", out->tokens.list));
    SetToken(&out->listHead, &out->tokens.listHead,
             PickBg(c, "list.head.background", out->tokens.list));
    SetToken(
        &out->listHover, &out->tokens.listHover,
        PickBg(c, "list.hover.background", RgbaOpacity(out->accent, 0.6f)));

    SetToken(&out->popover, &out->tokens.popover,
             PickBg(c, "popover.background", out->tokens.background));
    out->popoverFg = Pick(c, "popover.foreground", out->foreground);

    SetToken(&out->progress, &out->tokens.progress,
             PickBg(c, "progress.bar.background", out->tokens.primary));
    out->ring = Pick(c, "ring", out->blue);
    SetToken(&out->scrollbarThumb, &out->tokens.scrollbarThumb,
             PickBg(c, "scrollbar.thumb.background", out->tokens.accent));
    SetToken(&out->scrollbarThumbHover, &out->tokens.scrollbarThumbHover,
             PickBg(c, "scrollbar.thumb.hover.background",
                    out->tokens.scrollbarThumb));
    SetToken(&out->scrollbarBg, &out->tokens.scrollbarBg,
             PickBg(c, "scrollbar.background", out->tokens.background));
    SetToken(&out->selection, &out->tokens.selection,
             PickBg(c, "selection.background", out->tokens.primary));

    SetToken(&out->sidebar, &out->tokens.sidebar,
             PickBg(c, "sidebar.background",
                    Blend(out->background, RgbaOpacity(out->border, 0.15f))));
    SetToken(&out->sidebarAccent, &out->tokens.sidebarAccent,
             PickBg(c, "sidebar.accent.background", out->tokens.accent));
    out->sidebarAccentFg = Pick(c, "sidebar.accent.foreground", accentFg);
    out->sidebarBorder = Pick(c, "sidebar.border", out->border);
    out->sidebarFg = Pick(c, "sidebar.foreground", out->foreground);
    SetToken(&out->sidebarPrimary, &out->tokens.sidebarPrimary,
             PickBg(c, "sidebar.primary.background", out->tokens.primary));
    out->sidebarPrimaryFg =
        Pick(c, "sidebar.primary.foreground", out->primaryFg);

    SetToken(&out->skeleton, &out->tokens.skeleton,
             PickBg(c, "skeleton.background", out->tokens.secondary));
    SetToken(&out->tabActiveBg, &out->tokens.tabActiveBg,
             PickBg(c, "tab.active.background", out->tokens.background));
    out->tabActiveFg = Pick(c, "tab.active.foreground", out->foreground);
    SetToken(&out->tabBar, &out->tokens.tabBar,
             PickBg(c, "tab_bar.background", out->tokens.background));
    out->tabFg = Pick(c, "tab.foreground", out->foreground);

    SetToken(&out->tableBg, &out->tokens.tableBg,
             PickBg(c, "table.background", out->tokens.list));
    SetToken(&out->tableActive, &out->tokens.tableActive,
             PickBg(c, "table.active.background", out->tokens.listActive));
    out->tableActiveBorder =
        Pick(c, "table.active.border", out->listActiveBorder);
    SetToken(&out->tableEven, &out->tokens.tableEven,
             PickBg(c, "table.even.background", out->tokens.listEven));
    SetToken(&out->tableHead, &out->tokens.tableHead,
             PickBg(c, "table.head.background", out->tokens.listHead));
    out->tableHeadFg = Pick(c, "table.head.foreground", out->mutedFg);
    SetToken(&out->tableFoot, &out->tokens.tableFoot,
             PickBg(c, "table.foot.background", out->tokens.listHead));
    out->tableFootFg = Pick(c, "table.foot.foreground", out->mutedFg);
    out->tableRowBorder = Pick(c, "table.row.border", out->border);

    SetToken(&out->titleBar, &out->tokens.titleBar,
             PickBg(c, "title_bar.background", out->tokens.background));
    out->titleBarBorder = Pick(c, "title_bar.border", out->border);
    out->statusBarBorder = Pick(c, "status_bar.border", out->titleBarBorder);
    SetToken(&out->overlay, &out->tokens.overlay,
             PickBg(c, "overlay", base.tokens.overlay));

    // status_bar falls back to the title bar, and the switch and slider
    // thumbs to the window background — the three tokens this tree keeps only
    // so a theme that spells one as a gradient gets one.
    SetToken(&out->statusBar, &out->tokens.statusBar,
             PickBg(c, "status_bar.background", out->tokens.titleBar));
    SetToken(&out->switchThumb, &out->tokens.switchThumb,
             PickBg(c, "switch.thumb.background", out->tokens.background));
    SetToken(&out->sliderThumb, &out->tokens.sliderThumb,
             PickBg(c, "slider.thumb.background", out->tokens.background));

    // The rest of ThemeColor, each on the key schema.rs reads it from and the
    // fallback it falls back to. `ThemeFillDerived` has already put every one
    // of them on that fallback, so what is left here is the file's own word
    // over the top — read in schema.rs's order, since a few of them fall back
    // to one another.
    ThemeFillDerived(out, dark);

    SetToken(&out->button, &out->tokens.button,
             PickBg(c, "button.background", out->tokens.button));
    out->buttonFg = Pick(c, "button.foreground", out->foreground);
    SetToken(&out->buttonHover, &out->tokens.buttonHover,
             PickBg(c, "button.hover.background", out->tokens.buttonHover));
    SetToken(&out->buttonActive, &out->tokens.buttonActive,
             PickBg(c, "button.active.background", out->tokens.buttonActive));
    SetToken(&out->buttonPrimary, &out->tokens.buttonPrimary,
             PickBg(c, "button.primary.background", out->tokens.primary));
    out->buttonPrimaryFg = Pick(c, "button.primary.foreground", out->primaryFg);
    SetToken(
        &out->buttonPrimaryHover, &out->tokens.buttonPrimaryHover,
        PickBg(c, "button.primary.hover.background", out->tokens.primaryHover));
    SetToken(&out->buttonPrimaryActive, &out->tokens.buttonPrimaryActive,
             PickBg(c, "button.primary.active.background",
                    out->tokens.primaryActive));
    SetToken(&out->buttonSecondary, &out->tokens.buttonSecondary,
             PickBg(c, "button.secondary.background", out->tokens.secondary));
    out->buttonSecondaryFg =
        Pick(c, "button.secondary.foreground", out->secondaryFg);
    SetToken(&out->buttonSecondaryHover, &out->tokens.buttonSecondaryHover,
             PickBg(c, "button.secondary.hover.background",
                    out->tokens.secondaryHover));
    SetToken(&out->buttonSecondaryActive, &out->tokens.buttonSecondaryActive,
             PickBg(c, "button.secondary.active.background",
                    out->tokens.secondaryActive));

    SetToken(&out->successHover, &out->tokens.successHover,
             PickBg(c, "success.hover.background",
                    Blend(out->background,
                          RgbaOpacity(out->success, hoverOpacity))));
    SetToken(&out->successActive, &out->tokens.successActive,
             PickBg(c, "success.active.background",
                    Darken(out->success, activeDarken)));
    SetToken(&out->buttonSuccess, &out->tokens.buttonSuccess,
             PickBg(c, "button.success.background",
                    MixOklab(out->success, clear, 0.2f)));
    out->buttonSuccessFg = Pick(c, "button.success.foreground", out->success);
    SetToken(&out->buttonSuccessHover, &out->tokens.buttonSuccessHover,
             PickBg(c, "button.success.hover.background",
                    MixOklab(out->success, clear, 0.3f)));
    SetToken(&out->buttonSuccessActive, &out->tokens.buttonSuccessActive,
             PickBg(c, "button.success.active.background",
                    MixOklab(out->success, clear, 0.4f)));

    SetToken(
        &out->infoHover, &out->tokens.infoHover,
        PickBg(c, "info.hover.background",
               Blend(out->background, RgbaOpacity(out->info, hoverOpacity))));
    SetToken(
        &out->infoActive, &out->tokens.infoActive,
        PickBg(c, "info.active.background", Darken(out->info, activeDarken)));
    SetToken(
        &out->buttonInfo, &out->tokens.buttonInfo,
        PickBg(c, "button.info.background", MixOklab(out->info, clear, 0.2f)));
    out->buttonInfoFg = Pick(c, "button.info.foreground", out->info);
    SetToken(&out->buttonInfoHover, &out->tokens.buttonInfoHover,
             PickBg(c, "button.info.hover.background",
                    MixOklab(out->info, clear, 0.3f)));
    SetToken(&out->buttonInfoActive, &out->tokens.buttonInfoActive,
             PickBg(c, "button.info.active.background",
                    MixOklab(out->info, clear, 0.4f)));

    SetToken(&out->warningHover, &out->tokens.warningHover,
             PickBg(c, "warning.hover.background",
                    Blend(out->background,
                          RgbaOpacity(out->warning, hoverOpacity))));
    SetToken(
        &out->warningActive, &out->tokens.warningActive,
        PickBg(c, "warning.active.background",
               Blend(out->background, Darken(out->warning, activeDarken))));
    SetToken(&out->buttonWarning, &out->tokens.buttonWarning,
             PickBg(c, "button.warning.background",
                    MixOklab(out->warning, clear, 0.2f)));
    out->buttonWarningFg = Pick(c, "button.warning.foreground", out->warning);
    SetToken(&out->buttonWarningHover, &out->tokens.buttonWarningHover,
             PickBg(c, "button.warning.hover.background",
                    MixOklab(out->warning, clear, 0.3f)));
    SetToken(&out->buttonWarningActive, &out->tokens.buttonWarningActive,
             PickBg(c, "button.warning.active.background",
                    MixOklab(out->warning, clear, 0.4f)));

    SetToken(&out->dangerActive, &out->tokens.dangerActive,
             PickBg(c, "danger.active.background",
                    Darken(out->danger, activeDarken)));
    SetToken(
        &out->dangerHover, &out->tokens.dangerHover,
        PickBg(c, "danger.hover.background",
               Blend(out->background, RgbaOpacity(out->danger, hoverOpacity))));
    SetToken(&out->buttonDanger, &out->tokens.buttonDanger,
             PickBg(c, "button.danger.background",
                    MixOklab(out->danger, clear, 0.2f)));
    out->buttonDangerFg = Pick(c, "button.danger.foreground", out->danger);
    SetToken(&out->buttonDangerHover, &out->tokens.buttonDangerHover,
             PickBg(c, "button.danger.hover.background",
                    MixOklab(out->danger, clear, 0.3f)));
    SetToken(&out->buttonDangerActive, &out->tokens.buttonDangerActive,
             PickBg(c, "button.danger.active.background",
                    MixOklab(out->danger, clear, 0.4f)));

    SetToken(&out->accordion, &out->tokens.accordion,
             PickBg(c, "accordion.background", out->tokens.background));
    SetToken(
        &out->dropTarget, &out->tokens.dropTarget,
        PickBg(c, "drop_target.background", RgbaOpacity(out->primary, 0.2f)));
    out->link = Pick(c, "link", out->primary);
    out->linkActive = Pick(c, "link.active", out->link);
    out->linkHover = Pick(c, "link.hover", out->link);
    SetToken(&out->tableHover, &out->tokens.tableHover,
             PickBg(c, "table.hover.background", out->tokens.listHover));
    SetToken(&out->sliderBar, &out->tokens.sliderBar,
             PickBg(c, "slider.background", out->tokens.primary));
    SetToken(&out->switchBg, &out->tokens.switchBg,
             PickBg(c, "switch.background", out->tokens.secondaryActive));
    SetToken(&out->tab, &out->tokens.tab,
             PickBg(c, "tab.background", out->tokens.background));
    SetToken(&out->tabBarSegmented, &out->tokens.tabBarSegmented,
             PickBg(c, "tab_bar.segmented.background", out->tokens.secondary));
    SetToken(&out->tiles, &out->tokens.tiles,
             PickBg(c, "tiles.background", out->tokens.background));
    out->windowBorder = Pick(c, "window.border", out->border);

    // The three that are painted over text, capped however the file spells
    // them: a row highlight at a fifth, a text selection at a third.
    ClampToken(&out->listActive, &out->tokens.listActive,
               FindColor(c, "list.active.background") != nullptr, 0.2f);
    ClampToken(&out->tableActive, &out->tokens.tableActive,
               FindColor(c, "table.active.background") != nullptr, 0.2f);
    ClampToken(&out->selection, &out->tokens.selection,
               FindColor(c, "selection.background") != nullptr, 0.3f);

    // Everything the file did not spell as a gradient is its flat colour.
    ThemeTokensReset(out);

    // Theme::apply_config's metrics. `radius_lg` follows the radius the way
    // ThemeSetRadius does when the file names only the one.
    if (cfg->radius >= 0) {
        out->radius = cfg->radius;
        out->radiusLg = cfg->radius > 0 ? cfg->radius + 2 : 0;
    }
    if (cfg->radiusLg >= 0) {
        out->radiusLg = cfg->radiusLg;
    }
    out->radiusFull = out->radius > 0 ? kRadiusFull : 0;
    if (cfg->fontSize > 0) out->fontSize = cfg->fontSize;
    if (cfg->fontFamily.s) out->fontFamily = cfg->fontFamily;
    if (cfg->monoFontFamily.s) out->monoFontFamily = cfg->monoFontFamily;
    if (cfg->monoFontSize > 0) out->monoFontSize = cfg->monoFontSize;
    if (cfg->hasShadow) out->shadow = cfg->shadow;
}

// ─── the registry — crates/ui/src/theme/registry.rs ──────────────────────

// The documents and strings configs point into. Rust's ThemeRegistry is an
// App Global; this has the same lifetime and isolates loaded/active themes
// between applications.
ThemeRegistry::~ThemeRegistry() {
    VecReset(themes);
    VecReset(loadedDirs);
    if (arena) {
        ArenaDelete(arena);
        arena = nullptr;
    }
}

static ThemeRegistry* RegistryOf(const App* app);

ThemeRegistry* ThemeRegistry::Global(App* app) {
    return RegistryOf(app);
}

const ThemeRegistry* ThemeRegistry::Global(const App* app) {
    return RegistryOf(app);
}

static ThemeMode ParseMode(Str s) {
    return StrEqI(s, "dark") ? ThemeMode::Dark : ThemeMode::Light;
}

// sorted_themes: is_default first, then light before dark, then by name
// folded to lower case.
static bool SortsBefore(const ThemeConfig& a, const ThemeConfig& b) {
    if (a.isDefault != b.isDefault) {
        return a.isDefault;
    }
    if (a.mode != b.mode) {
        return a.mode == ThemeMode::Light;
    }
    return StrCmpI(a.name.s ? a.name.s : "", b.name.s ? b.name.s : "") < 0;
}

static void InsertSorted(ThemeRegistry* state, const ThemeConfig& cfg) {
    int at = state->themes.len;
    for (int i = 0; i < state->themes.len; i++) {
        if (SortsBefore(cfg, state->themes[i])) {
            at = i;
            break;
        }
    }
    VecInsertAt(state->themes, at, cfg);
}

static float JsonFloatOr(const JsonValue* v, const char* key, float fallback) {
    const JsonValue* m = JsonGet(v, key);
    if (!m || m->kind != JsonKind::Number) {
        return fallback;
    }
    return (float)m->num;
}

int ThemeSetConfig::Count() const {
    return themes && themes->kind == JsonKind::Array ? JsonLen(themes) : 0;
}

bool ThemeSetConfigParse(const JsonValue* value, ThemeSetConfig* out) {
    if (!value || value->kind != JsonKind::Object || !out) return false;
    const JsonValue* themes = JsonGet(value, "themes");
    if (!themes || themes->kind != JsonKind::Array) return false;
    out->name = JsonString(JsonGet(value, "name"));
    out->author = JsonString(JsonGet(value, "author"));
    out->url = JsonString(JsonGet(value, "url"));
    out->themes = themes;
    return true;
}

int ThemeRegistryLoadStr(App* app, Str json) {
    ThemeRegistry* state = RegistryOf(app);
    if (!state || !state->arena || !json.s || len(json) <= 0) {
        return 0;
    }
    // What a theme keeps — its name, its colors object — points into the
    // document, so an accepted theme pins the parse. A document that adds
    // nothing pins nothing, and the arena goes back to where it was.
    uint64_t mark = ArenaUsed(state->arena);
    JsonValue* doc = JsonParse(state->arena, json);
    if (!doc) {
        state->arena->PopTo(mark);
        return 0;
    }
    const JsonValue* themes = JsonGet(doc, "themes");
    if (!themes || themes->kind != JsonKind::Array) {
        state->arena->PopTo(mark);
        return 0;
    }
    Str setAuthor = JsonString(JsonGet(doc, "author"));
    Str setUrl = JsonString(JsonGet(doc, "url"));
    int added = 0;
    for (const JsonValue* t = themes->first; t; t = t->next) {
        if (t->kind != JsonKind::Object) {
            continue;
        }
        ThemeConfig cfg;
        cfg.name = JsonString(JsonGet(t, "name"));
        if (len(cfg.name) <= 0 || ThemeRegistryFind(app, cfg.name)) {
            continue;
        }
        cfg.author = setAuthor;
        cfg.url = setUrl;
        cfg.mode = ParseMode(JsonString(JsonGet(t, "mode")));
        cfg.isDefault = JsonBool(JsonGet(t, "is_default"));
        cfg.colors = JsonGet(t, "colors");
        cfg.fontSize = JsonFloatOr(t, "font.size", 0);
        cfg.fontFamily = JsonString(JsonGet(t, "font.family"));
        cfg.monoFontFamily = JsonString(JsonGet(t, "mono_font.family"));
        cfg.monoFontSize = JsonFloatOr(t, "mono_font.size", 0);
        cfg.radius = JsonFloatOr(t, "radius", -1);
        cfg.radiusLg = JsonFloatOr(t, "radius.lg", -1);
        const JsonValue* shadow = JsonGet(t, "shadow");
        if (shadow && shadow->kind == JsonKind::Bool) {
            cfg.shadow = shadow->b;
            cfg.hasShadow = true;
        }
        cfg.highlight = JsonGet(t, "highlight");
        InsertSorted(state, cfg);
        added++;
    }
    if (added == 0) {
        state->arena->PopTo(mark);
    }
    return added;
}

static ThemeRegistry* RegistryOf(const App* app) {
    ThemeRegistry* state = AppGlobalEnsure<ThemeRegistry>((App*)app);
    if (!state || state->initialized) {
        return state;
    }
    state->initialized = true;
    state->arena = ArenaNew();
    // The two the tree was built with are what everything else resolves
    // against, so they go in first and stay first.
    ThemeRegistryLoadStr((App*)app, Str(kDefaultThemeJson));
    for (int i = 0; i < state->themes.len; i++) {
        if (state->themes[i].isDefault) {
            state->active[(int)state->themes[i].mode] = state->themes[i].name;
        }
    }
    return state;
}

void ThemeRegistryInit(App* app) {
    (void)RegistryOf(app);
}

// ─── a theme written as semantic tokens ──────────────────────────────────
//
// `SemanticThemeConfigFile`: a document whose whole content is
// `{"tokens": {...}}`, in the vocabulary of `theme_tokens.rs` rather than the
// legacy key list. Rust resolves it over the theme in force — every field is
// an Option and what it leaves out stays as it was — and then applies the
// result back onto that theme. Same here, over the mode's current palette.
static void ApplyColorField(const JsonValue* obj, const char* key, Rgba* out) {
    Str v = JsonString(JsonGet(obj, key));
    Rgba c;
    if (v.s && ThemeParseColor(v, &c)) {
        *out = c;
    }
}

static void ApplyFloatField(const JsonValue* obj, const char* key, float* out) {
    const JsonValue* v = JsonGet(obj, key);
    if (v && v->kind == JsonKind::Number) {
        *out = (float)v->num;
    }
}

static void ApplyTextStyle(const JsonValue* obj, const char* key,
                           SemanticTextStyle* out) {
    const JsonValue* v = JsonGet(obj, key);
    if (!v || v->kind != JsonKind::Object) {
        return;
    }
    ApplyFloatField(v, "size", &out->size);
    ApplyFloatField(v, "line_height", &out->lineHeight);
    const JsonValue* weight = JsonGet(v, "weight");
    if (weight && weight->kind == JsonKind::Number) {
        out->weight = (FontWeight)(uint16_t)weight->num;
    }
}

static void ApplyShadowLevel(const JsonValue* obj, const char* key,
                             Vec<BoxShadow>* level, bool* any) {
    const JsonValue* v = JsonGet(obj, key);
    if (!v || (v->kind != JsonKind::Object && v->kind != JsonKind::Array)) {
        return;
    }
    *any = true;
    if (v->kind == JsonKind::Array) {
        // Source Vec<BoxShadow> replaces the elevation when an array is
        // supplied. Retain the port's one-object shorthand as a partial
        // overlay on the existing first shadow.
        VecClear(*level);
    } else if (level->len == 0) {
        VecAppend(*level, BoxShadow{});
    }
    const JsonValue* item = v->kind == JsonKind::Array ? v->first : v;
    for (; item; item = v->kind == JsonKind::Array ? item->next : nullptr) {
        if (item->kind != JsonKind::Object) continue;
        if (v->kind == JsonKind::Array) VecAppend(*level, BoxShadow{});
        BoxShadow* out = v->kind == JsonKind::Array ? &(*level)[level->len - 1]
                                                    : &(*level)[0];
        ApplyFloatField(item, "x", &out->x);
        ApplyFloatField(item, "y", &out->y);
        ApplyFloatField(item, "blur", &out->blur);
        ApplyFloatField(item, "blur_radius", &out->blur);
        ApplyFloatField(item, "spread", &out->spread);
        ApplyFloatField(item, "spread_radius", &out->spread);
        ApplyColorField(item, "color", &out->color);
        const JsonValue* inset = JsonGet(item, "inset");
        if (inset && inset->kind == JsonKind::Bool) out->inset = inset->b;
    }
}

static void ParseStringOption(const JsonValue* obj, const char* key,
                              ThemeConfigValue<Str>* out) {
    const JsonValue* value = JsonGet(obj, key);
    if (!value || value->kind != JsonKind::String) return;
    out->value = value->str;
    out->has = true;
}

static void ParseFloatOption(const JsonValue* obj, const char* key,
                             ThemeConfigValue<float>* out) {
    const JsonValue* value = JsonGet(obj, key);
    if (!value || value->kind != JsonKind::Number) return;
    out->value = (float)value->num;
    out->has = true;
}

static void ParseTextStyleConfig(const JsonValue* obj, const char* key,
                                 SemanticTextStyleConfig* out) {
    const JsonValue* value = JsonGet(obj, key);
    if (!value || value->kind != JsonKind::Object) return;
    ParseFloatOption(value, "size", &out->size);
    ParseFloatOption(value, "line_height", &out->lineHeight);
    const JsonValue* weight = JsonGet(value, "weight");
    if (weight && weight->kind == JsonKind::Number) {
        out->weight.value = (FontWeight)(uint16_t)weight->num;
        out->weight.has = true;
    }
}

bool SemanticThemeConfigParse(const JsonValue* value,
                              SemanticThemeConfig* out) {
    if (!value || value->kind != JsonKind::Object || !out) return false;
    *out = {};
    if (const JsonValue* colors = JsonGet(value, "colors")) {
        ParseStringOption(colors, "background", &out->colors.background);
        ParseStringOption(colors, "foreground", &out->colors.foreground);
        ParseStringOption(colors, "surface", &out->colors.surface);
        ParseStringOption(colors, "surface_foreground",
                          &out->colors.surfaceForeground);
        ParseStringOption(colors, "primary", &out->colors.primary);
        ParseStringOption(colors, "primary_foreground",
                          &out->colors.primaryForeground);
        ParseStringOption(colors, "secondary", &out->colors.secondary);
        ParseStringOption(colors, "secondary_foreground",
                          &out->colors.secondaryForeground);
        ParseStringOption(colors, "muted", &out->colors.muted);
        ParseStringOption(colors, "muted_foreground",
                          &out->colors.mutedForeground);
        ParseStringOption(colors, "accent", &out->colors.accent);
        ParseStringOption(colors, "accent_foreground",
                          &out->colors.accentForeground);
        ParseStringOption(colors, "destructive", &out->colors.destructive);
        ParseStringOption(colors, "destructive_foreground",
                          &out->colors.destructiveForeground);
        ParseStringOption(colors, "border", &out->colors.border);
        ParseStringOption(colors, "input", &out->colors.input);
        ParseStringOption(colors, "ring", &out->colors.ring);
    }
    if (const JsonValue* radius = JsonGet(value, "radius")) {
        ParseFloatOption(radius, "none", &out->radius.none);
        ParseFloatOption(radius, "sm", &out->radius.sm);
        ParseFloatOption(radius, "md", &out->radius.md);
        ParseFloatOption(radius, "lg", &out->radius.lg);
        ParseFloatOption(radius, "xl", &out->radius.xl);
        ParseFloatOption(radius, "full", &out->radius.full);
    }
    if (const JsonValue* spacing = JsonGet(value, "spacing")) {
        ParseFloatOption(spacing, "xxs", &out->spacing.xxs);
        ParseFloatOption(spacing, "xs", &out->spacing.xs);
        ParseFloatOption(spacing, "sm", &out->spacing.sm);
        ParseFloatOption(spacing, "md", &out->spacing.md);
        ParseFloatOption(spacing, "lg", &out->spacing.lg);
        ParseFloatOption(spacing, "xl", &out->spacing.xl);
        ParseFloatOption(spacing, "xxl", &out->spacing.xxl);
    }
    if (const JsonValue* typography = JsonGet(value, "typography")) {
        ParseStringOption(typography, "sans", &out->typography.sans);
        ParseStringOption(typography, "mono", &out->typography.mono);
        ParseTextStyleConfig(typography, "xs", &out->typography.xs);
        ParseTextStyleConfig(typography, "sm", &out->typography.sm);
        ParseTextStyleConfig(typography, "md", &out->typography.md);
        ParseTextStyleConfig(typography, "lg", &out->typography.lg);
        ParseTextStyleConfig(typography, "xl", &out->typography.xl);
        ParseTextStyleConfig(typography, "mono_md", &out->typography.monoMd);
    }
    if (const JsonValue* shadow = JsonGet(value, "shadow")) {
        out->shadow.sm = JsonGet(shadow, "sm");
        out->shadow.md = JsonGet(shadow, "md");
        out->shadow.lg = JsonGet(shadow, "lg");
    }
    return true;
}

bool SemanticThemeConfigFileParse(const JsonValue* value,
                                  SemanticThemeConfigFile* out) {
    if (!value || !out) return false;
    const JsonValue* tokens = JsonGet(value, "tokens");
    if (!tokens) return false;
    return SemanticThemeConfigParse(tokens, &out->tokens);
}

static void ApplyConfiguredColor(const ThemeConfigValue<Str>& value,
                                 Rgba* out) {
    Rgba parsed;
    if (value.has && ThemeParseColor(value.value, &parsed)) *out = parsed;
}

static void ApplyConfiguredTextStyle(const SemanticTextStyleConfig& value,
                                     TextStyleToken* out) {
    if (value.size.has) out->size = value.size.value;
    if (value.lineHeight.has) out->lineHeight = value.lineHeight.value;
    if (value.weight.has) out->weight = value.weight.value;
}

static void ApplyConfiguredShadow(const JsonValue* value, Vec<BoxShadow>* out) {
    if (!value) return;
    JsonValue wrapper;
    wrapper.kind = JsonKind::Object;
    JsonValue member = *value;
    member.key = StrL("value");
    member.next = nullptr;
    wrapper.first = &member;
    bool any = false;
    ApplyShadowLevel(&wrapper, "value", out, &any);
}

bool SemanticThemeConfig::ApplyTo(SemanticThemeTokens* out) const {
    if (!out) return false;
    ApplyConfiguredColor(colors.background, &out->colors.background);
    ApplyConfiguredColor(colors.foreground, &out->colors.foreground);
    ApplyConfiguredColor(colors.surface, &out->colors.surface);
    ApplyConfiguredColor(colors.surfaceForeground, &out->colors
                                                        .surfaceForeground);
    ApplyConfiguredColor(colors.primary, &out->colors.primary);
    ApplyConfiguredColor(colors.primaryForeground, &out->colors
                                                        .primaryForeground);
    ApplyConfiguredColor(colors.secondary, &out->colors.secondary);
    ApplyConfiguredColor(colors.secondaryForeground, &out->colors
                                                          .secondaryForeground);
    ApplyConfiguredColor(colors.muted, &out->colors.muted);
    ApplyConfiguredColor(colors.mutedForeground, &out->colors.mutedForeground);
    ApplyConfiguredColor(colors.accent, &out->colors.accent);
    ApplyConfiguredColor(colors.accentForeground, &out->colors
                                                       .accentForeground);
    ApplyConfiguredColor(colors.destructive, &out->colors.destructive);
    ApplyConfiguredColor(colors.destructiveForeground,
                         &out->colors.destructiveForeground);
    ApplyConfiguredColor(colors.border, &out->colors.border);
    ApplyConfiguredColor(colors.input, &out->colors.input);
    ApplyConfiguredColor(colors.ring, &out->colors.ring);
#define GPUI_APPLY_THEME_FLOAT(config, target, field) \
    if ((config).field.has) (target).field = (config).field.value
    GPUI_APPLY_THEME_FLOAT(radius, out->radius, none);
    GPUI_APPLY_THEME_FLOAT(radius, out->radius, sm);
    GPUI_APPLY_THEME_FLOAT(radius, out->radius, md);
    GPUI_APPLY_THEME_FLOAT(radius, out->radius, lg);
    GPUI_APPLY_THEME_FLOAT(radius, out->radius, xl);
    GPUI_APPLY_THEME_FLOAT(radius, out->radius, full);
    GPUI_APPLY_THEME_FLOAT(spacing, out->spacing, xxs);
    GPUI_APPLY_THEME_FLOAT(spacing, out->spacing, xs);
    GPUI_APPLY_THEME_FLOAT(spacing, out->spacing, sm);
    GPUI_APPLY_THEME_FLOAT(spacing, out->spacing, md);
    GPUI_APPLY_THEME_FLOAT(spacing, out->spacing, lg);
    GPUI_APPLY_THEME_FLOAT(spacing, out->spacing, xl);
    GPUI_APPLY_THEME_FLOAT(spacing, out->spacing, xxl);
#undef GPUI_APPLY_THEME_FLOAT
    if (typography.sans.has) out->typography.sans = typography.sans.value;
    if (typography.mono.has) out->typography.mono = typography.mono.value;
    ApplyConfiguredTextStyle(typography.xs, &out->typography.xs);
    ApplyConfiguredTextStyle(typography.sm, &out->typography.sm);
    ApplyConfiguredTextStyle(typography.md, &out->typography.md);
    ApplyConfiguredTextStyle(typography.lg, &out->typography.lg);
    ApplyConfiguredTextStyle(typography.xl, &out->typography.xl);
    ApplyConfiguredTextStyle(typography.monoMd, &out->typography.monoMd);
    ApplyConfiguredShadow(shadow.sm, &out->shadow.sm);
    ApplyConfiguredShadow(shadow.md, &out->shadow.md);
    ApplyConfiguredShadow(shadow.lg, &out->shadow.lg);
    return true;
}

bool ThemeSemanticConfigApply(const JsonValue* doc, SemanticThemeTokens* io) {
    const JsonValue* tokens = JsonGet(doc, "tokens");
    if (!tokens || tokens->kind != JsonKind::Object) {
        return false;
    }
    if (const JsonValue* c = JsonGet(tokens, "colors")) {
        SemanticColorTokens& t = io->colors;
        ApplyColorField(c, "background", &t.background);
        ApplyColorField(c, "foreground", &t.foreground);
        ApplyColorField(c, "surface", &t.surface);
        ApplyColorField(c, "surface_foreground", &t.surfaceForeground);
        ApplyColorField(c, "primary", &t.primary);
        ApplyColorField(c, "primary_foreground", &t.primaryForeground);
        ApplyColorField(c, "secondary", &t.secondary);
        ApplyColorField(c, "secondary_foreground", &t.secondaryForeground);
        ApplyColorField(c, "muted", &t.muted);
        ApplyColorField(c, "muted_foreground", &t.mutedForeground);
        ApplyColorField(c, "accent", &t.accent);
        ApplyColorField(c, "accent_foreground", &t.accentForeground);
        ApplyColorField(c, "destructive", &t.destructive);
        ApplyColorField(c, "destructive_foreground", &t.destructiveForeground);
        ApplyColorField(c, "border", &t.border);
        ApplyColorField(c, "input", &t.input);
        ApplyColorField(c, "ring", &t.ring);
    }
    if (const JsonValue* r = JsonGet(tokens, "radius")) {
        ApplyFloatField(r, "none", &io->radius.none);
        ApplyFloatField(r, "sm", &io->radius.sm);
        ApplyFloatField(r, "md", &io->radius.md);
        ApplyFloatField(r, "lg", &io->radius.lg);
        ApplyFloatField(r, "xl", &io->radius.xl);
        ApplyFloatField(r, "full", &io->radius.full);
    }
    if (const JsonValue* sp = JsonGet(tokens, "spacing")) {
        ApplyFloatField(sp, "xxs", &io->spacing.xxs);
        ApplyFloatField(sp, "xs", &io->spacing.xs);
        ApplyFloatField(sp, "sm", &io->spacing.sm);
        ApplyFloatField(sp, "md", &io->spacing.md);
        ApplyFloatField(sp, "lg", &io->spacing.lg);
        ApplyFloatField(sp, "xl", &io->spacing.xl);
        ApplyFloatField(sp, "xxl", &io->spacing.xxl);
    }
    if (const JsonValue* ty = JsonGet(tokens, "typography")) {
        Str sans = JsonString(JsonGet(ty, "sans"));
        Str mono = JsonString(JsonGet(ty, "mono"));
        if (sans.s) {
            io->typography.sans = sans;
        }
        if (mono.s) {
            io->typography.mono = mono;
        }
        ApplyTextStyle(ty, "xs", &io->typography.xs);
        ApplyTextStyle(ty, "sm", &io->typography.sm);
        ApplyTextStyle(ty, "md", &io->typography.md);
        ApplyTextStyle(ty, "lg", &io->typography.lg);
        ApplyTextStyle(ty, "xl", &io->typography.xl);
        ApplyTextStyle(ty, "mono_md", &io->typography.monoMd);
    }
    if (const JsonValue* sh = JsonGet(tokens, "shadow")) {
        bool any = false;
        ApplyShadowLevel(sh, "sm", &io->shadow.sm, &any);
        ApplyShadowLevel(sh, "md", &io->shadow.md, &any);
        ApplyShadowLevel(sh, "lg", &io->shadow.lg, &any);
        (void)any;
    }
    return true;
}

bool ThemeApplySemanticConfigStr(App* app, ThemeMode mode, Str json,
                                 SemanticThemeTokens* out) {
    if (!json.s || len(json) <= 0) {
        return false;
    }
    ThemeRegistry* registry = RegistryOf(app);
    // The document is read and thrown away: nothing a semantic config holds
    // outlives the colours it is turned into.
    Arena* a = ArenaNew();
    JsonValue* doc = JsonParse(a, json);
    Theme t = mode == ThemeMode::Dark ? ThemeDark(app) : ThemeLight(app);
    SemanticThemeTokens tokens = ThemeSemanticTokens(t);
    bool ok = doc && ThemeSemanticConfigApply(doc, &tokens);
    // The two font families are the only strings a token set keeps, and they
    // point into the document. They move to the registry's own arena — where
    // every theme's name already lives — so the answer outlives the parse.
    if (ok && registry && registry->arena) {
        tokens.typography
            .sans = StrDup(registry->arena, tokens.typography.sans);
        tokens.typography
            .mono = StrDup(registry->arena, tokens.typography.mono);
    }
    ArenaDelete(a);
    if (!ok) {
        return false;
    }
    ThemeApplySemanticTokens(&t, tokens);
    ThemeInstall(app, mode, t);
    if (out) {
        *out = tokens;
    }
    return true;
}

int ThemeRegistryLoadDir(App* app, Str dir) {
    ThemeRegistry* state = RegistryOf(app);
    if (!state || !state->arena) {
        return 0;
    }
    int n = len(dir) < kMaxPath - 1 ? len(dir) : kMaxPath - 1;
    TempStr path = StrDupTemp(Str(dir.s ? dir.s : "", n));
    if (!PlatDirExists(path.s)) {
        TempStr resolved = AllocStrTemp(kMaxPath - 1);
        if (!AssetsFindDir(dir, resolved.s, len(resolved) + 1)) {
            return 0;
        }
        path = Str(resolved.s);
    }
    // Already read once. A theme is never dropped, so a second pass over the
    // same directory can only find what is in the registry already.
    Str dirKey = path;
    for (int i = 0; i < state->loadedDirs.len; i++) {
        if (base::StrEq(state->loadedDirs[i], dirKey)) {
            return 0;
        }
    }
    VecAppend(state->loadedDirs, StrDup(state->arena, dirKey));
    // Rust reads the whole directory; a hundred entries is more themes than
    // anyone ships and the listing is a fixed buffer either way.
    const int kMaxEntries = 128;
    DirEntry* entries = AllocArray<DirEntry>(kMaxEntries);
    if (!entries) {
        return 0;
    }
    int count = PlatListDir(path.s, entries, kMaxEntries);
    int added = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].isDir) {
            continue;
        }
        const char* name = entries[i].name;
        int nameLen = (int)strlen(name);
        if (nameLen < 6 || !base::StrEqI(Str(name + nameLen - 5), ".json")) {
            continue;
        }
        TempStr file = fmt("%s%c%s", path, kSep, Str(name));
        Str text = len(file) < kMaxPath ? ReadTextFile(file.s) : Str{};
        if (text.s) {
            // An unparseable file is skipped rather than fatal, the way
            // Rust's `reload()` logs and carries on.
            added += ThemeRegistryLoadStr(app, text);
            StrFree(text);
        }
    }
    free(entries);
    return added;
}

int ThemeRegistryCount(const App* app) {
    ThemeRegistry* state = RegistryOf(app);
    return state ? state->themes.len : 0;
}

const ThemeConfig* ThemeRegistryAt(const App* app, int ix) {
    ThemeRegistry* state = RegistryOf(app);
    if (!state || ix < 0 || ix >= state->themes.len) {
        return nullptr;
    }
    return &state->themes[ix];
}

const ThemeConfig* ThemeRegistryFind(const App* app, Str name) {
    ThemeRegistry* state = RegistryOf(app);
    if (!state) {
        return nullptr;
    }
    for (int i = 0; i < state->themes.len; i++) {
        if (base::StrEq(state->themes[i].name, name)) {
            return &state->themes[i];
        }
    }
    return nullptr;
}

Str ThemeRegistryActive(const App* app, ThemeMode mode) {
    ThemeRegistry* state = RegistryOf(app);
    return state ? state->active[(int)mode] : Str{};
}

bool ThemeRegistryApply(App* app, const ThemeConfig* cfg) {
    if (!cfg) {
        return false;
    }
    ThemeUpdate(app, [&](Theme* t) { ThemeApplyConfig(app, t, cfg); });
    return true;
}

bool ThemeRegistryApply(App* app, Str name) {
    return ThemeRegistryApply(app, ThemeRegistryFind(app, name));
}

void ThemeRegistryReset(App* app) {
    ThemeRegistry* state = RegistryOf(app);
    if (!state) {
        return;
    }
    ThemeInstall(app, ThemeMode::Light, ThemeDefaultLight());
    ThemeInstall(app, ThemeMode::Dark, ThemeDefaultDark());
    for (int i = 0; i < state->themes.len; i++) {
        if (state->themes[i].isDefault) {
            state->active[(int)state->themes[i].mode] = state->themes[i].name;
        }
    }
    if (app) {
        AppRefreshWindows(app);
    }
}

void ThemeRegistryFree(App* app) {
    AppGlobalRemove<ThemeRegistry>(app);
}

} // namespace gpui
