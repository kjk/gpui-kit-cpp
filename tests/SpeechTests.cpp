/* Ported from the tests in crates/ui/src/speech/: level.rs, waveform.rs,
 * state.rs and button.rs.
 *
 * The state tests run on the test platform. Rust defers a sink's call with
 * cx.defer and times a stop out on the executor; here both ride a window —
 * the call is posted to it and the timeout is its timer — so the fixture
 * opens one, and run_until_parked / advance_clock are the test platform's.
 *
 * microphone.rs's converter is ported without the microphone around it, and
 * its tests with it. system/macos.rs's three tests pin how the macOS system
 * recognizer assembles SFSpeechRecognizer's results, which this tree does
 * not have (port-status.md). */

#include "Test.h"
#include <math.h>

using namespace gpui::component;

// ─── level.rs ─────────────────────────────────────────────────────────────

static Vec<int16_t> Tone(int16_t amplitude, int n) {
    Vec<int16_t> out;
    for (int ix = 0; ix < n; ix++) {
        VecAppend(out, (int16_t)(ix % 2 == 0 ? amplitude : -amplitude));
    }
    return out;
}

static bool PushTone(LevelMeter* meter, int16_t amplitude, int n) {
    Vec<int16_t> tone = Tone(amplitude, n);
    return meter->Push(tone.els, len(tone));
}

static void OneLevelPerIntervalAcrossPushes() {
    LevelMeter meter;
    meter.Reset(16000, 1);
    // 25 ms at 16 kHz is 400 samples; feed 1 000 samples in uneven pushes.
    utassert(!PushTone(&meter, 1000, 150));
    utassert(PushTone(&meter, 1000, 300));
    utassert(PushTone(&meter, 1000, 550));
    utassert(meter.LevelsLen() == 2);
    utassert(meter.lastLevelAt >= 0);
}

static void TheWindowFollowsTheStreamFormat() {
    utassert(SpeechLevelWindowFor(16000, 1) == 400);
    utassert(SpeechLevelWindowFor(48000, 2) == 2400);
}

static void PeaksRiseFastAndFallSlowly() {
    float up = SpeechLevelSmooth(0.f, 1.f);
    utassert(up >= 0.85f);
    float down = SpeechLevelSmooth(1.f, 0.f);
    utassert(down >= 0.65f);
    utassert(SpeechLevelSmooth(down, 0.f) < down);
}

static void BackgroundNoiseReadsAsSilence() {
    LevelMeter meter;
    meter.Reset(16000, 1);
    // -60 dBFS: below the -50 dB floor of the scale.
    PushTone(&meter, 32, 400);
    utassert(meter.LevelsLen() == 1 && meter.LevelAt(0) == 0.f);
}

static void LevelMapsDecibelsToTheUnitRange() {
    utassert(SpeechLevelOfRms(0.) == 0.f);
    utassert(SpeechLevelOfRms(1.) == 1.f);
    // -20 dBFS sits at 0.6 on the -50..0 dB scale.
    utassert(fabsf(SpeechLevelOfRms(0.1) - 0.6f) < 0.01f);
}

static void HistoryIsBounded() {
    LevelMeter meter;
    meter.Reset(16000, 1);
    PushTone(&meter, 8000, 400 * (kSpeechLevelHistory + 10));
    utassert(meter.LevelsLen() == kSpeechLevelHistory);
}

// ─── microphone.rs ────────────────────────────────────────────────────────

static void ConverterKeepsRateAndDuplicatesChannels() {
    SpeechAudioConverter converter =
        SpeechAudioConverter::New(16000, AudioFormat::New(16000, 2));
    const float input[] = {0.f, 0.5f, -0.5f};
    Vec<int16_t> output;
    converter.Convert(input, 3, output);
    const int16_t want[] = {0, 0, 16383, 16383, -16383, -16383};
    utassert(len(output) == 6);
    for (int i = 0; i < 6 && i < len(output); i++) {
        utassert(output[i] == want[i]);
    }
}

static void ConverterDownsamplesAcrossChunks() {
    SpeechAudioConverter converter =
        SpeechAudioConverter::New(48000, AudioFormat{});
    float silence[480] = {};
    Vec<int16_t> output;
    for (int i = 0; i < 10; i++) {
        converter.Convert(silence, 480, output);
    }
    // 100 ms at 48 kHz is 100 ms at 16 kHz, whatever the chunking.
    utassert(len(output) == 1600);
}

