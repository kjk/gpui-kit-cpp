#include "Story.h"

// crates/story/src/stories/questionnaire_story.rs

using component::Questionnaire;
using component::QuestionnaireActions;
using component::QuestionnaireChoice;
using component::QuestionnaireChoiceDescription;
using component::QuestionnaireChoices;
using component::QuestionnaireDescription;
using component::QuestionnaireError;
using component::QuestionnaireInput;
using component::QuestionnaireItem;
using component::QuestionnaireNext;
using component::QuestionnairePrevious;
using component::QuestionnaireProgress;
using component::QuestionnaireSkip;
using component::QuestionnaireSubmit;
using component::QuestionnaireTitle;

namespace {

// The last four events, oldest first; each line is heap-owned.
struct StoryEventLog {
    Str lines[4] = {};
    int n = 0;

    void Push(Str line) {
        if (n == 4) {
            StrFree(lines[0]);
            for (int i = 1; i < 4; i++) {
                lines[i - 1] = lines[i];
            }
            n = 3;
        }
        lines[n++] = StrDup(line);
    }
    void Free() {
        for (int i = 0; i < n; i++) {
            StrFree(lines[i]);
        }
        n = 0;
    }
};

} // namespace

struct QuestionnaireStory {
    StoryToolbarState toolbar;
    InputState directionInput;
    InputState toolsInput;
    InputState handleInput;
    InputState keyboardInput;
    Entity<QuestionnaireState> state = {};
    Entity<QuestionnaireState> validationState = {};
    Entity<QuestionnaireState> externalState = {};
    Entity<QuestionnaireState> controlState = {};
    Entity<QuestionnaireState> lettersState = {};
    Entity<QuestionnaireState> numbersState = {};
    Entity<QuestionnaireState> cardState = {};
    Entity<QuestionnaireState> customChoiceState = {};
    Entity<QuestionnaireState> dialogState = {};
    StoryEventLog eventLog;
    StoryEventLog keyboardEventLog;
    Subscription subscriptions[5] = {};
    bool dialogOpen = false;
    bool seeded = false;

    ~QuestionnaireStory() {
        eventLog.Free();
        keyboardEventLog.Free();
    }

    static El* Render(QuestionnaireStory* self, Ctx* cx);
};

static Str StatusName(QuestionnaireItemStatus status) {
    switch (status) {
        case QuestionnaireItemStatus::Answered:
            return StrL("Answered");
        case QuestionnaireItemStatus::Skipped:
            return StrL("Skipped");
        default:
            return StrL("Unanswered");
    }
}

// `{:?}` of a QuestionnaireAnswer, the derived Debug Rust prints.
static Str AnswerDebug(Arena* a, const QuestionnaireAnswer& answer) {
    StrBuilder out;
    out.Append(StrL("QuestionnaireAnswer { choices: ["));
    for (int i = 0; i < answer.nChoices; i++) {
        out.Append(fmt(i ? ", \"%s\"" : "\"%s\"", answer.choices[i]));
    }
    out.Append(StrL("], freeform: "));
    if (answer.HasFreeform()) {
        out.Append(fmt("Some(\"%s\")", answer.freeform));
    } else {
        out.Append(StrL("None"));
    }
    out.Append(StrL(" }"));
    return StrDup(a, Str(out.els, len(out)));
}

static Str SubmissionSummary(Arena* a, const QuestionnaireSubmission& s) {
    StrBuilder out;
    for (int i = 0; i < s.n; i++) {
        if (i) {
            out.Append(StrL(" · "));
        }
        out.Append(fmt("%s:%s=%s", s.items[i].name,
                       StatusName(s.items[i].status),
                       AnswerDebug(a, s.items[i].answer)));
    }
    return StrDup(a, Str(out.els, len(out)));
}

static void OnMainEvent(QuestionnaireStory* self, Ctx* cx,
                        const QuestionnaireEvent* ev) {
    Arena* a = GetTempArena();
    Str message = {};
    switch (ev->kind) {
        case QuestionnaireEventKind::CurrentItemChanged:
            message = fmt("Current item: %s",
                          len(ev->current) ? ev->current : StrL("none"));
            break;
        case QuestionnaireEventKind::AnswerChanged:
            message = fmt("Answer changed: %s (%s)", ev->change.item,
                          StatusName(ev->change.status));
            break;
        case QuestionnaireEventKind::Completed:
            message =
                fmt("Completed: %s", SubmissionSummary(a, ev->submission));
            break;
        case QuestionnaireEventKind::Submit:
            message =
                fmt("Submitted: %s", SubmissionSummary(a, ev->submission));
            break;
    }
    self->eventLog.Push(message);
    Notify(cx);
}

