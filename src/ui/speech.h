#ifndef GPUI_SRC_UI_SPEECH_H_
#define GPUI_SRC_UI_SPEECH_H_
/* Speech input — crates/ui/src/speech/

   Capture audio, recognize it and hand the text to the caller. SpeechState
   owns a session and SpeechButton and SpeechWaveform render it. Recognition
   and audio capture are both replaceable: fill a SpeechRecognizer to use any
   speech service and an AudioInput to feed audio from anywhere.

   A state without its own recognizer falls back to the SystemRecognizer of
   macOS or Windows, and captures from the Microphone. Linux has no system
   recognizer, so speech input works there only with an application
   recognizer. Rust puts both behind its `speech` feature; here they are
   always in, written against the OS in sys/audio_input.h and
   sys/speech_recognizer.h. */

#include "sys/speech_recognizer.h"
#include "ui/sizing.h"

namespace gpui {

namespace component {

// The PCM format a SpeechRecognizer consumes.
//
// Audio always arrives as interleaved signed 16-bit samples; an AudioInput
// converts whatever its device produces to this rate and channel count. The
// default is 16 kHz mono, the format most speech services expect.
struct AudioFormat {
    uint32_t sampleRate = 16000;
    uint16_t channels = 1;

    // A format of `sampleRate` samples per second on each of `channels`.
    static AudioFormat New(uint32_t sampleRate, uint16_t channels) {
        return AudioFormat{sampleRate, channels};
    }
    // Samples per second of each channel.
    uint32_t SampleRate() const { return sampleRate; }
    // Number of interleaved channels.
    uint16_t Channels() const { return channels; }
};
inline bool operator==(AudioFormat a, AudioFormat b) {
    return a.sampleRate == b.sampleRate && a.channels == b.channels;
}
inline bool operator!=(AudioFormat a, AudioFormat b) {
    return !(a == b);
}

enum class SpeechErrorKind : uint8_t {
    // The user or the system denied access to the microphone.
    PermissionDenied,
    // No audio input device is available.
    NoInputDevice,
    // Speech input is not supported on this platform or build.
    Unsupported,
    // The audio input failed or its device went away.
    Input,
    // The recognizer failed, e.g. it could not reach its service.
    Recognizer
};

// Why a speech session failed. Rust's Input and Recognizer arms carry an
// `Arc<anyhow::Error>`; what a caller reads out of one is its message, which
// is what this keeps.
struct SpeechError {
    SpeechErrorKind kind = SpeechErrorKind::Unsupported;
    char message[160] = {};

    static SpeechError PermissionDenied();
    static SpeechError NoInputDevice();
    static SpeechError Unsupported();
    // A SpeechErrorKind::Input from any error's message.
    static SpeechError Input(Str message);
    // A SpeechErrorKind::Recognizer from any error's message.
    static SpeechError Recognizer(Str message);
    // `impl Display`.
    Str Display(Arena* a) const;
};

struct SpeechState;

// Where a SpeechRecognizer reports its session's progress.
//
// The sink is a plain value and may outlive its session: once the session is
// stopped, cancelled or replaced, its calls are ignored. Calls are applied
// after the current update, so they never re-enter the SpeechState.
struct SpeechSink {
    Entity<SpeechState> state = {};
    uint32_t session = 0;

    // The service is connected and consuming audio.
    void Ready(App* app) const;
    // Replace the hypothesis for the phrase being spoken.
    void Hypothesis(Str text, App* app) const;
    // Commit a recognized phrase and clear the hypothesis.
    //
    // Phrases are joined verbatim, so include any separator the language
    // needs, such as a leading space between English sentences.
    void Phrase(Str text, App* app) const;
    // The session is complete; no more results will follow.
    void Finish(App* app) const;
    // The session failed.
    void Error(const SpeechError& error, App* app) const;
};

// Where an AudioInput delivers captured audio.
//
// Like SpeechSink, it is a plain value, ignored once its session ends and
// applied after the current update.
struct AudioSink {
    Entity<SpeechState> state = {};
    uint32_t session = 0;

