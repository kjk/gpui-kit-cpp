#include "ui/speech.h"

#include "base/motion.h"
#include "gpui/paint.h"
#include "ui/button.h"
#include "ui/i18n.h"
#include "ui/theme.h"

#include <math.h>

namespace gpui {

namespace component {

// ─── recognizer.rs ────────────────────────────────────────────────────────

static SpeechError ErrorOf(SpeechErrorKind kind, Str message = {}) {
    SpeechError error;
    error.kind = kind;
    int n = len(message);
    int cap = (int)sizeof(error.message) - 1;
    if (n > cap) {
        n = cap;
    }
    if (n > 0) {
        memcpy(error.message, message.s, (size_t)n);
    }
    error.message[n] = 0;
    return error;
}

SpeechError SpeechError::PermissionDenied() {
    return ErrorOf(SpeechErrorKind::PermissionDenied);
}

SpeechError SpeechError::NoInputDevice() {
    return ErrorOf(SpeechErrorKind::NoInputDevice);
}

SpeechError SpeechError::Unsupported() {
    return ErrorOf(SpeechErrorKind::Unsupported);
}

SpeechError SpeechError::Input(Str message) {
    return ErrorOf(SpeechErrorKind::Input, message);
}

SpeechError SpeechError::Recognizer(Str message) {
    return ErrorOf(SpeechErrorKind::Recognizer, message);
}

Str SpeechError::Display(Arena* a) const {
    switch (kind) {
        case SpeechErrorKind::PermissionDenied:
            return StrL("microphone access was denied");
        case SpeechErrorKind::NoInputDevice:
            return StrL("no audio input device is available");
        case SpeechErrorKind::Unsupported:
            return StrL("speech input is not supported");
        case SpeechErrorKind::Input:
            return StrDup(a, fmt("audio input failed: %s", Str(message)));
        case SpeechErrorKind::Recognizer:
            return StrDup(a,
                          fmt("speech recognition failed: %s", Str(message)));
    }
    return {};
}

// ─── microphone.rs ────────────────────────────────────────────────────────

SpeechAudioConverter SpeechAudioConverter::New(uint32_t sourceRate,
                                               AudioFormat format) {
    uint32_t targetRate = format.sampleRate > 0 ? format.sampleRate : 1;
    if (sourceRate < 1) {
        sourceRate = 1;
    }
    SpeechAudioConverter c;
    c.step = (double)sourceRate / (double)targetRate;
    c.lowPass = sourceRate > targetRate;
    if (c.lowPass) {
        const float kTau = 6.283185307179586f;
        float cutoff = (float)targetRate * 0.45f;
        c.alpha = 1.f - expf(-kTau * cutoff / (float)sourceRate);
    }
    c.channels = format.channels > 0 ? format.channels : 1;
    return c;
}

void SpeechAudioConverter::Convert(const float* input, int n,
                                   Vec<int16_t>& out) {
    if (n <= 0 || !input) {
        return;
    }
    Vec<float> filtered;
    if (lowPass) {
        for (int i = 0; i < n; i++) {
            float x = input[i];
            for (float& stage : stages) {
                stage += alpha * (x - stage);
                x = stage;
            }
            VecAppend(filtered, x);
        }
        input = filtered.els;
    }
    // sample(ix): 0 is the last sample of the previous chunk.
    double length = (double)n;
    while (position <= length) {
        int ix = (int)floor(position);
        float frac = (float)(position - (double)ix);
        int nextIx = ix + 1 < n ? ix + 1 : n;
        float here = ix == 0 ? previous : input[ix - 1];
        float next = nextIx == 0 ? previous : input[nextIx - 1];
        float value = here * (1.f - frac) + next * frac;
        value = value < -1.f ? -1.f : (value > 1.f ? 1.f : value);
        int16_t sample = (int16_t)(value * 32767.f);
        for (int c = 0; c < channels; c++) {
            VecAppend(out, sample);
        }
        position += step;
    }
    position -= length;
    previous = input[n - 1];
}

// ─── level.rs ─────────────────────────────────────────────────────────────

// NOISE_FLOOR: raw levels below this are background noise and read as
// silence, so a quiet room shows a calm baseline instead of flickering bars.
static const float kNoiseFloor = 0.06f;
// ATTACK: how far a level moves toward a louder reading per step, almost at
// once, so peaks pop.
static const float kAttack = 0.85f;
// RELEASE: how far a level moves toward a quieter reading per step, slowly,
// so bars fall back smoothly.
static const float kRelease = 0.3f;

int SpeechLevelWindowFor(uint32_t sampleRate, uint16_t channels) {
    uint64_t perSecond = (uint64_t)sampleRate * (channels > 0 ? channels : 1);
    uint64_t window = perSecond * (uint64_t)kSpeechLevelIntervalMs / 1000;
    return window < 1 ? 1 : (int)window;
}

float SpeechLevelOfRms(double rms) {
    if (rms <= 0) {
        return 0;
    }
    double db = 20. * log10(rms);
    double level = (db + 50.) / 50.;
    return (float)(level < 0 ? 0 : (level > 1 ? 1 : level));
}

static float Gate(float level) {
    return level < kNoiseFloor ? 0.f : level;
}

float SpeechLevelSmooth(float previous, float raw) {
    float rate = raw > previous ? kAttack : kRelease;
    return previous + (raw - previous) * rate;
}

void LevelMeter::Reset(uint32_t sampleRate, uint16_t channels) {
    *this = LevelMeter{};
    window = SpeechLevelWindowFor(sampleRate, channels);
}

bool LevelMeter::Push(const int16_t* samples, int n) {
    bool recorded = false;
    for (int i = 0; i < n; i++) {
        double sample = (double)samples[i] / 32767.;
        sum += sample * sample;
        count++;
        if (count == window) {
            float raw = Gate(SpeechLevelOfRms(sqrt(sum / (double)count)));
            smoothed = SpeechLevelSmooth(smoothed, raw);
            if (nLevels == kSpeechLevelHistory) {
                first = (first + 1) % kSpeechLevelHistory;
                nLevels--;
            }
            levels[(first + nLevels) % kSpeechLevelHistory] = smoothed;
            nLevels++;
            sum = 0;
            count = 0;
            recorded = true;
        }
    }
    if (recorded) {
        lastLevelAt = TimeNow();
    }
    return recorded;
}

// ─── state.rs ─────────────────────────────────────────────────────────────

enum class SpeechOpKind : uint8_t {
    Ready,
    Hypothesis,
    Phrase,
    Finish,
    Error,
    Audio
};

struct SpeechDeferredOp {
    SpeechOpKind kind = SpeechOpKind::Ready;
    uint32_t session = 0;
    // Heap, owned: the text of a Hypothesis or a Phrase.
    Str text = {};
    // Heap, owned: the samples of an Audio.
    int16_t* samples = nullptr;
    int count = 0;
    SpeechError error = {};
};

static void OpFree(SpeechDeferredOp* op) {
    StrFree(op->text);
    free(op->samples);
    delete op;
}

static void SetOwned(Str* slot, Str value) {
    StrFree(*slot);
    *slot = len(value) > 0 ? StrDup(value) : Str{};
}

static void NotifySelf(SpeechState* s, Ctx* cx) {
    if (cx && s->self.IsValid()) {
        NotifyEntity(cx->app, s->self.id, cx->win);
    }
}

static void Emit(SpeechState* s, Ctx* cx, SpeechEvent* ev) {
    if (cx && s->self.IsValid()) {
        EntityEmit(cx->app, cx->win, s->self, ev);
    }
}

static void EmitKind(SpeechState* s, Ctx* cx, SpeechEventKind kind) {
    SpeechEvent ev;
    ev.kind = kind;
    if (kind == SpeechEventKind::Partial || kind == SpeechEventKind::Final) {
        ev.text = s->TranscriptTemp();
    }
    Emit(s, cx, &ev);
}

static void EmitError(SpeechState* s, Ctx* cx, const SpeechError& error) {
    SpeechEvent ev;
    ev.kind = SpeechEventKind::Error;
    ev.error = error;
    Emit(s, cx, &ev);
}

static void DropCapture(SpeechState* s) {
    if (s->hasCapture) {
        s->hasCapture = false;
        if (s->capture.stop) {
            s->capture.stop(s->capture.data);
        }
        s->capture = {};
    }
}

static void CancelStopTimer(SpeechState* s) {
    if (s->stopTimer && s->stopTimerWin) {
        WindowCancelTimer(s->stopTimerWin, s->stopTimer);
    }
    s->stopTimer = 0;
    s->stopTimerWin = nullptr;
}

// `self.session.take()`: capture first, then the recognition, whose drop
// cancels it.
static void DropSession(SpeechState* s) {
    if (!s->hasSession) {
        return;
    }
    s->hasSession = false;
    DropCapture(s);
    CancelStopTimer(s);
    if (s->recognition.drop) {
        s->recognition.drop(s->recognition.data);
    }
    s->recognition = {};
}

// end: tear the session down, capture first, and report how it ended.
static void End(SpeechState* s, Ctx* cx, SpeechEvent* ev) {
    DropSession(s);
    s->status = SpeechStatus::Idle;
    s->meter = LevelMeter{};
    Emit(s, cx, ev);
    NotifySelf(s, cx);
}

static void EndWith(SpeechState* s, Ctx* cx, SpeechEventKind kind) {
    SpeechEvent ev;
    ev.kind = kind;
    if (kind == SpeechEventKind::Final) {
        ev.text = s->TranscriptTemp();
    }
    End(s, cx, &ev);
}

static void Apply(SpeechState* s, Ctx* cx, const SpeechDeferredOp* op) {
    // Sinks are ignored once their session is over.
    if (!s->IsSession(op->session)) {
        return;
    }
    switch (op->kind) {
        case SpeechOpKind::Ready:
            // on_ready
            if (s->status == SpeechStatus::Connecting) {
                s->status = SpeechStatus::Recording;
                NotifySelf(s, cx);
            }
            break;
        case SpeechOpKind::Audio:
            // on_audio
            if (!SpeechStatusIsCapturing(s->status)) {
                break;
            }
            if (s->recognition.pushAudio) {
                s->recognition.pushAudio(s->recognition.data, op->samples,
                                         op->count, cx->app);
            }
            // Redraw once per new level, not per push: levels are what the
            // waveform shows.
            if (s->meter.Push(op->samples, op->count)) {
                NotifySelf(s, cx);
            }
            break;
        case SpeechOpKind::Hypothesis:
            // on_hypothesis
            SetOwned(&s->hypothesis, op->text);
            EmitKind(s, cx, SpeechEventKind::Partial);
            NotifySelf(s, cx);
            break;
        case SpeechOpKind::Phrase: {
            // on_phrase
            Str joined = StrDup(fmt("%s%s", s->committed, op->text));
            StrFree(s->committed);
            s->committed = joined;
            SetOwned(&s->hypothesis, {});
            EmitKind(s, cx, SpeechEventKind::Partial);
            NotifySelf(s, cx);
            break;
        }
        case SpeechOpKind::Finish:
            // on_finish
            EndWith(s, cx, SpeechEventKind::Final);
            break;
        case SpeechOpKind::Error: {
            // on_error
            SpeechEvent ev;
            ev.kind = SpeechEventKind::Error;
            ev.error = op->error;
            End(s, cx, &ev);
            break;
        }
    }
}

// The deferred calls, in the order they were made. One that a call applied
// here makes in turn is applied in the same pass.
static void Drain(SpeechState* s, Ctx* cx) {
    s->drainPosted = false;
    s->updateDepth++;
    for (int i = 0; i < len(s->deferred); i++) {
        SpeechDeferredOp* op = s->deferred[i];
        Apply(s, cx, op);
        OpFree(op);
    }
    s->deferred.len = 0;
    s->updateDepth--;
}

void SpeechState::OnDrain(SpeechState* self, Ctx* cx, const void*) {
    Drain(self, cx);
}

// A state's own update: sink calls made inside it wait for it to end.
struct SpeechUpdate {
    SpeechState* s;
    Ctx* cx;

