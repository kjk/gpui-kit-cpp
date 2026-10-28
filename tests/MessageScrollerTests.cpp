/* Ported from the tests in crates/ui/src/message_scroller.rs:
 * test_message_scroller_state_builder and test_message_scroller_builder.
 *
 * Both are `#[gpui::test]` upstream only because a ListState needs an App to
 * live in. The state here is a plain entity payload, so these drive it
 * directly; `cx` is null, which the mutators take to mean "nothing to
 * notify". */

#include "Test.h"

using namespace gpui::component;

static void TheStateSplicesScrollsAndFollowsItsTail() {
    MessageScrollerState state;
    MessageScrollerState::Init(&state, 3);

    utassert(state.ItemCount() == 3);
    utassert(!state.IsScrolledUp());
    utassert(state.IsFollowingTail());

    utassert(!state.ScrollToItem(nullptr, 3));
    utassert(state.Append(nullptr, 2));
    utassert(state.ItemCount() == 5);
    utassert(state.Prepend(nullptr, 1));
    utassert(state.ItemCount() == 6);
    // 5..7 runs off the end of a six-row list.
    utassert(!state.Splice(nullptr, 5, 7, 0));
    utassert(state.RemeasureItems(nullptr, 0, 6));
    utassert(!state.RemeasureItems(nullptr, 6, 7));
    utassert(state.ScrollToItem(nullptr, 2));
    utassert(!state.IsScrolledUp());
    utassert(!state.IsFollowingTail());
    // scroll_to_item sets the row at once, before a layout moves the pixels.
    ListOffset jumped = state.LogicalScrollTop();
    utassert(jumped.itemIx == 2);
    utassertnear(jumped.offsetInItem, 0.f);
    state.ScrollToEnd(nullptr);
    utassert(state.IsFollowingTail());
    // Right after scroll_to_end the anchor is past the last row.
    ListOffset end = state.LogicalScrollTop();
    utassert(end.itemIx == state.ItemCount());
    utassertnear(end.offsetInItem, 0.f);
    state.Reset(nullptr, 2);
    utassert(state.ItemCount() == 2);
    utassert(state.IsFollowingTail());
}

// The heights the C++ state keeps in place of ListState's own measurement:
// a splice keeps the surviving rows, resets the new ones, and remeasures the
// row whose "last" status may have flipped.
static void ASpliceKeepsTheSurvivingRowHeights() {
    MessageScrollerState state;
    MessageScrollerState::Init(&state, 4);
    for (int i = 0; i < 4; i++) {
        state.heights[i] = 100.f + (float)i;
    }

    utassert(state.Prepend(nullptr, 2));
    utassert(state.ItemCount() == 6);
    utassertnear(state.heights[0], kMessageScrollerEstimatedRowHeight);
    utassertnear(state.heights[1], kMessageScrollerEstimatedRowHeight);
    // The old first row lost its list inset, so it is remeasured.
    utassertnear(state.heights[2], kMessageScrollerEstimatedRowHeight);
    utassertnear(state.heights[3], 101.f);
    utassertnear(state.heights[4], 102.f);
    // The last row is remeasured: it lost its inter-row padding.
    utassertnear(state.heights[5], kMessageScrollerEstimatedRowHeight);

    state.heights[5] = 200.f;
    utassert(state.RemeasureItems(nullptr, 2, 3));
    utassertnear(state.heights[2], kMessageScrollerEstimatedRowHeight);
    utassertnear(state.heights[3], 101.f);
    state.Remeasure(nullptr);
    for (int i = 0; i < state.ItemCount(); i++) {
        utassertnear(state.heights[i], kMessageScrollerEstimatedRowHeight);
    }

    // is_scrolled_up needs a viewport smaller than the content, a released
    // tail, and an offset short of the end.
    state.handle.viewport = 100;
    state.handle.contentSize = 400;
    state.followTail = false;
    state.handle.offset = 0;
    utassert(state.IsScrolledUp());
    state.handle.offset = 300;
    utassert(!state.IsScrolledUp());
    state.followTail = true;
    state.handle.offset = 0;
    utassert(!state.IsScrolledUp());
    state.followTail = false;
    state.handle.contentSize = 50;
    utassert(!state.IsScrolledUp());
}

