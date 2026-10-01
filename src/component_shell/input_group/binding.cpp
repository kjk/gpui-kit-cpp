// crates/component-shell/src/shell/input_group/binding.rs

#include "component_shell/input_group/mod.h"
#include "component_shell/input_tokens.h"
#include "shell/view.h"

#include <limits.h>
#include <math.h>

namespace gpui::component_shell::input_group::binding {

bool Rows(const ComponentArgument& argument, int* out, Str* error) {
    // Rust takes any whole count below usize::MAX; LayoutMode holds an int
    // here, so a larger count saturates to the largest row count it can.
    double value = argument.number;
    if (argument.kind != shell::ComponentArgumentKind::Number ||
        !isfinite(value) || value < 1 || value != floor(value) ||
        value >= 18446744073709551615.0) {
        *error = StrL("textarea rows must be a positive integer");
        return false;
    }
    *out = value > (double)INT_MAX ? INT_MAX : (int)value;
    return true;
}

bool RecordRows(PayloadBuild* build, const ComponentArgument* args, int count) {
    if (count != 1)
        return build->Fail(StrL("rows expects one positive integer"));
    int rows = 0;
    Str error;
    if (!Rows(args[0], &rows, &error)) return build->Fail(error);
    Op* op = build->New<Op>();
    op->kind = Op::Layout;
    op->layout.rows = rows;
    return true;
}

bool RecordAutoGrow(PayloadBuild* build, const ComponentArgument* args,
                    int count) {
    if (count != 2)
        return build
            ->Fail(StrL("auto_grow expects positive minimum and maximum rows"));
    int min = 0, max = 0;
    Str error;
    if (!Rows(args[0], &min, &error) || !Rows(args[1], &max, &error))
        return build->Fail(error);
    if (max < min)
        return build->Fail(StrL("auto_grow requires max_rows >= min_rows"));
    Op* op = build->New<Op>();
    op->kind = Op::Layout;
    op->layout.autoGrow = true;
    op->layout.minRows = min;
    op->layout.maxRows = max;
    return true;
}

bool Prepare(MaterializeRequest* request, InputState* state, uint64_t handle,
             bool textarea, Binding* out) {
    *out = {};
    out->state = state;
    out->handle = handle;
    out->elementId = request->elementId;
    out->textarea = textarea;
    const ComponentArgument* change = nullptr;
    EachMethod<Op>(request, [&](const Op& op) {
        switch (op.kind) {
            case Op::OnChange:
                change = &op.argument;
                break;
            case Op::Value:
                out->hasValue = true;
                out->value = op.text;
                break;
            case Op::Placeholder:
                out->hasPlaceholder = true;
                out->placeholder = op.text;
                break;
            case Op::Layout:
                out->hasLayout = true;
                out->layout = op.layout;
                break;
            case Op::Masked:
                out->hasMasked = true;
                out->masked = op.flag;
                break;
            default:
                break;
        }
    });
    if (change) out->change = request->ResolveCallback(*change);
    return len(request->failure) == 0;
}

// Host: what Rust's keyed state keeps per text state beyond the
// subscription — the layout last applied, so an unchanged one is not
// reapplied every render.
struct Host {
    bool hasLayout = false;
    TextareaLayout layout = {};
};

static void RunChange(const shell::ComponentEventBinding* binding,
                      ScriptView* view, Ctx* cx, const void* event) {
    const InputEvent* input = (const InputEvent*)event;
    InputState* state = (InputState*)binding->user;
    if (!input || input->kind != InputEventKind::Change || !state) return;
    if (!binding->callback.IsSet()) return;
    shell::ComponentDataValue value =
        shell::ComponentDataValue::String(InputValue(state));
    binding->callback
        .InvokeAndReport(view->runtime, "InputGroup text callback failed",
                         &value, 1, cx->win, cx->app);
}

void Apply(const Binding& binding, Ctx* cx) {
    InputState* state = binding.state;
    if (!state) return;
    // window.use_keyed_state(("shell-input-group", state.id())): the host
    // holding the subscription, whose callback this render replaces. The
    // keyed state is the element's path as well, so the relay is named by
    // the node too and two controls on one state are two subscribers.
    Str key = StrDup(cx->a, fmt("shell-input-group-%llu",
                                (unsigned long long)binding.handle));
    input_tokens::SubscribeChange(
        cx, state, binding.handle,
        StrDup(cx->a, fmt("%s-%s", key, binding.elementId)), &RunChange,
        binding.change);
    Host* host = ElementStateEntity<Host>(cx, key, StrL("shell-input-group"))
                     .Get(cx);

    // set_value is silent. Reassert only a different controlled value so
    // ordinary renders preserve selection and undo history.
    if (binding.hasValue && !StrEq(InputValue(state), binding.value))
        InputSetValue(state, binding.value);
    if (binding.hasPlaceholder &&
        !StrEq(state->placeholder, binding.placeholder)) {
        InputSetPlaceholder(state, binding.placeholder);
    }
    if (!binding.textarea && binding.hasMasked &&
        state->masked != binding.masked) {
        state->masked = binding.masked;
    }
    if (binding.textarea && binding.hasLayout && host &&
        (!host->hasLayout || host->layout != binding.layout)) {
        if (binding.layout.autoGrow) {
            TextareaSetAutoGrow(state, binding.layout.minRows,
                                binding.layout.maxRows);
        } else {
            TextareaSetRows(state, binding.layout.rows);
        }
        host->hasLayout = true;
        host->layout = binding.layout;
    }
}

} // namespace gpui::component_shell::input_group::binding
