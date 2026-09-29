/* Ported from crates/base/src/questionnaire/state.rs and
 * crates/component/src/questionnaire/components.rs.
 *
 * Rust drives these through TestAppContext and a window view. Here the
 * fixture is an App, a bare Window and a frame arena, which is everything the
 * state asks of a Ctx; a key is handed to QuestionnaireHandleKeyDown — the
 * listener the root installs on its capture phase — rather than simulated
 * through a platform window, and a layout is one LayoutEl over the parts. */

#include "Test.h"

namespace {

struct QEvents {
    Vec<int> events;

    static void OnEvent(QEvents* self, Ctx*, const QuestionnaireEvent* ev) {
        VecAppend(self->events, (int)ev->kind);
    }

    int Count(QuestionnaireEventKind kind) const {
        int n = 0;
        for (int e : events) {
            n += e == (int)kind ? 1 : 0;
        }
        return n;
    }
};

struct QFixture {
    App app;
    Window* win = nullptr;
    Arena* a = nullptr;
    Ctx cx = {};
    InputState firstInput;
    InputState secondInput;
    Entity<QuestionnaireState> state = {};
    Entity<QEvents> events = {};

    QFixture() {
        ThemeSet(&app, ThemeMode::Light);
        win = new Window();
        win->app = &app;
        win->paint.app = &app;
        win->paint.window = win;
        a = ArenaNew();
        cx = {&app, win, a, {}};
        events = EntityNewState<QEvents>(&app);
    }

    ~QFixture() {
        if (win->input) {
            InputBlur(win->input, &app, win);
        }
        win->input = nullptr;
        win->prevInput = nullptr;
        WindowKeyedFree(win);
        EntityDropAll(&app);
        ArenaDelete(a);
        delete win;
    }

    void Watch() { SubscribeTo(&app, state, events, &QEvents::OnEvent); }

    QuestionnaireState* S() { return state.Get(&app); }
    const Vec<int>& Events() { return events.Get(&app)->events; }
    int EventCount() { return len(Events()); }

    QuestionnaireAnswer Answer(const char* item) {
        QuestionnaireAnswer answer;
        S()->Answer(Str(item), a, &answer);
        return answer;
    }

    Str CurrentItem() { return S()->CurrentItem(); }

    void Key(int vk, bool secondary = false) {
        KeyEvent ev = {};
        ev.vk = vk;
        ev.down = true;
        if (secondary) {
#if GPUI_OS_MAC
            ev.platform = true;
#else
            ev.ctrl = true;
#endif
        }
        QuestionnaireHandleKeyDown(state, &ev, &cx);
    }
};

bool ValidAnswer(const QuestionnaireValidationContext* context, Str* error) {
    if (StrEq(context->answer.freeform, "valid")) {
        return true;
    }
    *error = StrL("Use the valid answer");
    return false;
}

QuestionnaireChoiceDefinition Choice(const char* value, const char* label) {
    return QuestionnaireChoiceDefinition::New(Str(value), Str(label));
}

// state.rs Harness: a required single-choice item with a freeform input, a
// validated multiple-choice item whose input starts with a draft, and a
// disabled item.
void HarnessState(QFixture& f) {
    InputSetValue(&f.secondInput, StrL("initial draft"));
    QuestionnaireItemDefinition items[] = {
        QuestionnaireItemDefinition::New(StrL("first"), StrL("First question"))
            .WithRequired(true)
            .WithChoice(Choice("a", "A"))
            .WithChoice(Choice("b", "B"))
            .WithInput(QuestionnaireInputDefinition::New(
                &f.firstInput, StrL("Another answer"))),
        QuestionnaireItemDefinition::New(StrL("second"),
                                         StrL("Second question"))
            .WithMultiple(true)
            .WithChoice(Choice("x", "X"))
            .WithChoice(Choice("y", "Y"))
            .WithInput(QuestionnaireInputDefinition::New(
                &f.secondInput, StrL("Another answer")))
            .WithValidator(&ValidAnswer),
        QuestionnaireItemDefinition::New(StrL("disabled"), StrL("Disabled"))
            .WithDisabled(true),
    };
    QuestionnaireStateNew(&f.app, items, 3, &f.state);
    f.S()->WithShortcuts(QuestionnaireShortcutMode::Letters);
    f.Watch();
}

bool ChoicesAre(const QuestionnaireAnswer& answer, const char* a0,
                const char* a1 = nullptr) {
    int n = a1 ? 2 : 1;
    return answer.nChoices == n && StrEq(answer.choices[0], a0) &&
           (!a1 || StrEq(answer.choices[1], a1));
}

} // namespace

