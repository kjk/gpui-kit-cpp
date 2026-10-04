/* The application's own menus — gpui's `App::set_menus`, which on macOS is
 * the bar at the top of the screen.
 *
 * The platform half is not exercised here: three of the four platforms have
 * no menu bar to install into, and the one that does answers through AppKit.
 * What is worth pinning is the half above it — the numbering that lets the id
 * a menu answers with name the row that was built, and the keymap lookup that
 * puts a chord beside a label. */

#include "Test.h"

using namespace gpui;

// The menu model is App-owned, like GPUI's `Vec<OwnedMenu>`.
static App gMenuApp;
static App* const kApp = &gMenuApp;

static uint32_t Act(const char* name) {
    return ActionOf(Str(name));
}

// Only the rows that can be chosen are numbered, in preorder: a separator, a
// row that opens onto a submenu and a greyed row all report nothing, so none
// of them takes a number. It is the same rule NativeMenu::Show numbers by,
// and what makes the two halves agree without either being told the order.
static void OnlyTheRowsThatCanBeChosenAreNumbered() {
    uint32_t about = Act("test::About");
    uint32_t light = Act("test::Light");
    uint32_t dark = Act("test::Dark");
    uint32_t quit = Act("test::Quit");
    uint32_t soon = Act("test::Soon");

    MenuRow appearance[2] = {};
    appearance[0].label = StrL("Light");
    appearance[0].action = light;
    appearance[1].label = StrL("Dark");
    appearance[1].action = dark;
    appearance[1].arg = 1;

    MenuRow rows[5] = {};
    rows[0].label = StrL("About");
    rows[0].action = about;
    rows[1].separator = true;
    rows[2].label = StrL("Appearance");
    rows[2].submenu = appearance;
    rows[2].submenuN = 2;
    rows[3].label = StrL("Coming Soon");
    rows[3].action = soon;
    rows[3].disabled = true;
    rows[4].label = StrL("Quit");
    rows[4].action = quit;

    MenuDef menus[1] = {};
    menus[0].name = StrL("Test");
    menus[0].items = rows;
    menus[0].n = 5;
    AppSetMenus(kApp, menus, 1);

    uint32_t action = 0;
    int64_t arg = -1;
    utassert(AppMenuRowForId(1, &action, &arg) && action == about && arg == 0);
    // The submenu's rows come where the row that opens them was, and in their
    // own order — preorder, not the top level first.
    utassert(AppMenuRowForId(2, &action, &arg) && action == light && arg == 0);
    utassert(AppMenuRowForId(3, &action, &arg) && action == dark && arg == 1);
    // The greyed row is not numbered, so Quit is the fourth and not the
    // fifth.
    utassert(AppMenuRowForId(4, &action, &arg) && action == quit);
    utassert(!AppMenuRowForId(5, &action, &arg));
    utassert(!AppMenuRowForId(0, &action, &arg));
    utassert(!AppMenuRowForId(-1, &action, &arg));

    // Installing again starts the numbering over: the table describes the
    // bar that is up, not every bar that ever was.
    MenuRow one[1] = {};
    one[0].label = StrL("Quit");
    one[0].action = quit;
    menus[0].items = one;
    menus[0].n = 1;
    AppSetMenus(kApp, menus, 1);
    utassert(AppMenuRowForId(1, &action, &arg) && action == quit);
    utassert(!AppMenuRowForId(2, &action, &arg));
}

// A row with no label is a separator whether or not it says so, which is what
// lets a table leave the separators as zeroed rows.
static void AnUnlabelledRowIsASeparator() {
    uint32_t open = Act("test::Open");
    MenuRow rows[3] = {};
    rows[0].action = open; // no label: a separator, and not numbered
    rows[1].label = StrL("Open");
    rows[1].action = open;
    rows[2].label = StrL("Empty Submenu");
    rows[2].submenuN = 0; // nothing under it, so it is a row of its own

    MenuDef menus[1] = {};
    menus[0].name = StrL("File");
    menus[0].items = rows;
    menus[0].n = 3;
    AppSetMenus(kApp, menus, 1);

    uint32_t action = 0;
    utassert(AppMenuRowForId(1, &action, nullptr) && action == open);
    // The submenu row with nothing under it is an ordinary row, and reports
    // the nothing it was built with.
    utassert(AppMenuRowForId(2, &action, nullptr) && action == 0);
    utassert(!AppMenuRowForId(3, &action, nullptr));
}

