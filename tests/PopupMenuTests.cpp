/* Ported from crates/ui/src/menu/popup_menu.rs.
 *
 * Rust binds enter, escape, up, down, left and right in the "PopupMenu" key
 * context, and so does this — the element declares the context, the keymap
 * resolves the chord against it, and the menu reads the action. select_up and
 * select_down walk only the clickable rows — a separator and a label are
 * stepped over — and both ends wrap; the two odd cases are Rust's own:
 * select_down with nothing selected takes row 0 whether or not it is clickable,
 * and select_up with nothing selected takes the last clickable row. Left and
 * Right depend on the side the submenus open towards. */

#include "Test.h"

using namespace gpui::component;

// The chord, through the keymap, to the thing the menu does about it —
// which is the whole path a keystroke takes now that the menu binds its keys
// instead of translating them.
static PopupMenuAction ForChord(const char* spec, Side side) {
    KeyChord c = {};
    utassert(KeyChordParse(Str(spec), &c));
    uint32_t ctx = KeyContextOf(PopupMenuContext());
    return PopupMenuActionOf(KeymapMatch(c, &ctx, 1).action, side);
}

static void TheBindingsAreTheOnesRustBinds() {
    PopupMenuInitKeys();

    utassert(ForChord("up", Side::Right) == PopupMenuAction::SelectPrev);
    utassert(ForChord("down", Side::Right) == PopupMenuAction::SelectNext);
    utassert(ForChord("enter", Side::Right) == PopupMenuAction::Confirm);
    utassert(ForChord("escape", Side::Right) == PopupMenuAction::Cancel);

    // A menu that opens to the right is reached into with Right.
    utassert(ForChord("right", Side::Right) == PopupMenuAction::OpenSubmenu);
    utassert(ForChord("left", Side::Right) == PopupMenuAction::CloseSubmenu);
    // One that opens to the left swaps them.
    utassert(ForChord("left", Side::Left) == PopupMenuAction::OpenSubmenu);
    utassert(ForChord("right", Side::Left) == PopupMenuAction::CloseSubmenu);

    // Nothing is bound to space in a menu, and an action from somewhere else
    // is not one of the menu's six.
    utassert(ForChord("space", Side::Right) == PopupMenuAction::None);
    utassert(PopupMenuActionOf(ActionOf(StrL("ui::SelectFirst")),
                               Side::Right) == PopupMenuAction::None);

    // Outside the menu's context the same chords resolve to nothing, which is
    // what keeps an arrow key from being the menu's while focus is elsewhere.
    KeyChord up = {};
    KeyChordParse(StrL("up"), &up);
    utassert(KeymapMatch(up, nullptr, 0).action == 0);
}

static void TheWalkStepsOverWhatCannotBeClicked() {
    // item, separator, item, label, item
    const bool clickable[5] = {true, false, true, false, true};

    utassert(PopupMenuNextIndex(clickable, 5, 0) == 2);
    utassert(PopupMenuNextIndex(clickable, 5, 2) == 4);
    // Past the last, back to the top.
    utassert(PopupMenuNextIndex(clickable, 5, 4) == 0);

    utassert(PopupMenuPrevIndex(clickable, 5, 4) == 2);
    utassert(PopupMenuPrevIndex(clickable, 5, 2) == 0);
    // Before the first, round to the last clickable one.
    utassert(PopupMenuPrevIndex(clickable, 5, 0) == 4);
}

static void NothingSelectedYet() {
    // Rust's select_down takes row 0 with nothing selected, even where row 0
    // is a separator.
    const bool clickable[3] = {false, true, true};
    utassert(PopupMenuNextIndex(clickable, 3, -1) == 0);
    // select_up takes the last clickable row instead.
    utassert(PopupMenuPrevIndex(clickable, 3, -1) == 2);
}

static void AMenuWithNoRows() {
    utassert(PopupMenuNextIndex(nullptr, 0, -1) == -1);
    utassert(PopupMenuPrevIndex(nullptr, 0, -1) == -1);
}

