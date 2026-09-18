/* Ported from crates/base/src/focus_trap.rs and the Tab actions that read it
 * in crates/ui/src/root.rs, plus FocusHandle::tab_index / tab_stop.
 *
 * A trap is a container that Tab cannot leave. The rules are: focus inside a
 * trap cycles within it, focus outside every trap never wanders into one, and
 * a container that has just opened takes focus into itself. On top of that,
 * the traversal visits the lowest tab index first and skips anything that is
 * focusable without being a stop. */

#include "Test.h"

// A window with focusables in tree order, as a frame's FocusCollect leaves
// them. `traps[i]` is the trap element i sits inside, 0 for none.
static Window* WindowWithFocusables(const int* ids, const int* traps, int n) {
    Window* win = new Window();
    for (int i = 0; i < n; i++) {
        FocusRect fr;
        fr.id = ids[i];
        fr.trapId = traps[i];
        VecAppend(win->focusEls, fr);
    }
    return win;
}

static void TabInsideATrapCyclesWithinIt() {
    // Two outside, then three in a trap.
    int ids[] = {1, 2, 11, 12, 13};
    int traps[] = {0, 0, 7, 7, 7};
    Window* win = WindowWithFocusables(ids, traps, 5);

    win->focusId = 11;
    utassert(FocusTrapActive(win) == 7);
    utassert(FocusTrapTab(win, false) == 12);
    utassert(FocusTrapTab(win, false) == 13);
    // The end of the trap comes back to its start rather than to the page.
    utassert(FocusTrapTab(win, false) == 11);
    // And backwards off the front goes to the back of the same trap.
    utassert(FocusTrapTab(win, true) == 13);
    delete win;
}

static void TabOutsideEveryTrapNeverWandersIn() {
    int ids[] = {1, 2, 11, 12, 3};
    int traps[] = {0, 0, 7, 7, 0};
    Window* win = WindowWithFocusables(ids, traps, 5);

    win->focusId = 1;
    utassert(FocusTrapActive(win) == 0);
    utassert(FocusTrapTab(win, false) == 2);
    // 11 and 12 are behind the trap, so the page's own cycle skips them.
    utassert(FocusTrapTab(win, false) == 3);
    utassert(FocusTrapTab(win, false) == 1);
    delete win;
}

static void TwoTrapsDoNotReachEachOther() {
    int ids[] = {11, 12, 21, 22};
    int traps[] = {7, 7, 8, 8};
    Window* win = WindowWithFocusables(ids, traps, 4);

    win->focusId = 12;
    utassert(FocusTrapTab(win, false) == 11);
    win->focusId = 21;
    utassert(FocusTrapTab(win, false) == 22);
    utassert(FocusTrapTab(win, false) == 21);
    delete win;
}

// The dialog that has just opened: Rust tracks focus on the trap container, so
// the container holds focus from its first frame.
static void AnOpenContainerTakesFocusIntoItself() {
    int ids[] = {1, 11, 12};
    int traps[] = {0, 7, 7};
    Window* win = WindowWithFocusables(ids, traps, 3);

    win->focusId = 1;
    FocusTrapArm(win, 7);
    FocusTrapApplyPending(win);
    utassert(win->focusId == 11);
    utassert(FocusTrapActive(win) == 7);

    // Once inside, the next frame leaves focus where the reader put it.
    win->focusId = 12;
    FocusTrapArm(win, 7);
    FocusTrapApplyPending(win);
    utassert(win->focusId == 12);
    delete win;
}

// Re-rendering an already-open modal does not reclaim focus that escaped it.
// Rust reverted the render-time focus call in d5821f27, leaving focus only on
// the open transition.
static void AnOpenContainerDoesNotReclaimEscapedFocus() {
    int ids[] = {1, 11, 12};
    int traps[] = {0, 7, 7};
    Window* win = WindowWithFocusables(ids, traps, 3);
    win->previousTrap = 7;
    win->focusId = 1;
    FocusTrapArm(win, 7);
    FocusTrapApplyPending(win);
    utassert(win->focusId == 1);
    delete win;
}

