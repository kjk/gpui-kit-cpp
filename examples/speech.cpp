#include "gpui.h"

#include "speech_demo.h"

#include <ctype.h>
#include <stdio.h>

using namespace gpui;

// Port of examples/speech/src/main.rs.
//
// Speech: a notepad you can talk into, built on GPUI Component's speech
// input. It doubles as a test bench for the platform recognizers: the sidebar
// reports what this machine supports and the session log records every event.
//
// `bun cmd/run.ts speech` opens the app; `speech --check` prints the same
// checks to the terminal and exits. A Windows build is a GUI program, so
// there the checks are only seen when its output is piped or redirected
// (`speech.exe --check | more`).
//
// Rust embeds three extra icons with `icon_assets!`; here they are files
// under assets/speech/icons, found the way examples/app_assets finds its own.
// The usage descriptions macOS asks for are examples/speech_Info.plist, which
// cmd/build.ts links into the binary as Rust's build.rs does.

static uint32_t ActToggleSpeech() {
    static uint32_t id = ActionOf(StrL("speech::ToggleSpeech"));
    return id;
}

// TOGGLE_KEYS
static const char* const kToggleKeys = "secondary-shift-d";
// LOG_LIMIT: most log entries kept; older ones scroll away.
static const int kLogLimit = 200;

// Where the text comes from.
enum class Engine : uint8_t {
    // The operating system's recognizer.
    System,
    // DemoRecognizer: a scripted passage, no service needed.
    Demo
};

struct Language {
    const char* tag;
    const char* name;
};

// languages()
static const Language kLanguages[] = {
    {"en-US", "English (US)"}, {"en-GB", "English (UK)"}, {"zh-CN", "简体中文"},
    {"zh-HK", "中文（香港）"}, {"ja-JP", "日本語"},
};
static const int kNLanguages = 5;
static component::SearchableItem gLanguageItems[kNLanguages];

enum class Tone : uint8_t {
    Neutral,
    Progress,
    Success,
    Danger
};

enum class LogLabel : uint8_t {
    Started,
    Partial,
    Final,
    Cancelled,
    Error
};

static const char* LogLabelText(LogLabel label) {
    switch (label) {
        case LogLabel::Started:
            return "Started";
        case LogLabel::Partial:
            return "Partial";
        case LogLabel::Final:
            return "Final";
        case LogLabel::Cancelled:
            return "Cancelled";
        case LogLabel::Error:
            return "Error";
    }
    return "";
}

// One line of the session log.
struct LogEntry {
    // Seconds since the app opened.
    double at = 0;
    Tone tone = Tone::Neutral;
    LogLabel label = LogLabel::Started;
    // Heap, owned by the log.
    Str text = {};
};

// platform_name()
static const char* PlatformName() {
    Str platform = Str(PlatShellPlatformName());
    if (StrEq(platform, StrL("macos"))) {
        return "macOS";
    }
    if (StrEq(platform, StrL("windows"))) {
        return "Windows";
    }
    if (StrEq(platform, StrL("linux"))) {
        return "Linux";
    }
    return "Other";
}

static Str UnavailableHint() {
    Str platform = Str(PlatShellPlatformName());
    if (StrEq(platform, StrL("macos"))) {
        return StrL(
            "This Mac can’t recognize the language offline, or speech "
            "recognition access is off in System Settings › Privacy & "
            "Security.");
    }
    if (StrEq(platform, StrL("windows"))) {
        return StrL(
            "Install the language’s speech pack and turn on Online speech "
            "recognition in Settings › Privacy & security › Speech.");
    }
    return StrL("The recognizer can’t start right now.");
}

// format_duration
static TempStr FormatDurationTemp(double seconds) {
    int64_t whole = (int64_t)seconds;
    return fmt("%d:%02d", (int)(whole / 60), (int)(whole % 60));
}

// format_timestamp
static TempStr FormatTimestampTemp(double seconds) {
    int64_t millis = (int64_t)(seconds * 1000.);
    return fmt("%02d:%02d.%03d", (int)(millis / 60000),
               (int)(millis / 1000 % 60), (int)(millis % 1000));
}

static int CharCount(Str s) {
    int n = 0;
    for (int ix = 0; ix < len(s); ix++) {
        n += ((uint8_t)s.s[ix] & 0xC0) != 0x80 ? 1 : 0;
    }
    return n;
}

