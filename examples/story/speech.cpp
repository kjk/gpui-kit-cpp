#include "Story.h"

// crates/story/src/stories/speech_story.rs

// SCRIPT: what DemoRecognizer "hears", one phrase at a time.
static const char* const kSpeechScript[] = {
    "Speech input turns what you say into text.",
    "Any recognizer plugs in through one trait.",
};
static const int kSpeechScriptLen = 2;

// SAMPLES_PER_WORD: how much 16 kHz mono audio DemoRecognizer takes per
// word, 0.3 s.
static const int kSamplesPerWord = 4800;

// DemoSession: the one session a DemoRecognizer has open. A recognizer that
// needs no service types kSpeechScript one word per kSamplesPerWord of audio,
// whatever the audio contains.
//
// A real recognizer has the same shape: `start` opens a connection, the
// session streams audio to it and reports the service's results to the sink.
struct DemoSession {
    component::SpeechSink sink = {};
    int phraseIx = 0;
    // Words of the current phrase recognized so far.
    int words = 0;
    // Samples received since the last word.
    int samples = 0;

    static int WordCount(const char* phrase) {
        int n = 1;
        for (const char* p = phrase; *p; p++) {
            n += *p == ' ' ? 1 : 0;
        }
        return n;
    }

    // The recognized part of the current phrase, if any.
    Str SpokenTemp() const {
        if (phraseIx >= kSpeechScriptLen || words <= 0) {
            return {};
        }
        const char* phrase = kSpeechScript[phraseIx];
        int end = 0;
        int seen = 0;
        while (phrase[end]) {
            if (phrase[end] == ' ' && ++seen == words) {
                break;
            }
            end++;
        }
        // Phrases are joined verbatim, so separate sentences here.
        return fmt("%s%s", Str(phraseIx > 0 ? " " : ""), Str(phrase, end));
    }

    void Commit(App* app) {
        Str spoken = SpokenTemp();
        if (len(spoken) > 0) {
            sink.Phrase(spoken, app);
        }
        phraseIx++;
        words = 0;
    }

    void NextWord(App* app) {
        if (phraseIx >= kSpeechScriptLen) {
            return;
        }
        words++;
        if (words < WordCount(kSpeechScript[phraseIx])) {
            Str spoken = SpokenTemp();
            if (len(spoken) > 0) {
                sink.Hypothesis(spoken, app);
            }
        } else {
            Commit(app);
        }
    }

    static bool Start(void* data, component::SpeechSink sink, App* app,
                      component::RecognitionSession* out,
                      component::SpeechError*) {
        DemoSession* self = (DemoSession*)data;
        // Nothing to connect to, so audio is consumed at once.
        sink.Ready(app);
        *self = DemoSession{};
        self->sink = sink;
        out->data = self;
        out->pushAudio = &DemoSession::PushAudio;
        out->finish = &DemoSession::Finish;
        return true;
    }

    static void PushAudio(void* data, const int16_t*, int count, App* app) {
        DemoSession* self = (DemoSession*)data;
        self->samples += count;
        while (self->samples >= kSamplesPerWord) {
            self->samples -= kSamplesPerWord;
            self->NextWord(app);
        }
    }

    static void Finish(void* data, App* app) {
        DemoSession* self = (DemoSession*)data;
        self->Commit(app);
        self->sink.Finish(app);
    }
};

struct SpeechStory;

// GeneratedInput: stands in for the microphone where the story cannot
// capture — the web, as in Rust, and a Linux build without ALSA. It pushes a
// tone whose loudness rises and falls like speech.
struct GeneratedInput {
    Window* win = nullptr;
    Entity<SpeechStory> view = {};
    component::AudioSink sink = {};
    component::AudioFormat format = {};
    int timer = 0;
    uint8_t tick = 0;

    static bool Start(void* data, component::AudioFormat format,
                      component::AudioSink sink, App* app,
                      component::AudioCapture* out,
                      component::SpeechError* error);
    static void Stop(void* data) {
        GeneratedInput* self = (GeneratedInput*)data;
        if (self->timer && self->win) {
            WindowCancelTimer(self->win, self->timer);
        }
        self->timer = 0;
    }
};

// A text field the user can dictate into.
struct Dictation {
    Entity<component::SpeechState> speech = {};
    InputState input;
    Subscription subscription = {};

    El* Render(Ctx* cx, const char* id, bool showWhenUnsupported);
};

struct SpeechStory {
    Dictation custom;
    Dictation system;
    DemoSession demo;
    GeneratedInput generated;
    bool seeded = false;

    ~SpeechStory() { GeneratedInput::Stop(&generated); }

    static void OnSpeech(SpeechStory* self, Ctx* cx,
                         const component::SpeechEvent* ev, Dictation* into);
    static void OnCustom(SpeechStory* self, Ctx* cx,
                         const component::SpeechEvent* ev) {
        OnSpeech(self, cx, ev, &self->custom);
    }
    static void OnSystem(SpeechStory* self, Ctx* cx,
                         const component::SpeechEvent* ev) {
        OnSpeech(self, cx, ev, &self->system);
    }
    static void OnTone(SpeechStory* self, Ctx* cx, const TickEvent*);
    static El* Render(SpeechStory* self, Ctx* cx);
};

