// crates/component-shell/src/shell/retained_forms/mod.rs
//
// Honest retained-state adapters for form controls whose state has a
// concrete, delegate-free construction API.
//
// Change callbacks are deliberately not exposed here yet (upstream's reason:
// the materializer has no subscription owner whose lifetime can retain one),
// except the shared Input token and change methods. Delegate-backed Select
// and Combobox are likewise deferred.

#include "component_shell/families.h"
#include "component_shell/input_tokens.h"
#include "component_shell/retained_forms/mod.h"
#include "ui/color_picker.h"
#include "ui/slider.h"
#include "ui/time.h"

#include <math.h>

namespace gpui::component_shell::retained_forms {

// FormOp.
struct FormOp {
    enum Kind : uint8_t {
        Disabled,
        Placeholder,
        AriaLabel,
        Groups,
        Vertical,
        Reverse,
        Label,
        AccessibilityLabel,
        Months,
    } kind = Disabled;
    bool flag = false;
    Str text;
    uint64_t count = 0;
};

bool PositiveUsize(const ComponentArgument* args, int count,
                   const char* callable, uint64_t* out, Str* error) {
    // 2^64: `2_f64.powi(usize::BITS as i32)`.
    const double limit = 18446744073709551616.0;
    if (count == 1 && args[0].kind == shell::ComponentArgumentKind::Number) {
        double value = args[0].number;
        if (isfinite(value) && value >= 1.0 && value == floor(value) &&
            value < limit) {
            *out = (uint64_t)value;
            if ((double)*out == value) return true;
        }
    }
    *error = fmt("%s expects a positive integer", Str(callable));
    return false;
}

bool EnsureLeaf(int childrenLen, const char* component, Str* error) {
    if (childrenLen == 0) return true;
    *error = fmt("%s does not accept children", Str(component));
    return false;
}

// A positive count as the int the C++ components hold, saturating: Rust's
// usize has room for any count a script can write, the components here do
// not.
static int SaturatedCount(uint64_t value) {
    return value > (uint64_t)INT32_MAX ? INT32_MAX : (int)value;
}

// ─── Recorders ─────────────────────────────────────────────────────────────

static bool BoolOp(PayloadBuild* build, const ComponentArgument* args,
                   int count, Str callable, FormOp::Kind kind) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Boolean)
        return build->Fail(fmt("%s expects one boolean", callable));
    FormOp* op = build->New<FormOp>();
    op->kind = kind;
    op->flag = args[0].boolean;
    return true;
}

static bool StringOp(PayloadBuild* build, const ComponentArgument* args,
                     int count, Str callable, FormOp::Kind kind) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::String)
        return build->Fail(fmt("%s expects one string", callable));
    FormOp* op = build->New<FormOp>();
    op->kind = kind;
    op->text = args[0].string;
    return true;
}

// disabled_method(owner): recorded as FormOp::Disabled. `disabled` is one of
// the registered common behaviors, which the engine routes into the request
// rather than recording, in Rust as here; the materializers still fold the
// op the way Rust's do.
template <const char* Owner>
static bool RecordDisabled(PayloadBuild* build, const ComponentArgument* args,
                           int count) {
    return BoolOp(build, args, count, fmt("%s.disabled", Str(Owner)),
                  FormOp::Disabled);
}

template <const char* Owner>
static bool RecordPlaceholder(PayloadBuild* build,
                              const ComponentArgument* args, int count) {
    return StringOp(build, args, count, fmt("%s.placeholder", Str(Owner)),
                    FormOp::Placeholder);
}

template <const char* Owner>
static bool RecordAriaLabel(PayloadBuild* build, const ComponentArgument* args,
                            int count) {
    return StringOp(build, args, count, fmt("%s.aria_label", Str(Owner)),
                    FormOp::AriaLabel);
}

static bool RecordGroups(PayloadBuild* build, const ComponentArgument* args,
                         int count) {
    uint64_t value = 0;
    Str error;
    if (!PositiveUsize(args, count, "OtpInput.groups", &value, &error))
        return build->Fail(error);
    FormOp* op = build->New<FormOp>();
    op->kind = FormOp::Groups;
    op->count = value;
    return true;
}

static bool RecordVertical(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<FormOp>()->kind = FormOp::Vertical;
    return true;
}

static bool RecordReverse(PayloadBuild* build, const ComponentArgument*, int) {
    build->New<FormOp>()->kind = FormOp::Reverse;
    return true;
}

