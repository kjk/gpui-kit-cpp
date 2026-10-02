/* examples/motion — the motion core's five demos, a port of
   crates/base/examples/motion.rs and its examples/motion/mod.rs.

   Sliding time is an interruptible transition per digit; Spring is a
   retargeted spring that keeps its velocity; Keyframes is seven bars on one
   track with staggered offsets; Stagger is three rows entering in order from
   one allocation-free delay policy; Presence is a surface that stays mounted
   until its exit has run.

   The colours come from the shared example palette, the same file the
   showcase reads — crates/base/examples/shared/palette.rs, which upstream
   compiles into both apps. */

#include "gpui.h"
#include "showcase/palette.h"

using namespace gpui;

static const int kStartMinutes = 8 * 60;
static const int kEndMinutes = 20 * 60;
static const float kDigitHeight = 38.f;

enum class Demo : uint8_t {
    SlidingTime,
    Spring,
    Keyframes,
    Stagger,
    Presence,
};

// Demo::ALL, in the order the row of buttons shows them.
static const Demo kDemos[] = {Demo::SlidingTime, Demo::Spring, Demo::Keyframes,
                              Demo::Stagger, Demo::Presence};
static const int kDemoCount = 5;

static Str DemoLabel(Demo d) {
    switch (d) {
        case Demo::SlidingTime:
            return StrL("Sliding time");
        case Demo::Spring:
            return StrL("Spring");
        case Demo::Keyframes:
            return StrL("Keyframes");
        case Demo::Presence:
            return StrL("Presence");
        case Demo::Stagger:
            return StrL("Stagger");
    }
    return {};
}

static Str DemoDescription(Demo d) {
    switch (d) {
        case Demo::SlidingTime:
            return StrL(
                "Interruptible transitions roll the clock from morning to "
                "evening.");
        case Demo::Spring:
            return StrL(
                "A retargeted spring keeps its velocity instead of "
                "restarting.");
        case Demo::Keyframes:
            return StrL(
                "Seven values follow one keyframe track with offset timing.");
        case Demo::Presence:
            return StrL(
                "A surface stays mounted until its exit transition "
                "completes.");
        case Demo::Stagger:
            return StrL(
                "List rows enter in order from one allocation-free delay "
                "policy.");
    }
    return {};
}

// time_digits: the four digits of a clock, hours then minutes.
static void TimeDigits(int minutes, int out[4]) {
    int hour = minutes / 60;
    int minute = minutes % 60;
    out[0] = hour / 10;
    out[1] = hour % 10;
    out[2] = minute / 10;
    out[3] = minute % 10;
}

// advance_digit: the next value a rolling digit should transition to, always
// forwards — 8 to 0 is 10, not 0, so the reel keeps turning one way.
static float AdvanceDigit(float current, int digit) {
    int visible = (int)floorf(current) % 10;
    int forward = (digit - visible) % 10;
    if (forward < 0) {
        forward += 10;
    }
    return current + (float)forward;
}

static El* Txt(Ctx* cx, Str s, float px, Rgba color) {
    return TextEl(cx->a, s)->Font(px)->Fg(color);
}

// The bordered button every demo uses: `Button::new(id).h_9().px_3()
// .border_1().border_color(example_rgb(0xd4d4d4))`.
static El* DemoButton(Ctx* cx, Str id, Str label, Listener onClick) {
    return Button::New(cx, id, false, onClick)
        ->H(36)
        ->PadX(12)
        ->ItemsCenter()
        ->JustifyCenter()
        ->Border(1, ExampleRgb(0xd4d4d4))
        ->Child(Txt(cx, label, 12, ExampleRgb(0x171717)));
}

