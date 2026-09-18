#ifndef GPUI_BASE_INPUT_TOKENS_H_
#define GPUI_BASE_INPUT_TOKENS_H_
/* Atomic inline tokens — crates/base/src/input/base/inline_tokens.rs
   and token_presentation.rs.

   A token is an annotation over a UTF-8 byte range of the input's text,
   not a document node. The value stays plain text; the token's id names
   the resource and may occur more than once. */

#include "gpui/gpui.h"

namespace gpui {

enum class InlineTokenError : uint8_t {
    Ok = 0,
    InvalidRange,
    InvalidBoundary,
    InvalidToken,
    OverlappingTokens,
    TextMismatch,
    UnsupportedMode,
    ValidationRejected,
    CompositionActive
};

const char* InlineTokenErrorMessage(InlineTokenError e);

struct InlineToken {
    Str id = {};
    Str text = {};
    Str label = {};

    static InlineToken New(Str id, Str text);
    InlineToken WithLabel(Str label) const;
    InlineTokenError Validate() const;
};

struct InlineTokenSpan {
    int start = 0;
    int end = 0;
    InlineToken token = {};
};

struct InputContent {
    Str text = {};
    Vec<InlineTokenSpan> tokens;

    static InputContent New(Str text);
    // Attach a token to [start, end). Empty or overlapping ranges, a
    // grapheme split, or a text mismatch fail without changing *this.
    InlineTokenError WithToken(int start, int end, InlineToken token);
};

struct InlineTokenContext {
    InlineTokenSpan span = {};
    bool selected = false;
    bool disabled = false;
    bool readonly = false;
    float lineHeight = 0;
    float availableWidth = 0;

    const InlineToken& Token() const { return span.token; }
};

struct InlineTokenClickEvent {
    InlineTokenSpan span = {};
    Bounds bounds = {};
    ClickEvent click = {};

    const InlineToken& Token() const { return span.token; }
};

typedef El* (*InlineTokenRenderer)(Ctx* cx, const InlineTokenContext* ctx,
                                   void* user);
typedef void (*InlineTokenClickListener)(const InlineTokenClickEvent* ev,
                                         void* user);

} // namespace gpui
#endif // GPUI_BASE_INPUT_TOKENS_H_
