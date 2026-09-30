#include "base/questionnaire.h"

#include "base/checkbox.h"
#include "base/radio.h"
#include "gpui/keymap.h"

#include <string.h>

namespace gpui {

// ─── types.rs ─────────────────────────────────────────────────────────────

QuestionnaireChoiceDefinition QuestionnaireChoiceDefinition::New(
    Str value, Str accessibilityLabel) {
    QuestionnaireChoiceDefinition d;
    d.value = value;
    d.accessibilityLabel = accessibilityLabel;
    return d;
}

QuestionnaireChoiceDefinition QuestionnaireChoiceDefinition::WithDescription(
    Str v) const {
    QuestionnaireChoiceDefinition d = *this;
    d.description = v;
    return d;
}

QuestionnaireChoiceDefinition QuestionnaireChoiceDefinition::WithDisabled(
    bool v) const {
    QuestionnaireChoiceDefinition d = *this;
    d.disabled = v;
    return d;
}

QuestionnaireChoiceDefinition
QuestionnaireChoiceDefinition::WithDefaultSelected(bool v) const {
    QuestionnaireChoiceDefinition d = *this;
    d.defaultSelected = v;
    return d;
}

QuestionnaireInputDefinition QuestionnaireInputDefinition::New(
    InputState* state, Str accessibilityLabel) {
    QuestionnaireInputDefinition d;
    d.state = state;
    d.accessibilityLabel = accessibilityLabel;
    return d;
}

QuestionnaireInputDefinition QuestionnaireInputDefinition::WithDisabled(
    bool v) const {
    QuestionnaireInputDefinition d = *this;
    d.disabled = v;
    return d;
}

QuestionnaireItemDefinition QuestionnaireItemDefinition::New(
    Str name, Str accessibilityLabel) {
    QuestionnaireItemDefinition d;
    d.name = name;
    d.accessibilityLabel = accessibilityLabel;
    return d;
}

QuestionnaireItemDefinition QuestionnaireItemDefinition::WithDescription(
    Str v) const {
    QuestionnaireItemDefinition d = *this;
    d.description = v;
    return d;
}

QuestionnaireItemDefinition QuestionnaireItemDefinition::WithRequired(
    bool v) const {
    QuestionnaireItemDefinition d = *this;
    d.required = v;
    return d;
}

QuestionnaireItemDefinition QuestionnaireItemDefinition::WithMultiple(
    bool v) const {
    QuestionnaireItemDefinition d = *this;
    d.multiple = v;
    return d;
}

QuestionnaireItemDefinition QuestionnaireItemDefinition::WithDisabled(
    bool v) const {
    QuestionnaireItemDefinition d = *this;
    d.disabled = v;
    return d;
}

QuestionnaireItemDefinition QuestionnaireItemDefinition::WithChoices(
    const QuestionnaireChoiceDefinition* values, int n) const {
    QuestionnaireItemDefinition d = *this;
    VecClear(d.choices);
    for (int i = 0; i < n; i++) {
        VecAppend(d.choices, values[i]);
    }
    return d;
}

QuestionnaireItemDefinition QuestionnaireItemDefinition::WithChoice(
    const QuestionnaireChoiceDefinition& choice) const {
    QuestionnaireItemDefinition d = *this;
    VecAppend(d.choices, choice);
    return d;
}

QuestionnaireItemDefinition QuestionnaireItemDefinition::WithInput(
    const QuestionnaireInputDefinition& v) const {
    QuestionnaireItemDefinition d = *this;
    d.input = v;
    d.hasInput = true;
    return d;
}

QuestionnaireItemDefinition QuestionnaireItemDefinition::WithValidator(
    QuestionnaireValidator v) const {
    QuestionnaireItemDefinition d = *this;
    d.validator = v;
    return d;
}

template <typename T>
static T* QAlloc(Arena* a, int n) {
    if (n <= 0) {
        return nullptr;
    }
    T* p = (T*)Alloc(a, n * (int)sizeof(T));
    memset((void*)p, 0, (size_t)n * sizeof(T));
    return p;
}

static bool IsBlank(Str s) {
    return len(StrTrimAscii(s)) == 0;
}

bool QuestionnaireAnswer::HasChoice(Str value) const {
    for (int i = 0; i < nChoices; i++) {
        if (StrEq(choices[i], value)) {
            return true;
        }
    }
    return false;
}

QuestionnaireAnswer QuestionnaireAnswer::WithChoices(Arena* a,
                                                     const Str* values,
                                                     int n) const {
    QuestionnaireAnswer out = *this;
    Str* list = QAlloc<Str>(a, n);
    int count = 0;
    for (int i = 0; i < n; i++) {
        bool seen = false;
        for (int j = 0; j < count; j++) {
            seen = seen || StrEq(list[j], values[i]);
        }
        if (!seen) {
            list[count++] = values[i];
        }
    }
    out.choices = list;
    out.nChoices = count;
    return out;
}

QuestionnaireAnswer QuestionnaireAnswer::WithFreeform(Str value) const {
    QuestionnaireAnswer out = *this;
    out.freeform = IsBlank(value) ? Str{} : value;
    return out;
}

bool operator==(const QuestionnaireAnswer& a, const QuestionnaireAnswer& b) {
    if (a.nChoices != b.nChoices || !StrEq(a.freeform, b.freeform)) {
        return false;
    }
    for (int i = 0; i < a.nChoices; i++) {
        if (!StrEq(a.choices[i], b.choices[i])) {
            return false;
        }
    }
    return true;
}

const QuestionnaireAnswer* QuestionnaireAnswers::Get(Str name) const {
    for (int i = 0; i < n; i++) {
        if (StrEq(entries[i].name, name)) {
            return &entries[i].answer;
        }
    }
    return nullptr;
}

const QuestionnaireAnswer* QuestionnaireSubmission::Answer(Str name) const {
    for (int i = 0; i < n; i++) {
        if (StrEq(items[i].name, name)) {
            return &items[i].answer;
        }
    }
    return nullptr;
}

bool operator==(const QuestionnaireValidationError& a,
                const QuestionnaireValidationError& b) {
    return a.kind == b.kind && StrEq(a.MessageText(), b.MessageText());
}

bool operator==(const QuestionnaireSchemaError& a,
                const QuestionnaireSchemaError& b) {
    return a.kind == b.kind && StrEq(a.item, b.item) &&
           StrEq(a.choice, b.choice);
}

TempStr QuestionnaireSchemaErrorMessageTemp(const QuestionnaireSchemaError& e) {
    switch (e.kind) {
        case QuestionnaireSchemaErrorKind::Ok:
            return Str{};
        case QuestionnaireSchemaErrorKind::DuplicateItem:
            return fmt("duplicate questionnaire item `%s`", e.item);
        case QuestionnaireSchemaErrorKind::DuplicateChoice:
            return fmt("duplicate choice `%s` in item `%s`", e.choice, e.item);
        case QuestionnaireSchemaErrorKind::MultipleDefaultsForSingleItem:
            return fmt(
                "single-choice item `%s` has more than one default "
                "answer",
                e.item);
        case QuestionnaireSchemaErrorKind::UnknownItem:
            return fmt("unknown questionnaire item `%s`", e.item);
        case QuestionnaireSchemaErrorKind::UnknownChoice:
            return fmt("unknown choice `%s` in item `%s`", e.choice, e.item);
        case QuestionnaireSchemaErrorKind::AnswerDoesNotMatchItem:
            return fmt("answer does not match item `%s`", e.item);
    }
    return Str{};
}

static QuestionnaireSchemaError SchemaOk() {
    return {};
}

static QuestionnaireSchemaError SchemaError(QuestionnaireSchemaErrorKind kind,
                                            Str item, Str choice = {}) {
    QuestionnaireSchemaError e;
    e.kind = kind;
    e.item = item;
    e.choice = choice;
    return e;
}

// ─── state.rs ─────────────────────────────────────────────────────────────

// A heap string slot: replaced by a copy of `v`, the old one freed after the
// copy so a slot may be set from itself.
static void SetOwned(Str* slot, Str v) {
    Str next = len(v) > 0 ? StrDup(v) : Str{};
    StrFree(*slot);
    *slot = next;
}

static void SetError(QuestionnaireValidationError* slot, bool* has,
                     const QuestionnaireValidationError* v) {
    Str old = slot->message;
    if (v) {
        *slot = *v;
        slot->message = len(v->message) > 0 ? StrDup(v->message) : Str{};
        *has = true;
    } else {
        *slot = {};
        *has = false;
    }
    StrFree(old);
}

// The state's own focus moves go through here. A handle focused while a text
// field holds the keyboard takes the keyboard from it, which is what GPUI's
// single focus does; win->input would otherwise keep taking the typing.
static void FocusOn(Ctx* cx, FocusHandle h) {
    Window* win = cx ? cx->win : nullptr;
    if (!win || !h.IsValid()) {
        return;
    }
    if (win->input && win->input->focused && win->input->focus != h) {
        InputBlur(win->input, cx->app, win);
    }
    FocusHandleFocus(win, h);
}

static void FocusInputOn(Ctx* cx, InputState* s) {
    if (!cx || !cx->win || !s) {
        return;
    }
    InputFocus(s, cx->app, cx->win);
}

QuestionnaireState::~QuestionnaireState() {
    for (int i = 0; i < nItems; i++) {
        QuestionnaireItemRuntime& r = runtime[i];
        StrFree(r.freeform);
        StrFree(r.initialFreeform);
        StrFree(r.initialInputValue);
        StrFree(r.internalError.message);
        StrFree(r.externalError.message);
    }
    delete[] items;
    if (arena) {
        ArenaDelete(arena);
    }
}

QuestionnaireSchemaError QuestionnaireState::ValidateSchema(
    const QuestionnaireItemDefinition* defs, int n) {
    for (int i = 0; i < n; i++) {
        const QuestionnaireItemDefinition& item = defs[i];
        for (int j = 0; j < i; j++) {
            if (StrEq(defs[j].name, item.name)) {
                return SchemaError(QuestionnaireSchemaErrorKind::DuplicateItem,
                                   item.name);
            }
        }
        int defaults = 0;
        int nc = len(item.choices);
        for (int c = 0; c < nc; c++) {
            for (int d = 0; d < c; d++) {
                if (StrEq(item.choices[d].value, item.choices[c].value)) {
                    return SchemaError(
                        QuestionnaireSchemaErrorKind::DuplicateChoice,
                        item.name, item.choices[c].value);
                }
            }
            defaults += item.choices[c].defaultSelected ? 1 : 0;
        }
        if (!item.multiple && defaults > 1) {
            return SchemaError(
                QuestionnaireSchemaErrorKind::MultipleDefaultsForSingleItem,
                item.name);
        }
    }
    return SchemaOk();
}

static int ItemIxOpt(const QuestionnaireState* s, Str name) {
    for (int i = 0; i < s->nItems; i++) {
        if (StrEq(s->items[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int ChoiceIxOpt(const QuestionnaireState* s, int itemIx, Str value) {
    const Vec<QuestionnaireChoiceDefinition>& choices = s->items[itemIx]
                                                            .choices;
    for (int i = 0; i < len(choices); i++) {
        if (StrEq(choices[i].value, value)) {
            return i;
        }
    }
    return -1;
}

static int NChoices(const QuestionnaireState* s, int itemIx) {
    return len(s->items[itemIx].choices);
}

// A copy of the schema string in the state's arena, so the caller's
// definitions can be temporaries.
static Str Own(Arena* a, Str s) {
    return len(s) > 0 ? StrDup(a, s) : Str{};
}

QuestionnaireSchemaError QuestionnaireStateNew(
    App* app, const QuestionnaireItemDefinition* defs, int n,
    Entity<QuestionnaireState>* out) {
    if (out) {
        *out = {};
    }
    QuestionnaireSchemaError err = QuestionnaireState::ValidateSchema(defs, n);
    if (err.IsError()) {
        return err;
    }
    Entity<QuestionnaireState> e = EntityNewState<QuestionnaireState>(app);
    QuestionnaireState* s = e.Get(app);
    if (!s) {
        return SchemaOk();
    }
    s->self = e;
    s->arena = ArenaNew();
    Arena* a = s->arena;
    s->nItems = n;
    s->items = n > 0 ? new QuestionnaireItemDefinition[n] : nullptr;
    s->runtime = QAlloc<QuestionnaireItemRuntime>(a, n);
    for (int i = 0; i < n; i++) {
        new (&s->runtime[i]) QuestionnaireItemRuntime();
    }

    for (int i = 0; i < n; i++) {
        QuestionnaireItemDefinition& item = s->items[i];
        item = defs[i];
        item.name = Own(a, item.name);
        item.accessibilityLabel = Own(a, item.accessibilityLabel);
        item.description = Own(a, item.description);
        item.input.accessibilityLabel = Own(a, item.input.accessibilityLabel);
        int nc = len(item.choices);
        for (int c = 0; c < nc; c++) {
            QuestionnaireChoiceDefinition& choice = item.choices[c];
            choice.value = Own(a, choice.value);
            choice.accessibilityLabel = Own(a, choice.accessibilityLabel);
            choice.description = Own(a, choice.description);
        }

        QuestionnaireItemRuntime& r = s->runtime[i];
        r.disabled = item.disabled;
        r.choiceDisabled = QAlloc<bool>(a, nc);
        r.selected = QAlloc<bool>(a, nc);
        r.initialSelected = QAlloc<bool>(a, nc);
        r.choiceFocus = QAlloc<FocusHandle>(a, nc);
        for (int c = 0; c < nc; c++) {
            r.choiceDisabled[c] = item.choices[c].disabled;
            r.selected[c] = item.choices[c].defaultSelected && !item.choices[c]
                                                                    .disabled;
            r.choiceFocus[c] = FocusHandleNew(app);
        }
        r.inputDisabled = !item.hasInput || item.input.disabled;

        if (item.hasInput && item.input.state) {
            InputState* input = item.input.state;
            input->disabled = item.disabled || item.input.disabled;
            Str value = InputValue(input);
            SetOwned(&r.initialInputValue, value);
            r.hasInitialInputValue = true;
            if (!input->focus.IsValid()) {
                input->focus = FocusHandleNew(app);
            }
            r.inputFocus = input->focus;
            if (!IsBlank(value) && !item.input.disabled) {
                if (!item.multiple) {
                    for (int c = 0; c < nc; c++) {
                        r.selected[c] = false;
                    }
                }
                SetOwned(&r.freeform, value);
            }
            input->onChange =
                ListenTo(e, &QuestionnaireState::OnInputChange, (intptr_t)i);
        }
        for (int c = 0; c < nc; c++) {
            r.initialSelected[c] = r.selected[c];
        }
        SetOwned(&r.initialFreeform, r.freeform);
        r.focus = FocusHandleNew(app);
    }

    for (int i = 0; i < n; i++) {
        if (!s->runtime[i].disabled) {
            s->current = i;
            break;
        }
    }
    s->initialCurrent = s->current >= 0 ? s->items[s->current].name : Str{};
    s->focus = FocusHandleNew(app);
    if (out) {
        *out = e;
    }
    return SchemaOk();
}

QuestionnaireSchemaError QuestionnaireState::WithCurrentItem(Str name) {
    int ix = ItemIxOpt(this, name);
    if (ix < 0) {
        return SchemaError(QuestionnaireSchemaErrorKind::UnknownItem, name);
    }
    if (!runtime[ix].disabled) {
        current = ix;
        initialCurrent = items[ix].name;
    }
    return SchemaOk();
}

void QuestionnaireState::WithShortcuts(QuestionnaireShortcutMode mode) {
    shortcutMode = mode;
    hasShortcutMode = true;
}

Str QuestionnaireState::CurrentItem() const {
    return current >= 0 ? items[current].name : Str{};
}

int QuestionnaireState::CurrentIx() const {
    if (current < 0) {
        return -1;
    }
    int pos = 0;
    for (int i = 0; i < nItems; i++) {
        if (runtime[i].disabled) {
            continue;
        }
        if (i == current) {
            return pos;
        }
        pos++;
    }
    return -1;
}

int QuestionnaireState::Total() const {
    int total = 0;
    for (int i = 0; i < nItems; i++) {
        total += runtime[i].disabled ? 0 : 1;
    }
    return total;
}

QuestionnaireProgressState QuestionnaireState::Progress() const {
    int ix = CurrentIx();
    return {ix >= 0 ? ix + 1 : 0, Total()};
}

const QuestionnaireItemDefinition* QuestionnaireState::ItemDefinition(
    Str name) const {
    int ix = ItemIxOpt(this, name);
    return ix >= 0 ? &items[ix] : nullptr;
}

const QuestionnaireChoiceDefinition* QuestionnaireState::ChoiceDefinition(
    Str item, Str value) const {
    int ix = ItemIxOpt(this, item);
    if (ix < 0) {
        return nullptr;
    }
    int c = ChoiceIxOpt(this, ix, value);
    return c >= 0 ? &items[ix].choices[c] : nullptr;
}

// effective_answer: a disabled item answers nothing, and a disabled choice or
// input drops out of the answer without being forgotten.
static QuestionnaireAnswer EffectiveAnswer(const QuestionnaireState* s, int ix,
                                           Arena* a) {
    QuestionnaireAnswer out;
    const QuestionnaireItemRuntime& r = s->runtime[ix];
    if (r.disabled) {
        return out;
    }
    int nc = NChoices(s, ix);
    Str* list = QAlloc<Str>(a, nc);
    int count = 0;
    for (int c = 0; c < nc; c++) {
        if (r.selected[c] && !r.choiceDisabled[c]) {
            list[count++] = s->items[ix].choices[c].value;
        }
    }
    out.choices = list;
    out.nChoices = count;
    if (!r.inputDisabled && len(r.freeform) > 0) {
        out.freeform = StrDup(a, r.freeform);
    }
    return out;
}

static bool EffectiveIsEmpty(const QuestionnaireState* s, int ix) {
    const QuestionnaireItemRuntime& r = s->runtime[ix];
    if (r.disabled) {
        return true;
    }
    for (int c = 0; c < NChoices(s, ix); c++) {
        if (r.selected[c] && !r.choiceDisabled[c]) {
            return false;
        }
    }
    return r.inputDisabled || len(r.freeform) == 0;
}

static QuestionnaireItemStatus Status(const QuestionnaireState* s, int ix) {
    if (s->runtime[ix].skipped) {
        return QuestionnaireItemStatus::Skipped;
    }
    return EffectiveIsEmpty(s, ix) ? QuestionnaireItemStatus::Unanswered
                                   : QuestionnaireItemStatus::Answered;
}

static const QuestionnaireValidationError* ErrorAt(const QuestionnaireState* s,
                                                   int ix) {
    const QuestionnaireItemRuntime& r = s->runtime[ix];
    if (r.skipped || r.disabled) {
        return nullptr;
    }
    if (r.hasExternalError) {
        return &r.externalError;
    }
    if (r.validationAttempted && r.hasInternalError) {
        return &r.internalError;
    }
    return nullptr;
}

bool QuestionnaireState::ItemState(Str name,
                                   QuestionnaireItemState* out) const {
    int ix = ItemIxOpt(this, name);
    if (ix < 0) {
        return false;
    }
    const QuestionnaireItemDefinition& d = items[ix];
    if (out) {
        out->name = d.name;
        out->status = Status(this, ix);
        out->required = d.required;
        out->multiple = d.multiple;
        out->disabled = runtime[ix].disabled;
        out->invalid = ErrorAt(this, ix) != nullptr;
        out->hasInput = d.hasInput;
    }
    return true;
}

bool QuestionnaireState::ChoiceState(Str item, Str value,
                                     QuestionnaireChoiceState* out) const {
    int ix = ItemIxOpt(this, item);
    if (ix < 0) {
        return false;
    }
    int c = ChoiceIxOpt(this, ix, value);
    if (c < 0) {
        return false;
    }
    const QuestionnaireItemRuntime& r = runtime[ix];
    if (out) {
        out->value = items[ix].choices[c].value;
        out->selected = r.selected[c];
        out->disabled = r.disabled || r.choiceDisabled[c];
        out->invalid = ErrorAt(this, ix) != nullptr;
        out->shortcut = ShortcutForChoice(item, value);
    }
    return true;
}

bool QuestionnaireState::ChoicePosition(Str item, Str value, int* position,
                                        int* total) const {
    int ix = ItemIxOpt(this, item);
    if (ix < 0) {
        return false;
    }
    const QuestionnaireItemRuntime& r = runtime[ix];
    int enabled = 0;
    int found = -1;
    for (int c = 0; c < NChoices(this, ix); c++) {
        if (r.disabled || r.choiceDisabled[c]) {
            continue;
        }
        if (StrEq(items[ix].choices[c].value, value)) {
            found = enabled;
        }
        enabled++;
    }
    if (found < 0) {
        return false;
    }
    if (position) {
        *position = found + 1;
    }
    if (total) {
        *total = enabled;
    }
    return true;
}

QuestionnaireNavigationState QuestionnaireState::NavigationState() const {
    QuestionnaireNavigationState nav;
    int ix = CurrentIx();
    if (ix < 0) {
        return nav;
    }
    int total = Total();
    nav.previousVisible = ix > 0;
    nav.nextVisible = ix + 1 < total;
    nav.skipVisible = !items[current].required;
    nav.submitVisible = ix + 1 == total;
    nav.confirmable =
        Status(this, current) != QuestionnaireItemStatus::Unanswered;
    return nav;
}

bool QuestionnaireState::Answer(Str name, Arena* a,
                                QuestionnaireAnswer* out) const {
    int ix = ItemIxOpt(this, name);
    if (ix < 0) {
        return false;
    }
    if (out) {
        *out = EffectiveAnswer(this, ix, a);
    }
    return true;
}

QuestionnaireAnswers QuestionnaireState::Answers(Arena* a) const {
    QuestionnaireAnswers out;
    QuestionnaireAnswerEntry* entries =
        QAlloc<QuestionnaireAnswerEntry>(a, nItems);
    int count = 0;
    for (int i = 0; i < nItems; i++) {
        if (runtime[i].disabled) {
            continue;
        }
        entries[count].name = items[i].name;
        entries[count].answer = EffectiveAnswer(this, i, a);
        count++;
    }
    out.entries = entries;
    out.n = count;
    return out;
}

const QuestionnaireValidationError* QuestionnaireState::Error(Str name) const {
    int ix = ItemIxOpt(this, name);
    return ix >= 0 ? ErrorAt(this, ix) : nullptr;
}

InputState* QuestionnaireState::InputStateOf(Str name) const {
    const QuestionnaireItemDefinition* d = ItemDefinition(name);
    return d && d->hasInput ? d->input.state : nullptr;
}

const FocusHandle* QuestionnaireState::ItemFocusHandle(Str name) const {
    int ix = ItemIxOpt(this, name);
    return ix >= 0 ? &runtime[ix].focus : nullptr;
}

const FocusHandle* QuestionnaireState::ChoiceFocusHandle(Str item,
                                                         Str value) const {
    int ix = ItemIxOpt(this, item);
    if (ix < 0) {
        return nullptr;
    }
    int c = ChoiceIxOpt(this, ix, value);
    return c >= 0 ? &runtime[ix].choiceFocus[c] : nullptr;
}

bool QuestionnaireState::IsCurrentInputFocused(const Window* win) const {
    if (current < 0) {
        return false;
    }
    FocusHandle h = runtime[current].inputFocus;
    return h.IsValid() && FocusHandleIsFocused(win, h);
}

bool QuestionnaireState::CurrentInputHasText() const {
    if (current < 0 || !items[current].hasInput ||
        !items[current].input.state) {
        return false;
    }
    return !IsBlank(InputValue(items[current].input.state));
}

Str QuestionnaireState::FocusedCurrentChoice(const Window* win) const {
    if (current < 0) {
        return {};
    }
    for (int c = 0; c < NChoices(this, current); c++) {
        if (FocusHandleIsFocused(win, runtime[current].choiceFocus[c])) {
            return items[current].choices[c].value;
        }
    }
    return {};
}

bool QuestionnaireState::ShortcutMode(QuestionnaireShortcutMode* out) const {
    if (hasShortcutMode && out) {
        *out = shortcutMode;
    }
    return hasShortcutMode;
}

Str QuestionnaireState::ShortcutForChoice(Str item, Str value) const {
    static const char kLetters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const char kDigits[] = "123456789";
    if (!hasShortcutMode) {
        return {};
    }
    int ix = ItemIxOpt(this, item);
    if (ix < 0) {
        return {};
    }
    int c = ChoiceIxOpt(this, ix, value);
    if (c < 0) {
        return {};
    }
    const QuestionnaireItemRuntime& r = runtime[ix];
    if (r.disabled || r.choiceDisabled[c]) {
        return {};
    }
    int position = -1;
    for (int k = 0; k <= c; k++) {
        position += r.choiceDisabled[k] ? 0 : 1;
    }
    if (position < 0) {
        return {};
    }
    if (shortcutMode == QuestionnaireShortcutMode::Letters && position < 26) {
        return Str(kLetters + position, 1);
    }
    if (shortcutMode == QuestionnaireShortcutMode::Numbers && position < 9) {
        return Str(kDigits + position, 1);
    }
    return {};
}

Str QuestionnaireState::ChoiceForShortcut(Str item, Str key) const {
    int ix = ItemIxOpt(this, item);
    if (ix < 0) {
        return {};
    }
    for (int c = 0; c < NChoices(this, ix); c++) {
        Str value = items[ix].choices[c].value;
        Str shortcut = ShortcutForChoice(item, value);
        if (len(shortcut) > 0 && StrEqI(shortcut, key)) {
            return value;
        }
    }
    return {};
}

static void NotifySelf(QuestionnaireState* s, Ctx* cx) {
    if (!cx) {
        return;
    }
    if (s->self.IsValid()) {
        NotifyEntity(cx->app, s->self.id, cx->win);
    } else {
        Notify(cx);
    }
}

static void Emit(QuestionnaireState* s, Ctx* cx, QuestionnaireEvent* ev) {
    if (cx && s->self.IsValid()) {
        EntityEmit(cx->app, cx->win, s->self, ev);
    }
}

bool QuestionnaireState::ActivateShortcut(Str key, Ctx* cx) {
    if (current < 0) {
        return false;
    }
    Str item = items[current].name;
    Str choice = ChoiceForShortcut(item, key);
    if (len(choice) == 0) {
        return false;
    }
    if (ActivateChoice(item, choice, cx).IsError()) {
        return false;
    }
    return FocusChoice(item, choice, cx);
}

QuestionnaireSchemaError QuestionnaireState::SetCurrentItem(Str name, Ctx* cx) {
    int ix = ItemIxOpt(this, name);
    if (ix < 0) {
        return SchemaError(QuestionnaireSchemaErrorKind::UnknownItem, name);
    }
    if (!runtime[ix].disabled) {
        current = ix;
        FocusCurrentItem(cx);
        NotifySelf(this, cx);
    }
    return SchemaOk();
}

// validate_item: a disabled or skipped item always passes. Anything else
// records that validation was attempted, which is what makes later edits
// update the error live.
static bool ValidateItem(QuestionnaireState* s, int ix) {
    QuestionnaireItemRuntime& r = s->runtime[ix];
    if (r.disabled || r.skipped) {
        return true;
    }
    r.validationAttempted = true;
    Arena* tmp = GetTempArena();
    QuestionnaireAnswer answer = EffectiveAnswer(s, ix, tmp);
    const QuestionnaireItemDefinition& d = s->items[ix];
    if (answer.IsEmpty()) {
        QuestionnaireValidationError e =
            d.required ? QuestionnaireValidationError::Required()
                       : QuestionnaireValidationError::Unanswered();
        SetError(&r.internalError, &r.hasInternalError, &e);
    } else if (d.validator) {
        QuestionnaireValidationContext context;
        context.item = d.name;
        context.answer = answer;
        context.answers = s->Answers(tmp);
        Str message = {};
        if (d.validator(&context, &message)) {
            SetError(&r.internalError, &r.hasInternalError, nullptr);
        } else {
            QuestionnaireValidationError e =
                QuestionnaireValidationError::Message(message);
            SetError(&r.internalError, &r.hasInternalError, &e);
        }
    } else {
        SetError(&r.internalError, &r.hasInternalError, nullptr);
    }
    return ErrorAt(s, ix) == nullptr;
}

static void EmitAnswerChanged(QuestionnaireState* s, int ix, Ctx* cx) {
    QuestionnaireEvent ev;
    ev.kind = QuestionnaireEventKind::AnswerChanged;
    ev.change.item = s->items[ix].name;
    ev.change.answer = EffectiveAnswer(s, ix, GetTempArena());
    ev.change.status = Status(s, ix);
    Emit(s, cx, &ev);
}

static void AnswerDidChange(QuestionnaireState* s, int ix, bool emit, Ctx* cx) {
    QuestionnaireItemRuntime& r = s->runtime[ix];
    if (r.validationAttempted) {
        ValidateItem(s, ix);
    } else {
        SetError(&r.internalError, &r.hasInternalError, nullptr);
    }
    s->complete = false;
    if (emit) {
        EmitAnswerChanged(s, ix, cx);
    }
    NotifySelf(s, cx);
}

// The before half of `before != effective_answer || before_status != status`.
struct AnswerSnapshot {
    QuestionnaireAnswer answer;
    QuestionnaireItemStatus status;
};

static AnswerSnapshot Snapshot(const QuestionnaireState* s, int ix) {
    return {EffectiveAnswer(s, ix, GetTempArena()), Status(s, ix)};
}

static bool Changed(const QuestionnaireState* s, int ix,
                    const AnswerSnapshot& before) {
    return before.answer != EffectiveAnswer(s, ix, GetTempArena()) ||
           before.status != Status(s, ix);
}

static QuestionnaireSchemaError CheckAnswer(const QuestionnaireState* s, int ix,
                                            const QuestionnaireAnswer& answer) {
    const QuestionnaireItemDefinition& d = s->items[ix];
    int sources = answer.nChoices + (answer.HasFreeform() ? 1 : 0);
    if ((!d.multiple && sources > 1) || (answer.HasFreeform() && !d.hasInput)) {
        return SchemaError(QuestionnaireSchemaErrorKind::AnswerDoesNotMatchItem,
                           d.name);
    }
    for (int i = 0; i < answer.nChoices; i++) {
        int c = ChoiceIxOpt(s, ix, answer.choices[i]);
        if (c < 0) {
            return SchemaError(QuestionnaireSchemaErrorKind::UnknownChoice,
                               d.name, answer.choices[i]);
        }
        if (s->runtime[ix].choiceDisabled[c]) {
            return SchemaError(
                QuestionnaireSchemaErrorKind::AnswerDoesNotMatchItem, d.name);
        }
    }
    return SchemaOk();
}

QuestionnaireSchemaError QuestionnaireState::SetAnswer(
    Str item, const QuestionnaireAnswer& given, Ctx* cx) {
    int ix = ItemIxOpt(this, item);
    if (ix < 0) {
        return SchemaError(QuestionnaireSchemaErrorKind::UnknownItem, item);
    }
    AnswerSnapshot before = Snapshot(this, ix);
    QuestionnaireAnswer answer = given;
    if (answer.HasFreeform() && IsBlank(answer.freeform)) {
        answer.freeform = {};
    }
    QuestionnaireSchemaError err = CheckAnswer(this, ix, answer);
    if (err.IsError()) {
        return err;
    }
    QuestionnaireItemRuntime& r = runtime[ix];
    for (int c = 0; c < NChoices(this, ix); c++) {
        r.selected[c] = answer.HasChoice(items[ix].choices[c].value);
    }
    SetOwned(&r.freeform, answer.freeform);
    r.skipped = false;
    if (items[ix].hasInput && items[ix].input.state && answer.HasFreeform()) {
        InputSetValue(items[ix].input.state, answer.freeform);
    }
    if (Changed(this, ix, before)) {
        AnswerDidChange(this, ix, false, cx);
    }
    return SchemaOk();
}

// sync_input_answer: the draft becomes the freeform answer. A blank draft
// clears it; in a single-answer item a filled one replaces the choice.
static void SyncInputAnswer(QuestionnaireState* s, int ix, bool emit, Ctx* cx) {
    QuestionnaireItemRuntime& r = s->runtime[ix];
    if (r.disabled || r.inputDisabled) {
        return;
    }
    const QuestionnaireItemDefinition& d = s->items[ix];
    if (!d.hasInput || !d.input.state) {
        return;
    }
    AnswerSnapshot before = Snapshot(s, ix);
    Str value = InputValue(d.input.state);
    if (IsBlank(value)) {
        SetOwned(&r.freeform, {});
    } else {
        if (!d.multiple) {
            for (int c = 0; c < NChoices(s, ix); c++) {
                r.selected[c] = false;
            }
        }
        SetOwned(&r.freeform, value);
        r.skipped = false;
    }
    if (Changed(s, ix, before)) {
        AnswerDidChange(s, ix, emit, cx);
    }
}

void QuestionnaireState::OnInputChange(QuestionnaireState* self, Ctx* cx,
                                       const InputEvent* ev, intptr_t itemIx) {
    if (!ev || ev->kind != InputEventKind::Change || itemIx < 0 ||
        itemIx >= self->nItems) {
        return;
    }
    SyncInputAnswer(self, (int)itemIx, true, cx);
}

QuestionnaireSchemaError QuestionnaireState::SetInputValue(Str item, Str value,
                                                           Ctx* cx) {
    int ix = ItemIxOpt(this, item);
    if (ix < 0) {
        return SchemaError(QuestionnaireSchemaErrorKind::UnknownItem, item);
    }
    if (!items[ix].hasInput || !items[ix].input.state) {
        return SchemaError(QuestionnaireSchemaErrorKind::AnswerDoesNotMatchItem,
                           items[ix].name);
    }
    InputSetValue(items[ix].input.state, value);
    SyncInputAnswer(this, ix, false, cx);
    return SchemaOk();
}

static int FirstEnabledAfter(const QuestionnaireState* s, int ix) {
    for (int i = ix + 1; i < s->nItems; i++) {
        if (!s->runtime[i].disabled) {
            return i;
        }
    }
    return -1;
}

static int LastEnabledBefore(const QuestionnaireState* s, int ix) {
    for (int i = ix - 1; i >= 0; i--) {
        if (!s->runtime[i].disabled) {
            return i;
        }
    }
    return -1;
}

QuestionnaireSchemaError QuestionnaireState::SetItemDisabled(Str name,
                                                             bool disabled,
                                                             Ctx* cx) {
    int ix = ItemIxOpt(this, name);
    if (ix < 0) {
        return SchemaError(QuestionnaireSchemaErrorKind::UnknownItem, name);
    }
    if (runtime[ix].disabled == disabled) {
        return SchemaOk();
    }
    runtime[ix].disabled = disabled;
    if (items[ix].hasInput && items[ix].input.state) {
        items[ix].input.state->disabled = disabled || runtime[ix].inputDisabled;
    }
    complete = false;
    if (current == ix && disabled) {
        int next = FirstEnabledAfter(this, ix);
        if (next < 0) {
            next = LastEnabledBefore(this, ix);
        }
        current = next;
        FocusCurrentItem(cx);
    } else if (current < 0 && !disabled) {
        current = ix;
        FocusCurrentItem(cx);
    }
    NotifySelf(this, cx);
    return SchemaOk();
}

QuestionnaireSchemaError QuestionnaireState::SetChoiceDisabled(Str item,
                                                               Str value,
                                                               bool disabled,
                                                               Ctx* cx) {
    int ix = ItemIxOpt(this, item);
    if (ix < 0) {
        return SchemaError(QuestionnaireSchemaErrorKind::UnknownItem, item);
    }
    int c = ChoiceIxOpt(this, ix, value);
    if (c < 0) {
        return SchemaError(QuestionnaireSchemaErrorKind::UnknownChoice,
                           items[ix].name, value);
    }
    if (runtime[ix].choiceDisabled[c] == disabled) {
        return SchemaOk();
    }
    runtime[ix].choiceDisabled[c] = disabled;
    AnswerDidChange(this, ix, false, cx);
    return SchemaOk();
}

QuestionnaireSchemaError QuestionnaireState::SetExternalError(Str item,
                                                              Str error,
                                                              Ctx* cx) {
    int ix = ItemIxOpt(this, item);
    if (ix < 0) {
        return SchemaError(QuestionnaireSchemaErrorKind::UnknownItem, item);
    }
    QuestionnaireValidationError e =
        QuestionnaireValidationError::Message(error);
    SetError(&runtime[ix].externalError, &runtime[ix].hasExternalError, &e);
    complete = false;
    NotifySelf(this, cx);
    return SchemaOk();
}

QuestionnaireSchemaError QuestionnaireState::ClearExternalError(Str item,
                                                                Ctx* cx) {
    int ix = ItemIxOpt(this, item);
    if (ix < 0) {
        return SchemaError(QuestionnaireSchemaErrorKind::UnknownItem, item);
    }
    SetError(&runtime[ix].externalError, &runtime[ix].hasExternalError,
             nullptr);
    NotifySelf(this, cx);
    return SchemaOk();
}

void QuestionnaireState::Reset(Ctx* cx) {
    for (int ix = 0; ix < nItems; ix++) {
        QuestionnaireItemRuntime& r = runtime[ix];
        for (int c = 0; c < NChoices(this, ix); c++) {
            r.selected[c] = r.initialSelected[c];
        }
        SetOwned(&r.freeform, r.initialFreeform);
        r.skipped = false;
        r.validationAttempted = false;
        SetError(&r.internalError, &r.hasInternalError, nullptr);
        if (items[ix].hasInput && items[ix].input.state) {
            InputSetValue(items[ix].input.state,
                          r.hasInitialInputValue ? r.initialInputValue : Str{});
        }
    }
    complete = false;
    current = ItemIxOpt(this, initialCurrent);
    if (current >= 0 && runtime[current].disabled) {
        current = FirstEnabledAfter(this, -1);
    }
    FocusCurrentItem(cx);
    NotifySelf(this, cx);
}

QuestionnaireSchemaError QuestionnaireState::ActivateChoice(Str item, Str value,
                                                            Ctx* cx) {
    int ix = ItemIxOpt(this, item);
    if (ix < 0) {
        return SchemaError(QuestionnaireSchemaErrorKind::UnknownItem, item);
    }
    int c = ChoiceIxOpt(this, ix, value);
    if (c < 0) {
        return SchemaError(QuestionnaireSchemaErrorKind::UnknownChoice,
                           items[ix].name, value);
    }
    QuestionnaireItemRuntime& r = runtime[ix];
    if (r.disabled || r.choiceDisabled[c]) {
        return SchemaOk();
    }
    AnswerSnapshot before = Snapshot(this, ix);
    if (items[ix].multiple) {
        r.selected[c] = !r.selected[c];
    } else {
        for (int k = 0; k < NChoices(this, ix); k++) {
            r.selected[k] = k == c;
        }
        SetOwned(&r.freeform, {});
    }
    r.skipped = false;
    if (Changed(this, ix, before)) {
        AnswerDidChange(this, ix, true, cx);
    }
    return SchemaOk();
}

bool QuestionnaireState::ConfirmCurrent(Ctx* cx) {
    int ix = CurrentIx();
    if (ix < 0) {
        return false;
    }
    return ix + 1 == Total() ? Submit(cx) : GoNext(cx);
}

// The enabled item at a position among the enabled ones, or -1.
static int EnabledAt(const QuestionnaireState* s, int position) {
    int pos = 0;
    for (int i = 0; i < s->nItems; i++) {
        if (s->runtime[i].disabled) {
            continue;
        }
        if (pos == position) {
            return i;
        }
        pos++;
    }
    return -1;
}

static void ChangeCurrent(QuestionnaireState* s, int next, bool emit, Ctx* cx) {
    if (s->current == next) {
        return;
    }
    Str previous = s->current >= 0 ? s->items[s->current].name : Str{};
    s->current = next;
    s->FocusCurrentItem(cx);
    if (emit) {
        QuestionnaireEvent ev;
        ev.kind = QuestionnaireEventKind::CurrentItemChanged;
        ev.previous = previous;
        ev.current = next >= 0 ? s->items[next].name : Str{};
        Emit(s, cx, &ev);
    }
    NotifySelf(s, cx);
}

bool QuestionnaireState::GoPrevious(Ctx* cx) {
    int ix = CurrentIx();
    if (ix <= 0) {
        return false;
    }
    ChangeCurrent(this, EnabledAt(this, ix - 1), true, cx);
    return true;
}

bool QuestionnaireState::GoNext(Ctx* cx) {
    if (current < 0) {
        return false;
    }
    if (!ValidateItem(this, current)) {
        FocusInvalidItem(items[current].name, cx);
        NotifySelf(this, cx);
        return false;
    }
    int ix = CurrentIx();
    if (ix < 0 || ix + 1 >= Total()) {
        return false;
    }
    ChangeCurrent(this, EnabledAt(this, ix + 1), true, cx);
    return true;
}

bool QuestionnaireState::SkipCurrent(Ctx* cx) {
    if (current < 0 || items[current].required) {
        return false;
    }
    int ix = current;
    QuestionnaireItemRuntime& r = runtime[ix];
    for (int c = 0; c < NChoices(this, ix); c++) {
        r.selected[c] = false;
    }
    SetOwned(&r.freeform, {});
    r.skipped = true;
    complete = false;
    EmitAnswerChanged(this, ix, cx);
    int pos = CurrentIx();
    if (pos >= 0 && pos + 1 == Total()) {
        return Submit(cx);
    }
    return GoNext(cx);
}

static QuestionnaireSubmission SubmissionOf(const QuestionnaireState* s,
                                            Arena* a) {
    QuestionnaireSubmission out;
    QuestionnaireSubmissionItem* list =
        QAlloc<QuestionnaireSubmissionItem>(a, s->nItems);
    int count = 0;
    for (int i = 0; i < s->nItems; i++) {
        if (s->runtime[i].disabled) {
            continue;
        }
        list[count].name = s->items[i].name;
        list[count].status = Status(s, i);
        list[count].answer = EffectiveAnswer(s, i, a);
        count++;
    }
    out.items = list;
    out.n = count;
    return out;
}

bool QuestionnaireState::Submit(Ctx* cx) {
    int firstInvalid = -1;
    for (int i = 0; i < nItems; i++) {
        if (runtime[i].disabled) {
            continue;
        }
        if (!ValidateItem(this, i) && firstInvalid < 0) {
            firstInvalid = i;
        }
    }
    if (firstInvalid >= 0) {
        ChangeCurrent(this, firstInvalid, true, cx);
        FocusInvalidItem(items[firstInvalid].name, cx);
        NotifySelf(this, cx);
        return false;
    }

    QuestionnaireSubmission submission = SubmissionOf(this, GetTempArena());
    if (!complete) {
        complete = true;
        QuestionnaireEvent ev;
        ev.kind = QuestionnaireEventKind::Completed;
        ev.submission = submission;
        Emit(this, cx, &ev);
    }
    QuestionnaireEvent ev;
    ev.kind = QuestionnaireEventKind::Submit;
    ev.submission = submission;
    Emit(this, cx, &ev);
    NotifySelf(this, cx);
    return true;
}

bool QuestionnaireState::FocusCurrentItem(Ctx* cx) const {
    if (current < 0) {
        return false;
    }
    FocusOn(cx, runtime[current].focus);
    return true;
}

bool QuestionnaireState::FocusInvalidItem(Str item, Ctx* cx) const {
    int ix = ItemIxOpt(this, item);
    if (ix < 0) {
        return false;
    }
    const QuestionnaireItemRuntime& r = runtime[ix];
    InputState* input =
        items[ix].hasInput ? items[ix].input.state : (InputState*)nullptr;
    if (len(r.freeform) > 0 && !r.inputDisabled && input) {
        FocusInputOn(cx, input);
        return true;
    }
    for (int c = 0; c < NChoices(this, ix); c++) {
        if (r.selected[c] && !r.choiceDisabled[c]) {
            FocusOn(cx, r.choiceFocus[c]);
            return true;
        }
    }
    for (int c = 0; c < NChoices(this, ix); c++) {
        if (!r.choiceDisabled[c]) {
            FocusOn(cx, r.choiceFocus[c]);
            return true;
        }
    }
    if (!r.inputDisabled && input) {
        FocusInputOn(cx, input);
        return true;
    }
    FocusOn(cx, r.focus);
    return true;
}

bool QuestionnaireState::FocusChoice(Str item, Str value, Ctx* cx) const {
    int ix = ItemIxOpt(this, item);
    if (ix < 0) {
        return false;
    }
    int c = ChoiceIxOpt(this, ix, value);
    if (c < 0) {
        return false;
    }
    FocusOn(cx, runtime[ix].choiceFocus[c]);
    return true;
}

bool QuestionnaireState::FocusInput(Str item, Ctx* cx) const {
    int ix = ItemIxOpt(this, item);
    if (ix < 0 || !items[ix].hasInput || !items[ix].input.state) {
        return false;
    }
    FocusInputOn(cx, items[ix].input.state);
    return true;
}

// focus_adjacent_answer: the item's answers — its enabled choices, then its
// freeform input — as one ring the arrows walk.
static const int kInputTarget = -1;

static bool FocusAdjacentAnswer(QuestionnaireState* s, int direction, Ctx* cx) {
    if (s->current < 0) {
        return false;
    }
    int ix = s->current;
    Window* win = cx ? cx->win : nullptr;
    if (s->IsCurrentInputFocused(win) && s->CurrentInputHasText()) {
        return false;
    }
    const QuestionnaireItemRuntime& r = s->runtime[ix];
    const QuestionnaireItemDefinition& d = s->items[ix];
    int nc = NChoices(s, ix);
    int* targets = QAlloc<int>(GetTempArena(), nc + 1);
    int n = 0;
    for (int c = 0; c < nc; c++) {
        if (!r.choiceDisabled[c]) {
            targets[n++] = c;
        }
    }
    if (d.hasInput && d.input.state && !r.inputDisabled) {
        targets[n++] = kInputTarget;
    }
    if (n == 0) {
        return false;
    }

    int focused = -1;
    for (int k = 0; k < n && focused < 0; k++) {
        int t = targets[k];
        bool isFocused = t == kInputTarget
                             ? s->IsCurrentInputFocused(win)
                             : FocusHandleIsFocused(win, r.choiceFocus[t]);
        if (isFocused) {
            focused = k;
        }
    }

    // From the item group itself, the first move lands on the existing
    // answer — the last one going up — rather than on the first slot.
    if (focused < 0 && FocusHandleIsFocused(win, r.focus)) {
        int filled = -1;
        for (int j = 0; j < n && filled < 0; j++) {
            int k = direction < 0 ? n - 1 - j : j;
            int t = targets[k];
            bool isFilled =
                t == kInputTarget ? len(r.freeform) > 0 : r.selected[t];
            if (isFilled) {
                filled = k;
            }
        }
        if (filled >= 0) {
            int t = targets[filled];
            if (t == kInputTarget) {
                FocusInputOn(cx, d.input.state);
            } else {
                FocusOn(cx, r.choiceFocus[t]);
            }
            return true;
        }
    }

    int targetIx = 0;
    if (focused >= 0) {
        targetIx = direction < 0 ? (focused == 0 ? n - 1 : focused - 1)
                                 : (focused + 1) % n;
    } else {
        targetIx = direction < 0 ? n - 1 : 0;
    }

    // GPUI's Radio primitive intentionally leaves group arrow behavior to
    // its owner. Let the root emulate native radio movement only when both
    // adjacent answers are radios; crossing the boundary to an Input stays
    // in this schema-ordered answer sequence.
    if (!d.multiple && focused >= 0 && targets[focused] != kInputTarget &&
        targets[targetIx] != kInputTarget) {
        return false;
    }

    int t = targets[targetIx];
    if (t == kInputTarget) {
        FocusInputOn(cx, d.input.state);
    } else {
        if (!d.multiple) {
            s->ActivateChoice(d.name, d.choices[t].value, cx);
        }
        FocusOn(cx, s->runtime[ix].choiceFocus[t]);
    }
    return true;
}

bool QuestionnaireState::FocusPreviousAnswer(Ctx* cx) {
    return FocusAdjacentAnswer(this, -1, cx);
}

bool QuestionnaireState::FocusNextAnswer(Ctx* cx) {
    return FocusAdjacentAnswer(this, 1, cx);
}

bool QuestionnaireState::MoveCurrentRadio(int direction, Ctx* cx) {
    if (current < 0) {
        return false;
    }
    int ix = current;
    Window* win = cx ? cx->win : nullptr;
    if (items[ix].multiple || IsCurrentInputFocused(win)) {
        return false;
    }
    const QuestionnaireItemRuntime& r = runtime[ix];
    int nc = NChoices(this, ix);
    int* enabled = QAlloc<int>(GetTempArena(), nc);
    int n = 0;
    for (int c = 0; c < nc; c++) {
        if (!r.choiceDisabled[c]) {
            enabled[n++] = c;
        }
    }
    if (n == 0) {
        return false;
    }
    int at = -1;
    for (int k = 0; k < n && at < 0; k++) {
        if (FocusHandleIsFocused(win, r.choiceFocus[enabled[k]])) {
            at = k;
        }
    }
    for (int k = 0; k < n && at < 0; k++) {
        if (r.selected[enabled[k]]) {
            at = k;
        }
    }
    int target = 0;
    if (at >= 0) {
        target = direction < 0 ? (at == 0 ? n - 1 : at - 1) : (at + 1) % n;
    } else {
        target = direction < 0 ? n - 1 : 0;
    }
    int c = enabled[target];
    ActivateChoice(items[ix].name, items[ix].choices[c].value, cx);
    FocusOn(cx, runtime[ix].choiceFocus[c]);
    return true;
}

// ─── control.rs ───────────────────────────────────────────────────────────

// The two halves of an (item, choice) pair a control's listeners carry.
static intptr_t PackChoice(int itemIx, int choiceIx) {
    return (intptr_t)itemIx << 16 | (intptr_t)(choiceIx & 0xffff);
}

static void UnpackChoice(intptr_t v, int* itemIx, int* choiceIx) {
    *itemIx = (int)(v >> 16);
    *choiceIx = (int)(v & 0xffff);
}

static bool ChoiceAt(const QuestionnaireState* s, intptr_t packed, Str* item,
                     Str* value) {
    int ix = 0;
    int c = 0;
    UnpackChoice(packed, &ix, &c);
    if (ix < 0 || ix >= s->nItems || c < 0 || c >= NChoices(s, ix)) {
        return false;
    }
    *item = s->items[ix].name;
    *value = s->items[ix].choices[c].value;
    return true;
}

static int KeyModifierCount(const KeyEvent* ev) {
    return (ev->shift ? 1 : 0) + (ev->ctrl ? 1 : 0) + (ev->alt ? 1 : 0) +
           (ev->platform ? 1 : 0) + (ev->function ? 1 : 0);
}

// Enter confirms an answer that is already selected; an unselected control
// keeps Enter for activation.
static void ChoiceConfirmKey(QuestionnaireState* self, Ctx* cx,
                             const KeyEvent* ev, intptr_t packed) {
    Str item;
    Str value;
    if (!ev || !ev->propagate || ev->held || ev->vk != KeyReturn ||
        KeyModifierCount(ev) != 0 || !ChoiceAt(self, packed, &item, &value)) {
        return;
    }
    QuestionnaireChoiceState choice;
    if (!self->ChoiceState(item, value, &choice) || !choice.selected) {
        return;
    }
    if (self->ConfirmCurrent(cx)) {
        const_cast<KeyEvent*>(ev)->propagate = false;
    }
}

static void ChoiceChange(QuestionnaireState* self, Ctx* cx, const ClickEvent*,
                         intptr_t packed) {
    Str item;
    Str value;
    if (!ChoiceAt(self, packed, &item, &value)) {
        return;
    }
    self->ActivateChoice(item, value, cx);
    self->FocusChoice(item, value, cx);
}

bool QuestionnaireChoiceControl::New(Ctx* cx, Entity<QuestionnaireState> state,
                                     Str item, Str value, Str id,
                                     QuestionnaireChoiceControl* out) {
    const QuestionnaireState* s = state.Get(cx->app);
    if (!s) {
        return false;
    }
    int ix = ItemIxOpt(s, item);
    if (ix < 0) {
        return false;
    }
    int c = ChoiceIxOpt(s, ix, value);
    if (c < 0) {
        return false;
    }
    QuestionnaireChoiceState choice;
    s->ChoiceState(item, value, &choice);
    const QuestionnaireChoiceDefinition& definition = s->items[ix].choices[c];
    bool multiple = s->items[ix].multiple;
    int position = 0;
    int total = 0;
    bool hasPosition = s->ChoicePosition(item, value, &position, &total);
    FocusHandle focus = s->runtime[ix].choiceFocus[c];
    intptr_t packed = PackChoice(ix, c);
    Listener change = ListenTo(state, &ChoiceChange, packed);

    El* e = nullptr;
    if (multiple) {
        e = Checkbox::New(
            cx, id,
            choice.selected ? CheckboxState::Checked : CheckboxState::Unchecked,
            choice.disabled, change, nullptr, nullptr,
            definition.accessibilityLabel, 0, true, focus);
    } else {
        e = Radio::New(cx, id, choice.selected, choice.disabled, change);
        e->AriaLabel(definition.accessibilityLabel);
        // Radio::track_focus, which Rust applies only to an enabled radio.
        if (!choice.disabled) {
            e->TrackFocus(focus);
        }
    }
    if (hasPosition) {
        e->AriaPositionInSet(position)->AriaSizeOfSet(total);
    }
    if (len(definition.description) > 0) {
        e->AriaDescription(definition.description);
    }
    e->CaptureKeyDown(ListenTo(state, &ChoiceConfirmKey, packed));
    if (out) {
        out->kind = multiple ? Kind::Checkbox : Kind::Radio;
        out->el = e;
    }
    return true;
}

// ─── keyboard.rs ──────────────────────────────────────────────────────────

static bool FocusedAnswerIsFilled(const QuestionnaireState* s,
                                  const Window* win) {
    Str item = s->CurrentItem();
    if (len(item) == 0) {
        return false;
    }
    if (s->IsCurrentInputFocused(win)) {
        QuestionnaireAnswer answer;
        return s->Answer(item, GetTempArena(), &answer) && answer.HasFreeform();
    }
    Str value = s->FocusedCurrentChoice(win);
    QuestionnaireChoiceState choice;
    return len(value) > 0 && s->ChoiceState(item, value, &choice) &&
           choice.selected;
}

// window.default_prevented(), then the three keystrokes that are not a
// deliberate press: the OS repeating a held key, a chord whose modifiers type
// a character, and a key an input method is composing with.
void QuestionnaireHandleKeyDown(Entity<QuestionnaireState> state, KeyEvent* ev,
                                Ctx* cx) {
    QuestionnaireState* s = state.Get(cx->app);
    if (!s || !ev || !ev->propagate || !ev->vk || ev->held ||
        ev->preferCharacterInput || ev->imeInProgress) {
        return;
    }
    Window* win = cx->win;
    int modifiers = KeyModifierCount(ev);
    Str key = KeyName(ev->vk);
    bool inputFocused = s->IsCurrentInputFocused(win);
    bool inputHasText = inputFocused && s->CurrentInputHasText();
    bool singleRadioFocused = false;
    {
        QuestionnaireItemState item;
        singleRadioFocused = s->ItemState(s->CurrentItem(), &item) &&
                             !item.multiple &&
                             len(s->FocusedCurrentChoice(win)) > 0;
    }

    bool handled = false;
    if (StrEq(key, "enter") && KeySecondary(ev->ctrl, ev->platform) &&
        modifiers == 1) {
        handled = s->ConfirmCurrent(cx);
    } else if (modifiers != 0) {
        handled = false;
    } else if (inputFocused) {
        if (StrEq(key, "enter") && FocusedAnswerIsFilled(s, win)) {
            handled = s->ConfirmCurrent(cx);
        } else if (StrEq(key, "up") && !inputHasText) {
            handled = s->FocusPreviousAnswer(cx);
        } else if (StrEq(key, "down") && !inputHasText) {
            handled = s->FocusNextAnswer(cx);
        }
    } else if (StrEq(key, "up")) {
        handled = s->FocusPreviousAnswer(cx) ||
                  (singleRadioFocused && s->MoveCurrentRadio(-1, cx));
    } else if (StrEq(key, "down")) {
        handled = s->FocusNextAnswer(cx) ||
                  (singleRadioFocused && s->MoveCurrentRadio(1, cx));
    } else if (StrEq(key, "left") && singleRadioFocused) {
        handled = s->MoveCurrentRadio(-1, cx);
    } else if (StrEq(key, "right") && singleRadioFocused) {
        handled = s->MoveCurrentRadio(1, cx);
    } else if (StrEq(key, "left")) {
        handled = s->GoPrevious(cx);
    } else if (StrEq(key, "right")) {
        handled = s->NavigationState().IsConfirmable() && s->GoNext(cx);
    } else if (StrEq(key, "enter")) {
        handled = FocusedAnswerIsFilled(s, win) && s->ConfirmCurrent(cx);
    } else if (len(key) > 0) {
        handled = s->ActivateShortcut(key, cx);
    }

    if (handled) {
        ev->propagate = false;
    }
}

} // namespace gpui
