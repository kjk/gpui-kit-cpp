/* Ported from crates/component/src/carousel/state.rs, mod tests.
 *
 * Constructors, snap points, nearest-index, navigation, events, axis lock,
 * finish-drag origin, and looping wrap/settle are driven through SetGeometry
 * and the gesture seams. Timer-backed trackpad settle is invoked as the
 * timeout handler (the 28ms quiet period is the same epoch). Scroll-mask
 * hit tests need TestAppContext and stay with the visual suite. */

#include "Test.h"

using namespace gpui::component;

static Bounds Box(float x, float y, float w, float h) {
    return {x, y, w, h};
}

static bool PointEq(Point a, Point b) {
    return TestNear(a.x, b.x) && TestNear(a.y, b.y);
}

namespace {
struct CarouselSink {
    int seen[8] = {};
    int n = 0;

    static void OnChange(CarouselSink* self, Ctx*, const CarouselEvent* ev) {
        if (self->n < 8) self->seen[self->n++] = ev->index;
    }
};

struct Harness {
    App app;
    Window* win = nullptr;
    Arena* a = nullptr;
    Ctx cx = {};
    Entity<CarouselState> state = {};
    Entity<CarouselSink> sink = {};
    CarouselState* s = nullptr;
    CarouselSink* events = nullptr;

    Harness(int count, bool looping = false, Axis axis = Axis::Horizontal) {
        win = new Window();
        win->app = &app;
        a = ArenaNew();
        cx.app = &app;
        cx.win = win;
        cx.a = a;
        state = CarouselStateNew(&app, count);
        s = state.Get(&app);
        if (looping) s->WithLooping(true);
        if (axis != Axis::Horizontal) s->axis = axis;
        sink = EntityNewState<CarouselSink>(&app);
        SubscribeTo(&app, state, sink, &CarouselSink::OnChange);
        events = sink.Get(&app);
        cx.self = state.id;
    }

    ~Harness() {
        ArenaDelete(a);
        delete win;
        EntityDropAll(&app);
    }
};
} // namespace

static void ConstructorsAndProgrammaticSettersClampWithoutEvents() {
    CarouselState state = CarouselState::New(3)
                              .WithSelectedIndex(99)
                              .WithAxis(Axis::Vertical)
                              .WithLooping(true);
    utassert(state.ItemCount() == 3);
    utassert(state.SelectedIndex() == 2);
    utassert(state.GetAxis() == Axis::Vertical);
    utassert(state.IsLooping());
    utassert(state.HasPrevious());
    utassert(state.HasNext());

    CarouselState empty = CarouselState::New(0);
    utassert(empty.SelectedIndex() == -1);
    utassert(!empty.HasPrevious());
    utassert(!empty.HasNext());
}

static void GeometryProducesAxisSpecificSnapPoints() {
    CarouselState state = CarouselState::New(2).WithAxis(Axis::Horizontal);
    Bounds items[] = {Box(10, 20, 100, 40), Box(110, 20, 100, 40)};
    state.SetGeometry(Box(10, 20, 100, 40), items, 2);
    utassert(PointEq(state.SnapTargetFor(0), {0, 0}));
    utassert(PointEq(state.SnapTargetFor(1), {-100, 0}));

    CarouselState vertical = CarouselState::New(2).WithAxis(Axis::Vertical);
    Bounds vitems[] = {Box(10, 20, 40, 100), Box(10, 120, 40, 100)};
    vertical.SetGeometry(Box(10, 20, 40, 100), vitems, 2);
    utassert(PointEq(vertical.SnapTargetFor(1), {0, -100}));

    CarouselState narrow = CarouselState::New(3);
    Bounds nitems[] = {Box(0, 0, 50, 40), Box(50, 0, 50, 40),
                       Box(100, 0, 50, 40)};
    narrow.SetGeometry(Box(0, 0, 100, 40), nitems, 3);
    utassert(PointEq(narrow.SnapTargetFor(2), {-50, 0}));
}

