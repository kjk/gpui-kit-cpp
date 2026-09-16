/* Ported from crates/base/src/scrollbar.rs.
 *
 * Rust's own cases there drive a window and a drag; the arithmetic underneath
 * is the thumb's length and the two ways a pointer sets the offset — a track
 * press, which centres the thumb where it landed, and a drag, which keeps the
 * grab point. Offsets here run positive-down, the way El::ScrollY takes them;
 * Rust's run negative because it offsets the content rather than the view.
 *
 * The second half is the visibility animation that arrived with gpui-kit
 * 2a1335d5: the animation lives beside the element tree here rather than in a
 * keyed state, so a test drives it by scroll id and steps the clock itself. */

#include "Test.h"

static void TheThumbShrinksWithWhatIsVisible() {
    // Half the content visible: half the track.
    utassertnear(ScrollbarThumbSize(400, 200, 400), 200.f);
    utassertnear(ScrollbarThumbSize(400, 100, 400), 100.f);
    // A very long document stops at the floor rather than vanishing.
    utassertnear(ScrollbarThumbSize(400, 100, 100000), 48.f);
    // And never exceeds the track it runs in.
    utassertnear(ScrollbarThumbSize(30, 100, 200), 30.f);
    utassertnear(ScrollbarThumbSize(400, 100, 100000, 72), 72.f);
}

static void TheThumbSitsWhereTheOffsetSaysAndStopsAtTheEnds() {
    // Track 400, thumb 100, so 300 of travel over 600 of scroll.
    utassertnear(ScrollbarThumbPos(400, 100, 0, 200, 800), 0.f);
    utassertnear(ScrollbarThumbPos(400, 100, 600, 200, 800), 300.f);
    utassertnear(ScrollbarThumbPos(400, 100, 300, 200, 800), 150.f);
    // Past the end it stays put rather than running off the track.
    utassertnear(ScrollbarThumbPos(400, 100, 5000, 200, 800), 300.f);
    // Nothing to scroll, nothing to move.
    utassertnear(ScrollbarThumbPos(400, 400, 0, 800, 800), 0.f);
    // A horizontal bar leaves the vertical track clear at its far end.
    utassertnear(ScrollbarThumbPos(400, 100, 600, 200, 800, 16), 284.f);
}

static void ATrackPressCentresTheThumbOnIt() {
    // Press halfway down a 400 track whose origin is 0: the thumb's centre
    // goes there, so its top lands at 150 of the 300 available -> half.
    utassertnear(ScrollbarOffsetForTrackPress(200, 0, 400, 100, 200, 800),
                 300.f);
    // At the very top the clamp holds it at zero.
    utassertnear(ScrollbarOffsetForTrackPress(0, 0, 400, 100, 200, 800), 0.f);
    // At the bottom, the full distance.
    utassertnear(ScrollbarOffsetForTrackPress(400, 0, 400, 100, 200, 800),
                 600.f);
    // The track's origin is subtracted, so an inset bar answers the same.
    utassertnear(ScrollbarOffsetForTrackPress(250, 50, 400, 100, 200, 800),
                 300.f);
}

static void ADragKeepsTheGrabPoint() {
    // Grabbed 100 into a 100-tall thumb... which is its bottom edge: dragging
    // to 250 puts the thumb's top at 150, half of the travel.
    utassertnear(ScrollbarOffsetForDrag(250, 100, 0, 400, 100, 200, 800),
                 300.f);
    // Grabbed at the top edge instead, the same thumb position needs the
    // pointer 100 higher.
    utassertnear(ScrollbarOffsetForDrag(150, 0, 0, 400, 100, 200, 800), 300.f);
    // Dragging past either end clamps rather than overshooting.
    utassertnear(ScrollbarOffsetForDrag(-500, 0, 0, 400, 100, 200, 800), 0.f);
    utassertnear(ScrollbarOffsetForDrag(5000, 0, 0, 400, 100, 200, 800), 600.f);
}

static void NothingToScrollMeansNoOffset() {
    utassertnear(ScrollbarOffsetForDrag(200, 0, 0, 400, 400, 800, 800), 0.f);
    utassertnear(ScrollbarOffsetForTrackPress(200, 0, 400, 400, 800, 800), 0.f);
}

