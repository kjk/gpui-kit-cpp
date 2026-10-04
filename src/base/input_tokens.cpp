#include "base/input_tokens.h"
#include "base/text_boundary.h"

namespace gpui {

const char* InlineTokenErrorMessage(InlineTokenError e) {
    switch (e) {
        case InlineTokenError::InvalidRange:
            return "token range is outside the document or empty";
        case InlineTokenError::InvalidBoundary:
            return "token range splits a Unicode grapheme";
        case InlineTokenError::InvalidToken:
            return "token requires a nonempty ID, text and label without "
                   "control characters";
        case InlineTokenError::OverlappingTokens:
            return "token ranges overlap";
        case InlineTokenError::TextMismatch:
            return "token text does not match its range or input "
                   "normalization";
        case InlineTokenError::UnsupportedMode:
            return "tokens are not supported by this input mode";
        case InlineTokenError::ValidationRejected:
            return "input validation rejected the content";
        case InlineTokenError::CompositionActive:
            return "finish the active IME composition before editing tokens";
        default:
            return "";
    }
}

static bool HasControlOrLineSep(Str s) {
    int i = 0;
    while (i < len(s)) {
        uint32_t c = 0;
        int n = Utf8At(s, i, &c);
        if (n <= 0) {
            return true;
        }
        if (c < 0x20 || c == 0x7f || c == 0x2028 || c == 0x2029) {
            return true;
        }
        i += n;
    }
    return false;
}

static bool IsBlank(Str s) {
    int i = 0;
    while (i < len(s)) {
        uint32_t c = 0;
        int n = Utf8At(s, i, &c);
        if (n <= 0) {
            return true;
        }
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            return false;
        }
        i += n;
    }
    return true;
}

InlineToken InlineToken::New(Str id, Str text) {
    InlineToken t;
    t.id = id;
    t.text = text;
    t.label = text;
    return t;
}

InlineToken InlineToken::WithLabel(Str value) const {
    InlineToken t = *this;
    t.label = value;
    return t;
}

InlineTokenError InlineToken::Validate() const {
    if (IsBlank(id) || !len(text) || !len(label) || HasControlOrLineSep(text) ||
        HasControlOrLineSep(label)) {
        return InlineTokenError::InvalidToken;
    }
    return InlineTokenError::Ok;
}

// Grapheme_Extend, the part of it text actually carries: the combining
// diacritical blocks, the variation selectors and ZWJ/ZWNJ. A character of
// these is never the start of a grapheme cluster (UAX #29 GB9).
static bool IsGraphemeExtend(uint32_t c) {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x0483 && c <= 0x0489) ||
           (c >= 0x0591 && c <= 0x05BD) || (c >= 0x0610 && c <= 0x061A) ||
           (c >= 0x064B && c <= 0x065F) || (c >= 0x1AB0 && c <= 0x1AFF) ||
           (c >= 0x1DC0 && c <= 0x1DFF) || c == 0x200C || c == 0x200D ||
           (c >= 0x20D0 && c <= 0x20FF) || (c >= 0x302A && c <= 0x302F) ||
           (c >= 0x3099 && c <= 0x309A) || (c >= 0xFE00 && c <= 0xFE0F) ||
           (c >= 0xFE20 && c <= 0xFE2F) || (c >= 0x1F3FB && c <= 0x1F3FF) ||
           (c >= 0xE0020 && c <= 0xE007F) || (c >= 0xE0100 && c <= 0xE01EF);
}

// validate_range's boundary: `text.grapheme_indices(true)` names `off`.
// Rust segments with unicode-segmentation; there is no segmenter in this
// tree, so this is the subset of UAX #29 a token edge can meet in practice:
// a character boundary that is not inside CR LF (GB3), not before an extend
// character (GB9) and not after a ZWJ (GB11, the emoji joiner).
static bool IsCharBoundary(Str text, int off) {
    if (off < 0 || off > len(text)) {
        return false;
    }
    if (off == 0 || off == len(text)) {
        return true;
    }
    if (Utf8ClipLeft(text, off) != off) {
        return false;
    }
    if (text.s[off - 1] == '\r' && text.s[off] == '\n') {
        return false;
    }
    uint32_t next = 0;
    Utf8At(text, off, &next);
    if (IsGraphemeExtend(next)) {
        return false;
    }
    uint32_t prev = 0;
    Utf8At(text, Utf8Prev(text, off), &prev);
    return prev != 0x200D;
}