struct MotionExample {
    Demo demo = Demo::SlidingTime;
    int minutes = kStartMinutes;
    float digitTargets[4] = {0.f, 8.f, 0.f, 0.f};
    // `playback: Option<Task<()>>` — the ticking task, as a window timer.
    int playback = 0;
    bool springSelected = false;
    bool present = true;
    // The generation an id carries so a replay is a new playback rather than
    // the one that already finished. Rust puts it in the element id the same
    // way: ("stagger-item", format!("{generation}-{ix}")).
    int staggerGeneration = 0;

    // ─── sliding time ────────────────────────────────────────────────────

    static void OnTick(MotionExample* self, Ctx* cx, const TickEvent*) {
        self->minutes += 30;
        if (self->minutes > kEndMinutes) {
            self->minutes = kEndMinutes;
        }
        int digits[4];
        TimeDigits(self->minutes, digits);
        for (int i = 0; i < 4; i++) {
            self->digitTargets[i] =
                AdvanceDigit(self->digitTargets[i], digits[i]);
        }
        if (self->minutes == kEndMinutes) {
            WindowCancelTimer(cx->win, self->playback);
            self->playback = 0;
        }
        Notify(cx);
    }

    static void OnPlayTime(MotionExample* self, Ctx* cx, const ClickEvent*) {
        if (self->playback) {
            WindowCancelTimer(cx->win, self->playback);
            self->playback = 0;
            Notify(cx);
            return;
        }
        if (self->minutes == kEndMinutes) {
            self->minutes = kStartMinutes;
            self->digitTargets[0] = 0.f;
            self->digitTargets[1] = 8.f;
            self->digitTargets[2] = 0.f;
            self->digitTargets[3] = 0.f;
        }
        self->playback =
            WindowSetInterval(cx->win, 500, Listen(cx, &MotionExample::OnTick));
        Notify(cx);
    }

    // clock_digit: one number on the reel, `top` from the box's own edge.
    static El* ClockDigit(Ctx* cx, int value, float top) {
        int shown = value % 10;
        if (shown < 0) {
            shown += 10;
        }
        return Div(cx->a)
            ->Absolute()
            ->Top(top)
            ->W(kFill)
            ->H(kDigitHeight)
            ->ItemsCenter()
            ->JustifyCenter()
            ->Child(TextEl(cx->a, StrDup(cx->a, fmt("%d", shown))));
    }

    // rolling_digit: the whole number transitions, and the fractional part is
    // how far the reel has turned between the two digits either side of it.
    static El* RollingDigit(MotionExample* self, Ctx* cx, int ix) {
        float value = motion::transition(
            cx,
            motion::TransitionId(StrL("clock-digit"),
                                 StrDup(cx->a, fmt("%d", ix))),
            self->digitTargets[ix],
            motion::Transition::New(620).Ease(Easing::EaseInOut()));
        int digit = (int)floorf(value);
        float offset = (value - floorf(value)) * kDigitHeight;
        return Div(cx->a)
            ->W(25)
            ->H(kDigitHeight)
            ->ClipY()
            ->Child(ClockDigit(cx, digit, -offset))
            ->Child(ClockDigit(cx, digit + 1, kDigitHeight - offset));
    }

    static El* SlidingTime(MotionExample* self, Ctx* cx) {
        Arena* a = cx->a;
        El* clock = Div(a)->FlexRow()->ItemsCenter()->Font(30)->Medium();
        clock->Child(RollingDigit(self, cx, 0));
        clock->Child(RollingDigit(self, cx, 1));
        clock->Child(Div(a)->PadX(4)->Child(TextEl(a, StrL(":"))));
        clock->Child(RollingDigit(self, cx, 2));
        clock->Child(RollingDigit(self, cx, 3));

        Str label = self->playback                 ? StrL("Stop")
                    : self->minutes == kEndMinutes ? StrL("Replay")
                                                   : StrL("Play");
        return Div(a)->FlexRow()->ItemsCenter()->Gap(24)->Child(clock)->Child(
            DemoButton(cx, StrL("play-time"), label,
                       Listen(cx, &MotionExample::OnPlayTime)));
    }

    // ─── spring ──────────────────────────────────────────────────────────

