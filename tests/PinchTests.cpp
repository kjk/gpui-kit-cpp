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

void TestPinch() {
    TestSuite("pinch");

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