static void SharedGeometryKeepsThePaintedGrabPoint() {
    ScrollbarThumbGeometry normal = ScrollbarGeometry(30, 240, 500, 16, 6, 64);
    ScrollbarThumbGeometry active = ScrollbarGeometry(30, 240, 500, 16, 3, 72);
    float offset = 100;
    float pointer = normal.Start(offset) + normal.length * .5f;
    float grab = pointer - normal.Start(offset);
    offset = active.DragOffset(pointer, grab, offset);
    utassertnear(active.Start(offset) + grab, pointer);

    float deltas[] = {24.f, -8.f, 32.f, -10.f, 8.f, 0.f};
    for (float delta : deltas) {
        float moved = pointer + delta;
        offset = active.DragOffset(moved, grab, offset);
        utassertnear(active.Start(offset) + grab, moved);
    }

    // A minimum thumb which fills the track has nowhere to travel and must
    // leave the existing offset alone.
    ScrollbarThumbGeometry full = ScrollbarGeometry(20, 40, 500, 0, 4, 64);
    utassertnear(full.DragOffset(50, 8, 100), 100.f);
}

static void PreciseGesturesKeepTheirAxisUntilAStrongTurn() {
    OngoingScroll scroll;
    Point first = {-40, -10};
    scroll.Filter(&first, TouchPhase::Started);
    utassertnear(first.x, -40);
    utassertnear(first.y, 0);

    // A little mid-gesture wobble remains horizontal.
    Point wobble = {-10, -15};
    scroll.Filter(&wobble, TouchPhase::Moved);
    utassertnear(wobble.x, -10);
    utassertnear(wobble.y, 0);

    // Twice as much motion on the other axis releases the lock.
    Point turn = {-10, -25};
    scroll.Filter(&turn, TouchPhase::Moved);
    utassertnear(turn.x, 0);
    utassertnear(turn.y, -25);
    scroll.Filter(&turn, TouchPhase::Ended);
    utassert(!scroll.active);
}

static void ScrollableElementPreservesTheSourceElementAndMask() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    El* source = Div(a)->W(123)->Child(Div(a));
    El* result = component::ScrollableElement::OverflowYScrollbar(&cx, source)
                     ->Id(StrL("source-scroll"))
                     ->ScrollY(17)
                     ->OnScroll({})
                     ->IntoEl();
    utassert(result == source);
    utassert(result->style.overflowY == Overflow::Scroll);
    utassert(result->style.overflowX == Overflow::Hidden);
    utassertnear(result->scrollY, 17);
    utassertnear(result->style.width, 123);
    utassert(result->first != nullptr);

    El* direct = component::ScrollableElement::HorizontalScrollbar(
        &cx, Div(a), StrL("direct-horizontal"), 9, {});
    utassert(direct->style.overflowX == Overflow::Scroll);
    utassert(direct->style.overflowY == Overflow::Hidden);
    utassertnear(direct->scrollX, 9);

    El* masked = component::ScrollableMask::New(&cx, Axis::Horizontal, result)
                     ->Id(StrL("horizontal-mask"))
                     ->IntoEl();
    utassert(masked == result);
    utassert((masked->scrollMaskAxes & 1) != 0);
    utassert(masked->scrollFromPath);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

namespace {
struct ScrollRecorder {
    float innerX = 0;
    float innerY = 0;
    float outerY = 0;
    int innerCalls = 0;
    int outerCalls = 0;

    static void Inner(ScrollRecorder* self, Ctx*, const ScrollEvent* ev) {
        self->innerX = ev->offsetX;
        self->innerY = ev->offsetY;
        self->innerCalls++;
    }
    static void Outer(ScrollRecorder* self, Ctx*, const ScrollEvent* ev) {
        self->outerY = ev->offsetY;
        self->outerCalls++;
    }
};
} // namespace

static ScrollRect TestScrollRect(int id, Bounds bounds, float contentW,
                                 float contentH, uint8_t masks, int maskHit,
                                 Listener listener) {
    ScrollRect s = {};
    s.id = id;
    s.bounds = bounds;
    s.contentW = contentW;
    s.contentH = contentH;
    s.maskAxes = masks;
    s.maskHit = maskHit;
    s.onScroll = listener;
    return s;
}

static void DispatchWheel(Window* win, float dx, float dy, bool precise = false,
                          TouchPhase phase = TouchPhase::Moved) {
    PlatformInput input = InputScrollWheel(10, 10, dx, dy, precise, {}, phase);
    WindowDispatchInput(win, &input);
}

