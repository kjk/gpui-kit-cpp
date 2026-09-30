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

// phaseless_wheel_scrolls_back_right_after_bouncing and
// phaseless_wheel_bounces_again_only_after_a_pause drive a window's wheel
// stream with Moved packets and no Started, as smooth-scrolling mouse
// drivers on macOS send them. These walk the same packets through what the
// wheel handler does with them: a packet at rest on the edge grabs and
// releases it, and the suppression that release sets is lifted by an inward
// packet or a pause.
static void APhaselessWheelScrollsBackRightAfterBouncing() {
    ScrollBouncePhysics physics;
    ScrollBounceSuppression suppression;
    // -50 at the bottom, at rest: the edge stretches and is let go.
    utassert(!suppression.Lifts(1.0, -50));
    physics.Begin(600);
    physics.Pull(-50);
    suppression.Release(&physics, true);
    utassert(physics.Offset() < 0 && physics.suppressMomentum);
    // +200 right after it points inward, which is a new scroll.
    utassert(suppression.Lifts(1.01, 200));
    physics.suppressMomentum = false;
    bool fromRest = !physics.dragging;
    utassert(fromRest);
    physics.Begin(600);
    float remainder = physics.Pull(200);
    suppression.Release(&physics, fromRest);
    utassert(remainder > 0);
    utassertnear(physics.Offset(), 0);
}

static void APhaselessWheelBouncesAgainOnlyAfterAPause() {
    ScrollBouncePhysics physics;
    ScrollBounceSuppression suppression;
    utassert(!suppression.Lifts(1.0, 50));
    physics.Begin(600);
    physics.Pull(50);
    suppression.Release(&physics, true);
    float bounced = physics.Offset();
    utassert(bounced > 0 && physics.suppressMomentum);
    // Momentum after an edge hit keeps pushing outward; it must not stretch
    // further.
    utassert(!suppression.Lifts(1.01, 50));
    utassert(physics.suppressMomentum);
    // After MOMENTUM_GAP another outward packet bounces again.
    utassert(suppression.Lifts(1.01 + kMomentumGapSeconds, 50));
    physics.suppressMomentum = false;
    physics.Begin(600);
    physics.Pull(50);
    suppression.Release(&physics, true);
    utassert(physics.Offset() > bounced);
    // A gesture's own release suppresses both ways: no inward lift.
    ScrollBounceSuppression gesture;
    physics.Begin(600);
    physics.Pull(50);
    gesture.Release(&physics, false);
    utassert(physics.suppressMomentum && gesture.direction == 0);
    utassert(!gesture.Lifts(2.0, -50));
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
    APhaselessWheelScrollsBackRightAfterBouncing();
    APhaselessWheelBouncesAgainOnlyAfterAPause();
}
