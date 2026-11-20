/* The portable half of the window frame API (gpui.h's "window frame and
 * placement"): keeping a rectangle on a display, centering, the dialog's
 * hold on its parent, and the bounds, activation and closed observers.
 * Rust's window.rs observe_window_bounds / observe_window_activation and
 * app.rs on_window_closed; ScreenBoundsShiftInto is SumatraPDF's
 * ShiftRectToWorkArea, which Rust has no twin of. The native halves need an
 * OS window and are not exercised here. */

#include "Test.h"

static void ShiftIntoKeepsVisibleRect() {
    ScreenBounds area = {0, 40, 1920, 1000};
    ScreenBounds r = {100, 100, 300, 200};
    utassert(ScreenBoundsShiftInto(r, area, true) == r);
    utassert(ScreenBoundsShiftInto(r, area, false) == r);
    // An empty area leaves it alone.
    ScreenBounds distant = {5000, 5000, 300, 200};
    utassert(ScreenBoundsShiftInto(distant, {}, true) == distant);
}

static void ShiftIntoPartlyOutside() {
    ScreenBounds area = {0, 40, 1920, 1000};
    // Hanging over the right and top edges: only `fully` moves it.
    ScreenBounds r = {1800, 20, 300, 200};
    utassert(ScreenBoundsShiftInto(r, area, false) == r);
    ScreenBounds in = ScreenBoundsShiftInto(r, area, true);
    utassert(in.x == 1920 - 300);
    utassert(in.y == 40);
    utassert(in.w == 300 && in.h == 200);
    // Off the bottom-left: x goes to the left edge, y to the bottom.
    ScreenBounds low = {-500, 2000, 300, 200};
    ScreenBounds back = ScreenBoundsShiftInto(low, area, false);
    utassert(back.x == 0);
    utassert(back.y == 40 + 1000 - 200);
}

static void ShiftIntoEntirelyOutside() {
    ScreenBounds area = {1920, 0, 1280, 1024};
    // On no part of the area: moved in even without `fully`.
    ScreenBounds r = {100, 100, 300, 200};
    ScreenBounds in = ScreenBoundsShiftInto(r, area, false);
    utassert(in.x == 1920);
    utassert(in.y == 100);
}

static void CenteredOver() {
    ScreenBounds over = {100, 50, 800, 600};
    ScreenBounds c = ScreenBoundsCentered({0, 0, 200, 100}, over);
    utassert(c.x == 400 && c.y == 300);
    utassert(c.w == 200 && c.h == 100);
    // Larger than what it is centered on: it overhangs both sides.
    ScreenBounds big = ScreenBoundsCentered({7, 7, 1000, 700}, over);
    utassert(big.x == 0 && big.y == 0);
}

struct FrameObserved {
    int activations = 0;
    bool lastActive = false;
    int closed = 0;
    Window* closedWin = nullptr;
    int bounds = 0;
    WindowBoundsEvent lastBounds = WindowBoundsEvent::Changed;
};

static void OnActivation(void* data, Window*, bool active) {
    auto* o = (FrameObserved*)data;
    o->activations++;
    o->lastActive = active;
}

static void OnClosed(void* data, Window* win) {
    auto* o = (FrameObserved*)data;
    o->closed++;
    o->closedWin = win;
}

static void OnBounds(void* data, Window*, WindowBoundsEvent ev) {
    auto* o = (FrameObserved*)data;
    o->bounds++;
    o->lastBounds = ev;
}

// observe_window_activation hears every notification, the first one too:
// `active` starts out true, and a window's first activation changes nothing.
static void ActivationObserverHearsEveryChange() {
    App* app = TestAppNew();
    Window* win = TestWindowOpen(app, EntityId{}, 400, 300);
    FrameObserved o;
    WindowOnActivationChanged(win, OnActivation, &o);
    WindowSetActive(win, true);
    utassert(o.activations == 1 && o.lastActive);
    WindowSetActive(win, false);
    utassert(o.activations == 2 && !o.lastActive);
    utassert(!win->active);
    WindowOnActivationChanged(win, nullptr, nullptr);
    WindowSetActive(win, true);
    utassert(o.activations == 2);
    TestAppFree(app);
}

static void BoundsObserver() {
    App* app = TestAppNew();
    Window* win = TestWindowOpen(app, EntityId{}, 400, 300);
    FrameObserved o;
    WindowBoundsNotify(win, WindowBoundsEvent::Changed);
    utassert(o.bounds == 0);
    WindowOnBoundsChanged(win, OnBounds, &o);
    WindowBoundsNotify(win, WindowBoundsEvent::Changed);
    WindowBoundsNotify(win, WindowBoundsEvent::MoveResizeEnded);
    utassert(o.bounds == 2);
    utassert(o.lastBounds == WindowBoundsEvent::MoveResizeEnded);
    TestAppFree(app);
}

// A shown dialog holds its parent; the parent takes input again only when
// the last dialog holding it lets go, and a closed window is nobody's parent.
static void DialogHoldsParent() {
    App* app = TestAppNew();
    Window* main = TestWindowOpen(app, EntityId{}, 800, 600);
    Window* dlg1 = TestWindowOpen(app, EntityId{}, 300, 200);
    Window* dlg2 = TestWindowOpen(app, EntityId{}, 300, 200);
    dlg1->opts.kind = WindowKind::Dialog;
    dlg1->opts.parent = main;
    dlg2->opts.kind = WindowKind::Dialog;
    dlg2->opts.parent = main;
    utassert(WindowIsEnabled(main));
    // The test platform has no native windows; the hold is the bookkeeping.
    dlg1->modalParent = main;
    dlg2->modalParent = main;
    utassert(!WindowIsEnabled(main));
    WindowModalRelease(dlg1);
    utassert(dlg1->modalParent == nullptr);
    utassert(!WindowIsEnabled(main));
    WindowModalRelease(dlg2);
    utassert(WindowIsEnabled(main));
    // Releasing twice does nothing.
    WindowModalRelease(dlg2);
    utassert(WindowIsEnabled(main));

    FrameObserved o;
    AppOnWindowClosed(app, OnClosed, &o);
    dlg1->modalParent = main;
    WindowClosed(main);
    utassert(o.closed == 1 && o.closedWin == main);
    utassert(dlg1->opts.parent == nullptr && dlg1->modalParent == nullptr);
    utassert(dlg2->opts.parent == nullptr);
    AppOnWindowClosed(app, nullptr, nullptr);
    TestAppFree(app);
}

// Without a native window (the test platform here, a browser canvas, a
// mobile host's view) the frame is empty and the setters are harmless.
static void HeadlessFrameIsEmpty() {
    App* app = TestAppNew();
    Window* win = TestWindowOpen(app, EntityId{}, 400, 300);
    utassert(WindowFrameBounds(win).IsEmpty());
    utassert(WindowContentBounds(win).IsEmpty());
    WindowSetFrameBounds(win, {10, 10, 100, 100});
    WindowSetVisible(win, false, false);
    WindowActivateInApp(win);
    utassert(!WindowIsActive(win));
    WindowSetMinSize(win, {100, 50});
    TestAppFree(app);
}

void TestWindowFrame() {
    TestSuite("window_frame");
    ShiftIntoKeepsVisibleRect();
    ShiftIntoPartlyOutside();
    ShiftIntoEntirelyOutside();
    CenteredOver();
    ActivationObserverHearsEveryChange();
    BoundsObserver();
    DialogHoldsParent();
    HeadlessFrameIsEmpty();
}