static void ConverterUpsamples() {
    SpeechAudioConverter converter =
        SpeechAudioConverter::New(8000, AudioFormat{});
    float silence[80] = {};
    Vec<int16_t> output;
    for (int i = 0; i < 10; i++) {
        converter.Convert(silence, 80, output);
    }
    // The first output lands on the first input sample, so the half step
    // before it is never produced.
    utassert(len(output) == 1599);
}

// ─── waveform.rs ──────────────────────────────────────────────────────────

static Bounds WaveBounds(float width, float height) {
    return Bounds{10.f, 20.f, width, height};
}

static void BarsFillTheWidthWithTheNewestAtTheTrailingEdge() {
    Bounds bounds = WaveBounds(98.f, 20.f);
    const float levels[] = {0.2f, 0.9f};
    Bounds rects[64];
    int n = SpeechWaveformBarRects(bounds, levels, 2, 0.f, 2.f, 2.f, rects, 64);
    // 98 px at a 4 px pitch fits 25 bars.
    utassert(n == 25);
    utassert(rects[0].x + rects[0].w == bounds.x + bounds.w);
    utassert(rects[0].h == 18.f);
    utassert(rects[1].h == 4.f);
}

static void BarsGrowFromTheMidlineAndRestAsABaseline() {
    Bounds bounds = WaveBounds(40.f, 20.f);
    const float levels[] = {0.5f};
    Bounds rects[64];
    int n = SpeechWaveformBarRects(bounds, levels, 1, 0.f, 2.f, 2.f, rects, 64);
    utassert(n > 0);
    for (int i = 0; i < n; i++) {
        float middle = rects[i].y + rects[i].h / 2.f;
        utassert(middle == 30.f);
        utassert(rects[i].h >= 2.f);
    }
}

static void ScrollingMovesBarsTowardTheLeadingEdge() {
    Bounds bounds = WaveBounds(40.f, 20.f);
    const float levels[] = {1.f};
    Bounds still[64];
    Bounds moving[64];
    int nStill =
        SpeechWaveformBarRects(bounds, levels, 1, 0.f, 2.f, 2.f, still, 64);
    int nMoving =
        SpeechWaveformBarRects(bounds, levels, 1, 0.5f, 2.f, 2.f, moving, 64);
    utassert(nStill > 0 && nMoving > 0);
    utassert(still[0].x - moving[0].x == 2.f);
    for (int i = 0; i < nMoving; i++) {
        utassert(moving[i].x >= bounds.x - 0.5f);
    }
}

// ─── state.rs ─────────────────────────────────────────────────────────────

namespace {

struct Recorded {
    SpeechSink sink = {};
    bool hasSink = false;
    int samples = 0;
    bool finished = false;
    bool dropped = false;
};

struct FakeRecognizer {
    Recorded recorded;
    bool failToStart = false;

    static bool Start(void* data, SpeechSink sink, App*,
                      RecognitionSession* out, SpeechError* error) {
        FakeRecognizer* self = (FakeRecognizer*)data;
        if (self->failToStart) {
            *error = SpeechError::Recognizer(StrL("offline"));
            return false;
        }
        self->recorded = Recorded{};
        self->recorded.sink = sink;
        self->recorded.hasSink = true;
        out->data = &self->recorded;
        out->pushAudio = &FakeRecognizer::PushAudio;
        out->finish = &FakeRecognizer::Finish;
        out->drop = &FakeRecognizer::Drop;
        return true;
    }
    static void PushAudio(void* data, const int16_t*, int count, App*) {
        ((Recorded*)data)->samples += count;
    }
    static void Finish(void* data, App*) { ((Recorded*)data)->finished = true; }
    static void Drop(void* data) { ((Recorded*)data)->dropped = true; }

    SpeechRecognizer AsRecognizer() {
        SpeechRecognizer r;
        r.data = this;
        r.start = &FakeRecognizer::Start;
        return r;
    }
};

struct FakeInput {
    AudioSink sink = {};
    bool hasSink = false;
    bool capturing = false;

    static bool Start(void* data, AudioFormat, AudioSink sink, App*,
                      AudioCapture* out, SpeechError*) {
        FakeInput* self = (FakeInput*)data;
        self->sink = sink;
        self->hasSink = true;
        self->capturing = true;
        out->data = self;
        out->stop = &FakeInput::Stop;
        return true;
    }
    static void Stop(void* data) { ((FakeInput*)data)->capturing = false; }

    AudioInput AsInput() {
        AudioInput input;
        input.data = this;
        input.start = &FakeInput::Start;
        return input;
    }
};

struct SpeechBlank {
    static El* Render(SpeechBlank*, Ctx* cx) { return Div(cx->a); }
};

// Events so far, as short labels.
struct SpeechLog {
    struct Entry {
        char label[96] = {};
    };
    Vec<Entry> entries;

