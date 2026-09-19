/* Ported from crates/base/src/animation.rs and the tests in motion.rs.
 *
 * Rust drives its transition through a test window and reads the value its
 * view sampled; the rule here is a function over the state and the clock, so
 * these drive it directly — same cases, same numbers: a zero duration arrives
 * at once, a changed target moves over time, a reversal carries on from where
 * it had got to rather than jumping, a delay holds the old value, a finished
 * transition stops asking for frames, and reduced motion adopts the target
 * without asking for one at all. */

#include "Test.h"

// The easing presets. ease_out_cubic is the default a Motion starts with.
static void TheEasingsAreTheCurvesRustNames() {
    utassertnear(EaseLinear(0.f), 0.f);
    utassertnear(EaseLinear(0.25f), 0.25f);
    utassertnear(EaseLinear(1.f), 1.f);

    // 1 - (1 - t)^3: past halfway before it is halfway through.
    utassertnear(EaseOutCubic(0.f), 0.f);
    utassertnear(EaseOutCubic(0.5f), 0.875f);
    utassertnear(EaseOutCubic(1.f), 1.f);

    // t^3: the mirror of it.
    utassertnear(EaseInCubic(0.5f), 0.125f);
    utassertnear(EaseInCubic(1.f), 1.f);

    // Slow at both ends, and exactly halfway at halfway.
    utassertnear(EaseInOutCubic(0.25f), 0.0625f);
    utassertnear(EaseInOutCubic(0.5f), 0.5f);
    utassertnear(EaseInOutCubic(0.75f), 0.9375f);

    // Every one of them clamps rather than running off the end of the curve.
    utassertnear(EaseOutCubic(-1.f), 0.f);
    utassertnear(EaseInCubic(2.f), 1.f);
    utassertnear(EaseInOutCubic(2.f), 1.f);
}

// GPUI's own easings, which the looping components are written against.
static void TheLoopingEasingsAreGpuisOwn() {
    utassertnear(EaseQuadratic(0.5f), 0.25f);
    // ease_in_out: quadratic at both ends, exactly halfway at halfway.
    utassertnear(EaseInOutQuad(0.25f), 0.125f);
    utassertnear(EaseInOutQuad(0.5f), 0.5f);
    utassertnear(EaseInOutQuad(0.75f), 0.875f);
    utassertnear(EaseOutQuint(0.5f), 1.f - 0.03125f);

    // bounce: the curve forwards over the first half and back over the
    // second, so it ends where it started and peaks in the middle.
    utassertnear(EaseBounce(EaseLinear, 0.f), 0.f);
    utassertnear(EaseBounce(EaseLinear, 0.25f), 0.5f);
    utassertnear(EaseBounce(EaseLinear, 0.5f), 1.f);
    utassertnear(EaseBounce(EaseLinear, 0.75f), 0.5f);
    utassertnear(EaseBounce(EaseLinear, 1.f), 0.f);
    // The skeleton's, which is that pair over ease_in_out.
    utassertnear(EaseBounceInOut(0.5f), EaseInOutQuad(1.f));
    utassertnear(EaseBounceInOut(0.25f), EaseInOutQuad(0.5f));
    // 1 - delta * 0.5 is what the skeleton makes of it: never below half.
    utassertnear(1.f - EaseBounceInOut(0.5f) * 0.5f, 0.5f);
    utassertnear(1.f - EaseBounceInOut(0.f) * 0.5f, 1.f);
}

// cubic_bezier: the y of the curve, with both ends pinned.
static void TheBezierRunsFromNothingToEverything() {
    utassertnear(CubicBezier(0.32f, 0.72f, 0.f, 1.f, 0.f), 0.f);
    utassertnear(CubicBezier(0.32f, 0.72f, 0.f, 1.f, 1.f), 1.f);
    // The dialog's curve, which is most of the way there at halfway.
    float mid = CubicBezier(1.f / 3, 0.72f, 2.f / 3, 1.f, 0.5f);
    utassert(mid > 0.5f && mid < 1.f);
    // A curve whose control points sit on the diagonal is the diagonal.
    utassertnear(CubicBezier(1.f / 3, 1.f / 3, 2.f / 3, 2.f / 3, 0.5f), 0.5f);
}

// It is CSS's cubic-bezier: `x(s) = t` is solved for the curve parameter
// before y is sampled. These are animation.rs's own cases — the published
// values of the CSS `ease` curve, and Chromium's own expectations for
// (0.25, 0, 0.75, 1) out of cubic_bezier_unittest.cc.
static bool BezierNear(float got, float want, float eps) {
    float d = got - want;
    return (d < 0 ? -d : d) < eps;
}

static void TheBezierMatchesWhatAnEngineSays() {
    struct Sample {
        float t;
        float y;
    };
    const Sample ease[] = {
        {0.f, 0.f}, {0.2f, 0.295f}, {0.5f, 0.802f}, {0.8f, 0.976f}, {1.f, 1.f}};
    for (const Sample& s : ease) {
        utassert(
            BezierNear(CubicBezier(0.25f, 0.1f, 0.25f, 1.f, s.t), s.y, 1e-3f));
    }

    const Sample chromium[] = {
        {0.05f, 0.01136f}, {0.1f, 0.03978f},  {0.15f, 0.07978f},
        {0.2f, 0.12803f},  {0.25f, 0.18235f}, {0.3f, 0.24115f},
        {0.35f, 0.30323f}, {0.4f, 0.36761f},  {0.45f, 0.43345f},
        {0.5f, 0.5f},      {0.6f, 0.63238f},  {0.65f, 0.69676f},
        {0.7f, 0.75884f},  {0.75f, 0.81764f}, {0.8f, 0.87196f},
        {0.85f, 0.92021f}, {0.9f, 0.96021f},  {0.95f, 0.98863f},
    };
    for (const Sample& s : chromium) {
        utassert(
            BezierNear(CubicBezier(0.25f, 0.f, 0.75f, 1.f, s.t), s.y, 3e-4f));
    }
}

// x1 = 1/3, x2 = 2/3 collapse the x solve to the identity, so the answer is
// the plain y polynomial. The dialog leans on that to keep the trajectory it
// was tuned with.
static void ThirdsMapTimeIdentically() {
    for (int step = 0; step <= 100; step++) {
        float t = (float)step / 100.f;
        float oneT = 1.f - t;
        float want =
            3.f * 0.72f * oneT * oneT * t + 3.f * oneT * t * t + t * t * t;
        utassert(BezierNear(CubicBezier(1.f / 3, 0.72f, 2.f / 3, 1.f, t), want,
                            1e-4f));
    }
}

static void TheLerpsAreTheThreeRustImplements() {
    utassertnear(Lerp(0.f, 10.f, 0.5f), 5.f);
    utassertnear(Lerp(10.f, 0.f, 0.25f), 7.5f);
    Point p = Lerp(Point{0, 100}, Point{50, 0}, 0.5f);
    utassertnear(p.x, 25.f);
    utassertnear(p.y, 50.f);
    // The colour walks in HSL, the way `impl Lerp for Hsla` does: all four
    // channels straight, so halfway from black to a saturated orange is half
    // of each of that orange's — hue included, since black's is nothing.
    // ColorTests has the rest of the rule.
    Hsla to = HslaFromRgba(Rgb(255, 100, 50));
    Rgba c = Lerp(Rgb(0, 0, 0), Rgb(255, 100, 50), 0.5f);
    Hsla mid = HslaFromRgba(c);
    utassertnear(mid.h, to.h * 0.5f);
    utassertnear(mid.l, to.l * 0.5f);
    utassert(c.a == 255);
    // The far end is the colour itself, which a round trip through HSL would
    // miss by a byte.
    Rgba end = Lerp(Rgb(0, 0, 0), Rgb(255, 255, 255), 1.f);
    utassert(end.r == 255 && end.g == 255 && end.b == 255);
}

