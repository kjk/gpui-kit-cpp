/* GestureEvent and El::OnGesture, TouchEvent and El::OnTouch, which have
   no Rust twin, and the touch pinch arithmetic. The Windows window turns
   WM_GESTURE and WM_POINTER into these; this drives the shared half:
   dispatch to the chain where the touch began, a pan nobody takes
   scrolling, and the zoom distance as PinchEvent deltas. */

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

struct TouchView {
    int inner = 0;
    int outer = 0;
    TouchEvent last = {};
    bool innerTakes = true;

    static void OnInner(TouchView* self, Ctx*, const TouchEvent* ev) {
        self->inner++;
        self->last = *ev;
        const_cast<TouchEvent*>(ev)->propagate = !self->innerTakes;
    }

    static void OnOuter(TouchView* self, Ctx*, const TouchEvent*) {
        self->outer++;
    }

    static El* Render(TouchView*, Ctx* cx) {
        El* inner = Div(cx->a)->W(100)->H(100)->OnTouch(
            Listen(cx, &TouchView::OnInner));
        return Div(cx->a)
            ->SizeFull()
            ->OnTouch(Listen(cx, &TouchView::OnOuter))
            ->Child(inner);
    }
};

static TouchEvent MkTouch(TouchPhase phase, uint32_t id, Point at) {
    TouchEvent ev = {};
    ev.phase = phase;
    ev.pointerId = id;
    ev.x = at.x;
    ev.y = at.y;
    return ev;
}

// The element that takes a contact's Started keeps its moves and its end,
// wherever they go; another contact is hit-tested on its own.
static void TouchStaysWithItsTaker() {
    App* app = TestAppNew();
    auto view = EntityNew<TouchView>(app);
    Window* win = TestWindowOpen(app, view, 400, 300, 1);
    TouchView* self = view.Get(app);

    TestSimulateTouch(win, MkTouch(TouchPhase::Started, 7, {50, 50}));
    TestSimulateTouch(win, MkTouch(TouchPhase::Moved, 7, {300, 200}));
    utassert(self->inner == 2 && self->outer == 0);
    utassert(self->last.phase == TouchPhase::Moved);
    utassert(self->last.x == 300 && self->last.pointerId == 7);

    // a second finger outside the inner box goes to the outer one, which
    // leaves `propagate` set and so keeps nothing
    TestSimulateTouch(win, MkTouch(TouchPhase::Started, 8, {350, 250}));
    TestSimulateTouch(win, MkTouch(TouchPhase::Moved, 8, {340, 250}));
    utassert(self->outer == 1);

    TestSimulateTouch(win, MkTouch(TouchPhase::Ended, 7, {300, 200}));
    utassert(self->inner == 3 && self->last.phase == TouchPhase::Ended);
    // after the end the contact belongs to nobody
    TestSimulateTouch(win, MkTouch(TouchPhase::Moved, 7, {50, 50}));
    utassert(self->inner == 3 && self->outer == 1);

    // an inner handler that lets it propagate hands the Started on
    self->innerTakes = false;
    TestSimulateTouch(win, MkTouch(TouchPhase::Started, 9, {50, 50}));
    utassert(self->inner == 4 && self->outer == 2);
    TestAppFree(app);
}

// The Windows window lets a pen contact reach DefWindowProc, which makes
// mouse messages of it, only while no handler keeps it.
static void KeptTouchIsKnownUntilItEnds() {
    App* app = TestAppNew();
    auto view = EntityNew<TouchView>(app);
    Window* win = TestWindowOpen(app, view, 400, 300, 1);
    TouchView* self = view.Get(app);

    TouchEvent pen = MkTouch(TouchPhase::Started, 3, {50, 50});
    pen.pen = true;
    pen.eraser = true;
    utassert(!WindowTouchKept(win, 3));
    TestSimulateTouch(win, pen);
    utassert(WindowTouchKept(win, 3));
    utassert(self->last.pen && self->last.eraser);
    pen.phase = TouchPhase::Ended;
    TestSimulateTouch(win, pen);
    utassert(!WindowTouchKept(win, 3));

    // one the outer box sees but lets propagate is nobody's
    pen.phase = TouchPhase::Started;
    pen.x = 350;
    TestSimulateTouch(win, pen);
    utassert(self->outer == 1 && !WindowTouchKept(win, 3));
    TestAppFree(app);
}