    // Deliver interleaved PCM samples in the requested AudioFormat. The
    // samples are copied.
    void Push(const int16_t* samples, int count, App* app) const;
    // Capture failed; the session ends with this error.
    void Error(const SpeechError& error, App* app) const;
};

// One running recognition, opened by SpeechRecognizer::start. Rust's trait
// object; `drop` is its Drop, which cancels the session.
struct RecognitionSession {
    void* data = nullptr;
    // Deliver interleaved PCM samples in the recognizer's AudioFormat.
    void (*pushAudio)(void* data, const int16_t* samples, int count,
                      App* app) = nullptr;
    // No more audio will arrive. Flush what is buffered and call
    // SpeechSink::Finish once the final result is in.
    void (*finish)(void* data, App* app) = nullptr;
    void (*drop)(void* data) = nullptr;
};

// Turns speech into text, e.g. by streaming audio to a cloud service.
//
// Fill it for the service the application uses and pass it to
// SpeechState::Recognizer. `data` must outlive the state.
//
// A session runs as follows:
//
// 1. `start` opens a session. Connecting may take a while, so return
//    immediately and buffer the audio pushed in the meantime; call
//    SpeechSink::Ready once the service accepts audio.
// 2. RecognitionSession::pushAudio delivers PCM in `audioFormat`. Report
//    results through SpeechSink::Hypothesis and SpeechSink::Phrase as they
//    arrive.
// 3. RecognitionSession::finish means the user stopped talking: send the
//    remaining audio, wait for the last result, then call SpeechSink::Finish.
//
// Dropping the RecognitionSession cancels it: close the connection and report
// nothing more. Every SpeechSink method may be called from any point on the
// main thread, including from inside `start` or `pushAudio`.
struct SpeechRecognizer {
    void* data = nullptr;
    // The audio format this recognizer consumes; null is 16 kHz mono.
    AudioFormat (*audioFormat)(void* data) = nullptr;
    // Whether the recognizer can start a session now; null is `true`.
    //
    // Answer false while it cannot work, e.g. before the user signs in; the
    // SpeechButton then renders disabled. Called on every render, so keep it
    // cheap.
    bool (*isAvailable)(void* data, const App* app) = nullptr;
    // Open a session that reports its results to `sink`. On failure fill
    // `error` and answer false.
    bool (*start)(void* data, SpeechSink sink, App* app,
                  RecognitionSession* out, SpeechError* error) = nullptr;

    bool IsSet() const { return start != nullptr; }
};

// What AudioInput::start hands back — Rust's Subscription: capture runs until
// `stop` is called, which the state does exactly once.
struct AudioCapture {
    void* data = nullptr;
    void (*stop)(void* data) = nullptr;
};

// A source of audio for a SpeechState, such as the microphone. Fill it to
// feed audio from anywhere, e.g. a file in tests. `data` must outlive the
// state.
struct AudioInput {
    void* data = nullptr;
    // Start capturing `format` audio into `sink`. On failure fill `error` and
    // answer false.
    bool (*start)(void* data, AudioFormat format, AudioSink sink, App* app,
                  AudioCapture* out, SpeechError* error) = nullptr;

    bool IsSet() const { return start != nullptr; }
};

// ─── microphone.rs ────────────────────────────────────────────────────────

// The default audio input device, captured through the platform's audio API
// (Core Audio, WASAPI or ALSA).
//
// The device's own format is mixed down to mono and resampled to the format
// the recognizer asks for.
//
// On macOS the application's `Info.plist` must describe why it uses the
// microphone (`NSMicrophoneUsageDescription`), or the system refuses access
// without asking.
struct Microphone {
    // Whether this build can capture from a device at all: false in the
    // browser and on a Linux build without ALSA.
    static bool IsSupported();
    // The default input device's name, or an empty string when there is
    // none. Not in Rust's Microphone: examples/speech asks cpal itself, and
    // an example here names no audio API.
    static TempStr DeviceNameTemp();
    // The AudioInput to hand SpeechState::Input. It carries no state of its
    // own, so it outlives whatever it is given to.
    static AudioInput Input();
};

// microphone.rs Converter: converts mono `float` audio at a device's rate to
// interleaved `int16_t` audio in the recognizer's format, carrying state
// across chunks. Rust keeps it private to its cpal Microphone; the
// microphone is not ported yet, and this half of it — which any AudioInput
// reading a device needs — is.
struct SpeechAudioConverter {
    // Source samples per output sample.
    double step = 1;
    // Position of the next output sample, in source samples, where 0 is the
    // last sample of the previous chunk.
    double position = 1;
    float previous = 0;
    // LowPass: two cascaded one-pole low-pass filters, 12 dB per octave.
    // Downsampling folds everything above the new Nyquist rate back into the
    // band speech lives in; filter it out first.
    bool lowPass = false;
    float alpha = 0;
    float stages[2] = {};
    int channels = 1;