struct SpeechApp {
    Engine engine = Engine::System;
    char language[16] = "en-US";
    Entity<component::SpeechState> speech = {};
    // The two recognizers a state is handed, which have to outlive it.
    component::SystemRecognizer system;
    DemoRecognizer demo;
    InputState notes;
    Entity<component::SelectState> languageSelect = {};
    // input_device: empty is None.
    char inputDevice[256] = {};
    // When the current or last session started; negative is None.
    double sessionStarted = -1;
    // When the app opened; log times count from here.
    double opened = 0;
    LogEntry log[kLogLimit] = {};
    int nLog = 0;
    float logScrollY = 0;
    Subscription speechSubscription = {};
    bool seeded = false;

    ~SpeechApp() {
        for (int ix = 0; ix < nLog; ix++) {
            StrFree(log[ix].text);
        }
    }

    // input_device_name()
    void ReadInputDevice() {
        Str name = component::Microphone::DeviceNameTemp();
        int n = len(name) < (int)sizeof(inputDevice) - 1
                    ? len(name)
                    : (int)sizeof(inputDevice) - 1;
        if (n > 0) {
            memcpy(inputDevice, name.s, (size_t)n);
        }
        inputDevice[n] = 0;
    }

    // new_speech() and subscribe_speech().
    void NewSpeech(Ctx* cx) {
        speech = component::SpeechStateNew(cx->app);
        component::SpeechState* state = speech.Get(cx->app);
        if (engine == Engine::System) {
            system.Locale(Str(language));
            state->Recognizer(system.AsRecognizer());
        } else {
            demo.Language(Str(language));
            state->Recognizer(demo.AsRecognizer());
        }
        speechSubscription = Subscribe(cx, speech, &SpeechApp::OnSpeechEvent);
    }

    // Recreate the speech state for the chosen engine and language.
    void RebuildSpeech(Ctx* cx) {
        if (component::SpeechState* state = speech.Get(cx->app)) {
            state->Cancel(cx);
        }
        EntityUnsubscribe(cx->app, speechSubscription);
        // Replacing the handle drops the entity in Rust.
        EntityDrop(cx->app, speech.id);
        NewSpeech(cx);
        ReadInputDevice();
        Notify(cx);
    }

    void SetEngine(Engine value, Ctx* cx) {
        if (engine != value) {
            engine = value;
            RebuildSpeech(cx);
        }
    }

    void PushLog(Tone tone, LogLabel label, Str text) {
        if (nLog == kLogLimit) {
            StrFree(log[0].text);
            memmove(&log[0], &log[1], sizeof(LogEntry) * (kLogLimit - 1));
            nLog--;
        }
        LogEntry* entry = &log[nLog++];
        entry->at = TimeNow() - opened;
        entry->tone = tone;
        entry->label = label;
        entry->text = StrDup(text);
    }

    void InsertIntoNotes(Str text, Ctx* cx) {
        // Keep dictated passages apart from what is already there.
        Str value = InputValue(&notes);
        uint8_t last = len(value) > 0 ? (uint8_t)value.s[len(value) - 1] : ' ';
        bool needsSpace = last < 0x80 && !isspace(last);
        InputInsert(&notes, cx, needsSpace ? Str(fmt(" %s", text)) : text);
        InputFocus(&notes, cx);
    }

    static void OnToggleSpeech(SpeechApp* self, Ctx* cx, const ActionEvent*) {
        if (component::SpeechState* state = self->speech.Get(cx->app)) {
            state->Toggle(cx);
        }
    }