// schema_rejects_duplicate_names_and_invalid_single_defaults
static void SchemaRejectsDuplicateNamesAndInvalidSingleDefaults() {
    QuestionnaireItemDefinition duplicateItems[] = {
        QuestionnaireItemDefinition::New(StrL("same"), StrL("One")),
        QuestionnaireItemDefinition::New(StrL("same"), StrL("Two")),
    };
    QuestionnaireSchemaError e =
        QuestionnaireState::ValidateSchema(duplicateItems, 2);
    utassert(e.kind == QuestionnaireSchemaErrorKind::DuplicateItem &&
             StrEq(e.item, "same"));

    QuestionnaireItemDefinition invalidDefault[] = {
        QuestionnaireItemDefinition::New(StrL("single"), StrL("Single"))
            .WithChoice(Choice("a", "A").WithDefaultSelected(true))
            .WithChoice(Choice("b", "B").WithDefaultSelected(true)),
    };
    e = QuestionnaireState::ValidateSchema(invalidDefault, 1);
    utassert(e.kind ==
                 QuestionnaireSchemaErrorKind::MultipleDefaultsForSingleItem &&
             StrEq(e.item, "single"));

    QuestionnaireItemDefinition duplicateChoice[] = {
        QuestionnaireItemDefinition::New(StrL("item"), StrL("Item"))
            .WithChoice(Choice("same", "One"))
            .WithChoice(Choice("same", "Two")),
    };
    e = QuestionnaireState::ValidateSchema(duplicateChoice, 1);
    utassert(e.kind == QuestionnaireSchemaErrorKind::DuplicateChoice &&
             StrEq(e.item, "item") && StrEq(e.choice, "same"));
}

// validates_navigates_skips_and_emits_completion_before_submit
static void ValidatesNavigatesSkipsAndEmitsCompletionBeforeSubmit() {
    QFixture f;
    HarnessState(f);
    QuestionnaireState* s = f.S();

    utassert(s->Progress().current == 1);
    utassert(s->Progress().total == 2);
    QuestionnaireItemState first;
    utassert(s->ItemState(StrL("first"), &first) &&
             first.status == QuestionnaireItemStatus::Unanswered);
    utassert(!s->Submit(&f.cx));
    utassert(s->Error(StrL("first")) != nullptr);
    const QuestionnaireValidationError* second = s->Error(StrL("second"));
    utassert(second && *second == QuestionnaireValidationError::Message(
                                      StrL("Use the valid answer")));

    s->SetInputValue(StrL("first"), StrL("draft"), &f.cx);
    utassert(s->Error(StrL("first")) == nullptr);
    s->SetInputValue(StrL("first"), StrL(""), &f.cx);
    // Once validation has been attempted, clearing an answer updates the
    // error live.
    utassert(s->Error(StrL("first")) != nullptr);

    utassert(s->ActivateChoice(StrL("first"), StrL("a"), &f.cx).IsOk());
    utassert(s->GoNext(&f.cx));
    utassert(StrEq(f.CurrentItem(), "second"));
    utassert(s->SkipCurrent(&f.cx));

    utassert(s->IsComplete());
    QuestionnaireItemState skipped;
    utassert(s->ItemState(StrL("second"), &skipped) &&
             skipped.status == QuestionnaireItemStatus::Skipped);
    const int expected[] = {
        (int)QuestionnaireEventKind::AnswerChanged,
        (int)QuestionnaireEventKind::CurrentItemChanged,
        (int)QuestionnaireEventKind::AnswerChanged,
        (int)QuestionnaireEventKind::Completed,
        (int)QuestionnaireEventKind::Submit,
    };
    utassert(f.EventCount() == 5);
    for (int i = 0; i < 5 && i < f.EventCount(); i++) {
        utassert(f.Events()[i] == expected[i]);
    }
}