// on_control_event: the host keeps Environment in step with Runtime.
static void OnControlEvent(QuestionnaireStory* self, Ctx* cx,
                           const QuestionnaireEvent* ev) {
    if (ev->kind != QuestionnaireEventKind::AnswerChanged ||
        !StrEq(ev->change.item, "runtime")) {
        return;
    }
    QuestionnaireState* state = self->controlState.Get(cx->app);
    if (!state) {
        return;
    }
    QuestionnaireAnswer answer;
    bool cloud = state->Answer(StrL("runtime"), GetTempArena(), &answer) &&
                 answer.HasChoice(StrL("cloud"));
    QuestionnaireItemState environment;
    bool environmentDisabled =
        state->ItemState(StrL("environment"), &environment) && environment
                                                                   .disabled;
    bool shouldDisableEnvironment = !cloud;
    if (environmentDisabled != shouldDisableEnvironment) {
        state->SetItemDisabled(StrL("environment"), shouldDisableEnvironment,
                               cx);
    }
}

static void KeyboardEvent(QuestionnaireStory* self, Ctx* cx, const char* mode,
                          const QuestionnaireEvent* ev) {
    Str message = {};
    switch (ev->kind) {
        case QuestionnaireEventKind::CurrentItemChanged:
            message = fmt("%s: current=%s", Str(mode),
                          len(ev->current) ? ev->current : StrL("none"));
            break;
        case QuestionnaireEventKind::AnswerChanged:
            message = fmt("%s: answer=%s", Str(mode),
                          AnswerDebug(GetTempArena(), ev->change.answer));
            break;
        case QuestionnaireEventKind::Completed:
            message = fmt("%s: completed", Str(mode));
            break;
        case QuestionnaireEventKind::Submit:
            message = fmt("%s: submitted", Str(mode));
            break;
    }
    self->keyboardEventLog.Push(message);
    Notify(cx);
}

static void OnLettersEvent(QuestionnaireStory* self, Ctx* cx,
                           const QuestionnaireEvent* ev) {
    KeyboardEvent(self, cx, "Letters", ev);
}

static void OnNumbersEvent(QuestionnaireStory* self, Ctx* cx,
                           const QuestionnaireEvent* ev) {
    KeyboardEvent(self, cx, "Numbers", ev);
}

// The Dialog host owns close: a submitted questionnaire closes it.
static void OnDialogEvent(QuestionnaireStory* self, Ctx* cx,
                          const QuestionnaireEvent* ev) {
    if (ev->kind == QuestionnaireEventKind::Submit) {
        self->dialogOpen = false;
        Notify(cx);
    }
}

static Entity<QuestionnaireState> NewState(
    Ctx* cx, const QuestionnaireItemDefinition* items, int n) {
    Entity<QuestionnaireState> state;
    // The story's definitions are valid; `expect` has nothing to report.
    QuestionnaireStateNew(cx->app, items, n, &state);
    return state;
}

static Entity<QuestionnaireState> NewShortcutState(
    Ctx* cx, const QuestionnaireItemDefinition* items, int n,
    QuestionnaireShortcutMode mode) {
    Entity<QuestionnaireState> state = NewState(cx, items, n);
    if (QuestionnaireState* s = state.Get(cx->app)) {
        s->WithShortcuts(mode);
    }
    return state;
}

static QuestionnaireChoiceDefinition Choice(const char* value,
                                            const char* label) {
    return QuestionnaireChoiceDefinition::New(Str(value), Str(label));
}

static void InputInit(InputState* input, const char* placeholder,
                      const char* value) {
    InputSetPlaceholder(input, Str(placeholder));
    if (value) {
        InputSetValue(input, Str(value));
    }
}

static bool ValidateHandle(const QuestionnaireValidationContext* context,
                           Str* error) {
    if (len(context->answer.freeform) >= 3) {
        return true;
    }
    *error = StrL("Use at least three characters.");
    return false;
}