    static SpeechAudioConverter New(uint32_t sourceRate, AudioFormat format);
    // Appends the converted samples to `out`.
    void Convert(const float* input, int n, Vec<int16_t>& out);
};

// ─── level.rs ─────────────────────────────────────────────────────────────

// LEVEL_INTERVAL: how much audio one level covers, short enough for the bars
// to follow syllables.
const int kSpeechLevelIntervalMs = 25;
// LEVEL_HISTORY: how many recent levels are kept, enough to fill a wide
// waveform (about six seconds of audio).
const int kSpeechLevelHistory = 256;

// Turns a stream of PCM into smoothed input levels, one per
// kSpeechLevelIntervalMs of audio, carrying partial intervals across pushes.
struct LevelMeter {
    // Samples per level: the interval at the stream's rate and channels.
    int window = 400;
    double sum = 0;
    int count = 0;
    float smoothed = 0;
    // A ring of the last kSpeechLevelHistory levels, oldest at `first`.
    float levels[kSpeechLevelHistory] = {};
    int first = 0;
    int nLevels = 0;
    // TimeNow() when the newest level was recorded, to scroll smoothly
    // between levels; negative is None.
    double lastLevelAt = -1;

    // Clear the history and measure a stream of `sampleRate` × `channels`.
    void Reset(uint32_t sampleRate, uint16_t channels);
    // Measure `samples`; answers whether a new level was recorded.
    bool Push(const int16_t* samples, int n);
    int LevelsLen() const { return nLevels; }
    // Oldest first.
    float LevelAt(int ix) const {
        return levels[(first + ix) % kSpeechLevelHistory];
    }
};

int SpeechLevelWindowFor(uint32_t sampleRate, uint16_t channels);
// The loudness of an RMS amplitude in 0..=1, mapping -50 dBFS..0 dBFS
// linearly so that normal speech fills most of the range.
float SpeechLevelOfRms(double rms);
// One step of fast-attack, slow-release smoothing from `previous` toward
// `raw`.
float SpeechLevelSmooth(float previous, float raw);

// ─── system/mod.rs ────────────────────────────────────────────────────────

// The operating system's speech recognizer.
//
// - macOS: `SFSpeechRecognizer`, recognizing on the device only. A language
//   the device cannot recognize offline is not available, so audio never
//   leaves the machine.
// - Windows: `Windows.Media.SpeechRecognition`. Dictation needs the
//   language's speech pack and the "Online speech recognition" privacy
//   setting, and runs through Microsoft's online service.
// - Other platforms: never available.
//
// A SpeechState without its own recognizer uses this one; create it directly
// to choose the language. It must outlive the state it is given to.
struct SystemRecognizer {
    // Heap, owned. Empty is the system's current language.
    Str locale = {};
    // The platform recognizer, made on first use.
    SysSpeechRecognizer* platform = nullptr;
    bool platformMade = false;

    SystemRecognizer() = default;
    SystemRecognizer(const SystemRecognizer&) = delete;
    SystemRecognizer& operator=(const SystemRecognizer&) = delete;
    ~SystemRecognizer();

