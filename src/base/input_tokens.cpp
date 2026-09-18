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

} // namespace gpui
