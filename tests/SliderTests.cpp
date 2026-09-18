/* Ported from crates/base/src/slider.rs, mod tests.
 *
 * The fourth Rust case, `legacy_logarithmic_validation_is_preserved`, asserts
 * a panic when a logarithmic slider is built with min <= 0. There are no
 * exceptions here and SliderSetScale nudges the limits instead, so that case
 * is replaced by one that checks the nudge. */

#include "Test.h"

static void ValueConversionsAndClampingArePreserved() {
    SliderValue single = SliderSingle(5.f);
    utassert(!single.range);
    utassertnear(single.End(), 5.f);
    utassertnear(single.Start(), 5.f);

    SliderValue range = SliderRange(2.f, 8.f);
    utassert(range.range);
    utassertnear(range.Start(), 2.f);
    utassertnear(range.End(), 8.f);

    SliderValue clamped = SliderValueClamp(SliderRange(-1.f, 12.f), 0.f, 10.f);
    utassert(clamped.range);
    utassertnear(clamped.Start(), 0.f);
    utassertnear(clamped.End(), 10.f);
}

static void LinearStateKeepsPercentageAndRangeOrdering() {
    SliderState s = {};
    SliderSetLimits(&s, 0.f, 200.f);
    SliderSetValue(&s, SliderRange(50.f, 150.f));

    utassertnear(s.value.Start(), 50.f);
    utassertnear(s.value.End(), 150.f);
    utassertnear(s.pctLo, 0.25f);
    utassertnear(s.pctHi, 0.75f);
}

static void LogarithmicStateKeepsMapping() {
    SliderState s = {};
    SliderSetLimits(&s, 1.f, 1000.f);
    SliderSetScale(&s, SliderScale::Logarithmic);
    SliderSetValue(&s, SliderSingle(10.f));

    utassert(s.pctHi > 1.f / 3.f - 0.0001f && s.pctHi < 1.f / 3.f + 0.0001f);
    // The mapping is its own inverse: a third of the way along is 10.
    utassertnear(SliderPctToValue(&s, 1.f / 3.f), 10.f);
    utassertnear(SliderPctToValue(&s, 0.f), 1.f);
    utassertnear(SliderPctToValue(&s, 1.f), 1000.f);
}

static void LogarithmicLimitsAreNudgedInsteadOfPanicking() {
    SliderState s = {};
    SliderSetScale(&s, SliderScale::Logarithmic);

    utassert(s.min > 0.f);
    utassert(s.max > s.min);
}

// update_value_by_position, against a 100x20 track at the origin.
static SliderState TrackState() {
    SliderState s = {};
    SliderSetLimits(&s, 0.f, 100.f);
    SliderSetBounds(&s, {0, 0, 100, 20});
    return s;
}

static void PositionUpdatesTheValueAlongTheAxis() {
    SliderState s = TrackState();
    utassert(SliderUpdateByPosition(&s, Axis::Horizontal, {50, 10}, false));
    utassertnear(s.value.End(), 50.f);
    utassert(s.dragging);

    // A vertical slider counts up from the bottom of its box.
    SliderState v = TrackState();
    SliderUpdateByPosition(&v, Axis::Vertical, {10, 15}, false);
    utassertnear(v.value.End(), 25.f);
}

static void PositionIsClampedToTheTrack() {
    SliderState s = TrackState();
    SliderUpdateByPosition(&s, Axis::Horizontal, {-40, 10}, false);
    utassertnear(s.value.End(), 0.f);
    SliderUpdateByPosition(&s, Axis::Horizontal, {400, 10}, false);
    utassertnear(s.value.End(), 100.f);
}

static void ValueSnapsToTheStep() {
    SliderState s = TrackState();
    SliderSetStep(&s, 25.f);
    SliderUpdateByPosition(&s, Axis::Horizontal, {30, 10}, false);
    utassertnear(s.value.End(), 25.f);
}

static void RangeEndsKeepTheirOrder() {
    SliderState s = TrackState();
    SliderSetValue(&s, SliderRange(20.f, 60.f));

    // Dragging the low end past the high one stops at it.
    SliderUpdateByPosition(&s, Axis::Horizontal, {90, 10}, true);
    utassertnear(s.value.Start(), 60.f);
    utassertnear(s.value.End(), 60.f);
}

static void APressTakesTheNearerHalfOfARange() {
    SliderState s = TrackState();
    SliderSetValue(&s, SliderRange(20.f, 60.f));

    // The midpoint between the thumbs is 40, not the midpoint of the track.
    utassert(SliderIsStartAt(&s, Axis::Horizontal, {30, 10}));
    utassert(!SliderIsStartAt(&s, Axis::Horizontal, {50, 10}));
    // A single-value slider has no start to take.
    SliderState single = TrackState();
    utassert(!SliderIsStartAt(&single, Axis::Horizontal, {10, 10}));
}

static void ReleaseOnlyFiresAfterAPress() {
    SliderState s = TrackState();
    utassert(!SliderHandleRelease(&s));

    SliderUpdateByPosition(&s, Axis::Horizontal, {50, 10}, false);
    utassert(SliderHandleRelease(&s));
    utassert(!s.dragging);
    utassert(!SliderHandleRelease(&s));
}

// on_a11y_action(Increment | Decrement), which is what the arrows carry: the
// value moves by the slider's own step and stops at its limits.
static void TheArrowsStepByTheStep() {
    SliderState s = TrackState();
    SliderSetStep(&s, 5.f);
    SliderSetValue(&s, SliderSingle(50.f));

    utassert(SliderStepBy(&s, 1, false));
    utassertnear(s.value.End(), 55.f);
    utassert(SliderStepBy(&s, -1, false));
    utassertnear(s.value.End(), 50.f);
    // The percentage follows, which is what the thumb is drawn from.
    utassertnear(s.pctHi, 0.5f);

    // A slider with no step of its own moves by a hundredth of its range.
    SliderState pct = TrackState();
    SliderSetStep(&pct, 0.f);
    SliderSetValue(&pct, SliderSingle(50.f));
    utassert(SliderStepBy(&pct, 1, false));
    utassertnear(pct.value.End(), 51.f);

    // The limits hold, and a step that cannot move answers false.
    SliderSetValue(&s, SliderSingle(100.f));
    utassert(!SliderStepBy(&s, 1, false));
    utassertnear(s.value.End(), 100.f);
    SliderSetValue(&s, SliderSingle(0.f));
    utassert(!SliderStepBy(&s, -1, false));

    // A range's ends never cross: the low end stops at the high one.
    SliderState r = TrackState();
    SliderSetStep(&r, 10.f);
    SliderSetValue(&r, SliderRange(30.f, 40.f));
    utassert(SliderStepBy(&r, 1, true));
    utassertnear(r.value.Start(), 40.f);
    utassert(!SliderStepBy(&r, 1, true));
    utassert(SliderStepBy(&r, 1, false));
    utassertnear(r.value.End(), 50.f);
}

void TestSlider() {
    TestSuite("slider");
    ValueConversionsAndClampingArePreserved();
    LinearStateKeepsPercentageAndRangeOrdering();
    LogarithmicStateKeepsMapping();
    LogarithmicLimitsAreNudgedInsteadOfPanicking();
    PositionUpdatesTheValueAlongTheAxis();
    PositionIsClampedToTheTrack();
    ValueSnapsToTheStep();
    RangeEndsKeepTheirOrder();
    APressTakesTheNearerHalfOfARange();
    ReleaseOnlyFiresAfterAPress();
    TheArrowsStepByTheStep();
}
