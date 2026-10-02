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

// fallback.rs: where the platform has no menu of its own, `show` builds the
// drawn PopupMenu from the same rows — greyed rows greyed, a submenu as a
// submenu — anchored at the pointer, and choosing a row reports what the OS
// menu would have. Rust builds a window for this; the seams here are the
// overlay's copy of the rows and the confirm each drawn menu reports.
struct FallbackRecorder {
    int64_t chosen = 0;
    int calls = 0;
    static El* Render(FallbackRecorder*, Ctx* cx) { return Div(cx->a); }
    static void OnSelect(FallbackRecorder* self, Ctx*, const ClickEvent*,
                         int64_t id) {
        self->chosen = id;
        self->calls++;
    }
};

static void TheFallbackDrawsTheSameRowsAndReportsTheChosenOne() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* frame = ArenaNew();
    Entity<FallbackRecorder> rec = EntityNew<FallbackRecorder>(&app);
    Ctx cx = {&app, win, frame, rec.id};

    component::NativeMenu* sub =
        component::NativeMenu::New(&cx)
            ->Menu(StrL("Copy"), 10)
            ->MenuWithDisabled(StrL("Paste"), true, 12);
    component::NativeMenu* m = component::NativeMenu::New(&cx)
                                   ->Menu(StrL("New"), 1)
                                   ->MenuWithDisabled(StrL("Save"), true, 4)
                                   ->Separator()
                                   ->Submenu(StrL("Edit"), sub)
                                   ->MenuWithCheck(StrL("Wrap"), true, 3);
    m->OnSelect(ListenTo(rec, &FallbackRecorder::OnSelect));
    utassert(NativeMenuShowFallback(&cx, m, 30, 40));

    // The rows outlive the frame that built them.
    ArenaDelete(frame);
    frame = ArenaNew();
    cx.a = frame;
    NativeMenuFallback* f = NativeMenuFallbackOf(win);
    utassert(f && f->open);
    utassert(f->position.x == 30 && f->position.y == 40);
    utassert(len(f->menus) == 2 && f->nStates == 2);
    utassert(StrEq(f->menus[1]->items[0].label, StrL("Copy")));
    utassert(f->menus[0]->items[4].checked);
    PopupMenuState* root = f->states[0].Get(&app);
    PopupMenuState* edit = f->states[1].Get(&app);
    utassert(root && root->open && edit && !edit->open);

    // Only the rows the OS would have numbered report anything.
    EntityId rootId = f->states[0].id;
    EntityId editId = f->states[1].id;
    utassert(NativeMenuFallbackRow(f, rootId, 0)->id == 1);
    utassert(!NativeMenuFallbackRow(f, rootId, 1));
    utassert(!NativeMenuFallbackRow(f, rootId, 2));
    utassert(!NativeMenuFallbackRow(f, rootId, 3));
    utassert(NativeMenuFallbackRow(f, rootId, 4)->id == 3);
    utassert(NativeMenuFallbackRow(f, editId, 0)->id == 10);
    utassert(!NativeMenuFallbackRow(f, editId, 1));
    utassert(!NativeMenuFallbackRow(f, editId, 2));

    // Drawn, the greyed row cannot be chosen and the submenu row opens one.
    utassert(NativeMenuFallbackOverlay(&cx) != nullptr);
    utassert(root->rows.len == 5);
    utassert(root->rows[0].clickable && !root->rows[1].clickable);
    utassert(root->rows[3].submenu && root->rows[4].clickable);

    // Choosing Copy in the submenu reports its id and closes the whole menu.
    root->openSubmenu = 3;
    edit->open = true;
    edit->parent = f->states[0];
    Ctx at = cx;
    at.self = editId;
    PopupMenuConfirm(edit, &at, 0);
    FallbackRecorder* r = rec.Get(&app);
    utassert(r->calls == 1 && r->chosen == 10);
    utassert(!root->open && !edit->open);
    utassert(!f->open);
    utassert(NativeMenuFallbackOverlay(&cx) == nullptr);

    // A greyed row reports nothing. The last frame's builder is gone, so
    // the menu is built again, as the next right click would.
    m = component::NativeMenu::New(&cx)
            ->Menu(StrL("New"), 1)
            ->MenuWithDisabled(StrL("Save"), true, 4)
            ->OnSelect(ListenTo(rec, &FallbackRecorder::OnSelect));
    utassert(NativeMenuShowFallback(&cx, m, 0, 0));
    utassert(root->open);
    at.self = rootId;
    PopupMenuConfirm(root, &at, 1);
    utassert(r->calls == 1);

    WindowKeyedFree(win);
    EntityDropAll(&app);
    ArenaDelete(frame);
    delete win;
}

// The drawn menu closes on a release outside it (Rust: a press outside), so
// the release of the press it came up under — the story opens it from
// on_mouse_down — is not one; the release ending the next press is.
static void OnlyALaterPressDismissesTheMenu() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* frame = ArenaNew();
    Ctx cx = {&app, win, frame, {}};
    component::NativeMenu* m = component::NativeMenu::New(&cx)
                                   ->Menu(StrL("One"), 1);
    win->lastDownAt = 5;
    utassert(NativeMenuShowFallback(&cx, m, 10, 10));
    NativeMenuFallback* f = NativeMenuFallbackOf(win);
    utassert(f->openedPress == 5);
    El* placed = NativeMenuFallbackOverlay(&cx);
    utassert(placed && placed->first);
    Listener out = placed->first->onMouseUpOut;
    utassert(out.IsValid());
    MouseUpEvent up = {};
    up.button = MouseButton::Right;
    up.x = 500;
    up.y = 500;
    ListenerCall(&app, win, out, &up);
    utassert(f->states[0].Get(&app)->open);
    win->lastDownAt = 6;
    up.button = MouseButton::Left;
    ListenerCall(&app, win, out, &up);
    utassert(!f->states[0].Get(&app)->open);
    utassert(NativeMenuFallbackOverlay(&cx) == nullptr);
    WindowKeyedFree(win);
    EntityDropAll(&app);
    ArenaDelete(frame);
    delete win;
}

void TestNativeMenu() {
    TestSuite("native_menu");
    ARowCarriesWhatItWasBuiltWith();
    IconDataReplacesThePathAndTravelsWithTheRow();
    EveryRowAddedIsKept();
    OnlyTheRowsThatCanBeChosenAreNumbered();
    AGreyedSubmenuStillNumbersItsRows();
    AnEmptyMenuShowsNothing();
    TheFallbackDrawsTheSameRowsAndReportsTheChosenOne();
    OnlyALaterPressDismissesTheMenu();
}