static void ATrapWithNothingFocusableLeavesFocusAlone() {
    int ids[] = {1, 2};
    int traps[] = {0, 0};
    Window* win = WindowWithFocusables(ids, traps, 2);

    win->focusId = 2;
    utassert(!FocusTrapEnter(win, 7));
    FocusTrapArm(win, 7);
    FocusTrapApplyPending(win);
    utassert(win->focusId == 2);
    // Tab still moves, because there is no trap holding it.
    utassert(FocusTrapTab(win, false) == 1);
    delete win;
}

// Nothing armed is the ordinary frame: focus is not touched.
static void NothingArmedTouchesNothing() {
    int ids[] = {1, 11};
    int traps[] = {0, 7};
    Window* win = WindowWithFocusables(ids, traps, 2);

    win->focusId = 1;
    win->pendingTrap = 0;
    FocusTrapApplyPending(win);
    utassert(win->focusId == 1);
    delete win;
}

// A trap id is a name's hash, so the same container traps under the same id
// every frame, and two names do not collide.
static void ATrapIsNamedNotNumbered() {
    utassert(FocusTrapId(StrL("dialog")) == FocusTrapId(StrL("dialog")));
    utassert(FocusTrapId(StrL("dialog")) != FocusTrapId(StrL("sheet")));
    utassert(FocusTrapId(StrL("dialog")) != 0);
}

// FocusTrapContainer delegates layout to the supplied element in Rust. The
// arena tree expresses the same wrapper as an in-place semantic refinement:
// wrapper identity, tracked focus and descendant trap membership.
static void ThePublicContainerRefinesItsElement() {
    App app;
    Window* win = new Window();
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    FocusHandle focus = {77};
    El* child = Div(a)->Child(Div(a)->FocusId(78));
    El* trap = FocusTrapContainer::New(&cx, StrL("modal"), focus, child);
    utassert(trap == child);
    utassert(StrEqI(trap->id, "modal"));
    utassert(trap->style.focusId == 77);
    utassert(trap->style.trapId == 77);

    FocusCollect(win, trap);
    utassert(win->focusEls.len == 2);
    utassert(win->focusEls[0].id == 77);
    utassert(win->focusEls[0].trapId == 77);
    utassert(win->focusEls[1].id == 78);
    utassert(win->focusEls[1].trapId == 77);

    delete win;
    ArenaDelete(a);
}

// FocusHandle::tab_stop(false): still focusable, simply not somewhere Tab
// stops. Whether it has a focus appearance is a separate UI-layer choice. An
// input's clear button and a dock tab bar's tools are what Rust turns it off
// for.
static void TabSkipsWhatIsNotAStop() {
    int ids[] = {1, 2, 3};
    int traps[] = {0, 0, 0};
    Window* win = WindowWithFocusables(ids, traps, 3);
    win->focusEls[1].tabStop = false;

    win->focusId = 1;
    utassert(FocusTrapTab(win, false) == 3);
    utassert(FocusTrapTab(win, true) == 1);
    delete win;
}

// A trap made only of non-stops leaves focus where it is rather than spinning.
static void ATrapOfNonStopsLeavesFocusAlone() {
    int ids[] = {1, 11, 12};
    int traps[] = {0, 7, 7};
    Window* win = WindowWithFocusables(ids, traps, 3);
    win->focusEls[1].tabStop = false;
    win->focusEls[2].tabStop = false;

    win->focusId = 11;
    utassert(FocusTrapTab(win, false) == 11);
    // Nor does arming the trap put focus on one of them.
    win->focusId = 1;
    utassert(!FocusTrapEnter(win, 7));
    utassert(win->focusId == 1);
    delete win;
}