    static void OnEvent(SpeechLog* self, Ctx*, const SpeechEvent* ev) {
        Arena* tmp = GetTempArena();
        Str label;
        switch (ev->kind) {
            case SpeechEventKind::Started:
                label = StrL("started");
                break;
            case SpeechEventKind::Partial:
                label = fmt("partial:%s", ev->text);
                break;
            case SpeechEventKind::Final:
                label = fmt("final:%s", ev->text);
                break;
            case SpeechEventKind::Cancelled:
                label = StrL("cancelled");
                break;
            case SpeechEventKind::Error:
                label = fmt("error:%s", ev->error.Display(tmp));
                break;
        }
        Entry entry;
        int n = len(label) < 95 ? len(label) : 95;
        memcpy(entry.label, label.s, (size_t)n);
        VecAppend(self->entries, entry);
    }
};

struct SpeechFixture {
    App* app = nullptr;
    Window* win = nullptr;
    Arena* a = nullptr;
    Ctx cx = {};
    FakeRecognizer recognizer;
    FakeInput input;
    Entity<SpeechState> state = {};
    Entity<SpeechLog> log = {};

    explicit SpeechFixture(bool failToStart = false) {
        recognizer.failToStart = failToStart;
        app = TestAppNew();
        win = TestWindowOpen(app, EntityNew<SpeechBlank>(app));
        a = ArenaNew();
        cx = {app, win, a, {}};
        state = SpeechStateNew(app);
        state.Get(app)
            ->Recognizer(recognizer.AsRecognizer())
            ->Input(input.AsInput())
            ->StopTimeout(1000);
        log = EntityNewState<SpeechLog>(app);
        SubscribeTo(app, state, log, &SpeechLog::OnEvent);
    }

    ~SpeechFixture() {
        TestAppFree(app);
        ArenaDelete(a);
    }

    SpeechState* S() { return state.Get(app); }
    SpeechSink Sink() { return recognizer.recorded.sink; }
    AudioSink Audio() { return input.sink; }
    SpeechStatus Status() { return S()->Status(); }

    // take_events, against `want`.
    bool TakeEventsAre(std::initializer_list<const char*> want) {
        SpeechLog* l = log.Get(app);
        bool same = len(l->entries) == (int)want.size();
        int i = 0;
        for (const char* w : want) {
            if (same && !StrEq(Str(l->entries[i].label), w)) {
                same = false;
            }
            i++;
        }
        l->entries.len = 0;
        return same;
    }

    bool LastEventIs(const char* want) {
        SpeechLog* l = log.Get(app);
        int n = len(l->entries);
        return n > 0 && StrEq(Str(l->entries[n - 1].label), want);
    }
};

} // namespace

static void SessionRunsFromStartToFinal() {
    SpeechFixture f;

    f.S()->Start(&f.cx);
    utassert(f.Status() == SpeechStatus::Connecting);
    utassert(f.input.capturing);

    f.Sink().Ready(f.app);
    {
        // Two levels' worth: 25 ms is 400 samples at 16 kHz.
        Vec<int16_t> half;
        for (int i = 0; i < 800; i++) {
            VecAppend(half, (int16_t)(32767 / 2));
        }
        f.Audio().Push(half.els, len(half), f.app);
    }
    f.Sink().Hypothesis(StrL("hello"), f.app);
    TestRunUntilParked(f.app);
    utassert(f.Status() == SpeechStatus::Recording);
    utassert(f.recognizer.recorded.samples == 800);
    utassert(f.S()->LevelsLen() == 2);
    utassert(f.S()->LevelAt(0) > 0.5f);

    f.Sink().Phrase(StrL("Hello."), f.app);
    f.Sink().Hypothesis(StrL(" How"), f.app);
    TestRunUntilParked(f.app);
    utassert(StrEq(f.S()->TranscriptTemp(), "Hello. How"));

    f.S()->Stop(&f.cx);
    utassert(f.Status() == SpeechStatus::Stopping);
    // "stop releases the microphone"
    utassert(!f.input.capturing);
    utassert(f.recognizer.recorded.finished);

    f.Sink().Phrase(StrL(" How are you?"), f.app);
    f.Sink().Finish(f.app);
    TestRunUntilParked(f.app);
    utassert(f.Status() == SpeechStatus::Idle);
    utassert(f.recognizer.recorded.dropped);
    utassert(f.TakeEventsAre({
        "started",
        "partial:hello",
        "partial:Hello.",
        "partial:Hello. How",
        "partial:Hello. How are you?",
        "final:Hello. How are you?",
    }));
}

