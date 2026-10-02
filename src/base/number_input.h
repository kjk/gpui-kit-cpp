#ifndef GPUI_BASE_NUMBER_INPUT_H_
#define GPUI_BASE_NUMBER_INPUT_H_
/* Unstyled number input — crates/base/src/number_input.rs */

#include "gpui/gpui.h"

namespace gpui {

// Rust's StepAction: which way a step goes.
enum class StepAction : uint8_t {
    Decrement,
    Increment
};

// Rust's NumberStep is a data-carrying enum. A tagged POD is its C++ shape:
// either one fixed amount, or a retained function that can choose an amount
// from the current value and direction (a logarithmic or boundary-sensitive
// editor is the reason the second variant exists).
enum class NumberStepKind : uint8_t {
    Fixed,
    ByValue
};

using NumberStepByValueFn = double (*)(double current, StepAction action,
                                       App* app, int64_t arg);

struct NumberStep {
    NumberStepKind kind = NumberStepKind::Fixed;
    double fixed = 1;
    NumberStepByValueFn byValue = nullptr;
    int64_t arg = 0;

    static NumberStep Fixed(double value);
    static NumberStep ByValue(NumberStepByValueFn fn, int64_t arg = 0);
    double Value(double current, StepAction action, App* app) const;
};

// The one EventEmitter value from number_input.rs. C++ represents the Rust
// `Step(StepAction)` variant as its payload record rather than an enum with
// hidden storage.
enum class NumberInputEventKind : uint8_t {
    Step
};

struct NumberInputEvent {
    NumberInputEventKind kind = NumberInputEventKind::Step;
    StepAction action = StepAction::Increment;
};

// step_value: step a numeric *string*, preserving the decimal precision the
// text already had and clamping into the range.
//
// Working on the text rather than a double is the point. "1.50" stepped by
// one is "2.50", not "2.5", because the field is what the reader is looking
// at; the digit count is the widest of the value's, the step's, and — where a
// bound is what the result landed on — that bound's.
//
// Answers an empty string where Rust answers None: nothing to write, because
// the text is not a number the step can move or because the clamp put the
// result back where it started. `hasMin` / `hasMax` are Rust's Option.
TempStr NumberStepValueTemp(Str value, StepAction action, double step,
                            bool hasMin, double min, bool hasMax, double max);
// The same strict parser NumberInput uses before it steps, exposed so the
// themed wrapper can project its numeric accessibility value.
bool NumberParseValue(Str value, double* out);

// The key table: crates/base/src/number_input.rs binds up to Increment and
// down to Decrement in the "NumberInput" context. Answers false for anything
// else, which is a key the field itself should keep.
bool NumberStepForKey(int key, StepAction* out);

// InputState::apply_number_step. The default themed control focuses the
// editor, applies `step` silently when one is supplied, and sends onStep only
// when stepping is caller-controlled or the candidate fails validation.
// A disabled control consumes nothing and never focuses. `hasMin`/`hasMax`
// are Rust's Option<f64>.
bool NumberInputApplyStep(InputState* state, App* app, Window* win,
                          StepAction action, const NumberStep* step,
                          bool hasMin, double min, bool hasMax, double max,
                          bool disabled, Listener onStep = {});

// A frame-local closure over the same operation. This is what the built-in
// buttons, accessibility actions and inherited arrow-key behavior share.
Func0 NumberInputStepCallback(Ctx* cx, InputState* state, StepAction action,
                              const NumberStep* step, bool hasMin, double min,
                              bool hasMax, double max, bool disabled,
                              Listener onStep = {});

// ensure_number_mask: a NumberInput supplies the ungrouped numeric mask only
// until the caller explicitly selected a mask of its own.
void NumberInputEnsureMask(InputState* state);

// The fixed text region in NumberInput's three-part composition.
struct NumberInputText {
    El* root = nullptr;

    static NumberInputText* New(Ctx* cx);
    NumberInputText* Child(El* el);
    El* IntoEl();
};

// The frame around the editor and its two step buttons. Identity only; the
// caller owns the parts, and the step buttons are Buttons that decline focus
// so a press on one leaves the editor focused. `InputBase::new(("number-
// input", state.entity_id()))`: the frame names itself so that "decrement"
// and "increment" under it are one spinbutton's and not another's — the id
// the caller passes is what stands in for the state's entity id here.
struct NumberInput {
    static El* New(Ctx* cx, Str id);
    static El* New(Ctx* cx, Str id, InputState* state);
    // NumberInput::render: the semantic root owns the fixed composition while
    // callers decorate the three supplied parts. With controlsRight both
    // buttons stack increment-over-decrement beside the text region.
    static El* Compose(Ctx* cx, Str id, InputState* state, bool disabled,
                       El* decrement, El* input, El* increment,
                       bool controlsRight = false, El* children = nullptr);
};
} // namespace gpui
#endif // GPUI_BASE_NUMBER_INPUT_H_
