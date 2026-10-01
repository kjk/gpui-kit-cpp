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
    out->elementId = request->elementId;
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

// The state's one change listener, standing for every subscription a host
// rendered this frame holds on it. Rust's host is keyed by the element path
// as well as the state, so two components rendering one state are two
// hosts and two subscriptions, and a change reaches both, in the order they
// subscribed; the relays here are those hosts, and this hands the event to
// each of them.
struct InputChangeFanout {
    // win->frameSeq of the frame that last rebuilt the list: the first host
    // to render in a new frame starts it over, so a component no longer
    // rendered is no longer told.
    uint64_t frame = 0;
    static constexpr int kCap = 16;
    Listener relays[kCap];
    int count = 0;

    static void OnChange(InputChangeFanout* self, Ctx* cx,
                         const InputEvent* event) {
        // The relays are copied first: a script reacting to the change may
        // render, which rebuilds the list under the loop.
        Listener relays[kCap];
        int count = self ? self->count : 0;
        for (int i = 0; i < count; i++) relays[i] = self->relays[i];
        for (int i = 0; i < count; i++) {
            ListenerCall(cx->app, cx->win, relays[i], event);
        }
    }
};

void SubscribeChange(Ctx* cx, InputState* state, uint64_t handle, Str key,
                     shell::ComponentEventRun run,
                     shell::ComponentCallback callback) {
    if (!state) return;
    Listener relay =
        shell::ComponentValueListener(cx, key, run, callback, state);
    if (!relay.IsValid()) return;
    uint64_t frame = cx->win ? cx->win->frameSeq : 0;
    // The fanout already on the state, when this frame put it there.
    InputChangeFanout* fanout = nullptr;
    Listener current = state->onChange;
    if (current.IsValid() &&
        current.Fn() == (uintptr_t)&InputChangeFanout::OnChange) {
        Entity<InputChangeFanout> owner;
        owner.id = current.view;
        fanout = owner.Get(cx);
        if (fanout && fanout->frame != frame) fanout = nullptr;
    }
    if (!fanout) {
        TempStr fanKey =
            fmt("shell-input-fanout-%llu", (unsigned long long)handle);
        Entity<InputChangeFanout> owner = ElementStateEntity<InputChangeFanout>(
            cx, StrDup(cx->a, fanKey), StrL("shell::InputChangeFanout"));
        fanout = owner.Get(cx);
        if (!fanout) {
            state->onChange = relay;
            return;
        }
        fanout->frame = frame;
        fanout->count = 0;
        state->onChange = ListenTo(owner, &InputChangeFanout::OnChange);
    }
    bool known = false;
    for (int i = 0; i < fanout->count; i++) {
        known = known || (fanout->relays[i].view == relay.view &&
                          fanout->relays[i].fn == relay.fn);
    }
    if (!known && fanout->count < InputChangeFanout::kCap) {
        fanout->relays[fanout->count++] = relay;
    }
}

El* Wrap(const Binding& binding, Ctx* cx, El* element) {
    // Rust keeps a host per element path and state, holding a subscription
    // whose listener each render swaps: here a keyed relay holding this
    // render's callback, named by the state and the node.
    if (!binding.state) return element;
    TempStr key = fmt("shell-input-change-%llu-%s",
                      (unsigned long long)binding.handle, binding.elementId);
    SubscribeChange(cx, binding.state, binding.handle, StrDup(cx->a, key),
                    &RunChange, binding.change);
    return element;
}

} // namespace gpui::component_shell::input_tokens
