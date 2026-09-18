/* Ported from crates/base/src/toast.rs advance.
 *
 * A toast's life is Starting, Present, Ending, gone, and the one subtlety is
 * that `paused` guards only the middle: a pointer resting on the stack stops
 * the countdown but not the animations. */

#include "Test.h"

static const ToastEntry* Find(const ToastStackState& s, int id) {
    for (int i = 0; i < s.entries.len; i++) {
        if (s.entries[i].id == id) {
            return &s.entries[i];
        }
    }
    return nullptr;
}

static void AToastAnimatesInThenCountsDownThenLeaves() {
    ToastStackState s;
    utassert(ToastPush(&s, 1, 1000));
    utassert(Find(s, 1)->status == ToastStatus::Starting);

    // 400 ms of transition, and it is up.
    utassert(ToastStackAdvance(&s, 400, false));
    utassert(Find(s, 1)->status == ToastStatus::Present);

    // Most of the timeout passes with nothing to report.
    utassert(!ToastStackAdvance(&s, 900, false));
    utassert(Find(s, 1)->status == ToastStatus::Present);

    // The rest of it starts it leaving.
    utassert(ToastStackAdvance(&s, 200, false));
    utassert(Find(s, 1)->status == ToastStatus::Ending);

    // 200 ms of exit, and it is gone.
    utassert(ToastStackAdvance(&s, 200, false));
    utassert(Find(s, 1) == nullptr);
    utassert(s.entries.len == 0);
}

static void PauseStopsTheCountdownButNotTheAnimations() {
    ToastStackState s;
    ToastPush(&s, 1, 1000);
    // Starting still finishes while paused: Rust guards only the Present arm.
    utassert(ToastStackAdvance(&s, 400, true));
    utassert(Find(s, 1)->status == ToastStatus::Present);

    // Now the countdown is frozen however long the pointer rests.
    utassert(!ToastStackAdvance(&s, 5000, true));
    utassert(Find(s, 1)->status == ToastStatus::Present);
    utassert(Find(s, 1)->timeoutRemainingMs == 1000);

    // And picks up exactly where it was when the pointer leaves.
    utassert(!ToastStackAdvance(&s, 999, false));
    utassert(ToastStackAdvance(&s, 1, false));
    utassert(Find(s, 1)->status == ToastStatus::Ending);
    // The exit runs while paused too.
    utassert(ToastStackAdvance(&s, 200, true));
    utassert(s.entries.len == 0);
}

static void AToastWithoutATimeoutStays() {
    ToastStackState s;
    ToastPush(&s, 1, 0);
    ToastStackAdvance(&s, 400, false);
    utassert(Find(s, 1)->status == ToastStatus::Present);
    utassert(!ToastStackAdvance(&s, 100000, false));
    utassert(Find(s, 1)->status == ToastStatus::Present);
    // It goes when it is dismissed, and not before.
    utassert(ToastRemove(&s, 1));
    utassert(s.entries.len == 0);
    utassert(!ToastRemove(&s, 1));
}

static void TheStackAdvancesEveryToastAtOnce() {
    ToastStackState s;
    ToastPush(&s, 1, 500);
    ToastPush(&s, 2, 1500);
    ToastStackAdvance(&s, 400, false);
    // The first times out and leaves while the second is still up.
    ToastStackAdvance(&s, 500, false);
    ToastStackAdvance(&s, 200, false);
    utassert(Find(s, 1) == nullptr);
    utassert(Find(s, 2)->status == ToastStatus::Present);
    utassert(s.entries.len == 1);
}

static void TheStackGrowsLikeRustsVecDeque() {
    ToastStackState s;
    for (int i = 0; i < 100; i++) {
        utassert(ToastPush(&s, i, 1000));
    }
    utassert(s.entries.len == 100);
}

