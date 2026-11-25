/* PinchEvent and div().on_pinch (gpui interactive.rs, elements/div.rs). The
   macOS window feeds magnify events as PinchEvent; this drives the shared
   dispatch: innermost element first, cx.propagate() to go on outward. */

#include "Test.h"

struct PinchView {
    int inner = 0;
    int outer = 0;
    float delta = 0;
    TouchPhase phase = TouchPhase::Moved;
    bool innerPropagates = false;

    static void OnInner(PinchView* self, Ctx*, const PinchEvent* ev) {
        self->inner++;
        self->delta += ev->delta;
        self->phase = ev->phase;
        const_cast<PinchEvent*>(ev)->propagate = self->innerPropagates;
    }

    static void OnOuter(PinchView* self, Ctx*, const PinchEvent*) {
        self->outer++;
    }

    static El* Render(PinchView*, Ctx* cx) {
        El* inner = Div(cx->a)->W(100)->H(100)->OnPinch(
            Listen(cx, &PinchView::OnInner));
        return Div(cx->a)
            ->SizeFull()
            ->OnPinch(Listen(cx, &PinchView::OnOuter))
            ->Child(inner);
    }
};

#if GPUI_OS_WINDOWS
// Windows direct_manipulation.rs permits scroll-to-pinch promotion and keeps
// pinch classification even if the scale returns to 1.
static void PrecisionTouchpadTransformPhases() {
    WinTouchpadTransform gesture;
    Vec<PlatformInput> events;
    gesture.Apply(1, {0, 12}, {40, 50}, {}, &events);
    gesture.Apply(1, {0, 17}, {40, 50}, {}, &events);
    gesture.Apply(1, {0, 17}, {40, 50}, {}, &events);
    gesture.Apply(1.25f, {6, 10}, {40, 50}, {}, &events);
    gesture.Apply(1, {0, 3}, {40, 50}, {}, &events);
    gesture.End({40, 50}, {}, &events);
    utassert(len(events) == 7);
    utassert(events[0].kind == PlatformInputKind::ScrollWheel &&
             events[0].scrollWheel.phase == TouchPhase::Started &&
             events[0].scrollWheel.precise &&
             events[0].scrollWheel.deltaY == 12.f);
    utassert(events[1].scrollWheel.phase == TouchPhase::Moved &&
             events[1].scrollWheel.deltaY == 5.f);
    utassert(events[2].scrollWheel.phase == TouchPhase::Ended);
    utassert(events[3].kind == PlatformInputKind::Pinch &&
             events[3].pinch.phase == TouchPhase::Started);
    utassertnear(events[4].pinch.delta, 0.25f);
    utassertnear(events[5].pinch.delta, -0.2f);
    utassert(events[6].pinch.phase == TouchPhase::Ended);
}
#endif

void TestPinch() {
    TestSuite("pinch");

#if GPUI_OS_WINDOWS
    PrecisionTouchpadTransformPhases();
#endif
    App* app = TestAppNew();
    auto view = EntityNew<PinchView>(app);
    Window* win = TestWindowOpen(app, view, 400, 300, 1);
    PinchView* self = view.Get(app);

    TestSimulatePinch(win, {50, 50}, 0.f, TouchPhase::Started);
    TestSimulatePinch(win, {50, 50}, 0.25f);
    utassert(self->inner == 2);
    utassert(self->outer == 0);
    utassert(self->delta == 0.25f);
    utassert(self->phase == TouchPhase::Moved);

    self->innerPropagates = true;
    TestSimulatePinch(win, {50, 50}, 0.f, TouchPhase::Ended);
    utassert(self->inner == 3);
    utassert(self->outer == 1);
    utassert(self->phase == TouchPhase::Ended);

    // outside the inner box only the outer one sees it
    TestSimulatePinch(win, {300, 200}, 0.1f);
    utassert(self->inner == 3);
    utassert(self->outer == 2);

    TestAppFree(app);
}
