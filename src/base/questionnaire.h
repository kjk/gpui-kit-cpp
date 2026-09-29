#ifndef GPUI_BASE_QUESTIONNAIRE_H_
#define GPUI_BASE_QUESTIONNAIRE_H_
/* Questionnaire behavior — crates/base/src/questionnaire/ (mod.rs, types.rs,
   state.rs, control.rs, keyboard.rs)

   Answers, validation, navigation, focus and shortcuts. The state machine
   lives here so an application can replace the visual language without
   reimplementing it; `ui/questionnaire.h` is the skin.

   Rust hands out owned clones (`QuestionnaireAnswer`, `QuestionnaireAnswers`,
   `QuestionnaireSubmission`). Those are plain values here whose arrays live in
   an Arena the caller names — the frame arena for a query, the temp arena for
   an event payload — so a handler that keeps one past the call copies it.
   Choice values point at the state's own copy of the schema, which lives as
   long as the state does. */

#include "gpui/gpui.h"

namespace gpui {

struct QuestionnaireValidationContext;

// QuestionnaireValidator: `Fn(&QuestionnaireValidationContext) -> Result<(),
// SharedString>`. Answers true for Ok; on an error it writes the sentence to
// `error` (the state copies it) and answers false.
using QuestionnaireValidator =
    bool (*)(const QuestionnaireValidationContext* context, Str* error);

// Describes one selectable answer.
struct QuestionnaireChoiceDefinition {
    Str value = {};
    Str accessibilityLabel = {};
    // Empty is None.
    Str description = {};
    bool disabled = false;
    bool defaultSelected = false;

    static QuestionnaireChoiceDefinition New(Str value, Str accessibilityLabel);
    QuestionnaireChoiceDefinition WithDescription(Str description) const;
    QuestionnaireChoiceDefinition WithDisabled(bool disabled) const;
    QuestionnaireChoiceDefinition WithDefaultSelected(bool selected) const;

    Str Value() const { return value; }
    Str AccessibilityLabel() const { return accessibilityLabel; }
    Str Description() const { return description; }
    bool IsDisabled() const { return disabled; }
    bool IsDefaultSelected() const { return defaultSelected; }
};

// Describes the optional freeform answer owned by an item. The InputState is
// the caller's, as Rust's `Entity<InputState>` is; it must outlive the state.
struct QuestionnaireInputDefinition {
    InputState* state = nullptr;
    Str accessibilityLabel = {};
    bool disabled = false;

    static QuestionnaireInputDefinition New(InputState* state,
                                            Str accessibilityLabel);
    QuestionnaireInputDefinition WithDisabled(bool disabled) const;

    InputState* State() const { return state; }
    Str AccessibilityLabel() const { return accessibilityLabel; }
    bool IsDisabled() const { return disabled; }
};

// Describes one ordered questionnaire item.
struct QuestionnaireItemDefinition {
    Str name = {};
    Str accessibilityLabel = {};
    Str description = {};
    bool required = false;
    bool multiple = false;
    bool disabled = false;
    Vec<QuestionnaireChoiceDefinition> choices;
    QuestionnaireInputDefinition input = {};
    bool hasInput = false;
    QuestionnaireValidator validator = nullptr;

    static QuestionnaireItemDefinition New(Str name, Str accessibilityLabel);
    QuestionnaireItemDefinition WithDescription(Str description) const;
    QuestionnaireItemDefinition WithRequired(bool required) const;
    QuestionnaireItemDefinition WithMultiple(bool multiple) const;
    QuestionnaireItemDefinition WithDisabled(bool disabled) const;
    QuestionnaireItemDefinition WithChoices(
        const QuestionnaireChoiceDefinition* values, int n) const;
    QuestionnaireItemDefinition WithChoice(
        const QuestionnaireChoiceDefinition& choice) const;
    QuestionnaireItemDefinition WithInput(
        const QuestionnaireInputDefinition& input) const;
    QuestionnaireItemDefinition WithValidator(
        QuestionnaireValidator validator) const;