    static void OnSpringOption(MotionExample* self, Ctx* cx, const ClickEvent*,
                               int64_t selected) {
        self->springSelected = selected != 0;
        Notify(cx);
    }

    static El* SpringDemo(MotionExample* self, Ctx* cx) {
        Arena* a = cx->a;
        // A spring that is under-damped, so the indicator arrives with a
        // little overshoot rather than stopping dead.
        float x =
            motion::spring(cx, motion::TransitionId(StrL("selector-indicator")),
                           self->springSelected ? 120.f : 0.f,
                           SpringNew(420.f).WithDamping(0.68f));
        El* options = Div(a)->FlexRow()->H(kFill);
        const char* labels[2] = {"Focus", "Flow"};
        for (int ix = 0; ix < 2; ix++) {
            bool selected = ix == 1;
            Rgba fg = self->springSelected == selected ? ExampleRgb(0xffffff)
                                                       : ExampleRgb(0x525252);
            options->Child(
                Button::New(cx, StrDup(a, fmt("spring-option-%d", ix)), false,
                            Listen(cx, &MotionExample::OnSpringOption,
                                   selected ? 1 : 0))
                    ->W(119)
                    ->H(kFill)
                    ->ItemsCenter()
                    ->JustifyCenter()
                    ->Child(Txt(cx, Str(labels[ix]), 12, fg)));
        }
        return Div(a)
            ->W(240)
            ->H(40)
            ->Border(1, ExampleRgb(0xd4d4d4))
            ->Child(Div(a)->Absolute()->Top(0)->Left(x)->W(119)->H(kFill)->Bg(
                ExampleRgb(0x171717)))
            ->Child(options);
    }

    // ─── keyframes ───────────────────────────────────────────────────────

    static El* KeyframesDemo(Ctx* cx) {
        Arena* a = cx->a;
        El* head = Div(a)
                       ->FlexRow()
                       ->ItemsCenter()
                       ->JustifyBetween()
                       ->Child(Div(a)->Child(
                           Txt(cx, StrL("Playback"), 12, ExampleRgb(0x171717))))
                       ->Child(Div(a)->Child(Txt(cx, StrL("Infinite · 1200ms"),
                                                 12, ExampleRgb(0x737373))));

        // One track for all seven bars: up over the first third, back down,
        // then held at rest for the last thirty per cent of the loop.
        const Keyframe<float> frames[] = {
            Keyframe<float>::New(0.f, 0.f),
            Keyframe<float>::New(0.35f, 1.f).Ease(Easing::EaseOut()),
            Keyframe<float>::New(0.7f, 0.f),
            Keyframe<float>::New(1.f, 0.f),
        };
        Keyframes<float> track = Keyframes<float>::TryNew(frames, 4).Unwrap();

        El* bars =
            Div(a)->H(64)->FlexRow()->ItemsEnd()->JustifyCenter()->Gap(8);
        for (int ix = 0; ix < 7; ix++) {
            // The delay is what offsets one bar from the next; the playback
            // itself is infinite, so the wave never stops.
            Timing timing = Timing::New(1200)
                                .Delay((float)ix * 80.f)
                                .Iterations(IterationCount::Infinite());
            float value = motion::animate_keyframes(
                              cx,
                              motion::TransitionId(StrL("keyframe-bar"),
                                                   StrDup(a, fmt("%d", ix))),
                              track, timing)
                              .value;
            bars->Child(Div(a)
                            ->W(20)
                            ->H(18.f + 38.f * value)
                            ->Bg(ExampleRgb(0x171717))
                            ->Opacity(0.35f + 0.65f * value));
        }

        return Div(a)
            ->W(320)
            ->H(132)
            ->Pad(16)
            ->Border(1, ExampleRgb(0xd4d4d4))
            ->FlexCol()
            ->JustifyBetween()
            ->Child(head)
            ->Child(bars);
    }