bool TextIsGraphemeBoundary(Str text, int off) {
    return IsCharBoundary(text, off);
}

InputContent InputContent::New(Str text) {
    InputContent c;
    c.text = text;
    return c;
}

InputContent InputContentDup(const InputContent& content) {
    InputContent out;
    out.text = StrDup(content.text);
    for (int i = 0; i < content.tokens.len; i++) {
        VecAppend(out.tokens, InlineTokenSpanDup(content.tokens[i]));
    }
    return out;
}

void InputContentFree(InputContent* content) {
    if (!content) {
        return;
    }
    StrFree(content->text);
    InlineTokenSpansClear(&content->tokens);
}

InputContent InputGetContent(const InputState* s) {
    InputContent out;
    if (!s) {
        return out;
    }
    out.text = StrDup(InputValue(s));
    const Vec<InlineTokenSpan>* spans = InputTokens(s);
    if (spans) {
        for (int i = 0; i < spans->len; i++) {
            VecAppend(out.tokens, InlineTokenSpanDup((*spans)[i]));
        }
    }
    return out;
}

InlineTokenError InputContent::WithToken(int start, int end,
                                         InlineToken token) {
    InlineTokenError err = token.Validate();
    if (err != InlineTokenError::Ok) {
        return err;
    }
    if (start < 0 || end < start || end > len(text)) {
        return InlineTokenError::InvalidRange;
    }
    if (start == end) {
        return InlineTokenError::InvalidRange;
    }
    if (!IsCharBoundary(text, start) || !IsCharBoundary(text, end)) {
        return InlineTokenError::InvalidBoundary;
    }
    Str slice(text.s + start, end - start);
    if (!StrEq(slice, token.text)) {
        return InlineTokenError::TextMismatch;
    }
    int ix = 0;
    while (ix < tokens.len && tokens[ix].end <= start) {
        ix++;
    }
    if (ix < tokens.len && tokens[ix].start < end) {
        return InlineTokenError::OverlappingTokens;
    }
    InlineTokenSpan span;
    span.start = start;
    span.end = end;
    span.token = token;
    VecInsertAt(tokens, ix, span);
    return InlineTokenError::Ok;
}

InlineToken InlineTokenDup(InlineToken t) {
    InlineToken out;
    out.id = StrDup(t.id);
    out.text = StrDup(t.text);
    out.label = StrDup(t.label);
    return out;
}

void InlineTokenFree(InlineToken* t) {
    if (!t) {
        return;
    }
    StrFree(t->id);
    StrFree(t->text);
    StrFree(t->label);
    *t = {};
}

InlineTokenSpan InlineTokenSpanDup(InlineTokenSpan s) {
    InlineTokenSpan out;
    out.start = s.start;
    out.end = s.end;
    out.token = InlineTokenDup(s.token);
    return out;
}

void InlineTokenSpanFree(InlineTokenSpan* s) {
    if (!s) {
        return;
    }
    InlineTokenFree(&s->token);
    *s = {};
}

bool InlineTokenEq(InlineToken a, InlineToken b) {
    return StrEq(a.id, b.id) && StrEq(a.text, b.text) &&
           StrEq(a.label, b.label);
}

void InlineTokenSpansClear(Vec<InlineTokenSpan>* spans) {
    if (!spans) {
        return;
    }
    for (int i = 0; i < spans->len; i++) {
        InlineTokenSpanFree(&(*spans)[i]);
    }
    VecReset(*spans);
}

void TokenDeltaFree(TokenDelta* d) {
    if (!d) {
        return;
    }
    InlineTokenSpansClear(&d->removed);
    InlineTokenSpansClear(&d->inserted);
    delete d;
}

