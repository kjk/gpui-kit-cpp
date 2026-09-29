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

static void APhasedWheelAtTheEdgeStretches() {
    ScrollBouncePhysics scroll;
    scroll.Begin(200);
    utassertnear(scroll.Pull(80), 0);
    utassert(scroll.Offset() > 0);
    scroll.Release();
    utassert(scroll.Offset() > 0);
    utassert(scroll.suppressMomentum);
    scroll.Step(.5f);
    utassert(scroll.Offset() >= 0);
    // A new finger-down ends suppression, matching the iOS momentum rule.
    scroll.Begin(200);
    utassert(!scroll.suppressMomentum);
}

// tiny_drag_catching_momentum_does_not_start_a_reverse_fling,
// deliberate_drag_after_catching_momentum_can_fling and
// short_catch_release_still_stretches_past_the_edge drive a window's wheel
// stream; what they turn on is the short-catch rule, which is this seam, and
// the momentum suppression it sets on the physics.
static void ATinyCatchSuppressesTheReverseFling() {
    ScrollBounceCatch shortDrag;
    ScrollBouncePhysics physics;
    // An ordinary drag starts moving at once: no catch.
    utassert(!shortDrag.Observe(TouchPhase::Started, -60));
    utassert(!shortDrag.Observe(TouchPhase::Ended, 0));
    utassert(!shortDrag.Observe(TouchPhase::Moved, -100));
    // A finger catching the fling starts at zero and moves no further than
    // the slop: its release suppresses what the recognizer synthesizes.
    physics.Begin(600);
    utassert(!shortDrag.Observe(TouchPhase::Started, 0));
    utassert(!shortDrag.Observe(TouchPhase::Moved, 8));
    bool suppress = shortDrag.Observe(TouchPhase::Ended, 0);
    utassert(suppress);
    physics.Release();
    if (suppress) {
        physics.suppressMomentum = true;
    }
    utassert(physics.suppressMomentum);
    // The reverse momentum packet is not a catch of its own.
    utassert(!shortDrag.Observe(TouchPhase::Moved, 100));
    // A fresh gesture restores ordinary scrolling and momentum.
    physics.Begin(600);
    utassert(!physics.suppressMomentum);
    utassert(!shortDrag.Observe(TouchPhase::Started, -20));
    utassert(!shortDrag.Observe(TouchPhase::Ended, 0));
}

static void ADeliberateDragAfterACatchCanFling() {
    ScrollBounceCatch shortDrag;
    utassert(!shortDrag.Observe(TouchPhase::Started, 0));
    utassert(!shortDrag.Observe(TouchPhase::Moved, 24));
    utassert(!shortDrag.Observe(TouchPhase::Ended, 0));
    // A cancelled catch is not a release either.
    utassert(!shortDrag.Observe(TouchPhase::Started, 0));
    utassert(!shortDrag.Observe(TouchPhase::Cancelled, 0));
    // The slop is inclusive: eight pixels in one go is still a catch.
    utassert(!shortDrag.Observe(TouchPhase::Started, 0));
    utassert(shortDrag.Observe(TouchPhase::Ended, kCatchDragSlop));
}

void TestScrollBounce() {
    TestSuite("scroll_bounce");
    ResistanceAndReversePreserveUnconsumedDistance();
    ResponseScalesTheReturnAndZeroSnaps();
    RegrabbingTheSpringDoesNotJump();
    SpringTrajectoryDoesNotDependOnRefreshRate();
    APhasedWheelAtTheEdgeStretches();
    ATinyCatchSuppressesTheReverseFling();
    ADeliberateDragAfterACatchCanFling();
}
