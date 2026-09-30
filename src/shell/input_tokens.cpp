// crates/shell/src/input_tokens.rs

#include "shell/input_tokens.h"

#include <math.h>

namespace gpui::shell {

TextStateEntity::~TextStateEntity() {
    // Rust's entity drops its blink cursor with it; the blink here is an
    // entity of its own, dropped through the App it was made in.
    if (app && input.blink.IsValid()) EntityDrop(app, input.blink);
}

void TextStateInit(TextStateEntity* state, App* app, bool textarea,
                   Str placeholder, Str value) {
    state->app = app;
    InputState* input = &state->input;
    if (textarea) {
        input->kind = InputKind::Textarea;
        input->mode.kind = LayoutModeKind::PlainText;
    }
    input->focus = FocusHandleNew(app);
    if (len(placeholder)) InputSetPlaceholder(input, placeholder);
    if (len(value)) InputSetValue(input, value);
}

// ─── Data construction ─────────────────────────────────────────────────────

namespace input_tokens {

struct Field {
    const char* key;
    ComponentDataValue value;
};

static ComponentDataValue Object(Arena* a, const Field* fields, int count) {
    ComponentDataValue* items = (ComponentDataValue*)Alloc(
        a, (int)sizeof(ComponentDataValue) * (count ? count : 1));
    Str* keys = (Str*)Alloc(a, (int)sizeof(Str) * (count ? count : 1));
    for (int i = 0; i < count; i++) {
        items[i] = fields[i].value;
        keys[i] = Str(fields[i].key);
    }
    ComponentDataValue out;
    out.kind = DataKind::Object;
    out.items = items;
    out.keys = keys;
    out.count = count;
    return out;
}

static ComponentDataValue String(Arena* a, Str value) {
    return ComponentDataValue::String(StrDup(a, value));
}

static ComponentDataValue TokenData(Arena* a, const InlineToken& token) {
    Field fields[] = {{"id", String(a, token.id)},
                      {"text", String(a, token.text)},
                      {"label", String(a, token.label)}};
    return Object(a, fields, 3);
}

static ComponentDataValue RangeData(Arena* a, Str text, int start, int end) {
    Field fields[] = {
        {"start", ComponentDataValue::Number(
                      (double)RopeOffsetToOffsetUtf16(text, start))},
        {"end", ComponentDataValue::Number(
                    (double)RopeOffsetToOffsetUtf16(text, end))}};
    return Object(a, fields, 2);
}

static ComponentDataValue ContentData(Arena* a, const InputContent& content) {
    int count = len(content.tokens);
    ComponentDataValue* spans = (ComponentDataValue*)Alloc(
        a, (int)sizeof(ComponentDataValue) * (count ? count : 1));
    for (int i = 0; i < count; i++) {
        const InlineTokenSpan& span = content.tokens[i];
        Field fields[] = {
            {"range", RangeData(a, content.text, span.start, span.end)},
            {"token", TokenData(a, span.token)}};
        spans[i] = Object(a, fields, 2);
    }
    Field fields[] = {{"text", String(a, content.text)},
                      {"tokens", ComponentDataValue::Array(spans, count)}};
    return Object(a, fields, 2);
}

// ─── Decoding ──────────────────────────────────────────────────────────────

static bool Fail(StateCall* call, const char* message) {
    return call->Fail(Str(message));
}

static bool FailToken(StateCall* call, InlineTokenError error) {
    static const char* const codes[] = {
        "Ok",
        "InvalidRange",
        "InvalidBoundary",
        "InvalidToken",
        "OverlappingTokens",
        "TextMismatch",
        "UnsupportedMode",
        "ValidationRejected",
        "CompositionActive",
    };
    call->error = StrDup(call->a, Str(InlineTokenErrorMessage(error)));
    call->code = Str(codes[(int)error]);
    return false;
}

static const ComponentDataValue* FieldOf(StateCall* call,
                                         const ComponentDataValue& value,
                                         const char* name) {
    if (value.kind != DataKind::Object) {
        Fail(call, "expected a plain object");
        return nullptr;
    }
    const ComponentDataValue* found = value.Get(Str(name));
    if (!found) call->Fail(fmt("missing `%s`", Str(name)));
    return found;
}

static bool StringOf(StateCall* call, const ComponentDataValue* value,
                     Str* out) {
    if (!value) return false;
    if (value->kind != DataKind::String) return Fail(call, "expected a string");
    *out = value->string;
    return true;
}

// token(): a token, its strings in `call->a`; `label` defaults to the text.
static bool DecodeToken(StateCall* call, const ComponentDataValue& value,
                        InlineToken* out) {
    Str id, text;
    if (!StringOf(call, FieldOf(call, value, "id"), &id)) return false;
    if (!StringOf(call, FieldOf(call, value, "text"), &text)) return false;
    *out = InlineToken::New(id, text);
    // Rust's `if let Ok(label) = field(value, "label")`: an absent label is
    // no error.
    if (const ComponentDataValue* label = value.Get(StrL("label"))) {
        Str labelText;
        if (!StringOf(call, label, &labelText)) return false;
        *out = out->WithLabel(labelText);
    }
    return true;
}

// byte_offset: a UTF-16 offset into `text` as a byte offset.
static bool ByteOffset(StateCall* call, Str text,
                       const ComponentDataValue* value, int* out) {
    if (!value) return false;
    if (value->kind != DataKind::Number)
        return FailToken(call, InlineTokenError::InvalidRange);
    double offset = value->number;
    if (!isfinite(offset) || offset < 0 || offset != floor(offset) ||
        offset > (double)len(text)) {
        return FailToken(call, InlineTokenError::InvalidRange);
    }
    int target = (int)offset;
    int utf16 = 0;
    int ix = 0;
    while (ix < len(text)) {
        if (utf16 == target) {
            *out = ix;
            return true;
        }
        uint32_t c = 0;
        int width = Utf8At(text, ix, &c);
        if (width <= 0) return FailToken(call, InlineTokenError::InvalidRange);
        utf16 += c >= 0x10000 ? 2 : 1;
        if (utf16 > target)
            return FailToken(call, InlineTokenError::InvalidBoundary);
        ix += width;
    }
    if (utf16 == target) {
        *out = len(text);
        return true;
    }
    return FailToken(call, InlineTokenError::InvalidRange);
}

static bool DecodeRange(StateCall* call, Str text,
                        const ComponentDataValue& value, int* start, int* end) {
    const ComponentDataValue* from = FieldOf(call, value, "start");
    if (!from || !ByteOffset(call, text, from, start)) return false;
    const ComponentDataValue* to = FieldOf(call, value, "end");
    if (!to || !ByteOffset(call, text, to, end)) return false;
    if (*start > *end) return FailToken(call, InlineTokenError::InvalidRange);
    return true;
}

// decode_content. The content owns its strings; the caller frees it.
static bool DecodeContent(StateCall* call, const ComponentDataValue& value,
                          InputContent* out) {
    Str text;
    if (!StringOf(call, FieldOf(call, value, "text"), &text)) return false;
    const ComponentDataValue* tokens = FieldOf(call, value, "tokens");
    if (!tokens) return false;
    if (tokens->kind != DataKind::Array)
        return Fail(call, "content.tokens must be an array");
    *out = InputContent::New(StrDup(text));
    for (int i = 0; i < tokens->count; i++) {
        const ComponentDataValue& span = tokens->items[i];
        const ComponentDataValue* range = FieldOf(call, span, "range");
        int start = 0, end = 0;
        bool ok = range && DecodeRange(call, text, *range, &start, &end);
        const ComponentDataValue* tokenValue =
            ok ? FieldOf(call, span, "token") : nullptr;
        InlineToken token;
        ok = ok && tokenValue && DecodeToken(call, *tokenValue, &token);
        InlineTokenError error = InlineTokenError::Ok;
        if (ok) {
            // The content keeps the token it accepts.
            InlineToken owned = InlineTokenDup(token);
            error = out->WithToken(start, end, owned);
            if (error != InlineTokenError::Ok) InlineTokenFree(&owned);
        }
        if (!ok || error != InlineTokenError::Ok) {
            InputContentFree(out);
            return ok ? FailToken(call, error) : false;
        }
    }
    return true;
}

// ─── The state operations: state_binding! ──────────────────────────────────

static InputState* Input(void* state) {
    return &((TextStateEntity*)state)->input;
}

static bool Arity(StateCall* call, int count, int expected, const char* name) {
    if (count == expected) return true;
    call->Fail(
        fmt("invalid arguments for input state operation `%s`", Str(name)));
    return false;
}

// An operation changed the state: Rust's `entity.update` notifies, and the
// views that render it draw again.
static void Changed(StateCall* call) {
    if (call->window) AppInvalidate(call->window);
}

static bool Value(StateCall* call, void* state, const ComponentDataValue*,
                  int count) {
    if (!Arity(call, count, 0, "value")) return false;
    call->out = String(call->a, InputValue(Input(state)));
    return true;
}

static bool SetValue(StateCall* call, void* state,
                     const ComponentDataValue* args, int count) {
    if (!Arity(call, count, 1, "set_value")) return false;
    InputContent content;
    if (args[0].kind == DataKind::String) {
        content = InputContent::New(StrDup(args[0].string));
    } else if (!DecodeContent(call, args[0], &content)) {
        return false;
    }
    InputSetValue(Input(state), content);
    InputContentFree(&content);
    Changed(call);
    call->out = ComponentDataValue::Null();
    return true;
}

static bool Content(StateCall* call, void* state, const ComponentDataValue*,
                    int count) {
    if (!Arity(call, count, 0, "content")) return false;
    InputContent content = InputGetContent(Input(state));
    call->out = ContentData(call->a, content);
    InputContentFree(&content);
    return true;
}

static bool Tokens(StateCall* call, void* state, const ComponentDataValue*,
                   int count) {
    if (!Arity(call, count, 0, "tokens")) return false;
    InputContent content = InputGetContent(Input(state));
    ComponentDataValue data = ContentData(call->a, content);
    InputContentFree(&content);
    const ComponentDataValue* tokens = data.Get(StrL("tokens"));
    call->out = tokens ? *tokens : ComponentDataValue::Array(nullptr, 0);
    return true;
}

static bool ReplaceWithToken(StateCall* call, void* state,
                             const ComponentDataValue* args, int count) {
    if (!Arity(call, count, 1, "replace_with_token")) return false;
    InlineToken token;
    if (!DecodeToken(call, args[0], &token)) return false;
    InlineTokenError error =
        InputReplaceWithToken(Input(state), call->app, call->window, token);
    if (error != InlineTokenError::Ok) return FailToken(call, error);
    Changed(call);
    call->out = ComponentDataValue::Null();
    return true;
}

static bool ReplaceRangeWithToken(StateCall* call, void* state,
                                  const ComponentDataValue* args, int count) {
    if (!Arity(call, count, 2, "replace_range_with_token")) return false;
    int start = 0, end = 0;
    if (!DecodeRange(call, InputValue(Input(state)), args[0], &start, &end))
        return false;
    InlineToken token;
    if (!DecodeToken(call, args[1], &token)) return false;
    InlineTokenError error = InputReplaceRangeWithToken(
        Input(state), call->app, call->window, start, end, token);
    if (error != InlineTokenError::Ok) return FailToken(call, error);
    Changed(call);
    call->out = ComponentDataValue::Null();
    return true;
}

static bool SetSelectedRange(StateCall* call, void* state,
                             const ComponentDataValue* args, int count) {
    if (!Arity(call, count, 1, "set_selected_range")) return false;
    int start = 0, end = 0;
    if (!DecodeRange(call, InputValue(Input(state)), args[0], &start, &end))
        return false;
    InputSetSelectedRange(Input(state), call->app, call->window, start, end);
    Changed(call);
    call->out = ComponentDataValue::Null();
    return true;
}

static bool Replace(StateCall* call, void* state,
                    const ComponentDataValue* args, int count) {
    if (!Arity(call, count, 1, "replace")) return false;
    Str text;
    if (!StringOf(call, &args[0], &text)) return false;
    InputReplaceTextInRange(Input(state), call->app, call->window, nullptr,
                            text);
    Changed(call);
    call->out = ComponentDataValue::Null();
    return true;
}

} // namespace input_tokens

// METHODS, in its order.
#define GPUI_INPUT_TOKEN_STATE_METHODS                                        \
    {                                                                         \
        {"value", "(): string", true, &input_tokens::Value},                  \
            {"set_value", "(value: string | InputContent): void", false,      \
             &input_tokens::SetValue},                                        \
            {"content", "(): InputContent", true, &input_tokens::Content},    \
            {"tokens", "(): InlineTokenSpan[]", true, &input_tokens::Tokens}, \
            {"replace_with_token", "(token: InlineToken): void", false,       \
             &input_tokens::ReplaceWithToken},                                \
            {"replace_range_with_token",                                      \
             "(range: InputRange, token: InlineToken): void", false,          \
             &input_tokens::ReplaceRangeWithToken},                           \
            {"set_selected_range", "(range: InputRange): void", false,        \
             &input_tokens::SetSelectedRange},                                \
        {                                                                     \
            "replace", "(text: string): void", false, &input_tokens::Replace  \
        }                                                                     \
    }

const StateMethodDescriptor kInputTokenStateMethods[8] =
    GPUI_INPUT_TOKEN_STATE_METHODS;
const StateMethodDescriptor kTextareaTokenStateMethods[8] =
    GPUI_INPUT_TOKEN_STATE_METHODS;

#undef GPUI_INPUT_TOKEN_STATE_METHODS

// ─── Payloads for the callbacks ────────────────────────────────────────────

ComponentDataValue InlineTokenContextData(Arena* a,
                                          const InlineTokenContext& token,
                                          Str text) {
    using namespace input_tokens;
    Field fields[] = {
        {"token", TokenData(a, token.Token())},
        {"range", RangeData(a, text, token.span.start, token.span.end)},
        {"selected", ComponentDataValue::Boolean(token.selected)},
        {"disabled", ComponentDataValue::Boolean(token.disabled)},
        {"readonly", ComponentDataValue::Boolean(token.readonly)},
        {"line_height", ComponentDataValue::Number(token.lineHeight)},
        {"available_width", ComponentDataValue::Number(token.availableWidth)},
    };
    return Object(a, fields, 7);
}

ComponentDataValue InlineTokenClickData(Arena* a,
                                        const InlineTokenClickEvent& event,
                                        Str text) {
    using namespace input_tokens;
    const Modifiers& modifiers = event.click.modifiers;
    Field bounds[] = {
        {"x", ComponentDataValue::Number(event.bounds.x)},
        {"y", ComponentDataValue::Number(event.bounds.y)},
        {"width", ComponentDataValue::Number(event.bounds.w)},
        {"height", ComponentDataValue::Number(event.bounds.h)},
    };
    Field keys[] = {
        {"shift", ComponentDataValue::Boolean(modifiers.shift)},
        {"alt", ComponentDataValue::Boolean(modifiers.alt)},
        {"control", ComponentDataValue::Boolean(modifiers.control)},
        {"platform", ComponentDataValue::Boolean(modifiers.platform)},
    };
    Field fields[] = {
        {"token", TokenData(a, event.Token())},
        {"range", RangeData(a, text, event.span.start, event.span.end)},
        {"bounds", Object(a, bounds, 4)},
        {"modifiers", Object(a, keys, 4)},
    };
    return Object(a, fields, 4);
}

// ─── InlineTokenCallbacks ──────────────────────────────────────────────────

static El* RenderInlineToken(Ctx* cx, const InlineTokenContext* token,
                             void* user) {
    const InlineTokenCallbacks* self = (const InlineTokenCallbacks*)user;
    ComponentDataValue data =
        InlineTokenContextData(cx->a, *token, InputValue(self->state));
    Str error;
    El* element = self->renderer.BuildInteractiveWith(self->runtime, &data, 1,
                                                      cx, &error);
    if (element) return element;
    if (len(error)) logf("inline token renderer failed: %s\n", error);
    return Div(cx->a)->Child(TextEl(cx->a, token->Token().label));
}

static void ClickInlineToken(const InlineTokenClickEvent* event, Ctx* cx,
                             void* user) {
    const InlineTokenCallbacks* self = (const InlineTokenCallbacks*)user;
    ComponentDataValue data =
        InlineTokenClickData(cx->a, *event, InputValue(self->state));
    Str error;
    if (!self->listener.InvokeWith(self->runtime, &data, 1, cx->win, cx->app,
                                   nullptr, &error, cx->a)) {
        logf("inline token activation failed: %s\n", error);
    }
}

InlineTokenCallbacks* InlineTokenCallbacks::New(Ctx* cx, ShellRuntime* runtime,
                                                InputState* state,
                                                ComponentCallback renderer,
                                                ComponentCallback listener) {
    InlineTokenCallbacks* callbacks = ArenaNew<InlineTokenCallbacks>(cx->a);
    callbacks->runtime = runtime;
    callbacks->state = state;
    callbacks->renderer = renderer;
    callbacks->listener = listener;
    return callbacks;
}

InlineTokenRenderer InlineTokenCallbacks::Renderer() const {
    return renderer.IsSet() ? &RenderInlineToken : nullptr;
}

InlineTokenClickListener InlineTokenCallbacks::Listener() const {
    return listener.IsSet() ? &ClickInlineToken : nullptr;
}

} // namespace gpui::shell