    static void OnSpeechEvent(SpeechApp* self, Ctx* cx,
                              const component::SpeechEvent* event) {
        switch (event->kind) {
            case component::SpeechEventKind::Started:
                self->sessionStarted = TimeNow();
                self->PushLog(Tone::Progress, LogLabel::Started,
                              StrL("Listening"));
                break;
            case component::SpeechEventKind::Partial: {
                // Partial results arrive many times a second; keep one line
                // per stretch of them.
                LogEntry* last =
                    self->nLog > 0 ? &self->log[self->nLog - 1] : nullptr;
                if (last && last->label == LogLabel::Partial) {
                    StrFree(last->text);
                    last->text = StrDup(event->text);
                    last->at = TimeNow() - self->opened;
                } else {
                    self->PushLog(Tone::Neutral, LogLabel::Partial,
                                  event->text);
                }
                break;
            }
            case component::SpeechEventKind::Final:
                if (len(event->text) == 0) {
                    self->PushLog(Tone::Neutral, LogLabel::Final,
                                  StrL("No speech recognized"));
                } else {
                    self->PushLog(Tone::Success, LogLabel::Final, event->text);
                    self->InsertIntoNotes(event->text, cx);
                }
                break;
            case component::SpeechEventKind::Cancelled:
                self->PushLog(Tone::Neutral, LogLabel::Cancelled,
                              StrL("Transcript discarded"));
                break;
            case component::SpeechEventKind::Error: {
                Str error = event->error.Display(cx->a);
                self->PushLog(Tone::Danger, LogLabel::Error, error);
                WindowPushNotification(
                    cx, component::Notification::Error(StrDup(
                            cx->a, fmt("Couldn’t dictate. %s.", error))));
                break;
            }
        }
        Notify(cx);
    }

    static void OnLanguage(SpeechApp* self, Ctx* cx,
                           const component::SelectEvent* event) {
        // SelectEvent::Confirm(Some(tag)).
        if (!event->hasValue || len(event->value) <= 0 ||
            len(event->value) >= (int)sizeof(self->language)) {
            return;
        }
        memcpy(self->language, event->value.s, (size_t)len(event->value));
        self->language[len(event->value)] = 0;
        self->RebuildSpeech(cx);
    }

    static void OnEngine(SpeechApp* self, Ctx* cx,
                         const component::ButtonGroupEvent* clicks) {
        self->SetEngine(clicks->Contains(1) ? Engine::Demo : Engine::System,
                        cx);
    }

    static void OnClearNotes(SpeechApp* self, Ctx* cx, const ClickEvent*) {
        InputSetValue(&self->notes, Str(""));
        Notify(cx);
    }

    static void OnDiscard(SpeechApp* self, Ctx* cx, const ClickEvent*) {
        if (component::SpeechState* state = self->speech.Get(cx->app)) {
            state->Cancel(cx);
        }
    }

    static void OnClearLog(SpeechApp* self, Ctx* cx, const ClickEvent*) {
        for (int ix = 0; ix < self->nLog; ix++) {
            StrFree(self->log[ix].text);
            self->log[ix].text = {};
        }
        self->nLog = 0;
        self->logScrollY = 0;
        Notify(cx);
    }

    static void OnLogScroll(SpeechApp* self, Ctx* cx, const ScrollEvent* ev) {
        self->logScrollY = ev->offsetY;
        Notify(cx);
    }

    // SpeechApp::new, which needs the window: run by the first render.
    void Seed(Ctx* cx) {
        seeded = true;
        opened = TimeNow();
        NewSpeech(cx);

        notes.kind = InputKind::Textarea;
        InputSetPlaceholder(
            &notes,
            StrL("Start typing, or dictate with the microphone below."));

        for (int ix = 0; ix < kNLanguages; ix++) {
            gLanguageItems[ix].title = Str(kLanguages[ix].name);
            gLanguageItems[ix].value = Str(kLanguages[ix].tag);
        }
        languageSelect = component::SelectState::New(cx->app);
        if (component::SelectState* select = languageSelect.Get(cx->app)) {
            select->SetItems(gLanguageItems, kNLanguages);
            // Some(IndexPath::default()), before anything listens.
            select->SetSelectedIndex(0, cx);
        }
        Subscribe(cx, languageSelect, &SpeechApp::OnLanguage);
        InputFocus(&notes, cx);
        ReadInputDevice();
    }

    El* RenderSidebar(Ctx* cx);
    El* RenderChecks(Ctx* cx);
    El* RenderNotesHeader(Ctx* cx);
    El* RenderSpeechBar(Ctx* cx);
    El* RenderLog(Ctx* cx);
    static El* Render(SpeechApp* self, Ctx* cx);
};

static El* SidebarSection(Ctx* cx, const char* title) {
    const Theme& th = ThemeNow(cx->app);
    return Div(cx->a)->FlexCol()->Gap(8)->Child(
        TextEl(cx->a, Str(title))->Font(12)->Medium()->Fg(th.mutedFg));
}

