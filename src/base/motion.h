#ifndef GPUI_BASE_MOTION_H_
#define GPUI_BASE_MOTION_H_
/* The motion core — crates/base/src/motion.rs and its submodules
   motion/easing.rs, keyframes.rs, presence.rs, reveal.rs, stagger.rs and
   timing.rs, which are one C++ file per Rust module directory.

   A CSS-like timing policy for a value the caller owns. The caller asks for
   the value it wants; this answers the value to draw *now*, on its way there.
   It never picks a visual property — that is what makes it different from
   animation.rs's EffectTransition, which restyles an element for you.

   The state is window-keyed, the way Rust's `window.use_keyed_state` is: an
   element tree that is rebuilt every frame has nowhere else to keep where a
   value had got to. A transition still running asks for another frame, which
   is `window.request_animation_frame()`.

   Components opt in explicitly; nothing here installs motion by default.

   Durations are milliseconds as floats throughout, where Rust carries a
   `Duration`; a `SignedDuration` is a signed number of milliseconds. */

#include "base/animation.h"

namespace gpui {

// ─── Result ───────────────────────────────────────────────────────────────
//
// Rust's `Result<T, E>` for the checked builders below: `Keyframes::try_new`,
// `Easing::steps`, `Discrete::switch_at`, `Spring::try_with_damping`. There
// are no exceptions here, so the pair travels together and the caller asks.
template <typename T, typename E>
struct MotionResult {
    bool ok = false;
    T value = {};
    E error = {};

    bool IsOk() const { return ok; }
    bool IsErr() const { return !ok; }
    // `.unwrap()` / `.expect(..)`: the value, which a static configuration
    // has already been checked to hold.
    const T& Unwrap() const { return value; }
    // `.unwrap_err()`.
    E UnwrapErr() const { return error; }
};

// ─── motion/easing.rs ─────────────────────────────────────────────────────

// The point at which a stepped easing jumps.
enum class StepPosition : uint8_t {
    JumpStart,
    JumpEnd,
    JumpNone,
    JumpBoth,
};

// One output and its optional input position in a CSS-like `linear()` curve.
struct LinearStop {
    float output = 0;
    float input = 0;
    bool hasInput = false;

    static LinearStop New(float output) {
        LinearStop s;
        s.output = output;
        return s;
    }
    static LinearStop At(float output, float input) {
        LinearStop s;
        s.output = output;
        s.input = input;
        s.hasInput = true;
        return s;
    }
};

// Invalid easing configuration.
enum class EasingError : uint8_t {
    // Retained for source compatibility. New validation reports
    // InvalidBezierControlPoint.
    InvalidBezierX,
    InvalidBezierControlPoint,
    InvalidStepCount,
    InvalidLinearStops,
};

// `impl Display for EasingError`.
const char* EasingErrorMessage(EasingError e);

enum class EasingKind : uint8_t {
    Linear,
    Ease,
    EaseIn,
    EaseOut,
    EaseInOut,
    CubicBezier,
    Steps,
    LinearStops,
    Custom,
};

struct Easing;
using EasingResult = MotionResult<Easing, EasingError>;

// A cheap, copyable CSS-compatible easing policy. Rust's is an enum whose
// LinearStops variant owns an `Arc<[(f32, f32)]>` and whose Custom variant
// owns an `Rc<dyn Fn>`; this POD borrows the stops from the arena the caller
// built them in and carries a function pointer, so the policy can sit in a
// theme's tokens and be copied by value.
struct Easing {
    EasingKind kind = EasingKind::EaseOut;
    // CubicBezier.
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    // Steps.
    uint32_t count = 1;
    StepPosition position = StepPosition::JumpEnd;
    // LinearStops: `len` (input, output) pairs, sorted by input.
    const float* stops = nullptr;
    int32_t stopsLen = 0;
    // Custom.
    EaseFn custom = nullptr;

    static Easing Linear() { return Of(EasingKind::Linear); }
    static Easing Ease() { return Of(EasingKind::Ease); }
    static Easing EaseIn() { return Of(EasingKind::EaseIn); }
    static Easing EaseOut() { return Of(EasingKind::EaseOut); }
    static Easing EaseInOut() { return Of(EasingKind::EaseInOut); }
    static Easing Custom(EaseFn fn) {
        Easing e = Of(EasingKind::Custom);
        e.custom = fn;
        return e;
    }

    // Easing::cubic_bezier: the control points must be finite and both x
    // within 0..=1.
    static EasingResult CubicBezier(float x1, float y1, float x2, float y2);
    // Easing::steps: a step count of zero is invalid, and so is one step
    // with JumpNone, which would have nowhere to jump.
    static EasingResult Steps(uint32_t count, StepPosition position);
    // Easing::linear_stops: at least two stops, every number finite, and the
    // omitted input positions filled in evenly between the ones given. The
    // resolved pairs are written into `a`.
    static EasingResult LinearStops(Arena* a, const LinearStop* stops,
                                    int32_t len);