    Str Name() const { return name; }
    Str AccessibilityLabel() const { return accessibilityLabel; }
    Str Description() const { return description; }
    bool IsRequired() const { return required; }
    bool IsMultiple() const { return multiple; }
    bool IsDisabled() const { return disabled; }
    const Vec<QuestionnaireChoiceDefinition>& Choices() const {
        return choices;
    }
    // Null is None.
    const QuestionnaireInputDefinition* Input() const {
        return hasInput ? &input : nullptr;
    }
    QuestionnaireValidator Validator() const { return validator; }
};

enum class QuestionnaireItemStatus : uint8_t {
    Unanswered,
    Answered,
    Skipped
};

enum class QuestionnaireShortcutMode : uint8_t {
    Letters,
    Numbers
};

// An answer snapshot. Input drafts are deliberately excluded. `freeform`
// empty is None: a freeform answer is never blank.
struct QuestionnaireAnswer {
    const Str* choices = nullptr;
    int nChoices = 0;
    Str freeform = {};

    static QuestionnaireAnswer New() { return {}; }
    // with_choices: duplicates dropped, first occurrence kept. The array is
    // allocated from `a`.
    QuestionnaireAnswer WithChoices(Arena* a, const Str* values, int n) const;
    // with_freeform: a blank value is no freeform answer.
    QuestionnaireAnswer WithFreeform(Str value) const;

    int ChoicesLen() const { return nChoices; }
    Str ChoiceAt(int i) const { return choices[i]; }
    Str Freeform() const { return freeform; }
    bool HasFreeform() const { return len(freeform) > 0; }
    bool IsEmpty() const { return nChoices == 0 && !HasFreeform(); }
    bool HasChoice(Str value) const;
};

bool operator==(const QuestionnaireAnswer& a, const QuestionnaireAnswer& b);
inline bool operator!=(const QuestionnaireAnswer& a,
                       const QuestionnaireAnswer& b) {
    return !(a == b);
}

struct QuestionnaireAnswerEntry {
    Str name = {};
    QuestionnaireAnswer answer = {};
};

struct QuestionnaireAnswers {
    const QuestionnaireAnswerEntry* entries = nullptr;
    int n = 0;

    // Null is None.
    const QuestionnaireAnswer* Get(Str name) const;
    int Len() const { return n; }
    bool IsEmpty() const { return n == 0; }
};

struct QuestionnaireProgressState {
    int current = 0;
    int total = 0;

    int Current() const { return current; }
    int Total() const { return total; }
};

struct QuestionnaireItemState {
    Str name = {};
    QuestionnaireItemStatus status = QuestionnaireItemStatus::Unanswered;
    bool required = false;
    bool multiple = false;
    bool disabled = false;
    bool invalid = false;
    bool hasInput = false;

    Str Name() const { return name; }
    QuestionnaireItemStatus Status() const { return status; }
    bool IsRequired() const { return required; }
    bool IsMultiple() const { return multiple; }
    bool IsDisabled() const { return disabled; }
    bool IsInvalid() const { return invalid; }
    bool HasInput() const { return hasInput; }
};

struct QuestionnaireChoiceState {
    Str value = {};
    bool selected = false;
    bool disabled = false;
    bool invalid = false;
    // Empty is None.
    Str shortcut = {};

    Str Value() const { return value; }
    bool IsSelected() const { return selected; }
    bool IsDisabled() const { return disabled; }
    bool IsInvalid() const { return invalid; }
    Str Shortcut() const { return shortcut; }
};

struct QuestionnaireNavigationState {
    bool previousVisible = false;
    bool nextVisible = false;
    bool skipVisible = false;
    bool submitVisible = false;
    bool confirmable = false;