static void EffectTransitionAppliesEveryUpstreamEffect() {
    App app;
    Window* win = new Window();
    Arena* arena = ArenaNew();
    win->app = &app;
    Ctx cx = {&app, win, arena, {}};

    win->frameNow = 10.0;
    El* first = Div(arena);
    Transition::New(&cx, 100)
        ->Ease(EaseLinear)
        ->SlideY(-4, 0)
        ->SlideX(8, 0)
        ->Fade(0, 1)
        ->Width(20, 40)
        ->Height(10, 30)
        ->Apply(first, StrL("entrance"));
    utassertnear(first->style.absTop, -4);
    utassertnear(first->style.absLeft, 8);
    utassertnear(first->style.opacity, 0);
    utassertnear(first->style.width, 20);
    utassertnear(first->style.height, 10);

    // The element tree and builder are new next frame, while the keyed
    // animation state survives on the window exactly as with_animation does.
    win->frameNow = 10.05;
    El* half = Div(arena);
    EffectTransition::New(&cx, 100)
        ->Ease(EaseLinear)
        ->SlideY(-4, 0)
        ->SlideX(8, 0)
        ->Fade(0, 1)
        ->Width(20, 40)
        ->Height(10, 30)
        ->Apply(half, StrL("entrance"));
    utassertnear(half->style.absTop, -2);
    utassertnear(half->style.absLeft, 4);
    utassertnear(half->style.opacity, 0.5f);
    utassertnear(half->style.width, 30);
    utassertnear(half->style.height, 20);

    // The SmallVec upstream can grow; the arena-backed port does too.
    EffectTransition* many = EffectTransition::New(&cx, 100);
    for (int i = 0; i < 80; i++) {
        many->SlideX((float)i, (float)i + 1);
    }
    El* grown = many->Apply(Div(arena), StrL("many-effects"));
    utassertnear(grown->style.absLeft, 79);

    delete win;
    ArenaDelete(arena);
}

// Transition::progress and ::sample.
static void ProgressWaitsOutTheDelayAndStopsAtTheEnd() {
    Motion m = MotionNew(100);
    utassertnear(MotionProgress(m, 0), 0.f);
    utassertnear(MotionProgress(m, 50), 0.5f);
    utassertnear(MotionProgress(m, 100), 1.f);
    utassertnear(MotionProgress(m, 5000), 1.f);

    m.delayMs = 50;
    utassertnear(MotionProgress(m, 50), 0.f);
    utassertnear(MotionProgress(m, 100), 0.5f);
    utassertnear(MotionProgress(m, 150), 1.f);

    // A duration of zero is over before it starts: the delay resolves to an
    // active elapsed of zero and `duration.is_zero()` is checked next, so
    // the answer is 1 at any elapsed at all. Nothing reaches it in practice —
    // a zero-duration transition adopts its target outright — but this is
    // where Rust's order shows.
    utassertnear(MotionProgress(MotionNew(0), 0), 1.f);
    utassertnear(MotionProgress(MotionNew(0), 1), 1.f);

    m = MotionNew(100);
    m.easing = Easing::Linear();
    utassertnear(MotionSample(m, 0.25f), 0.25f);
    m.easing = Easing::Custom(EaseOutCubic);
    utassertnear(MotionSample(m, 0.5f), 0.875f);
}

// a_zero_duration_target_change_is_immediate.
static void AZeroDurationTargetChangeIsImmediate() {
    MotionState<float> st;
    Motion m = MotionNew(0);
    MotionStep<float> first = MotionAdvance(&st, 0.f, m, 0, false);
    utassertnear(first.value, 0.f);
    MotionStep<float> next = MotionAdvance(&st, 1.f, m, 0, false);
    utassertnear(next.value, 1.f);
    utassert(!next.running);
}

// a_changed_target_transitions_over_time.
static void AChangedTargetTransitionsOverTime() {
    MotionState<float> st;
    Motion m = MotionNew(100);
    m.easing = Easing::Linear();
    // The first value is adopted rather than transitioned to.
    utassertnear(MotionAdvance(&st, 0.f, m, 0, false).value, 0.f);
    // The change itself reports the value it is leaving.
    MotionStep<float> moved = MotionAdvance(&st, 10.f, m, 0, false);
    utassertnear(moved.value, 0.f);
    utassert(moved.running);
    // Half the duration later, half the way there.
    MotionStep<float> half = MotionAdvance(&st, 10.f, m, 0.05, false);
    utassertnear(half.value, 5.f);
    utassert(half.running);
}

// reversing_uses_the_current_sample_without_jumping.
static void ReversingCarriesOnFromWhereItGotTo() {
    MotionState<float> st;
    Motion m = MotionNew(100);
    m.easing = Easing::Linear();
    MotionAdvance(&st, 0.f, m, 0, false);
    MotionAdvance(&st, 10.f, m, 0, false);
    // Halfway there, and told to go back: it leaves from 5, not from 10.
    utassertnear(MotionAdvance(&st, 0.f, m, 0.05, false).value, 5.f);
    // And the return is *shortened* to the half of the curve it had actually
    // travelled — the reversing factor — so 25 ms later it is halfway back
    // rather than a quarter of the way. That is
    // reversing_uses_the_current_sample_and_shortens_the_return upstream.
    utassertnear(MotionAdvance(&st, 0.f, m, 0.075, false).value, 2.5f);
}

// delay_holds_the_previous_value_before_interpolation.
static void ADelayHoldsThePreviousValue() {
    MotionState<float> st;
    Motion m = MotionNew(100);
    m.delayMs = 50;
    m.easing = Easing::Linear();
    MotionAdvance(&st, 0.f, m, 0, false);
    utassertnear(MotionAdvance(&st, 10.f, m, 0, false).value, 0.f);
    // Still inside the delay.
    utassertnear(MotionAdvance(&st, 10.f, m, 0.05, false).value, 0.f);
    // Half the duration past it.
    utassertnear(MotionAdvance(&st, 10.f, m, 0.1, false).value, 5.f);
}

// a_completed_transition_stops_requesting_frames.
static void AFinishedTransitionStopsAskingForFrames() {
    MotionState<float> st;
    Motion m = MotionNew(100);
    m.easing = Easing::Linear();
    MotionAdvance(&st, 0.f, m, 0, false);
    utassert(MotionAdvance(&st, 1.f, m, 0, false).running);
    MotionStep<float> done = MotionAdvance(&st, 1.f, m, 0.1, false);
    utassertnear(done.value, 1.f);
    utassert(!done.running);
    // And it keeps not asking.
    utassert(!MotionAdvance(&st, 1.f, m, 0.2, false).running);
    // A value that never moved does not ask either.
    MotionState<float> still;
    MotionAdvance(&still, 3.f, m, 0, false);
    utassert(!MotionAdvance(&still, 3.f, m, 1.0, false).running);
}

