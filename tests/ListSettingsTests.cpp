/* Ported from crates/base/src/list_settings.rs and the two places that read it
 * — crates/ui/src/list/list_item.rs and crates/ui/src/table/state.rs.
 *
 * One setting, `active_highlight`, decides what a selected row looks like: the
 * translucent list.active tint ruled with list.active.border, or the plain
 * `accent` block. */

#include "Test.h"

static const Rgba kActive = Rgba8(0xbf, 0xdb, 0xfe, 0x33);
static const Rgba kActiveBorder = Rgb(0x60, 0xa5, 0xfa);
static const Rgba kAccent = Rgb(0xf5, 0xf5, 0xf5);

static bool Same(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// ListSettings::default: the highlight is on.
static void TheHighlightIsOnUnlessItIsTurnedOff() {
    ListSettings s;
    utassert(s.activeHighlight);
}

static void ASelectedRowTakesTheTintAndTheRule() {
    ListActiveStyle st = ListActiveStyleOf(ListSettings{}, kActive,
                                           kActiveBorder, kAccent, true);
    utassert(Same(st.bg.color, kActive));
    utassert(st.hasBorder);
    utassert(Same(st.border, kActiveBorder));
}

// The row a right press marked is not the selection, so it takes `accent`
// whatever the setting says — the rule is the setting's, the fill is the
// row's.
static void ARowThatIsOnlySecondarySelectedKeepsAccent() {
    ListActiveStyle st = ListActiveStyleOf(ListSettings{}, kActive,
                                           kActiveBorder, kAccent, false);
    utassert(Same(st.bg.color, kAccent));
    utassert(st.hasBorder);
}

static void WithTheHighlightOffASelectionIsAPlainBlock() {
    ListSettings off;
    off.activeHighlight = false;
    ListActiveStyle st =
        ListActiveStyleOf(off, kActive, kActiveBorder, kAccent, true);
    utassert(Same(st.bg.color, kAccent));
    utassert(!st.hasBorder);
    // And nothing about the setting is per-row.
    st = ListActiveStyleOf(off, kActive, kActiveBorder, kAccent, false);
    utassert(Same(st.bg.color, kAccent));
    utassert(!st.hasBorder);
}

static void SettingsBelongToTheirApplication() {
    App first;
    App second;
    ListSettings off;
    off.activeHighlight = false;
    ListSettingsSet(&first, off);
    utassert(!ListSettingsNow(&first).activeHighlight);
    utassert(ListSettingsNow(&second).activeHighlight);
    AppGlobalClear(&first);
    AppGlobalClear(&second);
}

// The rule is drawn over the row rather than around it, so turning the
// highlight on does not move anything.
static void TheRuleCoversTheRowWithoutResizingIt() {
    Arena* a = ArenaNew();
    El* e = ListActiveOverlay(a, kActiveBorder, 6);
    utassert(e->style.absolute);
    utassertnear(e->style.absTop, 0.f);
    utassertnear(e->style.absRight, 0.f);
    utassertnear(e->style.border, 1.f);
    utassert(Same(e->style.borderColor, kActiveBorder));
    utassertnear(e->style.radius, 6.f);
    ArenaDelete(a);
}

// Both theme pairs are there and the table's falls back to the list's, which
// is what schema.rs writes down.
static void TheTableTakesTheListColorsWhenItHasNoneOfItsOwn() {
    const Theme& light = ThemeLight();
    const Theme& dark = ThemeDark();
    utassert(Same(light.tableActive, light.listActive));
    utassert(Same(light.tableActiveBorder, light.listActiveBorder));
    utassert(Same(dark.tableActive, dark.listActive));
    utassert(light.listActive.a < 0xff);
    utassert(dark.listActive.a < 0xff);
    utassert(light.listActiveBorder.a == 0xff);
}

void TestListSettings() {
    TestSuite("list_settings");
    TheHighlightIsOnUnlessItIsTurnedOff();
    ASelectedRowTakesTheTintAndTheRule();
    ARowThatIsOnlySecondarySelectedKeepsAccent();
    WithTheHighlightOffASelectionIsAPlainBlock();
    TheRuleCoversTheRowWithoutResizingIt();
    TheTableTakesTheListColorsWhenItHasNoneOfItsOwn();
    SettingsBelongToTheirApplication();
}