// keeps_input_draft_separate_and_synchronizes_silent_setters_and_reset
static void KeepsInputDraftSeparateAndSynchronizesSilentSettersAndReset() {
    QFixture f;
    HarnessState(f);
    QuestionnaireState* s = f.S();
    int initialEvents = f.EventCount();

    s->SetAnswer(StrL("first"),
                 QuestionnaireAnswer::New().WithFreeform(StrL("  custom  ")),
                 &f.cx);
    // Programmatic setters are silent.
    utassert(f.EventCount() == initialEvents);
    utassert(StrEq(InputValue(&f.firstInput), "  custom  "));
    utassert(StrEq(f.Answer("first").freeform, "  custom  "));

    Str a = StrL("a");
    s->SetAnswer(StrL("first"),
                 QuestionnaireAnswer::New().WithChoices(f.a, &a, 1), &f.cx);
    utassert(StrEq(InputValue(&f.firstInput), "  custom  "));
    utassert(!f.Answer("first").HasFreeform());
    utassert(ChoicesAre(f.Answer("first"), "a"));

    InputReplaceAll(&f.firstInput, &f.app, f.win, StrL(""));
    // Editing an unselected draft does not change the semantic answer.
    utassert(f.EventCount() == initialEvents);
    utassert(ChoicesAre(f.Answer("first"), "a"));

    s->ActivateChoice(StrL("first"), StrL("b"), &f.cx);

    s->Reset(&f.cx);
    utassert(StrEq(InputValue(&f.firstInput), ""));
    utassert(StrEq(InputValue(&f.secondInput), "initial draft"));
    utassert(f.Answer("first").IsEmpty());
    utassert(StrEq(f.Answer("second").freeform, "initial draft"));
    utassert(f.EventCount() == initialEvents + 1);
}

// validates_all_items_and_returns_to_the_first_invalid_item
static void ValidatesAllItemsAndReturnsToTheFirstInvalidItem() {
    QFixture f;
    HarnessState(f);
    QuestionnaireState* s = f.S();
    s->ActivateChoice(StrL("first"), StrL("a"), &f.cx);

    utassert(!s->Submit(&f.cx));
    utassert(StrEq(f.CurrentItem(), "second"));
    const QuestionnaireValidationError* e = s->Error(StrL("second"));
    utassert(e && *e == QuestionnaireValidationError::Message(
                            StrL("Use the valid answer")));

    s->SetExternalError(StrL("first"), StrL("Server rejected it"), &f.cx);
    utassert(!s->Submit(&f.cx));
    utassert(StrEq(f.CurrentItem(), "first"));

    s->ClearExternalError(StrL("first"), &f.cx);
    s->SetInputValue(StrL("second"), StrL("valid"), &f.cx);
    utassert(s->Submit(&f.cx));
}

// preserves_schema_order_and_temporarily_excludes_disabled_answers
static void PreservesSchemaOrderAndTemporarilyExcludesDisabledAnswers() {
    QFixture f;
    HarnessState(f);
    QuestionnaireState* s = f.S();

    Str yxy[] = {StrL("y"), StrL("x"), StrL("y")};
    s->SetAnswer(StrL("second"),
                 QuestionnaireAnswer::New()
                     .WithChoices(f.a, yxy, 3)
                     .WithFreeform(StrL("valid")),
                 &f.cx);
    utassert(ChoicesAre(f.Answer("second"), "x", "y"));
    s->SetCurrentItem(StrL("second"), &f.cx);
    s->FocusCurrentItem(&f.cx);
    utassert(s->FocusNextAnswer(&f.cx));
    // Filled multiple choices are focused in schema order.
    utassert(StrEq(s->FocusedCurrentChoice(f.win), "x"));
    // Focusing a filled choice does not toggle it.
    utassert(ChoicesAre(f.Answer("second"), "x", "y"));

    s->SetChoiceDisabled(StrL("second"), StrL("x"), true, &f.cx);
    utassert(ChoicesAre(f.Answer("second"), "y"));
    s->SetChoiceDisabled(StrL("second"), StrL("x"), false, &f.cx);
    utassert(ChoicesAre(f.Answer("second"), "x", "y"));

    Str unknown = StrL("unknown");
    QuestionnaireSchemaError e = s->SetAnswer(
        StrL("second"),
        QuestionnaireAnswer::New().WithChoices(f.a, &unknown, 1), &f.cx);
    utassert(e.kind == QuestionnaireSchemaErrorKind::UnknownChoice &&
             StrEq(e.item, "second") && StrEq(e.choice, "unknown"));
}