static void InitializeStory(QuestionnaireStory* self, Ctx* cx) {
    if (self->seeded) {
        return;
    }
    self->seeded = true;

    // main_items
    InputInit(&self->directionInput, "Type another direction…", nullptr);
    InputInit(&self->toolsInput, "Add another tool…", "Terminal");
    QuestionnaireItemDefinition mainItems[] = {
        QuestionnaireItemDefinition::New(StrL("direction"),
                                         StrL("What should we prototype next?"))
            .WithRequired(true)
            .WithDescription(StrL("Choose one direction or write your own."))
            .WithChoice(Choice("delegation", "Delegation")
                            .WithDescription(
                                StrL("Show how work moves to a specialist."))
                            .WithDefaultSelected(true))
            .WithChoice(Choice("questions", "Question prompts")
                            .WithDescription(StrL(
                                "Show choices while the interface waits.")))
            .WithChoice(Choice("both", "Both together"))
            .WithInput(QuestionnaireInputDefinition::New(
                &self->directionInput, StrL("Another direction"))),
        QuestionnaireItemDefinition::New(StrL("tools"),
                                         StrL("Which tools do you use?"))
            .WithMultiple(true)
            .WithDescription(StrL("Choose any that belong in the prototype."))
            .WithChoice(Choice("editor", "Editor"))
            .WithChoice(Choice("terminal", "Terminal"))
            .WithChoice(Choice("browser", "Browser").WithDisabled(true))
            .WithInput(QuestionnaireInputDefinition::New(&self->toolsInput,
                                                         StrL("Another tool"))),
        QuestionnaireItemDefinition::New(
            StrL("tone"), StrL("What tone should the interface use?"))
            .WithDescription(
                StrL("This optional question can be intentionally skipped."))
            .WithChoice(Choice("direct", "Direct"))
            .WithChoice(Choice("warm", "Warm")),
        QuestionnaireItemDefinition::New(StrL("advanced"),
                                         StrL("Advanced preferences"))
            .WithDisabled(true)
            .WithChoice(Choice("enabled", "Enable advanced options")),
    };
    self->state =
        NewShortcutState(cx, mainItems, 4, QuestionnaireShortcutMode::Letters);

    // validation_items
    InputInit(&self->handleInput, "At least three characters", nullptr);
    QuestionnaireItemDefinition validationItems[] = {
        QuestionnaireItemDefinition::New(StrL("handle"),
                                         StrL("Choose a public handle"))
            .WithRequired(true)
            .WithDescription(StrL("The validator rejects short handles."))
            .WithInput(QuestionnaireInputDefinition::New(&self->handleInput,
                                                         StrL("Public handle")))
            .WithValidator(&ValidateHandle),
        QuestionnaireItemDefinition::New(StrL("summary"),
                                         StrL("How should we summarize it?"))
            .WithChoice(Choice("short", "Short"))
            .WithChoice(Choice("detailed", "Detailed")),
    };
    self->validationState = NewState(cx, validationItems, 2);

    QuestionnaireItemDefinition externalItems[] = {
        QuestionnaireItemDefinition::New(
            StrL("server"), StrL("Which workspace should we connect?"))
            .WithRequired(true)
            .WithChoice(Choice("personal", "Personal"))
            .WithChoice(Choice("team", "Team")),
    };
    self->externalState = NewState(cx, externalItems, 1);
    if (QuestionnaireState* s = self->externalState.Get(cx->app)) {
        s->SetExternalError(StrL("server"),
                            StrL("This workspace is not available."), cx);
    }

    QuestionnaireItemDefinition controlItems[] = {
        QuestionnaireItemDefinition::New(StrL("runtime"),
                                         StrL("Where will this workflow run?"))
            .WithRequired(true)
            .WithDescription(
                StrL("Selecting Cloud enables the Environment question."))
            .WithChoice(Choice("local", "Local").WithDefaultSelected(true))
            .WithChoice(Choice("cloud", "Cloud")),
        QuestionnaireItemDefinition::New(
            StrL("delivery"), StrL("How should updates be delivered?"))
            .WithChoice(Choice("guided", "Guided"))
            .WithChoice(Choice("automatic", "Automatic")),
        QuestionnaireItemDefinition::New(StrL("environment"),
                                         StrL("Which cloud environment?"))
            .WithDisabled(true)
            .WithChoice(Choice("staging", "Staging"))
            .WithChoice(Choice("production", "Production")),
    };
    self->controlState = NewState(cx, controlItems, 3);

    // keyboard_items
    InputInit(&self->keyboardInput, "Type without triggering A, B, or C…",
              nullptr);
    QuestionnaireItemDefinition keyboardItems[] = {
        QuestionnaireItemDefinition::New(
            StrL("shortcut"), StrL("Choose an answer or type your own"))
            .WithDescription(StrL("Use Up/Down to include the freeform input "
                                  "in the answer focus order."))
            .WithChoice(Choice("first", "First choice"))
            .WithChoice(Choice("second", "Second choice"))
            .WithChoice(Choice("third", "Third choice"))
            .WithInput(QuestionnaireInputDefinition::New(
                &self->keyboardInput, StrL("Custom answer"))),
        QuestionnaireItemDefinition::New(StrL("keyboard_review"),
                                         StrL("Confirm the keyboard result"))
            .WithChoice(Choice("keep", "Keep it"))
            .WithChoice(Choice("change", "Change it")),
    };
    self->lettersState = NewShortcutState(cx, keyboardItems, 2,
                                          QuestionnaireShortcutMode::Letters);

    // single_item
    QuestionnaireItemDefinition numberItems[] = {
        QuestionnaireItemDefinition::New(StrL("shortcut"),
                                         StrL("Choose an answer with a number"))
            .WithChoice(Choice("first", "First choice"))
            .WithChoice(Choice("second", "Second choice"))
            .WithChoice(Choice("third", "Third choice")),
    };
    self->numbersState = NewShortcutState(cx, numberItems, 1,
                                          QuestionnaireShortcutMode::Numbers);

    QuestionnaireItemDefinition cardItems[] = {
        QuestionnaireItemDefinition::New(StrL("card_scope"),
                                         StrL("Who can use this workspace?"))
            .WithRequired(true)
            .WithChoice(Choice("team", "Team members"))
            .WithChoice(Choice("everyone", "Everyone")),
        QuestionnaireItemDefinition::New(StrL("card_updates"),
                                         StrL("Send setup updates?"))
            .WithChoice(Choice("yes", "Yes"))
            .WithChoice(Choice("no", "No")),
    };
    self->cardState = NewState(cx, cardItems, 2);

    QuestionnaireItemDefinition customItems[] = {
        QuestionnaireItemDefinition::New(StrL("custom"),
                                         StrL("Choose a presentation"))
            .WithChoice(Choice("compact", "Compact")
                            .WithDescription(StrL("Use a custom indicator and "
                                                  "composed description.")))
            .WithChoice(Choice("comfortable", "Comfortable")),
    };
    self->customChoiceState = NewShortcutState(
        cx, customItems, 1, QuestionnaireShortcutMode::Letters);

    QuestionnaireItemDefinition dialogItems[] = {
        QuestionnaireItemDefinition::New(
            StrL("dialog"), StrL("Which workspace should we open?"))
            .WithRequired(true)
            .WithChoice(Choice("first", "Personal"))
            .WithChoice(Choice("second", "Team")),
        QuestionnaireItemDefinition::New(
            StrL("dialog_verification"),
            StrL("How should we verify the setup?"))
            .WithRequired(true)
            .WithChoice(Choice("targeted", "Targeted checks"))
            .WithChoice(Choice("full", "Full verification")),
    };
    self->dialogState = NewState(cx, dialogItems, 2);

    self->subscriptions[0] = Subscribe(cx, self->state, &OnMainEvent);
    self->subscriptions[1] = Subscribe(cx, self->controlState, &OnControlEvent);
    self->subscriptions[2] = Subscribe(cx, self->lettersState, &OnLettersEvent);
    self->subscriptions[3] = Subscribe(cx, self->numbersState, &OnNumbersEvent);
    self->subscriptions[4] = Subscribe(cx, self->dialogState, &OnDialogEvent);
}