static bool RecordLabel(PayloadBuild* build, const ComponentArgument* args,
                        int count) {
    return StringOp(build, args, count, StrL("ColorPicker.label"),
                    FormOp::Label);
}

static bool RecordAccessibilityLabel(PayloadBuild* build,
                                     const ComponentArgument* args, int count) {
    return StringOp(build, args, count, StrL("ColorPicker.accessibility_label"),
                    FormOp::AccessibilityLabel);
}

static bool RecordMonths(PayloadBuild* build, const ComponentArgument* args,
                         int count) {
    uint64_t value = 0;
    Str error;
    if (!PositiveUsize(args, count, "Calendar.number_of_months", &value,
                       &error))
        return build->Fail(error);
    FormOp* op = build->New<FormOp>();
    op->kind = FormOp::Months;
    op->count = value;
    return true;
}

// state_payload: the state argument itself.
static bool StatePayload(PayloadBuild* build, const ComponentArgument* args,
                         int) {
    *build->New<ComponentArgument>() = args[0];
    return true;
}

// ─── State factories ───────────────────────────────────────────────────────

// A Ctx for the constructors that take one, over the build's window and app.
static Ctx BuildCtx(shell::StateBuild* build) {
    Ctx cx;
    cx.app = build->app;
    cx.win = build->window;
    cx.a = build->a;
    return cx;
}

static bool OptionalText(shell::StateBuild* build, const ComponentArgument& arg,
                         const char* name, Str* out) {
    *out = {};
    if (arg.kind != shell::ComponentArgumentKind::Optional)
        return build
            ->Fail(fmt("InputState %s must be optional text", Str(name)));
    const ComponentArgument* value = arg.Some();
    if (!value) return true;
    if (value->kind != shell::ComponentArgumentKind::String)
        return build->Fail(fmt("InputState %s expects text", Str(name)));
    *out = value->string;
    return true;
}

static bool NewInputState(shell::StateBuild* build,
                          const ComponentArgument* args, int count) {
    if (count != 2)
        return build->Fail(
            StrL("InputState expects optional placeholder and initial value"));
    Str placeholder, value;
    if (!OptionalText(build, args[0], "placeholder", &placeholder))
        return false;
    if (!OptionalText(build, args[1], "initial_value", &value)) return false;
    shell::TextStateInit(build->New<shell::TextStateEntity>(), build->app,
                         false, placeholder, value);
    return true;
}

static bool NewCalendarState(shell::StateBuild* build, const ComponentArgument*,
                             int) {
    Ctx cx = BuildCtx(build);
    EntityState<CalendarState>* state = build
                                            ->New<EntityState<CalendarState>>();
    state->app = build->app;
    state->entity = CalendarStateNew(&cx);
    return true;
}

static bool NewOtpState(shell::StateBuild* build, const ComponentArgument* args,
                        int count) {
    uint64_t length = 0;
    Str error;
    if (!PositiveUsize(args, count, "OtpState", &length, &error))
        return build->Fail(error);
    // Rust's OtpState::new takes any length (gpui-shell's own OtpState.new
    // keeps to 64; this one does not). The cells are counted in an int, and
    // no window lays out more of them than that.
    EntityState<OtpState>* state = build->New<EntityState<OtpState>>();
    state->app = build->app;
    state->entity = EntityNewState<OtpState>(build->app);
    OtpState* otp = state->entity.Get(build->app);
    if (!otp) return build->Fail(StrL("OtpState could not be created"));
    otp->self = state->entity;
    otp->length = length > (uint64_t)INT32_MAX ? INT32_MAX : (int)length;
    otp->focus = FocusHandleNew(build->app);
    return true;
}

static bool NewSliderState(shell::StateBuild* build,
                           const ComponentArgument* args, int count) {
    float value = 0;
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Optional)
        return build
            ->Fail(StrL("SliderState expects an optional initial value"));
    if (const ComponentArgument* initial = args[0].Some()) {
        double number = initial->number;
        if (initial->kind != shell::ComponentArgumentKind::Number ||
            !isfinite(number) || number < 0.0 || number > 100.0) {
            return build
                ->Fail(StrL("SliderState initial_value expects 0 through 100"));
        }
        value = (float)number;
    }
    *build->New<SliderState>() = SliderStateNew(0, 100, SliderSingle(value));
    return true;
}

static bool NewColorPickerState(shell::StateBuild* build,
                                const ComponentArgument*, int) {
    Ctx cx = BuildCtx(build);
    EntityState<ColorPickerState>* state =
        build->New<EntityState<ColorPickerState>>();
    state->app = build->app;
    state->entity = ColorPickerStateNew(&cx);
    return true;
}