static void ScrollableMasksChainAndTrapLikeTheSource() {
    App app = {};
    Window* win = new Window();
    win->app = &app;
    Entity<ScrollRecorder> entity = EntityNewState<ScrollRecorder>(&app);
    ScrollRecorder* state = entity.Get(&app);

    // The inner hit is below the outer hit in the same chain, just as nested
    // transparent ScrollableMask elements are in GPUI.
    HitRect outerHit = {};
    outerHit.bounds = {0, 0, 100, 100};
    VecAppend(win->paint.hits, outerHit);
    HitRect innerHit = {};
    innerHit.bounds = {0, 0, 100, 60};
    innerHit.parent = 0;
    VecAppend(win->paint.hits, innerHit);

    ScrollRect outer = TestScrollRect(1, {0, 0, 100, 100}, 100, 500, 2, 0,
                                      ListenTo(entity, &ScrollRecorder::Outer));
    ScrollRect inner = TestScrollRect(2, {0, 0, 100, 60}, 100, 300, 2, 1,
                                      ListenTo(entity, &ScrollRecorder::Inner));
    VecAppend(win->paint.scrolls, outer);
    VecAppend(win->paint.scrolls, inner);

    DispatchWheel(win, 0, -40);
    utassertnear(state->innerY, 40);
    utassertnear(state->outerY, 0);

    // At the inner bottom the vertical mask hands off. Updating the retained
    // ScrollRect on emit makes two events accumulate even before a repaint.
    win->paint.scrolls[0].scrollY = 0;
    win->paint.scrolls[1].scrollY = 240;
    state->outerY = 0;
    state->outerCalls = 0;
    DispatchWheel(win, 0, -40);
    DispatchWheel(win, 0, -40);
    utassertnear(state->outerY, 80);
    utassert(state->outerCalls == 2);

    // A horizontal-dominant gesture is trapped by the horizontal mask,
    // including at its edge; vertical-dominant input reaches the parent.
    win->paint.scrolls[0].scrollY = 0;
    win->paint
        .scrolls[1] = TestScrollRect(3, {0, 0, 100, 60}, 300, 60, 1, 1,
                                     ListenTo(entity, &ScrollRecorder::Inner));
    state->innerX = 0;
    state->outerY = 0;
    state->outerCalls = 0;
    DispatchWheel(win, -40, -10);
    utassertnear(state->innerX, 40);
    utassertnear(state->outerY, 0);
    win->paint.scrolls[1].scrollX = 200;
    DispatchWheel(win, -40, -10);
    utassertnear(state->outerY, 0);
    DispatchWheel(win, -10, -40);
    utassertnear(state->outerY, 40);

    // Precise deltas retain the horizontal axis across a wobble.
    win->paint.scrolls[0].scrollY = 0;
    win->paint.scrolls[1].id = 4;
    win->paint.scrolls[1].scrollX = 0;
    state->innerX = 0;
    DispatchWheel(win, -40, -10, true, TouchPhase::Started);
    DispatchWheel(win, -10, -15, true, TouchPhase::Moved);
#if GPUI_OS_WASM
    // gpui-base's OngoingScrollExt deliberately does not lock on wasm: GPUI's
    // clock-backed filter is unimplemented there. This event is vertical-
    // dominant on its own, so the horizontal viewport remains at 40.
    utassertnear(state->innerX, 40);
#else
    utassertnear(state->innerX, 50);
#endif
    DispatchWheel(win, -10, -25, true, TouchPhase::Moved);
#if GPUI_OS_WASM
    utassertnear(state->innerX, 40);
#else
    utassertnear(state->innerX, 50);
#endif

    // A later sibling hit is an overlay, so neither masked viewport under it
    // receives the wheel.
    HitRect overlay = {};
    overlay.bounds = {0, 0, 100, 100};
    VecAppend(win->paint.hits, overlay);
    int calls = state->innerCalls + state->outerCalls;
    DispatchWheel(win, -40, 0);
    utassert(state->innerCalls + state->outerCalls == calls);

    VecReset(win->paint.hits);
    VecReset(win->paint.scrolls);
    delete win;
    EntityDropAll(&app);
}

static void ATrackPressMovesOnceAndOnlyAThumbPressDrags() {
    App app = {};
    Window* win = new Window();
    win->app = &app;
    Entity<ScrollRecorder> entity = EntityNewState<ScrollRecorder>(&app);
    ScrollRecorder* state = entity.Get(&app);
    ScrollRect scroll =
        TestScrollRect(51, {0, 0, 100, 100}, 100, 400, 0, -1,
                       ListenTo(entity, &ScrollRecorder::Inner));
    scroll.trackWidth = 20;
    VecAppend(win->paint.scrolls, scroll);

    PlatformInput track =
        InputMouseDown(MouseButton::Left, 85, 90, {}, 1, false);
    WindowDispatchInput(win, &track);
    utassert(state->innerCalls == 1 && state->innerY > 0);
    utassert(win->scrollDragId == 0);

    win->paint.scrolls[0].scrollY = 0;
    PlatformInput thumb =
        InputMouseDown(MouseButton::Left, 85, 10, {}, 1, false);
    WindowDispatchInput(win, &thumb);
    utassert(win->scrollDragId == 51);
    utassert(!win->scrollDragHorizontal);

    VecReset(win->paint.scrolls);
    delete win;
    EntityDropAll(&app);
}