    // ─── presence ────────────────────────────────────────────────────────

    static void OnTogglePresence(MotionExample* self, Ctx* cx,
                                 const ClickEvent*) {
        self->present = !self->present;
        Notify(cx);
    }

    static El* PresenceDemo(MotionExample* self, Ctx* cx) {
        Arena* a = cx->a;
        PresenceSample sample =
            Presence::New(MotionId(StrL("presence-notice")), self->present)
                .Transition(motion::Transition::New(360)
                                .Ease(Easing::EaseInOut()))
                .Sample(cx);

        El* slot = Div(a)->W(320);
        if (sample.ShouldRender()) {
            // Mounted through the exit phase, and fading with its progress.
            slot->Child(Div(a)
                            ->W(kFill)
                            ->Pad(12)
                            ->FlexCol()
                            ->Border(1, ExampleRgb(0xd4d4d4))
                            ->Bg(ExampleRgb(0xffffff))
                            ->Opacity(sample.progress)
                            ->Child(Div(a)
                                        ->FlexRow()
                                        ->ItemsCenter()
                                        ->JustifyBetween()
                                        ->Child(Txt(cx, StrL("Background task"),
                                                    14, ExampleRgb(0x171717))
                                                    ->Medium())
                                        ->Child(Txt(cx, StrL("Complete"), 12,
                                                    ExampleRgb(0x737373))))
                            ->Child(Div(a)->PadT(8)->Child(
                                Txt(cx, StrL("Mounted through the exit phase."),
                                    12, ExampleRgb(0x737373)))));
        }

        return Div(a)
            ->H(120)
            ->FlexRow()
            ->ItemsCenter()
            ->Gap(16)
            ->Child(slot)
            ->Child(DemoButton(cx, StrL("toggle-presence"),
                               self->present ? StrL("Remove") : StrL("Insert"),
                               Listen(cx, &MotionExample::OnTogglePresence)));
    }

    // ─── stagger ─────────────────────────────────────────────────────────

    static void OnReplayStagger(MotionExample* self, Ctx* cx,
                                const ClickEvent*) {
        self->staggerGeneration++;
        Notify(cx);
    }

    static El* StaggerDemo(MotionExample* self, Ctx* cx) {
        Arena* a = cx->a;
        Stagger stagger = Stagger::New(90.f, StaggerOrigin::FirstOrigin());
        const Keyframe<float> frames[] = {Keyframe<float>::New(0.f, 0.f),
                                          Keyframe<float>::New(1.f, 1.f)};
        Keyframes<float> track = Keyframes<float>::TryNew(frames, 2).Unwrap();
        const char* titles[3] = {"Transition", "Spring", "Keyframes"};

        El* list = Div(a)->FlexCol()->Border(1, ExampleRgb(0xd4d4d4));
        for (int ix = 0; ix < 3; ix++) {
            // The generation is part of the id, so Replay is a new playback
            // rather than the one that has already finished.
            Str id = StrDup(a, fmt("%d-%d", self->staggerGeneration, ix));
            float value =
                motion::animate_keyframes(
                    cx, motion::TransitionId(StrL("stagger-item"), id), track,
                    Timing::New(360)
                        .Delay(stagger.Delay(ix, 3))
                        .Ease(Easing::EaseOut()))
                    .value;
            El* row = Div(a)
                          ->MarginL((1.f - value) * 24.f)
                          ->W(kFill)
                          ->H(40)
                          ->PadX(12)
                          ->Bg(ExampleRgb(0xffffff))
                          ->Opacity(value)
                          ->FlexRow()
                          ->ItemsCenter()
                          ->Gap(12);
            if (ix < 2) {
                row->BorderB(1, ExampleRgb(0xe5e5e5));
            }
            row->Child(Div(a)->W(20)->Child(Txt(
                cx, StrDup(a, fmt("0%d", ix + 1)), 12, ExampleRgb(0x737373))));
            row->Child(Txt(cx, Str(titles[ix]), 14, ExampleRgb(0x171717)));
            list->Child(row);
        }

        return Div(a)->W(380)->FlexCol()->Gap(12)->Child(list)->Child(
            Div(a)->FlexRow()->JustifyEnd()->Child(
                DemoButton(cx, StrL("replay-stagger"), StrL("Replay"),
                           Listen(cx, &MotionExample::OnReplayStagger))));
    }

