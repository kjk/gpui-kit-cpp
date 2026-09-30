// crates/component-shell/src/shell/input_tokens.rs

#include "component_shell/input_tokens.h"
#include "shell/view.h"

namespace gpui::component_shell::input_tokens {

static bool Record(PayloadBuild* build, const ComponentArgument* args,
                   int count, Op::Kind kind, const char* message) {
    if (count != 1 || args[0].kind != shell::ComponentArgumentKind::Callback)
        return build->Fail(Str(message));
    Op* op = build->New<Op>();
    op->kind = kind;
    op->argument = args[0];
    return true;
}

bool RecordRender(PayloadBuild* build, const ComponentArgument* args,
                  int count) {
    return Record(build, args, count, Op::Render, "token expects a renderer");
}

bool RecordClick(PayloadBuild* build, const ComponentArgument* args,
                 int count) {
    return Record(build, args, count, Op::Click,
                  "on_token_click expects a listener");
}

bool RecordChange(PayloadBuild* build, const ComponentArgument* args,
                  int count) {
    return Record(build, args, count, Op::Change,
                  "on_change expects a listener");
}

bool Prepare(MaterializeRequest* request, InputState* state, uint64_t handle,
             Binding* out) {
    const ComponentArgument* renderer = nullptr;
    const ComponentArgument* listener = nullptr;
    const ComponentArgument* change = nullptr;
    EachMethod<Op>(request, [&](const Op& op) {
        switch (op.kind) {
            case Op::Render:
                renderer = &op.argument;
                break;
            case Op::Click:
                listener = &op.argument;
                break;
            case Op::Change:
                change = &op.argument;
                break;
        }
    });
    *out = {};
    out->state = state;
    out->handle = handle;
    shell::ComponentCallback render = {}, click = {};
    if (renderer) render = request->ResolveCallback(*renderer);
    if (listener) click = request->ResolveCallback(*listener);
    if (change) out->change = request->ResolveCallback(*change);
    if (len(request->failure)) return false;
    out->callbacks = shell::InlineTokenCallbacks::New(
        request->cx, request->runtime, state, render, click);
    return true;
}

void ApplyInput(const Binding& binding, component::Input* input) {
    if (!binding.callbacks || !input) return;
    if (InlineTokenRenderer render = binding.callbacks->Renderer())
        input->Token(render, binding.callbacks);
    if (InlineTokenClickListener click = binding.callbacks->Listener())
        input->OnTokenClick(click, binding.callbacks);
}

void ApplyTextarea(const Binding& binding, component::Textarea* textarea) {
    if (!binding.callbacks || !textarea) return;
    if (InlineTokenRenderer render = binding.callbacks->Renderer())
        textarea->Token(render, binding.callbacks);
    if (InlineTokenClickListener click = binding.callbacks->Listener())
        textarea->OnTokenClick(click, binding.callbacks);
}

// The subscription's handler: only a Change reaches the script.
static void RunChange(const shell::ComponentEventBinding* binding,
                      ScriptView* view, Ctx* cx, const void* event) {
    const InputEvent* input = (const InputEvent*)event;
    InputState* state = (InputState*)binding->user;
    if (!input || input->kind != InputEventKind::Change || !state) return;
    if (!binding->callback.IsSet()) return;
    shell::ComponentDataValue value =
        shell::ComponentDataValue::String(InputValue(state));
    binding->callback
        .InvokeAndReport(view->runtime, "input change callback failed", &value,
                         1, cx->win, cx->app);
}

El* Wrap(const Binding& binding, Ctx* cx, El* element) {
    // Rust keeps one subscription per state in a keyed host and swaps the
    // listener each render. Here the state's one change listener is that
    // subscription: a keyed relay holding this render's callback.
    if (binding.state) {
        TempStr key =
            fmt("shell-input-change-%llu", (unsigned long long)binding.handle);
        binding.state->onChange = shell::ComponentValueListener(
            cx, StrDup(cx->a, key), &RunChange, binding.change, binding.state);
    }
    return element;
}

} // namespace gpui::component_shell::input_tokens