// shortcuts_disabled_current_fallback_and_recompletion_are_deterministic
static void ShortcutsDisabledCurrentFallbackAndRecompletionAreDeterministic() {
    QFixture f;
    HarnessState(f);
    QuestionnaireState* s = f.S();

    Str b = StrL("b");
    s->SetAnswer(StrL("first"),
                 QuestionnaireAnswer::New().WithChoices(f.a, &b, 1), &f.cx);
    s->FocusCurrentItem(&f.cx);
    utassert(s->FocusNextAnswer(&f.cx));
    // The first move from the item group focuses the existing answer.
    utassert(StrEq(s->FocusedCurrentChoice(f.win), "b"));
    // Focusing the filled radio does not replace the answer.
    utassert(ChoicesAre(f.Answer("first"), "b"));
    s->Reset(&f.cx);

    s->FocusInput(StrL("first"), &f.cx);
    utassert(!s->CurrentInputHasText());
    utassert(s->FocusNextAnswer(&f.cx));
    // An empty focused input may move to and activate a radio.
    utassert(ChoicesAre(f.Answer("first"), "a"));
    s->Reset(&f.cx);

    s->SetInputValue(StrL("first"), StrL("draft"), &f.cx);
    s->FocusInput(StrL("first"), &f.cx);
    utassert(s->CurrentInputHasText());
    utassert(!s->FocusNextAnswer(&f.cx));
    utassert(FocusHandleIsFocused(f.win, f.firstInput.focus));
    s->Reset(&f.cx);

    s->FocusCurrentItem(&f.cx);
    utassert(s->FocusNextAnswer(&f.cx));
    // Moving from the item group to a radio activates it.
    utassert(ChoicesAre(f.Answer("first"), "a"));
    s->Reset(&f.cx);

    utassert(StrEq(s->ShortcutForChoice(StrL("first"), StrL("a")), "A"));
    s->SetChoiceDisabled(StrL("first"), StrL("a"), true, &f.cx);
    utassert(StrEq(s->ShortcutForChoice(StrL("first"), StrL("b")), "A"));
    s->SetChoiceDisabled(StrL("first"), StrL("a"), false, &f.cx);
    utassert(s->ActivateShortcut(StrL("a"), &f.cx));
    s->FocusChoice(StrL("first"), StrL("a"), &f.cx);
    utassert(s->MoveCurrentRadio(1, &f.cx));
    utassert(s->GoNext(&f.cx));
    utassert(s->SkipCurrent(&f.cx));
    utassert(s->IsComplete());

    s->SetInputValue(StrL("second"), StrL("valid"), &f.cx);
    s->ActivateChoice(StrL("second"), StrL("x"), &f.cx);
    utassert(!s->IsComplete());
    utassert(s->Submit(&f.cx));
    utassert(f.events.Get(&f.app)
                 ->Count(QuestionnaireEventKind::Completed) == 2);

    int beforeDisable = f.EventCount();
    s->SetItemDisabled(StrL("second"), true, &f.cx);
    utassert(StrEq(f.CurrentItem(), "first"));
    // Programmatic disable and fallback are silent.
    utassert(f.EventCount() == beforeDisable);
}

// ─── components.rs ────────────────────────────────────────────────────────