static void StopEndsWithTheTranscriptSoFarAfterTheTimeout() {
    SpeechFixture f;
    f.S()->Start(&f.cx);
    f.Sink().Hypothesis(StrL("half a sentence"), f.app);
    TestRunUntilParked(f.app);

    f.S()->Stop(&f.cx);
    TestAdvanceClock(f.app, 900);
    utassert(f.Status() == SpeechStatus::Stopping);
    TestAdvanceClock(f.app, 200);
    TestRunUntilParked(f.app);

    utassert(f.Status() == SpeechStatus::Idle);
    utassert(f.LastEventIs("final:half a sentence"));
}

static void CancelDiscardsTheSessionAndIgnoresLateResults() {
    SpeechFixture f;
    f.S()->Start(&f.cx);
    SpeechSink stale = f.Sink();
    stale.Phrase(StrL("draft"), f.app);
    TestRunUntilParked(f.app);

    f.S()->Cancel(&f.cx);
    utassert(f.Status() == SpeechStatus::Idle);
    utassert(!f.input.capturing);
    utassert(f.recognizer.recorded.dropped);

    // A new session must not pick up the old session's results.
    f.S()->Start(&f.cx);
    stale.Phrase(StrL("late"), f.app);
    stale.Finish(f.app);
    TestRunUntilParked(f.app);

    utassert(f.Status() == SpeechStatus::Connecting);
    utassert(len(f.S()->TranscriptTemp()) == 0);
    utassert(
        f.TakeEventsAre({"started", "partial:draft", "cancelled", "started"}));
}

static void InputErrorEndsTheSession() {
    SpeechFixture f;
    f.S()->Start(&f.cx);
    f.Audio().Error(SpeechError::NoInputDevice(), f.app);
    TestRunUntilParked(f.app);

    utassert(f.Status() == SpeechStatus::Idle);
    utassert(f.recognizer.recorded.dropped);
    utassert(f.TakeEventsAre(
        {"started", "error:no audio input device is available"}));
}

static void RecognizerThatFailsToStartLeavesTheStateIdle() {
    SpeechFixture f(true);
    f.S()->Start(&f.cx);

    utassert(f.Status() == SpeechStatus::Idle);
    // "the input never starts"
    utassert(!f.input.capturing);
    utassert(f.TakeEventsAre({"error:speech recognition failed: offline"}));
}

static void StateWithoutARecognizerIsUnsupported() {
    App* app = TestAppNew();
    FakeInput input;
    Entity<SpeechState> state = SpeechStateNew(app);
    state.Get(app)->Input(input.AsInput())->SystemFallback(false);
    utassert(!state.Get(app)->HasRecognizer());
    utassert(!state.Get(app)->IsAvailable(app));
    TestAppFree(app);
}

// ─── button.rs ────────────────────────────────────────────────────────────

static void TestSpeechButtonBuilder() {
    App* app = TestAppNew();
    Arena* a = ArenaNew();
    Ctx cx = {app, nullptr, a, {}};
    Entity<SpeechState> state = SpeechStateNew(app);
    state.Get(app)->SystemFallback(false);
    SpeechButton* button = SpeechButton::New(&cx, state)
                               ->WithSize(UiSize::Small)
                               ->Disabled(true)
                               ->ShowWhenUnsupported(true);

    utassert(button->size == UiSize::Small);
    utassert(button->disabled);
    utassert(button->showWhenUnsupported);
    utassert(StrEq(button->id,
                   fmt("speech-button-%d-%u", state.id.index, state.id.gen)));
    ArenaDelete(a);
    TestAppFree(app);
}

void TestSpeech() {
    TestSuite("speech");
    OneLevelPerIntervalAcrossPushes();
    TheWindowFollowsTheStreamFormat();
    PeaksRiseFastAndFallSlowly();
    BackgroundNoiseReadsAsSilence();
    LevelMapsDecibelsToTheUnitRange();
    HistoryIsBounded();
    ConverterKeepsRateAndDuplicatesChannels();
    ConverterDownsamplesAcrossChunks();
    ConverterUpsamples();
    BarsFillTheWidthWithTheNewestAtTheTrailingEdge();
    BarsGrowFromTheMidlineAndRestAsABaseline();
    ScrollingMovesBarsTowardTheLeadingEdge();
    SessionRunsFromStartToFinal();
    StopEndsWithTheTranscriptSoFarAfterTheTimeout();
    CancelDiscardsTheSessionAndIgnoresLateResults();
    InputErrorEndsTheSession();
    RecognizerThatFailsToStartLeavesTheStateIdle();
    StateWithoutARecognizerIsUnsupported();
    TestSpeechButtonBuilder();
}