    float Sample(float progress) const;

  private:
    static Easing Of(EasingKind k) {
        Easing e;
        e.kind = k;
        return e;
    }
};

// ─── motion/timing.rs ─────────────────────────────────────────────────────

// A delay that may run backwards: a negative one starts a motion partway
// through its active interval.
struct SignedDuration {
    float ms = 0;
    bool negative = false;

    static SignedDuration Zero() { return {}; }
    static SignedDuration Positive(float ms) { return {ms, false}; }
    static SignedDuration Negative(float ms) { return {ms, true}; }

    // active_elapsed: how far into the active interval `elapsedMs` is, or
    // false while a positive delay is still running.
    bool ActiveElapsed(float elapsedMs, float* out) const;

    bool operator==(const SignedDuration& o) const {
        return ms == o.ms && negative == o.negative;
    }
    bool operator!=(const SignedDuration& o) const { return !(*this == o); }
};

struct IterationCount {
    bool infinite = false;
    uint64_t count = 1;

    static IterationCount Finite(uint64_t n) { return {false, n}; }
    static IterationCount Infinite() { return {true, 0}; }
};

enum class PlaybackDirection : uint8_t {
    Normal,
    Reverse,
    Alternate,
    AlternateReverse,
};

enum class MotionPhase : uint8_t {
    Before,
    Active,
    After,
};

struct TimingSample {
    MotionPhase phase = MotionPhase::Before;
    float directedProgress = 0;
    uint64_t iteration = 0;
    bool active = false;
    bool finished = false;
};

// CSS animation timing: delay, duration, iteration count, direction and an
// easing, sampled at an elapsed time.
struct Timing {
    SignedDuration delay = {};
    float durationMs = 0;
    IterationCount iterations = IterationCount::Finite(1);
    PlaybackDirection direction = PlaybackDirection::Normal;
    Easing easing = Easing::Linear();

    static Timing New(float durationMs) {
        Timing t;
        t.durationMs = durationMs;
        return t;
    }
    Timing Delay(SignedDuration d) const {
        Timing t = *this;
        t.delay = d;
        return t;
    }
    Timing Delay(float ms) const { return Delay(SignedDuration::Positive(ms)); }
    Timing Iterations(IterationCount n) const {
        Timing t = *this;
        t.iterations = n;
        return t;
    }
    Timing Direction(PlaybackDirection d) const {
        Timing t = *this;
        t.direction = d;
        return t;
    }
    Timing Ease(Easing e) const {
        Timing t = *this;
        t.easing = e;
        return t;
    }

    TimingSample Sample(float elapsedMs) const;

  private:
    TimingSample AfterSample(uint64_t count) const;
    float Directed(uint64_t iteration, float progress) const;
};

// ─── motion/stagger.rs ────────────────────────────────────────────────────

struct StaggerOrigin {
    enum Kind : uint8_t {
        First,
        Last,
        Center,
        Index
    };
    Kind kind = First;
    int32_t index = 0;

    static StaggerOrigin FirstOrigin() { return {First, 0}; }
    static StaggerOrigin LastOrigin() { return {Last, 0}; }
    static StaggerOrigin CenterOrigin() { return {Center, 0}; }
    static StaggerOrigin IndexOrigin(int32_t ix) { return {Index, ix}; }
};

// A per-item delay worked out from an interval and where the wave starts,
// without a schedule allocated for it.
struct Stagger {
    float intervalMs = 0;
    StaggerOrigin origin = {};

    static Stagger New(float intervalMs, StaggerOrigin origin) {
        return {intervalMs, origin};
    }
    float Delay(int32_t index, int32_t count) const;
};

// ─── motion.rs ────────────────────────────────────────────────────────────

// Matches GPUI's own default spring settling tolerance.
constexpr float kDefaultSpringEpsilon = 0.001f;

// A presentation-neutral bundle for coordinated paint transforms.
struct MotionTransform {
    Point translation = {0, 0};
    Point scale = {1, 1};
    float rotationRadians = 0;
    float opacity = 1;