// compound_parts_support_builder_customization
static void CompoundPartsSupportBuilderCustomization() {
    QFixture f;
    Style faded = {};
    faded.opacity = 0.8f;
    El* e = component::QuestionnaireChoiceDescription::New(&f.cx)
                ->Refine(faded, StyleFieldOpacity)
                ->Child(TextEl(f.a, StrL("Description")))
                ->IntoEl();
    utassert(e && e->first);
    // A refinement lands at layout time, after whatever the part set.
    const RuntimeStyle& th = RuntimeStyleNow(&f.app);
    LayoutEl(&f.win->paint, e, 0, 0, 200, 50, th.fontSize, th.foreground);
    utassertnear(e->style.opacity, 0.8f);
}

// progress_projects_numeric_accessibility
static void ProgressProjectsNumericAccessibility() {
    QFixture f;
    QuestionnaireItemDefinition items[] = {
        QuestionnaireItemDefinition::New(StrL("first"), StrL("First")),
        QuestionnaireItemDefinition::New(StrL("second"), StrL("Second")),
    };
    QuestionnaireStateNew(&f.app, items, 2, &f.state);
    El* progress = component::QuestionnaireProgress::New(&f.cx, f.state)
                       ->IntoEl();
    utassert(progress->accessibility
                 .role == AccessibilityRole::ProgressIndicator);
    utassert(progress->accessibility.hasNumericValue &&
             progress->accessibility.numericValue == 1.f);
    utassert(progress->accessibility.hasMinNumericValue &&
             progress->accessibility.minNumericValue == 0.f);
    utassert(progress->accessibility.hasMaxNumericValue &&
             progress->accessibility.maxNumericValue == 2.f);
    utassert(StrEq(progress->accessibility.label, "Question 1 of 2"));

    El* root = component::Questionnaire::New(&f.cx, f.state)->IntoEl();
    utassert(root->accessibility.role == AccessibilityRole::Form);
}

namespace {

// QuestionnaireHarness's actions row, laid out in a 480px root. The four
// buttons are handed back so their boxes can be read.
struct ActionsRow {
    El* root = nullptr;
    El* actions = nullptr;
    El* previous = nullptr;
    El* skip = nullptr;
    El* next = nullptr;
    El* submit = nullptr;
};

ActionsRow LayOutActions(QFixture& f, bool overrideSkipMargin) {
    ActionsRow row;
    Ctx* cx = &f.cx;
    row.previous = component::QuestionnairePrevious::New(cx, f.state)->IntoEl();
    row.skip = component::QuestionnaireSkip::New(cx, f.state)->IntoEl();
    if (row.skip && overrideSkipMargin) {
        row.skip->MarginL(0);
    }
    row.next = component::QuestionnaireNext::New(cx, f.state)->IntoEl();
    row.submit = component::QuestionnaireSubmit::New(cx, f.state)->IntoEl();
    row.actions = component::QuestionnaireActions::New(cx, f.state)
                      ->Child(row.previous)
                      ->Child(row.skip)
                      ->Child(row.next)
                      ->Child(row.submit)
                      ->IntoEl();
    row.root = component::Questionnaire::New(cx, f.state)
                   ->Child(row.actions)
                   ->IntoEl()
                   ->W(480)
                   ->H(480);
    const RuntimeStyle& th = RuntimeStyleNow(&f.app);
    LayoutEl(&f.win->paint, row.root, 0, 0, 800, 800, th.fontSize,
             th.foreground);
    return row;
}

float Right(const El* e) {
    return e->Bounds().x + e->Bounds().w;
}

void ActionsState(QFixture& f) {
    QuestionnaireItemDefinition items[] = {
        QuestionnaireItemDefinition::New(StrL("first"), StrL("First"))
            .WithRequired(true),
        QuestionnaireItemDefinition::New(StrL("second"), StrL("Second")),
        QuestionnaireItemDefinition::New(StrL("third"), StrL("Third"))
            .WithRequired(true),
    };
    QuestionnaireStateNew(&f.app, items, 3, &f.state);
    f.S()->WithCurrentItem(StrL("second"));
}

} // namespace

