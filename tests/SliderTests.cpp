/* Ported from crates/base/src/slider.rs, mod tests.
 *
 * The fourth Rust case, `legacy_logarithmic_validation_is_preserved`, asserts
 * a panic when a logarithmic slider is built with min <= 0. There are no
 * exceptions here and SliderSetScale nudges the limits instead, so that case
 * is replaced by one that checks the nudge. */

#include "Test.h"

#include <math.h>

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

// slider.rs min_can_be_set_above_the_default_max_before_max.
// .min(150) while max is still the default 100, then .max(200).
static void MinCanBeSetAboveTheDefaultMaxBeforeMax() {
    SliderState single = {};
    SliderSetLimits(&single, 150.f, single.max);
    SliderSetLimits(&single, 150.f, 200.f);
    SliderSetValue(&single, SliderSingle(175.f));
    utassert(!single.value.range);
    utassertnear(single.value.End(), 175.f);
    utassertnear(single.pctLo, 0.f);
    utassertnear(single.pctHi, 0.5f);

    SliderState range = {};
    SliderSetValue(&range, SliderRange(160.f, 180.f));
    SliderSetLimits(&range, 150.f, range.max);
    SliderSetLimits(&range, 150.f, 200.f);
    utassertnear(range.pctLo, 0.2f);
    utassertnear(range.pctHi, 0.6f);
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

// dragging_snaps_from_min. Steps count from min, and a step that is not a
// positive finite number leaves the value continuous.
static void DraggingSnapsFromMin() {
    utassertnear(SliderSnapToStep(9.4f, 3.f, 100.f, 5.f), 8.f);
    utassertnear(SliderSnapToStep(11.f, 3.f, 100.f, 5.f), 13.f);
    utassertnear(SliderSnapToStep(99.f, 3.f, 100.f, 5.f), 98.f);
    utassertnear(SliderSnapToStep(100.f, 0.f, 99.f, 10.f), 99.f);
    utassertnear(SliderSnapToStep(42.5f, 0.f, 100.f, 0.f), 42.5f);
    utassertnear(SliderSnapToStep(42.5f, 0.f, 100.f, -5.f), 42.5f);
    utassertnear(SliderSnapToStep(42.5f, 0.f, 100.f, NAN), 42.5f);
    utassertnear(SliderSnapToStep(42.5f, 0.f, 100.f, INFINITY), 42.5f);
}

// a_drag_snaps_to_steps_counted_from_min. 9 sits between 8 and 13, not 10.
static void ADragSnapsToStepsCountedFromMin() {
    SliderState s = SliderStateNew(3.f, 103.f, SliderSingle(0.f), 5.f);
    SliderSetBounds(&s, {0, 0, 100, 10});
    SliderUpdateByPosition(&s, Axis::Horizontal, {6, 0}, false);
    utassertnear(s.value.End(), 8.f);
}

// a_dropped_thumb_stands_at_its_value. The thumb percentage follows the
// snapped value, not the pointer.
static void ADroppedThumbStandsAtItsValue() {
    SliderState s = SliderStateNew(0.f, 100.f, SliderSingle(0.f), 25.f);
    SliderSetBounds(&s, {0, 0, 100, 10});
    SliderUpdateByPosition(&s, Axis::Horizontal, {30, 0}, false);
    utassertnear(s.value.End(), 25.f);
    utassertnear(s.pctLo, 0.f);
    utassertnear(s.pctHi, 0.25f);

    SliderSetValue(&s, SliderRange(25.f, 75.f));
    SliderUpdateByPosition(&s, Axis::Horizontal, {40, 0}, true);
    SliderUpdateByPosition(&s, Axis::Horizontal, {60, 0}, false);
    utassertnear(s.value.Start(), 50.f);
    utassertnear(s.value.End(), 50.f);
    utassertnear(s.pctLo, 0.5f);
    utassertnear(s.pctHi, 0.5f);
}

// a_zero_step_drags_continuously. 0, negative, NaN and infinity stay at 30.
static void AZeroStepDragsContinuously() {
    float steps[4] = {0.f, -5.f, NAN, INFINITY};
    for (int i = 0; i < 4; i++) {
        SliderState s = SliderStateNew(0.f, 100.f, SliderSingle(0.f), steps[i]);
        SliderSetBounds(&s, {0, 0, 100, 10});
        SliderUpdateByPosition(&s, Axis::Horizontal, {30, 0}, false);
        utassertnear(s.value.End(), 30.f);
        utassertnear(s.pctHi, 0.3f);
    }
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

namespace {
struct SliderEventCounter {
    int changes = 0;
    int releases = 0;

    static void OnEvent(SliderEventCounter* self, Ctx*, const SliderEvent* ev) {
        if (ev->kind == SliderEventKind::Change) {
            self->changes++;
        } else {
            self->releases++;
        }
    }
};
} // namespace

// crates/kit/tests/disclosure.rs: slider_touch_drag_moves_the_actual_thumb,
// and the touch half of disabled_slider_ignores_pointer_changes. A finger
// drag arrives as TouchDrag Started/Moved/Ended; one that starts on the
// track moves the thumb both ways and ends with a Release, and a drag on a
// disabled slider (which binds no state) leaves it alone. The host's pan
// on a track is claimed as a TouchDrag rather than a scroll.
static void ATouchDragOnTheTrackMovesTheThumb() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Entity<SliderEventCounter> counter =
        EntityNewState<SliderEventCounter>(&app);
    SliderState s = TrackState();
    SliderSetValue(&s, SliderSingle(20.f));
    s.onChange = ListenTo(counter, &SliderEventCounter::OnEvent);
    HitRect track = {};
    track.id = 7;
    track.bounds = {0, 0, 100, 20};
    track.slider = &s;
    VecAppend(win->paint.hits, track);

    auto drag = [&](TouchPhase phase, Point start, Point at) {
        PlatformInput in = InputTouchDrag(phase, start, at);
        WindowDispatchInput(win, &in);
    };
    Point start = {20, 10};
    drag(TouchPhase::Started, start, start);
    drag(TouchPhase::Moved, start, {70, 10});
    utassertnear(s.value.End(), 70.f);
    drag(TouchPhase::Moved, start, {40, 10});
    utassertnear(s.value.End(), 40.f);
    drag(TouchPhase::Ended, start, {40, 10});
    utassert(counter.Get(&app)->changes >= 2);
    utassert(counter.Get(&app)->releases == 1);
    utassert(!s.dragging && win->touchSlider == nullptr);

    // A range moves the thumb nearer where the touch started.
    SliderSetValue(&s, SliderRange(20.f, 60.f));
    drag(TouchPhase::Started, {25, 10}, {25, 10});
    drag(TouchPhase::Moved, {25, 10}, {10, 10});
    drag(TouchPhase::Ended, {25, 10}, {10, 10});
    utassertnear(s.value.Start(), 10.f);
    utassertnear(s.value.End(), 60.f);

    // The host claims a pan that starts on the track instead of scrolling.
    SliderSetValue(&s, SliderSingle(20.f));
    WindowTouchBegin(win, 20, 10);
    WindowTouchMove(win, 80, 10);
    utassert(win->touchHost == TouchHostKind::SliderDrag);
    utassertnear(s.value.End(), 80.f);
    WindowTouchEnd(win, 80, 10);
    utassert(win->touchSlider == nullptr);

    // Disabled: the track binds no state, so nothing claims the drag.
    VecClear(win->paint.hits);
    HitRect disabled = track;
    disabled.slider = nullptr;
    VecAppend(win->paint.hits, disabled);
    SliderSetValue(&s, SliderSingle(20.f));
    drag(TouchPhase::Started, start, start);
    drag(TouchPhase::Moved, start, {70, 10});
    drag(TouchPhase::Ended, start, {70, 10});
    utassertnear(s.value.End(), 20.f);

    VecReset(win->paint.hits);
    delete win;
    EntityDropAll(&app);
}

// slider.rs a_disabled_thumb_marks_its_node_disabled. The thumb is given
// Role::Slider the way the test does, then disabled false and true.
// false leaves the flag unset; true marks the node disabled.
static void ADisabledThumbMarksItsNodeDisabled() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    SliderState state = SliderStateNew(0, 100, SliderSingle(0), 1);
    for (int pass = 0; pass < 2; pass++) {
        bool disabled = pass == 1;
        El* thumb = SliderThumb::New(&cx, &state)
                        ->Disabled(disabled)
                        ->IntoEl()
                        ->Role(AccessibilityRole::Slider);
        Vec<AccessibilityNode> nodes;
        AccessibilityCollect(thumb, &nodes);
        const AccessibilityNode* node = nullptr;
        for (int i = 0; i < len(nodes); i++) {
            if (nodes[i].info.role == AccessibilityRole::Slider) {
                node = &nodes[i];
                break;
            }
        }
        utassert(node);
        utassert(node && (node->info.disabled != 0) == disabled);
        VecReset(nodes);
    }
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

// slider.rs a_slider_takes_focus_on_its_own_node. track_focus binds the
// handle to the slider node, and focusing it marks that node focused.
static void ASliderTakesFocusOnItsOwnNode() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {&app, win, a, {}};
    SliderState state = SliderStateNew(0, 100, SliderSingle(40), 1);
    FocusHandle focus = FocusHandleNew(&cx);
    El* slider = Slider::New(&cx, &state)->TrackFocus(focus);
    FocusHandleFocus(win, focus);
    Vec<AccessibilityNode> nodes;
    AccessibilityCollect(slider, &nodes);
    const AccessibilityNode* node = nullptr;
    for (int i = 0; i < len(nodes); i++) {
        if (nodes[i].info.role == AccessibilityRole::Slider) {
            node = &nodes[i];
            break;
        }
    }
    utassert(node && node->focusId == focus.id);
    utassert(node && node->focusId == win->focusId);
    utassert(FocusHandleIsFocused(win, focus));
    VecReset(nodes);
    WindowKeyedFree(win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
    AppGlobalClear(&app);
}

void TestSlider() {
    TestSuite("slider");
    ValueConversionsAndClampingArePreserved();
    LinearStateKeepsPercentageAndRangeOrdering();
    MinCanBeSetAboveTheDefaultMaxBeforeMax();
    LogarithmicStateKeepsMapping();
    LogarithmicLimitsAreNudgedInsteadOfPanicking();
    PositionUpdatesTheValueAlongTheAxis();
    PositionIsClampedToTheTrack();
    DraggingSnapsFromMin();
    ADragSnapsToStepsCountedFromMin();
    ADroppedThumbStandsAtItsValue();
    AZeroStepDragsContinuously();
    ValueSnapsToTheStep();
    RangeEndsKeepTheirOrder();
    APressTakesTheNearerHalfOfARange();
    ReleaseOnlyFiresAfterAPress();
    TheArrowsStepByTheStep();
    ATouchDragOnTheTrackMovesTheThumb();
    ADisabledThumbMarksItsNodeDisabled();
    ASliderTakesFocusOnItsOwnNode();
}