static void TheBuilderCarriesEveryOption() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    Entity<MessageScrollerState> state =
        EntityNewState<MessageScrollerState>(&app);
    MessageScrollerState::Init(state.Get(&app), 0);

    Style empty = {};
    MessageScroller* scroller =
        MessageScroller::New(&cx, StrL("message-scroller"), state, nullptr,
                             nullptr)
            ->Scrollbar(false)
            ->JumpButton(false)
            ->WithJumpButtonLabel(StrL("Latest"))
            ->WithContentStyle(empty, 0)
            ->WithListStyle(empty, 0)
            ->WithRowStyle(empty, 0)
            ->WithJumpButtonStyle(empty, 0)
            ->WithJumpButtonRenderer(nullptr)
            ->WithJumpButtonTransition(300)
            ->WithBottomFade(Rgb(255, 255, 255));

    utassert(!scroller->scrollbar);
    utassert(!scroller->jumpButton);
    utassert(base::StrEq(scroller->jumpButtonLabel, StrL("Latest")));
    utassertnear(scroller->jumpButtonTransitionMs, 300.f);
    utassert(scroller->hasBottomFade);
    utassert(RgbaEq(scroller->bottomFade, Rgb(255, 255, 255)));
    // The defaults the source starts from.
    MessageScroller* plain =
        MessageScroller::New(&cx, StrL("plain"), state, nullptr, nullptr);
    utassert(plain->scrollbar);
    utassert(plain->jumpButton);
    utassert(base::StrEq(plain->jumpButtonLabel, StrL("Jump to latest")));
    utassertnear(plain->jumpButtonTransitionMs,
                 kMessageScrollerJumpTransitionMs);
    utassert(!plain->hasBottomFade);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

// GPUI's `list` fills whatever box it is given and virtualizes against its
// bounds at prepaint. A scroller with no height is `size_full()`, its
// viewport and list filling it; a fixed height is the box's, and the
// viewport and list still fill that.
static void AScrollerWithoutAHeightFillsItsBox() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;
    Entity<MessageScrollerState> state =
        EntityNewState<MessageScrollerState>(&app);
    MessageScrollerState::Init(state.Get(&app), 3);

    auto build = [&](float fixed) {
        MessageScroller* s =
            MessageScroller::New(&cx, StrL("fill"), state, nullptr, nullptr)
                ->JumpButton(false);
        utassertnear(s->h, 0.f);
        if (fixed > 0) {
            s->H(fixed);
        }
        El* root = s->IntoEl();
        El* col = Div(a)->FlexCol()->W(300)->H(500)->Child(root);
        LayoutEl(nullptr, col, 0, 0, 300, 500, 14, Rgba{});
        return root;
    };
    El* root = build(0);
    El* viewport = root->first;
    El* list = viewport ? viewport->first : nullptr;
    utassertnear(root->h, 500.f);
    utassert(viewport && list);
    utassertnear(viewport->h, 500.f);
    utassertnear(list->h, 500.f);

    root = build(240);
    viewport = root->first;
    list = viewport ? viewport->first : nullptr;
    utassertnear(root->h, 240.f);
    utassert(viewport && list);
    utassertnear(viewport->h, 240.f);
    utassertnear(list->h, 240.f);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

// test_logical_scroll_top_reports_the_row_at_the_top. A 30-row scroller in a
// 400px box. Each row is 40px plus the 32px gap after every row but the last.
struct LogicalTopView {
    Entity<MessageScrollerState> state;

    static El* Row(void*, Ctx* cx, int) { return Div(cx->a)->H(40); }

    static El* Render(LogicalTopView* self, Ctx* cx) {
        return Div(cx->a)->W(400)->H(400)->Child(
            MessageScroller::New(cx, StrL("messages"), self->state, &Row,
                                 nullptr)
                ->Scrollbar(false)
                ->JumpButton(false)
                ->IntoEl());
    }
};

static void TheLogicalScrollTopReportsTheRowAtTheTop() {
    App* app = TestAppNew();
    component::Init(app);
    Entity<LogicalTopView> view = EntityNew<LogicalTopView>(app);
    LogicalTopView* self = view.Get(app);
    self->state = EntityNewState<MessageScrollerState>(app);
    MessageScrollerState::Init(self->state.Get(app), 30);
    Window* win = TestWindowOpen(app, view, 400, 400, 1);
    TestDraw(win);

    MessageScrollerState* st = self->state.Get(app);
    // Following the tail, layout settles on the row the tail starts on.
    ListOffset tail = st->LogicalScrollTop();
    utassert(tail.itemIx > 0 && tail.itemIx < 30);

    utassert(st->ScrollToItem(nullptr, 10));
    TestDraw(win);
    utassert(st->LogicalScrollTop().itemIx == 10);

    // A wheel scroll by one row: its 40px and the 32px gap after it.
    TestSimulateScrollWheel(win, Point{200, 200}, Point{0, -72});
    ListOffset after = st->LogicalScrollTop();
    utassert(after.itemIx == 11);
    utassertnear(after.offsetInItem, 0.f);

    TestAppFree(app);
}