TokenDelta* TokenDeltaDup(const TokenDelta* d) {
    if (!d) {
        return nullptr;
    }
    TokenDelta* out = new TokenDelta();
    for (int i = 0; i < d->removed.len; i++) {
        VecAppend(out->removed, InlineTokenSpanDup(d->removed[i]));
    }
    for (int i = 0; i < d->inserted.len; i++) {
        VecAppend(out->inserted, InlineTokenSpanDup(d->inserted[i]));
    }
    return out;
}

static InlineTokenSpan ShiftedSpan(InlineTokenSpan s, int delta) {
    s.start += delta;
    s.end += delta;
    return s;
}

TokenDelta* TokenStoreReplace(Vec<InlineTokenSpan>* spans, int start, int end,
                              int newLen, const InlineTokenSpan* inserted,
                              int nInserted) {
    if (!spans) {
        return nullptr;
    }
    if (end < start) {
        end = start;
    }
    int shift = newLen - (end - start);
    TokenDelta* delta = new TokenDelta();
    Vec<InlineTokenSpan> kept;
    for (int i = 0; i < spans->len; i++) {
        InlineTokenSpan span = (*spans)[i];
        if (span.start < end && start < span.end) {
            VecAppend(delta->removed, ShiftedSpan(span, -start));
        } else {
            if (span.start >= end) {
                span.start += shift;
                span.end += shift;
            }
            VecAppend(kept, span);
        }
    }
    VecReset(*spans);
    *spans = kept;
    for (int i = 0; i < nInserted; i++) {
        InlineTokenSpan span = InlineTokenSpanDup(inserted[i]);
        span.start += start;
        span.end += start;
        VecAppend(*spans, span);
        VecAppend(delta->inserted, InlineTokenSpanDup(inserted[i]));
    }
    if (nInserted > 0 && spans->len > 1) {
        for (int i = 1; i < spans->len; i++) {
            InlineTokenSpan key = (*spans)[i];
            int j = i;
            while (j > 0 && (*spans)[j - 1].start > key.start) {
                (*spans)[j] = (*spans)[j - 1];
                j--;
            }
            (*spans)[j] = key;
        }
    }
    if (delta->removed.len == 0 && delta->inserted.len == 0) {
        TokenDeltaFree(delta);
        return nullptr;
    }
    return delta;
}

int TokenBoundary(const Vec<InlineTokenSpan>& spans, int offset, Bias bias) {
    int lo = 0;
    int hi = spans.len;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (spans[mid].end <= offset) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo < spans.len && spans[lo].start < offset) {
        return bias == Bias::Left ? spans[lo].start : spans[lo].end;
    }
    return offset;
}

void NormalizeTokenRange(const Vec<InlineTokenSpan>& spans, int* start,
                         int* end) {
    if (!start || !end) {
        return;
    }
    if (*start == *end) {
        int off = TokenBoundary(spans, *start, Bias::Right);
        *start = off;
        *end = off;
        return;
    }
    *start = TokenBoundary(spans, *start, Bias::Left);
    *end = TokenBoundary(spans, *end, Bias::Right);
}

void InlineTokenStoreFree(InlineTokenStore* store) {
    if (!store) {
        return;
    }
    InlineTokenSpansClear(&store->spans);
    if (store->hasPending) {
        InlineTokenFree(&store->pending);
    }
    if (store->hasHovered) {
        InlineTokenSpanFree(&store->hovered.span);
    }
    for (int i = 0; i < len(store->pendingHoverExits); i++) {
        InlineTokenSpanFree(&store->pendingHoverExits[i].span);
    }
    free(store->placed);
    delete store;
}

InlineTokenStore* InputTokenStore(InputState* s, bool create) {
    if (!s) {
        return nullptr;
    }
    if (!s->tokens && create) {
        s->tokens = new InlineTokenStore();
    }
    return s->tokens;
}

const InlineTokenStore* InputTokenStore(const InputState* s) {
    return s ? s->tokens : nullptr;
}

bool InputTokensVisible(const InputState* s) {
    const InlineTokenStore* store = InputTokenStore(s);
    if (!store || store->spans.len == 0) {
        return false;
    }
    return !s->masked && !store->secret && MaskIsNone(s->maskPattern);
}