static void NearestIndexKeepsTheFirstTrailingDuplicateAsCanonical() {
    CarouselState state = CarouselState::New(3);
    Bounds items[] = {Box(0, 0, 50, 40), Box(50, 0, 50, 40),
                      Box(100, 0, 50, 40)};
    state.SetGeometry(Box(0, 0, 100, 40), items, 3);
    utassert(PointEq(state.SnapTargetFor(1), {-50, 0}));
    utassert(PointEq(state.SnapTargetFor(2), {-50, 0}));
    utassert(state.NearestIndex({-50, 0}) == 1);
}

static void GeometryNavigationSkipsDuplicateSnapPoints() {
    Harness h(3);
    Bounds items[] = {Box(0, 0, 50, 40), Box(50, 0, 50, 40),
                      Box(100, 0, 50, 40)};
    h.s->SetGeometry(Box(0, 0, 100, 40), items, 3);
    h.s->SetSelectedIndex(1, &h.cx);
    utassert(!h.s->HasNext());
    utassert(h.s->HasPrevious());
    utassert(h.s->SelectPrevious(&h.cx));
    utassert(h.s->SelectedIndex() == 0);
    utassert(h.s->SelectNext(&h.cx));
    utassert(h.s->SelectedIndex() == 1);
    h.s->SetSelectedIndex(2, &h.cx);
    utassert(!h.s->HasNext());
    utassert(h.s->SelectPrevious(&h.cx));
    utassert(h.s->SelectedIndex() == 0);
}

static void NonLoopingNavigationStopsAtBothBoundaries() {
    Harness h(3);
    utassert(!h.s->SelectPrevious(&h.cx));
    utassert(!h.s->SelectFirst(&h.cx));
    utassert(h.s->SelectLast(&h.cx));
    utassert(!h.s->SelectNext(&h.cx));
    utassert(!h.s->SelectLast(&h.cx));
    utassert(h.events->n == 1 && h.events->seen[0] == 2);
}

static void UserNavigationEmitsOnceAndProgrammaticChangesStaySilent() {
    Harness h(3);
    utassert(h.s->SelectNext(&h.cx));
    utassert(!h.s->SelectIndex(1, &h.cx));
    utassert(!h.s->SelectIndex(99, &h.cx));
    h.s->SetSelectedIndex(2, &h.cx);
    h.s->SetAxis(Axis::Vertical, &h.cx);
    h.s->SetLooping(true, &h.cx);
    utassert(h.events->n == 1 && h.events->seen[0] == 1);
    utassert(h.s->SelectedIndex() == 2);
    utassert(h.s->GetAxis() == Axis::Vertical);
    utassert(h.s->IsLooping());

    Harness empty(0);
    empty.s->SetItemCount(2, &empty.cx);
    empty.s->SetSelectedIndex(99, &empty.cx);
    utassert(empty.s->SelectedIndex() == 1);
}

static void PointerDragLocksToThePrimaryAxis() {
    Harness h(3);
    utassert(h.s->BeginDrag({0, 0}, &h.cx));
    utassert(!h.s->UpdateDrag({1, 1}, &h.cx));
    utassert(h.s->IsInteracting());
    utassert(!h.s->IsPointerDragLocked());
    utassert(!h.s->UpdateDrag({4, 20}, &h.cx));
    utassert(!h.s->IsInteracting());
    utassert(h.s->ShouldSuppressPointerClick());
    utassert(!h.s->FinishDrag(&h.cx));
    utassert(!h.s->ShouldSuppressPointerClick());

    utassert(h.s->BeginDrag({0, 0}, &h.cx));
    utassert(h.s->UpdateDrag({20, 4}, &h.cx));
    utassert(h.s->IsInteracting());
    utassert(h.s->IsPointerDragLocked());
    h.s->FinishDrag(&h.cx);
    utassert(!h.s->IsInteracting());
}