static void ALongStoryMenuFitsWithoutTruncation() {
    // There is no cap to fit inside any more; what this pins is that there
    // is not one. A hundred rows go in and a hundred come back out.
    PopupMenuState s;
    PopupMenuBeginRows(&s);
    for (int i = 0; i < 100; i++) {
        PopupMenuRow row;
        row.clickable = true;
        PopupMenuAddRow(&s, row);
    }
    utassert(s.rows.len == 100);
    PopupMenuBeginRows(&s);
    utassert(s.rows.len == 0);
}

// The whole path, as the element lays it out: a focusable root inside the
// "PopupMenu" context with the six actions on it, and a chord that walks in
// from the window. The themed element needs a paint backend to measure its
// rows, so this builds the same shape by hand.
static El* MenuLikeEl(Arena* a, Entity<PopupMenuState> menu, int focusId) {
    Listener onAction = ListenTo(menu, &PopupMenuState::OnAction);
    return Div(a)
        ->KeyContext(PopupMenuContext())
        ->FocusId(focusId)
        ->OnAction(action::Confirm(), onAction)
        ->OnAction(action::Cancel(), onAction)
        ->OnAction(action::SelectUp(), onAction)
        ->OnAction(action::SelectDown(), onAction);
}

static void AnOpenMenuAnswersTheChordItself() {
    KeymapClear();
    PopupMenuInitKeys();

    Arena* a = ArenaNew();
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<PopupMenuState> menu = EntityNewState<PopupMenuState>(&app);
    PopupMenuState* s = menu.Get(&app);
    s->open = true;
    // Three clickable rows, as the element would have recorded them.
    PopupMenuBeginRows(s);
    for (int i = 0; i < 3; i++) {
        PopupMenuRow row;
        row.clickable = true;
        PopupMenuAddRow(s, row);
    }

    El* root = Div(a)->Child(MenuLikeEl(a, menu, 7));
    FocusCollect(win, root);
    win->focusId = 7;

    // down, down: Rust's select_down takes row 0 first and then walks.
    utassert(WindowDispatchKeyAction(win, KeyDown, false, false, false));
    utassert(s->selected == 0);
    utassert(WindowDispatchKeyAction(win, KeyDown, false, false, false));
    utassert(s->selected == 1);
    utassert(WindowDispatchKeyAction(win, KeyUp, false, false, false));
    utassert(s->selected == 0);
    // escape closes it, and a menu that closes forgets its selection.
    utassert(WindowDispatchKeyAction(win, KeyEscape, false, false, false));
    utassert(!s->open && s->selected == -1);

    // Closed, the menu wants none of it: the action carries on outwards, the
    // way cx.propagate() does, and nothing else here keeps it.
    utassert(!WindowDispatchKeyAction(win, KeyDown, false, false, false));
    utassert(s->selected == -1);

    // And with focus outside the menu the chord is not even the menu's: the
    // context is not on the ancestry, so no binding matches at all.
    s->open = true;
    win->focusId = 0;
    utassert(!WindowDispatchKeyAction(win, KeyDown, false, false, false));
    utassert(s->selected == -1);

    delete win;
    ArenaDelete(a);
    KeymapClear();
}

static void TheMenuBarWrapsBothWays() {
    using namespace gpui::component;
    // on_move_right / on_move_left, over three titles.
    utassert(AppMenuBarNextIndex(0, 3) == 1);
    utassert(AppMenuBarNextIndex(2, 3) == 0);
    utassert(AppMenuBarPrevIndex(1, 3) == 0);
    utassert(AppMenuBarPrevIndex(0, 3) == 2);
    // Neither moves while nothing is open: Rust returns early on a None
    // selected_index.
    utassert(AppMenuBarNextIndex(-1, 3) == -1);
    utassert(AppMenuBarPrevIndex(-1, 3) == -1);
    // And an empty bar has nothing to move to.
    utassert(AppMenuBarNextIndex(0, 0) == 0);
}