static void AThumbPressOnAnInputScrollerDragsEvenWithoutAScrollId() {
    // The code editor's highlighter box binds the InputState. Until it also
    // named a scrollId, a thumb grab stored 0 and the move handler skipped
    // the drag. The field is enough to find the box next move.
    App app = {};
    Window* win = new Window();
    win->app = &app;
    InputState field;
    ScrollRect scroll =
        TestScrollRect(0, {0, 0, 100, 100}, 100, 400, 0, -1, Listener{});
    scroll.trackWidth = 20;
    scroll.input = &field;
    VecAppend(win->paint.scrolls, scroll);

    PlatformInput thumb =
        InputMouseDown(MouseButton::Left, 85, 10, {}, 1, false);
    WindowDispatchInput(win, &thumb);
    utassert(win->scrollDragInput == &field);
    utassert(win->scrollDragId == 0);

    PlatformInput move = InputMouseMove(85, 50, true, MouseButton::Left, {});
    WindowDispatchInput(win, &move);
    utassert(field.scrollY > 0);

    VecReset(win->paint.scrolls);
    delete win;
}

static void ATouchThumbDragMovesDownAndCancelReleasesIt() {
    App app = {};
    Window* win = new Window();
    win->app = &app;
    Entity<ScrollRecorder> entity = EntityNewState<ScrollRecorder>(&app);
    ScrollRecorder* state = entity.Get(&app);
    ScrollRect scroll =
        TestScrollRect(52, {0, 0, 100, 100}, 100, 500, 0, -1,
                       ListenTo(entity, &ScrollRecorder::Inner));
    scroll.trackWidth = 20;
    VecAppend(win->paint.scrolls, scroll);

    Point start = {95, 20};
    PlatformInput began = InputTouchDrag(TouchPhase::Started, start, start);
    WindowDispatchInput(win, &began);
    utassert(win->touchScrollbarDrag && win->scrollDragId == 52);
    PlatformInput moved =
        InputTouchDrag(TouchPhase::Moved, start, Point{95, 55});
    WindowDispatchInput(win, &moved);
    utassert(state->innerCalls == 1 && win->paint.scrolls[0].scrollY > 0);
    PlatformInput cancelled =
        InputTouchDrag(TouchPhase::Cancelled, start, Point{95, 55});
    WindowDispatchInput(win, &cancelled);
    utassert(!win->touchScrollbarDrag && win->scrollDragId == 0);
    utassert(state->innerCalls == 2 && state->innerY > 0);

    VecReset(win->paint.scrolls);
    delete win;
    EntityDropAll(&app);
}

static void ATouchReleaseKeepsItsFinalDisplacement() {
    App app = {};
    Window* win = new Window();
    win->app = &app;
    InputState field;
    ScrollRect scroll =
        TestScrollRect(0, {0, 0, 100, 100}, 100, 500, 0, -1, Listener{});
    scroll.trackWidth = 20;
    scroll.input = &field;
    VecAppend(win->paint.scrolls, scroll);

    Point start = {95, 20};
    PlatformInput began = InputTouchDrag(TouchPhase::Started, start, start);
    WindowDispatchInput(win, &began);
    PlatformInput ended =
        InputTouchDrag(TouchPhase::Ended, start, Point{95, 55});
    WindowDispatchInput(win, &ended);
    utassert(field.scrollY > 0);
    utassert(!win->touchScrollbarDrag);

    VecReset(win->paint.scrolls);
    delete win;
}

static void TheHighlighterScrollerHasAStableScrollId() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;
    InputState state;
    state.kind = InputKind::Editor;
    state.softWrap = false;
    El* el = component::Highlighter::New(&cx, StrL("editor"), &state)
                 ->Searchable(false)
                 ->H(400)
                 ->IntoEl();
    utassert(el->style.overflowY == Overflow::Scroll);
    utassert(el->scrollFromPath);
    utassert(el->style.overflowX == Overflow::Scroll);

    AppGlobalClear(&app);
    ArenaDelete(a);
}