// item_view: title, description, the choices with the freeform answer in the
// same answer group (the ReUI composition and spacing), then the error.
static El* ItemView(Ctx* cx, Entity<QuestionnaireState> state, const char* item,
                    const char* const* choices, int nChoices) {
    Str name = Str(item);
    QuestionnaireChoices* parts = QuestionnaireChoices::New(cx, state, name);
    for (int i = 0; i < nChoices; i++) {
        parts->Child(QuestionnaireChoice::New(cx, state, name, Str(choices[i]))
                         ->IntoEl());
    }
    parts->Child(QuestionnaireInput::New(cx, state, name)->IntoEl());
    return QuestionnaireItem::New(cx, state, name)
        ->Child(QuestionnaireTitle::New(cx, state, name)->IntoEl())
        ->Child(QuestionnaireDescription::New(cx, state, name)->IntoEl())
        ->Child(parts->IntoEl())
        ->Child(QuestionnaireError::New(cx, state, name)->IntoEl())
        ->IntoEl();
}

struct ItemSpec {
    const char* name;
    const char* const* choices;
    int nChoices;
};

// questionnaire_view: the root is the only place the scale is named; every
// part follows it.
static El* QuestionnaireView(Ctx* cx, Entity<QuestionnaireState> state,
                             UiSize size, const ItemSpec* items, int n) {
    Questionnaire* q = Questionnaire::New(cx, state)->WithSize(size)->Child(
        QuestionnaireProgress::New(cx, state)->IntoEl());
    for (int i = 0; i < n; i++) {
        q->Child(ItemView(cx, state, items[i].name, items[i].choices,
                          items[i].nChoices));
    }
    q->Child(QuestionnaireActions::New(cx, state)
                 ->Child(QuestionnairePrevious::New(cx, state)->IntoEl())
                 ->Child(QuestionnaireSkip::New(cx, state)->IntoEl())
                 ->Child(QuestionnaireNext::New(cx, state)->IntoEl())
                 ->Child(QuestionnaireSubmit::New(cx, state)->IntoEl())
                 ->IntoEl());
    return q->IntoEl();
}

static El* Muted(Ctx* cx, Str text, float px) {
    return StoryTxt(cx, text, px, ThemeNow(cx->app).mutedFg);
}

static El* Label(Ctx* cx, const char* text) {
    return StoryTxt(cx, Str(text), 16, ThemeNow(cx->app).foreground)->Medium();
}

static El* QButton(Ctx* cx, const char* id, const char* label, Listener onClick,
                   bool primary) {
    component::Button* button = component::Button::New(cx, Str(id))
                                    ->Label(Str(label))
                                    ->OnClick(onClick);
    if (primary) {
        button->Primary();
    } else {
        button->Outline();
    }
    return button->IntoEl();
}

static void ApplyServerError(QuestionnaireStory* self, Ctx* cx,
                             const ClickEvent*) {
    if (QuestionnaireState* s = self->externalState.Get(cx->app)) {
        s->SetExternalError(StrL("server"),
                            StrL("This workspace is not available."), cx);
    }
}

static void ClearServerError(QuestionnaireStory* self, Ctx* cx,
                             const ClickEvent*) {
    if (QuestionnaireState* s = self->externalState.Get(cx->app)) {
        s->ClearExternalError(StrL("server"), cx);
    }
}

static void FixAndSubmit(QuestionnaireStory* self, Ctx* cx, const ClickEvent*) {
    QuestionnaireState* s = self->externalState.Get(cx->app);
    if (!s) {
        return;
    }
    Str team = StrL("team");
    s->SetAnswer(StrL("server"),
                 QuestionnaireAnswer::New().WithChoices(cx->a, &team, 1), cx);
    s->ClearExternalError(StrL("server"), cx);
    s->Submit(cx);
}