static void FinishingADragKeepsTheOffsetAsTheSnapAnimationOrigin() {
    Harness h(2);
    Bounds items[] = {Box(0, 0, 100, 40), Box(100, 0, 100, 40)};
    h.s->SetGeometry(Box(0, 0, 100, 40), items, 2);
    utassert(h.s->BeginDrag({0, 0}, &h.cx));
    h.s->SetOffset({-60, 0});
    utassert(h.s->FinishDrag(&h.cx));
    utassert(h.s->SelectedIndex() == 1);
    utassert(PointEq(h.s->Offset(), {-60, 0}));
}

static void UserNavigationInvalidatesAnActiveTrackpadGesture() {
    Harness h(3);
    h.s->HandleScrollDelta(Axis::Horizontal, -20, TouchPhase::Started, &h.cx);
    utassert(h.s->IsInteracting());
    utassert(h.s->SelectNext(&h.cx));
    utassert(!h.s->IsInteracting());
    utassert(!h.s->HandleScrollDelta(Axis::Horizontal, -20, TouchPhase::Moved,
                                     &h.cx));
    utassert(!h.s->IsInteracting());
    utassert(h.s->ignoreScrollUntilQuiet);

    CarouselState::OnIgnoredScrollRecovery(h.s, &h.cx, nullptr,
                                           h.s->scrollSettleEpoch);
    utassert(!h.s->ignoreScrollUntilQuiet);
    utassert(!h.s->HandleScrollDelta(Axis::Horizontal, -20, TouchPhase::Moved,
                                     &h.cx));
    utassert(h.s->IsInteracting());
    utassert(!h.s->FinishScroll(true, &h.cx));
    utassert(!h.s->IsInteracting());

    utassert(!h.s->HandleScrollDelta(Axis::Horizontal, -20, TouchPhase::Started,
                                     &h.cx));
    utassert(h.s->IsInteracting());
    utassert(!h.s->FinishScroll(true, &h.cx));
    utassert(!h.s->IsInteracting());
    utassert(h.events->n == 1 && h.events->seen[0] == 1);
}

static void MovedOnlyTrackpadGestureSettlesAfterTheQuietPeriod() {
    Harness h(2);
    Bounds items[] = {Box(0, 0, 100, 40), Box(100, 0, 100, 40)};
    h.s->SetGeometry(Box(0, 0, 100, 40), items, 2);
    h.s->HandleScrollDelta(Axis::Horizontal, -60, TouchPhase::Moved, &h.cx);
    h.s->SetOffset({-60, 0});
    utassert(h.s->IsInteracting());
    CarouselState::OnScrollSettle(h.s, &h.cx, nullptr, h.s->scrollSettleEpoch);
    utassert(h.s->SelectedIndex() == 1);
    utassert(!h.s->IsInteracting());
    utassert(h.events->n == 1 && h.events->seen[0] == 1);
}

static void InvalidBoundaryNavigationStillCancelsTheActiveGesture() {
    Harness h(2);
    h.s->HandleScrollDelta(Axis::Horizontal, 20, TouchPhase::Started, &h.cx);
    utassert(h.s->IsInteracting());
    utassert(!h.s->SelectPrevious(&h.cx));
    utassert(!h.s->IsInteracting());
    utassert(!h.s->HandleScrollDelta(Axis::Horizontal, 20, TouchPhase::Moved,
                                     &h.cx));
    utassert(!h.s->FinishScroll(false, &h.cx));

    utassert(h.s->SelectLast(&h.cx));
    h.s->HandleScrollDelta(Axis::Horizontal, -20, TouchPhase::Started, &h.cx);
    utassert(h.s->IsInteracting());
    utassert(!h.s->SelectNext(&h.cx));
    utassert(!h.s->IsInteracting());
    utassert(!h.s->FinishScroll(false, &h.cx));
}

static void LoopingBoundariesEmitOneEventAndAdvanceMotionRevision() {
    Harness h(2, true);
    utassert(h.s->SelectPrevious(&h.cx));
    utassert(h.s->SelectedIndex() == 1);
    utassert(h.s->MotionRevision() == 1);
    utassert(h.s->SelectNext(&h.cx));
    utassert(h.s->SelectedIndex() == 0);
    utassert(h.s->MotionRevision() == 2);
    utassert(h.events->n == 2 && h.events->seen[0] == 1 &&
             h.events->seen[1] == 0);
}