static El* Caption(Ctx* cx, Str text) {
    const Theme& th = ThemeNow(cx->app);
    return TextEl(cx->a, text)->Font(12)->Fg(th.mutedFg)->Wrap();
}

static El* CheckRow(Ctx* cx, const char* label, El* value) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    return Div(a)
        ->FlexRow()
        ->Gap(12)
        ->ItemsCenter()
        ->JustifyBetween()
        ->Child(TextEl(a, Str(label))->Font(14)->Fg(th.mutedFg)->Shrink(0))
        ->Child(
            Div(a)->FlexRow()->Flex1()->MinW(0)->JustifyEnd()->Child(value));
}

static El* LogRow(Ctx* cx, const LogEntry* entry) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    Rgba color = th.mutedFg;
    switch (entry->tone) {
        case Tone::Neutral:
            break;
        case Tone::Progress:
            color = th.info;
            break;
        case Tone::Success:
            color = th.success;
            break;
        case Tone::Danger:
            color = th.danger;
            break;
    }
    return Div(a)
        ->FlexRow()
        ->Gap(12)
        ->ItemsStart()
        ->Child(TextEl(a, StrDup(a, FormatTimestampTemp(entry->at)))
                    ->Font(12)
                    ->FontFamily(th.monoFontFamily)
                    ->Fg(th.mutedFg)
                    ->Shrink(0))
        ->Child(Div(a)->W(64)->Shrink(0)->Child(
            TextEl(a, Str(LogLabelText(entry->label)))
                ->Font(12)
                ->Medium()
                ->Fg(color)))
        ->Child(Div(a)->Flex1()->MinW(0)->Child(
            TextEl(a, StrDup(a, entry->text))->Font(12)->Wrap()));
}

El* SpeechApp::RenderSidebar(Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    const component::SpeechState* state = speech.Get(cx->app);
    bool active = state && component::SpeechStatusIsActive(state->Status());
    Str engineNote =
        engine == Engine::System
            ? StrL(
                  "The operating system’s recognizer. On macOS it only "
                  "recognizes on this Mac; on Windows it uses Microsoft’s "
                  "online service.")
            : StrL(
                  "Types a scripted passage as you speak and ends a "
                  "sentence when you pause. Needs no service, network or "
                  "speech permission.");

    // `.w_full()` on the group and `.flex_1()` on each button: the group
    // builds a row of its children, which is where both are applied.
    El* engines = component::ButtonGroup::New(cx, StrL("engine"))
                      ->WithSize(UiSize::Small)
                      ->Outline()
                      ->Disabled(active)
                      ->Child(component::Button::New(cx, StrL("engine-system"))
                                  ->Label(StrL("System"))
                                  ->Selected(engine == Engine::System))
                      ->Child(component::Button::New(cx, StrL("engine-demo"))
                                  ->Label(StrL("Demo"))
                                  ->Selected(engine == Engine::Demo))
                      ->OnClick(Listen(cx, &SpeechApp::OnEngine))
                      ->IntoEl();
    engines->W(kFill);
    for (El* button = engines->first; button; button = button->next) {
        button->Flex1();
    }

    return Div(a)
        ->FlexCol()
        ->W(272)
        ->H(kFill)
        ->Shrink(0)
        ->Gap(24)
        ->Pad(16)
        ->Bg(th.tokens.sidebar)
        ->Fg(th.sidebarFg)
        ->BorderR(1, th.sidebarBorder)
        ->Child(SidebarSection(cx, "Recognizer")
                    ->Child(engines)
                    ->Child(Caption(cx, engineNote)))
        ->Child(SidebarSection(cx, "Language")
                    ->Child(component::Select::New(cx, StrL("language"),
                                                   languageSelect)
                                ->Items(gLanguageItems, kNLanguages)
                                ->WithSize(UiSize::Small)
                                ->Disabled(active)
                                ->IntoEl()))
        ->Child(SidebarSection(cx, "Checks")->Child(RenderChecks(cx)));
}