static void ResetMain(QuestionnaireStory* self, Ctx* cx, const ClickEvent*) {
    if (QuestionnaireState* s = self->state.Get(cx->app)) {
        s->Reset(cx);
    }
}

static void JumpToRuntime(QuestionnaireStory* self, Ctx* cx,
                          const ClickEvent*) {
    if (QuestionnaireState* s = self->controlState.Get(cx->app)) {
        s->SetCurrentItem(StrL("runtime"), cx);
    }
}

enum {
    ControlBack,
    ControlSkip,
    ControlNext,
    ControlFinish
};

static void ControlAction(QuestionnaireStory* self, Ctx* cx, const ClickEvent*,
                          intptr_t action) {
    QuestionnaireState* s = self->controlState.Get(cx->app);
    if (!s) {
        return;
    }
    switch (action) {
        case ControlBack:
            s->GoPrevious(cx);
            break;
        case ControlSkip:
            s->SkipCurrent(cx);
            break;
        case ControlNext:
            s->GoNext(cx);
            break;
        default:
            s->Submit(cx);
            break;
    }
}

static void OpenDialog(QuestionnaireStory* self, Ctx* cx, const ClickEvent*) {
    self->dialogOpen = true;
    Notify(cx);
}

static void CloseDialog(QuestionnaireStory* self, Ctx* cx, const ClickEvent*) {
    self->dialogOpen = false;
    Notify(cx);
}

static El* CompactIndicator(Ctx* cx, const QuestionnaireChoiceState* choice) {
    const Theme& th = ThemeNow(cx->app);
    return Div(cx->a)->W(16)->H(16)->Radius(9999)->Bg(
        choice->selected ? th.primary : th.muted);
}

static El* CompactShortcut(Ctx* cx, const QuestionnaireChoiceState* choice) {
    if (len(choice->shortcut) == 0) {
        return Div(cx->a);
    }
    component::Keystroke stroke;
    char key = choice->shortcut.s[0];
    stroke.key =
        StrDup(cx->a, fmt("%c", key >= 'A' && key <= 'Z' ? key + 32 : key));
    return component::Kbd::New(cx, stroke)->Outline()->IntoEl();
}

static El* CustomControl(QuestionnaireStory* self, Ctx* cx, UiSize size) {
    Entity<QuestionnaireState> state = self->controlState;
    const QuestionnaireState* s = state.Get(cx->app);
    QuestionnaireNavigationState nav =
        s ? s->NavigationState() : QuestionnaireNavigationState{};
    static const char* const kRuntime[] = {"local", "cloud"};
    static const char* const kDelivery[] = {"guided", "automatic"};
    static const char* const kEnvironment[] = {"staging", "production"};
    El* actions = QuestionnaireActions::New(cx, state)->IntoEl();
    Listener act = Listen(cx, &ControlAction);
    if (nav.previousVisible) {
        actions->Child(QButton(cx, "questionnaire-custom-previous", "Back",
                               ListenerArg(act, ControlBack), false));
    }
    if (nav.skipVisible) {
        actions->Child(QButton(cx, "questionnaire-custom-skip", "Not now",
                               ListenerArg(act, ControlSkip), false)
                           ->MlAuto());
    }
    if (nav.nextVisible) {
        El* next = QButton(cx, "questionnaire-custom-next", "Continue",
                           ListenerArg(act, ControlNext), true);
        if (!nav.skipVisible) {
            next->MlAuto();
        }
        actions->Child(next);
    }
    if (nav.submitVisible) {
        El* finish = QButton(cx, "questionnaire-custom-submit", "Finish",
                             ListenerArg(act, ControlFinish), true);
        if (!nav.skipVisible) {
            finish->MlAuto();
        }
        actions->Child(finish);
    }
    return Questionnaire::New(cx, state)
        ->WithSize(size)
        ->Child(QuestionnaireProgress::New(cx, state)->IntoEl())
        ->Child(ItemView(cx, state, "runtime", kRuntime, 2))
        ->Child(ItemView(cx, state, "delivery", kDelivery, 2))
        ->Child(ItemView(cx, state, "environment", kEnvironment, 2))
        ->Child(actions)
        ->IntoEl();
}