static void TheStackIsAsTallAsItsFrontPlusASliverOfEachOther() {
    // Three cards of eighty, peeking fourteen apart.
    const float heights[3] = {80, 80, 80};
    float collapsedOff[3];
    float expandedOff[3];
    float expanded = 0;
    float collapsed =
        ToastStackGeometry(heights, 3, kToastCollapsedPeek, kToastExpandedGap,
                           false, collapsedOff, expandedOff, &expanded);
    // Open: every card plus the gaps between them.
    utassertnear(expanded, 80 * 3 + 14 * 2);
    // Closed: the front card plus a sliver of each one behind it.
    utassertnear(collapsed, 80 + 14 * 2);
    // The newest is at the front, so it sits at the top of the stack and the
    // older ones fall behind it.
    utassertnear(collapsedOff[2], 0.f);
    utassertnear(collapsedOff[1], 14.f);
    utassertnear(collapsedOff[0], 28.f);
    utassertnear(expandedOff[2], 0.f);
    utassertnear(expandedOff[1], 94.f);
    utassertnear(expandedOff[0], 188.f);
}

static void ABottomStackGrowsUpwards() {
    const float heights[2] = {60, 100};
    float collapsedOff[2];
    float expandedOff[2];
    float expanded = 0;
    float collapsed = ToastStackGeometry(heights, 2, 14, 14, true, collapsedOff,
                                         expandedOff, &expanded);
    utassertnear(expanded, 60 + 100 + 14);
    // The taller card behind is what the closed stack has to fit.
    utassertnear(collapsed, 100 + 14);
    // Anchored at the bottom, the front card is the lowest one.
    utassertnear(collapsedOff[1], collapsed - 100);
    utassertnear(collapsedOff[0], collapsed - 60 - 14);
    utassertnear(expandedOff[1], expanded - 100);
    utassertnear(expandedOff[0], 0.f);
}

static void AnEmptyStackHasNoGeometry() {
    float expanded = 1;
    utassertnear(ToastStackGeometry(nullptr, 0, 14, 14, false, nullptr, nullptr,
                                    &expanded),
                 0.f);
    utassertnear(expanded, 0.f);
}

static void ManagerReportsEveryLifecycleBoundary() {
    ToastManager<int, int> manager =
        ToastManager<int, int>::New(ToastMotion::Sonner());
    utassert(manager.IsEmpty());
    utassert(manager.Push(1, 10, ToastOptions::Timeout(5000), 0));
    utassert(manager.Len() == 1 && *manager.Get(1) == 10);

    ToastAdvance<int, int> presented = manager.Advance(400, false);
    utassert(presented.changed && presented.presented.len == 1);
    utassert(presented.presented[0] == 1);
    utassert(manager.At(0)->status == ToastTransitionStatus::Present);

    ToastAdvance<int, int> paused = manager.Advance(4400, true);
    utassert(!paused.changed);
    utassert(manager.At(0)->timeoutRemainingMs == 5000);
    ToastAdvance<int, int> ending = manager.Advance(9400, false);
    utassert(ending.changed && ending.ending.len == 1);
    utassert(manager.At(0)->status == ToastTransitionStatus::Ending);
    ToastAdvance<int, int> removed = manager.Advance(9600, false);
    utassert(removed.changed && removed.removed.len == 1);
    utassert(removed.removed[0].id == 1 && removed.removed[0].value == 10);
    utassert(manager.IsEmpty());
}

static void ManagerReplacesLimitsAndDismissesLikeTheSource() {
    ToastManager<int, int> manager =
        ToastManager<int, int>::New(ToastMotion::Sonner());
    ToastOptions persistent = ToastOptions::Persistent();
    manager.Push(1, 10, persistent, 100);
    manager.Push(2, 20, persistent, 100);
    int replaced = 0;
    bool hadReplaced = false;
    utassert(manager.Push(1, 30, persistent, 100, &replaced, &hadReplaced));
    utassert(hadReplaced && replaced == 10);
    utassert(manager.At(0)->id == 2 && manager.At(1)->id == 1);

    utassert(manager.Dismiss(2, 100));
    utassert(!manager.Dismiss(2, 100));
    ToastVisible<int, int> visible[3];
    int count = manager.Visible(1, visible, 3);
    utassert(count == 2);
    utassert(*visible[0].id == 2 && *visible[1].id == 1);
    Vec<int> dismissed = manager.DismissAll(100);
    utassert(len(dismissed) == 1 && dismissed[0] == 1);
}