    // ─── the shell ───────────────────────────────────────────────────────

    static void OnPickDemo(MotionExample* self, Ctx* cx, const ClickEvent*,
                           int64_t ix) {
        self->demo = kDemos[ix];
        Notify(cx);
    }

    static El* Render(MotionExample* self, Ctx* cx) {
        Arena* a = cx->a;
        PaletteActivate(ThemeNow(cx->app).mode == ThemeMode::Dark);

        El* content = nullptr;
        switch (self->demo) {
            case Demo::SlidingTime:
                content = SlidingTime(self, cx);
                break;
            case Demo::Spring:
                content = SpringDemo(self, cx);
                break;
            case Demo::Keyframes:
                content = KeyframesDemo(cx);
                break;
            case Demo::Presence:
                content = PresenceDemo(self, cx);
                break;
            case Demo::Stagger:
                content = StaggerDemo(self, cx);
                break;
        }

        El* tabs = Div(a)->FlexRow()->FlexWrap()->Gap(4);
        for (int ix = 0; ix < kDemoCount; ix++) {
            bool selected = kDemos[ix] == self->demo;
            El* button = Button::New(cx, StrDup(a, fmt("demo-%d", ix)), false,
                                     Listen(cx, &MotionExample::OnPickDemo, ix))
                             ->H(32)
                             ->PadX(12)
                             ->ItemsCenter()
                             ->JustifyCenter()
                             ->Border(1, ExampleRgb(0xd4d4d4));
            if (selected) {
                button->Bg(ExampleRgb(0x171717));
            }
            button->Child(
                Txt(cx, DemoLabel(kDemos[ix]), 12,
                    selected ? ExampleRgb(0xffffff) : ExampleRgb(0x171717)));
            tabs->Child(button);
        }

        El* panel =
            Div(a)
                ->H(260)
                ->PadT(16)
                ->Border(1, ExampleRgb(0xd4d4d4))
                ->FlexCol()
                ->Gap(8)
                ->Child(Div(a)->PadX(16)->Child(
                    Txt(cx, DemoLabel(self->demo), 14, ExampleRgb(0x171717))
                        ->Semibold()))
                ->Child(
                    Div(a)->PadX(16)->Child(Txt(cx, DemoDescription(self->demo),
                                                14, ExampleRgb(0x737373))
                                                ->Wrap()))
                ->Child(Div(a)
                            ->Flex1()
                            ->FlexRow()
                            ->ItemsCenter()
                            ->JustifyCenter()
                            ->Child(content));

        El* column = Div(a)
                         ->W(640)
                         ->MaxW(640)
                         ->FlexCol()
                         ->Gap(16)
                         ->Child(Txt(cx, StrL("Motion examples"), 18,
                                     ExampleRgb(0x171717))
                                     ->Semibold())
                         ->Child(tabs)
                         ->Child(panel);

        return Div(a)
            ->SizeFull()
            ->Bg(ExampleCanvas())
            ->Fg(ExampleRgb(0x171717))
            ->Font(12)
            ->FlexRow()
            ->ItemsCenter()
            ->JustifyCenter()
            ->Pad(24)
            ->Child(column);
    }
};

int GpuiMain(int argc, char** argv) {
    (void)argc;
    (void)argv;
    App* app = AppNew();
    component::Init(app);
    ThemeSet(app, ThemeMode::Light);
    return AppRunView(StrL("Motion examples"), 820, 620,
                      EntityNew<MotionExample>(app).id, app, WinOpts{});
}