// reduced_motion_adopts_the_target_without_requesting_a_frame.
static void ReducedMotionTakesTheTargetOutright() {
    MotionState<float> st;
    Motion m = MotionNew(100);
    m.easing = Easing::Linear();
    MotionAdvance(&st, 0.f, m, 0, true);
    MotionStep<float> step = MotionAdvance(&st, 1.f, m, 0, true);
    utassertnear(step.value, 1.f);
    utassert(!step.running);
    // Turned on partway through a transition, the next frame arrives rather
    // than finishing the curve.
    MotionState<float> mid;
    MotionAdvance(&mid, 0.f, m, 0, false);
    MotionAdvance(&mid, 10.f, m, 0, false);
    utassertnear(MotionAdvance(&mid, 10.f, m, 0.05, true).value, 10.f);
}

// The other two types go the same way round; a color is what a fill fades
// through, and a point what something slides along.
static void TheSameRuleCarriesAPointAndAColor() {
    MotionState<Point> pt;
    Motion m = MotionNew(100);
    m.easing = Easing::Linear();
    MotionAdvance(&pt, Point{0, 0}, m, 0, false);
    MotionAdvance(&pt, Point{100, 40}, m, 0, false);
    MotionStep<Point> half = MotionAdvance(&pt, Point{100, 40}, m, 0.05, false);
    utassertnear(half.value.x, 50.f);
    utassertnear(half.value.y, 20.f);

    MotionState<Rgba> col;
    MotionAdvance(&col, Rgb(0, 0, 0), m, 0, false);
    MotionAdvance(&col, Rgb(100, 100, 100), m, 0, false);
    MotionStep<Rgba> mid =
        MotionAdvance(&col, Rgb(100, 100, 100), m, 0.05, false);
    utassert(mid.value.r == 50);
    utassert(mid.running);
}

// The id is the element and the channel together, so one element can move two
// values at once without them sharing a slot.
static void AChannelKeepsTwoValuesOfOneElementApart() {
    utassert(MotionId(StrL("terms")) == MotionId(StrL("terms")));
    utassert(MotionId(StrL("terms"), StrL("fill")) !=
             MotionId(StrL("terms"), StrL("mark-opacity")));
    utassert(MotionId(StrL("terms"), StrL("fill")) !=
             MotionId(StrL("other"), StrL("fill")));
}

static void SourceNamedValueTransitionContractIsAvailable() {
    // This exercises the keyed transition path, not the host accessibility
    // preference. Reduced motion itself is covered by the pure state test.
    MotionSetReduced(false);
    motion::Transition linear =
        motion::Transition::New(100).Delay(50).Ease(EaseLinear);
    utassertnear(linear.durationMs, 100.f);
    utassertnear(linear.delayMs, 50.f);
    utassertnear(MotionSample(linear, 0.25f), 0.25f);

    motion::TransitionId fill(StrL("terms"), StrL("fill"));
    motion::TransitionId mark(StrL("terms"), StrL("mark-opacity"));
    utassert(fill != mark);
    utassert(fill == motion::TransitionId(StrL("terms"), StrL("fill")));

    utassertnear(motion::Interpolate<float>::Between(2.f, 10.f, 0.25f), 4.f);

    App app;
    Window* win = new Window();
    Arena* arena = ArenaNew();
    win->app = &app;
    win->frameNow = 1.0;
    Ctx cx = {&app, win, arena, {}};
    motion::TransitionId value(StrL("source-transition"));
    utassertnear(motion::transition(&cx, value, 0.f, linear), 0.f);
    utassertnear(motion::transition(&cx, value, 10.f, linear), 0.f);
    win->frameNow = 1.1;
    utassertnear(motion::transition(&cx, value, 10.f, linear), 5.f);

    delete win;
    ArenaDelete(arena);
}

// Style::opacity, which is what a fade is made of: GPUI multiplies every
// colour a primitive paints by the opacity in force, and nested opacities
// multiply, so a subtree fades as one thing rather than each box separately.
static void OpacityMultipliesEveryColourItPaints() {
    PaintCtx ctx;
    utassert(ctx.opacity == 1.f);
    // Untouched at 1, whatever the colour's own alpha is.
    Rgba c = Rgba8(10, 20, 30, 200);
    Rgba same = PaintFade(&ctx, c);
    utassert(same.a == 200 && same.r == 10);

    // Half of a colour's alpha, with the channels left alone — the same thing
    // `Hsla::opacity` does to a colour, done to the primitive instead.
    ctx.opacity = 0.5f;
    Rgba half = PaintFade(&ctx, c);
    utassert(half.a == 100);
    utassert(half.r == 10 && half.g == 20 && half.b == 30);

    // A colour that was already translucent fades from where it was.
    Rgba faint = PaintFade(&ctx, Rgba8(0, 0, 0, 20));
    utassert(faint.a == 10);

    // Nothing at zero, everything back at one.
    ctx.opacity = 0.f;
    utassert(PaintFade(&ctx, c).a == 0);
    ctx.opacity = 1.f;
    utassert(PaintFade(&ctx, c).a == 200);

    // El::Opacity clamps rather than letting a stray value through: a
    // transition that overshoots cannot make something more opaque than it is.
    Arena* a = ArenaNew();
    El* e = Div(a)->Opacity(1.5f);
    utassertnear(e->style.opacity, 1.f);
    e->Opacity(-0.2f);
    utassertnear(e->style.opacity, 0.f);
    e->Opacity(0.25f);
    utassertnear(e->style.opacity, 0.25f);
    ArenaDelete(a);
}

// ─── springs ──────────────────────────────────────────────────────────────

// Run a spring forward in 16 ms frames, answering where it ends up.
static float SpringRun(SpringState* st, float target, const Spring& s,
                       double* now, int frames) {
    float v = 0;
    for (int i = 0; i < frames; i++) {
        *now += 0.016;
        v = SpringAdvance(st, target, s, *now, false).value;
    }
    return v;
}

static void ASpringArrivesAndStops() {
    Spring s = SpringNew(200);
    s.epsilon = 0.001f;
    SpringState st;
    double now = 0;
    // The first target is adopted outright, as a transition's is.
    SpringStep first = SpringAdvance(&st, 0.f, s, now, false);
    utassert(first.value == 0.f && !first.running);

    // It travels, without passing the target: critically damped.
    float mid = SpringRun(&st, 1.f, s, &now, 6);
    utassert(mid > 0.f && mid < 1.f);
    float v = SpringRun(&st, 1.f, s, &now, 60);
    utassertnear(v, 1.f);
    // And once it is there it asks for nothing more.
    now += 0.016;
    utassert(!SpringAdvance(&st, 1.f, s, now, false).running);
}

// The whole point of a spring: a target that changes mid-flight is turned
// around rather than restarted, so the value keeps going the way it was for
// a moment before it comes back.
static void ASpringCarriesItsVelocityThroughAReversal() {
    Spring s = SpringNew(200);
    SpringState st;
    double now = 0;
    SpringAdvance(&st, 0.f, s, now, false);
    SpringRun(&st, 1.f, s, &now, 5);
    float atTurn = st.position;
    utassert(atTurn > 0.f && atTurn < 1.f);
    utassert(st.velocity > 0.f);

    // Back to 0 from here. The next frame is still moving the old way.
    now += 0.016;
    float after = SpringAdvance(&st, 0.f, s, now, false).value;
    utassert(after > atTurn);
    utassert(st.velocity > 0.f);
    // It decelerates, turns, and gets back.
    float back = SpringRun(&st, 0.f, s, &now, 60);
    utassertnear(back, 0.f);
}

