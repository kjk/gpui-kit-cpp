/* Ported from crates/base/src/scroll_bounce.rs. */

#include "Test.h"

#include <math.h>

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

static void MotionBuilderConfiguresTrackingAndResponse() {
    ScrollBounceMotion motion = ScrollBounceMotion()
                                    .WithTracking(0.4f)
                                    .WithResponse(300)
                                    .WithMaxStretch(24);
    utassertnear(motion.Tracking(), 0.4f);
    utassertnear(motion.Response(), 300);
    utassert(motion.HasMaxStretch());
    utassertnear(motion.MaxStretch(), 24);
}

static float StretchAt(float tracking) {
    ScrollBouncePhysics scroll;
    scroll.motion = scroll.motion.WithTracking(tracking);
    scroll.Begin(600);
    scroll.Pull(100);
    return scroll.Offset();
}

static void TrackingScalesTheFirstStretch() {
    utassert(StretchAt(0.3f) < StretchAt(0.55f));
    utassert(StretchAt(0.55f) < StretchAt(0.8f));
}

static void RegrabbingADisplacedEdgeKeepsItsExtent() {
    ScrollBouncePhysics scroll;
    scroll.Begin(600);
    scroll.Pull(-150);
    scroll.Release();
    scroll.Step(0.08f);
    float before = scroll.Offset();
    // The viewport shrank below the displacement while the edge returned.
    scroll.Begin(40);
    utassert(fabsf(scroll.Offset() - before) < 0.001f);
    scroll.Pull(10);
    utassert(scroll.Offset() > before);
    utassert(scroll.Offset() < before + 10);
}

static void AGestureFromRestAdoptsTheCurrentExtent() {
    ScrollBouncePhysics scroll;
    scroll.Begin(600);
    scroll.Begin(40);
    scroll.Pull(-100);
    // Saturates below the 40 px viewport, not the 600 px one.
    utassert(scroll.Offset() > -40.f);
}

static void ReturnKeepsAVisibleTailAfterAQuarterSecond() {
    ScrollBouncePhysics scroll;
    scroll.position = 100;
    utassert(scroll.Step(0.25f));
    utassert(scroll.Offset() > 10.f && scroll.Offset() < 30.f);
    utassert(scroll.velocity < 0.f);
}

static void SpringSettlesWithoutCrossingTheBoundary() {
    ScrollBouncePhysics scroll;
    scroll.Begin(600);
    scroll.Pull(-200);
    scroll.Release();
    utassert(scroll.suppressMomentum);
    float previous = scroll.Offset();
    for (int i = 0; i < 120; i++) {
        scroll.Step(1.f / 120.f);
        utassert(scroll.Offset() >= previous && scroll.Offset() <= 0.f);
        previous = scroll.Offset();
    }
    utassertnear(scroll.Offset(), 0);
    scroll.Begin(600);
    utassert(!scroll.suppressMomentum);
}

struct OverRoom {
    float room = 0;
};

static float OverUseRoom(void* user, float dy) {
    OverRoom* r = (OverRoom*)user;
    float used = dy > -r->room ? dy : -r->room;
    r->room += used;
    return used;
}

static void ExpectIn(float v, float lo, float hi) {
    if (!(v >= lo && v <= hi)) {
        logf("scroll bounce %f not in [%f, %f]\n", v, lo, hi);
    }
    utassert(v >= lo && v <= hi);
}

static void ASwipePastTheEndStretchesAndSpringsBack() {
    ScrollOverscroll over;
    OverRoom room{30};
    double now = 0;
    over.Packet(-10, TouchPhase::Started, 400, now, OverUseRoom, &room);
    over.Packet(-100, TouchPhase::Moved, 400, now, OverUseRoom, &room);
    utassertnear(room.room, 0);
    float stretched = over.Offset();
    utassert(stretched < 0.f && stretched > -80.f);
    over.Packet(0, TouchPhase::Ended, 400, now, OverUseRoom, &room);
    utassert(
        !over.Packet(-40, TouchPhase::Moved, 400, now, OverUseRoom, &room));
    double later = now;
    int guard = 0;
    while (over.Step(later) && guard < 400) {
        later += 0.016;
        guard++;
    }
    utassertnear(over.Offset(), 0);
}

static void PullingBackUnstretchesBeforeScrollingTheText() {
    ScrollOverscroll over;
    OverRoom room{0};
    double now = 0;
    over.Packet(0, TouchPhase::Started, 400, now, OverUseRoom, &room);
    over.Packet(-50, TouchPhase::Moved, 400, now, OverUseRoom, &room);
    utassert(over.Offset() < 0.f);
    struct Back {
        float scrolled = 0;
    } back;
    over.Packet(
        70, TouchPhase::Moved, 400, now,
        [](void* user, float dy) {
            Back* b = (Back*)user;
            b->scrolled += dy;
            return dy;
        },
        &back);
    utassertnear(over.Offset(), 0);
    utassertnear(back.scrolled, 20);
}

