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

// crates/kit/tests/rendering.rs `menu_highlight_is_the_keyboard_cursor`: a
// menu has one highlight, and it is the keyboard cursor. The pointer moves it
// on hover and keys move it from wherever it is; a key press under a still
// pointer ends the hover (GPUI's keyboard modality) and must not put the
// highlight out. Rust reads pixels; this reads the selection the rows are lit
// from, driving the window the way the platform does.
static void MenuHighlightIsTheKeyboardCursor() {
    KeymapClear();
    PopupMenuInitKeys();

    Arena* a = ArenaNew();
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<PopupMenuState> menu = EntityNewState<PopupMenuState>(&app);
    PopupMenuState* s = menu.Get(&app);
    s->open = true;
    PopupMenuBeginRows(s);
    Listener hover = ListenTo(menu, &PopupMenuState::OnItemHover, 0);
    for (int i = 0; i < 4; i++) {
        PopupMenuRow row;
        row.clickable = true;
        PopupMenuAddRow(s, row);
        HitRect hr = {};
        hr.id = 100 + i;
        hr.bounds = {0, (float)i * 26, 200, 26};
        hr.onHover = ListenerArg(hover, i);
        VecAppend(win->paint.hits, hr);
    }
    El* root = Div(a)->Child(MenuLikeEl(a, menu, 7));
    FocusCollect(win, root);
    win->focusId = 7;

    auto moveTo = [&](int row) {
        PlatformInput in = {};
        in.kind = PlatformInputKind::MouseMove;
        in.mouseMove.x = 20;
        in.mouseMove.y = (float)row * 26 + 13;
        WindowDispatchInput(win, &in);
    };

    moveTo(1);
    utassert(s->selected == 1 && !WindowLastInputWasKeyboard(win));
    // `down` moves the highlight on, and the hovered row is not lit beside
    // it: the key ended the hover.
    WindowKeyDown(win, KeyDown, false, false, false);
    utassert(s->selected == 2 && WindowLastInputWasKeyboard(win));
    utassert(win->hoverId == 0);
    // Moving the pointer brings the highlight back to the row under it.
    moveTo(1);
    utassert(s->selected == 1 && !WindowLastInputWasKeyboard(win));
    // An unbound key under a still pointer keeps it there...
    WindowKeyDown(win, KeyX, false, false, false);
    utassert(s->selected == 1 && WindowLastInputWasKeyboard(win));
    // ...so `down` moves on from it rather than restarting at the top.
    WindowKeyDown(win, KeyDown, false, false, false);
    utassert(s->selected == 2);
    // A modifier on its own is ModifiersChanged in GPUI, not a key press.
    moveTo(2);
    WindowKeyDown(win, KeyShift, true, false, false);
    utassert(!WindowLastInputWasKeyboard(win) && win->hoverId == 102);

    VecReset(win->paint.hits);
    delete win;
    ArenaDelete(a);
    EntityDropAll(&app);
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

// menu.rs long_press_*: a finger has no right button. The trigger's long
// press opens the same menu, unless the point is an input or a glyph, which
// keep their own word selection.
struct LongPressMenus {
    InputState input;
    Entity<ContextMenuState> rowMenu;
    Entity<ContextMenuState> inputMenu;
    Entity<ContextMenuState> textMenu;

    static PopupMenu* CopyMenu(Ctx* cx, Str id) {
        return PopupMenu::New(cx, id)->Menu(StrL("Copy"));
    }

    static El* Render(LongPressMenus* self, Ctx* cx) {
        El* row = Div(cx->a)->W(320)->H(40)->Child(
            TextEl(cx->a, StrL("Long press me")));
        ContextMenu* rowMenu = ContextMenu::New(cx, StrL("row"))
                                   ->Child(row)
                                   ->Menu(CopyMenu(cx, StrL("row-menu")));
        self->rowMenu = rowMenu->state;

        El* field = component::Input::New(cx, StrL("row-input"), &self->input)
                        ->W(320)
                        ->IntoEl();
        ContextMenu* inputMenu = ContextMenu::New(cx, StrL("input-row"))
                                     ->Child(field)
                                     ->Menu(CopyMenu(cx, StrL("input-menu")));
        self->inputMenu = inputMenu->state;

        El* text = Div(cx->a)->W(320)->H(80)->Child(
            TextEl(cx->a, StrL("quick select"))->Selectable());
        ContextMenu* textMenu = ContextMenu::New(cx, StrL("text-row"))
                                    ->Child(text)
                                    ->Menu(CopyMenu(cx, StrL("text-menu")));
        self->textMenu = textMenu->state;

        return Div(cx->a)
            ->SizeFull()
            ->Pad(16)
            ->FlexCol()
            ->Gap(16)
            ->Child(rowMenu->IntoEl()->Click(101))
            ->Child(inputMenu->IntoEl()->Click(102))
            ->Child(textMenu->IntoEl()->Click(103));
    }
};

static Bounds HitBounds(Window* win, int id) {
    for (int i = 0; i < win->paint.hits.len; i++) {
        if (win->paint.hits[i].id == id) {
            return win->paint.hits[i].bounds;
        }
    }
    return {};
}

static void DispatchLongPress(Window* win, App* app, Point at) {
    for (TouchPhase phase : {TouchPhase::Started, TouchPhase::Ended}) {
        PlatformInput in = InputLongPress(phase, at, at);
        WindowDispatchInput(win, &in);
        TestFlushEffects(app);
        TestRunUntilParked(app);
        TestDraw(win);
    }
}

static void LongPressOpensTheContextMenu() {
    App* app = TestAppNew();
    Entity<LongPressMenus> view = EntityNew<LongPressMenus>(app);
    InputSetValue(&view.Get(app)->input, StrL("quick select"));
    Window* win = TestWindowOpen(app, view, 640, 480);
    TestDraw(win);

    Bounds row = HitBounds(win, 101);
    utassert(row.w > 0);
    DispatchLongPress(win, app, {row.CenterX(), row.CenterY()});
    utassert(view.Get(app)->rowMenu.Get(app)->open);
    utassert(!view.Get(app)->inputMenu.Get(app)->open);
    utassert(!view.Get(app)->textMenu.Get(app)->open);
    TestAppFree(app);
}

static void LongPressOnAnInputInAContextMenuTriggerSelects() {
    App* app = TestAppNew();
    Entity<LongPressMenus> view = EntityNew<LongPressMenus>(app);
    InputState* input = &view.Get(app)->input;
    InputSetValue(input, StrL("quick select"));
    Window* win = TestWindowOpen(app, view, 640, 480);
    TestDraw(win);
    utassert(input->inputBounds.w > 0);
    Point at = {input->inputBounds.x + 24, input->inputBounds.CenterY()};
    DispatchLongPress(win, app, at);
    utassert(base::StrEq(InputSelectedValue(input), "quick"));
    utassert(!view.Get(app)->inputMenu.Get(app)->open);
    utassert(!view.Get(app)->rowMenu.Get(app)->open);
    TestAppFree(app);
}

static const TextHit* FindPaintedText(Window* win, const char* want) {
    for (int i = 0; i < win->paint.texts.len; i++) {
        if (base::StrEq(win->paint.texts[i].text, Str(want))) {
            return &win->paint.texts[i];
        }
    }
    return nullptr;
}

static void LongPressOnTextInAContextMenuTriggerSelects() {
    App* app = TestAppNew();
    Entity<LongPressMenus> view = EntityNew<LongPressMenus>(app);
    Window* win = TestWindowOpen(app, view, 640, 480);
    TestDraw(win);
    const TextHit* hit = FindPaintedText(win, "quick select");
    utassert(hit);
    if (!hit) {
        TestAppFree(app);
        return;
    }
    Point at = {hit->bounds.x + 4, hit->bounds.CenterY()};
    DispatchLongPress(win, app, at);
    char buf[32];
    int n = WindowSelectionText(win, buf, 32);
    utassert(base::StrEq(Str(buf, n), StrL("quick")));
    TouchSelectionSnapshot snap = {};
    utassert(WindowSelectionTouchSnapshot(win, &snap));
    utassert(!view.Get(app)->textMenu.Get(app)->open);
    TestAppFree(app);
}

// kit menu.rs nested_context_menu_uses_the_innermost_right_click_target.
// A right click on the inner trigger opens only that menu. A right click on
// the card, away from the button, opens only the ancestor menu.
struct NestedContextMenus {
    int innerChosen = 0;
    int outerChosen = 0;
    Entity<PopupMenuState> innerMenu;
    Entity<PopupMenuState> outerMenu;

    static void OnCopy(NestedContextMenus* self, Ctx* cx, const ClickEvent*) {
        self->innerChosen++;
        Notify(cx);
    }

    static void OnRemove(NestedContextMenus* self, Ctx* cx, const ClickEvent*) {
        self->outerChosen++;
        Notify(cx);
    }

    static El* Render(NestedContextMenus* self, Ctx* cx) {
        PopupMenu* inner =
            PopupMenu::New(cx, StrL("inner-menu"))
                ->Menu(StrL("Copy link"))
                ->OnClick(Listen(cx, &NestedContextMenus::OnCopy));
        self->innerMenu = inner->state;
        El* share = ContextMenu::New(cx, StrL("share-menu"))
                        ->Child(component::Button::New(cx, StrL("share"))
                                    ->Label(StrL("Share"))
                                    ->IntoEl())
                        ->Menu(inner)
                        ->IntoEl();
        El* title = Div(cx->a)
                        ->Id(StrL("card-title"))
                        ->H(40)
                        ->Child(TextEl(cx->a, StrL("Card")));
        PopupMenu* outer =
            PopupMenu::New(cx, StrL("outer-menu"))
                ->Menu(StrL("Remove card"))
                ->OnClick(Listen(cx, &NestedContextMenus::OnRemove));
        self->outerMenu = outer->state;
        El* card = Div(cx->a)
                       ->Id(StrL("card"))
                       ->FlexCol()
                       ->Gap(16)
                       ->W(320)
                       ->Child(title)
                       ->Child(share);
        return Div(cx->a)->SizeFull()->Pad(16)->Child(
            ContextMenu::New(cx, StrL("card-menu"))
                ->Child(card)
                ->Menu(outer)
                ->IntoEl());
    }
};

static void RightClickAt(Window* win, Point at) {
    TestSimulateMouseDown(win, at, MouseButton::Right);
    TestSimulateMouseUp(win, at, MouseButton::Right);
    TestDraw(win);
}

static void NestedContextMenuUsesTheInnermostRightClick() {
    App* app = TestAppNew();
    Entity<NestedContextMenus> view = EntityNew<NestedContextMenus>(app);
    Window* win = TestWindowOpen(app, view, 640, 480);
    TestDraw(win);
    // Button labels are not text hits. The card is the tall hit; the share
    // button is the short one inside it. The title is the card's top band.
    Bounds card = {};
    Bounds share = {};
    for (int i = 0; i < win->paint.hits.len; i++) {
        Bounds b = win->paint.hits[i].bounds;
        if (b.h > card.h) {
            share = card;
            card = b;
        } else if (b.h > share.h) {
            share = b;
        }
    }
    utassert(card.h > share.h && share.h > 0);
    Point shareAt = {share.CenterX(), share.CenterY()};
    Point titleAt = {card.x + 24, card.y + 12};
    RightClickAt(win, shareAt);
    NestedContextMenus* self = view.Get(app);
    utassert(self->innerMenu.Get(app)->open);
    utassert(!self->outerMenu.Get(app)->open);
    TestSimulateKeystrokes(win, "down");
    TestDraw(win);
    utassert(self->innerMenu.Get(app)->selected == 0);
    TestSimulateKeystrokes(win, "enter");
    TestDraw(win);
    utassert(!self->innerMenu.Get(app)->open);
    utassert(self->innerChosen == 1);
    utassert(self->outerChosen == 0);

    RightClickAt(win, titleAt);
    utassert(!self->innerMenu.Get(app)->open);
    utassert(self->outerMenu.Get(app)->open);
    TestSimulateKeystrokes(win, "escape");
    TestDraw(win);
    utassert(!self->outerMenu.Get(app)->open);
    utassert(self->innerChosen == 1);
    utassert(self->outerChosen == 0);

    RightClickAt(win, titleAt);
    TestSimulateKeystrokes(win, "down");
    TestDraw(win);
    utassert(self->outerMenu.Get(app)->selected == 0);
    TestSimulateKeystrokes(win, "enter");
    TestDraw(win);
    utassert(!self->outerMenu.Get(app)->open);
    utassert(self->innerChosen == 1);
    utassert(self->outerChosen == 1);
    TestAppFree(app);
}

static void LongPressOnBlankSpaceInATextTriggerOpensTheMenu() {
    App* app = TestAppNew();
    Entity<LongPressMenus> view = EntityNew<LongPressMenus>(app);
    Window* win = TestWindowOpen(app, view, 640, 480);
    TestDraw(win);
    Bounds box = HitBounds(win, 103);
    utassert(box.h > 40);
    Point at = {box.Right() - 12, box.Bottom() - 12};
    DispatchLongPress(win, app, at);
    char buf[32];
    int n = WindowSelectionText(win, buf, 32);
    utassert(n == 0);
    utassert(view.Get(app)->textMenu.Get(app)->open);
    utassert(!view.Get(app)->rowMenu.Get(app)->open);
    TestAppFree(app);
}

void TestPopupMenu() {
    TheBindingsAreTheOnesRustBinds();
    TheWalkStepsOverWhatCannotBeClicked();
    NothingSelectedYet();
    AMenuWithNoRows();
    ALongStoryMenuFitsWithoutTruncation();
    AnOpenMenuAnswersTheChordItself();
    MenuHighlightIsTheKeyboardCursor();
    ATriggerTogglesTheMenuAsItWasDrawn();
    TheMenuBarWrapsBothWays();
    SourceMenuItemKindsRemainDistinct();
    ContextMenuStateOwnsThePointerOpeningContract();
    EachContextMenuTriggerKeepsItsOwnState();
    LongPressOpensTheContextMenu();
    LongPressOnAnInputInAContextMenuTriggerSelects();
    LongPressOnTextInAContextMenuTriggerSelects();
    LongPressOnBlankSpaceInATextTriggerOpensTheMenu();
    NestedContextMenuUsesTheInnermostRightClick();
    AppMenuBarBindsAndHandlesItsSourceActions();
    RootPopupPropagatesUnusedHorizontalActionsToTheMenuBar();
    OpenWithoutDismissReleasesTheMenu();
}
