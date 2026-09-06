/* Ported from crates/ui/src/native_menu.
 *
 * The builder puts one row per call, carrying its label, its disabled and
 * checked state, its icon and what it reports. What the OS is handed is
 * numbered over the rows that can be chosen — preorder, skipping separators,
 * submenu rows and greyed rows — which is Rust's `actions` vector and what
 * makes the id the OS answers with map back to the row that was built. */

#include "Test.h"

using namespace gpui::component;

// test_native_menu_builder_accepts_icon: the row carries what it was built
// with, icon included.
static void ARowCarriesWhatItWasBuiltWith() {
    Arena* ta = ArenaNew();
    component::NativeMenu m;
    m.a = ta;
    utassert(m.IsEmpty());
    m.MenuWithIcon(StrL("Github"), IconName::Github, 7);
    utassert(!m.IsEmpty());
    utassert(m.items.len == 1);
    utassert(m.items[0].kind == component::NativeMenuItemKind::Item);
    utassert(StrEqI(m.items[0].label, "Github"));
    utassert(!m.items[0].disabled);
    utassert(!m.items[0].checked);
    utassert(m.items[0].icon == IconName::Github);
    utassert(m.items[0].id == 7);

    // menu_with_disabled and menu_with_check each set one of the two.
    m.MenuWithDisabled(StrL("Inbox"), true, 8);
    m.MenuWithCheck(StrL("Wrap"), true, 9);
    utassert(m.items[1].disabled && !m.items[1].checked);
    utassert(m.items[2].checked && !m.items[2].disabled);

    m.Separator();
    utassert(m.items[3].kind == component::NativeMenuItemKind::Separator);

    component::NativeMenu sub;
    sub.a = ta;
    sub.Menu(StrL("Copy"), 10);
    m.Submenu(StrL("Edit"), &sub);
    utassert(m.items[4].kind == component::NativeMenuItemKind::Submenu);
    utassert(m.items[4].submenu == &sub);
    utassert(m.items.len == 5);
    ArenaDelete(ta);
}

// There is no cap to drop rows past any more; what this pins is that there is
// not one. A hundred rows go in and a hundred come back out.
static void EveryRowAddedIsKept() {
    Arena* ta = ArenaNew();
    component::NativeMenu m;
    m.a = ta;
    for (int i = 0; i < 100; i++) {
        m.Menu(StrL("Item"), i);
    }
    utassert(m.items.len == 100);
    utassert(m.items[99].id == 99);
    ArenaDelete(ta);
}

// The table the id maps back through: 1-based over what can be chosen, in the
// order the rows are built, with a submenu's rows taken where it sits.
static void OnlyTheRowsThatCanBeChosenAreNumbered() {
    Arena* ta = ArenaNew();
    component::NativeMenu sub;
    sub.a = ta;
    sub.Menu(StrL("Copy"), 20);
    sub.Menu(StrL("Cut"), 21);
    component::NativeMenu m;
    m.a = ta;
    m.Menu(StrL("New"), 1);
    m.Separator();
    m.MenuWithDisabled(StrL("Save"), true, 2);
    m.Submenu(StrL("Edit"), &sub);
    m.Menu(StrL("Quit"), 3);

    const component::NativeMenuItem* table[8] = {};
    int n = NativeMenuSelectable(&m, table, 8);
    utassert(n == 4);
    utassert(table[0]->id == 1);
    utassert(table[1]->id == 20);
    utassert(table[2]->id == 21);
    utassert(table[3]->id == 3);

    // Counting works without a table to write into, and a menu that is not
    // there has nothing to count.
    utassert(NativeMenuSelectable(&m, nullptr, 0) == 4);
    utassert(NativeMenuSelectable(nullptr, table, 8) == 0);
    ArenaDelete(ta);
}

// A greyed submenu row still has its rows numbered: Win32 greys the row that
// opens the submenu, not what is inside it.
static void AGreyedSubmenuStillNumbersItsRows() {
    Arena* ta = ArenaNew();
    component::NativeMenu sub;
    sub.a = ta;
    sub.Menu(StrL("Copy"), 30);
    component::NativeMenu m;
    m.a = ta;
    m.Submenu(StrL("Edit"), &sub);
    m.items[0].disabled = true;
    const component::NativeMenuItem* table[4] = {};
    utassert(NativeMenuSelectable(&m, table, 4) == 1);
    utassert(table[0]->id == 30);
    ArenaDelete(ta);
}

// An empty menu has nothing to show, which is what keeps `show` from opening
// a popup with no rows in it.
static void AnEmptyMenuShowsNothing() {
    Arena* ta = ArenaNew();
    component::NativeMenu m;
    m.a = ta;
    utassert(m.IsEmpty());
    utassert(!m.Show(0, 0));
    const component::NativeMenuItem* table[4] = {};
    utassert(NativeMenuSelectable(&m, table, 4) == 0);
    ArenaDelete(ta);
}

// native_menu/mod.rs:
// test_native_menu_icon_data_replaces_path_and_survives_clone. A row given an
// `Icon::Data` carries the SVG source, which the platform rasterizes without an
// asset lookup; a later `Path` on the icon replaces it.
static void IconDataReplacesThePathAndTravelsWithTheRow() {
    Arena* ta = ArenaNew();
    Ctx cx = {};
    cx.a = ta;
    static const char kSvg[] =
        "<svg viewBox=\"0 0 24 24\"><path d=\"M4 12h16\"/></svg>";
    component::NativeMenu m;
    m.a = ta;
    m.cx = &cx;
    component::Icon* icon = component::Icon::Empty(&cx)
                                ->Path(StrL("icons/previous.png"))
                                ->Data(Str(kSvg));
    m.MenuWithIcon(StrL("Search"), icon, 6);
    utassert(m.items.len == 1);
    utassert(m.items[0].kind == component::NativeMenuItemKind::Item);
    utassert(StrEq(m.items[0].iconSvg, Str(kSvg)));
    utassert(!m.items[0].iconPath.s && m.items[0].icon == IconName::None);
    utassert(m.items[0].id == 6);

    icon->Path(StrL("icons/replacement.svg"));
    m.MenuWithIcon(StrL("Replaced"), icon, 7);
    utassert(!m.items[1].iconSvg.s);
    utassert(StrEq(m.items[1].iconPath, StrL("icons/replacement.svg")));

    // The drawn fallback keeps the same source on its row.
    component::PopupMenu* drawn = m.IntoPopupMenu(StrL("fallback"));
    utassert(drawn && drawn->items.len == 2);
    utassert(StrEq(drawn->items[0].iconSvg, Str(kSvg)));
    utassert(StrEq(drawn->items[1].iconPath, StrL("icons/replacement.svg")));
    ArenaDelete(ta);
}

void TestNativeMenu() {
    TestSuite("native_menu");
    ARowCarriesWhatItWasBuiltWith();
    IconDataReplacesThePathAndTravelsWithTheRow();
    EveryRowAddedIsKept();
    OnlyTheRowsThatCanBeChosenAreNumbered();
    AGreyedSubmenuStillNumbersItsRows();
    AnEmptyMenuShowsNothing();
}