    bool IsPreviousVisible() const { return previousVisible; }
    bool IsNextVisible() const { return nextVisible; }
    bool IsSkipVisible() const { return skipVisible; }
    bool IsSubmitVisible() const { return submitVisible; }
    bool IsConfirmable() const { return confirmable; }
};

// Immutable validation input. Validators cannot mutate the questionnaire.
struct QuestionnaireValidationContext {
    Str item = {};
    QuestionnaireAnswer answer = {};
    QuestionnaireAnswers answers = {};

    Str Item() const { return item; }
    const QuestionnaireAnswer& Answer() const { return answer; }
    const QuestionnaireAnswers& Answers() const { return answers; }
};

struct QuestionnaireAnswerChange {
    Str item = {};
    QuestionnaireAnswer answer = {};
    QuestionnaireItemStatus status = QuestionnaireItemStatus::Unanswered;

    Str Item() const { return item; }
    const QuestionnaireAnswer& Answer() const { return answer; }
    QuestionnaireItemStatus Status() const { return status; }
};

struct QuestionnaireSubmissionItem {
    Str name = {};
    QuestionnaireItemStatus status = QuestionnaireItemStatus::Unanswered;
    QuestionnaireAnswer answer = {};

    Str Name() const { return name; }
    QuestionnaireItemStatus Status() const { return status; }
    const QuestionnaireAnswer& Answer() const { return answer; }
};

struct QuestionnaireSubmission {
    const QuestionnaireSubmissionItem* items = nullptr;
    int n = 0;

    const QuestionnaireSubmissionItem* Items() const { return items; }
    int Len() const { return n; }
    // Null is None.
    const QuestionnaireAnswer* Answer(Str name) const;
};

enum class QuestionnaireEventKind : uint8_t {
    CurrentItemChanged,
    AnswerChanged,
    Completed,
    Submit
};

// QuestionnaireEvent. The payload fields that do not belong to `kind` are
// left empty. Arrays inside live in the temp arena for the length of the
// emit.
struct QuestionnaireEvent {
    QuestionnaireEventKind kind = QuestionnaireEventKind::CurrentItemChanged;
    // CurrentItemChanged { previous, current }; empty is None.
    Str previous = {};
    Str current = {};
    // AnswerChanged
    QuestionnaireAnswerChange change = {};
    // Completed / Submit
    QuestionnaireSubmission submission = {};
};

// Why an item currently fails validation. `Required` and `Unanswered` carry
// no text: base does not own product copy, so the presentation layer
// supplies the localized sentence. `Message` is the text a validator or the
// host already wrote.
enum class QuestionnaireValidationErrorKind : uint8_t {
    Required,
    Unanswered,
    Message
};

struct QuestionnaireValidationError {
    QuestionnaireValidationErrorKind kind =
        QuestionnaireValidationErrorKind::Required;
    Str message = {};

    static QuestionnaireValidationError Required() {
        return {QuestionnaireValidationErrorKind::Required, {}};
    }
    static QuestionnaireValidationError Unanswered() {
        return {QuestionnaireValidationErrorKind::Unanswered, {}};
    }
    static QuestionnaireValidationError Message(Str message) {
        return {QuestionnaireValidationErrorKind::Message, message};
    }
    // The text the host or a validator wrote; empty for a built-in reason.
    Str MessageText() const {
        return kind == QuestionnaireValidationErrorKind::Message ? message
                                                                 : Str{};
    }
};

bool operator==(const QuestionnaireValidationError& a,
                const QuestionnaireValidationError& b);

// QuestionnaireSchemaError, and `Ok` where Rust answers `Ok(())`. The names
// point at the caller's arguments or the state's schema.
enum class QuestionnaireSchemaErrorKind : uint8_t {
    Ok,
    DuplicateItem,
    DuplicateChoice,
    MultipleDefaultsForSingleItem,
    UnknownItem,
    UnknownChoice,
    AnswerDoesNotMatchItem
};

struct QuestionnaireSchemaError {
    QuestionnaireSchemaErrorKind kind = QuestionnaireSchemaErrorKind::Ok;
    Str item = {};
    Str choice = {};