// actions_stay_inside_questionnaire_width
static void ActionsStayInsideQuestionnaireWidth() {
    QFixture f;
    ActionsState(f);
    ActionsRow row = LayOutActions(f, false);
    utassert(row.previous && row.skip && row.next && !row.submit);
    utassert(row.actions->Bounds().x >= row.root->Bounds().x);
    utassert(Right(row.actions) <= Right(row.root) + 0.001f);
    utassertnear(row.previous->Bounds().x, row.actions->Bounds().x);
    utassert(Right(row.previous) <= row.skip->Bounds().x);
    utassert(Right(row.skip) <= row.next->Bounds().x);
    utassertnear(Right(row.next), Right(row.actions));

    f.S()->SetCurrentItem(StrL("first"), &f.cx);
    row = LayOutActions(f, false);
    utassert(!row.previous && !row.skip && row.next);
    utassert(row.next->Bounds().x >= row.actions->Bounds().x);
    utassertnear(Right(row.next), Right(row.actions));

    f.S()->SetCurrentItem(StrL("third"), &f.cx);
    row = LayOutActions(f, false);
    utassert(row.previous && row.submit && !row.next);
    utassertnear(row.previous->Bounds().x, row.actions->Bounds().x);
    utassert(Right(row.previous) <= row.submit->Bounds().x);
    utassertnear(Right(row.submit), Right(row.actions));
}

// action_instance_style_overrides_default_trailing_anchor
static void ActionInstanceStyleOverridesDefaultTrailingAnchor() {
    QFixture f;
    ActionsState(f);
    ActionsRow row = LayOutActions(f, true);
    utassertnear(row.previous->Bounds().x, row.actions->Bounds().x);
    utassert(Right(row.previous) <= row.skip->Bounds().x);
    utassert(Right(row.skip) <= row.next->Bounds().x);
    utassert(Right(row.next) < Right(row.actions));
}

namespace {

// ScaleHarness: one choice laid out under a root of `rootSize`, the part
// optionally naming its own. Answers the choice's box.
Bounds ScaleChoice(UiSize rootSize, const UiSize* partSize) {
    QFixture f;
    QuestionnaireItemDefinition items[] = {
        QuestionnaireItemDefinition::New(StrL("scale"), StrL("Scale"))
            .WithChoice(Choice("alpha", "Alpha")),
    };
    QuestionnaireStateNew(&f.app, items, 1, &f.state);
    Ctx* cx = &f.cx;
    Str item = StrL("scale");
    // The root publishes the scale as it renders, before its parts do.
    component::Questionnaire* root = component::Questionnaire::New(cx, f.state)
                                         ->WithSize(rootSize);
    El* rootEl = root->IntoEl()->W(480)->H(480);
    component::QuestionnaireChoice* choice =
        component::QuestionnaireChoice::New(cx, f.state, item, StrL("alpha"));
    component::QuestionnaireChoices* choices =
        component::QuestionnaireChoices::New(cx, f.state, item);
    component::QuestionnaireItem* part =
        component::QuestionnaireItem::New(cx, f.state, item);
    if (partSize) {
        choice->WithSize(*partSize);
        choices->WithSize(*partSize);
        part->WithSize(*partSize);
    }
    El* wrapper = Div(f.a)->Child(choice->IntoEl());
    rootEl->Child(part->Child(choices->Child(wrapper)->IntoEl())->IntoEl());
    const RuntimeStyle& th = RuntimeStyleNow(&f.app);
    LayoutEl(&f.win->paint, rootEl, 0, 0, 800, 800, th.fontSize, th.foreground);
    return wrapper->Bounds();
}

} // namespace

// parts_take_their_scale_from_the_root_and_a_part_may_override_it
static void PartsTakeTheirScaleFromTheRootAndAPartMayOverrideIt() {
    UiSize smallSize = UiSize::Small;
    // A part left alone must land exactly where the same part told to use
    // the root's size lands: the root publishes the scale for all of them.
    Bounds inherited = ScaleChoice(UiSize::Small, nullptr);
    Bounds explicitSize = ScaleChoice(UiSize::Small, &smallSize);
    utassertnear(inherited.w, explicitSize.w);
    utassertnear(inherited.h, explicitSize.h);

    // A different root scale has to move the part, or nothing was inherited.
    Bounds largeRoot = ScaleChoice(UiSize::Large, nullptr);
    utassert(largeRoot.h > inherited.h);

    // A part that names its own size keeps it whatever the root says.
    Bounds overridden = ScaleChoice(UiSize::Large, &smallSize);
    utassertnear(overridden.w, inherited.w);
    utassertnear(overridden.h, inherited.h);
}

