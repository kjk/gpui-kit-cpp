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

// Hover snapshot for one atomic inline token. Hover never selects or edits;
// it reports pointer presence so the application can show a tooltip or run
// custom logic. The token's strings are only good for the length of the
// listener.
struct InlineTokenHoverEvent {
    InlineTokenSpan span = {};
    Bounds bounds = {};
    bool hovered = false;
    // The token range in UTF-16 code units, captured when the pointer
    // entered. Exits delivered after the text changed report these entry
    // coordinates, since the byte range can no longer be converted against
    // the current text.
    int rangeUtf16Start = 0;
    int rangeUtf16End = 0;

    const InlineToken& Token() const { return span.token; }
    // Whether the pointer entered (true) or left (false) the token.
    bool IsHovered() const { return hovered; }
};

// HoverSnapshot: the retained hover presence — the entered span, its placed
// bounds, and its UTF-16 range as of entry. JavaScript string offsets shift
// with later edits, so an exit delivered after a change cannot recompute
// them from the current text and reuses these instead. The span is owned.
struct InlineTokenHoverSnapshot {
    InlineTokenSpan span = {};
    Bounds bounds = {};
    int rangeUtf16Start = 0;
    int rangeUtf16End = 0;
};

// One placed token element: where the chip starting at `start` was laid out
// this frame.
struct InlineTokenPlaced {
    int start = 0;
    Bounds bounds = {};
};

typedef El* (*InlineTokenRenderer)(Ctx* cx, const InlineTokenContext* ctx,
                                   void* user);
typedef void (*InlineTokenClickListener)(const InlineTokenClickEvent* ev,
                                         Ctx* cx, void* user);
// A hover listener installed by a styled control.
typedef void (*InlineTokenHoverListener)(const InlineTokenHoverEvent* ev,
                                         Ctx* cx, void* user);

struct InlineTokenStore {
    Vec<InlineTokenSpan> spans;
    InlineToken pending = {};
    bool hasPending = false;
    InlineTokenRenderer renderer = nullptr;
    void* rendererUser = nullptr;
    InlineTokenClickListener click = nullptr;
    void* clickUser = nullptr;
    InlineTokenHoverListener hover = nullptr;
    void* hoverUser = nullptr;
    bool secret = false;
    bool replaying = false;
    bool validatedEdit = false;
    // token_bounds: the real per-frame bounds of the placed token elements;
    // used for hover payloads and stale-hover reconciliation. The array
    // holds its capacity for the whole frame, so an element can report its
    // bounds straight into its slot.
    InlineTokenPlaced* placed = nullptr;
    int nPlaced = 0;
    int capPlaced = 0;
    // hovered_token: the currently hovered token, retained so hover exit can
    // still be delivered when the token is removed, replaced, scrolled out,
    // or disabled.
    InlineTokenHoverSnapshot hovered = {};
    bool hasHovered = false;
    Vec<InlineTokenHoverSnapshot> pendingHoverExits;
    // Bumped whenever a retained hover is dropped without the pointer
    // leaving its element, so the element the pointer is still over is a
    // new one to the window's hover tracking and reports its entry again.
    uint32_t hoverEpoch = 0;
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
// install_token_hover_presentation: install a styled control's token hover
// listener without editing or notifying the document. Kept separate so
// InputSetTokenPresentation, which leaves it alone, keeps its signature.
void InputSetTokenHoverPresentation(InputState* s,
                                    InlineTokenHoverListener hover,
                                    void* hoverUser);
// token_hover: the token starting at `start`, as the hover event to hand the
// listener; false when there is nothing to report. Disabled tokens never
// enter; existing hover still receives its exit. Readonly tokens report
// hover. `expected`, when given, must be the token there.
//
// Records and clears the retained hover snapshot used for exit
// reconciliation. An exit only clears the snapshot when it belongs to the
// exiting token: a newly entered token is dispatched before the exit of the
// token the pointer left, so an older exit must not drop the newer token's
// snapshot. The event's strings are in the temp arena.
bool InputTokenHover(InputState* s, int start, Bounds bounds, bool hovered,
                     const InlineToken* expected, InlineTokenHoverEvent* out);
// reconcile_token_hover: deliver exits from entry snapshots, even after
// edits or callback reentry remove the token. Answers one exit at a time;
// call until it answers false.
bool InputReconcileTokenHover(InputState* s, InlineTokenHoverEvent* out);
// token_bounds, for the element that places the chips and for tests.
void InputTokenBoundsClear(InputState* s);
// The slot a chip starting at `start` reports its bounds into, or null when
// the frame's slots are used up.
Bounds* InputTokenBoundsSlot(InputState* s, int start);
bool InputTokenBoundsGet(const InputState* s, int start, Bounds* out);
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
