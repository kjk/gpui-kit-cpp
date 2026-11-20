/* GestureEvent and El::OnGesture, which have no Rust twin, and the touch
   pinch arithmetic. The Windows window turns WM_GESTURE into these; this
   drives the shared half: dispatch to the chain where the touch began, a
   pan nobody takes scrolling, and the zoom distance as PinchEvent deltas. */

#include "Test.h"

struct GestureView {
    int inner = 0;
    int outer = 0;
    int pinches = 0;
    GestureEvent last = {};
    bool innerPropagates = false;

    static void OnInner(GestureView* self, Ctx*, const GestureEvent* ev) {
        self->inner++;
        self->last = *ev;
        const_cast<GestureEvent*>(ev)->propagate = self->innerPropagates;
    }

    static void OnOuter(GestureView* self, Ctx*, const GestureEvent*) {
        self->outer++;
    }

    static void OnPinch(GestureView* self, Ctx*, const PinchEvent* ev) {
        self->pinches++;
        const_cast<PinchEvent*>(ev)->propagate = false;
    }

    static El* Render(GestureView*, Ctx* cx) {
        El* inner = Div(cx->a)
                        ->W(100)
                        ->H(100)
                        ->OnGesture(Listen(cx, &GestureView::OnInner))
                        ->OnPinch(Listen(cx, &GestureView::OnPinch));
        return Div(cx->a)
            ->SizeFull()
            ->OnGesture(Listen(cx, &GestureView::OnOuter))
            ->Child(inner);
    }
};

static GestureEvent MkGesture(GestureKind kind, Point at,
                              TouchPhase phase = TouchPhase::Moved) {
    GestureEvent ev = {};
    ev.kind = kind;
    ev.phase = phase;
    ev.x = at.x;
    ev.y = at.y;
    return ev;
}

static void GesturesGoInnermostFirst() {
    App* app = TestAppNew();
    auto view = EntityNew<GestureView>(app);
    Window* win = TestWindowOpen(app, view, 400, 300, 1);
    GestureView* self = view.Get(app);

    GestureEvent rot = MkGesture(GestureKind::Rotate, {50, 50});
    rot.angle = 1.5f;
    TestSimulateGesture(win, rot);
    utassert(self->inner == 1);
    utassert(self->outer == 0);
    utassert(self->last.kind == GestureKind::Rotate);
    utassert(self->last.angle == 1.5f);

    self->innerPropagates = true;
    TestSimulateGesture(
        win, MkGesture(GestureKind::TwoFingerTap, {50, 50}, TouchPhase::Ended));
    utassert(self->inner == 2);
    utassert(self->outer == 1);

    // outside the inner box only the outer one hears it
    TestSimulateGesture(win, MkGesture(GestureKind::PressAndTap, {300, 200}));
    utassert(self->inner == 2);
    utassert(self->outer == 2);

    utassert(WindowWantsGestures(win, 50, 50));
    TestAppFree(app);
}

// A pan (or a pinch) that wanders off the element it began on stays with
// it until the touch ends.
static void GesturesStayWhereTheTouchBegan() {
    App* app = TestAppNew();
    auto view = EntityNew<GestureView>(app);
    Window* win = TestWindowOpen(app, view, 400, 300, 1);
    GestureView* self = view.Get(app);

    WindowGestureBegin(win, 50, 50);
    TestSimulateGesture(
        win, MkGesture(GestureKind::Pan, {50, 50}, TouchPhase::Started));
    GestureEvent pan = MkGesture(GestureKind::Pan, {300, 200});
    pan.dx = 250;
    pan.dy = 150;
    pan.inertia = true;
    TestSimulateGesture(win, pan);
    TestSimulatePinch(win, {300, 200}, 0.1f);
    utassert(self->inner == 2);
    utassert(self->outer == 0);
    utassert(self->pinches == 1);
    utassert(self->last.inertia);
    utassert(self->last.dx == 250 && self->last.x == 300);
    WindowGestureEnd(win);

    TestSimulateGesture(win, MkGesture(GestureKind::Pan, {300, 200}));
    TestSimulatePinch(win, {300, 200}, 0.1f);
    utassert(self->inner == 2);
    utassert(self->outer == 1);
    utassert(self->pinches == 1);
    TestAppFree(app);
}

struct GesturePanScrollView {
    float scrollY = 0;

    static void OnScroll(GesturePanScrollView* self, Ctx*,
                         const ScrollEvent* ev) {
        self->scrollY = ev->offsetY;
    }

    static El* Render(GesturePanScrollView* self, Ctx* cx) {
        Arena* a = cx->a;
        El* area = VFlex(a)->W(kFill)->MaxH(100);
        for (int i = 0; i < 6; i++) {
            area->Child(Div(a)->H(50)->Shrink0());
        }
        El* scroller =
            component::ScrollableElement::OverflowYScrollbar(cx, area)
                ->ScrollY(self->scrollY)
                ->OnScroll(Listen(cx, &GesturePanScrollView::OnScroll))
                ->IntoEl();
        return HFlex(a)->W(100)->Child(VFlex(a)->W(kFill)->Child(scroller));
    }
};

// A pan no element takes is a precise wheel: the finger moving up scrolls
// the content down.
static void AnUntakenPanScrolls() {
    App* app = TestAppNew();
    component::Init(app);
    auto view = EntityNew<GesturePanScrollView>(app);
    Window* win = TestWindowOpen(app, view, 400, 300, 1);
    GesturePanScrollView* self = view.Get(app);
    TestDraw(win);

    // the scrolled box is centered in the window: 0,100 100x100
    utassert(!WindowWantsGestures(win, 50, 150));
    TestSimulateGesture(
        win, MkGesture(GestureKind::Pan, {50, 150}, TouchPhase::Started));
    GestureEvent pan = MkGesture(GestureKind::Pan, {50, 110});
    pan.dy = -40;
    TestSimulateGesture(win, pan);
    TestDraw(win);
    utassertnear(self->scrollY, 40.f);
    TestAppFree(app);
}

// The fingers 100 apart, then 150, then 200: deltas that add up to the 2x
// the distance grew by.
static void TouchZoomDeltasAddUpToTheScale() {
    TouchZoom z = {};
    utassert(TouchZoomDelta(&z, 100, TouchPhase::Started) == 0);
    utassertnear(TouchZoomDelta(&z, 150, TouchPhase::Moved), 0.5f);
    utassertnear(TouchZoomDelta(&z, 200, TouchPhase::Ended), 0.5f);
    utassert(z.first == 0);

    // a change whose begin was missed starts the zoom
    utassert(TouchZoomDelta(&z, 80, TouchPhase::Moved) == 0);
    utassertnear(TouchZoomDelta(&z, 40, TouchPhase::Moved), -0.5f);
}

void TestGesture() {
    TestSuite("gesture");
    GesturesGoInnermostFirst();
    GesturesStayWhereTheTouchBegan();
    AnUntakenPanScrolls();
    TouchZoomDeltasAddUpToTheScale();
}
