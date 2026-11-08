#include "ui/speech.h"

#include "base/motion.h"
#include "sys/audio_input.h"
#include "sys/executor.h"
#include "gpui/paint.h"
#include "ui/button.h"
#include "ui/i18n.h"
#include "ui/icon.h"
#include "ui/theme.h"

#include <math.h>

namespace gpui {

namespace component {

// ─── recognizer.rs ────────────────────────────────────────────────────────

static SpeechError ErrorOf(SpeechErrorKind kind, Str message = {}) {
    SpeechError error;
    error.kind = kind;
    StrCopyCapped(error.message, (int)sizeof(error.message), message);
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

// The Microphone's capture: what the audio thread fills and the main thread
// drains. Rust sends each callback's frames over a channel to a foreground
// task; this is that channel and that task.
struct MicCapture {
    Mutex lock;
    // Guarded by `lock`: mono frames at the device's rate, and the stream's
    // failure if it had one.
    Vec<float> queued;
    char error[160] = {};
    bool hasError = false;
    // A drain is on its way to the main thread.
    bool posted = false;

    // Main thread only.
    AudioInputStream* stream = nullptr;
    bool stopped = false;
    SpeechAudioConverter converter = {};
    AudioSink sink = {};
    App* app = nullptr;
    Vec<float> taken;
    Vec<int16_t> converted;
};

static void MicDrain(MicCapture* c);

// Called with `lock` held: ask for a drain unless one is already coming.
static bool MicWantsPost(MicCapture* c) {
    if (c->posted) {
        return false;
    }
    c->posted = true;
    return true;
}

// Audio thread. It only copies and posts.
static void MicSamples(void* user, const float* mono, int frames) {
    MicCapture* c = (MicCapture*)user;
    c->lock.Lock();
    float* dst = VecInsertSpace(c->queued, len(c->queued), frames);
    if (dst) {
        memcpy(dst, mono, (size_t)frames * sizeof(float));
    }
    bool post = MicWantsPost(c);
    c->lock.Unlock();
    if (post) {
        ExecPost(MkFunc0(&MicDrain, c));
    }
}

static void MicError(void* user, Str message) {
    MicCapture* c = (MicCapture*)user;
    c->lock.Lock();
    StrCopyCapped(c->error, (int)sizeof(c->error), message);
    c->hasError = true;
    bool post = MicWantsPost(c);
    c->lock.Unlock();
    if (post) {
        ExecPost(MkFunc0(&MicDrain, c));
    }
}

// Main thread. The device delivers ~10 ms chunks; what has queued up goes on
// as one push so the state updates once per batch.
static void MicDrain(MicCapture* c) {
    if (c->stopped) {
        // The capture ended with this drain already posted; it was left
        // alive for exactly this.
        delete c;
        return;
    }
    char error[160] = {};
    c->taken.len = 0;
    c->lock.Lock();
    int n = len(c->queued);
    float* dst = n > 0 ? VecInsertSpace(c->taken, 0, n) : nullptr;
    if (dst) {
        memcpy(dst, c->queued.els, (size_t)n * sizeof(float));
    }
    c->queued.len = 0;
    bool hasError = c->hasError;
    if (hasError) {
        memcpy(error, c->error, sizeof(error));
        c->hasError = false;
    }
    c->posted = false;
    c->lock.Unlock();

    // The sink is a value: the session ending under these calls is fine.
    AudioSink sink = c->sink;
    App* app = c->app;
    c->converted.len = 0;
    c->converter.Convert(c->taken.els, len(c->taken), c->converted);
    if (len(c->converted) > 0) {
        sink.Push(c->converted.els, len(c->converted), app);
    }
    if (hasError) {
        sink.Error(SpeechError::Input(Str(error)), app);
    }
}

static void MicStop(void* data) {
    MicCapture* c = (MicCapture*)data;
    // No callback runs after this returns, so `posted` is settled.
    SysAudioInputStop(c->stream);
    c->stream = nullptr;
    c->lock.Lock();
    bool pending = c->posted;
    c->lock.Unlock();
    c->stopped = true;
    if (!pending) {
        delete c;
    }
}

static bool MicStart(void*, AudioFormat format, AudioSink sink, App* app,
                     AudioCapture* out, SpeechError* error) {
    MicCapture* c = new MicCapture();
    c->sink = sink;
    c->app = app;
    AudioInputCallbacks callbacks;
    callbacks.user = c;
    callbacks.samples = &MicSamples;
    callbacks.error = &MicError;
    uint32_t sourceRate = 0;
    AudioInputError failed = AudioInputError::None;
    char message[160] = {};
    // The converter is set before the first callback can ask for a drain:
    // drains run on this thread, after this call.
    c->stream = SysAudioInputStart(callbacks, &sourceRate, &failed, message,
                                   (int)sizeof(message));
    if (!c->stream) {
        delete c;
        switch (failed) {
            case AudioInputError::PermissionDenied:
                *error = SpeechError::PermissionDenied();
                break;
            case AudioInputError::NoInputDevice:
                *error = SpeechError::NoInputDevice();
                break;
            case AudioInputError::Unsupported:
                *error = SpeechError::Unsupported();
                break;
            default:
                *error = SpeechError::Input(Str(message));
                break;
        }
        return false;
    }
    c->converter = SpeechAudioConverter::New(sourceRate, format);
    out->data = c;
    out->stop = &MicStop;
    return true;
}

bool Microphone::IsSupported() {
    return SysAudioInputAvailable();
}

TempStr Microphone::DeviceNameTemp() {
    char name[256];
    if (!SysAudioInputDeviceName(name, (int)sizeof(name))) {
        return {};
    }
    return StrDupTemp(Str(name));
}

AudioInput Microphone::Input() {
    AudioInput input;
    input.start = &MicStart;
    return input;
}

// ─── system/mod.rs ────────────────────────────────────────────────────────

// One session of the platform recognizer, reporting to the sink.
struct SystemSession {
    SpeechSink sink = {};
    App* app = nullptr;
    SysSpeechSession* session = nullptr;
};

static void SystemReady(void* user) {
    SystemSession* s = (SystemSession*)user;
    s->sink.Ready(s->app);
}

static void SystemHypothesis(void* user, Str text) {
    SystemSession* s = (SystemSession*)user;
    s->sink.Hypothesis(text, s->app);
}

static void SystemPhrase(void* user, Str text) {
    SystemSession* s = (SystemSession*)user;
    s->sink.Phrase(text, s->app);
}

static void SystemFinish(void* user) {
    SystemSession* s = (SystemSession*)user;
    s->sink.Finish(s->app);
}

static SpeechError SystemErrorOf(SysSpeechError kind, Str message) {
    switch (kind) {
        case SysSpeechError::PermissionDenied:
            return SpeechError::PermissionDenied();
        case SysSpeechError::NoInputDevice:
            return SpeechError::NoInputDevice();
        case SysSpeechError::Unsupported:
            return SpeechError::Unsupported();
        case SysSpeechError::Recognizer:
            break;
    }
    return SpeechError::Recognizer(message);
}

static void SystemError(void* user, SysSpeechError kind, Str message) {
    SystemSession* s = (SystemSession*)user;
    s->sink.Error(SystemErrorOf(kind, message), s->app);
}

static void SystemPushAudio(void* data, const int16_t* samples, int count,
                            App*) {
    SysSpeechSessionPushAudio(((SystemSession*)data)->session, samples, count);
}

static void SystemSessionFinish(void* data, App*) {
    SysSpeechSessionFinish(((SystemSession*)data)->session);
}

static void SystemSessionDrop(void* data) {
    SystemSession* s = (SystemSession*)data;
    SysSpeechSessionDrop(s->session);
    delete s;
}

static SysSpeechRecognizer* SystemPlatform(SystemRecognizer* r) {
    if (!r->platformMade) {
        r->platformMade = true;
        r->platform = SysSpeechRecognizerNew(r->locale);
    }
    return r->platform;
}

static bool SystemIsAvailable(void* data, const App*) {
    return SysSpeechRecognizerAvailable(
        SystemPlatform((SystemRecognizer*)data));
}

static bool SystemStart(void* data, SpeechSink sink, App* app,
                        RecognitionSession* out, SpeechError* error) {
    SysSpeechRecognizer* platform = SystemPlatform((SystemRecognizer*)data);
    if (!platform) {
        *error = SpeechError::Unsupported();
        return false;
    }
    SystemSession* s = new SystemSession();
    s->sink = sink;
    s->app = app;
    SysSpeechEvents events;
    events.user = s;
    events.ready = &SystemReady;
    events.hypothesis = &SystemHypothesis;
    events.phrase = &SystemPhrase;
    events.finish = &SystemFinish;
    events.error = &SystemError;
    SysSpeechError kind = SysSpeechError::Unsupported;
    char message[600] = {};
    s->session = SysSpeechSessionStart(platform, events, &kind, message,
                                       (int)sizeof(message));
    if (!s->session) {
        delete s;
        *error = SystemErrorOf(kind, Str(message));
        return false;
    }
    out->data = s;
    out->pushAudio = &SystemPushAudio;
    out->finish = &SystemSessionFinish;
    out->drop = &SystemSessionDrop;
    return true;
}

SystemRecognizer::~SystemRecognizer() {
    SysSpeechRecognizerFree(platform);
    StrFree(locale);
}

SystemRecognizer* SystemRecognizer::Locale(Str value) {
    StrFree(locale);
    locale = len(value) > 0 ? StrDup(value) : Str{};
    SysSpeechRecognizerFree(platform);
    platform = nullptr;
    platformMade = false;
    return this;
}

bool SystemRecognizer::IsSupported() {
    return SystemPlatform(this) != nullptr;
}

SpeechRecognizer SystemRecognizer::AsRecognizer() {
    SpeechRecognizer r;
    r.data = this;
    // audio_format: 16 kHz mono on every platform, which is the default.
    r.isAvailable = &SystemIsAvailable;
    r.start = &SystemStart;
    return r;
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
    return PushAt(samples, n, TimeNow());
}

void LevelMeter::AlignPlayhead(double now) {
    if (anchor < 0) {
        // The first level arrives on time by definition.
        anchor = now - (double)kSpeechLevelIntervalMs / 1000.;
        return;
    }
    double error = (now - anchor) - (double)recorded * pace;
    double interval = (double)kSpeechLevelIntervalMs / 1000.;
    double next = pace + error * kSpeechPlayheadPaceGain;
    double lo = interval / 2.;
    double hi = interval * 2.;
    if (next < lo) {
        next = lo;
    } else if (next > hi) {
        next = hi;
    }
    // Re-anchor so the playhead's position at `now` is unchanged by the new
    // pace.
    double elapsed = (now - anchor) / pace;
    double shifted = now + (-elapsed * next);
    pace = next;
    anchor = shifted + error * kSpeechPlayheadPhaseGain;
}

bool LevelMeter::LeadAt(double now, float* out) const {
    if (anchor < 0) {
        return false;
    }
    double playhead = (now - anchor) / pace - kSpeechPlayheadLag;
    float lead = (float)((double)recorded - playhead);
    if (lead < 0) {
        lead = 0;
    } else if (lead > kSpeechPlayheadMaxLead) {
        lead = kSpeechPlayheadMaxLead;
    }
    *out = lead;
    return true;
}

bool LevelMeter::PushAt(const int16_t* samples, int n, double now) {
    bool any = false;
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
            recorded++;
            AlignPlayhead(now);
            any = true;
        }
    }
    return any;
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
    s->surfaces.Free();
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
    s->drainWin = nullptr;
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

// Whether the drain posted earlier can still run: WindowPost drops a task
// whose window the app no longer lists. A post against a gone window is
// forgotten so the next Defer can post again or drain in place.
static bool DrainStillPosted(SpeechState* s, App* app) {
    if (!s->drainPosted) {
        return false;
    }
    if (app) {
        for (int i = 0; i < len(app->windows); i++) {
            if (app->windows[i] == s->drainWin) {
                return true;
            }
        }
    }
    s->drainPosted = false;
    s->drainWin = nullptr;
    return false;
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
        if (s->updateDepth == 0 && len(s->deferred) > 0 &&
            !DrainStillPosted(s, cx ? cx->app : nullptr)) {
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
    if (DrainStillPosted(s, app)) {
        return;
    }
    Window* win = len(app->windows) > 0 ? app->windows[0] : nullptr;
    if (win) {
        s->drainPosted = true;
        s->drainWin = win;
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
        // default_input: the microphone, where there is one.
        if (Microphone::IsSupported()) {
            s->input = Microphone::Input();
        }
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
    surfaces.Free();
    delete systemRecognizer;
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
// fallback is off or the platform has none.
static bool ActiveRecognizer(const SpeechState* s, SpeechRecognizer* out) {
    if (s->recognizer.IsSet()) {
        *out = s->recognizer;
        return true;
    }
    if (!s->systemFallback) {
        return false;
    }
    if (!s->systemRecognizer) {
        s->systemRecognizer = new SystemRecognizer();
    }
    if (!s->systemRecognizer->IsSupported()) {
        return false;
    }
    *out = s->systemRecognizer->AsRecognizer();
    return true;
}

bool SpeechState::HasRecognizer() const {
    SpeechRecognizer active;
    return input.IsSet() && ActiveRecognizer(this, &active);
}

bool SpeechState::IsAvailable(const App* app) const {
    SpeechRecognizer active;
    if (!input.IsSet() || !ActiveRecognizer(this, &active)) {
        return false;
    }
    return !active.isAvailable || active.isAvailable(active.data, app);
}

TempStr SpeechState::TranscriptTemp() const {
    return fmt("%s%s", committed, hypothesis);
}

void SpeechState::AdvanceSurface(double now, float width) {
    surfaces.Advance(now, width, &meter);
}

const float* SpeechState::SurfaceHeights(float width, int* outN) const {
    return surfaces.Heights(width, outN);
}

void SpeechState::Start(Ctx* cx) {
    if (SpeechStatusIsActive(status)) {
        return;
    }
    SpeechUpdate update(this, cx);
    SpeechRecognizer found;
    if (!ActiveRecognizer(this, &found) || !input.IsSet()) {
        EmitError(this, cx, SpeechError::Unsupported());
        return;
    }
    const SpeechRecognizer* active = &found;

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
    surfaces.Free();
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

// The icon button's square for this size, and the icon size the indicator's
// stop glyph takes. px(20) / px(24) / px(32) at a 16 px rem, the same sides
// Button gives an icon-only button. A pixel Size stays pixels; Large keeps
// its own icon inside the 32 px square.
static float SpeechButtonCapturingSide(const Ctx* cx, UiSize buttonSize,
                                       UiSize* iconSize) {
    if (buttonSize.kind == UiSize::Kind::Size) {
        *iconSize = UiSize::Custom(buttonSize.pixels * 0.75f);
        return buttonSize.pixels;
    }
    if (buttonSize == UiSize::XSmall) {
        *iconSize = UiSize::XSmall;
        return Rems(cx, 1.25f);
    }
    if (buttonSize == UiSize::Small) {
        *iconSize = UiSize::Small;
        return Rems(cx, 1.5f);
    }
    *iconSize = buttonSize;
    return Rems(cx, 2.f);
}

// use_keyed_state((id, "focus")): keyboard focus can show the stop glyph,
// which the indicator otherwise reveals only under the pointer.
struct SpeechButtonFocus {
    FocusHandle handle = {};
};

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
    bool stopping = status == SpeechStatus::Stopping;
    Str label = !available  ? Tr("Speech.Unavailable")
                : capturing ? Tr("Speech.Stop")
                            : Tr("Speech.Start");

    bool keyboardFocused = false;
    FocusHandle focus = {};
    if (cx->win) {
        SpeechButtonFocus* slot =
            ElementState<SpeechButtonFocus>(cx, id, StrL("focus"));
        if (!slot->handle.IsValid()) {
            slot->handle = FocusHandleNew(cx);
        }
        focus = slot->handle;
        keyboardFocused = FocusHandleIsFocused(cx->win, focus) &&
                          WindowLastInputWasKeyboard(cx->win);
    }

    Button* button = Button::New(cx, id)->Ghost()->WithSize(size);
    if (focus.IsValid()) {
        button->TrackFocus(focus);
    }
    // With a child instead of an icon the button sizes to its content, so
    // give it the icon button's square and let the indicator fill it.
    if (capturing && !keyboardFocused) {
        UiSize iconSize = size;
        float side = SpeechButtonCapturingSide(cx, size, &iconSize);
        button->Size(side)->Child(
            SpeechIndicator::New(cx, state)->WithSize(iconSize)->IntoEl());
    } else {
        button
            ->Icon((capturing || stopping) ? IconName::Square : IconName::Mic);
    }

    El* e = button->Selected(capturing)
                ->Loading(stopping)
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

// ─── indicator.rs ─────────────────────────────────────────────────────────

// How strongly each bar follows the voice: fuller in the middle.
static const float kSpeechIndicatorWeights[kSpeechIndicatorBars] = {
    0.65f, 1.f, 0.85f, 0.55f};
// How far behind the newest level each bar reads, in levels.
static const float kSpeechIndicatorLags[kSpeechIndicatorBars] = {1.f, 0.f, 0.5f,
                                                                 1.5f};
// The bars' heights with reduced motion: a still glyph of a voice.
static const float kSpeechIndicatorStill[kSpeechIndicatorBars] = {0.4f, 0.7f,
                                                                  0.55f, 0.35f};
// How high the bars breathe in silence.
static const float kSpeechIndicatorBreath = 0.12f;
// How fast the bars breathe, in radians per second, and how far apart.
static const float kSpeechIndicatorBreathRate = 2.4f;
static const float kSpeechIndicatorBreathSpread = 0.9f;
// A bar's width, the gap between bars and a full bar's height, relative to
// the indicator's shorter side.
static const float kSpeechIndicatorBarRatio = 0.085f;
static const float kSpeechIndicatorGapRatio = 0.075f;
static const float kSpeechIndicatorHeightRatio = 0.46f;

SpeechIndicator* SpeechIndicator::New(Ctx* cx, Entity<SpeechState> state) {
    SpeechIndicator* indicator = ArenaNew<SpeechIndicator>(cx->a);
    indicator->a = cx->a;
    indicator->cx = cx;
    indicator->state = state;
    return indicator;
}

SpeechIndicator* SpeechIndicator::WithSize(UiSize s) {
    size = s;
    return this;
}

static float SpeechIndicatorSample(const float* levels, int last,
                                   float position) {
    float lastF = (float)last;
    if (position < 0.f) {
        position = 0.f;
    } else if (position > lastF) {
        position = lastF;
    }
    int below = (int)floorf(position);
    int above = below + 1;
    if (above > last) {
        above = last;
    }
    float fraction = position - (float)below;
    return levels[below] + (levels[above] - levels[below]) * fraction;
}

void SpeechIndicatorBarHeights(const float* levels, int nLevels,
                               uint64_t recorded, bool hasLead, float lead,
                               float* out) {
    for (int i = 0; i < kSpeechIndicatorBars; i++) {
        out[i] = 0.f;
    }
    // No lead, or no level to read: dots.
    if (!hasLead || nLevels <= 0 || !levels) {
        return;
    }
    int last = nLevels - 1;
    // The playhead moves evenly between levels, so reading at it rather than
    // at the newest level makes the bars glide instead of jump every 80 ms.
    float playhead = (float)last - lead;
    // The bars breathe only while the voice is quiet, on a clock that runs
    // with the audio. LEVEL_INTERVAL is 80 ms.
    float seconds =
        ((float)recorded - lead) * ((float)kSpeechLevelIntervalMs / 1000.f);
    float atPlayhead = SpeechIndicatorSample(levels, last, playhead);
    float loud = atPlayhead * 4.f;
    if (loud > 1.f) {
        loud = 1.f;
    }
    float quiet = 1.f - loud;
    for (int i = 0; i < kSpeechIndicatorBars; i++) {
        float voice = SpeechIndicatorSample(
                          levels, last, playhead - kSpeechIndicatorLags[i]) *
                      kSpeechIndicatorWeights[i];
        float phase = seconds * kSpeechIndicatorBreathRate +
                      (float)i * kSpeechIndicatorBreathSpread;
        float breath =
            kSpeechIndicatorBreath * (0.5f + 0.5f * sinf(phase)) * quiet;
        float height = voice > breath ? voice : breath;
        if (height < 0.f) {
            height = 0.f;
        } else if (height > 1.f) {
            height = 1.f;
        }
        out[i] = height;
    }
}

void SpeechIndicatorBarRects(Bounds bounds, const float* heights, Bounds* out) {
    float side = bounds.w < bounds.h ? bounds.w : bounds.h;
    float bar = side * kSpeechIndicatorBarRatio;
    float gap = side * kSpeechIndicatorGapRatio;
    float full = side * kSpeechIndicatorHeightRatio;
    float total = bar * (float)kSpeechIndicatorBars +
                  gap * (float)(kSpeechIndicatorBars - 1);
    float left = bounds.x + bounds.w / 2.f - total / 2.f;
    float middle = bounds.y + bounds.h / 2.f;
    for (int i = 0; i < kSpeechIndicatorBars; i++) {
        float h = heights ? heights[i] : 0.f;
        float height = bar + (full - bar) * h;
        out[i] = Bounds{left + (bar + gap) * (float)i, middle - height / 2.f,
                        bar, height};
    }
}

struct IndicatorPaint {
    float heights[kSpeechIndicatorBars] = {};
};

static void PaintIndicator(PaintCtx* ctx, El* e, void* user) {
    // Under the pointer the bars give way to the stop glyph. There is no
    // group-hover opacity, so the bars simply are not drawn while the
    // "speech-indicator" group is hovered.
    if (ctx->groupHovered) {
        return;
    }
    const IndicatorPaint* paint = (const IndicatorPaint*)user;
    Bounds rects[kSpeechIndicatorBars];
    SpeechIndicatorBarRects(e->Bounds(), paint->heights, rects);
    Rgba color = PaintFade(ctx, e->style.color);
    for (int i = 0; i < kSpeechIndicatorBars; i++) {
        float radius = rects[i].w < rects[i].h ? rects[i].w : rects[i].h;
        FillRound(ctx, rects[i].x, rects[i].y, rects[i].w, rects[i].h,
                  radius / 2.f, color);
    }
}

El* SpeechIndicator::IntoEl() {
    const SpeechState* s = state.Get(cx->app);
    bool reduced = MotionReduced();
    IndicatorPaint* paint = ArenaNew<IndicatorPaint>(a);
    if (reduced) {
        for (int i = 0; i < kSpeechIndicatorBars; i++) {
            paint->heights[i] = kSpeechIndicatorStill[i];
        }
    } else if (s) {
        int n = s->LevelsLen();
        float levels[kSpeechLevelHistory];
        if (n > kSpeechLevelHistory) {
            n = kSpeechLevelHistory;
        }
        for (int i = 0; i < n; i++) {
            levels[i] = s->LevelAt(i);
        }
        float lead = 0.f;
        bool hasLead = s->LevelLeadAt(TimeNow(), &lead);
        SpeechIndicatorBarHeights(levels, n, s->LevelsRecorded(), hasLead, lead,
                                  paint->heights);
    }
    if (s && SpeechStatusIsCapturing(s->Status()) && !reduced && cx->win) {
        WindowRequestAnimationFrame(cx->win);
    }

    El* canvas = Div(a)->SizeFull();
    canvas->customPaint = &PaintIndicator;
    canvas->customUser = paint;
    El* bars = Div(a)->Absolute()->Inset0()->Child(canvas);
    El* stop = CenteredOverlay(a)->GroupHoverVisible()->Child(
        Icon::New(cx, IconName::Square)->Size(size)->IntoEl());
    return Div(a)->SizeFull()->Group()->Child(bars)->Child(stop);
}

// ─── surface.rs ───────────────────────────────────────────────────────────

static const float kLiquidNodeSpacing = 3.f;
static const int kLiquidMinNodes = 16;
static const float kLiquidStep = 1.f / 240.f;
static const float kLiquidMaxFrame = 0.05f;
static const float kLiquidStiffness = 2600.f;
static const float kLiquidRestoring = 4.f;
static const float kLiquidDamping = 2.4f;
static const float kLiquidSourceFrequency = 3.2f;
static const float kLiquidSourceAmplitude = 0.9f;
static const float kLiquidSourcePull = 0.35f;
static const float kLiquidSourceResponse = 0.05f;
static const float kLiquidDropLevel = 0.18f;
static const float kLiquidDropRelease = 0.09f;
static const float kLiquidDropRise = 0.15f;
static const float kLiquidDropStrength = 9.f;
static const int kLiquidDropReach = 4;
static const float kLiquidStaleAfter = 0.5f;
static const float kTau = 6.283185307179586f;

static int LiquidNodeCount(float width) {
    int n = (int)(width / kLiquidNodeSpacing);
    return n > kLiquidMinNodes ? n : kLiquidMinNodes;
}

void LiquidSurface::Free() {
    delete[] heights;
    delete[] velocities;
    heights = nullptr;
    velocities = nullptr;
    n = 0;
}

static void LiquidResize(LiquidSurface* s, int nodes) {
    if (s->n == nodes) {
        return;
    }
    delete[] s->heights;
    delete[] s->velocities;
    s->heights = new float[nodes]();
    s->velocities = new float[nodes]();
    s->n = nodes;
}

static void LiquidDrop(LiquidSurface* s, float level) {
    uint32_t seed = s->seed;
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    s->seed = seed;
    float place = 0.12f + 0.76f * ((float)seed / (float)0xffffffffu);
    int last = s->n - 1;
    int center = (int)(place * (float)last);
    for (int offset = -kLiquidDropReach; offset <= kLiquidDropReach; offset++) {
        int ix = center + offset;
        if (ix > 0 && ix < last) {
            float falloff = expf(-(float)(offset * offset) / 5.f);
            s->velocities[ix] -= level * kLiquidDropStrength * falloff;
        }
    }
}

static void LiquidDropSyllables(LiquidSurface* s, const LevelMeter* meter) {
    if (!meter) {
        return;
    }
    uint64_t recorded = meter->recorded;
    uint64_t fresh = recorded >= s->seen ? recorded - s->seen : 0;
    s->seen = recorded;
    int nLevels = meter->LevelsLen();
    // History holds at most kSpeechLevelHistory; a wider gap still walks
    // what is left, oldest first, the way the meter's iterator does.
    int skip = fresh < (uint64_t)nLevels ? nLevels - (int)fresh : 0;
    for (int i = skip; i < nLevels; i++) {
        float level = meter->LevelAt(i);
        if (level < kLiquidDropRelease) {
            s->sounding = false;
        } else if (level >= kLiquidDropLevel &&
                   (!s->sounding || level >= s->previous + kLiquidDropRise)) {
            s->sounding = true;
            LiquidDrop(s, level);
        }
        s->previous = level;
    }
}

static void LiquidStep(LiquidSurface* s, float dt) {
    int last = s->n - 1;
    if (last < 1) {
        return;
    }
    s->phase = fmodf(s->phase + dt * kTau * kLiquidSourceFrequency, kTau);
    if (s->phase < 0.f) {
        s->phase += kTau;
    }
    float source = s->level * kLiquidSourceAmplitude * sinf(s->phase);
    int middle = s->n / 2;
    for (int offset = -2; offset <= 2; offset++) {
        int ix = middle + offset;
        if (ix <= 0 || ix >= last) {
            continue;
        }
        float target = source * expf(-(float)(offset * offset) / 2.f);
        s->heights[ix] += (target - s->heights[ix]) * kLiquidSourcePull;
    }
    float damping = expf(-kLiquidDamping * dt);
    for (int ix = 1; ix < last; ix++) {
        float curvature =
            s->heights[ix - 1] + s->heights[ix + 1] - 2.f * s->heights[ix];
        s->velocities[ix] +=
            (kLiquidStiffness * curvature - kLiquidRestoring * s->heights[ix]) *
            dt;
        s->velocities[ix] *= damping;
    }
    for (int ix = 1; ix < last; ix++) {
        s->heights[ix] += s->velocities[ix] * dt;
    }
    s->heights[0] = 0.f;
    s->heights[last] = 0.f;
}

void LiquidSurface::Advance(double now, float width, const LevelMeter* meter) {
    LiquidResize(this, LiquidNodeCount(width));
    float elapsed = 0.f;
    if (advanced >= 0 && now >= advanced) {
        elapsed = (float)(now - advanced);
        if (elapsed > kLiquidMaxFrame) {
            elapsed = kLiquidMaxFrame;
        }
    }
    advanced = now;
    LiquidDropSyllables(this, meter);
    float raw = 0.f;
    if (meter && meter->LevelsLen() > 0) {
        raw = meter->LevelAt(meter->LevelsLen() - 1);
    }
    float follow = 1.f - expf(-elapsed / kLiquidSourceResponse);
    level += (raw - level) * follow;
    int steps = elapsed > 0.f ? (int)ceilf(elapsed / kLiquidStep) : 0;
    float dt = steps > 0 ? elapsed / (float)steps : 0.f;
    for (int i = 0; i < steps; i++) {
        LiquidStep(this, dt);
    }
}

void LiquidSurfaces::Free() {
    for (int i = 0; i < n; i++) {
        items[i]->Free();
        delete items[i];
    }
    delete[] items;
    items = nullptr;
    n = 0;
    cap = 0;
}

void LiquidSurfaces::Advance(double now, float width, const LevelMeter* meter) {
    int nodes = LiquidNodeCount(width);
    int kept = 0;
    for (int i = 0; i < n; i++) {
        LiquidSurface* s = items[i];
        bool same = s->n == nodes;
        bool fresh = s->advanced < 0 || now < s->advanced ||
                     (now - s->advanced) < (double)kLiquidStaleAfter;
        if (same || fresh) {
            items[kept++] = s;
        } else {
            s->Free();
            delete s;
        }
    }
    n = kept;
    int ix = -1;
    for (int i = 0; i < n; i++) {
        if (items[i]->n == nodes) {
            ix = i;
            break;
        }
    }
    if (ix < 0) {
        if (n == cap) {
            int ncap = cap < 4 ? 4 : cap * 2;
            LiquidSurface** next = new LiquidSurface*[ncap];
            for (int i = 0; i < n; i++) {
                next[i] = items[i];
            }
            delete[] items;
            items = next;
            cap = ncap;
        }
        items[n] = new LiquidSurface();
        ix = n;
        n++;
    }
    items[ix]->Advance(now, width, meter);
}

const float* LiquidSurfaces::Heights(float width, int* outN) const {
    int nodes = LiquidNodeCount(width);
    for (int i = 0; i < n; i++) {
        if (items[i]->n == nodes && items[i]->heights) {
            if (outN) {
                *outN = items[i]->n;
            }
            return items[i]->heights;
        }
    }
    if (outN) {
        *outN = 0;
    }
    return nullptr;
}

void SpeechStillHeights(float level, float* out) {
    const int points = kSpeechStillPoints;
    for (int ix = 0; ix < points; ix++) {
        float fromMiddle = (float)ix / (float)(points - 1) - 0.5f;
        float envelope = expf(-powf(fromMiddle / 0.3f, 2.f));
        out[ix] = level * kLiquidSourceAmplitude *
                  cosf(kTau * 2.5f * fromMiddle) * envelope;
    }
}

float SpeechEdgeTaper(float t) {
    if (t < 0.f) {
        t = 0.f;
    } else if (t > 1.f) {
        t = 1.f;
    }
    float s = sinf(kPi * t);
    if (s < 0.f) {
        s = 0.f;
    }
    return powf(s, 0.35f);
}

static const float kCrestRatio = 0.42f;
static const float kCrestSoftness = 1.4f;
static const float kLineTaper = 0.25f;
static const int kMinSurfacePoints = 32;
static const float kWaterOpacity = 0.16f;

int SpeechSurfacePoints(Bounds bounds, const float* heights, int nHeights,
                        Point* out, int cap) {
    int count = (!heights || nHeights <= 0) ? kMinSurfacePoints
                                            : (nHeights < 2 ? 2 : nHeights);
    int write = count < cap ? count : cap;
    float middle = bounds.y + bounds.h / 2.f;
    float crest = bounds.h * kCrestRatio;
    for (int ix = 0; ix < write; ix++) {
        float t = count <= 1 ? 0.f : (float)ix / (float)(count - 1);
        float height = (heights && ix < nHeights) ? heights[ix] : 0.f;
        float rise = tanhf(height * kCrestSoftness) * SpeechEdgeTaper(t);
        out[ix] = Point{bounds.x + bounds.w * t, middle + crest * rise};
    }
    return write;
}

void SpeechSurfaceRibbon(const Point* points, int n, float width, Point* top,
                         Point* bottom) {
    int last = n > 1 ? n - 1 : 1;
    for (int ix = 0; ix < n; ix++) {
        Point before = points[ix > 0 ? ix - 1 : 0];
        Point after = points[ix + 1 < n ? ix + 1 : n - 1];
        float dx = after.x - before.x;
        float dy = after.y - before.y;
        float length = sqrtf(dx * dx + dy * dy);
        if (length < 1e-20f) {
            length = 1e-20f;
        }
        float swell = sinf(kPi * (float)ix / (float)last);
        if (swell < 0.f) {
            swell = 0.f;
        }
        float half = width * (kLineTaper + (1.f - kLineTaper) * swell) / 2.f;
        float ox = half * (-dy / length);
        float oy = half * (dx / length);
        Point at = points[ix];
        top[ix] = Point{at.x - ox, at.y - oy};
        bottom[ix] = Point{at.x + ox, at.y + oy};
    }
}

// ─── waveform.rs ──────────────────────────────────────────────────────────

// DEFAULT_WIDTH: the width of a waveform the caller does not size.
static const float kWaveformDefaultWidth = 96.f;
// GAP_RATIO: the space between bars, relative to their width.
static const float kWaveformGapRatio = 2.5f;

SpeechWaveform* SpeechWaveform::Liquid() {
    liquid = true;
    return this;
}

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
    // Enough bars to reach the leading edge from wherever the newest sits.
    float past = scroll < 0.f ? -scroll : 0.f;
    float fits = ceilf((bounds.w + gap) / pitch - past);
    if (fits < 0.f) {
        fits = 0.f;
    }
    int count = (int)fits + 1;
    int n = 0;
    for (int fromEnd = 0; fromEnd < count && n < cap; fromEnd++) {
        float right = right0 - pitch * ((float)fromEnd + scroll);
        float left = right - bar;
        if (left < bounds.x - 0.5f || left >= right0) {
            continue;
        }
        int ix = nLevels - (fromEnd + 1);
        if (ix < 0) {
            continue;
        }
        float level = levels[ix];
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
    float gap = 5;
    Rgba color = {};
};

struct LiquidPaint {
    Entity<SpeechState> state = {};
    bool animate = false;
    bool reduced = false;
    float line = 2;
    Rgba color = {};
    const float* heights = nullptr;
    int nHeights = 0;
};

static void CopyHeights(Arena* arena, LiquidPaint* paint, const float* src,
                        int n) {
    if (n <= 0 || !src) {
        paint->heights = nullptr;
        paint->nHeights = 0;
        return;
    }
    float* copy = (float*)Alloc(arena, n * (int)sizeof(float));
    for (int i = 0; i < n; i++) {
        copy[i] = src[i];
    }
    paint->heights = copy;
    paint->nHeights = n;
}

static void PrepareLiquid(PaintCtx* ctx, El* e, void* user) {
    LiquidPaint* paint = (LiquidPaint*)user;
    SpeechState* s = paint->state.Get(ctx->app);
    if (paint->reduced) {
        float still[kSpeechStillPoints];
        float level = 0.f;
        if (s && s->LevelsLen() > 0) {
            level = s->LevelAt(s->LevelsLen() - 1);
        }
        SpeechStillHeights(level, still);
        CopyHeights(e->arena, paint, still, kSpeechStillPoints);
        return;
    }
    if (s && paint->animate) {
        s->AdvanceSurface(TimeNow(), e->w);
    }
    int n = 0;
    const float* heights = s ? s->SurfaceHeights(e->w, &n) : nullptr;
    CopyHeights(e->arena, paint, heights, n);
}

static void PathQuadTo(Path* path, float x0, float y0, float cx, float cy,
                       float x, float y) {
    PathCubicTo(path, x0 + (2.f / 3.f) * (cx - x0),
                y0 + (2.f / 3.f) * (cy - y0), x + (2.f / 3.f) * (cx - x),
                y + (2.f / 3.f) * (cy - y), x, y);
}

// follow: current point is points[0]. A quadratic through each later
// midpoint, then a line to the last point. GPUI's curve_to is quadratic;
// the path here only has cubics.
static void PathFollow(Path* path, const Point* points, int n) {
    if (n <= 0) {
        return;
    }
    float x = points[0].x;
    float y = points[0].y;
    for (int i = 1; i + 1 < n; i++) {
        float mx = (points[i].x + points[i + 1].x) / 2.f;
        float my = (points[i].y + points[i + 1].y) / 2.f;
        PathQuadTo(path, x, y, points[i].x, points[i].y, mx, my);
        x = mx;
        y = my;
    }
    PathLineTo(path, points[n - 1].x, points[n - 1].y);
}

static void PaintLiquid(PaintCtx* ctx, El* e, void* user) {
    const LiquidPaint* paint = (const LiquidPaint*)user;
    Bounds box = e->Bounds();
    int cap = paint->nHeights > kMinSurfacePoints ? paint->nHeights
                                                  : kMinSurfacePoints;
    if (cap < 2) {
        cap = 2;
    }
    Point* points = (Point*)Alloc(e->arena, cap * (int)sizeof(Point));
    int n =
        SpeechSurfacePoints(box, paint->heights, paint->nHeights, points, cap);
    if (n < 2) {
        return;
    }
    Point* top = (Point*)Alloc(e->arena, n * (int)sizeof(Point));
    Point* bottom = (Point*)Alloc(e->arena, n * (int)sizeof(Point));
    Point* reversed = (Point*)Alloc(e->arena, n * (int)sizeof(Point));
    SpeechSurfaceRibbon(points, n, paint->line, top, bottom);
    for (int i = 0; i < n; i++) {
        reversed[i] = bottom[n - 1 - i];
    }
    Rgba color = PaintFade(ctx, paint->color);
    Rgba water = PaintFade(ctx, RgbaOpacity(paint->color, kWaterOpacity));
    Rgba clear = PaintFade(ctx, RgbaOpacity(paint->color, 0.f));
    CanvasPushClip(ctx, box.x, box.y, box.w, box.h);
    Path* fill = PathNew(ctx, true);
    PathMoveTo(fill, points[0].x, points[0].y);
    PathFollow(fill, points, n);
    PathLineTo(fill, box.x + box.w, box.y + box.h);
    PathLineTo(fill, box.x, box.y + box.h);
    PathClose(fill);
    PathFillGradientV(ctx, fill, box.y, box.y + box.h, water, clear);
    PathFree(fill);
    Path* line = PathNew(ctx, true);
    PathMoveTo(line, top[0].x, top[0].y);
    PathFollow(line, top, n);
    PathLineTo(line, bottom[n - 1].x, bottom[n - 1].y);
    PathFollow(line, reversed, n);
    PathClose(line);
    PathFill(ctx, line, color);
    PathFree(line);
    CanvasPopClip(ctx);
}

static void PaintWaveform(PaintCtx* ctx, El* e, void* user) {
    const WaveformPaint* w = (const WaveformPaint*)user;
    Bounds rects[kSpeechLevelHistory + 4];
    Bounds box = e->Bounds();
    int n =
        SpeechWaveformBarRects(box, w->levels, w->nLevels, w->scroll, w->bar,
                               w->gap, rects, kSpeechLevelHistory + 4);
    Rgba color = PaintFade(ctx, w->color);
    // A bar sliding in is cut at the trailing edge. FillRound is a capsule
    // (radius of half the short side) and is not snapped to device pixels,
    // which is what Rust's path is for: a snapped quad would step.
    CanvasPushClip(ctx, box.x, box.y, box.w, box.h);
    for (int i = 0; i < n; i++) {
        float radius = rects[i].w < rects[i].h ? rects[i].w : rects[i].h;
        FillRound(ctx, rects[i].x, rects[i].y, rects[i].w, rects[i].h,
                  radius / 2.f, color);
    }
    CanvasPopClip(ctx);
}

El* SpeechWaveform::IntoEl() {
    const Theme& th = ThemeNow(cx->app);
    float height = Height();
    // Bars thicken with the waveform so a tall one does not read as
    // hairlines, and stand apart so a run of silence reads as a row of dots.
    float bar = roundf(height * 0.125f);
    bar = bar < 2.f ? 2.f : (bar > 4.f ? 4.f : bar);
    float gap = roundf(bar * kWaveformGapRatio);
    const SpeechState* s = state.Get(cx->app);
    bool capturing = s && SpeechStatusIsCapturing(s->Status());
    bool animate = capturing && !MotionReduced();

    WaveformPaint* paint = ArenaNew<WaveformPaint>(a);
    paint->bar = bar;
    paint->gap = gap;
    paint->color = capturing ? th.primary : th.mutedFg;
    if (s) {
        paint->nLevels = s->LevelsLen();
        for (int i = 0; i < paint->nLevels; i++) {
            paint->levels[i] = s->LevelAt(i);
        }
        // The bars follow an even clock rather than each arrival. Without
        // animation the newest bar sits at the edge.
        float lead = 0;
        if (animate && s->LevelLeadAt(TimeNow(), &lead)) {
            paint->scroll = -lead;
        }
    }
    if (animate && cx->win) {
        WindowRequestAnimationFrame(cx->win);
    }

    El* canvas = Div(a)->SizeFull();
    if (liquid) {
        LiquidPaint* water = ArenaNew<LiquidPaint>(a);
        water->state = state;
        water->animate = animate;
        water->reduced = MotionReduced();
        float line = height * 0.1f;
        water->line = line < 2.f ? 2.f : line;
        water->color = paint->color;
        canvas->prePaint = &PrepareLiquid;
        canvas->customPaint = &PaintLiquid;
        canvas->customUser = water;
    } else {
        canvas->customPaint = &PaintWaveform;
        canvas->customUser = paint;
    }
    El* e = Div(a)->H(height)->W(kWaveformDefaultWidth)->Shrink0();
    if (styleSet) {
        e->Refine(style, styleSet);
    }
    return e->Child(canvas);
}

} // namespace component
} // namespace gpui
