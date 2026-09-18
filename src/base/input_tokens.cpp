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

static bool IsCharBoundary(Str text, int off) {
    if (off < 0 || off > len(text)) {
        return false;
    }
    if (off == 0 || off == len(text)) {
        return true;
    }
    return Utf8ClipLeft(text, off) == off;
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