    bool IsOk() const { return kind == QuestionnaireSchemaErrorKind::Ok; }
    bool IsError() const { return !IsOk(); }
};

bool operator==(const QuestionnaireSchemaError& a,
                const QuestionnaireSchemaError& b);
// impl Display for QuestionnaireSchemaError.
TempStr QuestionnaireSchemaErrorMessageTemp(const QuestionnaireSchemaError& e);

// ItemRuntime: what each item holds besides its definition. Choice arrays are
// as long as the definition's choices and live in the state's arena; the
// freeform strings are heap-owned by the runtime.
struct QuestionnaireItemRuntime {
    bool disabled = false;
    bool* choiceDisabled = nullptr;
    bool inputDisabled = true;
    // The answer, as a flag per choice: the effective answer is read back in
    // schema order, so the order the flags were set in never shows.
    bool* selected = nullptr;
    Str freeform = {};
    bool* initialSelected = nullptr;
    Str initialFreeform = {};
    Str initialInputValue = {};
    bool hasInitialInputValue = false;
    bool skipped = false;
    bool validationAttempted = false;
    QuestionnaireValidationError internalError = {};
    bool hasInternalError = false;
    QuestionnaireValidationError externalError = {};
    bool hasExternalError = false;
    FocusHandle focus = {};
    FocusHandle* choiceFocus = nullptr;
    FocusHandle inputFocus = {};
};

// Owns questionnaire answers, validation, navigation and focus state.
struct QuestionnaireState {
    Arena* arena = nullptr;
    QuestionnaireItemDefinition* items = nullptr;
    QuestionnaireItemRuntime* runtime = nullptr;
    int nItems = 0;
    int current = -1;
    Str initialCurrent = {};
    QuestionnaireShortcutMode shortcutMode = QuestionnaireShortcutMode::Letters;
    bool hasShortcutMode = false;
    bool complete = false;
    FocusHandle focus = {};
    // cx.emit needs to know who is emitting; QuestionnaireStateNew stamps it.
    Entity<QuestionnaireState> self = {};

    ~QuestionnaireState();

    static QuestionnaireSchemaError ValidateSchema(
        const QuestionnaireItemDefinition* items, int n);

    // with_current_item / with_shortcuts, on a state just made.
    QuestionnaireSchemaError WithCurrentItem(Str name);
    void WithShortcuts(QuestionnaireShortcutMode mode);

    // Empty is None.
    Str CurrentItem() const;
    // -1 is None.
    int CurrentIx() const;
    int Total() const;
    QuestionnaireProgressState Progress() const;
    const QuestionnaireItemDefinition* ItemDefinition(Str name) const;
    const QuestionnaireChoiceDefinition* ChoiceDefinition(Str item,
                                                          Str value) const;
    bool ItemState(Str name, QuestionnaireItemState* out) const;
    bool ChoiceState(Str item, Str value, QuestionnaireChoiceState* out) const;
    // One-based position of a choice among its item's enabled choices, with
    // the enabled total. Assistive technology announces the pair.
    bool ChoicePosition(Str item, Str value, int* position, int* total) const;
    QuestionnaireNavigationState NavigationState() const;
    bool Answer(Str name, Arena* a, QuestionnaireAnswer* out) const;
    QuestionnaireAnswers Answers(Arena* a) const;
    // The active validation failure for an item, if any.
    const QuestionnaireValidationError* Error(Str name) const;
    bool IsComplete() const { return complete; }
    InputState* InputStateOf(Str name) const;
    FocusHandle GetFocusHandle() const { return focus; }
    const FocusHandle* ItemFocusHandle(Str name) const;
    const FocusHandle* ChoiceFocusHandle(Str item, Str value) const;
    bool IsCurrentInputFocused(const Window* win) const;
    // Whether the active item's freeform input currently holds text. The skin
    // needs it to decide whether an arrow key moves focus or edits the draft.
    bool CurrentInputHasText() const;
    // Empty is None.
    Str FocusedCurrentChoice(const Window* win) const;
    bool ShortcutMode(QuestionnaireShortcutMode* out) const;
    // "A".."Z" or "1".."9"; empty is None.
    Str ShortcutForChoice(Str item, Str value) const;
    Str ChoiceForShortcut(Str item, Str key) const;
    bool ActivateShortcut(Str key, Ctx* cx);