static void FlingPath(float speed, float room0, uint32_t stall,
                      Vec<float>* path) {
    const double frame = 0.016667;
    ScrollOverscroll over;
    float dy = -speed / 60.f;
    Vec<float> held;
    OverRoom room{room0};
    for (int n = 0; n < 120; n++) {
        double at = frame * (double)n;
        if (n < 100) {
            VecAppend(held, dy);
            dy *= 0.95f;
            float sum = 0;
            for (int i = 0; i < len(held); i++) sum += held[i];
            if (stall == 0 || n < (int)stall || room.room == 0.f ||
                -sum > room.room) {
                for (int i = 0; i < len(held); i++) {
                    over.Packet(held[i], TouchPhase::Moved, 300, at,
                                OverUseRoom, &room);
                }
                VecClear(held);
            }
        }
        over.Step(at);
        VecAppend(*path, over.Offset());
    }
}

static float PathPeak(const Vec<float>& path) {
    float peak = 0;
    for (int i = 0; i < len(path); i++) {
        if (path[i] < peak) peak = path[i];
    }
    return peak;
}

static void AFlingThrowsTheEdgeAsFarAsNSScrollViewDoes() {
    Vec<float> path;
    FlingPath(1500, 0, 0, &path);
    float peak = PathPeak(path);
    ExpectIn(peak, -16.f, -12.f);
    int turned = 0;
    for (int i = 0; i < len(path); i++) {
        if (path[i] == peak) {
            turned = i;
            break;
        }
    }
    utassert(turned >= 4 && turned <= 6);
    utassertnear(path[50], 0);
    for (int i = turned + 1; i < len(path); i++) {
        utassert(path[i] >= path[i - 1]);
    }
    for (int i = 50; i < len(path); i++) utassert(path[i] == 0.f);
    Vec<float> faster;
    FlingPath(3000, 0, 0, &faster);
    float fastPeak = PathPeak(faster);
    utassert(fabsf(fastPeak / peak - 2.f) < 0.15f);
    Vec<float> hardest;
    FlingPath(20000, 0, 0, &hardest);
    ExpectIn(PathPeak(hardest), -36.f, -34.f);
}

static void PacketsHandedOverAtOnceDoNotThrowTheEdgeFurther() {
    Vec<float> evenPath;
    Vec<float> stalledPath;
    FlingPath(4000, 500, 0, &evenPath);
    FlingPath(4000, 500, 3, &stalledPath);
    float even = PathPeak(evenPath);
    float stalled = PathPeak(stalledPath);
    utassert(even < -10.f);
    utassert(fabsf(stalled / even - 1.f) < 0.25f);
}

static void ATouchThatLiftsWithoutMovingLetsGoOfTheEdge() {
    ScrollOverscroll over;
    OverRoom room{0};
    over.Packet(-80, TouchPhase::Moved, 400, 0, OverUseRoom, &room);
    double later = 0.030;
    over.Step(later);
    float held = over.Offset();
    over.Packet(0, TouchPhase::Started, 400, later, OverUseRoom, &room);
    utassert(over.Packet(0, TouchPhase::Moved, 400, later, OverUseRoom, &room));
    double at = later;
    int guard = 0;
    while (over.Step(at) && guard < 400) {
        at += 0.016;
        guard++;
    }
    utassert(held < 0.f);
    utassertnear(over.Offset(), 0);
}

static float StretchTravel(float travel) {
    ScrollOverscroll over;
    OverRoom room{0};
    over.Packet(0, TouchPhase::Started, 300, 0, OverUseRoom, &room);
    over.Packet(-travel, TouchPhase::Moved, 300, 0, OverUseRoom, &room);
    return -over.Offset();
}

static void AStretchFollowsTheTravelAsNSScrollViewDoes() {
    ExpectIn(StretchTravel(100), 6.5f, 8.f);
    ExpectIn(StretchTravel(764), 28.f, 34.f);
    float stretched = StretchTravel(2400);
    utassert(stretched >= 35.f && stretched < 36.f);
    utassert(StretchTravel(100000) < 36.f);
}

static void AStillReleaseReturnsAtFullSpeed() {
    ScrollOverscroll over;
    OverRoom room{0};
    over.Packet(0, TouchPhase::Started, 300, 0, OverUseRoom, &room);
    over.Packet(-800, TouchPhase::Moved, 300, 0, OverUseRoom, &room);
    float held = over.Offset();
    double lifted = 0.200;
    over.Packet(0, TouchPhase::Ended, 300, lifted, OverUseRoom, &room);
    over.Step(lifted + 0.008);
    utassert(over.Offset() / held < 0.92f);
    over.Step(lifted + 0.055);
    float half = over.Offset() / held;
    ExpectIn(half, 0.45f, 0.55f);
}