static void ASpringUnderOneOvershoots() {
    Spring s = SpringNew(200);
    s.damping = 0.5f;
    SpringState st;
    double now = 0;
    SpringAdvance(&st, 0.f, s, now, false);
    float peak = 0;
    for (int i = 0; i < 30; i++) {
        now += 0.016;
        float v = SpringAdvance(&st, 1.f, s, now, false).value;
        if (v > peak) {
            peak = v;
        }
    }
    utassert(peak > 1.f);
    // Critically damped, the same run never passes it.
    Spring critical = SpringNew(200);
    SpringState st2;
    double now2 = 0;
    SpringAdvance(&st2, 0.f, critical, now2, false);
    for (int i = 0; i < 30; i++) {
        now2 += 0.016;
        utassert(SpringAdvance(&st2, 1.f, critical, now2, false).value <= 1.f);
    }
}

static void ASuspendedSpringPinsItselfToTheTarget() {
    Spring s = SpringNew(200);
    SpringState st;
    double now = 0;
    SpringAdvance(&st, 0.f, s, now, false);
    SpringRun(&st, 1.f, s, &now, 3);
    utassert(st.position < 1.f);
    // travel(false) is a value the pointer is already moving: it takes the
    // target on the spot and keeps its state there, so travel resumes from
    // where the drag left it.
    s.travel = false;
    now += 0.016;
    SpringStep step = SpringAdvance(&st, 5.f, s, now, false);
    utassert(step.value == 5.f && !step.running);
    utassert(st.position == 5.f && st.velocity == 0.f);

    // Reduced motion does the same, whatever the travel says.
    s.travel = true;
    now += 0.016;
    utassert(SpringAdvance(&st, 9.f, s, now, true).value == 9.f);
    utassert(st.position == 9.f);
    // A response of zero is the degenerate spring: infinitely stiff.
    Spring instant = SpringNew(0);
    SpringState st3;
    double now3 = 0;
    SpringAdvance(&st3, 0.f, instant, now3, false);
    now3 += 0.016;
    utassert(SpringAdvance(&st3, 1.f, instant, now3, false).value == 1.f);
}

static void ACoarseEpsilonSettlesSooner() {
    Spring fine = SpringNew(200);
    Spring coarse = SpringNew(200);
    coarse.epsilon = 1.f;
    SpringState a;
    SpringState b;
    double now = 0;
    SpringAdvance(&a, 0.f, fine, now, false);
    SpringAdvance(&b, 0.f, coarse, now, false);
    int fineFrames = 0;
    int coarseFrames = 0;
    for (int i = 0; i < 200; i++) {
        now += 0.016;
        if (SpringAdvance(&a, 100.f, fine, now, false).running) {
            fineFrames++;
        }
        if (SpringAdvance(&b, 100.f, coarse, now, false).running) {
            coarseFrames++;
        }
    }
    // Both arrive; the coarse one stops asking for frames first.
    utassertnear(a.position, 100.f);
    utassertnear(b.position, 100.f);
    utassert(coarseFrames < fineFrames);
}

// ─── motion/easing.rs ─────────────────────────────────────────────────────

// css_keyword_easing_matches_published_reference_samples: the four CSS
// keyword curves, against the numbers the specification's examples give.
static void TheCssKeywordEasingsMatchTheirPublishedSamples() {
    struct Case {
        Easing easing;
        float progress;
        float expected;
    };
    const Case cases[] = {
        {Easing::Ease(), 0.2f, 0.295f},    {Easing::Ease(), 0.5f, 0.802f},
        {Easing::Ease(), 0.8f, 0.976f},    {Easing::EaseIn(), 0.2f, 0.062f},
        {Easing::EaseIn(), 0.5f, 0.315f},  {Easing::EaseIn(), 0.8f, 0.692f},
        {Easing::EaseOut(), 0.2f, 0.308f}, {Easing::EaseOut(), 0.5f, 0.685f},
        {Easing::EaseOut(), 0.8f, 0.938f}, {Easing::EaseInOut(), 0.2f, 0.082f},
        {Easing::EaseInOut(), 0.5f, 0.5f}, {Easing::EaseInOut(), 0.8f, 0.918f},
    };
    for (const Case& c : cases) {
        utassert(BezierNear(c.easing.Sample(c.progress), c.expected, 0.002f));
    }
    // Linear is the identity, and a custom curve is whatever it says.
    utassertnear(Easing::Linear().Sample(0.25f), 0.25f);
    utassertnear(Easing::Custom(EaseOutCubic).Sample(0.5f), 0.875f);
    // Every one of them clamps its input rather than running off the curve.
    utassertnear(Easing::Linear().Sample(2.f), 1.f);
    utassertnear(Easing::EaseInOut().Sample(-1.f), 0.f);
}

// step_easing_observes_css_jump_positions.
static void StepEasingObservesTheCssJumpPositions() {
    Easing start = Easing::Steps(4, StepPosition::JumpStart).Unwrap();
    Easing end = Easing::Steps(4, StepPosition::JumpEnd).Unwrap();

    utassertnear(start.Sample(0.f), 0.25f);
    utassertnear(start.Sample(0.24f), 0.25f);
    utassertnear(start.Sample(0.25f), 0.5f);
    utassertnear(end.Sample(0.f), 0.f);
    utassertnear(end.Sample(0.24f), 0.f);
    utassertnear(end.Sample(0.25f), 0.25f);
    utassert(Easing::Steps(0, StepPosition::JumpEnd).IsErr());

    Easing none = Easing::Steps(4, StepPosition::JumpNone).Unwrap();
    Easing both = Easing::Steps(4, StepPosition::JumpBoth).Unwrap();
    utassertnear(none.Sample(0.f), 0.f);
    utassertnear(none.Sample(0.5f), 2.f / 3.f);
    utassertnear(none.Sample(1.f), 1.f);
    utassertnear(both.Sample(0.f), 0.2f);
    utassertnear(both.Sample(1.f), 1.f);
    // One step with nowhere to jump is not a curve.
    utassert(Easing::Steps(1, StepPosition::JumpNone).IsErr());
    utassert(Easing::Steps(1, StepPosition::JumpNone)
                 .UnwrapErr() == EasingError::InvalidStepCount);
}

static float TestNaN() {
    volatile float zero = 0.f;
    return zero / zero;
}

static float TestInf() {
    volatile float zero = 0.f;
    return 1.f / zero;
}

// linear_stops_fill_omitted_positions_before_sampling, and
// bezier_errors_name_the_invalid_control_point.
static void LinearStopsFillTheirOmittedPositions() {
    Arena* a = ArenaNew();
    const LinearStop stops[] = {
        LinearStop::At(0.f, 0.f),
        LinearStop::New(0.2f),
        LinearStop::New(0.8f),
        LinearStop::At(1.f, 1.f),
    };
    Easing easing = Easing::LinearStops(a, stops, 4).Unwrap();
    // The two omitted inputs land a third and two thirds of the way along.
    utassert(BezierNear(easing.Sample(1.f / 3.f), 0.2f, 1e-5f));
    utassert(BezierNear(easing.Sample(0.5f), 0.5f, 1e-5f));

    // Two stops is the minimum.
    const LinearStop tooFew[] = {LinearStop::At(0.f, 0.8f)};
    utassert(Easing::LinearStops(a, tooFew, 1).IsErr());
    // And their input positions have to run forwards: Rust rejects this pair
    // because its inputs are 0.8 then 0.2.
    const LinearStop backwards[] = {LinearStop::At(0.f, 0.8f),
                                    LinearStop::At(1.f, 0.2f)};
    utassert(Easing::LinearStops(a, backwards, 2).IsErr());
    utassert(Easing::LinearStops(a, backwards, 2)
                 .UnwrapErr() == EasingError::InvalidLinearStops);

    // A control point that is not a number names itself.
    EasingResult bad = Easing::CubicBezier(0.2f, TestNaN(), 0.8f, 1.f);
    utassert(bad.IsErr());
    utassert(bad.UnwrapErr() == EasingError::InvalidBezierControlPoint);
    utassert(base::StrEq(Str(EasingErrorMessage(bad.UnwrapErr())),
                         Str("cubic Bézier control points must be finite and "
                             "x must be within 0..=1")));
    // x outside 0..=1 is out too; y is free to overshoot.
    utassert(Easing::CubicBezier(1.2f, 0.f, 0.8f, 1.f).IsErr());
    utassert(Easing::CubicBezier(0.2f, -2.f, 0.8f, 3.f).IsOk());
    ArenaDelete(a);
}