// The X11 window's tablet devices, by the names and axes XInput2 lists.
static void PenToolsByDevice() {
    utassert(PenToolOf(StrL("Wacom Intuos S Pen (0x1)"), false, true) ==
             PenTool::Pen);
    utassert(PenToolOf(StrL("Wacom Intuos S Eraser (0x2)"), false, true) ==
             PenTool::Eraser);
    utassert(PenToolOf(StrL("Wacom Intuos Pen stylus"), false, false) ==
             PenTool::Pen);
    utassert(PenToolOf(StrL("Wacom Intuos Pen eraser"), false, false) ==
             PenTool::Eraser);
    utassert(PenToolOf(StrL("xwayland-tablet stylus"), false, true) ==
             PenTool::Pen);
    utassert(PenToolOf(StrL("xwayland-tablet eraser"), false, true) ==
             PenTool::Eraser);
    // a puck, a touch screen and a mouse are not pens
    utassert(PenToolOf(StrL("Wacom Intuos Pen Mouse (0x3)"), false, false) ==
             PenTool::NotPen);
    utassert(PenToolOf(StrL("ELAN Touchscreen"), true, true) ==
             PenTool::NotPen);
    utassert(PenToolOf(StrL("xwayland-pointer:17"), false, false) ==
             PenTool::NotPen);
    // "stylus" inside a word is not one
    utassert(PenToolOf(StrL("Nostylus Mouse"), false, false) ==
             PenTool::NotPen);
}

// The X11 pen: a tip marked on the slave device makes the core press of
// the same time a TouchEvent; a kept contact eats its core events.
static void PenContactTakesItsCorePress() {
    App* app = TestAppNew();
    auto view = EntityNew<TouchView>(app);
    Window* win = TestWindowOpen(app, view, 400, 300, 1);
    TouchView* self = view.Get(app);
    Modifiers none = {};
    PenContact pc = {};

    // no tip: a mouse press
    utassert(!PenContactCore(win, &pc, TouchPhase::Started, 10, 50, 50, none));
    utassert(self->inner == 0);

    // a tip whose core press has another time is not this one's
    PenContactTip(&pc, 0x80000005u, false, 20);
    utassert(!PenContactCore(win, &pc, TouchPhase::Started, 21, 50, 50, none));
    utassert(self->inner == 0 && !pc.pending);

    PenContactTip(&pc, 0x80000005u, true, 30);
    utassert(PenContactCore(win, &pc, TouchPhase::Started, 30, 50, 50, none));
    utassert(self->inner == 1 && self->last.pen && self->last.eraser);
    utassert(self->last.pointerId == 0x80000005u);
    utassert(PenContactCore(win, &pc, TouchPhase::Moved, 31, 300, 200, none));
    utassert(self->inner == 2 && self->last.x == 300);
    utassert(PenContactCore(win, &pc, TouchPhase::Ended, 32, 300, 200, none));
    utassert(self->last.phase == TouchPhase::Ended);
    utassert(!WindowTouchKept(win, 0x80000005u));
    // after the release the pointer is a mouse again
    utassert(!PenContactCore(win, &pc, TouchPhase::Moved, 33, 60, 60, none));
    utassert(self->inner == 3);

    // nobody keeps it: the core events stay mouse input
    PenContactTip(&pc, 0x80000006u, false, 40);
    utassert(
        !PenContactCore(win, &pc, TouchPhase::Started, 40, 350, 250, none));
    utassert(self->outer == 1);
    utassert(!PenContactCore(win, &pc, TouchPhase::Moved, 41, 340, 250, none));
    utassert(self->outer == 1);
    TestAppFree(app);
}

void TestGesture() {
    TestSuite("gesture");
    GesturesGoInnermostFirst();
    GesturesStayWhereTheTouchBegan();
    AnUntakenPanScrolls();
    TouchZoomDeltasAddUpToTheScale();
    TouchStaysWithItsTaker();
    KeptTouchIsKnownUntilItEnds();
    PenToolsByDevice();
    PenContactTakesItsCorePress();
}
