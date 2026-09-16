/* Ported from crates/base/src/scroll_bounce.rs. */

#include "Test.h"

static void ResistanceAndReversePreserveUnconsumedDistance() {
    ScrollBouncePhysics scroll;
    scroll.Begin(600);
    utassertnear(scroll.Pull(100), 0);
    utassert(scroll.Offset() > 0 && scroll.Offset() < 55);
    utassertnear(scroll.Pull(-130), -30);
    utassertnear(scroll.Offset(), 0);
}

static float BounceAfter(float responseMs) {
    ScrollBouncePhysics scroll;
    scroll.motion = scroll.motion.WithResponse(responseMs);
    scroll.Begin(600);
    scroll.Pull(180);
    scroll.Release();
    scroll.Step(.25f);
    return scroll.Offset();
}

static void ResponseScalesTheReturnAndZeroSnaps() {
    utassert(BounceAfter(1000) > BounceAfter(524));
    utassert(BounceAfter(524) > BounceAfter(200));
    utassertnear(BounceAfter(0), 0);
}

static void RegrabbingTheSpringDoesNotJump() {
    ScrollBouncePhysics scroll;
    scroll.Begin(600);
    scroll.Pull(-150);
    scroll.Release();
    scroll.Step(.08f);
    float before = scroll.Offset();
    scroll.Begin(600);
    utassertnear(scroll.Offset(), before);
    utassert(!scroll.Step(.1f));
    utassertnear(scroll.Offset(), before);
}

static float QuarterSecondAt(float hz) {
    ScrollBouncePhysics scroll;
    scroll.Begin(600);
    scroll.Pull(180);
    scroll.Release();
    int n = (int)(hz / 4);
    for (int i = 0; i < n; i++) scroll.Step(1.f / hz);
    return scroll.Offset();
}

static void SpringTrajectoryDoesNotDependOnRefreshRate() {
    utassertnear(QuarterSecondAt(60), QuarterSecondAt(120));
}

void TestScrollBounce() {
    TestSuite("scroll_bounce");
    ResistanceAndReversePreserveUnconsumedDistance();
    ResponseScalesTheReturnAndZeroSnaps();
    RegrabbingTheSpringDoesNotJump();
    SpringTrajectoryDoesNotDependOnRefreshRate();
}