    QuestionnaireSchemaError SetCurrentItem(Str name, Ctx* cx);
    QuestionnaireSchemaError SetAnswer(Str item,
                                       const QuestionnaireAnswer& answer,
                                       Ctx* cx);
    QuestionnaireSchemaError SetInputValue(Str item, Str value, Ctx* cx);
    QuestionnaireSchemaError SetItemDisabled(Str name, bool disabled, Ctx* cx);
    QuestionnaireSchemaError SetChoiceDisabled(Str item, Str value,
                                               bool disabled, Ctx* cx);
    QuestionnaireSchemaError SetExternalError(Str item, Str error, Ctx* cx);
    QuestionnaireSchemaError ClearExternalError(Str item, Ctx* cx);
    void Reset(Ctx* cx);
    QuestionnaireSchemaError ActivateChoice(Str item, Str value, Ctx* cx);
    bool ConfirmCurrent(Ctx* cx);
    bool GoPrevious(Ctx* cx);
    bool GoNext(Ctx* cx);
    bool SkipCurrent(Ctx* cx);
    bool Submit(Ctx* cx);
    bool FocusCurrentItem(Ctx* cx) const;
    bool FocusInvalidItem(Str item, Ctx* cx) const;
    bool FocusChoice(Str item, Str value, Ctx* cx) const;
    bool FocusInput(Str item, Ctx* cx) const;
    bool FocusPreviousAnswer(Ctx* cx);
    bool FocusNextAnswer(Ctx* cx);
    bool MoveCurrentRadio(int direction, Ctx* cx);

    // The subscription each freeform InputState is given; the argument is
    // the item's index.
    static void OnInputChange(QuestionnaireState* self, Ctx* cx,
                              const InputEvent* ev, intptr_t itemIx);
};

// QuestionnaireState::new: validates the schema, copies it into the state
// and subscribes to every freeform input. On an error `out` is left invalid.
QuestionnaireSchemaError QuestionnaireStateNew(
    App* app, const QuestionnaireItemDefinition* items, int n,
    Entity<QuestionnaireState>* out);

template <>
struct EventEmitter<QuestionnaireState, QuestionnaireEvent> {};

// The answer control for one choice, wired to the questionnaire's behavior.
// A multiple-answer item hands back a Checkbox and a single-answer item a
// Radio, each already carrying its checked state, disabled state,
// accessibility name, position in set, focus handle, confirm key and change
// handler. The skin decides what the control looks like and what it contains.
struct QuestionnaireChoiceControl {
    enum class Kind : uint8_t {
        Checkbox,
        Radio
    };
    Kind kind = Kind::Radio;
    El* el = nullptr;

    // Answers false when the item or the choice is not part of the schema.
    static bool New(Ctx* cx, Entity<QuestionnaireState> state, Str item,
                    Str value, Str id, QuestionnaireChoiceControl* out);
};

// handle_key_down: routes a key press to the questionnaire's behavior. The
// skin installs it on the root's capture phase; the contract — arrows move
// between answers and items, Enter confirms a filled answer, a bare letter or
// digit activates a shortcut — lives here so a different skin keeps it. A
// consumed key has its `propagate` cleared, which is `prevent_default`.
void QuestionnaireHandleKeyDown(Entity<QuestionnaireState> state, KeyEvent* ev,
                                Ctx* cx);

} // namespace gpui
#endif // GPUI_BASE_QUESTIONNAIRE_H_