// scrollable_flex_item_shrinks_below_its_content, and its horizontal twin.
//
// A flex item only drops its content-based automatic minimum size when its
// own overflow is not Visible; otherwise the item refuses to shrink around
// its content. Rust's Scrollable wraps the caller's element, so the wrapper
// had to declare the scrolled axis clipped itself; here the caller's element
// *is* the scroll container — Scrollbar::Apply puts the overflow on it — so
// the axis is already non-visible and CSS puts its automatic minimum at zero.
// Without that a scroll region used as a flex item pushes its siblings out of
// the container instead of scrolling, and every call site has to remember a
// zero minimum.
static void AScrollableFlexItemShrinksBelowItsContent() {
    App app = {};
    component::Init(&app);
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = a;

    // A fixed-height column of header, flexible scroll area, footer. The
    // content is far taller than the room left for the area, so the area must
    // shrink into the remaining 60px and scroll.
    El* header = Div(a)->W(kFill)->H(20)->Shrink0();
    El* footer = Div(a)->W(kFill)->H(20)->Shrink0();
    El* area = Div(a)->FlexCol()->Flex1()->W(kFill);
    for (int i = 0; i < 6; i++) {
        area->Child(Div(a)->W(kFill)->H(50)->Shrink0());
    }
    El* scroller = component::ScrollableElement::OverflowYScrollbar(&cx, area)
                       ->Id(StrL("flex-item"))
                       ->IntoEl();
    El* column =
        VFlex(a)->W(100)->H(100)->Child(header)->Child(scroller)->Child(footer);
    LayoutEl(nullptr, column, 0, 0, 100, 100, 14, Rgba{});

    // Header and footer stay inside the 100px column, so the area took the
    // 60px left over instead of its content height.
    utassertnear(header->y, 0.f);
    utassertnear(footer->y, 80.f);
    utassertnear(scroller->h, 60.f);

    // The same on the other axis.
    El* leading = Div(a)->W(20)->H(kFill)->Shrink0();
    El* trailing = Div(a)->W(20)->H(kFill)->Shrink0();
    El* strip = Div(a)->FlexRow()->Flex1()->H(kFill);
    for (int i = 0; i < 6; i++) {
        strip->Child(Div(a)->W(50)->H(20)->Shrink0());
    }
    El* across = component::ScrollableElement::OverflowXScrollbar(&cx, strip)
                     ->Id(StrL("flex-item-across"))
                     ->IntoEl();
    El* rowBox =
        HFlex(a)->W(100)->H(40)->Child(leading)->Child(across)->Child(trailing);
    LayoutEl(nullptr, rowBox, 0, 0, 100, 40, 14, Rgba{});
    utassertnear(leading->x, 0.f);
    utassertnear(trailing->x, 80.f);

    // An explicit minimum is the caller's decision and must survive: the area
    // cannot shrink past the requested 70px, so the footer is pushed to
    // 20 + 70 instead of being clamped into the column.
    El* minHeader = Div(a)->W(kFill)->H(20)->Shrink0();
    El* minFooter = Div(a)->W(kFill)->H(20)->Shrink0();
    El* minArea = Div(a)->FlexCol()->Flex1()->W(kFill)->MinH(70);
    for (int i = 0; i < 6; i++) {
        minArea->Child(Div(a)->W(kFill)->H(50)->Shrink0());
    }
    El* minScroller =
        component::ScrollableElement::OverflowYScrollbar(&cx, minArea)
            ->Id(StrL("explicit-min"))
            ->IntoEl();
    El* minColumn = VFlex(a)
                        ->W(100)
                        ->H(100)
                        ->Child(minHeader)
                        ->Child(minScroller)
                        ->Child(minFooter);
    LayoutEl(nullptr, minColumn, 0, 0, 100, 100, 14, Rgba{});
    utassertnear(minHeader->y, 0.f);
    utassertnear(minFooter->y, 90.f);

    AppGlobalClear(&app);
    ArenaDelete(a);
    EntityDropAll(&app);
}

void TestScrollbar() {
    TestSuite("scrollbar");
    AScrollableFlexItemShrinksBelowItsContent();
    TheThumbShrinksWithWhatIsVisible();
    TheThumbSitsWhereTheOffsetSaysAndStopsAtTheEnds();
    ATrackPressCentresTheThumbOnIt();
    ADragKeepsTheGrabPoint();
    NothingToScrollMeansNoOffset();
    SharedGeometryKeepsThePaintedGrabPoint();
    PreciseGesturesKeepTheirAxisUntilAStrongTurn();
    ScrollableElementPreservesTheSourceElementAndMask();
    ScrollableMasksChainAndTrapLikeTheSource();
    ATrackPressMovesOnceAndOnlyAThumbPressDrags();
    AThumbPressOnAnInputScrollerDragsEvenWithoutAScrollId();
    ATouchThumbDragMovesDownAndCancelReleasesIt();
    ATouchReleaseKeepsItsFinalDisplacement();
    TheHighlighterScrollerHasAStableScrollId();
}