    // Recognize `locale`, a BCP 47 language tag such as `en-US` or `zh-CN`,
    // instead of the system's language.
    SystemRecognizer* Locale(Str locale);
    // Whether this platform has a recognizer for the language at all.
    bool IsSupported();
    // The SpeechRecognizer to hand SpeechState::Recognizer.
    SpeechRecognizer AsRecognizer();
};

// ─── state.rs ─────────────────────────────────────────────────────────────

// Where a SpeechState is in its session.
enum class SpeechStatus : uint8_t {
    // No session is running.
    Idle,
    // Audio is being captured while the recognizer connects.
    Connecting,
    // Audio is being captured and recognized.
    Recording,
    // Capture stopped; waiting for the recognizer's final result.
    Stopping
};
// Whether a session is running.
inline bool SpeechStatusIsActive(SpeechStatus s) {
    return s != SpeechStatus::Idle;
}
// Whether the microphone is capturing.
inline bool SpeechStatusIsCapturing(SpeechStatus s) {
    return s == SpeechStatus::Connecting || s == SpeechStatus::Recording;
}

enum class SpeechEventKind : uint8_t {
    // A session started and audio is being captured.
    Started,
    // The transcript changed: every committed phrase followed by the current
    // hypothesis. Later events supersede earlier ones.
    Partial,
    // The session ended normally with this transcript, possibly empty.
    Final,
    // The session was cancelled and its transcript discarded.
    Cancelled,
    // The session failed and ended.
    Error
};

// Events emitted by SpeechState. `text` is the transcript of a Partial or a
// Final and is only good for the length of the handler; `error` is the
// failure of an Error.
struct SpeechEvent {
    SpeechEventKind kind = SpeechEventKind::Started;
    Str text = {};
    SpeechError error = {};
};

// One sink call waiting for the current update to end.
struct SpeechDeferredOp;

// The state of a speech input: captures audio from an AudioInput, feeds it to
// a SpeechRecognizer and tracks the transcript.
//
// Render it with SpeechButton and SpeechWaveform, and subscribe to
// SpeechEvent to receive the text.
//
// The recognizer is, in order: the one passed to Recognizer(); else the
// platform's SystemRecognizer, unless SystemFallback turned it off; else
// none, and the state is not available. The input defaults to the
// Microphone.
struct SpeechState {
    SpeechRecognizer recognizer = {};
    AudioInput input = {};
    bool systemFallback = true;
    // system_recognizer: the fallback, made the first time it is asked for.
    mutable SystemRecognizer* systemRecognizer = nullptr;
    // DEFAULT_STOP_TIMEOUT.
    int stopTimeoutMs = 3000;
    SpeechStatus status = SpeechStatus::Idle;

    // Session: the running one, when `hasSession`.
    bool hasSession = false;
    uint32_t sessionId = 0;
    // Dropping this stops capture.
    AudioCapture capture = {};
    bool hasCapture = false;
    RecognitionSession recognition = {};
    // The stop timeout's window timer, and the window it was armed on.
    int stopTimer = 0;
    Window* stopTimerWin = nullptr;

    uint32_t nextSession = 0;
    // Heap strings, owned.
    Str committed = {};
    Str hypothesis = {};
    LevelMeter meter = {};

    // Sink calls, applied after the update they were made in: Rust's
    // `cx.defer`. They are posted to a window of the application; an
    // application with no window applies them when the state's own update
    // ends, or at once when the call came from outside one.
    Vec<SpeechDeferredOp*> deferred;
    bool drainPosted = false;
    int updateDepth = 0;

    // cx.weak_entity(); SpeechStateNew stamps it.
    Entity<SpeechState> self = {};

    ~SpeechState();

    // Recognize speech with `value` instead of the system's.
    SpeechState* Recognizer(const SpeechRecognizer& value);
    // Capture audio from `value` instead of the microphone.
    SpeechState* Input(const AudioInput& value);
    // Whether to fall back to the platform's recognizer when no Recognizer is
    // set, default true.
    //
    // On Windows the system recognizer dictates through Microsoft's online
    // service; turn this off when audio must not leave the application.
    SpeechState* SystemFallback(bool value);
    // Set how long Stop waits for the recognizer's final result before ending
    // the session with the transcript so far, default 3 seconds.
    SpeechState* StopTimeout(int ms);

    // Whether a recognizer and an input are configured, so that speech input
    // can work on this platform at all.
    bool HasRecognizer() const;
    // Whether a session can start now: HasRecognizer and the recognizer
    // reports itself available.
    bool IsAvailable(const App* app) const;
    // Where the state is in its session.
    SpeechStatus Status() const { return status; }
    // The transcript of the current or last session: every committed phrase
    // followed by the current hypothesis. A temp-arena string.
    TempStr TranscriptTemp() const;
    // Recent input levels in 0..=1, oldest first, one per 25 ms of audio.
    // Peaks rise at once and fall back smoothly; background noise reads as 0.
    int LevelsLen() const { return meter.LevelsLen(); }
    float LevelAt(int ix) const { return meter.LevelAt(ix); }
    double LastLevelAt() const { return meter.lastLevelAt; }