    SpeechUpdate(SpeechState* state, Ctx* ctx) : s(state), cx(ctx) {
        s->updateDepth++;
    }
    ~SpeechUpdate() {
        s->updateDepth--;
        // With a window the calls were posted to it; without one this is
        // where the update ends.
        if (s->updateDepth == 0 && !s->drainPosted && len(s->deferred) > 0) {
            Drain(s, cx);
        }
    }
};

// defer_session_update: apply `op` to the state after the current update, if
// its session is still the running one.
static void Defer(Entity<SpeechState> state, App* app, SpeechDeferredOp* op) {
    SpeechState* s = app ? state.Get(app) : nullptr;
    if (!s) {
        OpFree(op);
        return;
    }
    VecAppend(s->deferred, op);
    if (s->drainPosted) {
        return;
    }
    Window* win = len(app->windows) > 0 ? app->windows[0] : nullptr;
    if (win) {
        s->drainPosted = true;
        WindowPost(win, ListenTo(state, &SpeechState::OnDrain));
        return;
    }
    if (s->updateDepth == 0) {
        Ctx cx = {app, nullptr, nullptr, {}};
        Drain(s, &cx);
    }
}

static SpeechDeferredOp* OpNew(SpeechOpKind kind, uint32_t session) {
    SpeechDeferredOp* op = new SpeechDeferredOp();
    op->kind = kind;
    op->session = session;
    return op;
}

void SpeechSink::Ready(App* app) const {
    Defer(state, app, OpNew(SpeechOpKind::Ready, session));
}

void SpeechSink::Hypothesis(Str text, App* app) const {
    SpeechDeferredOp* op = OpNew(SpeechOpKind::Hypothesis, session);
    SetOwned(&op->text, text);
    Defer(state, app, op);
}

void SpeechSink::Phrase(Str text, App* app) const {
    SpeechDeferredOp* op = OpNew(SpeechOpKind::Phrase, session);
    SetOwned(&op->text, text);
    Defer(state, app, op);
}

void SpeechSink::Finish(App* app) const {
    Defer(state, app, OpNew(SpeechOpKind::Finish, session));
}

void SpeechSink::Error(const SpeechError& error, App* app) const {
    SpeechDeferredOp* op = OpNew(SpeechOpKind::Error, session);
    op->error = error;
    Defer(state, app, op);
}

void AudioSink::Push(const int16_t* samples, int count, App* app) const {
    SpeechDeferredOp* op = OpNew(SpeechOpKind::Audio, session);
    if (count > 0 && samples) {
        op->samples = (int16_t*)malloc((size_t)count * sizeof(int16_t));
        if (op->samples) {
            memcpy(op->samples, samples, (size_t)count * sizeof(int16_t));
            op->count = count;
        }
    }
    Defer(state, app, op);
}

void AudioSink::Error(const SpeechError& error, App* app) const {
    SpeechDeferredOp* op = OpNew(SpeechOpKind::Error, session);
    op->error = error;
    Defer(state, app, op);
}

Entity<SpeechState> SpeechStateNew(App* app) {
    Entity<SpeechState> state = EntityNewState<SpeechState>(app);
    if (SpeechState* s = state.Get(app)) {
        s->self = state;
    }
    return state;
}

SpeechState::~SpeechState() {
    DropSession(this);
    for (int i = 0; i < len(deferred); i++) {
        OpFree(deferred[i]);
    }
    StrFree(committed);
    StrFree(hypothesis);
}

SpeechState* SpeechState::Recognizer(const SpeechRecognizer& value) {
    recognizer = value;
    return this;
}

SpeechState* SpeechState::Input(const AudioInput& value) {
    input = value;
    return this;
}

SpeechState* SpeechState::SystemFallback(bool value) {
    systemFallback = value;
    return this;
}

SpeechState* SpeechState::StopTimeout(int ms) {
    stopTimeoutMs = ms;
    return this;
}

// active_recognizer: the application's, else the platform's unless the
// fallback is off. There is no system recognizer in this tree, so the
// fallback finds none on every platform.
static const SpeechRecognizer* ActiveRecognizer(const SpeechState* s) {
    return s->recognizer.IsSet() ? &s->recognizer : nullptr;
}

bool SpeechState::HasRecognizer() const {
    return input.IsSet() && ActiveRecognizer(this) != nullptr;
}

bool SpeechState::IsAvailable(const App* app) const {
    const SpeechRecognizer* active = ActiveRecognizer(this);
    if (!input.IsSet() || !active) {
        return false;
    }
    return !active->isAvailable || active->isAvailable(active->data, app);
}

TempStr SpeechState::TranscriptTemp() const {
    return fmt("%s%s", committed, hypothesis);
}

void SpeechState::Start(Ctx* cx) {
    if (SpeechStatusIsActive(status)) {
        return;
    }
    SpeechUpdate update(this, cx);
    const SpeechRecognizer* active = ActiveRecognizer(this);
    if (!active || !input.IsSet()) {
        EmitError(this, cx, SpeechError::Unsupported());
        return;
    }

    nextSession++;
    uint32_t id = nextSession;
    SpeechSink sink = {self, id};
    RecognitionSession opened = {};
    SpeechError error = {};
    if (!active->start(active->data, sink, cx->app, &opened, &error)) {
        EmitError(this, cx, error);
        return;
    }

    AudioFormat format =
        active->audioFormat ? active->audioFormat(active->data) : AudioFormat{};
    AudioSink audio = {self, id};
    AudioCapture capturing = {};
    if (!input.start(input.data, format, audio, cx->app, &capturing, &error)) {
        // The recognition that did open is dropped, which cancels it.
        if (opened.drop) {
            opened.drop(opened.data);
        }
        EmitError(this, cx, error);
        return;
    }

    status = SpeechStatus::Connecting;
    SetOwned(&committed, {});
    SetOwned(&hypothesis, {});
    meter.Reset(format.sampleRate, format.channels);
    hasSession = true;
    sessionId = id;
    capture = capturing;
    hasCapture = true;
    recognition = opened;
    EmitKind(this, cx, SpeechEventKind::Started);
    NotifySelf(this, cx);
}

void SpeechState::OnStopTimeout(SpeechState* self, Ctx* cx, const TickEvent*,
                                int64_t session) {
    self->stopTimer = 0;
    self->stopTimerWin = nullptr;
    if (self->IsSession((uint32_t)session)) {
        SpeechUpdate update(self, cx);
        EndWith(self, cx, SpeechEventKind::Final);
    }
}

void SpeechState::Stop(Ctx* cx) {
    if (!SpeechStatusIsCapturing(status) || !hasSession) {
        return;
    }
    SpeechUpdate update(this, cx);
    DropCapture(this);
    if (recognition.finish) {
        recognition.finish(recognition.data, cx->app);
    }

    // The timeout is a window's timer, which is the clock this tree has: the
    // window the stop came from, or any window of the application.
    Window* win = cx->win;
    if (!win && len(cx->app->windows) > 0) {
        win = cx->app->windows[0];
    }
    CancelStopTimer(this);
    if (win) {
        stopTimerWin = win;
        stopTimer = WindowSetTimeout(
            win, stopTimeoutMs > 0 ? stopTimeoutMs : 1,
            ListenTo(self, &SpeechState::OnStopTimeout, (int64_t)sessionId));
    }

    status = SpeechStatus::Stopping;
    NotifySelf(this, cx);
}

void SpeechState::Cancel(Ctx* cx) {
    if (!SpeechStatusIsActive(status)) {
        return;
    }
    SpeechUpdate update(this, cx);
    SetOwned(&committed, {});
    SetOwned(&hypothesis, {});
    EndWith(this, cx, SpeechEventKind::Cancelled);
}

void SpeechState::Toggle(Ctx* cx) {
    if (SpeechStatusIsActive(status)) {
        Stop(cx);
    } else {
        Start(cx);
    }
}

void SpeechState::OnToggle(SpeechState* self, Ctx* cx, const ClickEvent*) {
    self->Toggle(cx);
}

// ─── button.rs ────────────────────────────────────────────────────────────

SpeechButton* SpeechButton::New(Ctx* cx, Entity<SpeechState> state) {
    SpeechButton* b = ArenaNew<SpeechButton>(cx->a);
    b->a = cx->a;
    b->cx = cx;
    // ("speech-button", state.entity_id()).
    b->id =
        StrDup(cx->a, fmt("speech-button-%d-%u", state.id.index, state.id.gen));
    b->state = state;
    return b;
}

SpeechButton* SpeechButton::ShowWhenUnsupported(bool show) {
    showWhenUnsupported = show;
    return this;
}

SpeechButton* SpeechButton::WithSize(UiSize s) {
    size = s;
    return this;
}

SpeechButton* SpeechButton::Disabled(bool v) {
    disabled = v;
    return this;
}

SpeechButton* SpeechButton::Refine(const Style& refinement, uint32_t fields) {
    StyleApplyFields(&style, refinement, fields);
    styleSet |= fields;
    return this;
}

El* SpeechButton::IntoEl() {
    const SpeechState* s = state.Get(cx->app);
    SpeechStatus status = s ? s->Status() : SpeechStatus::Idle;
    bool supported = s && s->HasRecognizer();
    if (!supported && !showWhenUnsupported) {
        return Div(a);
    }
    // A running session stays stoppable even if the recognizer turns
    // unavailable mid-way.
    bool available =
        SpeechStatusIsActive(status) || (s && s->IsAvailable(cx->app));
    bool capturing = SpeechStatusIsCapturing(status);
    Str label = !available  ? Tr("Speech.Unavailable")
                : capturing ? Tr("Speech.Stop")
                            : Tr("Speech.Start");

    El* e = Button::New(cx, id)
                ->Ghost()
                ->WithSize(size)
                ->Icon(capturing ? IconName::Square : IconName::Mic)
                ->Selected(capturing)
                ->Loading(status == SpeechStatus::Stopping)
                ->Disabled(disabled || !available)
                ->Tooltip(label)
                ->AccessibilityLabel(label)
                ->OnClick(ListenTo(state, &SpeechState::OnToggle))
                ->IntoEl();
    if (styleSet) {
        e->Refine(style, styleSet);
    }
    return e;
}

// ─── waveform.rs ──────────────────────────────────────────────────────────

// DEFAULT_WIDTH: the width of a waveform the caller does not size, 24 bars
// at the smaller sizes.
static const float kWaveformDefaultWidth = 96.f;

SpeechWaveform* SpeechWaveform::New(Ctx* cx, Entity<SpeechState> state) {
    SpeechWaveform* w = ArenaNew<SpeechWaveform>(cx->a);
    w->a = cx->a;
    w->cx = cx;
    w->state = state;
    return w;
}

SpeechWaveform* SpeechWaveform::WithSize(UiSize s) {
    size = s;
    return this;
}

SpeechWaveform* SpeechWaveform::Refine(const Style& refinement,
                                       uint32_t fields) {
    StyleApplyFields(&style, refinement, fields);
    styleSet |= fields;
    return this;
}

float SpeechWaveform::Height() const {
    switch (size.kind) {
        case UiSize::Kind::Size:
            return size.pixels;
        case UiSize::Kind::XSmall:
            return 12.f;
        case UiSize::Kind::Small:
            return 16.f;
        case UiSize::Kind::Medium:
            return 20.f;
        case UiSize::Kind::Large:
            return 24.f;
    }
    return 20.f;
}

int SpeechWaveformBarRects(Bounds bounds, const float* levels, int nLevels,
                           float scroll, float bar, float gap, Bounds* out,
                           int cap) {
    float pitch = bar + gap;
    if (pitch <= 0) {
        return 0;
    }
    float height = bounds.h;
    float middle = bounds.y + height / 2.f;
    float right0 = bounds.x + bounds.w;
    // One more bar than fits, to scroll in from the trailing edge.
    float fits = floorf((bounds.w + gap) / pitch);
    int count = (int)(fits > 0 ? fits : 0) + 1;
    int n = 0;
    for (int fromEnd = 0; fromEnd < count && n < cap; fromEnd++) {
        float right = right0 - pitch * ((float)fromEnd + scroll);
        float left = right - bar;
        if (left < bounds.x - 0.5f) {
            continue;
        }
        int ix = nLevels - (fromEnd + 1);
        float level = ix >= 0 ? levels[ix] : 0.f;
        level = level < 0 ? 0 : (level > 1 ? 1 : level);
        float barHeight = height * level;
        if (barHeight < bar) {
            barHeight = bar;
        }
        out[n++] = Bounds{left, middle - barHeight / 2.f, bar, barHeight};
    }
    return n;
}

struct WaveformPaint {
    float levels[kSpeechLevelHistory] = {};
    int nLevels = 0;
    float scroll = 0;
    float bar = 2;
    Rgba color = {};
};

static void PaintWaveform(PaintCtx* ctx, El* e, void* user) {
    const WaveformPaint* w = (const WaveformPaint*)user;
    Bounds rects[kSpeechLevelHistory + 1];
    int n =
        SpeechWaveformBarRects(e->Bounds(), w->levels, w->nLevels, w->scroll,
                               w->bar, w->bar, rects, kSpeechLevelHistory + 1);
    Rgba color = PaintFade(ctx, w->color);
    for (int i = 0; i < n; i++) {
        FillRound(ctx, rects[i].x, rects[i].y, rects[i].w, rects[i].h,
                  w->bar / 2.f, color);
    }
}

El* SpeechWaveform::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    float height = Height();
    // Bars thicken with the waveform so a tall one does not read as
    // hairlines.
    float bar = roundf(height * 0.125f);
    bar = bar < 2.f ? 2.f : (bar > 4.f ? 4.f : bar);
    const SpeechState* s = state.Get(cx->app);
    bool capturing = s && SpeechStatusIsCapturing(s->Status());
    bool animate = capturing && !MotionReduced();