bool GeneratedInput::Start(void* data, component::AudioFormat format,
                           component::AudioSink sink, App*,
                           component::AudioCapture* out,
                           component::SpeechError* error) {
    GeneratedInput* self = (GeneratedInput*)data;
    if (!self->win) {
        *error = component::SpeechError::NoInputDevice();
        return false;
    }
    Stop(self);
    self->sink = sink;
    self->format = format;
    self->tick = 0;
    // CHUNK: 100 ms of audio at a time.
    self->timer = WindowSetInterval(self->win, 100,
                                    ListenTo(self->view, &SpeechStory::OnTone));
    out->data = self;
    out->stop = &GeneratedInput::Stop;
    return true;
}

void SpeechStory::OnTone(SpeechStory* self, Ctx* cx, const TickEvent*) {
    GeneratedInput* g = &self->generated;
    if (!g->timer) {
        return;
    }
    int n = (int)(g->format.sampleRate / 10) * (int)g->format.channels;
    int16_t* samples =
        (int16_t*)Alloc(GetTempArena(), n * (int)sizeof(int16_t));
    if (!samples) {
        return;
    }
    g->tick++;
    float loudness = 0.05f + 0.25f * fabsf(sinf((float)g->tick * 0.9f));
    for (int ix = 0; ix < n; ix++) {
        samples[ix] = (int16_t)(sinf((float)ix * 0.07f) * loudness * 32767.f);
    }
    g->sink.Push(samples, n, cx->app);
}

void SpeechStory::OnSpeech(SpeechStory*, Ctx* cx,
                           const component::SpeechEvent* ev, Dictation* into) {
    if (ev->kind == component::SpeechEventKind::Final && len(ev->text) > 0) {
        InputInsert(&into->input, cx, ev->text);
    } else if (ev->kind == component::SpeechEventKind::Error) {
        WindowPushNotification(
            cx, component::Notification::Error(ev->error.Display(cx->a)));
    }
}

El* Dictation::Render(Ctx* cx, const char* id, bool showWhenUnsupported) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    const component::SpeechState* state = speech.Get(cx->app);
    component::SpeechStatus status =
        state ? state->Status() : component::SpeechStatus::Idle;

    El* suffix = Div(a)->FlexRow()->ItemsCenter()->Gap(8);
    if (component::SpeechStatusIsCapturing(status)) {
        Style width;
        width.width = 48;
        suffix->Child(component::SpeechWaveform::New(cx, speech)
                          ->Refine(width, StyleFieldWidth)
                          ->WithSize(UiSize::XSmall)
                          ->IntoEl());
    }
    suffix->Child(component::SpeechButton::New(cx, speech)
                      ->WithSize(UiSize::XSmall)
                      ->ShowWhenUnsupported(showWhenUnsupported)
                      ->IntoEl());

    El* col = Div(a)->FlexCol()->W(kFill)->Gap(8);
    col->Child(
        component::Input::New(cx, Str(id), &input)->Suffix(suffix)->IntoEl());
    if (state && component::SpeechStatusIsActive(status)) {
        col->Child(
            StoryTxt(cx, StrDup(a, state->TranscriptTemp()), 14, th.mutedFg));
    }
    return col;
}

El* SpeechStory::Render(SpeechStory* self, Ctx* cx) {
    Arena* a = cx->a;
    if (!self->seeded) {
        self->seeded = true;
        self->generated.win = cx->win;
        self->generated.view = Entity<SpeechStory>{cx->self};

        component::SpeechRecognizer demo;
        demo.data = &self->demo;
        demo.start = &DemoSession::Start;
        component::AudioInput generated;
        generated.data = &self->generated;
        generated.start = &GeneratedInput::Start;

        self->custom.speech = component::SpeechStateNew(cx->app);
        self->custom.speech.Get(cx->app)->Recognizer(demo);
        if (!component::Microphone::IsSupported()) {
            self->custom.speech.Get(cx->app)->Input(generated);
        }
        self->system.speech = component::SpeechStateNew(cx->app);
        InputSetPlaceholder(&self->custom.input, StrL("Type or dictate"));
        InputSetPlaceholder(&self->system.input, StrL("Type or dictate"));
        self->custom.subscription =
            Subscribe(cx, self->custom.speech, &SpeechStory::OnCustom);
        self->system.subscription =
            Subscribe(cx, self->system.speech, &SpeechStory::OnSystem);
    }

    El* page = Div(a)->FlexCol()->SizeFull()->JustifyStart()->Gap(12);

    El* custom = StorySection(
        cx, "With a custom recognizer",
        "A recognizer defined in this story types a scripted sentence while "
        "it receives audio.");
    StorySectionBody(custom)->W(512);
    StorySectionAdd(custom, self->custom
                                .Render(cx, "speech-custom-input", false));
    page->Child(custom);

    El* system = StorySection(
        cx, "System recognizer",
        "Recognizes speech on macOS and Windows. Elsewhere, and in an app "
        "without the required usage descriptions, the button is disabled.");
    StorySectionBody(system)->W(512);
    StorySectionAdd(system, self->system
                                .Render(cx, "speech-system-input", true));
    page->Child(system);
    return page;
}

STORY_PAGE(StorySpeech, SpeechStory);