static bool NewDatePickerState(shell::StateBuild* build,
                               const ComponentArgument*, int) {
    Ctx cx = BuildCtx(build);
    EntityState<component::DatePickerState>* state =
        build->New<EntityState<component::DatePickerState>>();
    state->app = build->app;
    state->entity = component::DatePickerStateNew(&cx);
    return true;
}

static bool NewTimeFieldState(shell::StateBuild* build,
                              const ComponentArgument*, int) {
    Ctx cx = BuildCtx(build);
    EntityState<TimeFieldState>* state =
        build->New<EntityState<TimeFieldState>>();
    state->app = build->app;
    state->entity = TimeFieldStateNew(&cx);
    return true;
}

// ─── Materializers ─────────────────────────────────────────────────────────

// state_entity!: the payload's state argument, as `T` of `kind`.
template <class T>
static T* StateEntity(MaterializeRequest* request, const char* kind,
                      uint64_t* handle = nullptr) {
    const ComponentArgument* argument = request->PayloadAs<ComponentArgument>();
    if (!argument) {
        request->Fail(StrL("retained form received an incompatible payload"));
        return nullptr;
    }
    if (handle) {
        const ComponentArgument* value = argument->Some();
        *handle = value ? value->handle : 0;
    }
    return request->StateAs<T>(*argument, kind);
}

// finish_leaf: the children check under the component's Rust type name, and
// the style onto the element.
static El* FinishLeaf(MaterializeRequest* request, El* element,
                      const char* typeName) {
    Str error;
    if (!EnsureLeaf(request->ChildrenLen(), typeName, &error))
        return request->Fail(error);
    return request->ApplyStyle(element);
}

// finish_unstyled_leaf: a wrapper div takes the style.
static El* FinishUnstyledLeaf(MaterializeRequest* request, El* element) {
    Str error;
    if (!EnsureLeaf(request->ChildrenLen(), "OtpInput", &error))
        return request->Fail(error);
    El* wrapper = Div(request->cx->a)->Child(element);
    return request->ApplyStyle(wrapper);
}

static Str StateElementId(Ctx* cx, const char* kind, uint64_t handle) {
    return StrDup(cx->a, fmt("gpui-component-%s-%llu", Str(kind),
                             (unsigned long long)handle));
}

static El* MaterializeInput(MaterializeRequest* request) {
    uint64_t handle = 0;
    shell::TextStateEntity* state =
        StateEntity<shell::TextStateEntity>(request, "InputState", &handle);
    if (!state) return nullptr;
    input_tokens::Binding binding;
    if (!input_tokens::Prepare(request, &state->input, handle, &binding))
        return nullptr;
    Ctx* cx = request->cx;
    component::Input* input = component::Input::New(
        cx, StateElementId(cx, "input", handle), &state->input);
    input_tokens::ApplyInput(binding, input);
    EachMethod<FormOp>(request, [&](const FormOp& op) {
        if (op.kind == FormOp::Disabled) input->Disabled(op.flag);
        if (op.kind == FormOp::AriaLabel) input->AriaLabel(op.text);
    });
    El* element = FinishLeaf(request, input->IntoEl(),
                             "gpui_component::input::input::Input");
    if (!element) return nullptr;
    return input_tokens::Wrap(binding, cx, element);
}

static El* MaterializeNumberInput(MaterializeRequest* request) {
    uint64_t handle = 0;
    shell::TextStateEntity* state =
        StateEntity<shell::TextStateEntity>(request, "InputState", &handle);
    if (!state) return nullptr;
    Ctx* cx = request->cx;
    component::NumberInput* input = component::NumberInput::New(
        cx, StateElementId(cx, "number-input", handle), &state->input);
    EachMethod<FormOp>(request, [&](const FormOp& op) {
        if (op.kind == FormOp::Disabled) input->Disabled(op.flag);
        // NumberInput::placeholder is kept by Rust's builder and never
        // rendered; the C++ component has no such field to keep it in.
    });
    return FinishLeaf(request, input->IntoEl(),
                      "gpui_component::input::number_input::NumberInput");
}

