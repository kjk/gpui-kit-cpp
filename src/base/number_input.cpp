#include "base/number_input.h"

#include <math.h>
#include <stdlib.h>

namespace gpui {

NumberStep NumberStep::Fixed(double value) {
    NumberStep step;
    step.fixed = value;
    return step;
}

NumberStep NumberStep::ByValue(NumberStepByValueFn fn, intptr_t value) {
    NumberStep step;
    step.kind = NumberStepKind::ByValue;
    step.byValue = fn;
    step.arg = value;
    return step;
}

double NumberStep::Value(double current, StepAction action, App* app) const {
    if (kind == NumberStepKind::ByValue) {
        return byValue ? byValue(current, action, app, arg) : 0;
    }
    return fixed;
}

// The digits after the '.' in a number written out as text.
static int FractionDigits(Str s) {
    if (!s.s) {
        return 0;
    }
    for (int i = 0; i < len(s); i++) {
        if (s.s[i] == '.') {
            return len(s) - i - 1;
        }
    }
    return 0;
}

// Rust reads the fraction width off `step.to_string()` / `min.to_string()`,
// which print a float without trailing zeros. "%g" is the same shape, and it
// is only ever asked about the small, exact numbers a step and a bound are.
static int FractionDigitsOf(double v) {
    return FractionDigits(fmt("%g", v));
}

// The leading number in the text, or false if there is not one. Rust is
// `value.trim().parse::<f64>().ok()`, which refuses trailing junk, so a
// partial parse does not count.
bool NumberParseValue(Str value, double* out) {
    if (!value.s || len(value) <= 0) {
        return false;
    }
    TempStr buf = StrDupTemp(len(value) < 127 ? value : Str(value.s, 127));
    char* end = nullptr;
    double v = strtod(buf.s, &end);
    if (end == buf.s) {
        return false;
    }
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n' ||
           *end == '\f' || *end == '\v') {
        end++;
    }
    if (*end != 0) {
        return false;
    }
    *out = v;
    return true;
}

TempStr NumberStepValueTemp(Str value, StepAction action, double step,
                            bool hasMin, double min, bool hasMax, double max) {
    double current = 0;
    bool haveCurrent = NumberParseValue(value, &current);
    double next = action == StepAction::Increment
                      ? (haveCurrent ? current : 0) + step
                      : (haveCurrent ? current : 0) - step;
    int digits = FractionDigits(value);
    int stepDigits = FractionDigitsOf(step);
    if (stepDigits > digits) {
        digits = stepDigits;
    }
    if (hasMin && next < min) {
        next = min;
        int d = FractionDigitsOf(min);
        if (d > digits) {
            digits = d;
        }
    }
    if (hasMax && next > max) {
        next = max;
        int d = FractionDigitsOf(max);
        if (d > digits) {
            digits = d;
        }
    }
    // A step that did not move the value is not a step: at a bound the field
    // keeps what it had rather than being rewritten to the same number.
    if (haveCurrent) {
        bool moved =
            action == StepAction::Increment ? next > current : next < current;
        if (!moved) {
            return {};
        }
    }
    TempStr format = fmt("%%.%df", digits);
    return fmt(format.s, next);
}

bool NumberStepForKey(int key, StepAction* out) {
    if (key == KeyUp) {
        *out = StepAction::Increment;
        return true;
    }
    if (key == KeyDown) {
        *out = StepAction::Decrement;
        return true;
    }
    return false;
}

void NumberInputEnsureMask(InputState* state) {
    if (!state || state->maskPatternSet ||
        state->maskPattern.kind == MaskKind::Number) {
        return;
    }
    MaskPatternFree(&state->maskPattern);
    state->maskPattern = MaskPatternNumber(0);
}

static bool NumberInputCandidateValid(const InputState* state, Str value) {
    if (!state || len(value) == 0) {
        return true;
    }
    if (state->validate && !state->validate(value, state->validateArg)) {
        return false;
    }
    return MaskIsValid(state->maskPattern, value);
}

