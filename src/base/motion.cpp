#include "base/motion.h"
#include "gpui/platform.h"

#include <math.h>

namespace gpui {

// A float that is neither NaN nor an infinity. `f32::is_finite` in Rust,
// written out rather than reaching for <cmath>'s isfinite, which a fast-math
// build is allowed to fold away.
static bool FiniteF(float v) {
    return v - v == 0.f;
}

uint32_t MotionId(Str id) {
    return (uint32_t)HashClickId(id);
}

uint32_t MotionName(Ctx* cx, Str name) {
    return KeyedName(cx, name);
}

uint32_t MotionId(Str id, Str channel) {
    // ElementId::NamedChild(id, channel): the channel is part of the key, so
    // one element can transition its fill and its offset separately.
    return (uint32_t)HashClickId(id) * 31u + (uint32_t)HashClickId(channel);
}

motion::TransitionId::TransitionId(Str id) : key(MotionId(id)) {}

motion::TransitionId::TransitionId(Str id, Str channel)
    : key(MotionId(id, channel)) {}

// ─── motion/easing.rs ─────────────────────────────────────────────────────

const char* EasingErrorMessage(EasingError e) {
    switch (e) {
        case EasingError::InvalidBezierX:
        case EasingError::InvalidBezierControlPoint:
            return "cubic Bézier control points must be finite and x must be "
                   "within 0..=1";
        case EasingError::InvalidStepCount:
            return "step easing requires a valid step count";
        case EasingError::InvalidLinearStops:
            return "linear easing stops are invalid";
    }
    return "";
}

EasingResult Easing::CubicBezier(float x1, float y1, float x2, float y2) {
    EasingResult r;
    if (!FiniteF(x1) || !FiniteF(x2) || x1 < 0.f || x1 > 1.f || x2 < 0.f ||
        x2 > 1.f || !FiniteF(y1) || !FiniteF(y2)) {
        r.error = EasingError::InvalidBezierControlPoint;
        return r;
    }
    Easing e = Of(EasingKind::CubicBezier);
    e.x1 = x1;
    e.y1 = y1;
    e.x2 = x2;
    e.y2 = y2;
    r.ok = true;
    r.value = e;
    return r;
}

EasingResult Easing::Steps(uint32_t count, StepPosition position) {
    EasingResult r;
    if (count == 0 || (position == StepPosition::JumpNone && count == 1)) {
        r.error = EasingError::InvalidStepCount;
        return r;
    }
    Easing e = Of(EasingKind::Steps);
    e.count = count;
    e.position = position;
    r.ok = true;
    r.value = e;
    return r;
}

EasingResult Easing::LinearStops(Arena* a, const LinearStop* stops,
                                 int32_t len) {
    EasingResult r;
    if (len < 2 || !a) {
        r.error = EasingError::InvalidLinearStops;
        return r;
    }
    for (int32_t i = 0; i < len; i++) {
        if (!FiniteF(stops[i].output) ||
            (stops[i].hasInput && !FiniteF(stops[i].input))) {
            r.error = EasingError::InvalidLinearStops;
            return r;
        }
    }
    // The resolved pairs, and the inputs while they are still being filled
    // in. Rust mutates the caller's Vec; the input here is borrowed, so the
    // working copy is the arena array the result keeps.
    float* pairs = (float*)Alloc(a, (int)sizeof(float) * len * 2);
    if (!pairs) {
        r.error = EasingError::InvalidLinearStops;
        return r;
    }
    bool* has = (bool*)Alloc(a, (int)sizeof(bool) * len);
    if (!has) {
        r.error = EasingError::InvalidLinearStops;
        return r;
    }
    for (int32_t i = 0; i < len; i++) {
        pairs[i * 2] = stops[i].input;
        pairs[i * 2 + 1] = stops[i].output;
        has[i] = stops[i].hasInput;
    }
    // An omitted first position is 0 and an omitted last one is 1, so the
    // curve always spans the whole progress.
    int32_t last = len - 1;
    if (!has[0]) {
        pairs[0] = 0.f;
        has[0] = true;
    }
    if (!has[last]) {
        pairs[last * 2] = 1.f;
        has[last] = true;
    }
    // Every run of omitted positions is spread evenly between the two given
    // ones around it.
    int32_t anchor = 0;
    while (anchor < last) {
        int32_t next = -1;
        for (int32_t ix = anchor + 1; ix <= last; ix++) {
            if (has[ix]) {
                next = ix;
                break;
            }
        }
        if (next < 0) {
            r.error = EasingError::InvalidLinearStops;
            return r;
        }
        float from = pairs[anchor * 2];
        float to = pairs[next * 2];
        if (from < 0.f || from > 1.f || to < 0.f || to > 1.f || to < from) {
            r.error = EasingError::InvalidLinearStops;
            return r;
        }
        float span = (float)(next - anchor);
        for (int32_t ix = anchor + 1; ix < next; ix++) {
            float offset = (float)(ix - anchor);
            pairs[ix * 2] = from + (to - from) * offset / span;
            has[ix] = true;
        }
        anchor = next;
    }
    Easing e = Of(EasingKind::LinearStops);
    e.stops = pairs;
    e.stopsLen = len;
    r.ok = true;
    r.value = e;
    return r;
}

float Easing::Sample(float progress) const {
    progress = ClampF01(progress);
    switch (kind) {
        case EasingKind::Linear:
            return progress;
        case EasingKind::Ease:
            return gpui::CubicBezier(0.25f, 0.1f, 0.25f, 1.f, progress);
        case EasingKind::EaseIn:
            return gpui::CubicBezier(0.42f, 0.f, 1.f, 1.f, progress);
        case EasingKind::EaseOut:
            return gpui::CubicBezier(0.f, 0.f, 0.58f, 1.f, progress);
        case EasingKind::EaseInOut:
            return gpui::CubicBezier(0.42f, 0.f, 0.58f, 1.f, progress);
        case EasingKind::CubicBezier:
            return gpui::CubicBezier(x1, y1, x2, y2, progress);
        case EasingKind::Steps: {
            float steps = (float)count;
            float jumps = steps;
            float offset = 0.f;
            switch (position) {
                case StepPosition::JumpStart:
                    jumps = steps;
                    offset = 1.f;
                    break;
                case StepPosition::JumpEnd:
                    jumps = steps;
                    offset = 0.f;
                    break;
                case StepPosition::JumpNone:
                    jumps = steps - 1.f;
                    offset = 0.f;
                    break;
                case StepPosition::JumpBoth:
                    jumps = steps + 1.f;
                    offset = 1.f;
                    break;
            }
            float step = floorf(progress * steps) + offset;
            if (step < 0.f) {
                step = 0.f;
            }
            if (step > jumps) {
                step = jumps;
            }
            return jumps > 0.f ? step / jumps : 0.f;
        }
        case EasingKind::LinearStops: {
            if (!stops || stopsLen <= 0) {
                return progress;
            }
            // partition_point(|(input, _)| *input <= progress).
            int32_t upper = 0;
            while (upper < stopsLen && stops[upper * 2] <= progress) {
                upper++;
            }
            if (upper == 0) {
                return stops[1];
            }
            if (upper == stopsLen) {
                return stops[(stopsLen - 1) * 2 + 1];
            }
            float ax = stops[(upper - 1) * 2];
            float ay = stops[(upper - 1) * 2 + 1];
            float bx = stops[upper * 2];
            float by = stops[upper * 2 + 1];
            if (ax == bx) {
                return by;
            }
            return ay + (by - ay) * ((progress - ax) / (bx - ax));
        }
        case EasingKind::Custom:
            return custom ? custom(progress) : progress;
    }
    return progress;
}

// ─── motion/timing.rs ─────────────────────────────────────────────────────

bool SignedDuration::ActiveElapsed(float elapsedMs, float* out) const {
    if (negative) {
        // A negative delay starts the motion inside its active interval.
        *out = elapsedMs + ms;
        return true;
    }
    // checked_sub: nothing yet while the delay is still running.
    if (elapsedMs < ms) {
        return false;
    }
    *out = elapsedMs - ms;
    return true;
}

float Timing::Directed(uint64_t iteration, float progress) const {
    bool reverse = false;
    switch (direction) {
        case PlaybackDirection::Normal:
            reverse = false;
            break;
        case PlaybackDirection::Reverse:
            reverse = true;
            break;
        case PlaybackDirection::Alternate:
            reverse = iteration % 2 == 1;
            break;
        case PlaybackDirection::AlternateReverse:
            reverse = iteration % 2 == 0;
            break;
    }
    return reverse ? 1.f - progress : progress;
}

TimingSample Timing::AfterSample(uint64_t count) const {
    uint64_t iteration = count > 0 ? count - 1 : 0;
    TimingSample s;
    s.phase = MotionPhase::After;
    s.directedProgress = easing.Sample(Directed(iteration, 1.f));
    s.iteration = iteration;
    s.active = false;
    s.finished = true;
    return s;
}

TimingSample Timing::Sample(float elapsedMs) const {
    float active = 0;
    if (!delay.ActiveElapsed(elapsedMs, &active)) {
        TimingSample s;
        s.phase = MotionPhase::Before;
        s.directedProgress = easing.Sample(Directed(0, 0.f));
        return s;
    }
    bool finite = !iterations.infinite;
    uint64_t count = iterations.count;
    if (durationMs <= 0 || (finite && count == 0)) {
        return AfterSample(finite ? count : 1);
    }
    if (active < 0) {
        active = 0;
    }
    // Rust works in whole nanoseconds; this is the same arithmetic in
    // milliseconds, in double so a long-running infinite playback keeps its
    // precision across the modulo.
    double elapsed = (double)active;
    double duration = (double)durationMs;
    if (finite && elapsed >= duration * (double)count) {
        return AfterSample(count);
    }
    double whole = floor(elapsed / duration);
    uint64_t iteration = whole > 0 ? (uint64_t)whole : 0;
    double within = elapsed - whole * duration;
    float progress = (float)(within / duration);
    TimingSample s;
    s.phase = MotionPhase::Active;
    s.directedProgress = easing.Sample(Directed(iteration, progress));
    s.iteration = iteration;
    s.active = true;
    s.finished = false;
    return s;
}

// ─── motion/stagger.rs ────────────────────────────────────────────────────

float Stagger::Delay(int32_t index, int32_t count) const {
    if (count <= 0) {
        return 0.f;
    }
    if (index > count - 1) {
        index = count - 1;
    }
    if (index < 0) {
        index = 0;
    }
    int32_t from = 0;
    switch (origin.kind) {
        case StaggerOrigin::First:
            from = 0;
            break;
        case StaggerOrigin::Last:
            from = count - 1;
            break;
        case StaggerOrigin::Center:
            from = (count - 1) / 2;
            break;
        case StaggerOrigin::Index:
            from = origin.index < count - 1 ? origin.index : count - 1;
            if (from < 0) {
                from = 0;
            }
            break;
    }
    int32_t distance = index - from;
    if (distance < 0) {
        distance = -distance;
    }
    return intervalMs * (float)distance;
}

// ─── motion.rs ────────────────────────────────────────────────────────────

float MotionProgress(const Motion& m, float elapsedMs, float durationMs,
                     MotionStatus* status) {
    float active = 0;
    if (!m.Delay().ActiveElapsed(elapsedMs, &active)) {
        *status = MotionStatus::Delayed;
        return 0.f;
    }
    if (durationMs <= 0 || active >= durationMs) {
        *status = MotionStatus::Finished;
        return 1.f;
    }
    *status = MotionStatus::Running;
    return active / durationMs;
}

float MotionSample(const Motion& m, float progress) {
    return m.easing.Sample(progress);
}

static bool gReducedAsked = false;
static bool gReduced = false;
static bool gHasApplied = false;
static bool gApplied = false;
static bool gFollowing = false;

bool MotionReduced() {
    return gReduced;
}

void MotionSetReduced(bool on) {
    gReducedAsked = true;
    gReduced = on;
}

void MotionResetReduceForTest() {
    gReducedAsked = false;
    gReduced = false;
    gHasApplied = false;
    gApplied = false;
}

void ApplyReduceMotionPreference(bool reduce) {
    bool applied = gHasApplied ? gApplied : false;
    if (gReduced != applied) {
        return;
    }
    gReducedAsked = true;
    gReduced = reduce;
    gHasApplied = true;
    gApplied = reduce;
}

void ApplySystemReduceMotion() {
    bool reduce = false;
    if (PlatReduceMotionKnown(&reduce)) {
        ApplyReduceMotionPreference(reduce);
    }
    if (!gFollowing) {
        gFollowing = true;
        PlatReduceMotionFollow(&ApplyReduceMotionPreference);
    }
}

// The clock a loop or a one-shot runs on: when it started, and nothing else —
// neither has a target to leave from.
struct MotionLoopState {
    double startedAt = 0;
    bool init = false;
};

// `with_animation`, which is not `motion::transition`: Rust gates
// `reduce_motion` inside `motion::transition` alone (motion.rs, the one
// `cx.reduce_motion()` in the crate), and every `with_animation` — the
// dialog's slide-down and fade, the sheet's slide — plays whatever the
// desktop's animation setting says. This had checked it, so on a machine
// with animation effects off (SPI_GETCLIENTAREAANIMATION false, which is
// not rare) a dialog appeared in place, fully opaque, with nothing having
// moved. `MotionTransition` keeps the check, where Rust has it.
float MotionAppear(Ctx* cx, uint32_t key, float durationMs, EaseFn ease) {
    if (durationMs <= 0) {
        return 1.f;
    }
    auto* st =
        (MotionLoopState*)MotionSlot(cx, key, (int)sizeof(MotionLoopState));
    if (!st) {
        return 1.f;
    }
    double now = MotionNow(cx);
    if (!st->init) {
        st->init = true;
        st->startedAt = now;
    }
    float elapsedMs = (float)((now - st->startedAt) * 1000.0);
    float t = elapsedMs / durationMs;
    if (t >= 1.f) {
        return 1.f;
    }
    if (t < 0.f) {
        t = 0.f;
    }
    MotionWantsFrame(cx);
    return ease ? ease(t) : t;
}

float MotionRepeat(Ctx* cx, uint32_t key, float periodMs, EaseFn ease) {
    if (periodMs <= 0) {
        return 0.f;
    }
    auto* st =
        (MotionLoopState*)MotionSlot(cx, key, (int)sizeof(MotionLoopState));
    if (!st) {
        return 0.f;
    }
    double now = MotionNow(cx);
    if (!st->init) {
        st->init = true;
        st->startedAt = now;
    }
    float elapsedMs = (float)((now - st->startedAt) * 1000.0);
    float phase = elapsedMs / periodMs;
    // The whole turns come off rather than the phase growing without end,
    // which would lose its precision within a day of running.
    phase -= (float)(int)phase;
    if (phase < 0) {
        phase = 0;
    }
    MotionWantsFrame(cx);
    return ease ? ease(phase) : phase;
}

double MotionNow(Ctx* cx) {
    // The frame's own instant, which WindowDrawFrame stamps before it renders.
    if (cx && cx->win && cx->win->frameNow > 0) {
        return cx->win->frameNow;
    }
    return TimeNow();
}

void* MotionSlot(Ctx* cx, uint32_t key, int size) {
    return cx ? WindowMotionState(cx->win, key, size) : nullptr;
}

void MotionWantsFrame(Ctx* cx) {
    if (cx) {
        WindowRequestAnimationFrame(cx->win);
    }
}

// ─── animate_keyframes ────────────────────────────────────────────────────

TimingSample AnimateKeyframesSample(Ctx* cx, uint32_t key, const Timing& timing,
                                    MotionStatus* status, bool* reduced) {
    TimingSample sample;
    auto* st =
        (KeyframePlayback*)MotionSlot(cx, key, (int)sizeof(KeyframePlayback));
    double now = MotionNow(cx);
    if (st && !st->init) {
        st->init = true;
        st->startedAt = now;
    }
    if (MotionReduced()) {
        // A keyframe playback under reduced motion shows its end state and
        // asks for nothing.
        *reduced = true;
        *status = MotionStatus::Finished;
        return sample;
    }
    *reduced = false;
    float elapsedMs = st ? (float)((now - st->startedAt) * 1000.0) : 0.f;
    if (elapsedMs < 0) {
        elapsedMs = 0;
    }
    sample = timing.Sample(elapsedMs);
    switch (sample.phase) {
        case MotionPhase::Before:
            *status = MotionStatus::Delayed;
            break;
        case MotionPhase::Active:
            *status = MotionStatus::Running;
            break;
        case MotionPhase::After:
            *status = MotionStatus::Finished;
            break;
    }
    if (*status == MotionStatus::Delayed || *status == MotionStatus::Running) {
        MotionWantsFrame(cx);
    }
    return sample;
}

// ─── motion/presence.rs ───────────────────────────────────────────────────

static PresenceSample PresenceStable(bool present) {
    PresenceSample s;
    s.phase = present ? PresencePhase::Present : PresencePhase::Absent;
    s.progress = present ? 1.f : 0.f;
    s.status = MotionStatus::Finished;
    return s;
}

PresenceSample PresenceAdvance(PresenceState* st, bool present,
                               const motion::Transition& transition, double now,
                               bool reduced) {
    float target = present ? 1.f : 0.f;
    if (!st->init) {
        // Unlike a value transition, presence always starts from absent: a
        // surface that is present on its first frame still enters.
        st->init = true;
        st->from = 0.f;
        st->target = target;
        st->startedAt = now;
        st->reversingFactor = 1.f;
        st->durationMs = transition.durationMs;
    }
    if (reduced || transition.durationMs <= 0) {
        if (st->from != target || st->target != target) {
            st->from = target;
            st->target = target;
            st->startedAt = now;
            st->reversingFactor = 1.f;
            st->durationMs = transition.durationMs;
        }
        return PresenceStable(present);
    }
    double elapsedS = now - st->startedAt;
    float elapsedMs = elapsedS > 0 ? (float)(elapsedS * 1000.0) : 0.f;
    MotionStatus status;
    float progress =
        MotionProgress(transition, elapsedMs, st->durationMs, &status);
    float eased = MotionSample(transition, progress);
    float sampled = st->from + (st->target - st->from) * eased;
    float value = sampled;
    if (st->target != target) {
        bool reversing = target == st->from;
        float factor = 1.f;
        if (reversing) {
            factor = ClampF01(eased * st->reversingFactor +
                              (1.f - st->reversingFactor));
        }
        float duration = transition.durationMs * factor;
        st->from = sampled;
        st->target = target;
        st->startedAt = now;
        st->reversingFactor = factor;
        st->durationMs = duration;
        MotionStatus initialStatus;
        float initial =
            MotionProgress(transition, 0.f, duration, &initialStatus);
        value =
            sampled + (target - sampled) * MotionSample(transition, initial);
        status = initialStatus;
    }
    if (status == MotionStatus::Finished) {
        return PresenceStable(present);
    }
    PresenceSample out;
    out.phase = present ? PresencePhase::Entering : PresencePhase::Exiting;
    out.progress = value;
    out.status = status;
    return out;
}

PresenceSample Presence::Sample(Ctx* cx) const {
    auto* st = (PresenceState*)MotionSlot(cx, key, (int)sizeof(PresenceState));
    if (!st) {
        return PresenceStable(present);
    }
    PresenceSample s = PresenceAdvance(st, present, transition, MotionNow(cx),
                                       MotionReduced());
    if (s.status == MotionStatus::Delayed ||
        s.status == MotionStatus::Running) {
        MotionWantsFrame(cx);
    }
    return s;
}

// ─── motion/reveal.rs ─────────────────────────────────────────────────────

// The measured natural height of one reveal, kept where GPUI keeps the
// element state Rust's `with_element_state` reaches for.
MotionRevealState* MotionRevealStateOf(Ctx* cx, Str id) {
    return (MotionRevealState*)MotionSlot(cx, MotionName(cx, id),
                                          (int)sizeof(MotionRevealState));
}

El* MotionReveal::New(Ctx* cx, Str id, float progress, El* child) {
    Arena* a = cx->a;
    progress = ClampF01(progress);
    MotionRevealState* st = MotionRevealStateOf(cx, id);
    El* box = Div(a)->W(kFill)->ClipY();
    if (!st) {
        return box->Child(child);
    }
    // The measured height is only used by the next layout, so ask for that
    // frame, or the new height never gets painted.
    if (st->measured.h > 0 && st->measured.h != st->height) {
        st->height = st->measured.h;
        st->hasHeight = true;
        MotionWantsFrame(cx);
    }
    if (st->hasHeight) {
        box->H(st->height * progress);
    } else if (progress <= 0.f) {
        // Not measured yet and nothing to show: zero, so a closed reveal
        // does not flash its content on its first frame.
        box->H(0);
    }
    // Not measured yet with progress above zero lets the content lay itself
    // out, which is what measures it.
    if (child) {
        child->BoundsOut(&st->measured);
        box->Child(child);
    }
    return box;
}

// ─── springs ──────────────────────────────────────────────────────────────

const char* SpringErrorMessage(SpringError e) {
    switch (e) {
        case SpringError::InvalidDamping:
            return "spring damping must be finite and non-negative";
        case SpringError::InvalidEpsilon:
            return "spring epsilon must be finite and greater than zero";
    }
    return "";
}

SpringResult Spring::TryWithDamping(float ratio) const {
    SpringResult r;
    if (!FiniteF(ratio) || ratio < 0.f) {
        r.error = SpringError::InvalidDamping;
        return r;
    }
    r.ok = true;
    r.value = *this;
    r.value.damping = ratio;
    return r;
}

Spring Spring::WithDamping(float ratio) const {
    // Rust panics here; there are no exceptions in this tree, so an invalid
    // ratio leaves the spring as it was rather than taking the process down.
    // TryWithDamping is the checked form for a value that is not a constant.
    SpringResult r = TryWithDamping(ratio);
    return r.ok ? r.value : *this;
}

SpringResult Spring::TryWithEpsilon(float eps) const {
    SpringResult r;
    if (!FiniteF(eps) || eps <= 0.f) {
        r.error = SpringError::InvalidEpsilon;
        return r;
    }
    r.ok = true;
    r.value = *this;
    r.value.epsilon = eps;
    return r;
}

Spring Spring::WithEpsilon(float eps) const {
    SpringResult r = TryWithEpsilon(eps);
    return r.ok ? r.value : *this;
}

// The damped harmonic oscillator, stepped exactly rather than integrated: at
// these frame times an Euler step visibly changes the motion with the frame
// rate, and the closed form does not. Mass is 1, so the stiffness is w0 * w0
// and the damping coefficient 2 * zeta * w0.
//
// x is the displacement from the target, v the velocity. Each case is the
// standard solution of x'' + 2*zeta*w0*x' + w0^2*x = 0 for the initial
// conditions (x, v).
static void SpringStepExact(float w0, float zeta, float dt, float* x,
                            float* v) {
    float x0 = *x;
    float v0 = *v;
    if (dt <= 0 || w0 <= 0) {
        return;
    }
    if (zeta < 1.f - 1e-4f) {
        // Underdamped: it passes the target and comes back.
        float wd = w0 * sqrtf(1.f - zeta * zeta);
        float e = expf(-zeta * w0 * dt);
        float c1 = x0;
        float c2 = (v0 + zeta * w0 * x0) / wd;
        float cosd = cosf(wd * dt);
        float sind = sinf(wd * dt);
        *x = e * (c1 * cosd + c2 * sind);
        *v = e * ((c2 * wd - zeta * w0 * c1) * cosd -
                  (c1 * wd + zeta * w0 * c2) * sind);
        return;
    }
    if (zeta > 1.f + 1e-4f) {
        // Overdamped: two real rates, and it crawls in on the slower one.
        float r = w0 * sqrtf(zeta * zeta - 1.f);
        float r1 = -zeta * w0 + r;
        float r2 = -zeta * w0 - r;
        float c2 = (v0 - r1 * x0) / (r2 - r1);
        float c1 = x0 - c2;
        float e1 = expf(r1 * dt);
        float e2 = expf(r2 * dt);
        *x = c1 * e1 + c2 * e2;
        *v = c1 * r1 * e1 + c2 * r2 * e2;
        return;
    }
    // Critically damped, which is the default: the fastest approach that
    // never passes the target.
    float e = expf(-w0 * dt);
    float c = v0 + w0 * x0;
    *x = (x0 + c * dt) * e;
    *v = (v0 - c * w0 * dt) * e;
}

SpringStep SpringAdvance(SpringState* st, float target, const Spring& s,
                         double now, bool reduced) {
    SpringStep out;
    out.value = target;
    if (!st->init) {
        st->init = true;
        st->position = target;
        st->velocity = 0;
        st->target = target;
        st->updatedAt = now;
        return out;
    }
    // The common case by far: a spring nothing is moving. It has no state to
    // advance and no frame to ask for.
    if (st->position == target && st->velocity == 0) {
        st->target = target;
        st->updatedAt = now;
        return out;
    }
    if (reduced || !s.travel || s.responseMs <= 0) {
        st->position = target;
        st->velocity = 0;
        st->target = target;
        st->updatedAt = now;
        return out;
    }
    // Over the frame that just elapsed, which the *previous* target governed,
    // before adopting the new one for the frame to come. That is what carries
    // the velocity through a reversal.
    float dt = (float)(now - st->updatedAt);
    if (dt < 0) {
        dt = 0;
    }
    // A tab out and back is not a frame's worth of motion to catch up on.
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    float w0 = 6.2831853f / (s.responseMs / 1000.f);
    float x = st->position - st->target;
    float v = st->velocity;
    SpringStepExact(w0, s.damping, dt, &x, &v);
    float position = st->target + x;
    st->target = target;
    st->updatedAt = now;
    // Settled: within epsilon of the target, and slow enough that what is
    // left to travel is inside it too — a velocity in units per second is
    // read as the distance one response period of it would cover.
    float rest = position - target;
    float restAbs = rest < 0 ? -rest : rest;
    float speed = v < 0 ? -v : v;
    if (restAbs <= s.epsilon && speed * (s.responseMs / 1000.f) <= s.epsilon) {
        st->position = target;
        st->velocity = 0;
        out.value = target;
        return out;
    }
    st->position = position;
    st->velocity = v;
    out.value = position;
    out.running = true;
    return out;
}

float SpringValue(Ctx* cx, uint32_t key, float target, const Spring& s) {
    auto* st = (SpringState*)MotionSlot(cx, key, (int)sizeof(SpringState));
    if (!st) {
        return target;
    }
    SpringStep step =
        SpringAdvance(st, target, s, MotionNow(cx), MotionReduced());
    if (step.running) {
        MotionWantsFrame(cx);
    }
    return step.value;
}

} // namespace gpui