// DropdownMenuPopover's trigger: `state.set_open(open); state.toggle_open()`,
// where `open` is what the frame that drew the trigger had. The trigger sits
// outside the menu, so the outside dismissal has already closed the menu by
// the time the trigger's click lands; the toggle is computed from the drawn
// state rather than the live one, which is what stops the press from
// reopening what the dismissal just closed. Nothing here asks who was hit.
static void ATriggerTogglesTheMenuAsItWasDrawn() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;

    PopupMenuState s;
    // Drawn closed: the press opens it.
    PopupMenuState::OnTriggerClick(&s, &cx, nullptr, 0);
    utassert(s.open);

    // Drawn open, and the outside dismissal has already run — so the flag
    // says closed by the time the click arrives. It still closes.
    s.open = false;
    PopupMenuState::OnTriggerClick(&s, &cx, nullptr, 1);
    utassert(!s.open);

    // Drawn open with no dismissal in between is the same answer.
    s.open = true;
    PopupMenuState::OnTriggerClick(&s, &cx, nullptr, 1);
    utassert(!s.open);

    delete win;
    EntityDropAll(&app);
}

static void SourceMenuItemKindsRemainDistinct() {
    PopupMenuItem item = PopupMenuItem::Item;
    PopupMenuItem element = PopupMenuItem::ElementItem;
    PopupMenuItem submenu = PopupMenuItem::Submenu;
    PopupMenuItem label = PopupMenuItem::Label;
    PopupMenuItem separator = PopupMenuItem::Separator;
    utassert(item != element);
    utassert(submenu != label);
    utassert(separator != item);
}

static void ContextMenuStateOwnsThePointerOpeningContract() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    Entity<PopupMenuState> menu = EntityNewState<PopupMenuState>(&app);

    ContextMenuState state;
    state.menu = menu;
    MouseDownEvent ev = {};
    ev.button = MouseButton::Left;
    ContextMenuState::OnMouseDown(&state, &cx, &ev);
    utassert(!state.open && !menu.Get(&app)->open);

    ev.button = MouseButton::Right;
    ev.x = 42;
    ev.y = 35;
    ev.el = {10, 7, 80, 60};
    ContextMenuState::OnMouseDown(&state, &cx, &ev);
    utassert(state.open && menu.Get(&app)->open);
    utassert(state.position.x == 32 && state.position.y == 28);
    utassert(menu.Get(&app)->x == 32 && menu.Get(&app)->y == 28);

    delete win;
    EntityDropAll(&app);
}

// item_click_fires_once_from_rows_without_an_id (context_menu.rs). Rust's
// rows shared one ContextMenuState because an id-less trigger falls back to
// its code location, and every row then drew the open menu. A trigger here
// always names itself, so rows built from one call site keep a state each and
// the right press opens only the menu of the row it landed on.
static void EachContextMenuTriggerKeepsItsOwnState() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* arena = ArenaNew();
    Ctx cx = {&app, win, arena, {}};
    Entity<PopupMenuState> menus[3];
    Entity<ContextMenuState> states[3];
    for (int i = 0; i < 3; i++) {
        menus[i] = EntityNewState<PopupMenuState>(&app);
        component::ContextMenu* row =
            component::ContextMenu::New(&cx, StrDup(arena, fmt("row-%d", i)));
        states[i] = row->state;
    }
    utassert(!(states[0].id == states[1].id));
    utassert(!(states[1].id == states[2].id));
    for (int i = 0; i < 3; i++) {
        states[i].Get(&app)->menu = menus[i];
    }

    MouseDownEvent ev = {};
    ev.button = MouseButton::Right;
    ContextMenuState::OnMouseDown(states[1].Get(&app), &cx, &ev);
    utassert(!menus[0].Get(&app)->open);
    utassert(menus[1].Get(&app)->open);
    utassert(!menus[2].Get(&app)->open);

    WindowKeyedFree(win);
    ArenaDelete(arena);
    delete win;
    EntityDropAll(&app);
}

static PopupMenuAction AppBarChord(AppMenuBarState* state, Ctx* cx,
                                   uint32_t actionId) {
    ActionEvent ev = {};
    ev.action = actionId;
    AppMenuBarState::OnAction(state, cx, &ev);
    return ev.propagate ? PopupMenuAction::None : PopupMenuAction::Confirm;
}