// ─── motion/timing.rs ─────────────────────────────────────────────────────

// negative_delay_starts_inside_the_active_interval.
static void ANegativeDelayStartsInsideTheActiveInterval() {
    Timing timing = Timing::New(100).Delay(SignedDuration::Negative(25));
    TimingSample sample = timing.Sample(0);

    utassert(sample.phase == MotionPhase::Active);
    utassertnear(sample.directedProgress, 0.25f);
    utassert(sample.active);
    utassert(!sample.finished);

    // A positive one holds everything back until it has run out.
    Timing later = Timing::New(100).Delay(50.f);
    utassert(later.Sample(20).phase == MotionPhase::Before);
    utassert(!later.Sample(20).active);
    utassertnear(later.Sample(75).directedProgress, 0.25f);
}

// alternate_direction_reverses_odd_iterations.
static void AlternateDirectionReversesTheOddIterations() {
    Timing timing = Timing::New(100)
                        .Iterations(IterationCount::Finite(2))
                        .Direction(PlaybackDirection::Alternate)
                        .Ease(Easing::Linear());

    TimingSample first = timing.Sample(25);
    TimingSample second = timing.Sample(125);
    TimingSample finished = timing.Sample(200);

    utassert(first.iteration == 0);
    utassertnear(first.directedProgress, 0.25f);
    utassert(second.iteration == 1);
    utassertnear(second.directedProgress, 0.75f);
    utassert(finished.phase == MotionPhase::After);
    utassertnear(finished.directedProgress, 0.f);
    utassert(finished.finished);

    // Infinite playback never finishes and keeps counting its iterations.
    Timing forever = Timing::New(100).Iterations(IterationCount::Infinite());
    TimingSample late = forever.Sample(1050);
    utassert(late.phase == MotionPhase::Active);
    utassert(late.iteration == 10);
    utassertnear(late.directedProgress, 0.5f);
    // Reverse runs the curve the other way from the first frame.
    Timing back = Timing::New(100).Direction(PlaybackDirection::Reverse);
    utassertnear(back.Sample(25).directedProgress, 0.75f);
    // A zero duration is over before it starts, and so are zero iterations
    // of a real one.
    utassert(Timing::New(0).Sample(0).phase == MotionPhase::After);
    utassert(Timing::New(100)
                 .Iterations(IterationCount::Finite(0))
                 .Sample(0)
                 .phase == MotionPhase::After);
}

// ─── motion/keyframes.rs ──────────────────────────────────────────────────

// keyframes_validate_offsets_and_sample_each_segments_easing.
static void KeyframesValidateOffsetsAndSampleTheirSegments() {
    const Keyframe<float> noStart[] = {Keyframe<float>::New(0.2f, 0.f),
                                       Keyframe<float>::New(1.f, 1.f)};
    utassert(Keyframes<float>::TryNew(noStart, 2)
                 .UnwrapErr() == KeyframeError::MissingEndpoint);

    const Keyframe<float> unordered[] = {
        Keyframe<float>::New(0.f, 0.f), Keyframe<float>::New(0.8f, 1.f),
        Keyframe<float>::New(0.7f, 2.f), Keyframe<float>::New(1.f, 3.f)};
    utassert(Keyframes<float>::TryNew(unordered, 4)
                 .UnwrapErr() == KeyframeError::OffsetsNotMonotonic);

    const Keyframe<float> one[] = {Keyframe<float>::New(0.f, 0.f)};
    utassert(Keyframes<float>::TryNew(one, 1)
                 .UnwrapErr() == KeyframeError::TooFewFrames);
    const Keyframe<float> outside[] = {Keyframe<float>::New(0.f, 0.f),
                                       Keyframe<float>::New(1.5f, 1.f)};
    utassert(Keyframes<float>::TryNew(outside, 2)
                 .UnwrapErr() == KeyframeError::OffsetOutOfRange);
    const Keyframe<float> notFinite[] = {Keyframe<float>::New(0.f, 0.f),
                                         Keyframe<float>::New(TestNaN(), 1.f)};
    utassert(Keyframes<float>::TryNew(notFinite, 2)
                 .UnwrapErr() == KeyframeError::OffsetNotFinite);

    // Each segment carries the easing of the frame it starts at.
    const Keyframe<float> frames[] = {
        Keyframe<float>::New(0.f, 0.f)
            .Ease(Easing::Steps(2, StepPosition::JumpEnd).Unwrap()),
        Keyframe<float>::New(0.5f, 10.f).Ease(Easing::Linear()),
        Keyframe<float>::New(1.f, 20.f),
    };
    Keyframes<float> track = Keyframes<float>::TryNew(frames, 3).Unwrap();
    utassertnear(track.Sample(0.2f), 0.f);
    utassertnear(track.Sample(0.3f), 5.f);
    utassertnear(track.Sample(0.75f), 15.f);
    utassertnear(track.Sample(1.f), 20.f);
    utassert(track.Len() == 3);
    utassert(!track.IsEmpty());
}

// discrete_values_switch_only_at_the_requested_progress.
static void DiscreteValuesSwitchOnlyAtTheirSwitchPoint() {
    Discrete<Str> value =
        Discrete<Str>::New(StrL("old"), StrL("new")).SwitchAt(0.75f).Unwrap();
    utassert(base::StrEq(value.Sample(0.749f), Str("old")));
    utassert(base::StrEq(value.Sample(0.75f), Str("new")));
    // Halfway is the default.
    Discrete<int> half = Discrete<int>::New(0, 1);
    utassert(half.Sample(0.49f) == 0);
    utassert(half.Sample(0.5f) == 1);
    utassert(Discrete<int>::New(0, 1).SwitchAt(TestNaN()).IsErr());
    utassert(Discrete<int>::New(0, 1).SwitchAt(1.5f).UnwrapErr() ==
             DiscreteError::InvalidSwitchPoint);
}

// ─── motion/stagger.rs ────────────────────────────────────────────────────

// stagger_origins_produce_stable_delays_without_allocating_a_schedule.
static void StaggerOriginsProduceStableDelays() {
    const float interval = 20.f;
    Stagger first = Stagger::New(interval, StaggerOrigin::FirstOrigin());
    Stagger last = Stagger::New(interval, StaggerOrigin::LastOrigin());
    Stagger center = Stagger::New(interval, StaggerOrigin::CenterOrigin());

    utassertnear(first.Delay(3, 5), 60.f);
    utassertnear(last.Delay(3, 5), 20.f);
    utassertnear(center.Delay(2, 5), 0.f);
    utassertnear(center.Delay(0, 5), 40.f);
    // An empty list has nothing to stagger, and an index past the end is the
    // last one.
    utassertnear(first.Delay(7, 0), 0.f);
    utassertnear(first.Delay(7, 3), 40.f);
    // An explicit origin is where the wave starts, clamped into the list.
    Stagger third = Stagger::New(interval, StaggerOrigin::IndexOrigin(2));
    utassertnear(third.Delay(0, 5), 40.f);
    utassertnear(third.Delay(4, 5), 40.f);
    utassertnear(third.Delay(2, 5), 0.f);
}

