#ifndef GPUI_SHELL_INPUT_TOKENS_H_
#define GPUI_SHELL_INPUT_TOKENS_H_

// crates/shell/src/input_tokens.rs: the token data conversion and the
// retained-state operations a component adapter registers on its text
// states, plus the adapter from script token callbacks to the native
// `token` / `on_token_click` hooks of an Input or Textarea.
//
// Rust's registered state is an `Entity<InputState>` (or TextareaState); here
// it is a TextStateEntity the state store owns, holding the one InputState
// both text modes share (InputKind tells them apart) and the App its blink
// entity belongs to.

#include "base/input_tokens.h"
#include "shell/component_registry.h"

namespace gpui::shell {

// What an adapter's InputState / TextareaState factory builds, through
// StateBuild::New<TextStateEntity>().
struct TextStateEntity {
    App* app = nullptr;
    InputState input;

    TextStateEntity() = default;
    TextStateEntity(const TextStateEntity&) = delete;
    TextStateEntity& operator=(const TextStateEntity&) = delete;
    ~TextStateEntity();
};

// Starts a new text state: its focus handle, and for a textarea the
// multi-line mode. `placeholder` and `value` may be empty.
void TextStateInit(TextStateEntity* state, App* app, bool textarea,
                   Str placeholder, Str value);

// inline_token_context_data / inline_token_click_data: the plain JS values a
// renderer and a click listener receive. Ranges are UTF-16 offsets into
// `text`, the state's current value.
ComponentDataValue InlineTokenContextData(Arena* a,
                                          const InlineTokenContext& token,
                                          Str text);
ComponentDataValue InlineTokenClickData(Arena* a,
                                        const InlineTokenClickEvent& event,
                                        Str text);
// inline_token_hover_data: the plain JS hover event, with current token
// identity and presence. Entry coordinates come from the current text; an
// exit delivered after the text changed reuses the UTF-16 coordinates
// captured at entry.
ComponentDataValue InlineTokenHoverData(Arena* a,
                                        const InlineTokenHoverEvent& event,
                                        Str text);

// input_token_state_methods / textarea_token_state_methods: value, set_value,
// content, tokens, replace_with_token, replace_range_with_token,
// set_selected_range, replace. The state value is a TextStateEntity.
extern const StateMethodDescriptor kInputTokenStateMethods[8];
extern const StateMethodDescriptor kTextareaTokenStateMethods[8];

// InlineTokenCallbacks: script callbacks for one input's tokens, adapted to
// the native renderer and click listener. Frame-allocated; `apply` is the
// element's own Token / OnTokenClick, which the caller invokes with these.
// Hover uses the separate WithHover / HoverListener pair, so New keeps its
// signature.
struct InlineTokenCallbacks {
    ShellRuntime* runtime = nullptr;
    InputState* state = nullptr;
    ComponentCallback renderer = {};
    ComponentCallback listener = {};
    ComponentCallback hoverListener = {};

    static InlineTokenCallbacks* New(Ctx* cx, ShellRuntime* runtime,
                                     InputState* state,
                                     ComponentCallback renderer,
                                     ComponentCallback listener);
    // The native hooks, or null when the script gave no such callback.
    InlineTokenRenderer Renderer() const;
    InlineTokenClickListener Listener() const;
    // with_hover: bind a hover listener without changing New.
    InlineTokenCallbacks* WithHover(ComponentCallback hover);
    // apply_hover's listener, or null when the script gave none.
    InlineTokenHoverListener HoverListener() const;
};

} // namespace gpui::shell
#endif // GPUI_SHELL_INPUT_TOKENS_H_