// the_scrollbar_drag_range_reaches_the_end_of_the_content. The thumb's
// range is the list's scroll extent. An extent that stops short of the
// padded end leaves the reader above the last row.
struct ScrollEndView {
    Entity<MessageScrollerState> state;

    static El* Row(void*, Ctx* cx, int) { return Div(cx->a)->H(40); }

    static El* Render(ScrollEndView* self, Ctx* cx) {
        return Div(cx->a)->W(400)->H(400)->Child(
            MessageScroller::New(cx, StrL("messages"), self->state, &Row,
                                 nullptr)
                ->Scrollbar(false)
                ->JumpButton(false)
                ->IntoEl());
    }
};

static void TheScrollbarDragRangeReachesTheEndOfTheContent() {
    App* app = TestAppNew();
    component::Init(app);
    Entity<ScrollEndView> view = EntityNew<ScrollEndView>(app);
    ScrollEndView* self = view.Get(app);
    self->state = EntityNewState<MessageScrollerState>(app);
    MessageScrollerState::Init(self->state.Get(app), 10);
    Window* win = TestWindowOpen(app, view, 400, 400, 1);
    TestDraw(win);

    MessageScrollerState* st = self->state.Get(app);
    float max = st->handle.contentSize - st->handle.viewport;
    if (max < 0) {
        max = 0;
    }
    Ctx cx = {app, win, nullptr, view.id};
    ScrollEvent ev = {};
    ev.offsetY = max;
    MessageScrollerState::OnScroll(st, &cx, &ev);
    TestDraw(win);

    st = self->state.Get(app);
    float end = st->handle.contentSize - st->handle.viewport;
    utassert(end <= 0 || st->handle.offset >= end - 0.5f);
    utassert(st->IsFollowingTail());
    utassert(!st->IsScrolledUp());
    TestAppFree(app);
}

// a_custom_list_padding_rides_on_the_first_and_the_last_row. The list's
// vertical padding is measured with the first and last rows. Row padding
// is a different box, so it does not replace the list inset.
struct PaddedScrollerView {
    Entity<MessageScrollerState> state;
    float listTop = 0;
    float listBottom = 0;

    static El* Row(void*, Ctx* cx, int) { return Div(cx->a)->H(40); }

    static El* Render(PaddedScrollerView* self, Ctx* cx) {
        Style list = {};
        list.pad.top = self->listTop;
        list.pad.bottom = self->listBottom;
        Style row = {};
        row.pad.top = 6;
        row.pad.bottom = 6;
        return Div(cx->a)->W(400)->H(400)->Child(
            MessageScroller::New(cx, StrL("messages"), self->state, &Row,
                                 nullptr)
                ->Scrollbar(false)
                ->JumpButton(false)
                ->WithListStyle(list, StyleFieldPad)
                ->WithRowStyle(row, StyleFieldPad)
                ->IntoEl());
    }
};

static float PaddedExtent(App* app, float listTop, float listBottom) {
    Entity<PaddedScrollerView> view = EntityNew<PaddedScrollerView>(app);
    PaddedScrollerView* self = view.Get(app);
    self->state = EntityNewState<MessageScrollerState>(app);
    MessageScrollerState::Init(self->state.Get(app), 10);
    self->listTop = listTop;
    self->listBottom = listBottom;
    Window* win = TestWindowOpen(app, view, 400, 400, 1);
    TestDraw(win);
    return self->state.Get(app)->handle.contentSize;
}

static void ACustomListPaddingRidesOnTheFirstAndTheLastRow() {
    App* app = TestAppNew();
    component::Init(app);
    float first = PaddedExtent(app, 20, 40);
    float second = PaddedExtent(app, 20, 20);
    float third = PaddedExtent(app, 40, 20);
    utassertnear(first - second, 20.f);
    utassertnear(third - second, 20.f);
    TestAppFree(app);
}

void TestMessageScroller() {
    TestSuite("message_scroller");
    TheStateSplicesScrollsAndFollowsItsTail();
    ASpliceKeepsTheSurvivingRowHeights();
    TheBuilderCarriesEveryOption();
    AScrollerWithoutAHeightFillsItsBox();
    TheLogicalScrollTopReportsTheRowAtTheTop();
    TheScrollbarDragRangeReachesTheEndOfTheContent();
    ACustomListPaddingRidesOnTheFirstAndTheLastRow();
}
