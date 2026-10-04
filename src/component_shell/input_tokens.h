#ifndef GPUI_COMPONENT_SHELL_INPUT_TOKENS_H_
#define GPUI_COMPONENT_SHELL_INPUT_TOKENS_H_

// crates/component-shell/src/shell/input_tokens.rs: the inline presentation
// and change subscriptions Input and Textarea share — the `token`,
// `on_token_click` and `on_change` methods, and what they bind.

#include "component_shell/support.h"
#include "shell/input_tokens.h"
#include "ui/input.h"

namespace gpui::component_shell::input_tokens {

struct Op {
    enum Kind : uint8_t {
        Render,
        Click,
        Hover,
        Change,
    } kind = Render;
    ComponentArgument argument = {};
};

bool RecordRender(PayloadBuild* build, const ComponentArgument* args, int);
bool RecordClick(PayloadBuild* build, const ComponentArgument* args, int);
bool RecordHover(PayloadBuild* build, const ComponentArgument* args, int);
bool RecordChange(PayloadBuild* build, const ComponentArgument* args, int);

inline constexpr ArgumentDescriptor kRenderArguments[] = {
    {"render", SchemaCallback("(token: InlineTokenContext, cx: Context) => "
                              "Element | null")}};
inline constexpr ArgumentDescriptor kClickArguments[] = {
    {"listener",
     SchemaCallback("(event: InlineTokenClickEvent, cx: Context) => void")}};
inline constexpr ArgumentDescriptor kHoverArguments[] = {
    {"listener",
     SchemaCallback("(event: InlineTokenHoverEvent, cx: Context) => void")}};
inline constexpr ArgumentDescriptor kChangeArguments[] = {
    {"listener", SchemaCallback("(text: string, cx: Context) => void")}};

// methods(include_change): these three, then kChangeMethod when included.
inline constexpr MethodDescriptor kTokenMethod = {
    "token", kRenderArguments,
    "Renders an atomic token from its current UTF-16 range and read-only "
    "context.",
    &RecordRender};
inline constexpr MethodDescriptor kTokenClickMethod = {
    "on_token_click", kClickArguments,
    "Activates a reference after a completed unconsumed click, outside the "
    "editing borrow.",
    &RecordClick};
inline constexpr MethodDescriptor kTokenHoverMethod = {
    "on_token_hover", kHoverArguments,
    "Reports pointer presence over a token; hover never selects or edits.",
    &RecordHover};
inline constexpr MethodDescriptor kChangeMethod = {
    "on_change", kChangeArguments,
    "Reports user text or token identity changes; explicit draft restoration "
    "remains silent.",
    &RecordChange};

// What `prepare` resolved for one input: its token callbacks and its change
// listener.
struct Binding {
    InputState* state = nullptr;
    // The retained state's handle, which names the subscription the way
    // Rust's entity id does.
    uint64_t handle = 0;
    // The node's place in the description: with the handle, what keys the
    // change host, as Rust's keyed state is the element's path and the state.
    Str elementId;
    shell::InlineTokenCallbacks* callbacks = nullptr;
    shell::ComponentCallback change = {};
};

// A change host for `state`: what window.subscribe on the state is in Rust,
// held in a keyed host named by the element's path and the state. `key`
// names the host's relay (unique among the hosts this frame renders for the
// state), `run` and `callback` are what it carries. The state's one
// change listener hands a change to every host rendered this frame, in the
// order they rendered, so two components on one state both hear it.
void SubscribeChange(Ctx* cx, InputState* state, uint64_t handle, Str key,
                     shell::ComponentEventRun run,
                     shell::ComponentCallback callback);

// prepare: the last token renderer, click listener and change listener
// recorded on the request's node. False after request->Fail.
bool Prepare(MaterializeRequest* request, InputState* state, uint64_t handle,
             Binding* out);
// Binding::input / Binding::textarea: installs the token callbacks through
// the element's own Token / OnTokenClick.
void ApplyInput(const Binding& binding, component::Input* input);
void ApplyTextarea(const Binding& binding, component::Textarea* textarea);
// Binding::wrap: `element`, with the state's change subscription retained
// for the current render's listener.
El* Wrap(const Binding& binding, Ctx* cx, El* element);

} // namespace gpui::component_shell::input_tokens
#endif // GPUI_COMPONENT_SHELL_INPUT_TOKENS_H_