static void StackBuilderUsesStableMeasurementsAndSourceMotion() {
    App app;
    Window* win = new Window();
    Arena* a = ArenaNew();
    win->app = &app;
    win->mouseX = 500;
    win->mouseY = 500;
    Ctx cx = {&app, win, a, {}};
    ToastStackState state;
    state.bounds = {0, 0, 300, 94};
    VecAppend(state.heights,
              {(uint32_t)HashClickId(StrL("first")), {0, 0, 300, 40}});
    VecAppend(state.heights,
              {(uint32_t)HashClickId(StrL("second")), {0, 0, 300, 80}});

    MotionSetReduced(true);
    El* collapsed = ToastStack::New(&cx, StrL("stack"), &state)
                        ->Item(StrL("first"), Div(a)->H(40))
                        ->Item(StrL("second"), Div(a)->H(80))
                        ->IntoEl();
    utassertnear(collapsed->style.height, 94.f);
    utassert(collapsed->first && collapsed->first->next);
    utassertnear(collapsed->first->style.absTop, 14.f);
    utassertnear(collapsed->first->style.absLeft, 7.5f);
    utassertnear(collapsed->first->next->style.absTop, 0.f);

    a->Reset();
    state.bounds = {0, 0, 300, 94};
    win->mouseX = 10;
    win->mouseY = 10;
    El* expanded = ToastStack::New(&cx, StrL("stack"), &state)
                       ->Item(StrL("first"), Div(a)->H(40))
                       ->Item(StrL("second"), Div(a)->H(80))
                       ->Placement(Anchor::TopRight)
                       ->IntoEl();
    utassert(state.IsExpanded());
    utassertnear(expanded->style.height, 134.f);
    utassertnear(expanded->first->style.absTop, 94.f);
    utassertnear(expanded->first->style.absLeft, 0.f);
    MotionSetReduced(false);

    WindowKeyedFree(win);
    delete win;
    ArenaDelete(a);
}

static void ToastRootOwnsTheExposedTransitionStatus() {
    App app;
    Window win;
    Arena* a = ArenaNew();
    Ctx cx = {&app, &win, a, {}};
    Toast* toast = Toast::New(&cx, StrL("toast"))
                       ->TransitionStatus(ToastTransitionStatus::Ending)
                       ->Child(TextEl(a, StrL("done")));
    utassert(toast->Status() == ToastTransitionStatus::Ending);
    El* root = toast->IntoEl();
    utassert(root->accessibility.role == AccessibilityRole::Alert);
    utassert(root->first && base::StrEq(root->first->text, StrL("done")));
    ArenaDelete(a);
}

void TestToast() {
    TestSuite("toast");
    AToastAnimatesInThenCountsDownThenLeaves();
    PauseStopsTheCountdownButNotTheAnimations();
    AToastWithoutATimeoutStays();
    TheStackAdvancesEveryToastAtOnce();
    TheStackGrowsLikeRustsVecDeque();
    TheStackIsAsTallAsItsFrontPlusASliverOfEachOther();
    ABottomStackGrowsUpwards();
    AnEmptyStackHasNoGeometry();
    ManagerReportsEveryLifecycleBoundary();
    ManagerReplacesLimitsAndDismissesLikeTheSource();
    StackBuilderUsesStableMeasurementsAndSourceMotion();
    ToastRootOwnsTheExposedTransitionStatus();
}