static El* MaterializeOtpInput(MaterializeRequest* request) {
    uint64_t handle = 0;
    EntityState<OtpState>* state =
        StateEntity<EntityState<OtpState>>(request, "OtpState", &handle);
    if (!state) return nullptr;
    Ctx* cx = request->cx;
    component::OtpInput* input = component::OtpInput::New(
        cx, StateElementId(cx, "otp-input", handle), state->entity);
    EachMethod<FormOp>(request, [&](const FormOp& op) {
        if (op.kind == FormOp::Disabled) input->Disabled(op.flag);
        if (op.kind == FormOp::Groups) input->Groups(SaturatedCount(op.count));
    });
    return FinishUnstyledLeaf(request, input->IntoEl());
}

static El* MaterializeSlider(MaterializeRequest* request) {
    uint64_t handle = 0;
    SliderState* state =
        StateEntity<SliderState>(request, "SliderState", &handle);
    if (!state) return nullptr;
    Ctx* cx = request->cx;
    component::Slider* slider =
        component::Slider::New(cx, StateElementId(cx, "slider", handle), state);
    EachMethod<FormOp>(request, [&](const FormOp& op) {
        if (op.kind == FormOp::Disabled) slider->Disabled(op.flag);
        if (op.kind == FormOp::Vertical) slider->Vertical();
        if (op.kind == FormOp::Reverse) slider->Reverse();
    });
    return FinishLeaf(request, slider->IntoEl(),
                      "gpui_component::slider::Slider");
}

static El* MaterializeColorPicker(MaterializeRequest* request) {
    EntityState<ColorPickerState>* state =
        StateEntity<EntityState<ColorPickerState>>(request, "ColorPickerState");
    if (!state) return nullptr;
    component::ColorPicker* picker =
        component::ColorPicker::New(request->cx, state->entity);
    EachMethod<FormOp>(request, [&](const FormOp& op) {
        if (op.kind == FormOp::Label) picker->Label(op.text);
        if (op.kind == FormOp::AccessibilityLabel)
            picker->AccessibilityLabel(op.text);
    });
    return FinishLeaf(request, picker->IntoEl(),
                      "gpui_component::color_picker::ColorPicker");
}

static El* MaterializeDatePicker(MaterializeRequest* request) {
    EntityState<component::DatePickerState>* state =
        StateEntity<EntityState<component::DatePickerState>>(request,
                                                             "DatePickerState");
    if (!state) return nullptr;
    component::DatePicker* picker =
        component::DatePicker::New(request->cx, state->entity);
    EachMethod<FormOp>(request, [&](const FormOp& op) {
        if (op.kind == FormOp::Disabled) picker->Disabled(op.flag);
        if (op.kind == FormOp::Placeholder) picker->Placeholder(op.text);
    });
    return FinishLeaf(request, picker->IntoEl(),
                      "gpui_component::time::date_picker::DatePicker");
}

static El* MaterializeTimeField(MaterializeRequest* request) {
    EntityState<TimeFieldState>* state =
        StateEntity<EntityState<TimeFieldState>>(request, "TimeFieldState");
    if (!state) return nullptr;
    component::TimeField* field =
        component::TimeField::New(request->cx, state->entity);
    EachMethod<FormOp>(request, [&](const FormOp& op) {
        if (op.kind == FormOp::Disabled) field->Disabled(op.flag);
    });
    return FinishLeaf(request, field->IntoEl(),
                      "gpui_component::time::time_field::TimeField");
}

static El* MaterializeCalendar(MaterializeRequest* request) {
    EntityState<CalendarState>* state =
        StateEntity<EntityState<CalendarState>>(request, "CalendarState");
    if (!state) return nullptr;
    component::Calendar* calendar =
        component::Calendar::New(request->cx, state->entity);
    EachMethod<FormOp>(request, [&](const FormOp& op) {
        if (op.kind == FormOp::Months)
            calendar->NumberOfMonths(SaturatedCount(op.count));
    });
    return FinishLeaf(request, calendar->IntoEl(),
                      "gpui_component::time::calendar::Calendar");
}

// ─── Descriptors ───────────────────────────────────────────────────────────

static constexpr char kInput[] = "Input";
static constexpr char kNumberInput[] = "NumberInput";
static constexpr char kOtpInput[] = "OtpInput";
static constexpr char kSlider[] = "Slider";
static constexpr char kDatePicker[] = "DatePicker";
static constexpr char kTimeField[] = "TimeField";

static constexpr ArgumentSchema kOptionalString = SchemaString();
static constexpr ArgumentSchema kOptionalNumber = SchemaNumber();

static constexpr ArgumentDescriptor kInputStateArgs[] = {
    {"placeholder", SchemaOptional(&kOptionalString)},
    {"initial_value", SchemaOptional(&kOptionalString)}};