// set_menus replaces the App's Vec, including with an empty one.
static void NoMenusClearsTheOnesThere() {
    uint32_t quit = Act("test::Quit");
    MenuRow rows[1] = {};
    rows[0].label = StrL("Quit");
    rows[0].action = quit;
    MenuDef menus[1] = {};
    menus[0].name = StrL("Test");
    menus[0].items = rows;
    menus[0].n = 1;
    AppSetMenus(kApp, menus, 1);

    AppSetMenus(kApp, nullptr, 0);
    AppSetMenus(nullptr, menus, 1);
    uint32_t action = 0;
    utassert(!AppMenuRowForId(kApp, 1, &action, nullptr));
}

static void MenusAreAppOwnedAndUnbounded() {
    const int n = 600;
    MenuRow* rows = AllocArray<MenuRow>(n);
    utassert(rows != nullptr);
    if (!rows) {
        return;
    }
    uint32_t many = Act("test::Many");
    for (int i = 0; i < n; i++) {
        rows[i].label = StrL("Row");
        rows[i].action = many;
        rows[i].arg = i;
    }
    MenuDef menu = {StrL("Many"), rows, n};
    AppSetMenus(kApp, &menu, 1);
    int64_t arg = -1;
    utassert(AppMenuRowForId(kApp, n, nullptr, &arg) && arg == n - 1);

    App other = {};
    MenuRow one = {StrL("Other"), Act("test::Other")};
    MenuDef otherMenu = {StrL("Other"), &one, 1};
    AppSetMenus(&other, &otherMenu, 1);
    uint32_t action = 0;
    utassert(AppMenuRowForId(&other, 1, &action, nullptr) &&
             action == one.action);
    utassert(AppMenuRowForId(kApp, n, nullptr, &arg) && arg == n - 1);

    AppMenuClear(&other);
    free(rows);
}

// The chord a menu row shows is looked up with the contexts ignored: the row
// is outside every element, and the binding that names its action is scoped
// to whichever one it belongs to — a field, for the Edit menu's rows.
static void AMenuRowFindsAChordBoundInsideAContext() {
    KeymapClear();
    uint32_t copy = Act("test::Copy");
    KeyBinding bindings[] = {
        {"ctrl-c", copy, "Input"},
    };
    KeymapBind(bindings, 1);

    KeyChord c = {};
    // The scoped lookup with no stack to resolve against finds nothing,
    // which is what a tooltip outside that element would say.
    utassert(!KeymapBindingForAction(copy, nullptr, 0, &c));
    utassert(KeymapAnyBindingForAction(copy, &c));
    utassert(c.vk == 'C' && c.ctrl && !c.shift);
    utassert(base::StrEq(KeyName(c.vk), StrL("c")));

    // The last binding for an action wins here too.
    KeyBinding later[] = {
        {"ctrl-alt-c", copy, "Editor"},
    };
    KeymapBind(later, 1);
    utassert(KeymapAnyBindingForAction(copy, &c) && c.alt);

    utassert(!KeymapAnyBindingForAction(Act("test::Unbound"), &c));
    utassert(!KeymapAnyBindingForAction(0, &c));

    // One action, many rows: the chord is the binding that carries the row's
    // argument, not the last binding of the action.
    uint32_t cmd = Act("test::Cmd");
    KeyBinding perArg[] = {
        {"ctrl-o", cmd, nullptr, 1},
        {"ctrl-s", cmd, nullptr, 2},
    };
    KeymapBind(perArg, 2);
    utassert(KeymapAnyBindingForActionArg(cmd, 1, &c) && c.vk == 'O');
    utassert(KeymapAnyBindingForActionArg(cmd, 2, &c) && c.vk == 'S');
    utassert(!KeymapAnyBindingForActionArg(cmd, 3, &c));
    utassert(!KeymapAnyBindingForActionArg(0, 1, &c));
    KeymapClear();
}

void TestAppMenu() {
    TestSuite("app_menu");
    OnlyTheRowsThatCanBeChosenAreNumbered();
    AnUnlabelledRowIsASeparator();
    NoMenusClearsTheOnesThere();
    MenusAreAppOwnedAndUnbounded();
    AMenuRowFindsAChordBoundInsideAContext();
    AppMenuClear(kApp);
}
