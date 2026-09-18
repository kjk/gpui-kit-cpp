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

InlineToken InlineTokenDup(InlineToken t);
void InlineTokenFree(InlineToken* t);
InlineTokenSpan InlineTokenSpanDup(InlineTokenSpan s);
void InlineTokenSpanFree(InlineTokenSpan* s);
bool InlineTokenEq(InlineToken a, InlineToken b);

// Only affected records are retained in history. Ranges are relative to
// the edit start.
struct TokenDelta {
    Vec<InlineTokenSpan> removed;
    Vec<InlineTokenSpan> inserted;
};

void TokenDeltaFree(TokenDelta* d);
TokenDelta* TokenDeltaDup(const TokenDelta* d);

void InlineTokenSpansClear(Vec<InlineTokenSpan>* spans);

// Replace [start, end) with newLen bytes. inserted ranges are relative to
// start. Returns a delta when any token was added or removed.
TokenDelta* TokenStoreReplace(Vec<InlineTokenSpan>* spans, int start, int end,
                              int newLen, const InlineTokenSpan* inserted,
                              int nInserted);

int TokenBoundary(const Vec<InlineTokenSpan>& spans, int offset, Bias bias);
void NormalizeTokenRange(const Vec<InlineTokenSpan>& spans, int* start,
                         int* end);

struct InputContent {
    Str text = {};
    Vec<InlineTokenSpan> tokens;

    static InputContent New(Str text);
    // Attach a token to [start, end). Empty or overlapping ranges, a
    // grapheme split, or a text mismatch fail without changing *this.
    InlineTokenError WithToken(int start, int end, InlineToken token);
};

InputContent InputContentDup(const InputContent& content);
void InputContentFree(InputContent* content);
InputContent InputGetContent(const InputState* s);

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
                                         Ctx* cx, void* user);

struct InlineTokenStore {
    Vec<InlineTokenSpan> spans;
    InlineToken pending = {};
    bool hasPending = false;
    InlineTokenRenderer renderer = nullptr;
    void* rendererUser = nullptr;
    InlineTokenClickListener click = nullptr;
    void* clickUser = nullptr;
    bool secret = false;
    bool replaying = false;
    bool validatedEdit = false;
};

void InlineTokenStoreFree(InlineTokenStore* store);
InlineTokenStore* InputTokenStore(InputState* s, bool create);
const InlineTokenStore* InputTokenStore(const InputState* s);
bool InputTokensVisible(const InputState* s);
const Vec<InlineTokenSpan>* InputTokens(const InputState* s);
int InputTokenBoundary(const InputState* s, int offset, Bias bias);
void InputNormalizeTokenRange(const InputState* s, int* start, int* end);
void InputSetTokenPresentation(InputState* s, InlineTokenRenderer renderer,
                               void* rendererUser,
                               InlineTokenClickListener click, void* clickUser,
                               bool secret);
void InputSetValue(InputState* s, const InputContent& content);
InlineTokenError InputReplaceRangeWithToken(InputState* s, App* app,
                                            Window* win, int start, int end,
                                            InlineToken token);
InlineTokenError InputReplaceWithToken(InputState* s, App* app, Window* win,
                                       InlineToken token);
int InputPreviousStartOfWordAt(const InputState* s, int offset);
int InputNextEndOfWordAt(const InputState* s, int offset);

} // namespace gpui
#endif // GPUI_BASE_INPUT_TOKENS_H_