El* SpeechApp::RenderChecks(Ctx* cx) {
    Arena* a = cx->a;
    const component::SpeechState* state = speech.Get(cx->app);
    bool supported = state && state->HasRecognizer();
    bool available = state && state->IsAvailable(cx->app);
    component::SpeechStatus status =
        state ? state->Status() : component::SpeechStatus::Idle;

    component::Tag* recognizer = nullptr;
    if (!supported) {
        recognizer = component::Tag::New(cx, StrL("Not supported"))->Danger();
    } else if (available) {
        recognizer = component::Tag::New(cx, StrL("Available"))->Success();
    } else {
        recognizer = component::Tag::New(cx, StrL("Unavailable"))->Warning();
    }
    component::Tag* session = nullptr;
    switch (status) {
        case component::SpeechStatus::Idle:
            session = component::Tag::New(cx, StrL("Idle"))->Secondary();
            break;
        case component::SpeechStatus::Connecting:
            session = component::Tag::New(cx, StrL("Connecting"))->Info();
            break;
        case component::SpeechStatus::Recording:
            session = component::Tag::New(cx, StrL("Recording"))->Info();
            break;
        case component::SpeechStatus::Stopping:
            session = component::Tag::New(cx, StrL("Finishing"))->Info();
            break;
    }
    El* input = inputDevice[0]
                    ? Div(a)->MinW(0)->Child(
                          TextEl(a, Str(inputDevice))->Font(14)->Truncate())
                    : component::Tag::New(cx, StrL("None"))
                          ->Danger()
                          ->WithSize(UiSize::Small)
                          ->Outline()
                          ->IntoEl();

    El* checks =
        Div(a)
            ->FlexCol()
            ->Gap(8)
            ->Child(CheckRow(cx, "Platform",
                             TextEl(a, Str(PlatformName()))->Font(14)))
            ->Child(CheckRow(cx, "Input device", input))
            ->Child(CheckRow(
                cx, "Recognizer",
                recognizer->Outline()->WithSize(UiSize::Small)->IntoEl()))
            ->Child(CheckRow(
                cx, "Session",
                session->Outline()->WithSize(UiSize::Small)->IntoEl()));
    if (engine == Engine::System && supported && !available) {
        checks->Child(Caption(cx, UnavailableHint()));
    }
    return checks;
}

El* SpeechApp::RenderNotesHeader(Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    int characters = CharCount(InputValue(&notes));
    Str count = characters == 0   ? StrL("Empty")
                : characters == 1 ? StrL("1 character")
                                  : StrDup(a, fmt("%d characters", characters));

    return Div(a)
        ->FlexRow()
        ->ItemsCenter()
        ->JustifyBetween()
        ->Child(Div(a)
                    ->FlexCol()
                    ->Child(TextEl(a, StrL("Notes"))->Font(18)->Semibold())
                    ->Child(TextEl(a, count)->Font(12)->Fg(th.mutedFg)))
        ->Child(component::Button::New(cx, StrL("clear-notes"))
                    ->Ghost()
                    ->WithSize(UiSize::Small)
                    ->Icon(component::ButtonIcon::New(
                        cx, component::Icon::Empty(cx)
                                ->Path(StrL("icons/eraser.svg"))))
                    ->Label(StrL("Clear"))
                    ->Disabled(characters == 0)
                    ->OnClick(Listen(cx, &SpeechApp::OnClearNotes))
                    ->IntoEl());
}