bool NumberInputApplyStep(InputState* state, App* app, Window* win,
                          StepAction action, const NumberStep* step,
                          bool hasMin, double min, bool hasMax, double max,
                          bool disabled, Listener onStep) {
    if (!state || disabled || state->disabled) {
        return false;
    }
    InputFocus(state, app, win);
    NumberInputEnsureMask(state);
    if (step) {
        Arena* temp = GetTempArena();
        Str value = InputUnmaskValue(temp, state);
        double current = 0;
        NumberParseValue(value, &current);
        double amount = step->Value(current, action, app);
        TempStr next = NumberStepValueTemp(value, action, amount, hasMin, min,
                                           hasMax, max);
        if (!next) {
            return false;
        }
        Str candidate = next;
        if (NumberInputCandidateValid(state, candidate)) {
            // replace_text_in_range_silent: a step is not typing and does not
            // emit the ordinary InputEvent::Change.
            InputSetValue(state, candidate);
            if (win) {
                AppInvalidate(win);
            }
            return true;
        }
    }
    if (onStep.IsValid()) {
        NumberInputEvent ev;
        ev.action = action;
        ListenerCall(app, win, onStep, &ev);
        return true;
    }
    return false;
}

struct NumberInputStepCall {
    App* app = nullptr;
    Window* win = nullptr;
    InputState* state = nullptr;
    NumberStep step = {};
    bool hasStep = false;
    bool hasMin = false;
    double min = 0;
    bool hasMax = false;
    double max = 0;
    bool disabled = false;
    Listener onStep = {};
    StepAction action = StepAction::Increment;
};

static void NumberInputRunStep(NumberInputStepCall* call) {
    NumberInputApplyStep(call->state, call->app, call->win, call->action,
                         call->hasStep ? &call->step : nullptr, call->hasMin,
                         call->min, call->hasMax, call->max, call->disabled,
                         call->onStep);
}

Func0 NumberInputStepCallback(Ctx* cx, InputState* state, StepAction action,
                              const NumberStep* step, bool hasMin, double min,
                              bool hasMax, double max, bool disabled,
                              Listener onStep) {
    NumberInputStepCall* call = ArenaNew<NumberInputStepCall>(cx->a);
    call->app = cx->app;
    call->win = cx->win;
    call->state = state;
    if (step) {
        call->step = *step;
        call->hasStep = true;
    }
    call->hasMin = hasMin;
    call->min = min;
    call->hasMax = hasMax;
    call->max = max;
    call->disabled = disabled;
    call->onStep = onStep;
    call->action = action;
    return MkFunc0(&NumberInputRunStep, call);
}

NumberInputText* NumberInputText::New(Ctx* cx) {
    NumberInputText* text = ArenaNew<NumberInputText>(cx->a);
    text->root = Div(cx->a)->MinW(0)->Flex1();
    return text;
}

NumberInputText* NumberInputText::Child(El* el) {
    root->Child(el);
    return this;
}

El* NumberInputText::IntoEl() {
    return root;
}

El* NumberInput::New(Ctx* cx, Str id) {
    Arena* a = cx->a;
    return Div(a)->Id(id)->Role(AccessibilityRole::SpinButton);
}

El* NumberInput::New(Ctx* cx, Str id, InputState* state) {
    NumberInputEnsureMask(state);
    return New(cx, id);
}

El* NumberInput::Compose(Ctx* cx, Str id, InputState* state, bool disabled,
                         El* decrement, El* input, El* increment,
                         bool controlsRight, El* children) {
    Arena* a = cx->a;
    El* root = New(cx, id, state)
                   ->AriaDisabled(disabled)
                   ->FlexRow()
                   ->ItemsCenter()
                   ->W(kFill)
                   ->H(kFill);
    NumberInputText* text = NumberInputText::New(cx)->Child(input);
    if (children) {
        text->Child(children);
    }
    if (controlsRight) {
        decrement->Flex1()->MinH(0);
        increment->Flex1()->MinH(0);
        root->Child(text->IntoEl())
            ->Child(Div(a)->FlexCol()->H(kFill)->Child(increment)->Child(
                decrement));
    } else {
        root->Child(decrement)->Child(text->IntoEl())->Child(increment);
    }
    return root;
}
} // namespace gpui