// Text `end` px long whose offset is an f32, as a scroll handle's is.
struct LongText {
    float offset = 0;
    float end = 0;
};

static float LongTextScroll(void* user, float dy) {
    LongText* t = (LongText*)user;
    float before = t->offset;
    float next = before + dy;
    if (next < -t->end) next = -t->end;
    if (next > 0.f) next = 0.f;
    t->offset = next;
    return t->offset - before;
}

static void ADragBackOffAFarEdgeKeepsScrollingTheText() {
    const float end = 8902.5f;
    for (int returnedMs = 1; returnedMs < 60; returnedMs++) {
        ScrollOverscroll over;
        LongText text{-end, end};
        double start = 0;
        over.Packet(0, TouchPhase::Started, 600, start, LongTextScroll, &text);
        over.Packet(-200, TouchPhase::Moved, 600, start, LongTextScroll, &text);
        double lifted = start + 0.100;
        over.Packet(0, TouchPhase::Ended, 600, lifted, LongTextScroll, &text);
        double caught = lifted + (double)returnedMs / 1000.0;
        over.Step(caught);
        over.Packet(0, TouchPhase::Started, 600, caught, LongTextScroll, &text);
        over.Packet(300, TouchPhase::Moved, 600, caught, LongTextScroll, &text);
        float before = text.offset;
        over.Packet(122, TouchPhase::Moved, 600, caught, LongTextScroll, &text);
        if (over.Offset() != 0.f) {
            logf("edge stays put (%d ms): %f\n", returnedMs, over.Offset());
        }
        utassert(over.Offset() == 0.f);
        utassert(fabsf(text.offset - before - 122.f) < 0.01f);
    }
}

static void AfterAPauseTheNextFlickThrowsTheEdgeAgain() {
    ScrollOverscroll over;
    OverRoom room{0};
    utassert(over.Packet(-40, TouchPhase::Moved, 400, 0, OverUseRoom, &room));
    double at = 0;
    int guard = 0;
    while (over.Step(at) && guard < 400) {
        at += 0.016;
        guard++;
    }
    double later = at + 0.25;
    utassert(
        over.Packet(-40, TouchPhase::Moved, 400, later, OverUseRoom, &room));
}

static void MomentumBackIntoTheTextScrollsWhileTheEdgeReturns() {
    ScrollOverscroll over;
    OverRoom room{0};
    over.Packet(0, TouchPhase::Started, 400, 0, OverUseRoom, &room);
    over.Packet(-800, TouchPhase::Moved, 400, 0, OverUseRoom, &room);
    double lifted = 0.100;
    over.Packet(0, TouchPhase::Ended, 400, lifted, OverUseRoom, &room);
    double later = lifted + 0.016;
    over.Step(later);
    float before = over.Offset();
    utassert(before < 0.f);
    over.Packet(20, TouchPhase::Moved, 400, later, OverUseRoom, &room);
    utassertnear(room.room, 20);
    utassertnear(over.Offset(), before);
    over.Step(later + 0.016);
    utassert(over.Offset() > before && over.Offset() < 0.f);
}

static void ANewPreciseGestureTakesOverWithoutJumping() {
    ScrollOverscroll over;
    OverRoom room{0};
    over.Packet(-80, TouchPhase::Moved, 400, 0, OverUseRoom, &room);
    over.Step(0.016);
    double later = 0.200;
    over.Step(later);
    utassert(over.Offset() < 0.f);
    float before = over.Offset();
    over.Packet(0, TouchPhase::Started, 400, later, OverUseRoom, &room);
    utassert(fabsf(over.Offset() - before) < 0.001f);
    utassert(
        over.Packet(-40, TouchPhase::Moved, 400, later, OverUseRoom, &room));
    over.Step(later + 0.016);
    utassert(over.Offset() < before);
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
    MotionBuilderConfiguresTrackingAndResponse();
    TrackingScalesTheFirstStretch();
    RegrabbingADisplacedEdgeKeepsItsExtent();
    AGestureFromRestAdoptsTheCurrentExtent();
    ReturnKeepsAVisibleTailAfterAQuarterSecond();
    SpringSettlesWithoutCrossingTheBoundary();
    ASwipePastTheEndStretchesAndSpringsBack();
    PullingBackUnstretchesBeforeScrollingTheText();
    AFlingThrowsTheEdgeAsFarAsNSScrollViewDoes();
    PacketsHandedOverAtOnceDoNotThrowTheEdgeFurther();
    ATouchThatLiftsWithoutMovingLetsGoOfTheEdge();
    AStretchFollowsTheTravelAsNSScrollViewDoes();
    AStillReleaseReturnsAtFullSpeed();
    ADragBackOffAFarEdgeKeepsScrollingTheText();
    AfterAPauseTheNextFlickThrowsTheEdgeAgain();
    MomentumBackIntoTheTextScrollsWhileTheEdgeReturns();
    ANewPreciseGestureTakesOverWithoutJumping();
}