// The bar that runs a speech session: button, level, live text and controls.
El* SpeechApp::RenderSpeechBar(Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    const component::SpeechState* state = speech.Get(cx->app);
    component::SpeechStatus status =
        state ? state->Status() : component::SpeechStatus::Idle;
    Str transcript = state ? StrDup(a, state->TranscriptTemp()) : Str{};
    bool supported = state && state->HasRecognizer();
    bool available = state && state->IsAvailable(cx->app);
    bool active = component::SpeechStatusIsActive(status);
    bool capturing = component::SpeechStatusIsCapturing(status);
    bool heard = len(transcript) > 0;

    Str title = {};
    Str detail = {};
    switch (status) {
        case component::SpeechStatus::Idle:
            if (!supported) {
                title = StrL("Not supported here");
                detail = StrL(
                    "This platform has no system recognizer. Switch "
                    "to Demo to try dictation.");
            } else if (!available) {
                title = StrL("Not available");
                detail = UnavailableHint();
            } else {
                title = StrL("Ready");
                detail = StrL(
                    "Click the microphone to dictate into the note "
                    "at the cursor.");
            }
            break;
        case component::SpeechStatus::Connecting:
            title = StrL("Connecting…");
            detail = heard ? transcript
                           : StrL(
                                 "Start talking. What you say is kept while "
                                 "it connects.");
            break;
        case component::SpeechStatus::Recording: {
            Str elapsed =
                sessionStarted >= 0
                    ? Str(FormatDurationTemp(TimeNow() - sessionStarted))
                    : Str("");
            title = StrDup(a, fmt("Listening · %s", elapsed));
            detail = heard ? transcript : StrL("Start talking.");
            break;
        }
        case component::SpeechStatus::Stopping:
            title = StrL("Finishing…");
            detail = heard ? transcript : StrL("Waiting for the last words.");
            break;
    }

    El* bar = Div(a)
                  ->FlexRow()
                  ->Gap(12)
                  ->PadX(12)
                  ->PadY(10)
                  ->ItemsCenter()
                  ->Radius(th.radiusLg)
                  ->Border(1, capturing ? th.ring : th.border)
                  ->Bg(th.tokens.background);
    if (capturing) {
        // shadow_sm
        BoxShadow shadow = {0, 1, 2, 0, Rgba8(0, 0, 0, 13), false};
        bar->Shadows(&shadow, 1);
    }
    bar->Child(component::SpeechButton::New(cx, speech)
                   ->ShowWhenUnsupported(true)
                   ->WithSize(UiSize::Large)
                   ->IntoEl());
    // While recording the waveform takes the rest of the row, so the text
    // keeps a fixed column and truncates the live transcript.
    El* text = Div(a)
                   ->FlexCol()
                   ->MinW(0)
                   ->Gap(2)
                   ->Child(TextEl(a, title)->Font(14)->Medium()->Fg(
                       (supported && available) || active ? th.foreground
                                                          : th.mutedFg))
                   ->Child(TextEl(a, detail)->Font(14)->Truncate()->Fg(
                       active && heard ? th.foreground : th.mutedFg));
    if (active) {
        text->W(220)->Shrink0();
    } else {
        text->Flex1();
    }
    bar->Child(text);
    if (active) {
        bar->Child(component::SpeechWaveform::New(cx, speech)
                       ->WithSize(UiSize::Small)
                       ->IntoEl()
                       ->Flex1()
                       ->MinW(0));
        bar->Child(component::Button::New(cx, StrL("discard"))
                       ->Ghost()
                       ->WithSize(UiSize::Small)
                       ->Label(StrL("Discard"))
                       ->Tooltip(StrL("Stop without inserting the text"))
                       ->OnClick(Listen(cx, &SpeechApp::OnDiscard))
                       ->IntoEl());
    } else if (available) {
        if (component::Kbd* keys =
                component::Kbd::ForAction(cx, ActToggleSpeech())) {
            bar->Child(keys->IntoEl());
        }
    }
    return bar;
}

El* SpeechApp::RenderLog(Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);

    El* header =
        Div(a)
            ->FlexRow()
            ->PadX(12)
            ->PadY(6)
            ->ItemsCenter()
            ->JustifyBetween()
            ->BorderB(1, th.border)
            ->Child(Div(a)
                        ->FlexRow()
                        ->Gap(8)
                        ->ItemsCenter()
                        ->Child(component::Icon::Empty(cx)
                                    ->Path(StrL("icons/scroll-text.svg"))
                                    ->Size(UiSize::XSmall)
                                    ->Color(th.mutedFg)
                                    ->IntoEl())
                        ->Child(TextEl(a, StrL("Session log"))
                                    ->Font(12)
                                    ->Medium()
                                    ->Fg(th.mutedFg)))
            ->Child(component::Button::New(cx, StrL("clear-log"))
                        ->Ghost()
                        ->WithSize(UiSize::XSmall)
                        ->Label(StrL("Clear"))
                        ->Disabled(nLog == 0)
                        ->OnClick(Listen(cx, &SpeechApp::OnClearLog))
                        ->IntoEl());

    El* body = Div(a)->Flex1()->MinH(0);
    if (nLog == 0) {
        body->Child(Div(a)
                        ->FlexCol()
                        ->SizeFull()
                        ->PadX(12)
                        ->PadY(8)
                        ->ItemsCenter()
                        ->JustifyCenter()
                        ->Child(Caption(
                            cx, StrL("Events of each session appear here."))));
    } else {
        El* rows = Div(a)->FlexCol()->W(kFill)->PadX(12)->PadY(8)->Gap(4);
        // Newest first.
        for (int ix = nLog - 1; ix >= 0; ix--) {
            rows->Child(LogRow(cx, &log[ix]));
        }
        body->Child(component::Scrollable::New(cx, StrL("session-log"))
                        ->H(kFill)
                        ->ScrollY(logScrollY)
                        ->OnScroll(Listen(cx, &SpeechApp::OnLogScroll))
                        ->Child(rows)
                        ->IntoEl());
    }

    return Div(a)
        ->FlexCol()
        ->H(168)
        ->Shrink(0)
        ->Radius(th.radiusLg)
        ->Border(1, th.border)
        ->ClipY()
        ->Child(header)
        ->Child(body);
}