// ─── composite interpolation ──────────────────────────────────────────────

// common_gpui_geometry_interpolates_channel_by_channel.
static void GeometryInterpolatesChannelByChannel() {
    Size fromSize = {10, 20};
    Size toSize = {30, 60};
    Size quarter = motion::Interpolate<Size>::Between(fromSize, toSize, 0.25f);
    utassertnear(quarter.w, 15.f);
    utassertnear(quarter.h, 30.f);

    Bounds from = {0, 10, 10, 20};
    Bounds to = {40, 50, 30, 60};
    Bounds mid = motion::Interpolate<Bounds>::Between(from, to, 0.5f);
    utassertnear(mid.x, 20.f);
    utassertnear(mid.y, 30.f);
    utassertnear(mid.w, 20.f);
    utassertnear(mid.h, 40.f);

    MotionTransform target;
    target.translation = {20, 40};
    target.scale = {2.f, 0.5f};
    target.rotationRadians = 3.14159265f;
    target.opacity = 0.f;
    MotionTransform half = motion::Interpolate<MotionTransform>::Between(
        MotionTransform::Identity(), target, 0.5f);
    utassertnear(half.translation.x, 10.f);
    utassertnear(half.translation.y, 20.f);
    utassertnear(half.scale.x, 1.5f);
    utassertnear(half.scale.y, 0.75f);
    utassertnear(half.rotationRadians, 3.14159265f / 2.f);
    utassertnear(half.opacity, 0.5f);
    // The identity is what a default transform is, and equality is by every
    // channel.
    utassert(MotionEq(MotionTransform::Identity(), MotionTransform()));
    utassert(!MotionEq(MotionTransform::Identity(), target));
}

// ─── transition status ────────────────────────────────────────────────────

// status_transition_reports_delay_running_and_finished, and
// negative_delay_samples_a_target_change_inside_its_interval.
static void ATransitionReportsWhereItIs() {
    MotionState<float> st;
    Motion m = MotionNew(100).Delay(20.f);
    MotionAdvance(&st, 0.f, m, 0, false);
    utassert(MotionAdvance(&st, 1.f, m, 0, false)
                 .status == MotionStatus::Delayed);
    utassert(MotionAdvance(&st, 1.f, m, 0.021, false)
                 .status == MotionStatus::Running);
    utassert(MotionAdvance(&st, 1.f, m, 0.121, false)
                 .status == MotionStatus::Finished);
    // A value nothing has moved is Idle rather than Running: there is
    // nothing between its from and its target to be on the way through.
    MotionState<float> still;
    MotionAdvance(&still, 3.f, m, 0, false);
    utassert(MotionAdvance(&still, 3.f, m, 0.001, false)
                 .status == MotionStatus::Idle);

    // A negative delay starts the run partway in, so the very first frame of
    // a target change already shows a quarter of it.
    MotionState<float> early;
    Motion back =
        MotionNew(100).Delay(SignedDuration::Negative(25)).Ease(EaseLinear);
    MotionAdvance(&early, 0.f, back, 0, false);
    MotionStep<float> step = MotionAdvance(&early, 1.f, back, 0, false);
    utassert(step.status == MotionStatus::Running);
    utassertnear(step.value, 0.25f);

    // Reduced motion and a zero duration are both Finished on the spot.
    MotionState<float> instant;
    utassert(MotionAdvance(&instant, 1.f, MotionNew(0), 0, false)
                 .status == MotionStatus::Finished);
    MotionState<float> off;
    utassert(MotionAdvance(&off, 1.f, m, 0, true)
                 .status == MotionStatus::Finished);
}

// motion/sequence.rs: steps hand over at their absolute boundary, including
// when a frame lands after it, and the sequence alone reports Finished.
static SequenceSample<float> SampleSequence(Ctx* cx, Str id, float from,
                                            float first, float second,
                                            float delayMs = 0) {
    Sequence<float> sequence =
        Sequence<float>::New(cx, motion::TransitionId(id), from)
            .WithStep(first, motion::Transition::New(100).Ease(EaseLinear))
            .WithStep(second, motion::Transition::New(100).Delay(delayMs).Ease(
                                  EaseLinear));
    return sequence.Sample(cx);
}

static void SequenceStepsAdvanceAtTheirBoundaries() {
    App app;
    Window* win = new Window();
    Arena* arena = ArenaNew();
    win->app = &app;
    Ctx cx = {&app, win, arena, {}};
    MotionSetReduced(false);

    win->frameNow = 1.0;
    SequenceSample<float> sample =
        SampleSequence(&cx, StrL("ordered"), 0, 10, 20, 50);
    utassertnear(sample.Value(), 0.f);
    utassert(sample.Step() == 0 && sample.Status() == MotionStatus::Running);
    utassert(win->animFrame);

    win->frameNow = 1.05;
    sample = SampleSequence(&cx, StrL("ordered"), 0, 10, 20, 50);
    utassertnear(sample.Value(), 5.f);
    win->frameNow = 1.1;
    sample = SampleSequence(&cx, StrL("ordered"), 0, 10, 20, 50);
    utassertnear(sample.Value(), 10.f);
    utassert(sample.Step() == 1 && sample.Status() == MotionStatus::Delayed);
    win->frameNow = 1.2;
    sample = SampleSequence(&cx, StrL("ordered"), 0, 10, 20, 50);
    utassertnear(sample.Value(), 15.f);
    utassert(sample.Step() == 1 && sample.Status() == MotionStatus::Running);
    win->animFrame = false;
    win->frameNow = 1.25;
    sample = SampleSequence(&cx, StrL("ordered"), 0, 10, 20, 50);
    utassertnear(sample.Value(), 20.f);
    utassert(sample.IsFinished() && !win->animFrame);

    // The 100 ms hand-off happened between frames. The second step is already
    // 75 ms through rather than starting at this late frame.
    win->frameNow = 2.0;
    SampleSequence(&cx, StrL("coarse"), 0, 10, 20);
    win->frameNow = 2.175;
    sample = SampleSequence(&cx, StrL("coarse"), 0, 10, 20);
    utassertnear(sample.Value(), 17.5f);
    utassert(sample.Step() == 1 && sample.Status() == MotionStatus::Running);

    delete win;
    ArenaDelete(arena);
    MotionSetReduced(false);
}

static void SequenceHandlesInstantStepsAndReducedMotion() {
    App app;
    Window* win = new Window();
    Arena* arena = ArenaNew();
    win->app = &app;
    Ctx cx = {&app, win, arena, {}};
    win->frameNow = 3.0;

    Sequence<float> instant =
        Sequence<float>::New(
            &cx, motion::TransitionId(StrL("instant-sequence")), 0.f)
            .WithStep(5.f, motion::Transition::New(0).Ease(EaseLinear))
            .WithStep(6.f, motion::Transition::New(0).Ease(EaseLinear))
            .WithStep(10.f, motion::Transition::New(100).Ease(EaseLinear));
    SequenceSample<float> sample = instant.Sample(&cx);
    utassertnear(sample.Value(), 6.f);
    utassert(sample.Step() == 2 && sample.Status() == MotionStatus::Running);

    MotionSetReduced(true);
    win->animFrame = false;
    sample = SampleSequence(&cx, StrL("reduced-sequence"), 0, 10, 20);
    utassertnear(sample.Value(), 20.f);
    utassert(sample.Step() == 1 && sample.IsFinished() && !win->animFrame);

    delete win;
    ArenaDelete(arena);
    MotionSetReduced(false);
}