static constexpr ArgumentDescriptor kOtpStateArgs[] = {
    {"length", SchemaNumber()}};
static constexpr ArgumentDescriptor kSliderStateArgs[] = {
    {"initial_value", SchemaOptional(&kOptionalNumber)}};

static constexpr shell::StateDescriptor kStates[] = {
    {"InputState", "InputState", kInputStateArgs,
     "Retained editable text state shared by Input and NumberInput, with "
     "optional placeholder and initial value.",
     &NewInputState, shell::kInputTokenStateMethods},
    {"CalendarState",
     "CalendarState",
     {},
     "Retained calendar navigation and selection state.",
     &NewCalendarState,
     {}},
    {"OtpState",
     "OtpState",
     kOtpStateArgs,
     "Retained fixed-length one-time-password editing state.",
     &NewOtpState,
     {}},
    {"SliderState",
     "SliderState",
     kSliderStateArgs,
     "Retained single-value slider state with an optional initial value.",
     &NewSliderState,
     {}},
    {"ColorPickerState",
     "ColorPickerState",
     {},
     "Retained color selection and preview state.",
     &NewColorPickerState,
     {}},
    {"DatePickerState",
     "DatePickerState",
     {},
     "Retained single-date picker and calendar state.",
     &NewDatePickerState,
     {}},
    {"TimeFieldState",
     "TimeFieldState",
     {},
     "Retained 24-hour, minute-precision time-of-day editing state.",
     &NewTimeFieldState,
     {}},
};

static constexpr ArgumentDescriptor kDisabledArgs[] = {
    {"disabled", SchemaBoolean()}};
static constexpr ArgumentDescriptor kPlaceholderArgs[] = {
    {"placeholder", SchemaString()}};
static constexpr ArgumentDescriptor kAriaLabelArgs[] = {
    {"label", SchemaString()}};
static constexpr ArgumentDescriptor kGroupsArgs[] = {
    {"groups", SchemaNumber()}};
static constexpr ArgumentDescriptor kLabelArgs[] = {{"label", SchemaString()}};
static constexpr ArgumentDescriptor kCountArgs[] = {{"count", SchemaNumber()}};

static constexpr const char* kDisabledDoc =
    "Controls whether the form control accepts interaction.";
static constexpr const char* kPlaceholderDoc =
    "Sets the empty-value prompt shown by the control.";

// state_constructor(export, kind).
static constexpr ArgumentDescriptor kInputStateEntity[] = {
    {"state", SchemaEntity("InputState")}};
static constexpr ArgumentDescriptor kOtpStateEntity[] = {
    {"state", SchemaEntity("OtpState")}};
static constexpr ArgumentDescriptor kSliderStateEntity[] = {
    {"state", SchemaEntity("SliderState")}};
static constexpr ArgumentDescriptor kColorPickerStateEntity[] = {
    {"state", SchemaEntity("ColorPickerState")}};
static constexpr ArgumentDescriptor kCalendarStateEntity[] = {
    {"state", SchemaEntity("CalendarState")}};
static constexpr ArgumentDescriptor kDatePickerStateEntity[] = {
    {"state", SchemaEntity("DatePickerState")}};
static constexpr ArgumentDescriptor kTimeFieldStateEntity[] = {
    {"state", SchemaEntity("TimeFieldState")}};

static constexpr ConstructorDescriptor kInputConstructors[] = {
    {"Input", kInputStateEntity, &StatePayload}};
static constexpr ConstructorDescriptor kNumberInputConstructors[] = {
    {"NumberInput", kInputStateEntity, &StatePayload}};
static constexpr ConstructorDescriptor kOtpInputConstructors[] = {
    {"OtpInput", kOtpStateEntity, &StatePayload}};
static constexpr ConstructorDescriptor kSliderConstructors[] = {
    {"Slider", kSliderStateEntity, &StatePayload}};
static constexpr ConstructorDescriptor kColorPickerConstructors[] = {
    {"ColorPicker", kColorPickerStateEntity, &StatePayload}};
static constexpr ConstructorDescriptor kCalendarConstructors[] = {
    {"Calendar", kCalendarStateEntity, &StatePayload}};
static constexpr ConstructorDescriptor kDatePickerConstructors[] = {
    {"DatePicker", kDatePickerStateEntity, &StatePayload}};
static constexpr ConstructorDescriptor kTimeFieldConstructors[] = {
    {"TimeField", kTimeFieldStateEntity, &StatePayload}};