static void InstallLoopRunway(CarouselState* s, Bounds viewport, Bounds a,
                              Bounds b, Bounds shiftedA, Bounds shiftedB) {
    Bounds first[] = {a, b};
    s->SetGeometry(viewport, first, 2);
    Bounds second[] = {shiftedA, shiftedB};
    s->SetGeometry(viewport, second, 2);
}

static void LoopingUsesAdjacentCycleTargetsAndRebasesWithoutAnExtraEvent() {
    Harness h(2, true);
    Bounds viewport = Box(0, 0, 100, 40);
    InstallLoopRunway(h.s, viewport, Box(0, 0, 100, 40), Box(100, 0, 100, 40),
                      Box(200, 0, 100, 40), Box(300, 0, 100, 40));
    utassertnear(h.s->LoopRunway(), 200.f);
    utassert(PointEq(h.s->Offset(), {-200, 0}));

    utassert(h.s->SelectPrevious(&h.cx));
    utassert(h.s->SelectedIndex() == 1);
    Point previousTarget = h.s->MotionTargetFor(1);
    utassert(PointEq(previousTarget, {-100, 0}));
    utassertnear(previousTarget.x - h.s->Offset().x, 100.f);
    Point settled = {};
    utassert(h.s->SettleLoopMotion(previousTarget, &settled, &h.cx));
    utassert(PointEq(settled, {-300, 0}));

    utassert(h.s->SelectNext(&h.cx));
    utassert(h.s->SelectedIndex() == 0);
    Point nextTarget = h.s->MotionTargetFor(0);
    utassert(PointEq(nextTarget, {-400, 0}));
    utassertnear(nextTarget.x - h.s->Offset().x, -100.f);
    utassert(h.s->SettleLoopMotion(nextTarget, &settled, &h.cx));
    utassert(PointEq(settled, {-200, 0}));
    utassert(h.events->n == 2 && h.events->seen[0] == 1 &&
             h.events->seen[1] == 0);
}

static void ProgrammaticLoopWrapUsesTheAdjacentCycleWithoutEmitting() {
    Harness h(2, true);
    Bounds viewport = Box(0, 0, 100, 40);
    InstallLoopRunway(h.s, viewport, Box(0, 0, 100, 40), Box(100, 0, 100, 40),
                      Box(200, 0, 100, 40), Box(300, 0, 100, 40));
    h.s->SetSelectedIndex(1, &h.cx);
    h.s->SetOffset(h.s->SnapTargetFor(1));
    h.s->SetSelectedIndex(0, &h.cx);
    utassert(h.s->SelectedIndex() == 0);
    utassert(PointEq(h.s->MotionTargetFor(0), {-400, 0}));
    Point settled = {};
    utassert(h.s->SettleLoopMotion({-400, 0}, &settled, &h.cx));
    utassert(PointEq(settled, {-200, 0}));
    utassert(h.events->n == 0);
}

static void VerticalLoopWrapUsesTheAdjacentCycle() {
    Harness h(2, true, Axis::Vertical);
    Bounds viewport = Box(0, 0, 40, 100);
    InstallLoopRunway(h.s, viewport, Box(0, 0, 40, 100), Box(0, 100, 40, 100),
                      Box(0, 200, 40, 100), Box(0, 300, 40, 100));
    utassert(h.s->SelectPrevious(&h.cx));
    utassert(PointEq(h.s->MotionTargetFor(1), {0, -100}));
    Point settled = {};
    utassert(h.s->SettleLoopMotion({0, -100}, &settled, &h.cx));
    utassert(PointEq(settled, {0, -300}));
}