// ─── motion/presence.rs ───────────────────────────────────────────────────

// presence_enters_exits_and_only_unmounts_after_exit, and
// presence_reentry_reverses_from_the_exit_sample.
static void PresenceEntersExitsAndUnmountsOnlyAfterItsExit() {
    PresenceState st;
    motion::Transition policy = motion::Transition::New(100).Ease(EaseLinear);

    // A surface that is present on its first frame still enters, from 0.
    PresenceSample entering = PresenceAdvance(&st, true, policy, 0, false);
    utassert(entering.phase == PresencePhase::Entering);
    utassertnear(entering.progress, 0.f);
    utassert(entering.ShouldRender());

    PresenceSample present = PresenceAdvance(&st, true, policy, 0.1, false);
    utassert(present.phase == PresencePhase::Present);
    utassertnear(present.progress, 1.f);
    utassert(present.status == MotionStatus::Finished);

    // Removed: it is still rendered, at full opacity, on its way out.
    PresenceSample exiting = PresenceAdvance(&st, false, policy, 0.1, false);
    utassert(exiting.phase == PresencePhase::Exiting);
    utassertnear(exiting.progress, 1.f);
    utassert(exiting.ShouldRender());

    PresenceSample absent = PresenceAdvance(&st, false, policy, 0.2, false);
    utassert(absent.phase == PresencePhase::Absent);
    utassertnear(absent.progress, 0.f);
    utassert(!absent.ShouldRender());

    // Re-entering partway through the exit reverses from the sample it was
    // showing rather than starting over from nothing.
    PresenceState re;
    PresenceAdvance(&re, true, policy, 0, false);
    PresenceAdvance(&re, true, policy, 0.1, false);
    PresenceAdvance(&re, false, policy, 0.1, false);
    PresenceSample reentering = PresenceAdvance(&re, true, policy, 0.14, false);
    utassert(reentering.phase == PresencePhase::Entering);
    utassertnear(reentering.progress, 0.6f);

    // Reduced motion resolves it outright, in both directions, and so does a
    // zero-duration policy.
    PresenceState quick;
    utassert(PresenceAdvance(&quick, true, policy, 0, true)
                 .phase == PresencePhase::Present);
    utassert(PresenceAdvance(&quick, false, policy, 0, true)
                 .phase == PresencePhase::Absent);
    PresenceState none;
    utassert(PresenceAdvance(&none, true, motion::Transition::New(0), 0, false)
                 .phase == PresencePhase::Present);
}

// ─── animate_keyframes and MotionReveal, through a window ─────────────────

// keyed_keyframes_follow_timing_and_stop_after_completion.
static void KeyedKeyframesFollowTheirTimingAndThenStop() {
    App app;
    Window* win = new Window();
    Arena* arena = ArenaNew();
    win->app = &app;
    Ctx cx = {&app, win, arena, {}};
    MotionSetReduced(false);

    const Keyframe<float> frames[] = {Keyframe<float>::New(0.f, 0.f),
                                      Keyframe<float>::New(1.f, 10.f)};
    Keyframes<float> track = Keyframes<float>::TryNew(frames, 2).Unwrap();
    Timing timing = Timing::New(100);
    uint32_t key = MotionId(StrL("keyframe-test"));

    win->frameNow = 1.0;
    MotionStep<float> first = AnimateKeyframes(&cx, key, track, timing);
    utassertnear(first.value, 0.f);
    utassert(first.status == MotionStatus::Running);
    // Running asks for another frame, which is what keeps a playback going
    // without anything else touching the view.
    utassert(win->animFrame);

    // Halfway through, and it is the same playback: rebuilding the track and
    // the timing does not restart it.
    win->animFrame = false;
    win->frameNow = 1.05;
    utassertnear(AnimateKeyframes(&cx, key, track, timing).value, 5.f);
    utassert(win->animFrame);

    win->animFrame = false;
    win->frameNow = 1.1;
    MotionStep<float> done = AnimateKeyframes(&cx, key, track, timing);
    utassertnear(done.value, 10.f);
    utassert(done.status == MotionStatus::Finished);
    // A finished playback asks for nothing more.
    utassert(!win->animFrame);

    // Reduced motion shows the end state and never asks for a frame.
    MotionSetReduced(true);
    win->frameNow = 1.2;
    MotionStep<float> reduced = AnimateKeyframes(
        &cx, MotionId(StrL("reduced-keyframes")), track, timing);
    utassertnear(reduced.value, 10.f);
    utassert(reduced.status == MotionStatus::Finished);
    utassert(!win->animFrame);
    MotionSetReduced(false);

    delete win;
    ArenaDelete(arena);
}

// MotionReveal: the measured, clipped vertical reveal an accordion panel and
// a collapsible with a motion id are both made of.
static void AMeasuredRevealClipsToItsProgress() {
    App app;
    Window* win = new Window();
    Arena* arena = ArenaNew();
    win->app = &app;
    Ctx cx = {&app, win, arena, {}};
    win->frameNow = 1.0;

    // Nothing measured yet and nothing to show: zero height, so a closed
    // reveal does not flash its content on its first frame.
    El* closed = MotionReveal::New(&cx, StrL("panel"), 0.f, Div(arena)->H(40));
    utassertnear(closed->style.height, 0.f);
    utassertnear(closed->style.width, kFill);

    // Layout writes the child's box back through the slot the element handed
    // it; the next frame reveals that height and asks for one more, because
    // the height it was built with has changed.
    MotionRevealState* st = MotionRevealStateOf(&cx, StrL("panel"));
    utassert(st != nullptr);
    st->measured.h = 40.f;
    win->animFrame = false;
    El* half = MotionReveal::New(&cx, StrL("panel"), 0.5f, Div(arena)->H(40));
    utassertnear(half->style.height, 20.f);
    utassert(win->animFrame);
    // Once the measurement has settled it stops asking.
    win->animFrame = false;
    El* full = MotionReveal::New(&cx, StrL("panel"), 1.f, Div(arena)->H(40));
    utassertnear(full->style.height, 40.f);
    utassert(!win->animFrame);
    // Progress is clamped, so an overshooting spring cannot open a panel
    // past its own content.
    El* over = MotionReveal::New(&cx, StrL("panel"), 1.4f, Div(arena)->H(40));
    utassertnear(over->style.height, 40.f);

    delete win;
    ArenaDelete(arena);
}

// crates/ui/tests/base_compat.rs
// motion_core_types_are_available_from_the_base_facade.
static void TheMotionCoreIsReachableFromTheBaseFacade() {
    Timing timing = Timing::New(100).Ease(Easing::Linear());
    utassertnear(timing.Sample(50).directedProgress, 0.5f);
}