static constexpr MethodDescriptor kInputMethods[] = {
    {"aria_label", kAriaLabelArgs,
     "Sets the name announced by accessibility clients.",
     &RecordAriaLabel<kInput>},
    {"disabled", kDisabledArgs, kDisabledDoc, &RecordDisabled<kInput>},
    input_tokens::kTokenMethod,
    input_tokens::kTokenClickMethod,
    input_tokens::kTokenHoverMethod,
    input_tokens::kChangeMethod,
};
static constexpr MethodDescriptor kNumberInputMethods[] = {
    {"placeholder", kPlaceholderArgs, kPlaceholderDoc,
     &RecordPlaceholder<kNumberInput>},
    {"disabled", kDisabledArgs, kDisabledDoc, &RecordDisabled<kNumberInput>},
};
static constexpr MethodDescriptor kOtpInputMethods[] = {
    {"groups", kGroupsArgs,
     "Splits the fixed-length code into the requested number of visual "
     "groups.",
     &RecordGroups},
    {"disabled", kDisabledArgs, kDisabledDoc, &RecordDisabled<kOtpInput>},
};
static constexpr MethodDescriptor kSliderMethods[] = {
    {"vertical",
     {},
     "Uses a vertical track instead of the default horizontal track.",
     &RecordVertical},
    {"reverse",
     {},
     "Reverses the filled side for a single-value slider.",
     &RecordReverse},
    {"disabled", kDisabledArgs, kDisabledDoc, &RecordDisabled<kSlider>},
};
static constexpr MethodDescriptor kColorPickerMethods[] = {
    {"label", kLabelArgs, "Sets the visible label above the picker.",
     &RecordLabel},
    {"accessibility_label", kLabelArgs,
     "Sets the announced name independently of the visible label.",
     &RecordAccessibilityLabel},
};
static constexpr MethodDescriptor kCalendarMethods[] = {
    {"number_of_months", kCountArgs,
     "Sets the positive number of adjacent months to display.", &RecordMonths},
};
static constexpr MethodDescriptor kDatePickerMethods[] = {
    {"placeholder", kPlaceholderArgs, kPlaceholderDoc,
     &RecordPlaceholder<kDatePicker>},
    {"disabled", kDisabledArgs, kDisabledDoc, &RecordDisabled<kDatePicker>},
};
static constexpr MethodDescriptor kTimeFieldMethods[] = {
    {"disabled", kDisabledArgs, kDisabledDoc, &RecordDisabled<kTimeField>},
};

static constexpr ComponentDescriptor kComponents[] = {
    {"Input", kInputConstructors, kInputMethods,
     "A retained single-line text field.", &MaterializeInput},
    {"NumberInput", kNumberInputConstructors, kNumberInputMethods,
     "A retained numeric text field with increment and decrement controls.",
     &MaterializeNumberInput},
    {"OtpInput", kOtpInputConstructors, kOtpInputMethods,
     "A retained fixed-length one-time-password field. Shell styles apply to "
     "its dedicated wrapper because OtpInput itself is not Styled; ordinary "
     "children are rejected.",
     &MaterializeOtpInput},
    {"Slider", kSliderConstructors, kSliderMethods,
     "A retained numeric slider using SliderState defaults.",
     &MaterializeSlider},
    {"ColorPicker", kColorPickerConstructors, kColorPickerMethods,
     "A retained color picker with preview and commit behavior.",
     &MaterializeColorPicker},
    {"Calendar", kCalendarConstructors, kCalendarMethods,
     "A retained calendar for date navigation and selection.",
     &MaterializeCalendar},
    {"DatePicker", kDatePickerConstructors, kDatePickerMethods,
     "A retained single-date picker backed by an internal calendar.",
     &MaterializeDatePicker},
    {"TimeField", kTimeFieldConstructors, kTimeFieldMethods,
     "A retained segmented time-of-day field edited from the keyboard.",
     &MaterializeTimeField},
};

} // namespace gpui::component_shell::retained_forms

namespace gpui::component_shell {

// Registers retained form states and their delegate-free controls.
bool RegisterRetainedForms(shell::ComponentRegistry* registry,
                           shell::RegistryError* error) {
    for (const shell::StateDescriptor& state : retained_forms::kStates) {
        if (!registry->RegisterState(&state, error)) return false;
    }
    for (const ComponentDescriptor& descriptor : retained_forms::kComponents) {
        if (!registry->Register(&descriptor, error)) return false;
    }
    return true;
}

} // namespace gpui::component_shell