namespace {

void KeyboardState(QFixture& f, bool shortcuts) {
    QuestionnaireItemDefinition items[] = {
        QuestionnaireItemDefinition::New(StrL("first"), StrL("First"))
            .WithRequired(true)
            .WithChoice(Choice("alpha", "Alpha"))
            .WithChoice(Choice("beta", "Beta")),
        QuestionnaireItemDefinition::New(StrL("second"), StrL("Second")),
    };
    QuestionnaireStateNew(&f.app, items, 2, &f.state);
    if (shortcuts) {
        f.S()->WithShortcuts(QuestionnaireShortcutMode::Letters);
    }
    FocusHandleFocus(f.win, f.S()->GetFocusHandle());
}

void InputKeyboardState(QFixture& f, bool multiple) {
    QuestionnaireItemDefinition items[] = {
        QuestionnaireItemDefinition::New(StrL("first"), StrL("First"))
            .WithRequired(true)
            .WithMultiple(multiple)
            .WithChoice(Choice("alpha", "Alpha"))
            .WithChoice(Choice("beta", "Beta"))
            .WithInput(QuestionnaireInputDefinition::New(&f.firstInput,
                                                         StrL("Other"))),
        QuestionnaireItemDefinition::New(StrL("second"), StrL("Second")),
    };
    QuestionnaireStateNew(&f.app, items, 2, &f.state);
}

} // namespace

// shortcut_guards_held_keys_before_activation. The held-key and IME halves
// have nothing to drive here: a key down carries neither (port-status.md).
static void ShortcutActivatesAndEnterConfirms() {
    QFixture f;
    KeyboardState(f, true);
    f.Key(KeyA, false);
    utassert(ChoicesAre(f.Answer("first"), "alpha"));
    utassert(StrEq(f.S()->FocusedCurrentChoice(f.win), "alpha"));
    f.Key(KeyReturn);
    utassert(StrEq(f.CurrentItem(), "second"));

    QFixture g;
    KeyboardState(g, true);
    KeyEvent shifted = {};
    shifted.vk = KeyA;
    shifted.down = true;
    shifted.shift = true;
    QuestionnaireHandleKeyDown(g.state, &shifted, &g.cx);
    utassert(g.Answer("first").IsEmpty());
}

// root_keyboard_preserves_navigation_and_radio_semantics
static void RootKeyboardPreservesNavigationAndRadioSemantics() {
    QFixture f;
    KeyboardState(f, false);

    f.Key(KeyRight);
    utassert(StrEq(f.CurrentItem(), "first"));
    utassert(f.Answer("first").IsEmpty());

    FocusHandleFocus(f.win,
                     *f.S()->ChoiceFocusHandle(StrL("first"), StrL("alpha")));
    f.Key(KeyReturn);
    utassert(StrEq(f.CurrentItem(), "first"));
    utassert(f.Answer("first").IsEmpty());

    f.Key(KeyRight);
    utassert(ChoicesAre(f.Answer("first"), "beta"));
    utassert(StrEq(f.CurrentItem(), "first"));

    f.Key(KeyReturn);
    utassert(StrEq(f.CurrentItem(), "second"));
}