static void UnequalItemsKeepTheRequestedDirectionAcrossALoopBoundary() {
    Harness h(2, true);
    Bounds viewport = Box(0, 0, 100, 40);
    InstallLoopRunway(h.s, viewport, Box(0, 0, 80, 40), Box(96, 0, 200, 40),
                      Box(328, 0, 80, 40), Box(424, 0, 200, 40));
    h.s->SetSelectedIndex(1, &h.cx);
    h.s->SetOffset({-424, 0});
    utassert(h.s->SelectNext(&h.cx));
    utassert(PointEq(h.s->MotionTargetFor(0), {-640, 0}));
    Point settled = {};
    utassert(h.s->SettleLoopMotion({-640, 0}, &settled, &h.cx));
    utassert(PointEq(settled, {-328, 0}));
    utassert(h.s->SelectPrevious(&h.cx));
    utassert(PointEq(h.s->MotionTargetFor(1), {-112, 0}));
}

static void ActiveLoopGestureRebasesBeforeReachingARunwayEdge() {
    Harness h(2, true);
    Bounds viewport = Box(0, 0, 100, 40);
    InstallLoopRunway(h.s, viewport, Box(0, 0, 100, 40), Box(100, 0, 100, 40),
                      Box(200, 0, 100, 40), Box(300, 0, 100, 40));
    utassert(h.s->BeginDrag({0, 0}, &h.cx));
    h.s->SetOffset({-420, 0});
    utassert(h.s->NormalizeLoopCoordinate());
    utassert(PointEq(h.s->Offset(), {-220, 0}));
    utassert(PointEq(h.s->pointerGesture.startOffset, {0, 0}));
    utassert(h.s->NearestIndex({-110, 0}) == 1);
    utassert(h.s->NearestIndex({-390, 0}) == 0);
}

static void LoopingFallsBackWhenOneCycleCannotCoverTheViewport() {
    CarouselState state = CarouselState::New(2).WithLooping(true);
    Bounds items[] = {Box(0, 0, 40, 40), Box(40, 0, 40, 40)};
    state.SetGeometry(Box(0, 0, 100, 40), items, 2);
    utassert(state.LoopRunway() < 0);
    utassert(PointEq(state.LoopItemOffset(0), {0, 0}));
    utassert(!state.HasPrevious());
    utassert(!state.HasNext());
    state.selectedIndex = 1;
    utassert(!state.HasPrevious());
    utassert(!state.HasNext());
}

static void LoopRunwayAccountsForTheTrackGapAndContentInset() {
    CarouselState state = CarouselState::New(2).WithLooping(true);
    Bounds viewport = Box(0, 0, 100, 40);
    Bounds first[] = {Box(16, 0, 100, 40), Box(132, 0, 100, 40)};
    state.SetGeometry(viewport, first, 2);
    utassertnear(state.LoopRunway(), 232.f);
    Bounds second[] = {Box(264, 0, 100, 40), Box(380, 0, 100, 40)};
    state.SetGeometry(viewport, second, 2);
    utassert(PointEq(state.Offset(), {-248, 0}));
    utassert(PointEq(state.SnapTargetFor(0), {-248, 0}));
    utassert(PointEq(state.SnapTargetFor(1), {-364, 0}));
}

static void LoopRunwayResizeAndRemovalPreserveTheVisibleCoordinate() {
    Harness h(2, true);
    Bounds viewport = Box(0, 0, 100, 40);
    InstallLoopRunway(h.s, viewport, Box(0, 0, 100, 40), Box(100, 0, 100, 40),
                      Box(200, 0, 100, 40), Box(300, 0, 100, 40));
    Bounds resized[] = {Box(200, 0, 150, 40), Box(350, 0, 150, 40)};
    h.s->SetGeometry(viewport, resized, 2);
    utassertnear(h.s->LoopRunway(), 300.f);
    utassert(h.s->IsLoopLayoutTransitioning());
    utassert(PointEq(h.s->Offset(), {0, 0}));
    Bounds ready[] = {Box(300, 0, 150, 40), Box(450, 0, 150, 40)};
    h.s->SetGeometry(viewport, ready, 2);
    utassert(!h.s->IsLoopLayoutTransitioning());
    utassert(PointEq(h.s->Offset(), {-300, 0}));

    h.s->SetLooping(false, &h.cx);
    utassert(h.s->LoopRunway() < 0);
    utassert(h.s->IsLoopLayoutTransitioning());
    utassert(PointEq(h.s->Offset(), {0, 0}));
    Bounds plain[] = {Box(0, 0, 150, 40), Box(150, 0, 150, 40)};
    h.s->SetGeometry(viewport, plain, 2);
    utassert(!h.s->IsLoopLayoutTransitioning());
}