// The timing a styled layer projects, which is what these assert against.
static const float kEnter = 0.3f;
static const float kExit = 0.5f;

static void VisibilityUsesDirectionSpecificCurvesAndDurations() {
    const int id = 9001;
    double start = 1000.0;
    ScrollbarVisibilitySet(id, false, ScrollbarEntrance::Fade, 0, 0, start);

    ScrollbarVisibilitySet(id, true, ScrollbarEntrance::SlideAndFade, kEnter,
                           kExit, start);
    utassert(ScrollbarVisibilityAt(id, start).opacity == 0.f);
    // ease-out must advance quickly
    utassert(ScrollbarVisibilityAt(id, start + kEnter / 2).position > 0.5f);

    ScrollbarVisibility entered = ScrollbarVisibilityAt(id, start + kEnter);
    utassert(entered.opacity == 1.f);
    utassert(entered.position == 1.f);

    ScrollbarVisibilitySet(id, false, ScrollbarEntrance::SlideAndFade, kEnter,
                           kExit, start + kEnter);
    // ease-in must remain visible early in the exit
    float exiting = ScrollbarVisibilityAt(id, start + kEnter + kExit / 2)
                        .opacity;
    utassert(exiting > 0.5f);
    utassert(ScrollbarVisibilityAt(id, start + kEnter + kExit).opacity == 0.f);
}

static void EntranceFadesLinearlyWhilePositionEasesOut() {
    const int id = 9002;
    double start = 2000.0;
    ScrollbarVisibilitySet(id, false, ScrollbarEntrance::Fade, 0, 0, start);
    ScrollbarVisibilitySet(id, true, ScrollbarEntrance::SlideAndFade, kEnter,
                           kExit, start);

    ScrollbarVisibility halfway = ScrollbarVisibilityAt(id, start + kEnter / 2);
    utassert(TestNear(halfway.opacity, 0.5f));
    utassert(halfway.position > halfway.opacity);
}

// A fade entrance does not move: the position is already home, so only the
// opacity travels.
static void AFadeEntranceDoesNotSlide() {
    const int id = 9003;
    double start = 3000.0;
    ScrollbarVisibilitySet(id, false, ScrollbarEntrance::Fade, 0, 0, start);
    ScrollbarVisibilitySet(id, true, ScrollbarEntrance::Fade, kEnter, kExit,
                           start);

    ScrollbarVisibility halfway = ScrollbarVisibilityAt(id, start + kEnter / 2);
    utassert(halfway.position == 1.f);
    utassert(halfway.opacity < 1.f);
}

static void SlideMovesTowardTheNearestEdge() {
    utassert(ScrollbarSlideOffset(16.f, 0.f) == 16.f);
    utassert(ScrollbarSlideOffset(16.f, 1.f) == 0.f);
    utassert(TestNear(ScrollbarSlideOffset(16.f, 0.5f), 8.f));
    // Out of range is clamped, the way `progress.clamp(0.0, 1.0)` is.
    utassert(ScrollbarSlideOffset(16.f, 2.f) == 0.f);
    utassert(ScrollbarSlideOffset(16.f, -1.f) == 16.f);
}

// An interruption picks up from where it had got to rather than restarting,
// and the leg it runs is shortened by the distance left to cover.
static void AReversalStartsFromTheCurrentProgress() {
    const int id = 9004;
    double start = 4000.0;
    ScrollbarVisibilitySet(id, false, ScrollbarEntrance::Fade, 0, 0, start);
    ScrollbarVisibilitySet(id, true, ScrollbarEntrance::SlideAndFade, kEnter,
                           kExit, start);

    double at = start + 0.06;
    ScrollbarVisibility before = ScrollbarVisibilityAt(id, at);
    ScrollbarVisibilitySet(id, false, ScrollbarEntrance::SlideAndFade, kEnter,
                           kExit, at);
    ScrollbarVisibility reversed = ScrollbarVisibilityAt(id, at);
    utassert(TestNear(reversed.opacity, before.opacity));
    utassert(TestNear(reversed.position, before.position));

    ScrollbarVisibility after = ScrollbarVisibilityAt(id, at + 0.01);
    utassert(after.opacity < before.opacity);
    utassert(after.position < before.position);
}