El* SpeechApp::Render(SpeechApp* self, Ctx* cx) {
    Arena* a = cx->a;
    const Theme& th = ThemeNow(cx->app);
    if (!self->seeded) {
        self->Seed(cx);
    }

    El* title = Div(a)
                    ->FlexRow()
                    ->Gap(8)
                    ->ItemsCenter()
                    ->Child(component::Icon::Empty(cx)
                                ->Path(StrL("icons/audio-lines.svg"))
                                ->Size(UiSize::Small)
                                ->Color(th.primary)
                                ->IntoEl())
                    ->Child(TextEl(a, StrL("Speech"))->Font(14)->Medium());

    El* content =
        Div(a)
            ->FlexCol()
            ->Flex1()
            ->MinW(0)
            ->H(kFill)
            ->Gap(16)
            ->Pad(20)
            ->Child(self->RenderNotesHeader(cx))
            ->Child(Div(a)->Flex1()->MinH(0)->Child(
                component::Textarea::New(cx, StrL("notes"), &self->notes)
                    ->H(kFill)
                    ->IntoEl()))
            ->Child(self->RenderSpeechBar(cx))
            ->Child(self->RenderLog(cx));

    return Div(a)
        ->FlexCol()
        ->SizeFull()
        ->Bg(th.tokens.background)
        ->Fg(th.foreground)
        ->OnAction(ActToggleSpeech(), Listen(cx, &SpeechApp::OnToggleSpeech))
        ->Child(component::TitleBar::New(cx)->Child(title)->IntoEl())
        ->Child(Div(a)
                    ->FlexRow()
                    ->Flex1()
                    ->MinH(0)
                    ->Child(self->RenderSidebar(cx))
                    ->Child(content));
}

// Print what this machine supports and quit.
static void PrintChecks(App* app) {
    printf("Platform       %s\n", PlatformName());
    Str device = component::Microphone::DeviceNameTemp();
    printf("Input device   %s\n", len(device) > 0 ? device.s : "none");
    printf("System recognizer, by language:\n");
    for (int ix = 0; ix < kNLanguages; ix++) {
        component::SystemRecognizer system;
        system.Locale(Str(kLanguages[ix].tag));
        component::SpeechRecognizer recognizer = system.AsRecognizer();
        bool available = recognizer.isAvailable
                             ? recognizer.isAvailable(recognizer.data, app)
                             : true;
        printf("  %-7s %-12s %s\n", kLanguages[ix].tag,
               available ? "available" : "unavailable", kLanguages[ix].name);
    }
    fflush(stdout);
}

int GpuiMain(int argc, char** argv) {
    bool check = false;
    for (int ix = 1; ix < argc; ix++) {
        check = check || StrEq(Str(argv[ix]), StrL("--check"));
    }
    App* app = AppNew();
    component::Init(app);
    if (check) {
        PrintChecks(app);
        AppFree(app);
        return 0;
    }

    AssetsClear();
    AssetsAddDefaultRoots(StrL("speech"));
    KeyBinding bindings[] = {{kToggleKeys, ActToggleSpeech(), nullptr}};
    KeymapBind(bindings, 1);

    // `window_min_size` (760 × 520) has no counterpart: a window here has no
    // minimum size (port-status.md).
    // TitleBar::window_options()
    WinOpts opts;
    opts.clientTitleBar = true;
    return KitRunView(StrL("Speech"), 980, 680, EntityNew<SpeechApp>(app).id,
                      app, opts);
}