    WaveformPaint* paint = ArenaNew<WaveformPaint>(a);
    paint->bar = bar;
    paint->color = capturing ? th.primary : th.mutedFg;
    if (s) {
        paint->nLevels = s->LevelsLen();
        for (int i = 0; i < paint->nLevels; i++) {
            paint->levels[i] = s->LevelAt(i);
        }
        // How far the bars have scrolled toward the next level, so the motion
        // stays smooth when levels arrive slower than the display refreshes.
        if (animate && s->LastLevelAt() >= 0) {
            float elapsedMs = (float)((TimeNow() - s->LastLevelAt()) * 1000.);
            float scroll = elapsedMs / (float)kSpeechLevelIntervalMs;
            paint->scroll = scroll < 0 ? 0 : (scroll > 1 ? 1 : scroll);
        }
    }
    if (animate && cx->win) {
        WindowRequestAnimationFrame(cx->win);
    }

    El* canvas = Div(a)->SizeFull();
    canvas->customPaint = &PaintWaveform;
    canvas->customUser = paint;
    El* e = Div(a)->H(height)->W(kWaveformDefaultWidth)->Shrink0();
    if (styleSet) {
        e->Refine(style, styleSet);
    }
    return e->Child(canvas);
}

} // namespace component
} // namespace gpui