// A motionless policy — reduced motion, or a theme that projects none —
// adopts the target outright, even mid-flight.
static void MotionlessSnaps() {
    const int id = 9005;
    double start = 5000.0;
    ScrollbarVisibilitySet(id, false, ScrollbarEntrance::Fade, 0, 0, start);
    ScrollbarVisibilitySet(id, true, ScrollbarEntrance::SlideAndFade, kEnter,
                           kExit, start);
    ScrollbarVisibilitySet(id, true, ScrollbarEntrance::Fade, 0, 0,
                           start + 0.05);

    ScrollbarVisibility now = ScrollbarVisibilityAt(id, start + 0.05);
    utassert(now.opacity == 1.f);
    utassert(now.position == 1.f);
    utassert(!now.running);
}

// The mode decides the choreography: hover mode slides its thumb in, the
// other two fade in place.
static void HoverModeIsTheOneThatSlides() {
    ScrollbarMotion bare;
    utassert(bare.idle == 2 && bare.enter == 0 && bare.exit == 0 &&
             bare.expand == 0);
    utassert(ScrollbarMotionFor(ScrollbarMode::Hover)
                 .thumbHoverEntrance == ScrollbarEntrance::SlideAndFade);
    utassert(ScrollbarMotionFor(ScrollbarMode::Scrolling)
                 .thumbHoverEntrance == ScrollbarEntrance::Fade);
    utassert(ScrollbarMotionFor(ScrollbarMode::Always)
                 .thumbHoverEntrance == ScrollbarEntrance::Fade);
    // The entrance every mode shares, and the hold before the exit.
    utassert(ScrollbarMotionFor(ScrollbarMode::Hover)
                 .entrance == ScrollbarEntrance::Fade);
    utassert(ScrollbarMotionFor(ScrollbarMode::Hover).idle == 2.f);
}

static void MotionBuildersAreImmutable() {
    ScrollbarMotion motion;
    ScrollbarMotion changed =
        motion.WithIdle(3)
            .WithEnter(.2f)
            .WithExit(.4f)
            .WithExpand(.6f)
            .WithEntrance(ScrollbarEntrance::SlideAndFade)
            .WithThumbHoverEntrance(ScrollbarEntrance::Fade);
    utassert(motion.idle == 2 && motion.enter == 0 && motion.exit == 0 &&
             motion.expand == 0);
    utassert(changed.idle == 3 && changed.enter == .2f && changed.exit == .4f &&
             changed.expand == .6f);
    utassert(changed.entrance == ScrollbarEntrance::SlideAndFade);
    utassert(changed.thumbHoverEntrance == ScrollbarEntrance::Fade);
}

struct TestScrollbarHandleState {
    Bounds viewport = {1, 2, 30, 40};
    Point offset = {5, 6};
    Size content = {300, 400};
    int starts = 0;
    int ends = 0;
};

static Bounds TestHandleViewport(void* user) {
    return ((TestScrollbarHandleState*)user)->viewport;
}

static Point TestHandleOffset(void* user) {
    return ((TestScrollbarHandleState*)user)->offset;
}

static void TestHandleSetOffset(void* user, Point value) {
    ((TestScrollbarHandleState*)user)->offset = value;
}

static Size TestHandleContent(void* user) {
    return ((TestScrollbarHandleState*)user)->content;
}

static void TestHandleStart(void* user) {
    ((TestScrollbarHandleState*)user)->starts++;
}

static void TestHandleEnd(void* user) {
    ((TestScrollbarHandleState*)user)->ends++;
}

static void HandlePreservesTheSourceOperations() {
    TestScrollbarHandleState state;
    ScrollbarHandle handle = {&state,
                              TestHandleViewport,
                              TestHandleOffset,
                              TestHandleSetOffset,
                              TestHandleContent,
                              TestHandleStart,
                              TestHandleEnd};
    utassert(handle.IsValid());
    utassert(handle.ViewportBounds().x == 1 && handle.ViewportBounds().h == 40);
    utassert(handle.Offset().x == 5 && handle.ContentSize().h == 400);
    handle.SetOffset({17, 19});
    handle.StartDrag();
    handle.EndDrag();
    utassert(state.offset.x == 17 && state.offset.y == 19);
    utassert(state.starts == 1 && state.ends == 1);
}