    static MotionTransform Identity() { return {}; }
};

// motion.rs has a Transition too. Keep it in the source module's namespace so
// it can coexist with animation.rs::Transition, rather than renaming one of
// the two public Rust contracts in the C++ surface.
namespace motion {

// A value that can be interpolated between two application-owned targets.
// Rust expresses this as a trait with a blanket implementation for Lerp; the
// C++ specialization seam has the same shape and the default delegates to the
// corresponding Lerp overload.
template <typename T>
struct Interpolate {
    static T Between(const T& from, const T& target, float progress) {
        return Lerp(from, target, progress);
    }
};

template <>
struct Interpolate<Size> {
    static Size Between(const Size& from, const Size& to, float p) {
        return {Lerp(from.w, to.w, p), Lerp(from.h, to.h, p)};
    }
};

template <>
struct Interpolate<Bounds> {
    static Bounds Between(const Bounds& from, const Bounds& to, float p) {
        return {Lerp(from.x, to.x, p), Lerp(from.y, to.y, p),
                Lerp(from.w, to.w, p), Lerp(from.h, to.h, p)};
    }
};

template <>
struct Interpolate<MotionTransform> {
    static MotionTransform Between(const MotionTransform& from,
                                   const MotionTransform& to, float p) {
        MotionTransform out;
        out.translation = Lerp(from.translation, to.translation, p);
        out.scale = {Lerp(from.scale.x, to.scale.x, p),
                     Lerp(from.scale.y, to.scale.y, p)};
        out.rotationRadians = Lerp(from.rotationRadians, to.rotationRadians, p);
        out.opacity = Lerp(from.opacity, to.opacity, p);
        return out;
    }
};

// CSS-like timing policy: how long, how late, and along which curve. Rust
// builds it with a consuming chain; this POD returns a copy from the same
// operations so named policies can be composed without retained ownership.
//
// `delayMs` is the SignedDuration: negative starts the run partway through.
struct Transition {
    float durationMs = 0;
    float delayMs = 0;
    Easing easing = Easing::Custom(EaseOutCubic);

    static Transition New(float durationMs) {
        Transition policy;
        policy.durationMs = durationMs;
        return policy;
    }

    Transition Delay(float ms) const {
        Transition policy = *this;
        policy.delayMs = ms;
        return policy;
    }

    Transition Delay(SignedDuration d) const {
        return Delay(d.negative ? -d.ms : d.ms);
    }

    // `.ease(|t| ..)`: a custom curve.
    Transition Ease(EaseFn fn) const {
        Transition policy = *this;
        policy.easing = Easing::Custom(fn);
        return policy;
    }

    // `.easing(Easing)`: a named CSS curve.
    Transition Ease(Easing e) const {
        Transition policy = *this;
        policy.easing = e;
        return policy;
    }

    SignedDuration Delay() const {
        return delayMs < 0 ? SignedDuration::Negative(-delayMs)
                           : SignedDuration::Positive(delayMs);
    }
};

// One independently transitioning value. Upstream wraps ElementId and adds a
// named child for the retained transition state. The runtime's keyed store is
// already the folded GlobalElementId, so this wrapper carries that POD key.
struct TransitionId {
    uint32_t key = 0;

    TransitionId() = default;
    explicit TransitionId(uint32_t value) : key(value) {}
    explicit TransitionId(Str id);
    TransitionId(Str id, Str channel);