static void AppMenuBarBindsAndHandlesItsSourceActions() {
    KeymapClear();
    app_menu_bar::init();
    uint32_t context = KeyContextOf(AppMenuBarContext());
    KeyChord chord = {};
    utassert(KeyChordParse(StrL("right"), &chord));
    utassert(KeymapMatch(chord, &context, 1).action == action::SelectRight());
    utassert(KeyChordParse(StrL("escape"), &chord));
    utassert(KeymapMatch(chord, &context, 1).action == action::Cancel());

    App app;
    Window* win = new Window();
    win->app = &app;
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    AppMenuBarState state;
    state.count = 3;
    state.selected = 0;
    utassert(AppBarChord(&state, &cx, action::SelectRight()) ==
             PopupMenuAction::Confirm);
    utassert(state.selected == 1);
    AppBarChord(&state, &cx, action::SelectLeft());
    utassert(state.selected == 0);
    AppBarChord(&state, &cx, action::SelectLeft());
    utassert(state.selected == 2);
    AppBarChord(&state, &cx, action::Cancel());
    utassert(state.selected == -1);
    utassert(AppBarChord(&state, &cx, action::SelectRight()) ==
             PopupMenuAction::None);

    Arena* a = ArenaNew();
    cx.a = a;
    DropdownMenuPopover* dropdown =
        DropdownMenuPopover::New(&cx, StrL("source-popover"));
    dropdown->Anchor(Anchor::TopRight);
    utassert(dropdown->anchorRight);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    KeymapClear();
}

static void RootPopupPropagatesUnusedHorizontalActionsToTheMenuBar() {
    PopupMenuState menu;
    menu.open = true;
    menu.side = Side::Right;
    PopupMenuBeginRows(&menu);
    PopupMenuRow row;
    row.clickable = true;
    PopupMenuAddRow(&menu, row);
    menu.selected = 0;

    ActionEvent left = {};
    left.action = action::SelectLeft();
    PopupMenuState::OnAction(&menu, nullptr, &left);
    utassert(left.propagate);
    ActionEvent right = {};
    right.action = action::SelectRight();
    PopupMenuState::OnAction(&menu, nullptr, &right);
    utassert(right.propagate);

    menu.rows[0].submenu = true;
    right.propagate = false;
    PopupMenuState::OnAction(&menu, nullptr, &right);
    utassert(!right.propagate && menu.openSubmenu == 0);
}

// dropdown_menu.rs `open_without_dismiss_releases_the_menu`: a menu left
// open when its window goes away must go with the window, and so must the
// popover's deferred-popover registration, or every later right-click menu
// steps aside as if a popup were still showing. Rust breaks a strong-handle
// cycle; here the window's keyed state is what owned both entities, so
// closing the window is what has to let them go.
static void OpenWithoutDismissReleasesTheMenu() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Ctx cx = {&app, win, nullptr, {}};
    BaseGlobalStateInit(&app);

    Entity<PopupMenuState> menu = PopupMenuStateFor(&cx, StrL("menu"));
    Entity<PopoverState> popover = ElementStateEntity<PopoverState>(
        &cx, StrL("popover"), StrL("gpui::PopoverState"));
    menu.Get(&app)->open = true;
    PopoverSetOpen(&cx, popover, true);
    utassert(menu.Get(&app) != nullptr);
    utassert(BaseIsInDeferredContext(&app));

    // Close the window without dismissing the menu.
    WindowClosed(win);
    utassert(menu.Get(&app) == nullptr);
    utassert(popover.Get(&app) == nullptr);
    utassert(!BaseIsInDeferredContext(&app));

    WindowKeyedFree(win);
    delete win;
    AppGlobalClear(&app);
    EntityDropAll(&app);
}

void TestPopupMenu() {
    TheBindingsAreTheOnesRustBinds();
    TheWalkStepsOverWhatCannotBeClicked();
    NothingSelectedYet();
    AMenuWithNoRows();
    ALongStoryMenuFitsWithoutTruncation();
    AnOpenMenuAnswersTheChordItself();
    ATriggerTogglesTheMenuAsItWasDrawn();
    TheMenuBarWrapsBothWays();
    SourceMenuItemKindsRemainDistinct();
    ContextMenuStateOwnsThePointerOpeningContract();
    EachContextMenuTriggerKeepsItsOwnState();
    AppMenuBarBindsAndHandlesItsSourceActions();
    RootPopupPropagatesUnusedHorizontalActionsToTheMenuBar();
    OpenWithoutDismissReleasesTheMenu();
}