static void StyleBuildersAndPrepaintMatchTheSourceGeometry() {
    ScrollbarTrackStyle track;
    ScrollbarTrackStyle styledTrack =
        track.Bg(Rgba{1, 0, 0, 1}).BorderColor(Rgba{0, 1, 0, 1}).Width(18);
    utassert(!track.hasBackground && !track.hasBorder && !track.hasWidth);
    utassert(styledTrack.hasBackground && styledTrack.hasBorder &&
             styledTrack.hasWidth && styledTrack.width == 18);

    ScrollbarThumbStyle thumb;
    ScrollbarThumbStyle styledThumb =
        thumb.Bg(Rgba{0, 0, 1, 1}).Width(8).Inset(4).Radius(20).MinLength(60);
    utassert(!thumb.hasWidth && !thumb.hasInset && !thumb.hasRadius);
    utassert(styledThumb.hasBackground && styledThumb.hasWidth &&
             styledThumb.hasInset && styledThumb.hasRadius &&
             styledThumb.hasMinLength);

    ScrollbarStyles styles =
        ScrollbarStyles{}
            .Track(styledTrack)
            .TrackHover(ScrollbarTrackStyle{}.Width(99))
            .Thumb(styledThumb)
            .ThumbHover(ScrollbarThumbStyle{}.Width(12))
            .ThumbActive(ScrollbarThumbStyle{}.Inset(6).MinLength(80));
    utassert(!ScrollbarStyles{}.track.hasWidth);
    utassert(styles.track.width == 18 && styles.trackHover.width == 99);

    App app = {};
    Arena* arena = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.a = arena;
    El* projected = Scrollbar::ApplyStyles(&cx, Div(arena), styles);
    // Only normal track width sets the geometry. State variants resolve each
    // thumb property through the normal local style before source defaults.
    utassert(projected->scrollThemeSet);
    utassertnear(projected->scrollTrackWidth, 18.f);
    utassertnear(projected->scrollThumbWidth, 8.f);
    utassertnear(projected->scrollThumbHoverWidth, 12.f);
    utassertnear(projected->scrollThumbHoverInset, 4.f);
    utassertnear(projected->scrollThumbHoverRadius, 20.f);
    utassertnear(projected->scrollThumbActiveWidth, 8.f);
    utassertnear(projected->scrollThumbActiveInset, 6.f);
    utassertnear(projected->scrollThumbActiveMinLength, 80.f);
    ArenaDelete(arena);

    AxisPrepaintState vertical = ScrollbarPrepaintAxis(
        Axis::Vertical, Bounds{10, 20, 16, 200}, 300, 200, 800, styledThumb);
    // 200 / 800 * 200 = 50, raised to the styled 60-pixel minimum;
    // the source stores the 52-pixel inset-adjusted clickable length.
    utassertnear(vertical.thumbSize, 52.f);
    utassertnear(vertical.thumbBounds.x, 6.f);
    utassertnear(vertical.thumbBounds.y, 94.f);
    utassertnear(vertical.thumbBounds.w, 16.f);
    utassertnear(vertical.thumbFillBounds.x, 14.f);
    utassertnear(vertical.thumbFillBounds.w, 8.f);
    utassertnear(vertical.radius, 4.f);
    utassert(vertical.visibilityRequested &&
             vertical.visibilityOpacity == 1.f &&
             vertical.visibilityPosition == 1.f);

    AxisPrepaintState horizontal = ScrollbarPrepaintAxis(
        Axis::Horizontal, Bounds{20, 30, 200, 16}, 600, 200, 800, styledThumb);
    utassertnear(horizontal.thumbBounds.x, 164.f);
    utassertnear(horizontal.thumbBounds.y, 26.f);
    utassertnear(horizontal.thumbBounds.h, 16.f);
    utassertnear(horizontal.thumbFillBounds.y, 34.f);
    utassertnear(horizontal.thumbFillBounds.h, 8.f);

    AxisPrepaintState hidden = ScrollbarPrepaintAxis(
        Axis::Vertical, Bounds{0, 0, 16, 200}, 0, 200, 200, styledThumb);
    utassert(!hidden.visibilityRequested && hidden.thumbSize == 0);

    ScrollbarAxis exact = ScrollbarAxis::Both;
    ScrollAxis compatibility = exact;
    utassert(compatibility == ScrollAxis::Both);
}

void TestScrollbarMotion() {
    TestSuite("scrollbar_motion");
    VisibilityUsesDirectionSpecificCurvesAndDurations();
    EntranceFadesLinearlyWhilePositionEasesOut();
    AFadeEntranceDoesNotSlide();
    SlideMovesTowardTheNearestEdge();
    AReversalStartsFromTheCurrentProgress();
    MotionlessSnaps();
    HoverModeIsTheOneThatSlides();
    MotionBuildersAreImmutable();
    HandlePreservesTheSourceOperations();
    StyleBuildersAndPrepaintMatchTheSourceGeometry();
}