// spring_rejects_non_finite_or_negative_physical_parameters, and
// spring_reports_its_unit_specific_settling_tolerance.
static void ASpringChecksItsPhysicalParameters() {
    Spring spring = SpringNew(300);

    utassert(spring.TryWithDamping(TestNaN())
                 .UnwrapErr() == SpringError::InvalidDamping);
    utassert(spring.TryWithDamping(-0.1f)
                 .UnwrapErr() == SpringError::InvalidDamping);
    utassert(spring.TryWithEpsilon(TestInf())
                 .UnwrapErr() == SpringError::InvalidEpsilon);
    utassert(spring.TryWithEpsilon(-0.1f)
                 .UnwrapErr() == SpringError::InvalidEpsilon);
    utassert(base::StrEq(Str(SpringErrorMessage(SpringError::InvalidDamping)),
                         Str("spring damping must be finite and "
                             "non-negative")));
    // Rust panics in the unchecked form; there are no exceptions here, so it
    // keeps the spring it had rather than taking the process down.
    utassertnear(spring.WithDamping(-1.f).damping, 1.f);
    utassertnear(spring.WithDamping(0.7f).damping, 0.7f);

    // The tolerance a spring settles at is in the target's own units.
    Spring normalized = SpringNew(180);
    Spring pixels = SpringNew(180).WithEpsilon(0.1f);
    utassert(normalized.Epsilon() < 0.01f);
    utassertnear(pixels.Epsilon(), 0.1f);
}

// theme/motion.rs: the semantic scale the styled components now read instead
// of each naming a spring of its own.
static void TheThemeCarriesOneSemanticMotionScale() {
    App app;
    const Theme& th = ThemeNow(&app);
    const MotionTokens& m = th.motion;

    utassertnear(m.durationInstantMs, 0.f);
    utassert(m.durationFastMs < m.durationNormalMs);
    utassert(m.durationNormalMs < m.durationSlowMs);
    utassert(m.distanceShort < m.distanceMedium);
    utassertnear(m.easingEnter.Sample(0.f), 0.f);
    utassertnear(m.easingEnter.Sample(1.f), 1.f);
    // normalized_and_pixel_springs_use_unit_appropriate_tolerances.
    utassert(m.springControl.Epsilon() < 0.01f);
    utassertnear(m.springMove.Epsilon(), 0.1f);
    // The two springs differ in what they are for: a control settles fast and
    // never overshoots, something travelling arrives with a little weight.
    utassertnear(m.springControl.damping, 1.f);
    utassertnear(m.springMove.damping, 0.85f);
    utassert(m.springControl.responseMs < m.springMove.responseMs);
}

// reduced_motion_spinner_is_static_and_requests_no_frame — crates/ui's own
// test, which is about motion rather than about the spinner's look.
static void AReducedMotionSpinnerIsStaticAndAsksForNoFrame() {
    App app;
    Window* win = new Window();
    Arena* arena = ArenaNew();
    win->app = &app;
    Ctx cx = {&app, win, arena, {}};
    win->frameNow = 1.0;

    MotionSetReduced(true);
    win->animFrame = false;
    El* still = component::Spinner::New(&cx)->Id(StrL("a"))->IntoEl();
    utassert(still->first != nullptr);
    utassertnear(still->first->style.rotate, 0.f);
    utassert(!win->animFrame);

    // With motion on it turns, and keeps asking for the frames to turn with.
    // The first frame is where the loop starts, so it is the one after that
    // has anything to show for it.
    MotionSetReduced(false);
    win->frameNow = 1.2;
    component::Spinner::New(&cx)->Id(StrL("a"))->IntoEl();
    win->frameNow = 1.4;
    El* turning = component::Spinner::New(&cx)->Id(StrL("a"))->IntoEl();
    utassert(turning->first->style.rotate > 0.f);
    utassert(win->animFrame);

    delete win;
    ArenaDelete(arena);
}

static void ASystemPreferenceForReducedMotionSetsTheFlag() {
    MotionResetReduceForTest();
    ApplyReduceMotionPreference(true);
    utassert(MotionReduced());
    MotionResetReduceForTest();
}

static void AnUnknownSystemPreferenceLeavesTheFlagAlone() {
    MotionResetReduceForTest();
    utassert(!MotionReduced());
    MotionSetReduced(true);
    // A missing reading does not call ApplyReduceMotionPreference.
    utassert(MotionReduced());
    MotionResetReduceForTest();
}

static void TheSystemDrivesTheFlagUntilTheApplicationSetsIt() {
    MotionResetReduceForTest();
    ApplyReduceMotionPreference(true);
    ApplyReduceMotionPreference(false);
    utassert(!MotionReduced());
    ApplyReduceMotionPreference(true);
    utassert(MotionReduced());

    MotionSetReduced(false);
    ApplyReduceMotionPreference(true);
    utassert(!MotionReduced());
    MotionResetReduceForTest();
}

static void AFlagTheApplicationSetBeforeTheFirstReadingIsKept() {
    MotionResetReduceForTest();
    MotionSetReduced(true);
    ApplyReduceMotionPreference(false);
    utassert(MotionReduced());
    MotionResetReduceForTest();
}

void TestMotion() {
    TestSuite("motion");
    TheEasingsAreTheCurvesRustNames();
    TheLoopingEasingsAreGpuisOwn();
    TheBezierRunsFromNothingToEverything();
    TheBezierMatchesWhatAnEngineSays();
    ThirdsMapTimeIdentically();
    TheLerpsAreTheThreeRustImplements();
    EffectTransitionAppliesEveryUpstreamEffect();
    ProgressWaitsOutTheDelayAndStopsAtTheEnd();
    AZeroDurationTargetChangeIsImmediate();
    AChangedTargetTransitionsOverTime();
    ReversingCarriesOnFromWhereItGotTo();
    ADelayHoldsThePreviousValue();
    AFinishedTransitionStopsAskingForFrames();
    ReducedMotionTakesTheTargetOutright();
    TheSameRuleCarriesAPointAndAColor();
    AChannelKeepsTwoValuesOfOneElementApart();
    SourceNamedValueTransitionContractIsAvailable();
    OpacityMultipliesEveryColourItPaints();
    ASpringArrivesAndStops();
    ASpringCarriesItsVelocityThroughAReversal();
    ASpringUnderOneOvershoots();
    ASuspendedSpringPinsItselfToTheTarget();
    ACoarseEpsilonSettlesSooner();
    ASpringChecksItsPhysicalParameters();
    TheCssKeywordEasingsMatchTheirPublishedSamples();
    StepEasingObservesTheCssJumpPositions();
    LinearStopsFillTheirOmittedPositions();
    ANegativeDelayStartsInsideTheActiveInterval();
    AlternateDirectionReversesTheOddIterations();
    KeyframesValidateOffsetsAndSampleTheirSegments();
    DiscreteValuesSwitchOnlyAtTheirSwitchPoint();
    StaggerOriginsProduceStableDelays();
    GeometryInterpolatesChannelByChannel();
    ATransitionReportsWhereItIs();
    SequenceStepsAdvanceAtTheirBoundaries();
    SequenceHandlesInstantStepsAndReducedMotion();
    PresenceEntersExitsAndUnmountsOnlyAfterItsExit();
    KeyedKeyframesFollowTheirTimingAndThenStop();
    AMeasuredRevealClipsToItsProgress();
    TheMotionCoreIsReachableFromTheBaseFacade();
    TheThemeCarriesOneSemanticMotionScale();
    AReducedMotionSpinnerIsStaticAndAsksForNoFrame();
    ASystemPreferenceForReducedMotionSetsTheFlag();
    AnUnknownSystemPreferenceLeavesTheFlagAlone();
    TheSystemDrivesTheFlagUntilTheApplicationSetsIt();
    AFlagTheApplicationSetBeforeTheFirstReadingIsKept();
}