static El* CustomChoice(QuestionnaireStory* self, Ctx* cx, UiSize size) {
    Entity<QuestionnaireState> state = self->customChoiceState;
    Str item = StrL("custom");
    Style gap = {};
    gap.gapX = gap.gapY = 8;
    Style faded = {};
    faded.opacity = 0.65f;
    El* compactContent =
        Div(cx->a)
            ->FlexCol()
            ->Gap(4)
            ->Child(Label(cx, "Compact"))
            ->Child(QuestionnaireChoiceDescription::New(cx)
                        ->Child(TextEl(cx->a,
                                       StrL("A custom indicator and composed "
                                            "description.")))
                        ->IntoEl());
    El* choices =
        QuestionnaireChoices::New(cx, state, item)
            ->Child(QuestionnaireChoice::New(cx, state, item, StrL("compact"))
                        ->ContentStyle(gap, StyleFieldGap)
                        ->RenderIndicator(&CompactIndicator)
                        ->RenderShortcut(&CompactShortcut)
                        ->Child(compactContent)
                        ->IntoEl())
            ->Child(
                QuestionnaireChoice::New(cx, state, item, StrL("comfortable"))
                    ->IndicatorStyle(faded, StyleFieldOpacity)
                    ->ShortcutStyle(faded, StyleFieldOpacity)
                    ->IntoEl())
            ->IntoEl();
    return Questionnaire::New(cx, state)
        ->WithSize(size)
        ->Child(QuestionnaireItem::New(cx, state, item)
                    ->Child(QuestionnaireTitle::New(cx, state, item)->IntoEl())
                    ->Child(choices)
                    ->Child(QuestionnaireError::New(cx, state, item)->IntoEl())
                    ->IntoEl())
        ->Child(QuestionnaireActions::New(cx, state)
                    ->Child(QuestionnaireSubmit::New(cx, state)->IntoEl())
                    ->IntoEl())
        ->IntoEl();
}

static El* DialogSection(QuestionnaireStory* self, Ctx* cx) {
    El* trigger =
        component::Button::New(cx, StrL("questionnaire-dialog-trigger"))
            ->Outline()
            ->Label(StrL("Open Questionnaire Dialog"))
            ->OnClick(Listen(cx, &OpenDialog))
            ->IntoEl();
    if (!self->dialogOpen) {
        return trigger;
    }
    Arena* a = cx->a;
    Entity<QuestionnaireState> state = self->dialogState;
    static const char* const kWorkspace[] = {"first", "second"};
    static const char* const kVerification[] = {"targeted", "full"};
    El* header =
        component::DialogHeader::New(cx)
            ->Child(component::DialogTitle::New(cx)
                        ->Child(TextEl(a, StrL("Workspace setup")))
                        ->IntoEl())
            ->Child(component::DialogDescription::New(cx)
                        ->Child(TextEl(a, StrL("Questionnaire validates the "
                                               "answer; the Dialog host owns "
                                               "close and cancel.")))
                        ->IntoEl())
            ->IntoEl()
            ->Pad(16);
    El* footer =
        component::DialogFooter::New(cx)
            ->Child(component::DialogClose::New(cx)
                        ->Child(component::Button::New(
                                    cx, StrL("questionnaire-dialog-cancel"))
                                    ->Outline()
                                    ->Label(StrL("Cancel"))
                                    ->IntoEl())
                        ->IntoEl())
            ->Child(QuestionnaireActions::New(cx, state)
                        ->Child(QuestionnairePrevious::New(cx, state)->IntoEl())
                        ->Child(QuestionnaireNext::New(cx, state)->IntoEl())
                        ->Child(QuestionnaireSubmit::New(cx, state)->IntoEl())
                        ->IntoEl())
            ->IntoEl();
    El* questionnaire =
        Questionnaire::New(cx, state)
            ->WithSize(UiSize::Small)
            ->Child(QuestionnaireProgress::New(cx, state)->IntoEl())
            ->Child(ItemView(cx, state, "dialog", kWorkspace, 2))
            ->Child(
                ItemView(cx, state, "dialog_verification", kVerification, 2))
            ->Child(footer)
            ->IntoEl()
            ->PadX(16)
            ->PadB(16);
    El* content =
        Div(a)->FlexCol()->W(kFill)->Child(header)->Child(questionnaire);
    Listener close = Listen(cx, &CloseDialog);
    El* dialog = component::Dialog::New(cx)
                     ->Open(true)
                     ->OnClose(close)
                     ->OnCancel(close)
                     ->OnOk(close)
                     ->Surface(content)
                     ->IntoEl(WindowSize(cx->win));
    return Div(a)->Child(trigger)->Child(dialog);
}