// Arming a trap focuses its own container, whatever is inside it. Rust's
// dialog is `track_focus(&self.focus).focus_trap(.., &self.focus)` and
// nothing focuses a control within it, so an alert opens with focus on the
// dialog rather than a ring around its Cancel button.
static void ATrapFocusesItsOwnContainer() {
    int ids[] = {1, 7, 12};
    int traps[] = {0, 7, 7};
    Window* win = WindowWithFocusables(ids, traps, 3);
    // The container tracks focus without taking tab, and so does the one
    // control inside it.
    win->focusEls[1].tabStop = false;
    win->focusEls[2].tabStop = false;

    win->focusId = 1;
    FocusTrapArm(win, 7, 7);
    FocusTrapApplyPending(win);
    utassert(win->focusId == 7);

    // A real stop inside it does not take the focus off the container.
    win->focusEls[2].tabStop = true;
    win->focusId = 1;
    FocusTrapArm(win, 7, 7);
    FocusTrapApplyPending(win);
    utassert(win->focusId == 7);

    // A host that is not on screen falls back to the first stop inside, so
    // the trap still has focus to keep.
    win->focusId = 1;
    FocusTrapArm(win, 7, 99);
    FocusTrapApplyPending(win);
    utassert(win->focusId == 12);
    delete win;
}

// tab_index: the traversal is by index first and by paint order inside it, so
// a control can be reached before one laid out above it. The sort is stable,
// which is what keeps everything at the default index in tree order.
static void TheTabIndexGroupsTheTraversal() {
    Arena* a = ArenaNew();
    Window* win = new Window();
    // Painted 1, 2, 3, 4 — but 3 asks to come first and 1 to come last.
    El* root = Div(a)
                   ->Child(Div(a)->FocusId(1)->TabIndex(2))
                   ->Child(Div(a)->FocusId(2))
                   ->Child(Div(a)->FocusId(3)->TabIndex(-1))
                   ->Child(Div(a)->FocusId(4));
    FocusCollect(win, root);
    utassert(win->focusEls.len == 4);
    utassert(win->focusEls[0].id == 3);
    utassert(win->focusEls[1].id == 2);
    utassert(win->focusEls[2].id == 4);
    utassert(win->focusEls[3].id == 1);

    win->focusId = 3;
    utassert(FocusTrapTab(win, false) == 2);
    utassert(FocusTrapTab(win, false) == 4);
    utassert(FocusTrapTab(win, false) == 1);
    utassert(FocusTrapTab(win, false) == 3);
    delete win;
    ArenaDelete(a);
}

// FocusHandle's three questions, which are what a popover, a select and a
// popup menu ask when they open and close: which element has focus, whether
// focus is theirs, and putting back what they parked.
static void AHandleParksFocusAndPutsItBack() {
    // A trigger, and a container with two things inside it.
    int ids[] = {1, 2, 7, 11, 12};
    int traps[] = {0, 0, 0, 7, 7};
    Window* win = WindowWithFocusables(ids, traps, 5);

    win->focusId = 2;
    utassert(WindowFocusedId(win) == 2);
    // The container takes focus, and says focus is its own — both when it is
    // on the container itself and when it is on something inside it.
    WindowSetFocusId(win, 7);
    utassert(WindowFocusWithin(win, 7));
    win->focusId = 12;
    utassert(WindowFocusWithin(win, 7));
    utassert(!WindowFocusWithin(win, 2));
    utassert(!WindowFocusWithin(win, 0));

    // And back to what was parked.
    utassert(WindowRestoreFocus(win, 2));
    utassert(WindowFocusedId(win) == 2);
    // A handle whose element is no longer on screen is nothing to do, which
    // is what Rust's weak handle answers.
    utassert(!WindowRestoreFocus(win, 99));
    utassert(WindowFocusedId(win) == 2);
    delete win;
}

void TestFocusTrap() {
    TestSuite("focus_trap");
    TabInsideATrapCyclesWithinIt();
    TabOutsideEveryTrapNeverWandersIn();
    TwoTrapsDoNotReachEachOther();
    AnOpenContainerTakesFocusIntoItself();
    AnOpenContainerDoesNotReclaimEscapedFocus();
    ATrapWithNothingFocusableLeavesFocusAlone();
    NothingArmedTouchesNothing();
    ATrapIsNamedNotNumbered();
    ThePublicContainerRefinesItsElement();
    TabSkipsWhatIsNotAStop();
    ATrapOfNonStopsLeavesFocusAlone();
    ATrapFocusesItsOwnContainer();
    TheTabIndexGroupsTheTraversal();
    AHandleParksFocusAndPutsItBack();
}
