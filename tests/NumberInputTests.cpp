/* Ported from crates/base/src/number_input.rs step_value.
 *
 * Rust's own mod tests there is all #[gpui::test]: they drive step buttons
 * through a window to check that a press never moves focus off the editor.
 * step_value itself is pure, and it is where the behavior worth pinning is —
 * the precision it keeps, the range it clamps into, and its refusal to report
 * a step that did not move. */

#include "Test.h"

static Str Stepped(const char* value, StepAction action, double step) {
    return NumberStepValueTemp(Str(value), action, step, false, 0, false, 0);
}

static void AStepKeepsTheTextsPrecision() {
    // The integer stays an integer, and the two decimals stay two.
    utassert(base::StrEqI(Stepped("1", StepAction::Increment, 1), "2"));
    utassert(base::StrEqI(Stepped("1.50", StepAction::Increment, 1), "2.50"));
    utassert(base::StrEqI(Stepped("1.5", StepAction::Decrement, 1), "0.5"));
    // A fractional step widens an integer to the step's own precision.
    utassert(base::StrEqI(Stepped("1", StepAction::Increment, 0.01), "1.01"));
    utassert(base::StrEqI(Stepped("1234.56", StepAction::Decrement, 0.01),
                          "1234.55"));
}

static void TextThatIsNotANumberStepsFromZero() {
    // Rust's `current.unwrap_or(0.)`, and with no current there is nothing to
    // compare against, so the step always reports.
    utassert(base::StrEqI(Stepped("", StepAction::Increment, 1), "1"));
    utassert(base::StrEqI(Stepped("abc", StepAction::Decrement, 1), "-1"));
    // Trailing junk is not a number: parse::<f64>() refuses it.
    utassert(base::StrEqI(Stepped("12px", StepAction::Increment, 1), "1"));
}

static void TheRangeClampsAndWidens() {
    TempStr out = NumberStepValueTemp(StrL("9"), StepAction::Increment, 5,
                                      false, 0, true, 10.5);
    utassert(out);
    // Clamped to the max, whose own precision widens the result.
    utassert(StrEqI(out, "10.5"));

    out = NumberStepValueTemp(StrL("1"), StepAction::Decrement, 5, true, 0.25,
                              false, 0);
    utassert(out && StrEqI(out, "0.25"));

    // With no current value the range is entered immediately, in either
    // direction. A value already outside the range may only move in the
    // pressed direction — clamping it across itself is not a decrement.
    out = NumberStepValueTemp(Str{}, StepAction::Increment, 1, true, 10, false,
                              0);
    utassert(out && StrEqI(out, "10"));
    out = NumberStepValueTemp(Str{}, StepAction::Decrement, 1, true, 10, false,
                              0);
    utassert(out && StrEqI(out, "10"));
    utassert(!NumberStepValueTemp(StrL("5"), StepAction::Decrement, 1, true, 10,
                                  false, 0));
    utassert(!NumberStepValueTemp(StrL("1000"), StepAction::Increment, 1, false,
                                  0, true, 100));
    out = NumberStepValueTemp(StrL("1000"), StepAction::Decrement, 1, false, 0,
                              true, 100);
    utassert(out && StrEqI(out, "100"));
}

static void AStepThatDoesNotMoveIsNoStep() {
    // Already at the max: the clamp puts it back where it started.
    utassert(!NumberStepValueTemp(StrL("10"), StepAction::Increment, 1, false,
                                  0, true, 10));
    utassert(!NumberStepValueTemp(StrL("0"), StepAction::Decrement, 1, true, 0,
                                  false, 0));
    // A zero step moves nothing in either direction.
    utassert(!NumberStepValueTemp(StrL("3"), StepAction::Increment, 0, false, 0,
                                  false, 0));
}

static double BoundaryStep(double current, StepAction action, App*,
                           int64_t scale) {
    double fineStep = (double)scale / 100.0;
    if (action == StepAction::Increment) {
        return current < 1 ? fineStep : 0.5;
    }
    return current <= 1 ? fineStep : 0.5;
}

static void FixedAndValueDependentStrategiesMatchRustsEnum() {
    NumberStep fixed = NumberStep::Fixed(2.5);
    utassert(fixed.kind == NumberStepKind::Fixed);
    utassert(fixed.Value(100, StepAction::Decrement, nullptr) == 2.5);

    NumberStep dynamic = NumberStep::ByValue(&BoundaryStep, 10);
    utassert(dynamic.kind == NumberStepKind::ByValue);
    utassert(dynamic.Value(0.5, StepAction::Increment, nullptr) == 0.1);
    utassert(dynamic.Value(1.0, StepAction::Increment, nullptr) == 0.5);
    utassert(dynamic.Value(1.0, StepAction::Decrement, nullptr) == 0.1);
}

struct NumberEventSink {
    int count = 0;
    StepAction last = StepAction::Increment;

    static void OnStep(NumberEventSink* self, Ctx*,
                       const NumberInputEvent* ev) {
        self->count++;
        self->last = ev->action;
    }

    static El* Render(NumberEventSink*, Ctx* cx) { return Div(cx->a); }
};

static bool RejectFour(Str value, int64_t) {
    return !StrEqI(value, "4");
}