const Vec<InlineTokenSpan>* InputTokens(const InputState* s) {
    const InlineTokenStore* store = InputTokenStore(s);
    return store ? &store->spans : nullptr;
}

int InputTokenBoundary(const InputState* s, int offset, Bias bias) {
    const Vec<InlineTokenSpan>* spans = InputTokens(s);
    if (!spans || spans->len == 0) {
        return offset;
    }
    return TokenBoundary(*spans, offset, bias);
}

void InputNormalizeTokenRange(const InputState* s, int* start, int* end) {
    const InlineTokenStore* store = InputTokenStore(s);
    if (!store || store->replaying || !start || !end) {
        return;
    }
    NormalizeTokenRange(store->spans, start, end);
}

void InputSetTokenHoverPresentation(InputState* s,
                                    InlineTokenHoverListener hover,
                                    void* hoverUser) {
    // Nothing to remember for a field that never had tokens or a listener.
    InlineTokenStore* store = InputTokenStore(s, hover != nullptr);
    if (!store) {
        return;
    }
    store->hover = hover;
    store->hoverUser = hover ? hoverUser : nullptr;
}

void InputTokenBoundsClear(InputState* s) {
    InlineTokenStore* store = InputTokenStore(s, false);
    if (!store) {
        return;
    }
    store->nPlaced = 0;
    // Room for every token, so no slot moves while this frame's elements
    // hold pointers into the array.
    int want = len(store->spans);
    if (want > store->capPlaced) {
        InlineTokenPlaced* grown = (InlineTokenPlaced*)realloc(
            store->placed, (size_t)want * sizeof(InlineTokenPlaced));
        if (grown) {
            store->placed = grown;
            store->capPlaced = want;
        }
    }
}

Bounds* InputTokenBoundsSlot(InputState* s, int start) {
    InlineTokenStore* store = InputTokenStore(s, false);
    if (!store) {
        return nullptr;
    }
    for (int i = 0; i < store->nPlaced; i++) {
        if (store->placed[i].start == start) {
            return &store->placed[i].bounds;
        }
    }
    if (store->nPlaced >= store->capPlaced) {
        return nullptr;
    }
    InlineTokenPlaced* slot = &store->placed[store->nPlaced++];
    slot->start = start;
    slot->bounds = Bounds{};
    return &slot->bounds;
}

bool InputTokenBoundsGet(const InputState* s, int start, Bounds* out) {
    const InlineTokenStore* store = InputTokenStore(s);
    for (int i = 0; store && i < store->nPlaced; i++) {
        if (store->placed[i].start == start) {
            if (out) {
                *out = store->placed[i].bounds;
            }
            return true;
        }
    }
    return false;
}

static bool SpanEq(const InlineTokenSpan& a, const InlineTokenSpan& b) {
    return a.start == b.start && a.end == b.end &&
           InlineTokenEq(a.token, b.token);
}

static bool SnapshotMatches(const InlineTokenHoverSnapshot& snapshot, int start,
                            const InlineToken* expected) {
    return snapshot.span.start == start &&
           (!expected || InlineTokenEq(snapshot.span.token, *expected));
}

// The event a snapshot becomes; its strings move to the temp arena so the
// snapshot itself can be freed before the listener runs.
static InlineTokenHoverEvent HoverEventOf(
    const InlineTokenHoverSnapshot& snapshot, bool hovered) {
    Arena* tmp = GetTempArena();
    InlineTokenHoverEvent ev;
    ev.span.start = snapshot.span.start;
    ev.span.end = snapshot.span.end;
    ev.span.token.id = StrDup(tmp, snapshot.span.token.id);
    ev.span.token.text = StrDup(tmp, snapshot.span.token.text);
    ev.span.token.label = StrDup(tmp, snapshot.span.token.label);
    ev.bounds = snapshot.bounds;
    ev.hovered = hovered;
    ev.rangeUtf16Start = snapshot.rangeUtf16Start;
    ev.rangeUtf16End = snapshot.rangeUtf16End;
    return ev;
}