    bool operator==(const TransitionId& other) const {
        return key == other.key;
    }
    bool operator!=(const TransitionId& other) const {
        return key != other.key;
    }
};

} // namespace motion

// Compatibility spelling used by the port before the Rust module namespace
// was restored. It is the same policy, not an adapter state.
using Motion = motion::Transition;

inline Motion MotionNew(float durationMs) {
    return motion::Transition::New(durationMs);
}

// One independently transitioning value. Rust's TransitionId is an ElementId
// with a channel name under it, so one element can transition several values
// without their state colliding; the same pair hashed is the key here.
uint32_t MotionId(Str id);
uint32_t MotionId(Str id, Str channel);
// `window.use_keyed_state(id, ..)` in motion.rs is keyed by the whole id
// stack, so a transition named among its siblings is still its own. This is
// that name folded into the stack the widget is being built under.
uint32_t MotionName(Ctx* cx, Str name);

// Where a transition is in its life.
enum class MotionStatus : uint8_t {
    Idle,
    Delayed,
    Running,
    Finished,
};

// Transition::progress: nothing has happened yet while the delay runs, a
// duration of zero is over as soon as it starts, and the rest is the fraction
// of the duration that has passed, capped at 1. The status says which.
float MotionProgress(const Motion& m, float elapsedMs, float durationMs,
                     MotionStatus* status);
inline float MotionProgress(const Motion& m, float elapsedMs) {
    MotionStatus status;
    return MotionProgress(m, elapsedMs, m.durationMs, &status);
}
// Transition::sample: the curve at that fraction.
float MotionSample(const Motion& m, float progress);

// cx.reduce_motion(). Rust reads the platform's setting; so does this, on the
// two platforms that have one to read (Windows' client-area animation and
// macOS' reduce-motion switch). A value that is transitioning when it goes on
// adopts its target on the next frame rather than finishing the curve.
bool MotionReduced();
// For a caller that wants to decide for itself — the story's settings menu,
// and the tests.
void MotionSetReduced(bool on);

// Where a value has got to. Rust keeps `from`, `target`, `started_at`, a
// `reversing_factor` and the `duration` the current run was given; `init`
// stands in for the state having been created by use_keyed_state's closure,
// since a keyed slot here arrives zeroed.
template <typename T>
struct MotionState {
    T from = {};
    T target = {};
    double startedAt = 0;
    float reversingFactor = 1.f;
    float durationMs = 0;
    bool init = false;
};

// MotionValue<T> in Rust: the value and where the transition is.
template <typename T>
struct MotionStep {
    T value = {};
    // Whether another frame is wanted: request_animation_frame is called for
    // exactly this — a transition that is Delayed or Running.
    bool running = false;
    MotionStatus status = MotionStatus::Idle;
};

// Two values are the same one. Rust bounds T on PartialEq; these are the
// overloads that stand for it.
inline bool MotionEq(float a, float b) {
    return a == b;
}
inline bool MotionEq(Point a, Point b) {
    return a.x == b.x && a.y == b.y;
}
inline bool MotionEq(Size a, Size b) {
    return a.w == b.w && a.h == b.h;
}
inline bool MotionEq(Bounds a, Bounds b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}
inline bool MotionEq(Rgba a, Rgba b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
inline bool MotionEq(const MotionTransform& a, const MotionTransform& b) {
    return MotionEq(a.translation, b.translation) &&
           MotionEq(a.scale, b.scale) &&
           a.rotationRadians == b.rotationRadians && a.opacity == b.opacity;
}

// Keyed-state seams used by the template policies below.
double MotionNow(Ctx* cx);
void* MotionSlot(Ctx* cx, uint32_t key, int size);
void MotionWantsFrame(Ctx* cx);

// ─── motion/sequence.rs ──────────────────────────────────────────────────

template <typename T>
struct SequenceStep {
    T target = {};
    motion::Transition transition = motion::Transition::New(0);

    static SequenceStep New(T value, const motion::Transition& policy) {
        SequenceStep step;
        step.target = value;
        step.transition = policy;
        return step;
    }
    const T& Target() const { return target; }
    const motion::Transition& Transition() const { return transition; }
};

template <typename T>
struct SequenceSample {
    T value = {};
    int32_t step = 0;
    MotionStatus status = MotionStatus::Idle;

    const T& Value() const { return value; }
    T IntoValue() const { return value; }
    int32_t Step() const { return step; }
    MotionStatus Status() const { return status; }
    bool IsFinished() const { return status == MotionStatus::Finished; }
    bool IsActive() const {
        return status == MotionStatus::Delayed ||
               status == MotionStatus::Running;
    }
};

template <typename T>
struct SequenceState {
    int32_t step = 0;
    T from = {};
    T target = {};
    motion::Transition transition = motion::Transition::New(0);
    double startedAt = 0;
    bool init = false;
};

// A chain of transitions. Each hand-off uses the preceding step's absolute
// finish time, so a slow frame samples the next step where it should already
// be instead of starting it late.
template <typename T>
struct Sequence {
    Arena* arena = nullptr;
    motion::TransitionId id = {};
    T from = {};
    ArenaVec<SequenceStep<T>> steps;

    static Sequence New(Arena* a, motion::TransitionId valueId, T value) {
        Sequence out;
        out.arena = a;
        out.id = valueId;
        out.from = value;
        return out;
    }
    static Sequence New(Ctx* cx, motion::TransitionId valueId, T value) {
        return New(cx ? cx->a : nullptr, valueId, value);
    }
    Sequence& WithStep(T target, const motion::Transition& policy) {
        steps.Append(arena, SequenceStep<T>::New(target, policy));
        return *this;
    }
    Sequence& WithSteps(const SequenceStep<T>* values, int32_t count) {
        for (int32_t i = 0; i < count; i++) steps.Append(arena, values[i]);
        return *this;
    }
    const T& From() const { return from; }
    const ArenaVec<SequenceStep<T>>& Steps() const { return steps; }

    SequenceSample<T> Sample(Ctx* cx) const {
        SequenceSample<T> out;
        out.value = from;
        if (!cx || len(steps) == 0) return out;

        const int32_t last = len(steps) - 1;
        uint32_t stateKey = id.key ^ 0x9e3779b9u;
        auto* state = (SequenceState<T>*)MotionSlot(
            cx, stateKey, (int)sizeof(SequenceState<T>));
        if (!state) {
            out.value = steps[last].target;
            out.step = last;
            out.status = MotionStatus::Finished;
            return out;
        }
        const double now = MotionNow(cx);
        auto start = [&](int32_t at, T startValue, double startedAt) {
            state->step = at;
            state->from = startValue;
            state->target = steps[at].target;
            state->transition = steps[at].transition;
            state->startedAt = startedAt;
            state->init = true;
        };
        if (!state->init) start(0, from, now);

        if (MotionReduced()) {
            start(last, steps[last].target, now);
            out.value = steps[last].target;
            out.step = last;
            out.status = MotionStatus::Finished;
            return out;
        }

        auto sample = [&](MotionStatus* status) {
            float elapsed =
                (float)(std::max(0.0, now - state->startedAt) * 1000.0);
            float progress =
                MotionProgress(state->transition, elapsed,
                               state->transition.durationMs, status);
            return motion::Interpolate<T>::Between(
                state->from, state->target,
                MotionSample(state->transition, progress));
        };

        MotionStatus status;
        T value = sample(&status);
        bool retargeted = state->step > last ||
                          !MotionEq(state->target, steps[state->step].target);
        if (retargeted) {
            start(0, value, now);
            value = sample(&status);
        } else {
            while (status == MotionStatus::Finished && state->step < last) {
                float finishMs =
                    state->transition.durationMs + state->transition.delayMs;
                if (finishMs < 0) finishMs = 0;
                double nextStart = state->startedAt + finishMs / 1000.0;
                T previousTarget = state->target;
                start(state->step + 1, previousTarget, nextStart);
                value = sample(&status);
            }
        }
        out.value = value;
        out.step = state->step;
        out.status = status;
        if (out.IsActive()) MotionWantsFrame(cx);
        return out;
    }
};

// The whole rule, with the state and the clock passed in — which is what makes
// it testable without a window. `transition_with_status()` in Rust, minus the
// two lines that fetch the state and ask for a frame.
//
// The first target is adopted outright; a later change transitions from the
// value sampled at that instant, so reversing halfway does not jump. A direct
// reversal — back to the value the run left from — shortens the return to the
// fraction of the curve already travelled, so an interrupted open closes in
// the time it took to get where it was.
template <typename T>
MotionStep<T> MotionAdvance(MotionState<T>* st, T target, const Motion& m,
                            double now, bool reduced) {
    MotionStep<T> out;
    if (!st->init) {
        st->init = true;
        st->from = target;
        st->target = target;
        st->startedAt = now;
        st->reversingFactor = 1.f;
        st->durationMs = m.durationMs;
    }
    if (reduced || m.durationMs <= 0) {
        if (!MotionEq(st->from, target) || !MotionEq(st->target, target)) {
            st->from = target;
            st->target = target;
            st->startedAt = now;
            st->reversingFactor = 1.f;
            st->durationMs = m.durationMs;
        }
        out.value = target;
        out.status = MotionStatus::Finished;
        return out;
    }
    double elapsedS = now - st->startedAt;
    float elapsedMs = elapsedS > 0 ? (float)(elapsedS * 1000.0) : 0.f;
    MotionStatus status;
    float progress = MotionProgress(m, elapsedMs, st->durationMs, &status);
    float eased = MotionSample(m, progress);
    T sampled = motion::Interpolate<T>::Between(st->from, st->target, eased);
    if (!MotionEq(st->target, target)) {
        // The target moved: carry on from where the last one had got to.
        bool reversing = MotionEq(target, st->from);
        float factor = 1.f;
        if (reversing) {
            factor = eased * st->reversingFactor + (1.f - st->reversingFactor);
            factor = ClampF01(factor);
        }
        float duration = m.durationMs * factor;
        st->from = sampled;
        st->target = target;
        st->startedAt = now;
        st->reversingFactor = factor;
        st->durationMs = duration;
        MotionStatus initialStatus;
        float initial = MotionProgress(m, 0.f, duration, &initialStatus);
        out.value = motion::Interpolate<T>::Between(sampled, target,
                                                    MotionSample(m, initial));
        out.status = initialStatus;
    } else {
        out.value = sampled;
        out.status =
            MotionEq(st->from, st->target) ? MotionStatus::Idle : status;
    }
    out.running = out.status == MotionStatus::Delayed ||
                  out.status == MotionStatus::Running;
    return out;
}

// Animation::repeat: a loop with no target and no end. The phase of a cycle
// of `periodMs`, put through `ease` — the delta GPUI hands the closure of a
// `with_animation(.., Animation::new(d).repeat(), ..)`. Something showing one
// is asking for a frame every frame, which is what a spinner is.
//
// Rust's repeats do not consult `reduce_motion`, and neither does this: the
// caller decides, the way the spinner and the progress bar do.
float MotionRepeat(Ctx* cx, uint32_t key, float periodMs,
                   EaseFn ease = nullptr);

// with_animation, the one-shot kind: how far into a `durationMs` run the
// element is, from the frame it first appeared in. GPUI keeps that clock in
// the element's own state and drops it with the element, which is why the
// slots here are swept — something that goes away and comes back plays its
// entrance again.
//
// Under reduced motion an entrance is over before it starts, which is the
// same answer `transition` gives.
float MotionAppear(Ctx* cx, uint32_t key, float durationMs,
                   EaseFn ease = nullptr);

// Where a transition starts from, for a value whose first frame is not where
// it is going. `transition()` has no such call — Rust's animated placeholder
// keeps its own `from` and an epoch beside it and restarts the run by hand —
// and the same effect here is to write the state before the first ask: the
// next MotionValue sees a target that differs from this one and runs the
// curve from it.
template <typename T>
void MotionSeed(Ctx* cx, uint32_t key, T from) {
    auto* st =
        (MotionState<T>*)MotionSlot(cx, key, (int)sizeof(MotionState<T>));
    if (!st) {
        return;
    }
    st->init = true;
    st->from = from;
    st->target = from;
    st->startedAt = MotionNow(cx);
    st->reversingFactor = 1.f;
    st->durationMs = 0;
}

// ─── springs ──────────────────────────────────────────────────────────────
//
// motion::Spring. A transition cannot carry *velocity* across a change of
// target: it restarts its curve from the value sampled at that instant, which
// is continuous in position and not in speed, so a value reversed mid-flight
// jumps to the new curve's initial pace. A spring turns around instead.
//
// The rule upstream applies, and this follows: spring where the target
// changes faster than the motion finishes — a switch toggled twice, a tab
// indicator chasing the pointer, a dock being dragged — and transition where
// a target is set once and runs to its end.

// Invalid physical or settling parameters for a Spring.
enum class SpringError : uint8_t {
    InvalidDamping,
    InvalidEpsilon,
};

// `impl Display for SpringError`.
const char* SpringErrorMessage(SpringError e);

struct Spring;
using SpringResult = MotionResult<Spring, SpringError>;

struct Spring {
    // The period one full undamped oscillation would take, which is the scale
    // the motion is felt at rather than the moment it stops. A spring has no
    // end to schedule: it settles once it is within `epsilon` of its target.
    // Zero adopts the target on the spot, as a zero duration does.
    float responseMs = 0;
    // The damping ratio. 1 is critical — it never passes its target — below
    // that it overshoots and comes back, above it crawls in. A value bounded
    // by the geometry around it (a thumb inside a track) keeps 1.
    float damping = 1.f;
    // How close counts as arrived, in the target's own units. The default
    // suits a 0..1 value; a spring over pixels settles perceptibly sooner
    // with a coarser one, and stops asking for frames it has nothing to draw
    // with.
    float epsilon = kDefaultSpringEpsilon;
    // Whether the spring travels at all. A value the pointer is already
    // moving — a panel being dragged by its handle — must not lag behind it,
    // so travel is off for as long as the drag lasts; the state stays pinned
    // to the target meanwhile, so travel resumes from where the drag left it
    // rather than from where the spring had got to before it started.
    bool travel = true;

    static Spring New(float responseMs) {
        Spring s;
        s.responseMs = responseMs;
        return s;
    }
    // Spring::with_damping. Rust panics on a negative or non-finite ratio;
    // there is no panic here, so an invalid one leaves the spring as it was.
    // Use TryWithDamping when the value is not a trusted constant.
    Spring WithDamping(float ratio) const;
    SpringResult TryWithDamping(float ratio) const;
    Spring WithTravel(bool v) const {
        Spring s = *this;
        s.travel = v;
        return s;
    }
    // Spring::with_epsilon, on the same terms as WithDamping.
    Spring WithEpsilon(float eps) const;
    SpringResult TryWithEpsilon(float eps) const;
    float Epsilon() const { return epsilon; }
};

inline Spring SpringNew(float responseMs) {
    return Spring::New(responseMs);
}

// Where a sprung value is and how fast it is going. `target` is what it was
// travelling to over the frame that just elapsed; a new one is adopted for
// the frame to come, keeping both.
struct SpringState {
    float position = 0;
    float velocity = 0;
    float target = 0;
    double updatedAt = 0;
    bool init = false;
};

struct SpringStep {
    float value = 0;
    bool running = false;
};

// The whole rule with the state and the clock passed in, which is what makes
// it testable without a window.
SpringStep SpringAdvance(SpringState* st, float target, const Spring& s,
                         double now, bool reduced);

// motion::spring: the value to draw now, on its way to `target`.
float SpringValue(Ctx* cx, uint32_t key, float target, const Spring& s);

// MotionSeed's counterpart: where a spring starts from, for a value whose
// first frame is not where it is going.
inline void SpringSeed(Ctx* cx, uint32_t key, float from) {
    auto* st = (SpringState*)MotionSlot(cx, key, (int)sizeof(SpringState));
    if (!st) {
        return;
    }
    st->init = true;
    st->position = from;
    st->velocity = 0;
    st->target = from;
    st->updatedAt = MotionNow(cx);
}

// motion::transition_with_status: the value to draw now, on its way to
// `target`, and where the transition is.
template <typename T>
MotionStep<T> MotionValueWithStatus(Ctx* cx, uint32_t key, T target,
                                    const Motion& m) {
    auto* st =
        (MotionState<T>*)MotionSlot(cx, key, (int)sizeof(MotionState<T>));
    if (!st) {
        MotionStep<T> out;
        out.value = target;
        out.status = MotionStatus::Finished;
        return out;
    }
    MotionStep<T> step =
        MotionAdvance(st, target, m, MotionNow(cx), MotionReduced());
    if (step.running) {
        MotionWantsFrame(cx);
    }
    return step;
}

// motion::transition: the value to draw now, on its way to `target`.
template <typename T>
T MotionValue(Ctx* cx, uint32_t key, T target, const Motion& m) {
    return MotionValueWithStatus(cx, key, target, m).value;
}

// ─── motion/keyframes.rs ──────────────────────────────────────────────────

template <typename T>
struct Keyframe {
    float offset = 0;
    T value = {};
    Easing easing = Easing::Linear();

    static Keyframe New(float offset, T value) {
        Keyframe k;
        k.offset = offset;
        k.value = value;
        return k;
    }
    Keyframe Ease(Easing e) const {
        Keyframe k = *this;
        k.easing = e;
        return k;
    }
};

enum class KeyframeError : uint8_t {
    TooFewFrames,
    OffsetNotFinite,
    OffsetOutOfRange,
    OffsetsNotMonotonic,
    MissingEndpoint,
};

// A validated track of keyframes. Rust owns them in an `Arc<[Keyframe<T>]>`;
// this borrows the array it was checked against, which the caller keeps for
// as long as the track is sampled — a frame's arena, or the stack of the
// render that built it.
template <typename T>
struct Keyframes {
    const Keyframe<T>* frames = nullptr;
    int32_t len = 0;

    static MotionResult<Keyframes<T>, KeyframeError> TryNew(
        const Keyframe<T>* frames, int32_t len) {
        MotionResult<Keyframes<T>, KeyframeError> r;
        if (len < 2) {
            r.error = KeyframeError::TooFewFrames;
            return r;
        }
        for (int32_t i = 0; i < len; i++) {
            if (!IsFiniteF(frames[i].offset)) {
                r.error = KeyframeError::OffsetNotFinite;
                return r;
            }
        }
        for (int32_t i = 0; i < len; i++) {
            if (frames[i].offset < 0.f || frames[i].offset > 1.f) {
                r.error = KeyframeError::OffsetOutOfRange;
                return r;
            }
        }
        for (int32_t i = 0; i + 1 < len; i++) {
            if (frames[i].offset > frames[i + 1].offset) {
                r.error = KeyframeError::OffsetsNotMonotonic;
                return r;
            }
        }
        if (frames[0].offset != 0.f || frames[len - 1].offset != 1.f) {
            r.error = KeyframeError::MissingEndpoint;
            return r;
        }
        r.ok = true;
        r.value.frames = frames;
        r.value.len = len;
        return r;
    }

    T Sample(float progress) const {
        progress = ClampF01(progress);
        // partition_point(|frame| frame.offset <= progress).
        int32_t upper = 0;
        while (upper < len && frames[upper].offset <= progress) {
            upper++;
        }
        if (upper == 0) {
            return frames[0].value;
        }
        if (upper == len) {
            return frames[len - 1].value;
        }
        const Keyframe<T>& from = frames[upper - 1];
        const Keyframe<T>& to = frames[upper];
        if (from.offset == to.offset) {
            return to.value;
        }
        float segment = (progress - from.offset) / (to.offset - from.offset);
        return motion::Interpolate<T>::Between(from.value, to.value,
                                               from.easing.Sample(segment));
    }

    int32_t Len() const { return len; }
    bool IsEmpty() const { return len == 0; }

  private:
    static bool IsFiniteF(float v) { return v - v == 0.f; }
};

enum class DiscreteError : uint8_t {
    InvalidSwitchPoint,
};

// A value that flips from one to the other at a point of the progress rather
// than interpolating.
template <typename T>
struct Discrete {
    T from = {};
    T to = {};
    float switchAt = 0.5f;

    static Discrete New(T from, T to) {
        Discrete d;
        d.from = from;
        d.to = to;
        return d;
    }

    MotionResult<Discrete<T>, DiscreteError> SwitchAt(float progress) const {
        MotionResult<Discrete<T>, DiscreteError> r;
        if (!(progress - progress == 0.f) || progress < 0.f || progress > 1.f) {
            r.error = DiscreteError::InvalidSwitchPoint;
            return r;
        }
        r.ok = true;
        r.value = *this;
        r.value.switchAt = progress;
        return r;
    }

    T Sample(float progress) const { return progress < switchAt ? from : to; }
};

// The keyed half of animate_keyframes: when the playback keyed by `key`
// started, which is all the state a keyframe track needs.
struct KeyframePlayback {
    double startedAt = 0;
    bool init = false;
};

// The timing sampled against the playback's own clock, so the template below
// is the only generic part. Answers the status, requests a frame while the
// playback is Delayed or Running, and says whether reduced motion has ended
// it before it began.
TimingSample AnimateKeyframesSample(Ctx* cx, uint32_t key, const Timing& timing,
                                    MotionStatus* status, bool* reduced);

// Samples a keyed keyframe playback and requests frames while it is active.
//
// The stable key owns the playback's start time. Re-rendering with the same
// key continues that playback; it does not restart when `keyframes` or
// `timing` is reconstructed. To replay a sequence, include an
// application-owned generation in the id, for example
// MotionId("notification-enter", generation).
template <typename T>
MotionStep<T> AnimateKeyframes(Ctx* cx, uint32_t key,
                               const Keyframes<T>& keyframes,
                               const Timing& timing) {
    MotionStep<T> out;
    bool reduced = false;
    TimingSample sample =
        AnimateKeyframesSample(cx, key, timing, &out.status, &reduced);
    if (reduced) {
        out.value = keyframes.Sample(1.f);
        return out;
    }
    out.value = keyframes.Sample(sample.directedProgress);
    out.running = out.status == MotionStatus::Delayed ||
                  out.status == MotionStatus::Running;
    return out;
}

// ─── motion/presence.rs ───────────────────────────────────────────────────

enum class PresencePhase : uint8_t {
    Entering,
    Present,
    Exiting,
    Absent,
};

struct PresenceSample {
    PresencePhase phase = PresencePhase::Absent;
    float progress = 0;
    MotionStatus status = MotionStatus::Finished;

    // Everything but Absent is on screen: a surface stays mounted through
    // its exit.
    bool ShouldRender() const { return phase != PresencePhase::Absent; }
};

// PresenceState in Rust: the same shape as a value transition over 0..1,
// except that it starts from 0 so a surface that is present from the first
// frame still enters.
using PresenceState = MotionState<float>;

// The whole presence rule with the state and the clock passed in.
PresenceSample PresenceAdvance(PresenceState* st, bool present,
                               const motion::Transition& transition, double now,
                               bool reduced);

// A mount/unmount transition: the surface enters when `present` turns on
// and stays rendered, exiting, until its transition has run out.
struct Presence {
    uint32_t key = 0;
    bool present = false;
    motion::Transition transition = motion::Transition::New(0);

    static Presence New(uint32_t key, bool present) {
        Presence p;
        p.key = key;
        p.present = present;
        return p;
    }
    static Presence New(Str id, bool present) {
        return New(MotionId(id), present);
    }
    Presence Transition(const motion::Transition& t) const {
        Presence p = *this;
        p.transition = t;
        return p;
    }
    PresenceSample Sample(Ctx* cx) const;
};

// ─── motion/reveal.rs ─────────────────────────────────────────────────────

// A measured, clipped vertical reveal driven by normalized progress. The
// child is laid out at its natural height and the box around it shows
// `progress` of that, clipped; the natural height is what the last frame
// measured. Rust keeps that in the element's state from its prepaint; here
// the child reports its own box into a keyed slot, and a measurement that
// differs from the one the frame was built with asks for another frame, as
// Rust's prepaint does.
struct MotionReveal {
    static El* New(Ctx* cx, Str id, float progress, El* child);
};

// The measured natural height behind one reveal, which is what Rust keeps in
// the element's own state. It is public for the reason FrameSamplerIngest is:
// without it the measure-and-clip rule could only be driven through a real
// layout pass, and the rule is the part worth pinning.
struct MotionRevealState {
    // What the child's box came out as during the last layout, written by
    // the element itself.
    Bounds measured = {};
    // The height this frame was built with. Rust compares the two in its
    // prepaint; here the write lands after the tree is built, so the frame
    // that notices a change is the next one — and it is asked for.
    float height = 0;
    bool hasHeight = false;
};

MotionRevealState* MotionRevealStateOf(Ctx* cx, Str id);

namespace motion {

// MotionValue<T> in Rust: the value and where the transition is.
template <typename T>
using MotionValue = gpui::MotionStep<T>;

// Source-named entry point. Fetching Window separately is unnecessary here:
// Ctx carries the render window and app together, and MotionValue performs the
// same keyed-state lookup and animation-frame request as Rust's transition.
template <typename T>
T transition(Ctx* cx, TransitionId id, T target, const Transition& policy) {
    return gpui::MotionValue(cx, id.key, target, policy);
}

template <typename T>
MotionValue<T> transition_with_status(Ctx* cx, TransitionId id, T target,
                                      const Transition& policy) {
    return MotionValueWithStatus(cx, id.key, target, policy);
}

template <typename T>
MotionValue<T> animate_keyframes(Ctx* cx, TransitionId id,
                                 const Keyframes<T>& keyframes,
                                 const Timing& timing) {
    return AnimateKeyframes(cx, id.key, keyframes, timing);
}

// Spring lives at gpui scope for existing callers; make the Rust module path
// available as well without duplicating policy or state.
using Spring = gpui::Spring;
using SpringState = gpui::SpringState;
using SpringStep = gpui::SpringStep;

inline float spring(Ctx* cx, TransitionId id, float target,
                    const Spring& policy) {
    return SpringValue(cx, id.key, target, policy);
}

} // namespace motion

} // namespace gpui
#endif // GPUI_BASE_MOTION_H_