static void LayingOutTheRunwayNormalizesAnActiveDragSnapshot() {
    Harness h(2, true);
    Bounds viewport = Box(0, 0, 100, 40);
    Bounds first[] = {Box(0, 0, 100, 40), Box(100, 0, 100, 40)};
    h.s->SetGeometry(viewport, first, 2);
    utassert(h.s->BeginDrag({0, 0}, &h.cx));
    Bounds second[] = {Box(200, 0, 100, 40), Box(300, 0, 100, 40)};
    h.s->SetGeometry(viewport, second, 2);
    utassert(PointEq(h.s->Offset(), {-200, 0}));
    utassert(PointEq(h.s->pointerGesture.startOffset, {-200, 0}));
}

static void CarouselControlsAcceptSemanticSizes() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    Entity<CarouselState> state = CarouselStateNew(&app, 2);
    utassert(CarouselPrevious::New(&cx, state)->size == UiSize::Medium);
    utassert(CarouselNext::New(&cx, state)->size == UiSize::Medium);
    utassert(CarouselPrevious::New(&cx, state)->WithSize(UiSize::Large)->size ==
             UiSize::Large);
    utassert(CarouselNext::New(&cx, state)->WithSize(UiSize::XSmall)->size ==
             UiSize::XSmall);
    utassert(CarouselPaginationItem::New(&cx, StrL("pagination"), 0, state)
                 ->WithSize(UiSize::Small)
                 ->size == UiSize::Small);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

static void CarouselFocusRingIsConfigurable() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    Entity<CarouselState> state = CarouselStateNew(&app, 2);
    utassert(Carousel::New(&cx, StrL("carousel"), state)->focusRingEnabled);
    utassert(!Carousel::New(&cx, StrL("carousel"), state)
                  ->FocusRing(false)
                  ->focusRingEnabled);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

void TestCarousel() {
    TestSuite("carousel");
    ConstructorsAndProgrammaticSettersClampWithoutEvents();
    GeometryProducesAxisSpecificSnapPoints();
    NearestIndexKeepsTheFirstTrailingDuplicateAsCanonical();
    GeometryNavigationSkipsDuplicateSnapPoints();
    NonLoopingNavigationStopsAtBothBoundaries();
    UserNavigationEmitsOnceAndProgrammaticChangesStaySilent();
    PointerDragLocksToThePrimaryAxis();
    FinishingADragKeepsTheOffsetAsTheSnapAnimationOrigin();
    UserNavigationInvalidatesAnActiveTrackpadGesture();
    MovedOnlyTrackpadGestureSettlesAfterTheQuietPeriod();
    InvalidBoundaryNavigationStillCancelsTheActiveGesture();
    LoopingBoundariesEmitOneEventAndAdvanceMotionRevision();
    LoopingUsesAdjacentCycleTargetsAndRebasesWithoutAnExtraEvent();
    ProgrammaticLoopWrapUsesTheAdjacentCycleWithoutEmitting();
    VerticalLoopWrapUsesTheAdjacentCycle();
    UnequalItemsKeepTheRequestedDirectionAcrossALoopBoundary();
    ActiveLoopGestureRebasesBeforeReachingARunwayEdge();
    LoopingFallsBackWhenOneCycleCannotCoverTheViewport();
    LoopRunwayAccountsForTheTrackGapAndContentInset();
    LoopRunwayResizeAndRemovalPreserveTheVisibleCoordinate();
    LayingOutTheRunwayNormalizesAnActiveDragSnapshot();
    CarouselControlsAcceptSemanticSizes();
    CarouselFocusRingIsConfigurable();
}