bool InputTokenHover(InputState* s, int start, Bounds bounds, bool hovered,
                     const InlineToken* expected, InlineTokenHoverEvent* out) {
    InlineTokenStore* store = InputTokenStore(s, false);
    if (!store || !store->hover) {
        return false;
    }
    if (hovered) {
        if (s->disabled || !InputTokensVisible(s)) {
            return false;
        }
        const InlineTokenSpan* found = nullptr;
        for (int i = 0; i < len(store->spans); i++) {
            const InlineTokenSpan& span = store->spans[i];
            if (span.start == start &&
                (!expected || InlineTokenEq(span.token, *expected))) {
                found = &span;
                break;
            }
        }
        if (!found) {
            return false;
        }
        if (store->hasHovered && SpanEq(store->hovered.span, *found)) {
            return false;
        }
        InlineTokenHoverSnapshot snapshot;
        snapshot.span = InlineTokenSpanDup(*found);
        snapshot.bounds = bounds;
        Str text = InputValue(s);
        snapshot.rangeUtf16Start = RopeOffsetToOffsetUtf16(text, found->start);
        snapshot.rangeUtf16End = RopeOffsetToOffsetUtf16(text, found->end);
        if (store->hasHovered) {
            VecAppend(store->pendingHoverExits, store->hovered);
        }
        store->hovered = snapshot;
        store->hasHovered = true;
        *out = HoverEventOf(snapshot, true);
        return true;
    }
    if (store->hasHovered && SnapshotMatches(store->hovered, start, expected)) {
        *out = HoverEventOf(store->hovered, false);
        InlineTokenSpanFree(&store->hovered.span);
        store->hovered = {};
        store->hasHovered = false;
        return true;
    }
    for (int i = 0; i < len(store->pendingHoverExits); i++) {
        if (SnapshotMatches(store->pendingHoverExits[i], start, expected)) {
            *out = HoverEventOf(store->pendingHoverExits[i], false);
            InlineTokenSpanFree(&store->pendingHoverExits[i].span);
            VecRemoveAt(store->pendingHoverExits, i);
            return true;
        }
    }
    return false;
}

bool InputReconcileTokenHover(InputState* s, InlineTokenHoverEvent* out) {
    InlineTokenStore* store = InputTokenStore(s, false);
    if (!store) {
        return false;
    }
    auto isPlaced = [&](const InlineTokenHoverSnapshot& snapshot) {
        if (s->disabled || !InputTokensVisible(s) || !store->hover ||
            !InputTokenBoundsGet(s, snapshot.span.start, nullptr)) {
            return false;
        }
        for (int i = 0; i < len(store->spans); i++) {
            if (SpanEq(store->spans[i], snapshot.span)) {
                return true;
            }
        }
        return false;
    };
    InlineTokenHoverSnapshot snapshot;
    bool found = false;
    for (int i = 0; i < len(store->pendingHoverExits); i++) {
        if (!isPlaced(store->pendingHoverExits[i])) {
            snapshot = store->pendingHoverExits[i];
            VecRemoveAt(store->pendingHoverExits, i);
            found = true;
            break;
        }
    }
    if (!found && store->hasHovered && !isPlaced(store->hovered)) {
        snapshot = store->hovered;
        store->hovered = {};
        store->hasHovered = false;
        // The pointer may still be over the element that was hovered; make
        // what is built there next a new element to hover tracking.
        store->hoverEpoch++;
        found = true;
    }
    if (!found) {
        return false;
    }
    bool listening = store->hover != nullptr;
    if (listening) {
        *out = HoverEventOf(snapshot, false);
    }
    InlineTokenSpanFree(&snapshot.span);
    return listening;
}

void InputSetTokenPresentation(InputState* s, InlineTokenRenderer renderer,
                               void* rendererUser,
                               InlineTokenClickListener click, void* clickUser,
                               bool secret) {
    InlineTokenStore* store = InputTokenStore(s, true);
    if (!store) {
        return;
    }
    store->renderer = renderer;
    store->rendererUser = rendererUser;
    store->click = click;
    store->clickUser = clickUser;
    store->secret = secret;
}

} // namespace gpui