    // Start a session. Does nothing while one is running.
    //
    // When the recognizer or the input fails to start, emits
    // SpeechEvent Error and stays idle.
    void Start(Ctx* cx);
    // Stop capturing and wait for the final result, which arrives as
    // SpeechEvent Final.
    void Stop(Ctx* cx);
    // End the session at once and discard its transcript.
    void Cancel(Ctx* cx);
    // Start a session when idle, otherwise stop the running one.
    void Toggle(Ctx* cx);

    bool IsSession(uint32_t id) const { return hasSession && sessionId == id; }

    static void OnToggle(SpeechState* self, Ctx* cx, const ClickEvent* ev);
    static void OnDrain(SpeechState* self, Ctx* cx, const void*);
    static void OnStopTimeout(SpeechState* self, Ctx* cx, const TickEvent* ev,
                              int64_t session);
};

// `cx.new(|cx| SpeechState::new(cx))`.
Entity<SpeechState> SpeechStateNew(App* app);

// ─── button.rs ────────────────────────────────────────────────────────────

// The button that starts and stops a SpeechState's session.
//
// Shows a microphone at rest and a stop glyph, pressed, while capturing; a
// click toggles the session. While the final result is pending it shows a
// spinner and ignores clicks.
//
// Renders nothing when the state has no recognizer on this platform, unless
// ShowWhenUnsupported is set, and renders disabled while the recognizer
// reports itself unavailable.
struct SpeechButton {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Str id = {};
    Entity<SpeechState> state = {};
    UiSize size = UiSize::Medium;
    bool disabled = false;
    bool showWhenUnsupported = false;
    Style style = {};
    uint32_t styleSet = 0;

    // A button for `state`.
    static SpeechButton* New(Ctx* cx, Entity<SpeechState> state);
    // Render a disabled button, instead of nothing, when the state has no
    // recognizer on this platform. Default false.
    SpeechButton* ShowWhenUnsupported(bool show);
    SpeechButton* WithSize(UiSize s);
    SpeechButton* Disabled(bool v);
    // Refines the button, e.g. its size or corner radius, to match the
    // controls around it.
    SpeechButton* Refine(const Style& refinement, uint32_t fields);
    El* IntoEl();
};

// ─── waveform.rs ──────────────────────────────────────────────────────────

// A live waveform of a SpeechState's input levels.
//
// Bars fill the waveform's width and grow up and down from its midline. The
// newest level enters at the trailing edge and older ones scroll toward the
// leading edge, redrawn every frame while audio is captured. Without capture
// the bars rest as a muted baseline. With reduced motion the bars still show
// each level but do not scroll between them.
//
// Size the waveform like any element, e.g. a kFill width to span a row; it is
// 96 px wide by default, and its height follows WithSize.
struct SpeechWaveform {
    Arena* a = nullptr;
    Ctx* cx = nullptr;
    Entity<SpeechState> state = {};
    UiSize size = UiSize::Medium;
    Style style = {};
    uint32_t styleSet = 0;

    // A waveform for `state`.
    static SpeechWaveform* New(Ctx* cx, Entity<SpeechState> state);
    SpeechWaveform* WithSize(UiSize s);
    SpeechWaveform* Refine(const Style& refinement, uint32_t fields);
    float Height() const;
    El* IntoEl();
};

// bar_rects: the bars for `levels` (oldest first) in `bounds`: the newest at
// the trailing edge, `scroll` of a step further toward the leading edge, each
// centered on the midline and at least as tall as it is wide. Writes up to
// `cap` bars to `out` and answers how many.
int SpeechWaveformBarRects(Bounds bounds, const float* levels, int nLevels,
                           float scroll, float bar, float gap, Bounds* out,
                           int cap);

} // namespace component

template <>
struct EventEmitter<component::SpeechState, component::SpeechEvent> {};

} // namespace gpui
#endif // GPUI_SRC_UI_SPEECH_H_