El* QuestionnaireStory::Render(QuestionnaireStory* self, Ctx* cx) {
    InitializeStory(self, cx);
    Arena* a = cx->a;
    UiSize size = self->toolbar.size;

    static const char* const kDirection[] = {"delegation", "questions", "both"};
    static const char* const kTools[] = {"editor", "terminal", "browser"};
    static const char* const kTone[] = {"direct", "warm"};
    static const char* const kAdvanced[] = {"enabled"};
    static const char* const kShortcut[] = {"first", "second", "third"};
    static const char* const kReview[] = {"keep", "change"};
    static const char* const kSummary[] = {"short", "detailed"};
    static const char* const kServer[] = {"personal", "team"};
    static const char* const kScope[] = {"team", "everyone"};
    static const char* const kUpdates[] = {"yes", "no"};

    const ItemSpec mainSpec[] = {{"direction", kDirection, 3},
                                 {"tools", kTools, 3},
                                 {"tone", kTone, 2},
                                 {"advanced", kAdvanced, 1}};
    El* main = QuestionnaireView(cx, self->state, size, mainSpec, 4);
    const ItemSpec validationSpec[] = {{"handle", nullptr, 0},
                                       {"summary", kSummary, 2}};
    El* validation =
        QuestionnaireView(cx, self->validationState, size, validationSpec, 2);
    const ItemSpec externalSpec[] = {{"server", kServer, 2}};
    El* external =
        QuestionnaireView(cx, self->externalState, size, externalSpec, 1);

    const QuestionnaireState* snapshot = self->state.Get(cx->app);
    QuestionnaireNavigationState nav = snapshot->NavigationState();
    Str navigationSummary = StoryFmt(
        cx,
        "Navigation: previous=%s · next=%s · skip=%s · submit=%s · "
        "can_confirm=%s",
        nav.previousVisible ? "true" : "false",
        nav.nextVisible ? "true" : "false", nav.skipVisible ? "true" : "false",
        nav.submitVisible ? "true" : "false",
        nav.confirmable ? "true" : "false");
    StrBuilder status;
    const char* statusNames[] = {"direction", "tools", "tone", "advanced"};
    for (int i = 0; i < 4; i++) {
        QuestionnaireItemState item;
        if (!snapshot->ItemState(Str(statusNames[i]), &item)) {
            continue;
        }
        if (len(status) > 0) {
            status.Append(StrL(" · "));
        }
        status.Append(
            fmt("%s: %s", Str(statusNames[i]), StatusName(item.status)));
    }
    Str statusSummary = StrDup(a, Str(status.els, len(status)));
    QuestionnaireAnswers answers = snapshot->Answers(a);
    StrBuilder answerText;
    for (int i = 0; i < answers.n; i++) {
        if (i) {
            answerText.Append(StrL(" · "));
        }
        answerText.Append(fmt("%s=%s", answers.entries[i].name,
                              AnswerDebug(a, answers.entries[i].answer)));
    }
    Str answerSummary =
        len(answerText) == 0
            ? StrL("Answers: none")
            : StoryFmt(cx, "Answers: %s", Str(answerText.els, len(answerText)));

    const ItemSpec lettersSpec[] = {{"shortcut", kShortcut, 3},
                                    {"keyboard_review", kReview, 2}};
    El* letters =
        QuestionnaireView(cx, self->lettersState, size, lettersSpec, 2);
    const ItemSpec numbersSpec[] = {{"shortcut", kShortcut, 3}};
    El* numbers =
        QuestionnaireView(cx, self->numbersState, size, numbersSpec, 1);
    const QuestionnaireState* lettersSnapshot = self->lettersState.Get(cx->app);
    Str keyboardFocus = {};
    Str focusedChoice = lettersSnapshot->FocusedCurrentChoice(cx->win);
    if (lettersSnapshot->IsCurrentInputFocused(cx->win)) {
        keyboardFocus = StrL("freeform input");
    } else if (len(focusedChoice) > 0) {
        keyboardFocus = StoryFmt(cx, "choice %s", focusedChoice);
    } else {
        keyboardFocus = StrL("item group or none");
    }
    QuestionnaireAnswer lettersAnswer;
    lettersSnapshot->Answer(StrL("shortcut"), a, &lettersAnswer);
    InputState* draftInput = lettersSnapshot->InputStateOf(StrL("shortcut"));
    Str lettersDraft = draftInput ? InputValue(draftInput) : Str{};

    const QuestionnaireValidationError* serverError =
        self->externalState.Get(cx->app)->Error(StrL("server"));
    Str externalError = serverError && len(serverError->MessageText()) > 0
                            ? serverError->MessageText()
                            : StrL("none");

    const QuestionnaireState* control = self->controlState.Get(cx->app);
    QuestionnaireItemState environment;
    bool environmentEnabled =
        control->ItemState(StrL("environment"), &environment) && !environment
                                                                      .disabled;

    const ItemSpec cardSpec[] = {{"card_scope", kScope, 2},
                                 {"card_updates", kUpdates, 2}};
    El* card =
        component::GroupBox::New(cx, StrL("Workspace access"))
            ->Outline()
            ->Child(QuestionnaireView(cx, self->cardState, size, cardSpec, 2))
            ->IntoEl();

    El* page = Div(a)->FlexCol()->W(kFill)->Gap(16);
    page->Child(StoryToolbar(cx, self));

    El* flow = StorySection(
        cx, "Complete flow",
        "Required single choice, multiple choice, freeform input, skip, "
        "disabled item, and submit events.");
    StorySectionBody(flow)->W(448);
    StorySectionAdd(flow, main);
    page->Child(flow);

    El* errors = StorySection(cx, "Validation and external errors",
                              "Next validates the active item; external "
                              "errors remain host-owned until cleared or "
                              "fixed.");
    StorySectionBody(errors)->W(600);
    StorySectionAdd(errors, Label(cx, "Internal validation"));
    StorySectionAdd(errors, validation);
    StorySectionAdd(errors, Label(cx, "External error lifecycle"));
    StorySectionAdd(errors, external);
    StorySectionAdd(
        errors,
        Div(a)
            ->FlexRow()
            ->Gap(8)
            ->Child(QButton(cx, "questionnaire-external-reapply",
                            "Apply server error", Listen(cx, &ApplyServerError),
                            false))
            ->Child(QButton(cx, "questionnaire-external-clear", "Clear error",
                            Listen(cx, &ClearServerError), false))
            ->Child(QButton(cx, "questionnaire-external-fix-submit",
                            "Fix and submit again", Listen(cx, &FixAndSubmit),
                            true)));
    StorySectionAdd(
        errors,
        Muted(cx, StoryFmt(cx, "External error: %s", externalError), 12));
    page->Child(errors);

    El* navSection = StorySection(cx, "Navigation state",
                                  "The host can inspect current item, "
                                  "answers, status, available actions, and "
                                  "events.");
    StorySectionBody(navSection)->W(600);
    StorySectionAdd(navSection,
                    QButton(cx, "questionnaire-reset", "Reset complete flow",
                            Listen(cx, &ResetMain), false));
    Str current = snapshot->CurrentItem();
    StorySectionAdd(
        navSection,
        Muted(cx,
              StoryFmt(cx, "Current: %s · enabled items: %d · complete: %s",
                       len(current) ? current : StrL("none"), snapshot->Total(),
                       snapshot->IsComplete() ? "true" : "false"),
              14));
    StorySectionAdd(navSection, Muted(cx, statusSummary, 12));
    StorySectionAdd(navSection, Muted(cx, navigationSummary, 12));
    StorySectionAdd(navSection, Muted(cx, answerSummary, 12));
    for (int i = 0; i < self->eventLog.n; i++) {
        StorySectionAdd(navSection, Muted(cx, self->eventLog.lines[i], 12));
    }
    page->Child(navSection);

    El* keys = StorySection(cx, "Shortcuts and keyboard",
                            "The fixture exposes focus, answers, drafts, and "
                            "events while testing the full keyboard "
                            "contract.");
    StorySectionBody(keys)->W(600);
    El* help = Div(a)->FlexCol()->Gap(4);
    help->Child(Muted(cx,
                      StrL("Up/Down: move through choices and the freeform "
                           "input; text editing keeps native arrow behavior."),
                      14));
    help->Child(Muted(cx,
                      StrL("Left/Right: switch items outside text input and "
                           "radio focus; Right requires an answer."),
                      14));
    help->Child(
        Muted(cx,
              StrL("Enter: confirm a filled answer. Command/Ctrl+Enter: "
                   "confirm the current item."),
              14));
    help->Child(Muted(cx,
                      StrL("A–Z or 1–9: activate enabled choices. While input "
                           "is focused, typed characters edit the draft and "
                           "are not intercepted."),
                      14));
    StorySectionAdd(keys, help);
    StorySectionAdd(keys, Div(a)
                              ->FlexRow()
                              ->W(kFill)
                              ->Gap(16)
                              ->Child(Div(a)
                                          ->FlexCol()
                                          ->Flex1()
                                          ->Gap(8)
                                          ->Child(Label(cx, "Letters"))
                                          ->Child(letters))
                              ->Child(Div(a)
                                          ->FlexCol()
                                          ->Flex1()
                                          ->Gap(8)
                                          ->Child(Label(cx, "Numbers"))
                                          ->Child(numbers)));
    StorySectionAdd(
        keys, Muted(cx,
                    StoryFmt(cx, "Letters focus: %s · answer=%s · draft=\"%s\"",
                             keyboardFocus, AnswerDebug(a, lettersAnswer),
                             lettersDraft),
                    12));
    for (int i = 0; i < self->keyboardEventLog.n; i++) {
        StorySectionAdd(keys, Muted(cx, self->keyboardEventLog.lines[i], 12));
    }
    page->Child(keys);

    El* controlled = StorySection(
        cx, "Controlled current, conditional items, and custom actions",
        "The host synchronizes Environment from the Runtime answer; custom "
        "actions use NavigationState visibility.");
    StorySectionBody(controlled)->W(600);
    StorySectionAdd(
        controlled,
        Div(a)
            ->FlexRow()
            ->Gap(8)
            ->ItemsCenter()
            ->Child(QButton(cx, "questionnaire-controlled-current",
                            "Jump to runtime question",
                            Listen(cx, &JumpToRuntime), false))
            ->Child(Muted(cx,
                          environmentEnabled
                              ? StrL("Environment enabled: Runtime is Cloud")
                              : StrL("Environment disabled: choose Cloud on "
                                     "Runtime"),
                          12)));
    StorySectionAdd(controlled, CustomControl(self, cx, size));
    page->Child(controlled);

    El* composition = StorySection(cx, "Card and Dialog composition",
                                   "GroupBox or Dialog owns the surface and "
                                   "its close behavior; the questionnaire "
                                   "keeps progress, items, and actions "
                                   "together.");
    StorySectionBody(composition)->W(600);
    StorySectionAdd(composition, card);
    StorySectionAdd(composition, DialogSection(self, cx));
    page->Child(composition);

    El* custom = StorySection(cx, "Custom choice composition",
                              "Customize indicator, content, shortcut "
                              "renderers, and style seams while preserving "
                              "Questionnaire state and behavior.");
    StorySectionBody(custom)->W(600);
    StorySectionAdd(custom, CustomChoice(self, cx, size));
    page->Child(custom);
    return page;
}

STORY_PAGE(StoryQuestionnaire, QuestionnaireStory);