// empty_input_enter_stays_put_and_arrows_move_to_answers
static void EmptyInputEnterStaysPutAndArrowsMoveToAnswers() {
    QFixture f;
    InputKeyboardState(f, false);
    QuestionnaireState* s = f.S();
    s->FocusInput(StrL("first"), &f.cx);

    f.Key(KeyReturn);
    utassert(StrEq(f.CurrentItem(), "first"));
    utassert(f.Answer("first").IsEmpty());
    utassert(s->Error(StrL("first")) == nullptr);

    f.Key(KeyReturn, true);
    utassert(StrEq(f.CurrentItem(), "first"));
    utassert(s->Error(StrL("first")) != nullptr);

    s->FocusInput(StrL("first"), &f.cx);
    f.Key(KeyUp);
    utassert(StrEq(s->FocusedCurrentChoice(f.win), "beta"));

    f.Key(KeyDown);
    utassert(s->IsCurrentInputFocused(f.win));
    utassert(ChoicesAre(f.Answer("first"), "beta"));

    f.Key(KeyDown);
    utassert(StrEq(s->FocusedCurrentChoice(f.win), "alpha"));
    utassert(ChoicesAre(f.Answer("first"), "alpha"));

    f.Key(KeyDown);
    utassert(StrEq(s->FocusedCurrentChoice(f.win), "beta"));
    utassert(ChoicesAre(f.Answer("first"), "beta"));

    s->SetInputValue(StrL("first"), StrL("Preserved draft"), &f.cx);
    s->ActivateChoice(StrL("first"), StrL("alpha"), &f.cx);
    s->FocusInput(StrL("first"), &f.cx);
    f.Key(KeyReturn);
    QuestionnaireAnswer answer = f.Answer("first");
    utassert(StrEq(f.CurrentItem(), "first"));
    utassert(ChoicesAre(answer, "alpha"));
    utassert(!answer.HasFreeform());
}

// filled_group_input_keeps_text_editing_directions
static void FilledGroupInputKeepsTextEditingDirections() {
    QFixture f;
    InputKeyboardState(f, true);
    QuestionnaireState* s = f.S();
    s->SetInputValue(StrL("first"), StrL("Freeform answer"), &f.cx);
    s->FocusInput(StrL("first"), &f.cx);

    f.Key(KeyDown);
    utassert(s->IsCurrentInputFocused(f.win));
    utassert(StrEq(f.Answer("first").freeform, "Freeform answer"));
    utassert(f.Answer("first").nChoices == 0);

    f.Key(KeyUp);
    utassert(s->IsCurrentInputFocused(f.win));
    utassert(StrEq(f.Answer("first").freeform, "Freeform answer"));
    utassert(f.Answer("first").nChoices == 0);
}

// invalid_error_projects_alert_role
static void InvalidErrorProjectsAlertRole() {
    QFixture f;
    QuestionnaireItemDefinition items[] = {
        QuestionnaireItemDefinition::New(StrL("first"), StrL("First"))
            .WithRequired(true),
    };
    QuestionnaireStateNew(&f.app, items, 1, &f.state);
    utassert(!component::QuestionnaireError::New(&f.cx, f.state, StrL("first"))
                  ->IntoEl());
    f.S()->Submit(&f.cx);
    El* error =
        component::QuestionnaireError::New(&f.cx, f.state, StrL("first"))
            ->IntoEl();
    utassert(error && error->accessibility.role == AccessibilityRole::Alert);
    utassert(
        StrEq(component::QuestionnaireErrorText(*f.S()->Error(StrL("first"))),
              "Choose an answer to continue."));
}

void TestQuestionnaire() {
    TestSuite("questionnaire");
    SchemaRejectsDuplicateNamesAndInvalidSingleDefaults();
    ValidatesNavigatesSkipsAndEmitsCompletionBeforeSubmit();
    KeepsInputDraftSeparateAndSynchronizesSilentSettersAndReset();
    ValidatesAllItemsAndReturnsToTheFirstInvalidItem();
    PreservesSchemaOrderAndTemporarilyExcludesDisabledAnswers();
    ShortcutsDisabledCurrentFallbackAndRecompletionAreDeterministic();
    CompoundPartsSupportBuilderCustomization();
    ProgressProjectsNumericAccessibility();
    ActionsStayInsideQuestionnaireWidth();
    ActionInstanceStyleOverridesDefaultTrailingAnchor();
    PartsTakeTheirScaleFromTheRootAndAPartMayOverrideIt();
    ShortcutActivatesAndEnterConfirms();
    RootKeyboardPreservesNavigationAndRadioSemantics();
    EmptyInputEnterStaysPutAndArrowsMoveToAnswers();
    FilledGroupInputKeepsTextEditingDirections();
    InvalidErrorProjectsAlertRole();
}