static void ApplyStepValidatesAndFallsBackToTheStepEvent() {
    App app;
    InputState state;
    InputSetValue(&state, StrL("3"));
    NumberStep one = NumberStep::Fixed(1);
    Entity<NumberEventSink> sink = EntityNew<NumberEventSink>(&app);
    Listener onStep = ListenTo(sink, &NumberEventSink::OnStep);

    state.validate = &RejectFour;
    utassert(NumberInputApplyStep(&state, &app, nullptr, StepAction::Increment,
                                  &one, false, 0, false, 0, false, onStep));
    utassert(StrEqI(InputValue(&state), "3"));
    utassert(sink.Get(&app)->count == 1);
    utassert(sink.Get(&app)->last == StepAction::Increment);

    state.validate = nullptr;
    utassert(NumberInputApplyStep(&state, &app, nullptr, StepAction::Decrement,
                                  &one, true, 0, false, 0, false, onStep));
    utassert(StrEqI(InputValue(&state), "2"));
    utassert(sink.Get(&app)->count == 1);
    utassert(state.maskPattern.kind == MaskKind::Number);
    utassert(!state.maskPatternSet);

    state.disabled = true;
    utassert(!NumberInputApplyStep(&state, &app, nullptr, StepAction::Increment,
                                   &one, false, 0, false, 0, false, onStep));
    utassert(StrEqI(InputValue(&state), "2"));
    EntityDropAll(&app);
}

static El* FindNamed(El* root, const char* name) {
    if (!root) {
        return nullptr;
    }
    if (root->id.s && base::StrEqI(root->id, name)) {
        return root;
    }
    for (El* c = root->first; c; c = c->next) {
        if (El* hit = FindNamed(c, name)) {
            return hit;
        }
    }
    return nullptr;
}

static void ThemedDefaultUsesOneSharedSemanticStep() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    win->frameArena = a;
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;

    InputState state;
    InputSetValue(&state, StrL("3.5"));
    El* root = component::NumberInput::New(&cx, StrL("quantity"), &state)
                   ->Step(0.5)
                   ->Max(4)
                   ->IntoEl();
    El* inc = FindNamed(root, "increment");
    El* dec = FindNamed(root, "decrement");
    El* semantic = root->first;
    utassert(inc && inc->onClick.IsValid());
    utassert(dec && dec->onClick.IsValid());
    utassert(semantic && semantic->accessibilityIncrementDirect.IsValid());
    utassert(semantic->accessibilityDecrementDirect.IsValid());

    inc->onClick.Call();
    utassert(StrEqI(InputValue(&state), "4.0"));
    utassert(state.focused && win->input == &state);
    // At the bound the operation is still the spinbutton's, but its text is
    // not rewritten to a differently formatted copy of the same value.
    semantic->accessibilityIncrementDirect.Call();
    utassert(StrEqI(InputValue(&state), "4.0"));
    dec->onClick.Call();
    utassert(StrEqI(InputValue(&state), "3.5"));

    IdsCollect(root);
    FocusCollect(win, root);
    int editorFocus = 0;
    for (int i = 0; i < win->focusEls.len; i++) {
        if (win->focusEls[i].accessibilityIncrementDirect.IsValid()) {
            editorFocus = win->focusEls[i].id;
            break;
        }
    }
    utassert(editorFocus != 0);
    WindowSetFocusId(win, editorFocus);
    WindowKeyDown(win, KeyUp, false, false, false, false);
    utassert(StrEqI(InputValue(&state), "4.0"));

    NumberInputText* text = NumberInputText::New(&cx);
    El* child = Div(a);
    El* region = text->Child(child)->IntoEl();
    utassert(region->style.minW == 0);
    utassert(region->style.flexGrow == 1);
    utassert(region->first == child);

    El* rightDec = Div(a);
    El* rightInput = Div(a);
    El* rightInc = Div(a);
    El* stacked =
        gpui::NumberInput::Compose(&cx, StrL("stacked"), &state, false,
                                   rightDec, rightInput, rightInc, true);
    utassert(stacked->first && stacked->first->first == rightInput);
    El* controls = stacked->first->next;
    utassert(controls && controls->first == rightInc);
    utassert(controls->first->next == rightDec);
    utassert(rightInc->style.flexGrow == 1 && rightDec->style.flexGrow == 1);

    InputBlur(&state, &app, win);
    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

// `InputBase::new(("number-input", state.entity_id()))` wrapping
// `Button::new("decrement")` and `Button::new("increment")`: the step buttons
// are named only among their siblings, and the frame is what tells one
// spinbutton's from another's. The port spelled the caller's id into each
// button instead; now the frame carries it and the fold does the rest.
static void TwoSpinbuttonsHaveTwoIncrements() {
    App app;
    Window* win = new Window();
    win->app = &app;
    Arena* a = ArenaNew();
    Ctx cx = {};
    cx.app = &app;
    cx.win = win;
    cx.a = a;

    InputState one;
    InputState two;
    El* page = Div(a);
    page->Child(component::NumberInput::New(&cx, StrL("first"), &one)
                    ->IntoEl());
    page->Child(component::NumberInput::New(&cx, StrL("second"), &two)
                    ->IntoEl());
    IdsCollect(page);

    El* incOne = FindNamed(page->first, "increment");
    El* incTwo = FindNamed(page->first->next, "increment");
    utassert(incOne && incTwo);
    utassert(incOne->clickId != 0 && incTwo->clickId != 0);
    utassert(incOne->clickId != incTwo->clickId);
    // And the two buttons of one frame are not each other.
    El* decOne = FindNamed(page->first, "decrement");
    utassert(decOne && decOne->clickId != incOne->clickId);

    ArenaDelete(a);
    delete win;
    EntityDropAll(&app);
}

void TestNumberInput() {
    TestSuite("number_input");
    TwoSpinbuttonsHaveTwoIncrements();
    AStepKeepsTheTextsPrecision();
    TextThatIsNotANumberStepsFromZero();
    TheRangeClampsAndWidens();
    AStepThatDoesNotMoveIsNoStep();
    FixedAndValueDependentStrategiesMatchRustsEnum();
    ApplyStepValidatesAndFallsBackToTheStepEvent();
    ThemedDefaultUsesOneSharedSemanticStep();
}
